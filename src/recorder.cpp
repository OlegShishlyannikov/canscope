#include "recorder.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#define FMT_HEADER_ONLY
#include <fmt/format.h>

extern std::pair<nlohmann::json, nlohmann::json> processFrame(sqlite::database &db, const std::string &iface,
                                                              const std::string &canid,
                                                              const std::vector<uint8_t> &data);

namespace {
void bindJsonValue(sqlite::database_binder &stmt, const nlohmann::json &v) {
  if (v.is_null()) {
    stmt << nullptr;
  } else if (v.is_number_integer()) {
    stmt << v.get<int64_t>();
  } else if (v.is_number_unsigned()) {
    stmt << static_cast<int64_t>(v.get<uint64_t>());
  } else if (v.is_number_float()) {
    stmt << v.get<double>();
  } else if (v.is_string()) {
    stmt << v.get<std::string>();
  } else if (v.is_boolean()) {
    stmt << (v.get<bool>() ? 1 : 0);
  } else {
    stmt << v.dump();
  }
}
} // namespace

Recorder::Recorder(const std::string &db_path, bool console_output, int64_t rotate_interval_ms, size_t rotate_max_files)
    : m_db_path_(db_path), m_console_output_(console_output), m_rotate_interval_ms_(rotate_interval_ms),
      m_rotate_max_files_(rotate_max_files), m_queue_(kQueueCapacity) {
  m_disk_db_ = std::make_unique<sqlite::database>(m_db_path_);

  m_last_flush_ms_ = epoch_ms();
  m_file_opened_at_ms_ = m_last_flush_ms_; // default; initSchema may adjust if DB has data
  initSchema();

  m_thread_ = std::async(std::launch::async, [this](std::stop_token st) { decoderLoop(st); }, m_stop_.get_token());
  enforceRetention();

  if (m_console_output_) {
    fmt::println("Recording to: {}", m_db_path_);
  }
}

void Recorder::initSchema() {
  *m_disk_db_ << "PRAGMA journal_mode = WAL;";
  *m_disk_db_ << "PRAGMA synchronous = NORMAL;";

  int user_version = 0;
  *m_disk_db_ << "PRAGMA user_version;" >> user_version;
  if (user_version != 0 && user_version != kSchemaVersion) {
    throw std::runtime_error(fmt::format("Recorder: database '{}' has schema version {}, this build expects {}. "
                                         "Use a fresh file path (older formats are not supported anymore).",
                                         m_db_path_, user_version, kSchemaVersion));
  }

  *m_disk_db_ << R"(
        CREATE TABLE IF NOT EXISTS ifaces (
            id   INTEGER PRIMARY KEY,
            name TEXT UNIQUE NOT NULL
        );
    )";

  *m_disk_db_ << R"(
        CREATE TABLE IF NOT EXISTS canids (
            id          INTEGER PRIMARY KEY,
            iface_id    INTEGER NOT NULL,
            canid       TEXT    NOT NULL,
            pgn         INTEGER,
            first_seen  INTEGER NOT NULL,
            last_seen   INTEGER NOT NULL,
            frame_count INTEGER NOT NULL DEFAULT 0,
            UNIQUE (iface_id, canid)
        );
    )";

  *m_disk_db_ << R"(
        CREATE TABLE IF NOT EXISTS spn_meta (
            pgn  INTEGER NOT NULL,
            spn  INTEGER NOT NULL,
            name TEXT,
            unit TEXT,
            PRIMARY KEY (pgn, spn)
        ) WITHOUT ROWID;
    )";

  *m_disk_db_ << R"(
        CREATE TABLE IF NOT EXISTS spn_values (
            canid_id INTEGER NOT NULL,
            spn      INTEGER NOT NULL,
            ts_first INTEGER NOT NULL,
            ts_last  INTEGER NOT NULL,
            value    NUMERIC,
            PRIMARY KEY (canid_id, spn, ts_first)
        ) WITHOUT ROWID;
    )";

  *m_disk_db_ << "DROP VIEW IF EXISTS v_decoded;";
  *m_disk_db_ << "DROP VIEW IF EXISTS v_canids;";

  *m_disk_db_ << R"(
        CREATE VIEW v_canids AS
        SELECT c.id, i.name AS iface, c.canid, c.pgn,
               c.first_seen, c.last_seen, c.frame_count,
               CASE WHEN c.frame_count > 1
                    THEN (c.last_seen - c.first_seen) * 1.0 / (c.frame_count - 1)
                    ELSE NULL END AS period_ms
        FROM canids c
        JOIN ifaces i ON i.id = c.iface_id;
    )";

  *m_disk_db_ << R"(
        CREATE VIEW v_decoded AS
        SELECT i.name     AS iface,
               c.canid    AS canid,
               c.pgn      AS pgn,
               sv.spn     AS spn,
               sm.name    AS spn_name,
               sv.value   AS value,
               sm.unit    AS unit,
               sv.ts_first,
               sv.ts_last,
               c.id       AS canid_id,
               CASE WHEN c.frame_count > 1
                    THEN (c.last_seen - c.first_seen) * 1.0 / (c.frame_count - 1)
                    ELSE NULL END AS period_ms
        FROM spn_values sv
        JOIN canids c ON c.id = sv.canid_id
        JOIN ifaces i ON i.id = c.iface_id
        LEFT JOIN spn_meta sm ON sm.pgn = c.pgn AND sm.spn = sv.spn;
    )";

  *m_disk_db_ << fmt::format("PRAGMA user_version = {};", kSchemaVersion);

  *m_disk_db_ << "SELECT id, name FROM ifaces;" >> [this](int64_t id, std::string name) { m_iface_ids_[name] = id; };

  *m_disk_db_ << "SELECT id, iface_id, canid FROM canids;" >>
      [this](int64_t id, int64_t iface_id, std::string canid) { m_canid_ids_[canid_key_s{iface_id, canid}] = id; };

  *m_disk_db_ << "SELECT pgn, spn FROM spn_meta;" >>
      [this](int64_t pgn, int64_t spn) { m_spn_meta_seen_.emplace(pgn, spn); };

  int64_t earliest = 0;
  *m_disk_db_ << "SELECT COALESCE(MIN(first_seen), 0) FROM canids;" >> earliest;

  if (earliest > 0) {
    m_file_opened_at_ms_ = earliest;
  }
}

