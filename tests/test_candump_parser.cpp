#include <catch2/catch_test_macros.hpp>

#include "candump_parser.hpp"

#include <chrono>

namespace {
int64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
      .count();
}
} // namespace

// -------------------------------------------------------- basic SFF/EFF, no timestamp

TEST_CASE("parseCandumpLine: standard frame without timestamp, SFF CAN ID", "[candump]") {
  auto p = parseCandumpLine("can0  123   [2]  AB CD");
  REQUIRE(p.kind == parsed_candump_s::kind_e::data);
  REQUIRE(p.iface == "can0");
  REQUIRE(p.canid == "123");
  REQUIRE(p.payload == std::vector<uint8_t>{0xAB, 0xCD});

  // No candump timestamp prefix → ts falls back to system clock. Should be close to now.
  const auto ref = now_ms();
  REQUIRE(p.ts_ms > ref - 5000);
  REQUIRE(p.ts_ms <= ref + 1000);
}

TEST_CASE("parseCandumpLine: 29-bit EFF CAN ID", "[candump]") {
  auto p = parseCandumpLine("can0  18F00400   [8]  06 7D B4 F8 1E 03 F3 B4");
  REQUIRE(p.kind == parsed_candump_s::kind_e::data);
  REQUIRE(p.canid == "18F00400");
  REQUIRE(p.payload.size() == 8);
  REQUIRE(p.payload[0] == 0x06);
  REQUIRE(p.payload[7] == 0xB4);
}

TEST_CASE("parseCandumpLine: DLC-only line without any payload tokens is rejected", "[candump]") {
  // Current parser requires at least one token past the DLC (original strict `>` check).
  // RTR/ERRORFRAME still work because they provide trailing marker tokens.
  REQUIRE(parseCandumpLine("can0 123 [0]").kind == parsed_candump_s::kind_e::invalid);
}

// -------------------------------------------------------- timestamp prefix: -t a

TEST_CASE("parseCandumpLine: -t a absolute unix seconds timestamp", "[candump][timestamp]") {
  auto p = parseCandumpLine("(1705327800.123456) can0 18F00400 [8] 06 7D B4 F8 1E 03 F3 B4");
  REQUIRE(p.kind == parsed_candump_s::kind_e::data);
  // 1705327800.123456 seconds -> 1705327800123 ms (microseconds truncated)
  REQUIRE(p.ts_ms == 1705327800123);
  REQUIRE(p.iface == "can0");
  REQUIRE(p.canid == "18F00400");
}

TEST_CASE("parseCandumpLine: -t a with leading whitespace (from `candump -t a any`)", "[candump][timestamp]") {
  auto p = parseCandumpLine(" (1705327800.5) can0 123 [1] FF");
  REQUIRE(p.kind == parsed_candump_s::kind_e::data);
  REQUIRE(p.ts_ms == 1705327800500);
  REQUIRE(p.payload == std::vector<uint8_t>{0xFF});
}

// -------------------------------------------------------- timestamp prefix: -t A

TEST_CASE("parseCandumpLine: -t A wall-clock timestamp", "[candump][timestamp]") {
  // Because mktime() uses the current TZ of the process, we can't assert an exact value
  // without controlling TZ. Instead, check round-trip: build a tm, mktime it, and compare.
  auto p = parseCandumpLine(" (2026-04-24 03:11:21.076307)  can0  0CFF1397   [8]  00 07 85 00 00 04 00 00");
  REQUIRE(p.kind == parsed_candump_s::kind_e::data);
  REQUIRE(p.canid == "0CFF1397");
  REQUIRE(p.payload.size() == 8);

  std::tm tm{};
  tm.tm_year = 2026 - 1900;
  tm.tm_mon = 4 - 1;
  tm.tm_mday = 24;
  tm.tm_hour = 3;
  tm.tm_min = 11;
  tm.tm_sec = 21;
  tm.tm_isdst = -1;
  std::time_t t = std::mktime(&tm);
  REQUIRE(t != -1);
  const int64_t expected_ms = static_cast<int64_t>(t) * 1000 + 76;
  REQUIRE(p.ts_ms == expected_ms);
}

// -------------------------------------------------------- timestamp prefix: relative / invalid

TEST_CASE("parseCandumpLine: -t z relative timestamp falls back to system clock", "[candump][timestamp]") {
  const auto before = now_ms();
  auto p = parseCandumpLine("(0.123456) can0 123 [1] 00");
  const auto after = now_ms();
  REQUIRE(p.kind == parsed_candump_s::kind_e::data);
  // Value 0.123456 is < 1e9, so the parser rejects it as a non-wall-clock ts.
  REQUIRE(p.ts_ms >= before);
  REQUIRE(p.ts_ms <= after);
}

TEST_CASE("parseCandumpLine: malformed (...) prefix is skipped but frame still parses", "[candump][timestamp]") {
  const auto before = now_ms();
  auto p = parseCandumpLine("(garbage) can0 123 [1] 42");
  const auto after = now_ms();
  REQUIRE(p.kind == parsed_candump_s::kind_e::data);
  REQUIRE(p.payload == std::vector<uint8_t>{0x42});
  REQUIRE(p.ts_ms >= before);
  REQUIRE(p.ts_ms <= after);
}

