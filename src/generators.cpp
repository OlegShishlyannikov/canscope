#include "generators.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>
#include <yaml-cpp/yaml.h>
#include <zlib.h>

#define FMT_HEADER_ONLY
#include <fmt/format.h>

#include "sqlite_modern_cpp.h"

namespace {

constexpr double kTwoPi = 6.283185307179586;

double yamlDouble(const YAML::Node &n, const char *key, double fallback) {
  if (!n || !n[key] || n[key].IsNull()) {
    return fallback;
  }

  try {
    return n[key].as<double>();
  } catch (const YAML::Exception &) {
    return fallback;
  }
}

int64_t yamlInt(const YAML::Node &n, const char *key, int64_t fallback) {
  if (!n || !n[key] || n[key].IsNull()) {
    return fallback;
  }

  try {
    return n[key].as<int64_t>();
  } catch (const YAML::Exception &) {
    return fallback;
  }
}

std::string yamlString(const YAML::Node &n, const char *key, std::string fallback = "") {
  if (!n || !n[key] || n[key].IsNull()) {
    return fallback;
  }

  try {
    return n[key].as<std::string>();
  } catch (const YAML::Exception &) {
    return fallback;
  }
}

double mapNormalisedToRange(double normalised, double lo, double hi) {
  return lo + (hi - lo) * normalised;
}

class ConstGenerator : public ValueGenerator {
public:
  ConstGenerator(double value) : m_value_(value) {}

  double nextValue(int64_t) override { return m_value_; }

private:
  double m_value_;
};

class RampGenerator : public ValueGenerator {
public:
  enum class mode_e { once, loop, pingpong };

  RampGenerator(double from, double to, int64_t duration_ms, mode_e mode)
      : m_from_(from), m_to_(to), m_duration_ms_(std::max<int64_t>(duration_ms, 1)), m_mode_(mode) {}

  double nextValue(int64_t elapsed_ms) override {
    if (m_mode_ == mode_e::once) {
      if (elapsed_ms >= m_duration_ms_) {
        return m_to_;
      }

      double t = static_cast<double>(elapsed_ms) / static_cast<double>(m_duration_ms_);
      return m_from_ + (m_to_ - m_from_) * t;
    }

    int64_t phase = elapsed_ms % m_duration_ms_;
    double t = static_cast<double>(phase) / static_cast<double>(m_duration_ms_);

    if (m_mode_ == mode_e::loop) {
      return m_from_ + (m_to_ - m_from_) * t;
    }

    int64_t cycle = (elapsed_ms / m_duration_ms_) % 2;
    if (cycle == 0) {
      return m_from_ + (m_to_ - m_from_) * t;
    }

    return m_to_ - (m_to_ - m_from_) * t;
  }

private:
  double m_from_, m_to_;
  int64_t m_duration_ms_;
  mode_e m_mode_;
};

class SineGenerator : public ValueGenerator {
public:
  SineGenerator(int64_t period_ms, double min, double max, double phase_deg)
      : m_period_ms_(std::max<int64_t>(period_ms, 1)), m_min_(min), m_max_(max),
        m_phase_rad_(phase_deg * kTwoPi / 360.0) {}

  double nextValue(int64_t elapsed_ms) override {
    double t = static_cast<double>(elapsed_ms) / static_cast<double>(m_period_ms_);
    double s = std::sin(kTwoPi * t + m_phase_rad_);
    return mapNormalisedToRange((s + 1.0) * 0.5, m_min_, m_max_);
  }

private:
  int64_t m_period_ms_;
  double m_min_, m_max_, m_phase_rad_;
};

class SquareGenerator : public ValueGenerator {
public:
  SquareGenerator(int64_t period_ms, double duty, double min, double max)
      : m_period_ms_(std::max<int64_t>(period_ms, 1)), m_duty_(std::clamp(duty, 0.0, 1.0)), m_min_(min), m_max_(max) {}

  double nextValue(int64_t elapsed_ms) override {
    int64_t phase = elapsed_ms % m_period_ms_;
    double t = static_cast<double>(phase) / static_cast<double>(m_period_ms_);
    return t < m_duty_ ? m_max_ : m_min_;
  }

private:
  int64_t m_period_ms_;
  double m_duty_, m_min_, m_max_;
};

class TriangleGenerator : public ValueGenerator {
public:
  TriangleGenerator(int64_t period_ms, double min, double max)
      : m_period_ms_(std::max<int64_t>(period_ms, 1)), m_min_(min), m_max_(max) {}

