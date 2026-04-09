#pragma once

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
#include <map>
#include <nlohmann/json.hpp>

#define FMT_HEADER_ONLY
#include <fmt/format.h>
#include <fmt/ranges.h>

#include "signals.hpp"
#include "tagsettings.hpp"

class SpnSettingRow : public ftxui::ComponentBase {
public:
  using spn_settings_map_t = std::map<std::string, std::map<int32_t, spn_settings_s>>;

  explicit SpnSettingRow(ftxui::ScreenInteractive *screen, signals_map_t &smap, const std::string &canid, ftxui::Component cmpCont, size_t tag_id,
                         spn_settings_map_t &tagSettingsMap) {
    Add(ftxui::Container::Vertical({
        ftxui::Container::Vertical({
            ftxui::Container::Horizontal({
                ftxui::Renderer([]() { return ftxui::text(fmt::format("{:32}", "Name: ")) | ftxui::bold | ftxui::color(ftxui::Color::Yellow); }),
                ftxui::Input({.content = &m_current_settings_.spn_name, .placeholder = "SPN name"}) | ftxui::vcenter | ftxui::xflex,
            }),

            ftxui::Container::Horizontal({
                ftxui::Renderer([]() { return ftxui::text(fmt::format("{:32}", "X coefficient: ")) | ftxui::bold | ftxui::color(ftxui::Color::Yellow); }),
                ftxui::Input({.content = &m_current_settings_.x_coeff, .placeholder = "X coefficient"}) | ftxui::vcenter | ftxui::xflex,
            }),

            ftxui::Container::Horizontal({
                ftxui::Renderer([]() { return ftxui::text(fmt::format("{:32}", "Y coefficient: ")) | ftxui::bold | ftxui::color(ftxui::Color::Yellow); }),
                ftxui::Input({.content = &m_current_settings_.y_coeff, .placeholder = "Y coefficient"}) | ftxui::vcenter | ftxui::xflex,
            }),

            ftxui::Container::Horizontal({
                ftxui::Renderer([]() { return ftxui::text(fmt::format("{:32}", "Offset: ")) | ftxui::bold | ftxui::color(ftxui::Color::Yellow); }),
                ftxui::Input({.content = &m_current_settings_.offset, .placeholder = "Offset"}) | ftxui::vcenter | ftxui::xflex,
            }),

            ftxui::Container::Horizontal({
                ftxui::Renderer([]() { return ftxui::text(fmt::format("{:32}", "Size: ")) | ftxui::bold | ftxui::color(ftxui::Color::Yellow); }),
                ftxui::Input({.content = &m_current_settings_.size, .placeholder = "Size"}) | ftxui::vcenter | ftxui::xflex,
            }),

            ftxui::Container::Horizontal({
                ftxui::Renderer([]() { return ftxui::text(fmt::format("{:32}", "Pos: ")) | ftxui::bold | ftxui::color(ftxui::Color::Yellow); }),
                ftxui::Input({.content = &m_current_settings_.pos, .placeholder = "Pos"}) | ftxui::vcenter | ftxui::xflex,
            }),

            ftxui::Container::Horizontal({
                ftxui::Renderer([]() { return ftxui::text(fmt::format("{:32}", "Bit offset: ")) | ftxui::bold | ftxui::color(ftxui::Color::Yellow); }),
                ftxui::Input({.content = &m_current_settings_.bit_offset, .placeholder = "Bit offset"}) | ftxui::vcenter | ftxui::xflex,
            }),

            ftxui::Container::Horizontal({
                ftxui::Renderer([]() { return ftxui::text(fmt::format("{:32}", "Bit count: ")) | ftxui::bold | ftxui::color(ftxui::Color::Yellow); }),
                ftxui::Input({.content = &m_current_settings_.bit_count, .placeholder = "Bit count"}) | ftxui::vcenter | ftxui::xflex,
            }),

            ftxui::Checkbox({
                .label = fmt::format("{:32}", "Little endian"),
                .checked = &m_current_settings_.le,
            }) | ftxui::vcenter |
                ftxui::xflex,

            ftxui::Checkbox({
                .label = fmt::format("{:32}", "Discrete"),
                .checked = &m_current_settings_.discrete,
            }) | ftxui::vcenter |
                ftxui::xflex,
        }) | ftxui::xflex,
    }));
  }

  const spn_settings_s &exportSettings() const { return m_current_settings_; }

private:
  spn_settings_s m_current_settings_;
};
