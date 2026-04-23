#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <yaml-cpp/yaml.h>

#include <linux/can.h>
#include <linux/can/raw.h>
#include <linux/if.h>
#include <linux/sockios.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#define FMT_HEADER_ONLY
#include <fmt/format.h>

#include "generators.hpp"
#include "sqlite_modern_cpp.h"

namespace {

struct spn_fragment_s {
  int32_t byte_offset = 0, bit_offset = 0, size = 0;
};

struct spn_spec_s {
  int32_t pgn = 0, spn = 0;
  std::string name, unit;
  double min = 0.0, max = 0.0, resolution = 1.0, offset = 0.0;
  int32_t length_bits = 0;
  std::vector<spn_fragment_s> fragments;
  bool little_endian = false;
};

struct pgn_spec_s {
  int32_t pgn = 0;
  int32_t datalen = 8;
  int32_t priority = 6;
  std::string label, acronym;
};

struct can_encoder_handle_s {
  std::function<std::optional<pgn_spec_s>(int32_t pgn)> getPgn;
  std::function<std::optional<spn_spec_s>(int32_t pgn, int32_t spn)> getSpn;
  std::function<void(std::vector<uint8_t> &payload, const spn_spec_s &spec, double physical_value)> encodeSpn;
};

can_encoder_handle_s makeCanEncoder(sqlite::database &db) {
  class Impl {
  public:
    Impl(sqlite::database &db) : m_db_(db) {}

    std::optional<pgn_spec_s> getPgn(int32_t pgn) {
      if (auto it = m_pgn_cache_.find(pgn); it != m_pgn_cache_.end()) {
        return it->second;
      }

      auto spec = loadPgn(pgn);
      if (spec) {
        m_pgn_cache_.emplace(pgn, *spec);
      }

      return spec;
    }

    std::optional<spn_spec_s> getSpn(int32_t pgn, int32_t spn) {
      auto key = std::make_pair(pgn, spn);
      if (auto it = m_spn_cache_.find(key); it != m_spn_cache_.end()) {
        return it->second;
      }

      auto spec = loadSpn(pgn, spn);
      if (spec) {
        m_spn_cache_.emplace(key, *spec);
      }

      return spec;
    }

    // Stateless helper — written as static so it can be invoked without an instance.
    static void encodeSpn(std::vector<uint8_t> &payload, const spn_spec_s &spec, double physical_value) {
      if (spec.fragments.empty() || payload.empty()) {
        return;
      }

      double clamped = std::clamp(physical_value, spec.min, spec.max);
      double raw_d = std::round((clamped - spec.offset) / spec.resolution);
      if (raw_d < 0.0) {
        raw_d = 0.0;
      }

      uint64_t raw = static_cast<uint64_t>(raw_d);

      if (spec.little_endian) {
        size_t total_bits = 0;
        for (const auto &f : spec.fragments) {
          total_bits += static_cast<size_t>(f.size);
        }

        size_t nbytes = total_bits / 8u;
        if (nbytes > 1 && nbytes <= sizeof(uint64_t)) {
          uint8_t bytes[sizeof(uint64_t)] = {};
          std::memcpy(bytes, &raw, sizeof(uint64_t));
          std::reverse(bytes, bytes + nbytes);
          std::memcpy(&raw, bytes, sizeof(uint64_t));
        }
      }

      for (const auto &f : spec.fragments) {
        const int32_t full_bytes = f.size / 8;
        const int32_t leftover_bits = f.size % 8;
        const int32_t total_byte_span = full_bytes + (leftover_bits ? 1 : 0);

        for (int32_t i = 0; i < total_byte_span; ++i) {
          const size_t idx = static_cast<size_t>(f.byte_offset + i);
          if (idx >= payload.size()) {
            break;
          }

          uint8_t &byte = payload[idx];
          uint8_t clear_mask;

          if (leftover_bits && i == total_byte_span - 1) {

            const uint8_t hi = static_cast<uint8_t>(0xFFu << (leftover_bits + f.bit_offset));
            const uint8_t lo = static_cast<uint8_t>(~(0xFFu << f.bit_offset));
            clear_mask = static_cast<uint8_t>(hi | lo);
          } else if (i == 0 && f.bit_offset) {

            clear_mask = static_cast<uint8_t>(~(0xFFu << f.bit_offset));
          } else {

            clear_mask = 0x00u;
          }

          byte = static_cast<uint8_t>(byte & clear_mask);
          byte = static_cast<uint8_t>(byte | static_cast<uint8_t>((raw >> (i * 8)) << f.bit_offset));
        }

        raw >>= static_cast<uint32_t>(f.size);
      }
    }

  private:
    std::optional<pgn_spec_s> loadPgn(int32_t pgn) {
      std::optional<pgn_spec_s> out;
      m_db_ << "SELECT pg_label, pg_acronym, pg_datalen, pg_priority FROM pgns WHERE pgn = ?;" << pgn >>
          [&](const std::string &label, const std::string &acronym, int32_t datalen, int32_t priority) {
            pgn_spec_s s;
            s.pgn = pgn;
            s.label = label;
            s.acronym = acronym;
            s.datalen = datalen > 0 ? datalen : 8;
            s.priority = (priority >= 0 && priority <= 7) ? priority : 6;
            out = std::move(s);
          };
      return out;
    }

    std::optional<spn_spec_s> loadSpn(int32_t pgn, int32_t spn) {
      std::optional<spn_spec_s> out;
      m_db_ << "SELECT spn_name, spn_length, resolution, offset, min_value, max_value, units "
               "FROM spns WHERE pgn = ? AND spn = ?;"
            << pgn << spn >>
          [&](const std::string &name, int32_t length_bits, double resolution, double offset, double min, double max,
              const std::string &unit) {
            out = {
                .pgn = pgn,
                .spn = spn,
                .name = name,
                .unit = unit,
                .min = min,
                .max = max,
                .resolution = resolution != 0.0 ? resolution : 1.0,
                .offset = offset,
                .length_bits = length_bits,
            };
          };

      if (!out) {
        return std::nullopt;
      }

      m_db_ << "SELECT byte_offset, bit_offset, size FROM spn_fragments WHERE pgn = ? AND spn = ?;" << pgn << spn >>
          [&](int32_t byte_offset, int32_t bit_offset, int32_t size) {
            out->fragments.push_back({byte_offset, bit_offset, size});
          };

      return out;
    }

    sqlite::database &m_db_;
    std::map<int32_t, pgn_spec_s> m_pgn_cache_;
    std::map<std::pair<int32_t, int32_t>, spn_spec_s> m_spn_cache_;
  };

  auto impl = std::make_shared<Impl>(db);

  return {
      .getPgn = [impl](int32_t pgn) { return impl->getPgn(pgn); },
      .getSpn = [impl](int32_t pgn, int32_t spn) { return impl->getSpn(pgn, spn); },
      .encodeSpn = [](std::vector<uint8_t> &payload, const spn_spec_s &spec,
                      double v) { Impl::encodeSpn(payload, spec, v); },
  };
}

} // namespace

