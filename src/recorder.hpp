#pragma once

#include "can_data.hpp"

#include <atomic>
#include <cstdint>
#include <future>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

#include <boost/lockfree/spsc_queue.hpp>
#include <nlohmann/json.hpp>

#include "sqlite_modern_cpp.h"

class Recorder {
public:
  Recorder(const std::string &db_path, bool console_output);
  ~Recorder();

  void pushFrame(std::shared_ptr<const raw_frame_s> frame);
  void setJ1939Db(sqlite::database *db);
  void flushAndClose();

private:
  struct spn_sample_s {
    int64_t ts_ms = 0;
    int64_t spn = 0;
    std::string name;
    nlohmann::json value;
    std::string unit;
  };

  struct frame_sample_s {
    int64_t ts_ms = 0;
    std::string iface;
    std::string canid;
    std::optional<int64_t> pgn;
    std::vector<spn_sample_s> spns;
  };

  static int64_t epoch_ms();
  static std::vector<uint8_t> gzip_compress(const std::string &src);

  void decoderLoop(std::stop_token st);
  void flushPending();
  void decodeOne(const raw_frame_s &raw);

  static constexpr size_t kQueueCapacity = 1u << 20; // 1M frames (~16 MB of shared_ptrs)
  static constexpr int64_t kFlushIntervalMs = 1000;

  std::string m_db_path_;
  bool m_console_output_ = false;

  boost::lockfree::spsc_queue<std::shared_ptr<const raw_frame_s>> m_queue_;
  std::atomic<uint64_t> m_dropped_{0};

  std::atomic<sqlite::database *> m_j1939_db_{nullptr};

  std::vector<frame_sample_s> m_pending_;
  int64_t m_last_flush_ms_ = 0;

  std::unique_ptr<sqlite::database> m_disk_db_;

  std::stop_source m_stop_;
  std::future<void> m_thread_;
};
