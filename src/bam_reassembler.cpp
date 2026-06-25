#include "bam_reassembler.hpp"

#include <cstring>

#define FMT_HEADER_ONLY
#include <fmt/format.h>

namespace {

constexpr uint8_t kTpCmPf = 0xEC; // Transport Protocol — Connection Management
constexpr uint8_t kTpDtPf = 0xEB; // Transport Protocol — Data Transfer
constexpr uint8_t kBamControlByte = 0x20;
constexpr size_t kTpFrameSize = 8;
constexpr size_t kTpDtPayloadPerFrame = 7;
constexpr size_t kJ1939BamMaxBytes = 1785; // (255 packets * 7 bytes/packet)

uint8_t hex2(const std::string &s, size_t pos) {
  uint8_t v = 0;
  for (size_t i = 0; i < 2; ++i) {
    char c = s[pos + i];
    uint8_t n = (c >= '0' && c <= '9')   ? (c - '0')
                : (c >= 'a' && c <= 'f') ? (c - 'a' + 10)
                : (c >= 'A' && c <= 'F') ? (c - 'A' + 10)
                                         : 0u;
    v = static_cast<uint8_t>((v << 4) | n);
  }
  return v;
}

struct can_id_fields_s {
  uint8_t priority;
  uint8_t edp;
  uint8_t dp;
  uint8_t pf;
  uint8_t ps;
  uint8_t sa;
  bool extended;
};

// Returns false for SFF (11-bit) IDs — they can't carry J1939 transport frames.
bool parseCanId(const std::string &canid, can_id_fields_s &out) {
  if (canid.size() != 8) {
    return false;
  }
  const uint8_t b0 = hex2(canid, 0);
  out.priority = static_cast<uint8_t>((b0 >> 2) & 0x07);
  out.edp = static_cast<uint8_t>((b0 >> 1) & 0x01);
  out.dp = static_cast<uint8_t>(b0 & 0x01);
  out.pf = hex2(canid, 2);
  out.ps = hex2(canid, 4);
  out.sa = hex2(canid, 6);
  out.extended = true;
  return true;
}

} // namespace

BamReassembler::BamReassembler(int64_t timeout_ms) : m_timeout_ms_(timeout_ms) {}

void BamReassembler::evictExpired(int64_t now_ms) {
  for (auto it = m_sessions_.begin(); it != m_sessions_.end();) {
    if (now_ms - it->second.last_activity_ms > m_timeout_ms_) {
      it = m_sessions_.erase(it);
    } else {
      ++it;
    }
  }
}

std::shared_ptr<raw_frame_s> BamReassembler::emitVirtualFrame(const session_s &s, const std::string &iface,
                                                              int64_t ts_ms) const {
  // Unpack target PGN (24 bits, big enough for EDP+DP+PF+PS).
  const uint8_t pf = static_cast<uint8_t>((s.target_pgn >> 8) & 0xFF);
  const uint8_t target_ps = static_cast<uint8_t>(s.target_pgn & 0xFF);
  const uint8_t dp = static_cast<uint8_t>((s.target_pgn >> 16) & 0x01);
  const uint8_t edp = static_cast<uint8_t>((s.target_pgn >> 17) & 0x01);

  // For PDU2 (PF>=240) the PGN already carries PS. For PDU1 (PF<240) PS in
  // the PGN field is zero per J1939-21 §5.2.1; the actual destination
  // address lives in the CAN ID and we reuse the one captured from TP.CM
  // (will be 0xFF for BAM, which is what broadcast looks like on the wire).
  const uint8_t ps = (pf >= 240) ? target_ps : s.orig_destination;

  const uint32_t canid_int = (static_cast<uint32_t>(s.priority & 0x07) << 26) |
                             (static_cast<uint32_t>(edp & 0x01) << 25) | (static_cast<uint32_t>(dp & 0x01) << 24) |
                             (static_cast<uint32_t>(pf) << 16) | (static_cast<uint32_t>(ps) << 8) |
                             (static_cast<uint32_t>(s.source_address));

  auto out = std::make_shared<raw_frame_s>();
  out->ts_ms = ts_ms;
  out->iface = iface;
  out->canid = fmt::format("{:08X}", canid_int);
  out->payload.assign(s.buffer.begin(), s.buffer.begin() + s.expected_size);
  return out;
}