Recorder::~Recorder() { flushAndClose(); }

void Recorder::pushFrame(std::shared_ptr<const raw_frame_s> frame) {
  if (!m_queue_.push(std::move(frame))) {
    m_dropped_.fetch_add(1, std::memory_order_relaxed);
  }
}

void Recorder::setJ1939Db(sqlite::database *db) { m_j1939_db_.store(db, std::memory_order_release); }

void Recorder::flushAndClose() {
  if (!m_thread_.valid()) {
    return;
  }

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
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
      .count();
}

void Recorder::decodeOne(const raw_frame_s &raw) {
  frame_entry_s entry;
  entry.ts_ms = raw.ts_ms;
  entry.iface = raw.iface;
  entry.canid = raw.canid;

  auto *db = m_j1939_db_.load(std::memory_order_acquire);
  if (db) {
    std::lock_guard<std::mutex> lock(g_j1939_db_mtx);
    auto [verbose, brief] = processFrame(*db, raw.iface, raw.canid, raw.payload);

    if (!verbose.is_null()) {
      if (verbose.contains("PGN (integer)") && verbose["PGN (integer)"].is_number_integer()) {
        entry.pgn = verbose["PGN (integer)"].get<int64_t>();
      }

      if (verbose.contains("SPNs") && verbose["SPNs"].is_array()) {
        for (const auto &spn : verbose["SPNs"]) {
          spn_entry_s s;
          if (spn.contains("SPN (integer)") && spn["SPN (integer)"].is_number_integer()) {
            s.spn = spn["SPN (integer)"].get<int64_t>();
          }

          if (spn.contains("SPN name") && spn["SPN name"].is_string()) {
            s.name = spn["SPN name"].get<std::string>();
          }

          if (spn.contains("Unit") && spn["Unit"].is_string()) {
            s.unit = spn["Unit"].get<std::string>();
          }

          if (spn.contains("Value")) {
            s.value = spn["Value"];
          }

          entry.spns.push_back(std::move(s));
        }
      }
    }
  }

  m_pending_.push_back(std::move(entry));
}

int64_t Recorder::getOrCreateIfaceId(const std::string &name) {
  auto it = m_iface_ids_.find(name);
  if (it != m_iface_ids_.end()) {
    return it->second;
  }

  *m_disk_db_ << "INSERT OR IGNORE INTO ifaces(name) VALUES (?);" << name;
  int64_t id = 0;
  *m_disk_db_ << "SELECT id FROM ifaces WHERE name = ?;" << name >> id;
  m_iface_ids_[name] = id;
  return id;
}

