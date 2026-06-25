#include "can_data.hpp"
#include "enum_tables.hpp"
#include <nlohmann/json.hpp>

#define FMT_HEADER_ONLY
#include <fmt/format.h>
#include <fmt/ranges.h>

// For sqlite
#include "sqlite_modern_cpp.h"

std::pair<nlohmann::json, nlohmann::json> processFrame(sqlite::database &db, const std::string &iface,
                                                       const std::string &canid, const std::vector<uint8_t> &data) {
  nlohmann::json verbose, brief;

  db << R"(SELECT pgn, pg_label, pg_acronym, pg_descr, edp, dp, pf, ps, pg_datalen, pg_priority, repeat_offset, repeat_stride FROM pgns WHERE pgn = ?;)"
     << [&]() -> int32_t {
    int32_t ret;
    std::stringstream{} << std::hex << canid.substr(2, 4) >> ret;
    return ret;
  }() >> [&](int32_t pgn, const std::string &pg_label, const std::string &pg_acronym, const std::string &pg_descr,
             int32_t edp, int32_t dp, int32_t pf, int32_t ps, int32_t pd_datalen, int32_t pg_priority,
             int32_t repeat_offset, int32_t repeat_stride) {
    int32_t pgn_int;
    std::stringstream{} << std::hex << canid.substr(2, 4) >> pgn_int;

    // Make header
    verbose = {
        {"Interface", iface},    {"Can ID", canid},   {"PGN", canid.substr(2, 4)}, {"PGN (integer)", pgn_int},
        {"Acronym", pg_acronym}, {"Label", pg_label}, {"Description", pg_descr},
    };

    // Get SPNs
    nlohmann::json::array_t spns_array;
    db << R"(SELECT spn, spn_name, spn_pos, spn_length, resolution, offset, data_range, min_value, max_value, units, slot_id, slot_name, spn_type, value_encoding, in_repeat_group FROM spns WHERE pgn = ?;)"
       << pgn_int >>
        [&](int32_t spn, const std::string &spn_name, const std::string &spn_pos, int32_t spn_length, double resolution,
            int32_t offset, const std::string &data_range, double min, double max, const std::string &unit,
            const std::string &slot_id, const std::string &slot_name, const std::string &spn_type,
            const std::string &value_encoding, int32_t in_repeat_group) {
          auto decode_one = [&](int32_t delta_bytes) -> nlohmann::json {
            nlohmann::json spn_json = {
                {"SPN (integer)", spn},
                {"SPN name", spn_name},
                {"SPN position", spn_pos},
                {"SPN length (bits)", spn_length},
                {"Resolution", resolution},
                {"Offset", offset},
                {"Minimum value", min},
                {"Maximum value", max},
                {"Unit", unit},
                {"SLOT id", slot_id},
                {"SPN type", spn_type},
                {"Encoding", value_encoding},
            };

            size_t result = 0u, iter = 0u, total_size_bits = 0u;
            std::vector<uint8_t> ascii_bytes;
            nlohmann::json::array_t parts_array;

            db << "SELECT byte_offset,bit_offset,size FROM spn_fragments WHERE spn = ? AND pgn = ?" << spn << pgn >>
                [&, byte_array = data, delta_bytes](int32_t byte_offset, int32_t bit_offset, int32_t size_bits) {
                  byte_offset += delta_bytes;

                  if (value_encoding == "ascii") {
                    const size_t nbytes = size_bits == 0 ? (byte_offset < static_cast<int32_t>(byte_array.size())
                                                                ? byte_array.size() - byte_offset
                                                                : 0)
                                                         : size_bits / UINT8_WIDTH;

                    for (size_t i = 0; i < nbytes && (byte_offset + i) < byte_array.size(); ++i) {
                      ascii_bytes.push_back(byte_array[byte_offset + i]);
                    }

                    // Report the actually extracted size rather than the sentinel.
                    const int32_t effective_size_bits = static_cast<int32_t>(nbytes * UINT8_WIDTH);
                    parts_array.push_back(nlohmann::json::parse(fmt::format(
                        R"({{"{}":{{"byte_offset":{},"bit_offset":{},"size_bits":{},"parse_result":"ascii"}}}})",
                        fmt::format("Fragment#{}", iter++), byte_offset, bit_offset, effective_size_bits)));
                    return;
                  }

                  size_t fragment_value = 0;
                  for (uint32_t i = 0; i < ((size_bits / UINT8_WIDTH) + (size_bits % UINT8_WIDTH ? 1 : 0)); ++i) {
                    if (byte_offset + static_cast<int32_t>(i) >= static_cast<int32_t>(byte_array.size())) {
                      break; // shifted past frame end (last group truncated) — leave bits as 0
                    }

                    const uint8_t &byte = byte_array[byte_offset + i];
                    fragment_value |= (((byte << (i * UINT8_WIDTH)) &
                                        (size_bits % UINT8_WIDTH
                                             ? ~((0xff << (size_bits % UINT8_WIDTH + bit_offset + i * UINT8_WIDTH)) |
                                                 ~(0xff << (bit_offset + i * UINT8_WIDTH)))
                                             : (0xff << (size_bits % UINT8_WIDTH + bit_offset + i * UINT8_WIDTH)) |
                                                   ~(0xff << (bit_offset + i * UINT8_WIDTH)))) >>
                                       (bit_offset));
                  }

                  result |= fragment_value << total_size_bits;
                  total_size_bits += size_bits;

                  parts_array.push_back(nlohmann::json::parse(
                      fmt::format(R"({{"{}":{{"byte_offset":{},"bit_offset":{},"size_bits":{},"parse_result":"{}"}}}})",
                                  fmt::format("Fragment#{}", iter++), byte_offset, bit_offset, size_bits,
                                  fmt::format("{:#x}", result))));
                };

            spn_json["Fragments"] = parts_array;
            if (value_encoding == "ascii") {
              std::string text;
              text.reserve(ascii_bytes.size());

              for (uint8_t b : ascii_bytes) {
                text += (b >= 0x20 && b < 0x7f) ? static_cast<char>(b) : '.';
              }

              spn_json["Value"] = text;

              if (spn_length == 0) {
                spn_json["SPN length (bits)"] = static_cast<int32_t>(ascii_bytes.size() * UINT8_WIDTH);
              }
            } else {
              const double numeric_value = result * resolution + offset;
              spn_json["Value"] = numeric_value;
              constexpr std::string_view kTablePrefix = "table:";

              if (std::string_view{value_encoding}.starts_with(kTablePrefix)) {
                const std::string_view table_name = std::string_view{value_encoding}.substr(kTablePrefix.size());
                const auto label = enum_tables::lookup(table_name, static_cast<int32_t>(numeric_value));

                if (!label.empty()) {
                  spn_json["Value Label"] = std::string(label);
                }
              }
            }

            return spn_json;
          };

          if (in_repeat_group != 0 && repeat_stride > 0) {
            const int32_t start_zero = std::max(0, repeat_offset - 1);
            const int32_t available = static_cast<int32_t>(data.size()) - start_zero;
            const int32_t n_groups = available > 0 ? (available / repeat_stride) : 0;

            for (int32_t i = 0; i < n_groups; ++i) {
              spns_array.push_back(decode_one(i * repeat_stride));
            }
          } else {
            spns_array.push_back(decode_one(0));
          }
        };

    verbose["SPNs"] = spns_array;
  };

  if (!verbose.is_null()) {
    brief = {
        {"PGN", verbose["PGN"]},
        {"Label", verbose["Label"]},
        {"Acronym", verbose["Acronym"]},
    };

    nlohmann::json::array_t spns_array;
    for (const auto &spn : verbose["SPNs"]) {
      const auto name = spn["SPN name"].get<std::string>();
      const auto unit = spn["Unit"].get<std::string>();
      const auto encoding = spn.value("Encoding", std::string{"numeric"});

      if (encoding == "ascii") {

        spns_array.push_back(fmt::format("{}: \"{}\"", name, spn["Value"].get<std::string>()));
      } else if (encoding == "binary") {

        spns_array.push_back(fmt::format("{}: {}", name, spn["Value"].get<double>() != 0.0 ? "yes" : "no"));
      } else if (std::string_view{encoding}.starts_with("table:")) {
        const auto label = spn.value("Value Label", std::string{});

        if (!label.empty()) {
          spns_array.push_back(fmt::format("{}: {:.0f} ({})", name, spn["Value"].get<double>(), label));
        } else {
          spns_array.push_back(fmt::format("{}: {:.0f}", name, spn["Value"].get<double>()));
        }
      } else {
        spns_array.push_back(fmt::format("{}: {:.6g} {}", name, spn["Value"].get<double>(), unit));
      }
    }

    brief["SPNs"] = spns_array;
  }

  return {verbose, brief};
}

