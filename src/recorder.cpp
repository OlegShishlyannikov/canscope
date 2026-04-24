#include "recorder.hpp"

#include <chrono>
#include <thread>
#include <utility>

#include <zlib.h>

#define FMT_HEADER_ONLY
#include <fmt/format.h>

extern std::pair<nlohmann::json, nlohmann::json> processFrame(
    sqlite::database &db, const std::string &iface, const std::string &canid,
    const std::vector<uint8_t> &data);

Recorder::Recorder(const std::string &db_path, bool console_output)
    : m_db_path_(db_path), m_console_output_(console_output), m_queue_(kQueueCapacity) {
  m_disk_db_ = std::make_unique<sqlite::database>(m_db_path_);
  *m_disk_db_ << "PRAGMA journal_mode = WAL;";
  *m_disk_db_ << "PRAGMA synchronous = NORMAL;";
  *m_disk_db_ << R"(
    CREATE TABLE IF NOT EXISTS frames (
      id    INTEGER PRIMARY KEY AUTOINCREMENT,
      ts_ms INTEGER NOT NULL,
      iface TEXT    NOT NULL,
      canid TEXT    NOT NULL,
      pgn   INTEGER,
      spns  BLOB
    );
  )";
  *m_disk_db_ << "CREATE INDEX IF NOT EXISTS idx_frames_ts    ON frames(ts_ms);";
  *m_disk_db_ << "CREATE INDEX IF NOT EXISTS idx_frames_canid ON frames(canid, ts_ms);";
  *m_disk_db_ << "CREATE INDEX IF NOT EXISTS idx_frames_pgn   ON frames(pgn,   ts_ms) WHERE pgn IS NOT NULL;";

  m_last_flush_ms_ = epoch_ms();
  m_thread_ = std::async(std::launch::async, [this](std::stop_token st) { decoderLoop(st); }, m_stop_.get_token());

  if (m_console_output_) fmt::println("Recording to: {}", m_db_path_);
}

Recorder::~Recorder() {
  flushAndClose();
}

void Recorder::pushFrame(std::shared_ptr<const raw_frame_s> frame) {
  if (!m_queue_.push(std::move(frame))) {
    m_dropped_.fetch_add(1, std::memory_order_relaxed);
  }
}

void Recorder::setJ1939Db(sqlite::database *db) {
  m_j1939_db_.store(db, std::memory_order_release);
}

void Recorder::flushAndClose() {
  if (!m_thread_.valid()) return;

  m_stop_.request_stop();
  m_thread_.wait();
  m_thread_ = {};

  if (!m_pending_.empty()) {
    flushPending();
  }

  if (m_console_output_) {
    uint64_t dropped = m_dropped_.load(std::memory_order_relaxed);
    if (dropped > 0) {
      fmt::println("Recorder: dropped {} frames due to queue overflow", dropped);
    }
  }

  m_disk_db_.reset();
}

int64_t Recorder::epoch_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::vector<uint8_t> Recorder::gzip_compress(const std::string &src) {
  z_stream strm{};
  deflateInit2(&strm, Z_BEST_COMPRESSION, Z_DEFLATED, 15 | 16, 8, Z_DEFAULT_STRATEGY);

  strm.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(src.data()));
  strm.avail_in = static_cast<uInt>(src.size());

  std::vector<uint8_t> out;
  out.resize(deflateBound(&strm, static_cast<uLong>(src.size())));

  strm.next_out = out.data();
  strm.avail_out = static_cast<uInt>(out.size());

  deflate(&strm, Z_FINISH);
  out.resize(strm.total_out);
  deflateEnd(&strm);

  return out;
}

