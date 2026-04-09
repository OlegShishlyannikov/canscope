#include "tuple.hpp"
#include <functional>
#include <boost/convert.hpp>
#include <boost/convert/stream.hpp>
#include <boost/functional/hash.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/signals2.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <future>
#include <nlohmann/json.hpp>
#include <ranges>
#include <unordered_map>

#define FMT_HEADER_ONLY
#include <fmt/format.h>
#include <fmt/ranges.h>

#include "signals.hpp"
#include "tagsettings.hpp"

class GraphId_ {
public:
  GraphId_(const std::string &name, const std::string &uuid = generateUUID_()) : m_hash_(std::hash<std::string>()(uuid)), m_name_(name) {}
  ~GraphId_() = default;

  bool operator==(const GraphId_ &other) const { return m_hash_ == other.hash(); }

  size_t hash() const { return m_hash_; }
  const std::string &name() const { return m_name_; }

private:
  static std::string generateUUID_() { return boost::lexical_cast<std::string>(boost::uuids::random_generator()()); };
  const size_t m_hash_;
  const std::string m_name_;
};

template <> struct std::hash<GraphId_> {
  std::size_t operator()(const GraphId_ &g) const noexcept { return g.hash(); }
};

ftxui::Component makeGraph(const std::string &canid, const struct spn_settings_s &settings, std::function<std::vector<uint8_t>()> data_fn, ftxui::ScreenInteractive *screen,
                           signals_map_t &smap) {
  static const auto json_array_to_payload = [](const nlohmann::json &settings, const std::vector<std::string> &strvec_bytes) {
    std::vector<uint8_t> res;

    if (settings.contains("size") && settings.contains("pos") && settings.contains("le")) {
      boost::cnv::cstream converter;
      size_t size_setting = settings["size"].get<size_t>();
      res.reserve(size_setting);
      converter(std::hex)(std::skipws);
      auto f = apply<int>(std::ref(converter)).value_or(-1);

      if (auto [pos, size, data_size] =
              std::tuple<int32_t, int32_t, size_t>{
                  settings["pos"].get<int32_t>(),
                  size_setting,
                  strvec_bytes.size(),
              };
          pos < data_size && (pos + size) < data_size) {

        {
          namespace rv = std::ranges::views;
          const auto push = [&](const auto &i) { res.push_back(boost::lexical_cast<int32_t>(f(strvec_bytes[i]))); };

          auto seq = rv::iota(pos, pos + size);
          if (settings["le"].get<bool>()) {
            for (const auto &i : seq) {

              push(i);
            }
          } else {
            for (const auto &i : seq | rv::reverse) {

              push(i);
            }
          }
        }
      }
    }

    return res;
  };

  static const auto create_json = [](const std::vector<uint8_t> &data, const struct spn_settings_s &settings) {
    nlohmann::json array = nlohmann::json::array(), settings_json, json;
    uint32_t crc = std::accumulate(data.begin(), data.end(), uint32_t{});
    double spn_val = 0.0f;

    for (auto &d : data) {
      array.push_back(fmt::format("{:2x}", d));
    }

    json = {{"crc", crc}, {"data", array}};

    {
      std::string current_setting;
      try {

        tp::for_each(
            std::tuple{
                std::make_tuple("name", &settings.spn_name, static_cast<std::string *>(nullptr)),
                std::make_tuple("le", &settings.le, static_cast<bool *>(nullptr)),
                std::make_tuple("size", &settings.size, static_cast<size_t *>(nullptr)),
                std::make_tuple("offset", &settings.offset, static_cast<double *>(nullptr)),
                std::make_tuple("pos", &settings.pos, static_cast<size_t *>(nullptr)),
                std::make_tuple("x", &settings.x_coeff, static_cast<double *>(nullptr)),
                std::make_tuple("y", &settings.y_coeff, static_cast<double *>(nullptr)),
                std::make_tuple("discrete", &settings.discrete, static_cast<bool *>(nullptr)),
                std::make_tuple("bit_offset", &settings.bit_offset, static_cast<size_t *>(nullptr)),
                std::make_tuple("bit_count", &settings.bit_count, static_cast<size_t *>(nullptr)),
                std::make_tuple("uuid", &settings.uuid, static_cast<std::string *>(nullptr)),
            },

            [&](const auto &e) {
              current_setting = std::get<0u>(e);
              settings_json[std::get<0u>(e)] = boost::lexical_cast<std::remove_cvref_t<decltype(*std::get<2u>(e))>>(*std::get<1u>(e));
            });

        current_setting.clear();

        std::vector<std::string> v = json["data"].is_array() ? json["data"].template get<std::vector<std::string>>() : std::vector<std::string>{};
        std::vector<uint8_t> bytes = json_array_to_payload(settings_json, v);

        json["payload_bytes"] = [&]() {
          nlohmann::json::array_t array;
          for (const auto &byte : bytes) {
            array.push_back(fmt::format("{:2x}", byte));
          }

          return array;
        }();

        int32_t integer = 0;

        for (auto iter = int32_t{0}; const auto &b : bytes) {
          integer |= b << ((iter++) * UINT8_WIDTH);
        }

        if (settings_json["x"].get<double>() / settings_json["y"].get<double>() != NAN) {
          spn_val = static_cast<double>(integer) * settings_json["x"].get<double>() / settings_json["y"].get<double>() + settings_json["offset"].get<double>();
        }

        json["spn_value"] = spn_val;
      } catch (const boost::bad_lexical_cast &e) {

        settings_json["warning"] = fmt::format("Bad '{}' setting", current_setting);
      }
    }

    return json;
  };

  class Impl : public ftxui::ComponentBase {
  public:
    explicit Impl(const std::string &canid, const struct spn_settings_s &settings, std::function<std::vector<uint8_t>()> data_fn, ftxui::ScreenInteractive *screen, signals_map_t &smap)
        : m_settings_(settings), m_data_fn_(std::move(data_fn)) {

      auto renderer = ftxui::Renderer([this]() {
        auto data = m_data_fn_();
        auto json = create_json(data, m_settings_);
        return ftxui::vbox({
            ftxui::text(fmt::format("Raw data: {}", json["data"].dump())) | ftxui::color(ftxui::Color::Cyan) | ftxui::hcenter,
            ftxui::text(fmt::format("CRC: {}", json["crc"].dump())) | ftxui::color(ftxui::Color::Cyan) | ftxui::hcenter,
            ftxui::text(fmt::format("Payload: {}", json.contains("payload_bytes") ? json["payload_bytes"].dump() : "[]")) | ftxui::color(ftxui::Color::Cyan) |
                ftxui::hcenter,

            ftxui::hbox({
                ftxui::text("Value: ") | ftxui::bold,
                ftxui::text(fmt::format("{:.6g}", json.contains("tag_value") ? json["tag_value"].template get<double>() : 0.0f)) |
                    ftxui::color(ftxui::Color::IndianRed),
            }) | ftxui::hcenter,
        });
      });

      Add(renderer);
    }

  private:
    spn_settings_s m_settings_;
    std::function<std::vector<uint8_t>()> m_data_fn_;
  };

  return ftxui::Make<Impl>(canid, settings, std::move(data_fn), screen, smap);
}
