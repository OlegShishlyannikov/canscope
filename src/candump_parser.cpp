#include "candump_parser.hpp"

#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <ranges>
#include <string_view>

namespace {

enum class field_e : size_t {
  INTERFACE = 0,
  CANID = 1,
  DLC = 2,
  PAYLOAD_BEGIN = 3,
};

constexpr size_t idx(field_e f) {
  return static_cast<size_t>(f);
}

int64_t epoch_ms_now() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

// Extracts the optional "(...)"  timestamp prefix from `rest` (modifying rest
// to point past the prefix) and returns its value in ms. Returns 0 if absent
// or unparseable, in which case the caller should fall back to the system clock.
int64_t extractTimestamp(std::string_view &rest) {
  while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t')) rest.remove_prefix(1);
  if (rest.empty() || rest.front() != '(') return 0;

  auto close = rest.find(')');
  if (close == std::string_view::npos) return 0;

  std::string inner(rest.substr(1, close - 1));
  rest.remove_prefix(close + 1);
  while (!rest.empty() && rest.front() == ' ') rest.remove_prefix(1);

  // Try absolute unix seconds (-t a).
  char *end = nullptr;
  double ts_s = std::strtod(inner.c_str(), &end);
  if (end == inner.c_str() + inner.size() && ts_s > 1e9) {
    return static_cast<int64_t>(ts_s * 1000.0);
  }

  // Try wall-clock "YYYY-MM-DD HH:MM:SS[.fff]" in local time (-t A).
  int Y = 0, Mo = 0, D = 0, H = 0, Mi = 0;
  double S = 0.0;
  if (std::sscanf(inner.c_str(), "%d-%d-%d %d:%d:%lf", &Y, &Mo, &D, &H, &Mi, &S) == 6) {
    std::tm tm{};
    tm.tm_year = Y - 1900;
    tm.tm_mon = Mo - 1;
    tm.tm_mday = D;
    tm.tm_hour = H;
    tm.tm_min = Mi;
    tm.tm_sec = static_cast<int>(S);
    tm.tm_isdst = -1;
    std::time_t t = std::mktime(&tm);
    if (t != -1) {
      double frac = S - static_cast<int64_t>(S);
      return static_cast<int64_t>(t) * 1000 + static_cast<int64_t>(frac * 1000.0);
    }
  }

  return 0;
}

} // namespace

parsed_candump_s parseCandumpLine(const std::string &line) {
  parsed_candump_s out;
  if (line.empty()) return out;

  std::string_view rest = line;
  int64_t ts_ms = extractTimestamp(rest);
  if (ts_ms == 0) ts_ms = epoch_ms_now();

  std::vector<std::string_view> words;
  for (auto part : rest | std::views::split(' ')) {
    if (!part.empty()) {
      words.emplace_back(part.begin(), part.end());
    }
  }

  if (words.size() <= idx(field_e::PAYLOAD_BEGIN)) return out;

  auto &iface = words[idx(field_e::INTERFACE)];
  auto &canid = words[idx(field_e::CANID)];

  // Locale-independent ASCII hex check. std::isxdigit's behaviour depends on the
  // current C locale — on some locales extra code points qualify as "digits",
  // which can let mangled lines (e.g. from a flaky SSH tunnel) slip through.
  constexpr auto is_ascii_hex = [](char c) noexcept {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
  };

  // Validate iface name: short ASCII identifier. Covers can0/vcan0/slcan1/any/etc.
  // Rejects anything containing quotes, spaces, control bytes or non-ASCII.
  constexpr size_t kIfaceMaxLen = 16;
  if (iface.empty() || iface.size() > kIfaceMaxLen) return out;
  for (const char &c : iface) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '_' || c == '-';
    if (!ok) return out;
  }

  // Validate CAN ID: exactly 3 hex digits (SFF, 11-bit) or 8 (EFF, 29-bit).
  constexpr auto sff_length_bytes = 3u, eff_length_bytes = 8u;
  if (canid.size() != sff_length_bytes && canid.size() != eff_length_bytes) return out;
  for (const char &c : canid) {
    if (!is_ascii_hex(c)) return out;
  }

  // Parse DLC "[N]".
  int32_t dlc_size = 0;
  {
    auto &dlc = words[idx(field_e::DLC)];
    if (dlc.size() < 3 || dlc.front() != '[' || dlc.back() != ']') return out;
    auto sv = dlc.substr(1, dlc.size() - 2);
    if (auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), dlc_size);
        ec != std::errc{} || ptr != sv.data() + sv.size()) {
      return out;
    }
    constexpr int32_t max_payload_bytes = 64; // CAN FD upper bound
    if (dlc_size < 0 || dlc_size > max_payload_bytes) return out;
  }

  // Detect ERRORFRAME marker (SocketCAN diagnostic pseudo-frame).
  if (words.back() == "ERRORFRAME") {
    out.kind = parsed_candump_s::kind_e::error_frame;
    return out;
  }

  // Detect RTR: candump prints "remote request" in place of payload bytes.
  if (words[idx(field_e::PAYLOAD_BEGIN)] == "remote") {
    out.kind = parsed_candump_s::kind_e::remote_frame;
    return out;
  }

  // Parse payload bytes.
  std::vector<uint8_t> payload;
  payload.reserve(words.size() - idx(field_e::PAYLOAD_BEGIN));
  for (size_t i = idx(field_e::PAYLOAD_BEGIN); i < words.size(); ++i) {
    if (words[i].size() != 2) return out;
    uint8_t byte = 0;
    auto *first = words[i].data();
    auto *last = first + words[i].size();
    if (auto [ptr, ec] = std::from_chars(first, last, byte, 16); ec != std::errc{} || ptr != last) {
      return out;
    }
    payload.push_back(byte);
  }

  if (static_cast<int32_t>(payload.size()) != dlc_size) return out;

  out.kind = parsed_candump_s::kind_e::data;
  out.ts_ms = ts_ms;
  out.iface = std::string(iface);
  out.canid = std::string(canid);
  out.payload = std::move(payload);
  return out;
}