// -------------------------------------------------------- error/remote frames

TEST_CASE("parseCandumpLine: ERRORFRAME marker", "[candump][error]") {
  auto p = parseCandumpLine("can0 20000000 [8] 00 00 00 00 00 00 00 00 ERRORFRAME");
  REQUIRE(p.kind == parsed_candump_s::kind_e::error_frame);
}

TEST_CASE("parseCandumpLine: remote request (RTR)", "[candump][rtr]") {
  auto p = parseCandumpLine("can0 123 [0] remote request");
  REQUIRE(p.kind == parsed_candump_s::kind_e::remote_frame);
}

// -------------------------------------------------------- invalid input

TEST_CASE("parseCandumpLine: empty line is invalid", "[candump][invalid]") {
  REQUIRE(parseCandumpLine("").kind == parsed_candump_s::kind_e::invalid);
}

TEST_CASE("parseCandumpLine: whitespace-only line is invalid", "[candump][invalid]") {
  REQUIRE(parseCandumpLine("     ").kind == parsed_candump_s::kind_e::invalid);
}

TEST_CASE("parseCandumpLine: CAN ID with non-hex character", "[candump][invalid]") {
  REQUIRE(parseCandumpLine("can0 1Z3 [1] 00").kind == parsed_candump_s::kind_e::invalid);
}

TEST_CASE("parseCandumpLine: CAN ID of invalid length (neither 3 nor 8 hex)", "[candump][invalid]") {
  REQUIRE(parseCandumpLine("can0 1234 [1] 00").kind == parsed_candump_s::kind_e::invalid);
  REQUIRE(parseCandumpLine("can0 12 [1] 00").kind == parsed_candump_s::kind_e::invalid);
}

TEST_CASE("parseCandumpLine: DLC must have [N] brackets", "[candump][invalid]") {
  REQUIRE(parseCandumpLine("can0 123 8 00 01 02 03 04 05 06 07").kind == parsed_candump_s::kind_e::invalid);
}

TEST_CASE("parseCandumpLine: DLC mismatches actual byte count", "[candump][invalid]") {
  // DLC says 4, only 3 bytes present.
  REQUIRE(parseCandumpLine("can0 123 [4] 00 01 02").kind == parsed_candump_s::kind_e::invalid);
}

TEST_CASE("parseCandumpLine: DLC above 64 (CAN FD upper bound) is rejected", "[candump][invalid]") {
  REQUIRE(parseCandumpLine("can0 123 [65] 00").kind == parsed_candump_s::kind_e::invalid);
}

TEST_CASE("parseCandumpLine: payload byte isn't 2 hex digits", "[candump][invalid]") {
  REQUIRE(parseCandumpLine("can0 123 [1] F").kind == parsed_candump_s::kind_e::invalid);
  REQUIRE(parseCandumpLine("can0 123 [1] ZZ").kind == parsed_candump_s::kind_e::invalid);
}

TEST_CASE("parseCandumpLine: too few fields", "[candump][invalid]") {
  REQUIRE(parseCandumpLine("can0 123").kind == parsed_candump_s::kind_e::invalid);
  REQUIRE(parseCandumpLine("can0").kind == parsed_candump_s::kind_e::invalid);
}

// -------------------------------------------------------- strict iface validation

TEST_CASE("parseCandumpLine: iface with special chars is rejected", "[candump][invalid][iface]") {
  REQUIRE(parseCandumpLine("c@n0 123 [1] 00").kind == parsed_candump_s::kind_e::invalid);
  REQUIRE(parseCandumpLine("'can0' 123 [1] 00").kind == parsed_candump_s::kind_e::invalid);
  REQUIRE(parseCandumpLine("can 0 123 [1] 00").kind == parsed_candump_s::kind_e::invalid);
}

TEST_CASE("parseCandumpLine: iface longer than 16 chars is rejected", "[candump][invalid][iface]") {
  REQUIRE(parseCandumpLine("very_long_interface_name 123 [1] 00").kind == parsed_candump_s::kind_e::invalid);
}

TEST_CASE("parseCandumpLine: common SocketCAN iface names accepted", "[candump][iface]") {
  REQUIRE(parseCandumpLine("can0   123 [1] 00").kind == parsed_candump_s::kind_e::data);
  REQUIRE(parseCandumpLine("vcan0  123 [1] 00").kind == parsed_candump_s::kind_e::data);
  REQUIRE(parseCandumpLine("slcan1 123 [1] 00").kind == parsed_candump_s::kind_e::data);
  REQUIRE(parseCandumpLine("any    123 [1] 00").kind == parsed_candump_s::kind_e::data);
  REQUIRE(parseCandumpLine("can-fd 123 [1] 00").kind == parsed_candump_s::kind_e::data);
}

// -------------------------------------------------------- strict hex check (locale-independent)

TEST_CASE("parseCandumpLine: non-ASCII 'digits' in CAN ID are rejected", "[candump][invalid][canid]") {
  REQUIRE(parseCandumpLine("can0 \xD9\xA3\xD9\xA1\xD9\xA2 [1] 00").kind == parsed_candump_s::kind_e::invalid);
}
