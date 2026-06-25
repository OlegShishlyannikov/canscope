#include <catch2/catch_test_macros.hpp>

#define FMT_HEADER_ONLY
#include <fmt/format.h>

#include "bam_reassembler.hpp"

namespace {

std::shared_ptr<raw_frame_s> makeFrame(const std::string &iface, const std::string &canid, std::vector<uint8_t> payload,
                                       int64_t ts_ms = 1'000) {
  auto f = std::make_shared<raw_frame_s>();
  f->ts_ms = ts_ms;
  f->iface = iface;
  f->canid = canid;
  f->payload = std::move(payload);
  return f;
}

// TP.CM_BAM announcing PGN 65226 (DM1), `n_packets` * 7 bytes, total `size`.
std::shared_ptr<raw_frame_s> bamAnnounce(uint8_t sa, uint16_t size, uint8_t n_packets, uint32_t target_pgn = 0x00FECA,
                                         int64_t ts_ms = 1'000) {
  std::vector<uint8_t> p(8, 0xFF);
  p[0] = 0x20; // BAM
  p[1] = static_cast<uint8_t>(size & 0xFF);
  p[2] = static_cast<uint8_t>((size >> 8) & 0xFF);
  p[3] = n_packets;
  p[4] = 0xFF;
  p[5] = static_cast<uint8_t>(target_pgn & 0xFF);
  p[6] = static_cast<uint8_t>((target_pgn >> 8) & 0xFF);
  p[7] = static_cast<uint8_t>((target_pgn >> 16) & 0xFF);

  // CAN ID: priority=6, EDP=0, DP=0, PF=0xEC, PS=0xFF (broadcast), SA=sa
  return makeFrame("can0", fmt::format("18ECFF{:02X}", sa), std::move(p), ts_ms);
}

// TP.DT segment from `sa`, sequence `seq`, with given 7-byte payload (padded with 0xFF).
std::shared_ptr<raw_frame_s> bamData(uint8_t sa, uint8_t seq, std::vector<uint8_t> data7, int64_t ts_ms = 1'001) {
  std::vector<uint8_t> p;
  p.reserve(8);
  p.push_back(seq);

  for (size_t i = 0; i < 7; ++i) {
    p.push_back(i < data7.size() ? data7[i] : 0xFF);
  }

  return makeFrame("can0", fmt::format("18EBFF{:02X}", sa), std::move(p), ts_ms);
}
} // namespace

TEST_CASE("BamReassembler: regular non-TP frame passes through unchanged", "[bam]") {
  BamReassembler r;
  auto in = makeFrame("can0", "18FEF100", {1, 2, 3, 4, 5, 6, 7, 8});
  auto out = r.feed(in);
  REQUIRE(out.size() == 1);
  REQUIRE(out[0]->canid == "18FEF100");
  REQUIRE(out[0]->payload == std::vector<uint8_t>{1, 2, 3, 4, 5, 6, 7, 8});
}

TEST_CASE("BamReassembler: SFF (11-bit) frame passes through unchanged", "[bam]") {
  BamReassembler r;
  auto out = r.feed(makeFrame("can0", "123", {0xAB}));
  REQUIRE(out.size() == 1);
  REQUIRE(out[0]->canid == "123");
}

// -------------------------------------------------------- BAM session lifecycle

TEST_CASE("BamReassembler: single-DTC DM1 via BAM is reassembled", "[bam]") {
  BamReassembler r;
  // Announce 8 bytes in 2 packets (DM1 in single frame doesn't use BAM normally,
  // but BAM is legal for any size — this exercises the smallest valid session).
  REQUIRE(r.feed(bamAnnounce(/*sa=*/0xAB, /*size=*/8, /*n_packets=*/2)).empty());
  REQUIRE(r.feed(bamData(0xAB, 1, {0x00, 0x00, 0x03, 0x04, 0xB8, 0x04, 0x00})).empty());
  auto out = r.feed(bamData(0xAB, 2, {0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}));
  REQUIRE(out.size() == 1);
  // Virtual frame: priority=6, EDP=0, DP=0, PF=0xFE, PS=0xCA, SA=0xAB → 18FECAAB
  REQUIRE(out[0]->canid == "18FECAAB");
  REQUIRE(out[0]->payload.size() == 8);
  REQUIRE(out[0]->payload[0] == 0x00);
  REQUIRE(out[0]->payload[4] == 0xB8); // SPN low byte
  REQUIRE(out[0]->payload[7] == 0x03); // FMI in 5.1-5
}