  double nextValue(int64_t elapsed_ms) override {
    int64_t phase = elapsed_ms % m_period_ms_;
    double t = static_cast<double>(phase) / static_cast<double>(m_period_ms_);
    double norm = (t < 0.5) ? (2.0 * t) : (2.0 * (1.0 - t));
    return mapNormalisedToRange(norm, m_min_, m_max_);
  }

private:
  int64_t m_period_ms_;
  double m_min_, m_max_;
};

class SawtoothGenerator : public ValueGenerator {
public:
  SawtoothGenerator(int64_t period_ms, double min, double max)
      : m_period_ms_(std::max<int64_t>(period_ms, 1)), m_min_(min), m_max_(max) {}

  double nextValue(int64_t elapsed_ms) override {
    int64_t phase = elapsed_ms % m_period_ms_;
    double t = static_cast<double>(phase) / static_cast<double>(m_period_ms_);
    return mapNormalisedToRange(t, m_min_, m_max_);
  }

private:
  int64_t m_period_ms_;
  double m_min_, m_max_;
};

class PulseGenerator : public ValueGenerator {
public:
  PulseGenerator(int64_t period_ms, int64_t pulse_ms, double min, double max)
      : m_period_ms_(std::max<int64_t>(period_ms, 1)), m_pulse_ms_(std::clamp<int64_t>(pulse_ms, 1, m_period_ms_)),
        m_min_(min), m_max_(max) {}

  double nextValue(int64_t elapsed_ms) override {
    return (elapsed_ms % m_period_ms_) < m_pulse_ms_ ? m_max_ : m_min_;
  }

private:
  int64_t m_period_ms_, m_pulse_ms_;
  double m_min_, m_max_;
};

class NoiseGenerator : public ValueGenerator {
public:
  NoiseGenerator(double min, double max, uint64_t seed) : m_min_(min), m_max_(max), m_rng_(seed), m_dist_(min, max) {}

  double nextValue(int64_t) override { return m_dist_(m_rng_); }

private:
  double m_min_, m_max_;
  std::mt19937_64 m_rng_;
  std::uniform_real_distribution<double> m_dist_;
};

class WalkGenerator : public ValueGenerator {
public:
  WalkGenerator(double step, double min, double max, uint64_t seed)
      : m_step_(std::abs(step)), m_min_(min), m_max_(max), m_current_((min + max) * 0.5), m_rng_(seed),
        m_dist_(-1.0, 1.0) {}

  double nextValue(int64_t) override {
    m_current_ += m_step_ * m_dist_(m_rng_);
    m_current_ = std::clamp(m_current_, m_min_, m_max_);
    return m_current_;
  }

private:
  double m_step_, m_min_, m_max_, m_current_;
  std::mt19937_64 m_rng_;
  std::uniform_real_distribution<double> m_dist_;
};

// Lazy-streams (ts_ms, value) samples from a previously recorded SQLite batches
// database produced by Recorder. Zero-order hold between samples; optionally
// loops at end of stream.
class ReplayGenerator : public ValueGenerator {
public:
  ReplayGenerator(std::string db_path, int32_t pgn, int32_t spn, bool loop, double speed)
      : m_db_path_(std::move(db_path)), m_pgn_(pgn), m_spn_(spn), m_loop_(loop),
        m_speed_(speed > 0.0 ? speed : 1.0), m_db_(m_db_path_) {
    advanceBatch();
  }

  double nextValue(int64_t elapsed_ms) override {
    int64_t target_rec_ms = static_cast<int64_t>(static_cast<double>(elapsed_ms) * m_speed_);

    while (true) {
      while (!m_samples_.empty() && m_samples_.front().rec_offset_ms <= target_rec_ms) {
        m_last_value_ = m_samples_.front().value;
        m_have_last_ = true;
        m_samples_.pop_front();
      }

      if (!m_samples_.empty()) {
        return m_have_last_ ? m_last_value_ : m_samples_.front().value;
      }

      if (!advanceBatch()) {
        if (m_loop_ && m_have_last_) {
          resetForLoop();
          continue;
        }

        return m_have_last_ ? m_last_value_ : 0.0;
      }
    }
  }

private:
  struct sample_s {
    int64_t rec_offset_ms;
    double value;
  };

