#pragma once

#include "can_data.hpp"

#include <atomic>
#include <cstdint>
#include <future>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <boost/lockfree/spsc_queue.hpp>
#include <nlohmann/json.hpp>

#include "sqlite_modern_cpp.h"

class Recorder {
public:
  Recorder(const std::string &db_path, bool console_output, int64_t rotate_interval_ms = 24LL * 3600 * 1000,
           size_t rotate_max_files = 30);
  ~Recorder();

  void pushFrame(std::shared_ptr<const raw_frame_s> frame);
  void setJ1939Db(sqlite::database *db);
  void flushAndClose();

private:
  struct spn_entry_s {
    int64_t spn = 0;
    std::string name;
    std::string unit;
    nlohmann::json value;
  };

  struct frame_entry_s {
    int64_t ts_ms = 0;
    std::string iface;
    std::string canid;
    std::optional<int64_t> pgn;
    std::vector<spn_entry_s> spns;
  };

  struct canid_key_s {
    int64_t iface_id;
    std::string canid;
    bool operator==(const canid_key_s &o) const noexcept { return iface_id == o.iface_id && canid == o.canid; }
  };

  struct canid_hash_s {
    std::size_t operator()(const canid_key_s &k) const noexcept {
      return std::hash<int64_t>{}(k.iface_id) ^ (std::hash<std::string>{}(k.canid) << 1);
    }
  };

  struct pair_hash_s {
    std::size_t operator()(const std::pair<int64_t, int64_t> &p) const noexcept {
      return std::hash<int64_t>{}(p.first) ^ (std::hash<int64_t>{}(p.second) << 1);
    }
  };

  // Current value run for (canid_id, spn): ts_first of the row + value.
  struct spn_run_s {
    int64_t ts_first = 0;
    nlohmann::json value;
  };

  static int64_t epoch_ms();

  void initSchema();
  void decoderLoop(std::stop_token st);
  void flushPending();
  void decodeOne(const raw_frame_s &raw);
  int64_t getOrCreateIfaceId(const std::string &name);
  int64_t getOrCreateCanidId(int64_t iface_id, const std::string &canid, std::optional<int64_t> pgn, int64_t ts_ms);
  void maybeRotate();
  void rotate();
  void enforceRetention();

  static constexpr size_t kQueueCapacity = 1u << 20; // 1M frames (~16 MB of shared_ptrs)
  static constexpr int64_t kFlushIntervalMs = 1000;
  static constexpr int kSchemaVersion = 3;

  std::string m_db_path_;
  bool m_console_output_ = false;

  int64_t m_rotate_interval_ms_ = 0;
  size_t m_rotate_max_files_ = 0;

  boost::lockfree::spsc_queue<std::shared_ptr<const raw_frame_s>> m_queue_;
  std::atomic<uint64_t> m_dropped_{0};

  std::atomic<sqlite::database *> m_j1939_db_{nullptr};

  std::vector<frame_entry_s> m_pending_;
  int64_t m_last_flush_ms_ = 0;
  int64_t m_file_opened_at_ms_ = 0;

  std::unique_ptr<sqlite::database> m_disk_db_;

  // In-memory caches that mirror DB state — avoid SELECTs in the hot path.
  std::unordered_map<std::string, int64_t> m_iface_ids_;
  std::unordered_map<canid_key_s, int64_t, canid_hash_s> m_canid_ids_;
  std::unordered_set<std::pair<int64_t, int64_t>, pair_hash_s> m_spn_meta_seen_;
  std::unordered_map<std::pair<int64_t, int64_t>, spn_run_s, pair_hash_s> m_spn_last_;

  std::stop_source m_stop_;
  std::future<void> m_thread_;
};
