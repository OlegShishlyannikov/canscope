#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

// Mutex protecting the J1939 SQLite database from concurrent access
extern std::mutex g_j1939_db_mtx;

// Count of CAN error frames seen on the bus (SocketCAN ERRORFRAME markers)
extern std::atomic<uint64_t> g_error_frame_count;

struct can_frame_data_s {
  std::vector<uint8_t> payload;
  int32_t size = 0;
  bool is_error_frame = false;
  bool is_remote_request = false;
};

struct can_frame_diff_s {
  bool is_new_interface = false;
  bool is_new_canid = false;
  std::vector<bool> payload_changed;
};

struct can_frame_update_s {
  std::string iface;
  std::string canid;
  can_frame_data_s data;
  can_frame_diff_s diff;
  std::shared_ptr<nlohmann::json> verbose;
  std::shared_ptr<nlohmann::json> brief;
};

// Convert verbose processFrame JSON to export format for a single CAN ID
nlohmann::json verboseToExportJson(const nlohmann::json &verbose);