std::vector<std::shared_ptr<const raw_frame_s>> BamReassembler::feed(std::shared_ptr<const raw_frame_s> frame) {
  if (!frame) {
    return {};
  }

  std::lock_guard<std::mutex> lock(m_mtx_);

  evictExpired(frame->ts_ms);

  can_id_fields_s f{};
  if (!parseCanId(frame->canid, f)) {
    return {std::move(frame)};
  }

  if (f.pf != kTpCmPf && f.pf != kTpDtPf) {
    return {std::move(frame)};
  }

  if (f.pf == kTpCmPf) {
    if (frame->payload.size() < kTpFrameSize) {
      // Malformed TP.CM — pass through so it's visible during debugging.
      return {std::move(frame)};
    }
    const uint8_t ctl = frame->payload[0];
    if (ctl != kBamControlByte) {
      return {std::move(frame)};
    }

    // BAM payload layout:
    //   [0]   = 0x20
    //   [1-2] = total size (LE)
    //   [3]   = total packets
    //   [4]   = reserved (should be 0xFF)
    //   [5-7] = target PGN (LE, 24 bits)
    session_s s;
    s.expected_size = static_cast<size_t>(frame->payload[1]) | (static_cast<size_t>(frame->payload[2]) << 8);
    s.expected_packets = frame->payload[3];
    s.target_pgn = static_cast<uint32_t>(frame->payload[5]) | (static_cast<uint32_t>(frame->payload[6]) << 8) |
                   (static_cast<uint32_t>(frame->payload[7]) << 16);

    // Sanity guards. A malformed/hostile BAM shouldn't be able to allocate
    // gigabytes or stall us forever waiting for packets that never arrive.
    if (s.expected_packets == 0 || s.expected_size == 0 || s.expected_size > kJ1939BamMaxBytes) {
      return {}; // swallow the bad announcement
    }
    const size_t expected_from_packets = static_cast<size_t>(s.expected_packets) * kTpDtPayloadPerFrame;
    if (s.expected_size > expected_from_packets || s.expected_size + kTpDtPayloadPerFrame <= expected_from_packets) {
      // size and packet count don't line up — refuse the session
      return {};
    }

    s.source_address = f.sa;
    s.priority = f.priority;
    s.orig_destination = f.ps;
    s.next_seq = 1;
    s.last_activity_ms = frame->ts_ms;
    s.buffer.assign(expected_from_packets, 0xFF);

    m_sessions_[fmt::format("{}|{:02X}", frame->iface, f.sa)] = std::move(s);
    return {}; // BAM announce itself is not forwarded
  }

  const std::string key = fmt::format("{}|{:02X}", frame->iface, f.sa);
  auto it = m_sessions_.find(key);
  if (it == m_sessions_.end()) {
    return {};
  }

  if (frame->payload.size() < 1) {
    m_sessions_.erase(it);
    return {};
  }

  const uint8_t seq = frame->payload[0];
  auto &s = it->second;

  if (seq != s.next_seq) {
    m_sessions_.erase(it);
    return {};
  }

  const size_t offset = static_cast<size_t>(seq - 1) * kTpDtPayloadPerFrame;
  const size_t avail = frame->payload.size() - 1; // exclude seq byte
  const size_t copy_n = std::min<size_t>(avail, kTpDtPayloadPerFrame);

  for (size_t i = 0; i < copy_n; ++i) {
    if (offset + i < s.buffer.size()) {
      s.buffer[offset + i] = frame->payload[1 + i];
    }
  }

  s.last_activity_ms = frame->ts_ms;
  s.next_seq = static_cast<uint8_t>(s.next_seq + 1);

  if (seq == s.expected_packets) {
    auto virt = emitVirtualFrame(s, frame->iface, frame->ts_ms);
    m_sessions_.erase(it);
    return {std::move(virt)};
  }

  return {};
}

BamReassembler &bamReassembler() {
  static BamReassembler instance;
  return instance;
}