int64_t Recorder::getOrCreateCanidId(int64_t iface_id, const std::string &canid, std::optional<int64_t> pgn,
                                     int64_t ts_ms) {
  canid_key_s key{iface_id, canid};
  auto it = m_canid_ids_.find(key);
  if (it != m_canid_ids_.end()) {
    // Existing CAN ID: bump last_seen + frame_count. COALESCE lets us fill
    // in the pgn once it becomes known (if the CAN ID was first seen before
    // the J1939 DA was ready).
    if (pgn.has_value()) {
      *m_disk_db_ << "UPDATE canids SET last_seen = ?, frame_count = frame_count + 1, "
                     "pgn = COALESCE(pgn, ?) WHERE id = ?;"
                  << ts_ms << *pgn << it->second;
    } else {
      *m_disk_db_ << "UPDATE canids SET last_seen = ?, frame_count = frame_count + 1 "
                     "WHERE id = ?;"
                  << ts_ms << it->second;
    }
    return it->second;
  }

  if (pgn.has_value()) {
    *m_disk_db_ << "INSERT INTO canids(iface_id, canid, pgn, first_seen, last_seen, frame_count) "
                   "VALUES (?, ?, ?, ?, ?, 1);"
                << iface_id << canid << *pgn << ts_ms << ts_ms;
  } else {
    *m_disk_db_ << "INSERT INTO canids(iface_id, canid, pgn, first_seen, last_seen, frame_count) "
                   "VALUES (?, ?, NULL, ?, ?, 1);"
                << iface_id << canid << ts_ms << ts_ms;
  }
  int64_t id = m_disk_db_->last_insert_rowid();
  m_canid_ids_[key] = id;
  return id;
}

