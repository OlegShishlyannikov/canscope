#pragma once

#include <stop_token>
#include <string>
#include <vector>

// Reads CAN frames directly from SocketCAN (Linux only) and emits them via the
// "raw_frame" signal. Drop-in alternative to the candump-text source pipeline:
// no subprocess, no text parsing, kernel timestamps via SO_TIMESTAMP.
//
// Construction:
//   ifaces   — comma-separated SocketCAN interface names ("can0,can1" or "any").
//              "any" is a SocketCAN pseudo-interface bound to a single socket
//              that receives frames from every CAN interface (same as
//              `candump any`). If "any" appears anywhere in the list, the
//              whole list collapses to ["any"] since it already covers all.
//   console_output — print recoverable errors (e.g. dropped malformed frames).
//
// Throws std::runtime_error on:
//   - non-Linux build (stub),
//   - failed socket()/bind() (e.g. interface not up),
//   - empty/invalid interface list.
class SocketCanReader {
public:
  SocketCanReader(const std::string &ifaces, bool console_output);
  ~SocketCanReader();

  SocketCanReader(const SocketCanReader &) = delete;
  SocketCanReader &operator=(const SocketCanReader &) = delete;

  // Reads frames until stop_token is requested. Each successfully decoded
  // frame is pushed into signals_s::map's "raw_frame" signal. Error frames
  // bump g_error_frame_count. RTR frames are silently dropped to match the
  // candump-text pipeline's behaviour.
  void run(std::stop_token st);

private:
  std::vector<int> m_fds_;
  bool m_console_output_ = false;
};
