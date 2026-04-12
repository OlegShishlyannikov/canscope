#pragma once

#include "can_data.hpp"

#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

struct FrameRecord {
  int64_t ts_ms;
  std::string iface;
  std::string canid;
  std::shared_ptr<nlohmann::json> verbose;
};

class Recorder {
public:
  Recorder(const std::string &db_path, bool console_output);
  ~Recorder();

  void onBatch(const std::vector<can_frame_update_s> &batch);
  void flushAndClose();

private:
  static int64_t epoch_ms();
  static size_t get_rss_mb();
  static std::vector<uint8_t> gzip_compress(const std::string &src);

  void compress_batch();
  void background_flush_task(std::stop_token st);

  std::string disk_db_path_;
  std::mutex batch_mtx_;
  std::vector<FrameRecord> pending_;
  int64_t batch_ts_start_ = 0;
  std::stop_source flush_stop_;
  std::future<void> flush_task_;
  bool console_output_ = false;
};
