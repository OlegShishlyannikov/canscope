#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct parsed_candump_s {
  enum class kind_e { invalid, data, error_frame, remote_frame };

  kind_e kind = kind_e::invalid;

  // Fields below are valid only when kind == data.
  int64_t ts_ms = 0;
  std::string iface;
  std::string canid;
  std::vector<uint8_t> payload;
};

// Pure parser for a single candump line. No globals, no side effects.
//
// Accepts lines with an optional timestamp prefix:
//   -t a : "(1705327800.123456) ..."    absolute unix seconds
//   -t A : "(2024-01-15 14:30:00.123)"  wall-clock in local time
//   (none or relative)                  -> ts_ms falls back to system_clock
//
// Returns kind == data on success, with ts_ms/iface/canid/payload filled.
// Returns kind == error_frame for SocketCAN ERRORFRAME markers.
// Returns kind == remote_frame for RTR frames ("remote request" in place of payload).
// Returns kind == invalid for empty/malformed lines.
parsed_candump_s parseCandumpLine(const std::string &line);
