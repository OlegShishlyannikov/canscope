#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "can_data.hpp"

// J1939-21 BAM (Broadcast Announce Message) reassembler.
//
// Watches the raw CAN stream for transport-protocol traffic:
//   * TP.CM_BAM   — PF=0xEC, payload[0]=0x20 — opens a session and stores
//                   target PGN, total size, packet count.
//   * TP.DT       — PF=0xEB — buffers up to 7 bytes per packet keyed by the
//                   sender's source address; on the final packet we emit a
//                   virtual reassembled frame whose CAN ID encodes the
//                   target PGN and whose payload is the concatenated data.
//
// Out of scope on first cut: peer-to-peer RTS/CTS/EndOfMsgAck/ConnAbort
// (PF=0xEC with control bytes 0x10/0x11/0x13/0xFF) and Extended TP
// (PF=0xC8/0xC7). DM1/DM2/DM12/DM23 use BAM, which is what we need now.
class BamReassembler {
public:
  // J1939-21 timer T3 is 1250 ms; we add a small safety margin.
  explicit BamReassembler(int64_t timeout_ms = 1500);

  // Feed one raw CAN frame. Returns 0..N frames to forward downstream:
  //   * TP.CM_BAM         — opens/replaces a session; returns {}.
  //   * TP.DT (mid)       — appends data; returns {}.
  //   * TP.DT (final)     — appends data, emits one virtual frame (the
  //                         reassembled message with target PGN), returns it.
  //   * Orphan TP.DT      — dropped silently; returns {}.
  //   * TP.CM (non-BAM)   — passed through as-is so downstream can still see
  //                         RTS/CTS traffic if it cares.
  //   * Any other frame   — passed through as-is.
  // Sessions older than `timeout_ms` are evicted at the start of each call.
  std::vector<std::shared_ptr<const raw_frame_s>> feed(std::shared_ptr<const raw_frame_s> frame);

private:
  struct session_s {
    uint32_t target_pgn = 0;         // Encoded in TP.CM_BAM bytes 5..7 (LE)
    size_t expected_size = 0;        // Bytes 1..2 of TP.CM_BAM (LE), <=1785
    uint8_t expected_packets = 0;    // Byte 3 of TP.CM_BAM
    uint8_t next_seq = 1;            // 1-based, matches TP.DT byte 0
    uint8_t source_address = 0;      // Last byte of TP.CM CAN ID
    uint8_t priority = 6;            // Top 3 bits of TP.CM CAN ID (default 6)
    uint8_t orig_destination = 0xFF; // PS of TP.CM CAN ID; 0xFF for BAM
    int64_t last_activity_ms = 0;
    std::vector<uint8_t> buffer;
  };

  void evictExpired(int64_t now_ms);

  std::shared_ptr<raw_frame_s> emitVirtualFrame(const session_s &s, const std::string &iface, int64_t ts_ms) const;

  // Key: "iface|SA". Only one BAM session per (iface, source) at a time per
  // J1939-21 — a new BAM from the same source replaces any pending one.
  std::unordered_map<std::string, session_s> m_sessions_;
  int64_t m_timeout_ms_ = 1500;
  std::mutex m_mtx_;
};

// Process-wide singleton used by both the candump-text and SocketCAN paths.
// Lazily constructed on first call; safe under concurrent feed() because the
// instance owns its own mutex.
BamReassembler &bamReassembler();