TEST_CASE("BamReassembler: 3-DTC DM1 (20-byte multipacket) is reassembled", "[bam]") {
  BamReassembler r;
  REQUIRE(r.feed(bamAnnounce(0xAB, /*size=*/20, /*n_packets=*/3)).empty());
  // 3 packets × 7 bytes = 21 raw, truncated to expected_size=20.
  REQUIRE(r.feed(bamData(0xAB, 1, {0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05})).empty());
  REQUIRE(r.feed(bamData(0xAB, 2, {0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C})).empty());
  // seq 3 carries raw offsets 14..20. expected_size=20 keeps 0..19 and drops
  // offset 20 (the 21st raw byte) as TP padding.
  auto out = r.feed(bamData(0xAB, 3, {0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13}));
  REQUIRE(out.size() == 1);
  REQUIRE(out[0]->payload.size() == 20);
  // Offsets 18 and 19 are seq 3's 5th and 6th data bytes — the last two kept.
  REQUIRE(out[0]->payload[18] == 0x11);
  REQUIRE(out[0]->payload[19] == 0x12);
  // The 21st raw byte (0x13 at offset 20) is past expected_size and must be dropped.
  REQUIRE(out[0]->payload[0] == 0x00);
}

TEST_CASE("BamReassembler: TP.CM_BAM and TP.DT frames are swallowed (not forwarded)", "[bam]") {
  BamReassembler r;
  auto ann_out = r.feed(bamAnnounce(0xAB, 8, 2));
  REQUIRE(ann_out.empty());
  auto dt1_out = r.feed(bamData(0xAB, 1, {0, 0, 0, 0, 0, 0, 0}));
  REQUIRE(dt1_out.empty());
  // Final TP.DT emits the virtual frame, but not the TP.DT itself.
  auto dt2_out = r.feed(bamData(0xAB, 2, {0, 0, 0, 0, 0, 0, 0}));
  REQUIRE(dt2_out.size() == 1);
  REQUIRE(dt2_out[0]->canid.starts_with("18FECA"));
}

// -------------------------------------------------------- concurrency / failure modes

TEST_CASE("BamReassembler: two parallel sessions on different SA don't interfere", "[bam]") {
  BamReassembler r;
  REQUIRE(r.feed(bamAnnounce(/*sa=*/0xAA, 8, 2)).empty());
  REQUIRE(r.feed(bamAnnounce(/*sa=*/0xBB, 8, 2)).empty());
  REQUIRE(r.feed(bamData(0xAA, 1, {1, 1, 1, 1, 1, 1, 1})).empty());
  REQUIRE(r.feed(bamData(0xBB, 1, {2, 2, 2, 2, 2, 2, 2})).empty());

  auto a_out = r.feed(bamData(0xAA, 2, {1, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}));
  REQUIRE(a_out.size() == 1);
  REQUIRE(a_out[0]->canid == "18FECAAA");
  REQUIRE(a_out[0]->payload[0] == 1);

  auto b_out = r.feed(bamData(0xBB, 2, {2, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}));
  REQUIRE(b_out.size() == 1);
  REQUIRE(b_out[0]->canid == "18FECABB");
  REQUIRE(b_out[0]->payload[0] == 2);
}

TEST_CASE("BamReassembler: sequence mismatch aborts the session", "[bam]") {
  BamReassembler r;
  REQUIRE(r.feed(bamAnnounce(0xAB, 14, 2)).empty());
  REQUIRE(r.feed(bamData(0xAB, 1, {1, 2, 3, 4, 5, 6, 7})).empty());
  // Skip seq 2; deliver seq 3 instead. Session must abort and emit nothing.
  auto bad = r.feed(bamData(0xAB, 3, {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}));
  REQUIRE(bad.empty());

  // Session is gone — a subsequent (now orphan) TP.DT also produces nothing.
  auto orphan = r.feed(bamData(0xAB, 2, {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}));
  REQUIRE(orphan.empty());
}