  bool advanceBatch() {
    while (true) {
      auto blob = fetchNextBlob();
      if (!blob) {
        return false;
      }

      auto json_str = gunzip(*blob);
      if (json_str.empty()) {
        continue;
      }

      nlohmann::json arr;
      try {
        arr = nlohmann::json::parse(json_str);
      } catch (const nlohmann::json::exception &) {
        continue;
      }

      if (!arr.is_array()) {
        continue;
      }

      for (const auto &entry : arr) {
        if (!entry.contains("ts") || !entry.contains("spns")) {
          continue;
        }

        int64_t ts = entry["ts"].get<int64_t>();

        if (entry.contains("pgn")) {
          try {
            int32_t entry_pgn = entry["pgn"].get<int32_t>();
            if (entry_pgn != m_pgn_) {
              continue;
            }
          } catch (const nlohmann::json::exception &) {
          }
        }

        for (const auto &s : entry["spns"]) {
          if (!s.contains("spn") || !s.contains("value")) {
            continue;
          }

          int32_t entry_spn = 0;
          try {
            entry_spn = s["spn"].get<int32_t>();
          } catch (const nlohmann::json::exception &) {
            continue;
          }

          if (entry_spn != m_spn_) {
            continue;
          }

          double val = 0.0;
          try {
            val = s["value"].get<double>();
          } catch (const nlohmann::json::exception &) {
            continue;
          }

          if (m_ts_origin_ms_ == 0) {
            m_ts_origin_ms_ = ts;
          }

          m_samples_.push_back({ts - m_ts_origin_ms_, val});
        }
      }

      if (!m_samples_.empty()) {
        return true;
      }
    }
  }

  std::optional<std::vector<uint8_t>> fetchNextBlob() {
    std::optional<std::vector<uint8_t>> out;

    m_db_ << "SELECT id, data FROM batches WHERE id > ? ORDER BY id LIMIT 1;" << m_last_batch_id_ >>
        [&](int64_t id, const std::vector<uint8_t> &data) {
          m_last_batch_id_ = id;
          out = data;
        };

    return out;
  }

  static std::string gunzip(const std::vector<uint8_t> &in) {
    if (in.empty()) {
      return {};
    }

    z_stream strm{};
    if (inflateInit2(&strm, 15 | 16) != Z_OK) {
      return {};
    }

    strm.next_in = const_cast<Bytef *>(in.data());
    strm.avail_in = static_cast<uInt>(in.size());

    std::string out;
    uint8_t buf[16384];

    while (true) {
      strm.next_out = buf;
      strm.avail_out = sizeof(buf);

      int rc = inflate(&strm, Z_NO_FLUSH);
      size_t produced = sizeof(buf) - strm.avail_out;
      out.append(reinterpret_cast<char *>(buf), produced);

      if (rc == Z_STREAM_END) {
        break;
      }

      if (rc != Z_OK) {
        inflateEnd(&strm);
        return {};
      }
    }

    inflateEnd(&strm);
    return out;
  }

  void resetForLoop() {
    m_last_batch_id_ = 0;
    m_ts_origin_ms_ = 0;
    m_samples_.clear();
    advanceBatch();
  }

  std::string m_db_path_;
  int32_t m_pgn_, m_spn_;
  bool m_loop_;
  double m_speed_;
  sqlite::database m_db_;
  int64_t m_last_batch_id_ = 0;
  int64_t m_ts_origin_ms_ = 0;
  std::deque<sample_s> m_samples_;
  double m_last_value_ = 0.0;
  bool m_have_last_ = false;
};

RampGenerator::mode_e parseRampMode(const std::string &s) {
  if (s == "once") return RampGenerator::mode_e::once;
  if (s == "pingpong") return RampGenerator::mode_e::pingpong;
  return RampGenerator::mode_e::loop;
}

}  // namespace