// Factory declared extern in main.cpp; no header needed.
extern std::unique_ptr<ValueGenerator> makeValueGenerator(const YAML::Node &node, double spn_min, double spn_max,
                                                          std::string &err);

std::shared_ptr<void> makePlayback(const std::string &yaml_path, sqlite::database &db, bool console_output) {
  class Impl {
  public:
    Impl(const std::string &yaml_path, sqlite::database &db, bool console_output)
        : m_console_output_(console_output), m_encoder_(makeCanEncoder(db)) {
      YAML::Node root;

      try {
        root = YAML::LoadFile(yaml_path);
      } catch (const YAML::Exception &e) {
        throw std::runtime_error(fmt::format("Failed to parse YAML '{}': {}", yaml_path, e.what()));
      }

      const std::string default_iface = readString(root, "interface", "can0");
      const int32_t default_priority = static_cast<int32_t>(readInt(root, "priority", 6));
      const int32_t default_address = parseHexOrDecimal(readString(root, "address", "0"), 0);

      if (!root["frames"] || !root["frames"].IsSequence()) {
        throw std::runtime_error("YAML: missing 'frames' sequence");
      }

      size_t frame_idx = 0;
      for (const auto &frame_node : root["frames"]) {
        ++frame_idx;

        if (!frame_node["pgn"]) {
          warn(fmt::format("frame #{}: missing 'pgn', skipped", frame_idx));
          continue;
        }

        const int32_t pgn = parseHexOrDecimal(readScalar(frame_node, "pgn"), -1);
        if (pgn < 0) {
          warn(fmt::format("frame #{}: invalid 'pgn', skipped", frame_idx));
          continue;
        }

        auto pgn_spec = m_encoder_.getPgn(pgn);
        if (!pgn_spec) {
          warn(fmt::format("frame #{}: PGN {} not found in J1939 database, skipped", frame_idx, pgn));
          continue;
        }

        frame_task_s task{
            .pgn = pgn,
            .priority = static_cast<int32_t>(readInt(frame_node, "priority", default_priority)),
            .address = parseHexOrDecimal(readString(frame_node, "address", ""), default_address),
            .datalen = pgn_spec->datalen,
            .iface = readString(frame_node, "interface", default_iface),
        };

        if (!frame_node["period_ms"]) {

          warn(fmt::format("frame #{} (PGN {}): 'period_ms' not specified, defaulting to 1000ms", frame_idx, pgn));
          task.period_ms = 1000;
        } else {
          task.period_ms = readInt(frame_node, "period_ms", 1000);
        }

        if (!frame_node["spns"] || !frame_node["spns"].IsSequence()) {
          warn(fmt::format("frame #{} (PGN {}): missing 'spns' sequence, skipped", frame_idx, pgn));
          continue;
        }

        for (const auto &spn_node : frame_node["spns"]) {
          if (!spn_node["spn"]) {
            warn(fmt::format("frame #{} (PGN {}): spn entry missing 'spn', skipped", frame_idx, pgn));
            continue;
          }

          const int32_t spn = static_cast<int32_t>(readInt(spn_node, "spn", -1));
          if (spn < 0) {
            warn(fmt::format("frame #{} (PGN {}): invalid 'spn', skipped", frame_idx, pgn));
            continue;
          }

          auto spn_spec = m_encoder_.getSpn(pgn, spn);
          if (!spn_spec) {
            warn(fmt::format("frame #{} (PGN {}): SPN {} not found in J1939 database, skipped", frame_idx, pgn, spn));
            continue;
          }

          std::unique_ptr<ValueGenerator> generator;

          if (spn_node["value"] && !spn_node["generator"]) {
            double v = spn_node["value"].as<double>();
            YAML::Node shortcut;
            shortcut["type"] = "const";
            shortcut["value"] = v;

            std::string err;
            generator = makeValueGenerator(shortcut, spn_spec->min, spn_spec->max, err);
            if (!generator) {
              warn(fmt::format("frame #{} (PGN {} SPN {}): {}", frame_idx, pgn, spn, err));
              continue;
            }
          } else if (spn_node["generator"]) {
            std::string err;
            generator = makeValueGenerator(spn_node["generator"], spn_spec->min, spn_spec->max, err);
            if (!generator) {
              warn(fmt::format("frame #{} (PGN {} SPN {}): {}", frame_idx, pgn, spn, err));
              continue;
            }
          } else {
            warn(fmt::format("frame #{} (PGN {} SPN {}): neither 'value' nor 'generator' specified, skipped", frame_idx,
                             pgn, spn));
            continue;
          }

          task.spns.push_back({std::move(*spn_spec), std::move(generator)});
        }

        if (task.spns.empty()) {
          warn(fmt::format("frame #{} (PGN {}): no valid SPNs, frame skipped", frame_idx, pgn));
          continue;
        }

        m_tasks_.push_back(std::move(task));
      }

      if (m_console_output_) {
        fmt::println("Playback: loaded {} frame(s) from '{}'", m_tasks_.size(), yaml_path);
      }

      start();
    }

    ~Impl() { stop(); }

  private:
    struct spn_slot_s {
      spn_spec_s spec;
      std::unique_ptr<ValueGenerator> generator;
    };

    struct frame_task_s {
      int32_t pgn = 0;
      int32_t priority = 6;
      int32_t address = 0;
      int32_t datalen = 8;
      int64_t period_ms = 1000;
      std::string iface;
      std::vector<spn_slot_s> spns;
    };

    struct runner_s {
      std::unique_ptr<std::stop_source> stop_source;
      std::future<void> future;
    };

    void start() {
      for (auto &task : m_tasks_) {
        auto ss = std::make_unique<std::stop_source>();
        auto token = ss->get_token();
        std::string iface = task.iface;
        int64_t period = task.period_ms;

        auto fut = std::async(std::launch::async,
                              [this, &task, token, iface, period]() { runFrameLoop(task, token, iface, period); });

        m_runners_.push_back({std::move(ss), std::move(fut)});
      }

      if (m_console_output_) {
        fmt::println("Playback: {} sender task(s) started", m_runners_.size());
      }
    }

    void stop() {
      if (m_stopped_) {
        return;
      }

      m_stopped_ = true;
      for (auto &r : m_runners_) {
        if (r.stop_source) {
          r.stop_source->request_stop();
        }
      }

      for (auto &r : m_runners_) {
        if (r.future.valid()) {
          r.future.wait();
        }
      }

      m_runners_.clear();
    }

    void runFrameLoop(frame_task_s &task, std::stop_token token, const std::string &iface, int64_t period_ms) {
      const int sock = openSocket(iface);
      if (sock < 0) {
        warn(fmt::format("frame PGN {}: cannot open SocketCAN on '{}', sender exited", task.pgn, iface));
        return;
      }

      const auto origin = std::chrono::steady_clock::now();

      while (!token.stop_requested()) {
        const auto now = std::chrono::steady_clock::now();
        const int64_t elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - origin).count();

        std::vector<uint8_t> payload(static_cast<size_t>(task.datalen), 0x00u);
        for (auto &slot : task.spns) {
          double v = slot.generator->nextValue(elapsed_ms);
          m_encoder_.encodeSpn(payload, slot.spec, v);
        }

        sendFrame(sock, task, payload);

        if (period_ms <= 0) {
          break; // one-shot
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(period_ms));
      }

      ::close(sock);
    }

    static int openSocket(const std::string &iface) {
      const int sock = ::socket(AF_CAN, SOCK_RAW, CAN_RAW);
      if (sock < 0) {
        return -1;
      }

      ifreq ifr{};
      std::strncpy(ifr.ifr_name, iface.c_str(), sizeof(ifr.ifr_name) - 1);
      if (::ioctl(sock, SIOCGIFINDEX, &ifr) < 0) {
        ::close(sock);
        return -1;
      }

      sockaddr_can addr{};
      addr.can_family = AF_CAN;
      addr.can_ifindex = ifr.ifr_ifindex;
      ::setsockopt(sock, SOL_CAN_RAW, CAN_RAW_FILTER, nullptr, 0);

      if (::bind(sock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        ::close(sock);
        return -1;
      }

      return sock;
    }

    static void sendFrame(int sock, const frame_task_s &task, const std::vector<uint8_t> &payload) {
      can_frame frame{
          .can_id =
              (((static_cast<uint32_t>(task.priority) & 0x7u) << 26u) |
               ((static_cast<uint32_t>(task.pgn) & 0x3FFFFu) << 8u) | (static_cast<uint32_t>(task.address) & 0xFFu)) |
              CAN_EFF_FLAG,
          .can_dlc = static_cast<uint8_t>(std::min<int32_t>(task.datalen, 8)),
      };

      std::memcpy(frame.data, payload.data(), std::min<size_t>(payload.size(), sizeof(frame.data)));
      ::write(sock, &frame, sizeof(frame));
    }

    void warn(const std::string &msg) {
      if (m_console_output_) {
        fmt::println(stderr, "Playback warning: {}", msg);
      }
    }

    static std::string readScalar(const YAML::Node &n, const char *key) {
      if (!n || !n[key] || n[key].IsNull()) {
        return {};
      }

      try {
        return n[key].as<std::string>();
      } catch (const YAML::Exception &) {
        return {};
      }
    }

    static std::string readString(const YAML::Node &n, const char *key, const std::string &fallback) {
      std::string v = readScalar(n, key);
      return v.empty() ? fallback : v;
    }

    static int64_t readInt(const YAML::Node &n, const char *key, int64_t fallback) {
      if (!n || !n[key] || n[key].IsNull()) {
        return fallback;
      }

      try {
        return n[key].as<int64_t>();
      } catch (const YAML::Exception &) {
      }

      try {
        std::string s = n[key].as<std::string>();
        return parseHexOrDecimal(s, fallback);
      } catch (const YAML::Exception &) {
        return fallback;
      }
    }

    static int32_t parseHexOrDecimal(const std::string &s, int32_t fallback) {
      if (s.empty()) {
        return fallback;
      }

      try {
        if (s.size() > 2 && (s[0] == '0') && (s[1] == 'x' || s[1] == 'X')) {
          return static_cast<int32_t>(std::stoll(s.substr(2), nullptr, 16));
        }

        return static_cast<int32_t>(std::stoll(s, nullptr, 10));
      } catch (const std::exception &) {
        return fallback;
      }
    }

    bool m_console_output_;
    bool m_stopped_ = false;
    can_encoder_handle_s m_encoder_;
    std::vector<frame_task_s> m_tasks_;
    std::vector<runner_s> m_runners_;
  };

  return std::make_shared<Impl>(yaml_path, db, console_output);
}