void Recorder::flushPending() {
  if (m_pending_.empty()) {
    return;
  }

  try {
    *m_disk_db_ << "BEGIN;";

    for (const auto &f : m_pending_) {
      const int64_t iface_id = getOrCreateIfaceId(f.iface);
      const int64_t canid_id = getOrCreateCanidId(iface_id, f.canid, f.pgn, f.ts_ms);
      std::unordered_map<int64_t, int32_t> spn_occurrence_count;

      for (const auto &s : f.spns) {
        if (s.value.is_null()) {
          continue;
        }

        if (f.pgn.has_value()) {
          auto meta_key = std::make_pair(*f.pgn, s.spn);
          if (m_spn_meta_seen_.insert(meta_key).second) {
            if (!s.unit.empty()) {
              *m_disk_db_ << "INSERT OR IGNORE INTO spn_meta(pgn, spn, name, unit) VALUES (?, ?, ?, ?);" << *f.pgn
                          << s.spn << s.name << s.unit;
            } else {
              *m_disk_db_ << "INSERT OR IGNORE INTO spn_meta(pgn, spn, name, unit) VALUES (?, ?, ?, NULL);" << *f.pgn
                          << s.spn << s.name;
            }
          }
        }

        const int32_t occ = spn_occurrence_count[s.spn]++;

        if (occ > 0) {
          const int64_t shifted_ts = f.ts_ms + occ;
          auto stmt = *m_disk_db_ << "INSERT OR IGNORE INTO spn_values"
                                     "(canid_id, spn, ts_first, ts_last, value) "
                                     "VALUES (?, ?, ?, ?, ?);";
          stmt << canid_id << s.spn << shifted_ts << shifted_ts;
          bindJsonValue(stmt, s.value);
          continue;
        }

        auto run_key = std::make_pair(canid_id, s.spn);
        auto run_it = m_spn_last_.find(run_key);

        const bool value_unchanged = run_it != m_spn_last_.end() && run_it->second.value == s.value;

        if (value_unchanged) {
          *m_disk_db_ << "UPDATE spn_values SET ts_last = ? "
                         "WHERE canid_id = ? AND spn = ? AND ts_first = ?;"
                      << f.ts_ms << canid_id << s.spn << run_it->second.ts_first;
          run_it->second.ts_first = run_it->second.ts_first;
        } else {
          auto stmt = *m_disk_db_ << "INSERT OR IGNORE INTO spn_values"
                                     "(canid_id, spn, ts_first, ts_last, value) "
                                     "VALUES (?, ?, ?, ?, ?);";
          stmt << canid_id << s.spn << f.ts_ms << f.ts_ms;
          bindJsonValue(stmt, s.value);

          m_spn_last_[run_key] = spn_run_s{
              f.ts_ms,
              s.value,
          };
        }
      }
    }

    *m_disk_db_ << "COMMIT;";
  } catch (const sqlite::sqlite_exception &e) {
    try {
      *m_disk_db_ << "ROLLBACK;";
    } catch (...) {
    }

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

    const int64_t now = epoch_ms();
    if (now - m_last_flush_ms_ >= kFlushIntervalMs && !m_pending_.empty()) {
      flushPending();
      m_last_flush_ms_ = now;
    }

    maybeRotate();

    if (drained == 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }

  // Drain remaining frames on shutdown.
  std::shared_ptr<const raw_frame_s> frame;
  while (m_queue_.pop(frame)) {
    decodeOne(*frame);
  }
}

void Recorder::maybeRotate() {
  if (m_rotate_interval_ms_ <= 0) {
    return; // rotation disabled
  }

  if (epoch_ms() - m_file_opened_at_ms_ < m_rotate_interval_ms_) {
    return;
  }

  rotate();
}

void Recorder::rotate() {
  namespace fs = std::filesystem;
  if (!m_pending_.empty()) {
    flushPending();
  }

  try {
    *m_disk_db_ << "PRAGMA wal_checkpoint(TRUNCATE);";
  } catch (...) {
  }

  m_disk_db_.reset();

  fs::path orig(m_db_path_);
  fs::path dir = orig.parent_path();

  if (dir.empty()) {
    dir = ".";
  }

  const std::string stem = orig.stem().string();
  const std::string ext = orig.extension().string();

  char stamp[32];
  const std::time_t opened_t = m_file_opened_at_ms_ / 1000;
  std::tm tm{};
  localtime_r(&opened_t, &tm);
  std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tm);

  const fs::path rotated_db = dir / fmt::format("{}-{}{}", stem, stamp, ext);
  std::error_code ec;
  fs::rename(orig, rotated_db, ec);

  if (ec) {
    if (m_console_output_) {
      fmt::println("Recorder: rotate rename failed: {} ({})", ec.message(), orig.string());
    }
  }

  for (const char *suffix : {"-wal", "-shm"}) {
    fs::remove(fs::path(m_db_path_ + suffix), ec);
  }

  if (!ec) {
    const std::string cmd = fmt::format("gzip -f {} &", rotated_db.string());
    std::system(cmd.c_str());
  }

  m_iface_ids_.clear();
  m_canid_ids_.clear();
  m_spn_meta_seen_.clear();
  m_spn_last_.clear();

  m_disk_db_ = std::make_unique<sqlite::database>(m_db_path_);
  initSchema();
  m_file_opened_at_ms_ = epoch_ms();
  enforceRetention();

  if (m_console_output_) {
    fmt::println("Recorder: rotated -> {}.gz", rotated_db.string());
  }
}

void Recorder::enforceRetention() {
  namespace fs = std::filesystem;

  fs::path orig(m_db_path_);
  fs::path dir = orig.parent_path();
  if (dir.empty()) {
    dir = ".";
  }

  const std::string stem = orig.stem().string();
  const std::string ext = orig.extension().string();
  const std::string prefix = stem + "-";
  const std::string suffix = ext + ".gz";

  std::vector<fs::path> rotations;
  std::error_code ec;
  for (const auto &entry : fs::directory_iterator(dir, ec)) {
    if (!entry.is_regular_file()) {
      continue;
    }

    const std::string name = entry.path().filename().string();
    if (name.size() <= prefix.size() + suffix.size()) {
      continue;
    }

    if (name.compare(0, prefix.size(), prefix) != 0) {
      continue;
    }

    if (name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0) {
      continue;
    }

    rotations.push_back(entry.path());
  }

  if (m_rotate_max_files_ == 0 || rotations.size() <= m_rotate_max_files_) {
    return; // unlimited retention or below limit
  }

  std::sort(rotations.begin(), rotations.end());
  const size_t to_delete = rotations.size() - m_rotate_max_files_;
  for (size_t i = 0; i < to_delete; ++i) {
    fs::remove(rotations[i], ec);

    if (!ec && m_console_output_) {
      fmt::println("Recorder: retention pruned {}", rotations[i].string());
    }
  }
}