void Recorder::decodeOne(const raw_frame_s &raw) {
  frame_sample_s sample;
  sample.ts_ms = raw.ts_ms;
  sample.iface = raw.iface;
  sample.canid = raw.canid;

  auto *db = m_j1939_db_.load(std::memory_order_acquire);
  if (db) {
    std::lock_guard<std::mutex> lock(g_j1939_db_mtx);
    auto [verbose, brief] = processFrame(*db, raw.iface, raw.canid, raw.payload);

    if (!verbose.is_null()) {
      if (verbose.contains("PGN")) {
        const auto &pgn = verbose["PGN"];
        if (pgn.is_number_integer()) sample.pgn = pgn.get<int64_t>();
      }
      if (verbose.contains("SPNs") && verbose["SPNs"].is_array()) {
        for (const auto &spn : verbose["SPNs"]) {
          spn_sample_s s;
          s.ts_ms = raw.ts_ms;
          if (spn.contains("SPN (integer)") && spn["SPN (integer)"].is_number_integer()) {
            s.spn = spn["SPN (integer)"].get<int64_t>();
          }
          if (spn.contains("SPN name") && spn["SPN name"].is_string()) {
            s.name = spn["SPN name"].get<std::string>();
          }
          if (spn.contains("Value")) s.value = spn["Value"];
          if (spn.contains("Unit") && spn["Unit"].is_string()) {
            s.unit = spn["Unit"].get<std::string>();
          }
          sample.spns.push_back(std::move(s));
        }
      }
    }
  }

  m_pending_.push_back(std::move(sample));
}

void Recorder::flushPending() {
  if (m_pending_.empty()) return;

  try {
    *m_disk_db_ << "BEGIN;";
    for (const auto &f : m_pending_) {
      std::vector<uint8_t> blob;
      if (!f.spns.empty()) {
        nlohmann::json::array_t arr;
        arr.reserve(f.spns.size());
        for (const auto &s : f.spns) {
          nlohmann::json j;
          j["ts_ms"] = s.ts_ms;
          j["spn"] = s.spn;
          if (!s.name.empty()) j["name"] = s.name;
          if (!s.value.is_null()) j["value"] = s.value;
          if (!s.unit.empty()) j["unit"] = s.unit;
          arr.push_back(std::move(j));
        }
        blob = gzip_compress(nlohmann::json(arr).dump());
      }

      if (f.pgn.has_value() && !blob.empty()) {
        *m_disk_db_ << "INSERT INTO frames (ts_ms, iface, canid, pgn, spns) VALUES (?, ?, ?, ?, ?);"
                    << f.ts_ms << f.iface << f.canid << *f.pgn << blob;
      } else if (f.pgn.has_value()) {
        *m_disk_db_ << "INSERT INTO frames (ts_ms, iface, canid, pgn, spns) VALUES (?, ?, ?, ?, NULL);"
                    << f.ts_ms << f.iface << f.canid << *f.pgn;
      } else if (!blob.empty()) {
        *m_disk_db_ << "INSERT INTO frames (ts_ms, iface, canid, pgn, spns) VALUES (?, ?, ?, NULL, ?);"
                    << f.ts_ms << f.iface << f.canid << blob;
      } else {
        *m_disk_db_ << "INSERT INTO frames (ts_ms, iface, canid, pgn, spns) VALUES (?, ?, ?, NULL, NULL);"
                    << f.ts_ms << f.iface << f.canid;
      }
    }
    *m_disk_db_ << "COMMIT;";

    if (m_console_output_) {
      fmt::println("Flushed {} frames", m_pending_.size());
    }
  } catch (const sqlite::sqlite_exception &e) {
    try { *m_disk_db_ << "ROLLBACK;"; } catch (...) {}
    if (m_console_output_) {
      fmt::println("Recorder flush failed: {}", e.what());
    }
  }

  m_pending_.clear();
}

void Recorder::decoderLoop(std::stop_token st) {
  while (!st.stop_requested()) {
    std::shared_ptr<const raw_frame_s> frame;
    size_t drained = 0;
    while (drained < 1024 && m_queue_.pop(frame)) {
      decodeOne(*frame);
      ++drained;
    }

    int64_t now = epoch_ms();
    if (now - m_last_flush_ms_ >= kFlushIntervalMs && !m_pending_.empty()) {
      flushPending();
      m_last_flush_ms_ = now;
    }

    if (drained == 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }

  // Drain remaining frames on shutdown
  std::shared_ptr<const raw_frame_s> frame;
  while (m_queue_.pop(frame)) {
    decodeOne(*frame);
  }
}