// Factory. Builds a ValueGenerator from a YAML node, filling `err` on failure
// (and returning nullptr). `spn_min` / `spn_max` come from the SPN spec and
// serve as defaults for generators that accept a range.
std::unique_ptr<ValueGenerator> makeValueGenerator(const YAML::Node &node, double spn_min, double spn_max,
                                                   std::string &err) {
  err.clear();

  if (!node || node.IsNull()) {
    err = "generator: empty config";
    return nullptr;
  }

  const std::string type = yamlString(node, "type");
  if (type.empty()) {
    err = "generator: missing 'type'";
    return nullptr;
  }

  if (type == "const") {
    double v = yamlDouble(node, "value", spn_min);
    return std::make_unique<ConstGenerator>(v);
  }

  if (type == "ramp") {
    double from = yamlDouble(node, "from", spn_min);
    double to = yamlDouble(node, "to", spn_max);
    int64_t dur = yamlInt(node, "duration_ms", 1000);
    auto mode = parseRampMode(yamlString(node, "mode", "loop"));
    return std::make_unique<RampGenerator>(from, to, dur, mode);
  }

  if (type == "sine") {
    int64_t period = yamlInt(node, "period_ms", 1000);
    double min = yamlDouble(node, "min", spn_min);
    double max = yamlDouble(node, "max", spn_max);
    double phase = yamlDouble(node, "phase_deg", 0.0);
    return std::make_unique<SineGenerator>(period, min, max, phase);
  }

  if (type == "square") {
    int64_t period = yamlInt(node, "period_ms", 1000);
    double duty = yamlDouble(node, "duty", 0.5);
    double min = yamlDouble(node, "min", spn_min);
    double max = yamlDouble(node, "max", spn_max);
    return std::make_unique<SquareGenerator>(period, duty, min, max);
  }

  if (type == "triangle") {
    int64_t period = yamlInt(node, "period_ms", 1000);
    double min = yamlDouble(node, "min", spn_min);
    double max = yamlDouble(node, "max", spn_max);
    return std::make_unique<TriangleGenerator>(period, min, max);
  }

  if (type == "sawtooth") {
    int64_t period = yamlInt(node, "period_ms", 1000);
    double min = yamlDouble(node, "min", spn_min);
    double max = yamlDouble(node, "max", spn_max);
    return std::make_unique<SawtoothGenerator>(period, min, max);
  }

  if (type == "pulse") {
    int64_t period = yamlInt(node, "period_ms", 1000);
    int64_t pulse = yamlInt(node, "pulse_ms", period / 10);
    double min = yamlDouble(node, "min", spn_min);
    double max = yamlDouble(node, "max", spn_max);
    return std::make_unique<PulseGenerator>(period, pulse, min, max);
  }

  if (type == "noise") {
    double min = yamlDouble(node, "min", spn_min);
    double max = yamlDouble(node, "max", spn_max);
    uint64_t seed = static_cast<uint64_t>(yamlInt(node, "seed", std::random_device{}()));
    return std::make_unique<NoiseGenerator>(min, max, seed);
  }

  if (type == "walk") {
    double step = yamlDouble(node, "step", (spn_max - spn_min) * 0.01);
    double min = yamlDouble(node, "min", spn_min);
    double max = yamlDouble(node, "max", spn_max);
    uint64_t seed = static_cast<uint64_t>(yamlInt(node, "seed", std::random_device{}()));
    return std::make_unique<WalkGenerator>(step, min, max, seed);
  }

  if (type == "replay") {
    std::string db = yamlString(node, "db");
    if (db.empty()) {
      err = "generator replay: missing 'db'";
      return nullptr;
    }

    int32_t pgn = static_cast<int32_t>(yamlInt(node, "pgn", 0));
    int32_t spn = static_cast<int32_t>(yamlInt(node, "spn", 0));
    bool loop = true;
    if (node["loop"]) {
      try {
        loop = node["loop"].as<bool>();
      } catch (const YAML::Exception &) {
      }
    }

    double speed = yamlDouble(node, "speed", 1.0);
    try {
      return std::make_unique<ReplayGenerator>(db, pgn, spn, loop, speed);
    } catch (const std::exception &e) {
      err = fmt::format("generator replay: {}", e.what());
      return nullptr;
    }
  }

  err = fmt::format("generator: unknown type '{}'", type);
  return nullptr;
}
