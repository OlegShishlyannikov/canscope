#include "recorder.hpp"
#include "sqlite_modern_cpp.h"

#include <zlib.h>
#include <sys/resource.h>

#define FMT_HEADER_ONLY
#include <fmt/format.h>

#include <spdlog/sinks/systemd_sink.h>

Recorder::Recorder(const std::string &db_path, bool console_output)
    : disk_db_path_(db_path), console_output_(console_output) {
  log_ = spdlog::systemd_logger_mt("recorder", "cansniffer-rec");
  log_->set_level(spdlog::level::info);

  flush_task_ = std::async(std::launch::async, [this](std::stop_token st) { background_flush_task(st); }, flush_stop_.get_token());
  log_->info("recorder initialized, db_path={}", db_path);
  if (console_output_) fmt::println("Recording to: {}", db_path);
}

Recorder::~Recorder() {
  flushAndClose();
}

void Recorder::onBatch(const std::vector<can_frame_update_s> &batch) {
  int64_t now = epoch_ms();
  std::lock_guard<std::mutex> lock(batch_mtx_);
  if (batch_ts_start_ == 0) batch_ts_start_ = now;
  for (const auto &u : batch) {
    if (u.verbose && !u.verbose->is_null() && u.verbose->contains("SPNs")) {
      pending_.push_back({now, u.iface, u.canid, u.verbose});
    }
  }
}

void Recorder::flushAndClose() {
  flush_stop_.request_stop();
  if (flush_task_.valid()) flush_task_.wait();

  std::lock_guard<std::mutex> lock(batch_mtx_);
  if (!pending_.empty()) {
    log_->info("flushing remaining {} frames on exit", pending_.size());
    compress_batch();
  }
}

int64_t Recorder::epoch_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

size_t Recorder::get_rss_mb() {
  struct rusage ru {};
  getrusage(RUSAGE_SELF, &ru);
  return static_cast<size_t>(ru.ru_maxrss) / 1024;
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

void Recorder::compress_batch() {
  if (pending_.empty()) return;

  nlohmann::json::array_t arr;
  arr.reserve(pending_.size());

  int64_t ts_start = pending_.front().ts_ms;
  int64_t ts_end = pending_.back().ts_ms;

  for (const auto &rec : pending_) {
    nlohmann::json entry;
    entry["ts"] = rec.ts_ms;
    entry["canid"] = rec.canid;

    if (rec.verbose && !rec.verbose->is_null()) {
      const auto &v = *rec.verbose;
      if (v.contains("PGN")) entry["pgn"] = v["PGN"];

      if (v.contains("SPNs")) {
        nlohmann::json::array_t spns;
        for (const auto &spn : v["SPNs"]) {
          nlohmann::json s;
          if (spn.contains("SPN (integer)")) s["spn"] = spn["SPN (integer)"];
          if (spn.contains("SPN name")) s["name"] = spn["SPN name"];
          if (spn.contains("Value")) s["value"] = spn["Value"];
          if (spn.contains("Unit")) s["unit"] = spn["Unit"];
          spns.push_back(std::move(s));
        }
        entry["spns"] = std::move(spns);
      }
    }

    arr.push_back(std::move(entry));
  }

  std::string json_str = nlohmann::json(arr).dump();
  auto compressed = gzip_compress(json_str);

  try {
    sqlite::database disk_db(disk_db_path_);
    disk_db << "PRAGMA journal_mode = WAL;";
    disk_db << R"(
      CREATE TABLE IF NOT EXISTS batches (
        id          INTEGER PRIMARY KEY AUTOINCREMENT,
        ts_start    INTEGER NOT NULL,
        ts_end      INTEGER NOT NULL,
        frame_count INTEGER NOT NULL,
        data        BLOB    NOT NULL
      );
    )";
    disk_db << R"(CREATE INDEX IF NOT EXISTS idx_batches_ts ON batches(ts_start);)";

    disk_db << "INSERT INTO batches (ts_start, ts_end, frame_count, data) VALUES (?, ?, ?, ?);"
            << ts_start << ts_end << static_cast<int64_t>(pending_.size()) << compressed;

    auto msg = fmt::format("Flushed batch: {} frames, {:.1f}KB -> {:.1f}KB gzip ({:.0f}% compression)",
                           pending_.size(),
                           static_cast<double>(json_str.size()) / 1024.0,
                           static_cast<double>(compressed.size()) / 1024.0,
                           (1.0 - static_cast<double>(compressed.size()) / static_cast<double>(json_str.size())) * 100.0);
    log_->info("{}", msg);
    if (console_output_) fmt::println("{}", msg);
  } catch (const sqlite::sqlite_exception &e) {
    log_->error("disk DB write failed: {}", e.what());
  }

  pending_.clear();
  batch_ts_start_ = 0;
}

void Recorder::background_flush_task(std::stop_token st) {
  while (!st.stop_requested()) {
    std::this_thread::sleep_for(std::chrono::seconds(10));

    std::lock_guard<std::mutex> lock(batch_mtx_);
    if (pending_.empty()) continue;

    int64_t now = epoch_ms();
    bool time_trigger = (now - batch_ts_start_) >= 60'000;
    bool mem_trigger = get_rss_mb() >= 500;

    if (time_trigger || mem_trigger) {
      if (mem_trigger) log_->warn("RSS >= 500MB, forcing flush");
      compress_batch();
    }
  }
}