nlohmann::json verboseToExportJson(const nlohmann::json &verbose) {
  nlohmann::json::array_t spns;

  if (verbose.is_null() || !verbose.contains("SPNs"))
    return spns;

  std::string pgn = verbose.contains("PGN") ? verbose["PGN"].get<std::string>() : "";

  for (const auto &v : verbose["SPNs"]) {
    nlohmann::json spn = {
        {"name", v.value("SPN name", "")},          {"offset", v.value("Offset", 0)},
        {"resolution", v.value("Resolution", 0.0)}, {"max", v.value("Maximum value", 0.0)},
        {"min", v.value("Minimum value", 0.0)},     {"pgn", pgn},
        {"value", v.value("Value", 0.0)},           {"unit", v.value("Unit", "")},
    };

    nlohmann::json::array_t frags;

    if (v.contains("Fragments")) {
      for (const auto &[k, frag] : v["Fragments"].items()) {
        auto frag_key = fmt::format("Fragment#{}", k);

        if (frag.contains(frag_key)) {
          frags.push_back({{
              fmt::format("fragment#{}", k),
              {
                  {"byte_pos", frag[frag_key].value("byte_offset", 0)},
                  {"bit_pos", frag[frag_key].value("bit_offset", 0)},
                  {"bit_size", frag[frag_key].value("size_bits", 0)},
              },
          }});
        }
      }
    }

    spn["fragments"] = std::move(frags);
    spns.push_back(std::move(spn));
  }

  return spns;
}