TEST_CASE("BamReassembler: stale session is evicted after timeout", "[bam]") {
  BamReassembler r(/*timeout_ms=*/100);
  REQUIRE(r.feed(bamAnnounce(0xAB, 8, 2, 0x00FECA, /*ts_ms=*/1'000)).empty());
  REQUIRE(r.feed(bamData(0xAB, 1, {0, 0, 0, 0, 0, 0, 0}, /*ts_ms=*/1'050)).empty());

  // Inject an unrelated frame far in the future to trigger eviction.
  auto unrelated = r.feed(makeFrame("can0", "18FEF100", {0}, /*ts_ms=*/2'000));
  REQUIRE(unrelated.size() == 1); // unrelated frame still passes through

  // Now the late final TP.DT arrives — session is gone, no virtual frame.
  auto late = r.feed(bamData(0xAB, 2, {0, 0, 0, 0, 0, 0, 0}, /*ts_ms=*/2'100));
  REQUIRE(late.empty());
}

TEST_CASE("BamReassembler: orphan TP.DT (no announce) is dropped", "[bam]") {
  BamReassembler r;
  auto out = r.feed(bamData(0x42, 1, {0, 0, 0, 0, 0, 0, 0}));
  REQUIRE(out.empty());
}

TEST_CASE("BamReassembler: malformed BAM announce (size > 1785) is rejected without crash", "[bam]") {
  BamReassembler r;
  auto out = r.feed(bamAnnounce(0xAB, /*size=*/2000, /*n_packets=*/255));
  REQUIRE(out.empty());
  // No session should have been opened.
  auto orphan = r.feed(bamData(0xAB, 1, {0, 0, 0, 0, 0, 0, 0}));
  REQUIRE(orphan.empty());
}

TEST_CASE("BamReassembler: malformed BAM (size=0 or packets=0) is rejected", "[bam]") {
  BamReassembler r;
  REQUIRE(r.feed(bamAnnounce(0xAB, 0, 1)).empty());
  REQUIRE(r.feed(bamAnnounce(0xAB, 8, 0)).empty());
  // No active session.
  REQUIRE(r.feed(bamData(0xAB, 1, {0, 0, 0, 0, 0, 0, 0})).empty());
}

TEST_CASE("BamReassembler: non-BAM TP.CM (RTS/CTS) is forwarded as-is", "[bam]") {
  BamReassembler r;
  // RTS = 0x10
  std::vector<uint8_t> rts_payload(8, 0xFF);
  rts_payload[0] = 0x10;
  auto rts = makeFrame("can0", "18EC0123", std::move(rts_payload));
  auto out = r.feed(rts);
  REQUIRE(out.size() == 1);
  REQUIRE(out[0]->canid == "18EC0123");
  REQUIRE(out[0]->payload[0] == 0x10);
}

TEST_CASE("BamReassembler: new BAM from same source replaces a pending session", "[bam]") {
  BamReassembler r;
  REQUIRE(r.feed(bamAnnounce(0xAB, 14, 2, 0x00FECA, /*ts_ms=*/1'000)).empty());
  REQUIRE(r.feed(bamData(0xAB, 1, {1, 2, 3, 4, 5, 6, 7}, /*ts_ms=*/1'001)).empty());
  // New BAM for a different PGN arrives before the first one finishes —
  // J1939-21 says only one BAM per (iface,SA) at a time; we replace it.
  REQUIRE(r.feed(bamAnnounce(0xAB, 14, 2, /*target_pgn=*/0x00FECB, /*ts_ms=*/1'010)).empty());

  // Stale TP.DT for the old session's seq 2 must NOT trigger emission of
  // the old PGN (the new session expects seq 1).
  auto out = r.feed(bamData(0xAB, 1, {9, 9, 9, 9, 9, 9, 9}, /*ts_ms=*/1'020));
  REQUIRE(out.empty());

  auto finished = r.feed(bamData(0xAB, 2, {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, /*ts_ms=*/1'030));
  REQUIRE(finished.size() == 1);
  // Target PGN was 0x00FECB → PF=0xFE, PS=0xCB
  REQUIRE(finished[0]->canid == "18FECBAB");
}
