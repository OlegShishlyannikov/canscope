// #include <algorithm>
// #include <cmath>
#include <cstdint>
#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>
#include <nlohmann/json_fwd.hpp>

#include "canid_unit.hpp"
#include "process.hpp"
#include "tagsettings.hpp"
#include "json/json.hpp"

// For sqlite
// #include "sqlite_modern_cpp.h"
#include "src/json/expander.hpp"

extern ftxui::Component makeSpnSettingsForm(ftxui::ScreenInteractive *, signals_map_t &, const std::string &,
                                            std::string &, ftxui::Component, ftxui::Component, bool &,
                                            std::map<std::string, std::map<int32_t, ftxui::Component>> &,
                                            spn_settings_map_t &);

ftxui::Component makeCanIDUnit(const std::string &iface, const std::string &canid, const std::string &protocol,
                               size_t &spn_count, const std::vector<uint8_t> &data, ftxui::ScreenInteractive *screen,
                               signals_map_t &smap, ftxui::Component content, ftxui::Component canids_container,
                               ftxui::Component spn_settings_dialog, ftxui::Component cansettings_dialog,
                               bool is_deployed, bool is_verbose, bool is_brief, bool is_manual,
                               std::string &canid_active, bool &custom_spn_settings_shown,
                               bool &canbus_parameters_export_shown, bool &filedialog_shown,
                               std::map<std::string, std::map<int32_t, ftxui::Component>> &spnSettingsFormMap,
                               spn_settings_map_t &spnSettingsMap) {
  return ftxui::Make<CanIDUnit>(iface, canid, protocol, spn_count, data, screen, smap, content, canids_container,
                                spn_settings_dialog, cansettings_dialog, is_deployed, is_verbose, is_brief, is_manual,
                                canid_active, custom_spn_settings_shown, canbus_parameters_export_shown,
                                filedialog_shown, spnSettingsFormMap, spnSettingsMap);
}

CanIDUnit::CanIDUnit(const std::string &iface, const std::string &canid, const std::string &protocol, size_t &spn_count,
                     const std::vector<uint8_t> &data, ftxui::ScreenInteractive *screen, signals_map_t &smap,
                     ftxui::Component content, ftxui::Component canids_container, ftxui::Component spn_settings_dialog,
                     ftxui::Component cansettings_dialog, bool is_deployed, bool is_verbose, bool is_brief,
                     bool is_manual, std::string &canid_active, bool &custom_spn_settings_shown,
                     bool &canbus_parameters_export_shown, bool &filedialog_shown,
                     std::map<std::string, std::map<int32_t, ftxui::Component>> &spnSettingsFormMap,
                     spn_settings_map_t &spnSettingsMap)

    : m_canid_(canid), m_iface_(iface), m_data_(data), m_deployed_(is_deployed), m_verbose_(is_verbose),
      m_brief_(is_brief), m_manual_mode_(is_manual),
      m_spnSettingsForm_(makeSpnSettingsForm(screen, smap, canid, canid_active, canids_container, spn_settings_dialog,
                                             custom_spn_settings_shown, spnSettingsFormMap, spnSettingsMap)) {

  m_cansettings_dialog_ = cansettings_dialog;
  m_spnSettingsMap_ = &spnSettingsMap;

  m_brief_content_ = ftxui::Container::Vertical({});
  m_verbose_content_ = ftxui::Container::Vertical({});

  auto arrow = ftxui::Checkbox({
      .checked = &m_deployed_,
      .transform = [this](const ftxui::EntryState &state) -> ftxui::Element {
        return ftxui::hbox({
            ftxui::text(m_deployed_ ? "▼ " : "▶ "),
        });
      },

      .on_change = [&, this]() { canid_active = m_canid_; },
  });

  auto contentbox = ftxui::Checkbox({
      .transform = [this](const ftxui::EntryState &state) -> ftxui::Element {
        ftxui::Elements line;

        // Interface
        line.push_back(ftxui::text(m_iface_ + " ") |
                       (m_diff_.is_new_interface ? (ftxui::color(ftxui::Color::Red) | ftxui::bold)
                                                 : ftxui::color(ftxui::Color::Aquamarine1) | ftxui::bold));

        // CAN ID
        line.push_back(ftxui::text(fmt::format("{:8} ", m_canid_)) |
                       (m_diff_.is_new_canid ? (ftxui::color(ftxui::Color::Red) | ftxui::bold)
                                             : ftxui::color(ftxui::Color::GreenLight) | ftxui::bold));

        // Size
        line.push_back(ftxui::text(fmt::format("{} ", m_data_.size())));

        // Padding for < 8 bytes
        for (size_t i = m_data_.size(); i < 8; ++i)
          line.push_back(ftxui::text("---- "));

        // Payload bytes with diff highlighting
        bool has_updates = false;
        for (size_t idx = 0; idx < m_data_.size(); ++idx) {
          bool changed = idx < m_diff_.payload_changed.size() && m_diff_.payload_changed[idx];

          if (changed) {
            has_updates = true;
          }

          line.push_back(ftxui::text(fmt::format("0x{:02X} ", m_data_[idx])) |
                         (changed ? (ftxui::color(ftxui::Color::Red) | ftxui::bold) : ftxui::nothing));
        }

        // Last update time
        line.push_back(
            ftxui::text(fmt::format("(updated: {})", m_last_update_time_)) |
            (has_updates ? (ftxui::color(ftxui::Color::Red) | ftxui::bold) : ftxui::color(ftxui::Color::Cyan)));

        auto row = ftxui::hbox(std::move(line));
        if (m_hovered_) {
          row = row | ftxui::bold | ftxui::bgcolor(ftxui::Color::Grey11);
        }

        return row | ftxui::reflect(m_box_);
      },

      .on_change =
          [&canid_active, this]() {
            canid_active = m_canid_;
            m_deployed_ = !m_deployed_;
          },
  });

  auto label = ftxui::Renderer([this, protocol]() -> ftxui::Element {
    if (m_data_verbose_->contains("Label")) {

      return ftxui::hbox({
          ftxui::text(fmt::format(" - {}", (*m_data_verbose_)["Label"].get<std::string>())) |
              ftxui::color(ftxui::Color::Magenta) | (m_deployed_ ? ftxui::bold : ftxui::nothing),
          ftxui::filler(),
          ftxui::text(fmt::format("{}", protocol.empty() ? "Unknown" : protocol)) | ftxui::color(ftxui::Color::Red),
      });
    } else {
      return ftxui::text("");
    }
  });

  Add({
      ftxui::Container::Vertical({
          ftxui::Container::Horizontal({
              arrow,
              contentbox,
              label | ftxui::flex,
          }) | ftxui::flex,

          ftxui::Maybe(
              ftxui::Container::Vertical({

                  // Tab switcher: <brief | verbose | manual>
                  ftxui::Container::Horizontal({
                      ftxui::Renderer([]() { return ftxui::text("  "); }),
                      ftxui::Checkbox({
                          .checked = &m_brief_,
                          .transform = [this](const ftxui::EntryState &state) -> ftxui::Element {
                            auto el = ftxui::hbox({
                                ftxui::text("<"),
                                ftxui::text("brief") | (m_brief_ ? ftxui::bold : ftxui::nothing) |
                                    ftxui::color(ftxui::Color::Cyan),
                                ftxui::text(" | "),
                            });

                            if (state.focused || state.active)
                              el = el | ftxui::bold | ftxui::bgcolor(ftxui::Color::Grey11);
                            return el;
                          },

                          .on_change =
                              [this]() {
                                m_brief_ = true;
                                m_verbose_ = false;
                                m_manual_mode_ = false;
                              },
                      }),

                      ftxui::Checkbox({
                          .checked = &m_verbose_,
                          .transform = [this](const ftxui::EntryState &state) -> ftxui::Element {
                            auto el = ftxui::text("verbose") | (m_verbose_ ? ftxui::bold : ftxui::nothing) |
                                      ftxui::color(ftxui::Color::Cyan);

                            if (state.focused || state.active) {
                              el = el | ftxui::bold | ftxui::bgcolor(ftxui::Color::Grey11);
                            }

                            return el;
                          },

                          .on_change =
                              [this]() {
                                m_brief_ = false;
                                m_verbose_ = true;
                                m_manual_mode_ = false;
                              },
                      }),

                      ftxui::Checkbox({
                          .checked = &m_manual_mode_,
                          .transform = [this](const ftxui::EntryState &state) -> ftxui::Element {
                            auto el = ftxui::hbox({
                                ftxui::text(" | "),
                                ftxui::text("manual") | (m_manual_mode_ ? ftxui::bold : ftxui::nothing) |
                                    ftxui::color(ftxui::Color::Cyan),
                                ftxui::text(">"),
                            });

                            if (state.focused || state.active) {
                              el = el | ftxui::bold | ftxui::bgcolor(ftxui::Color::Grey11);
                            }

                            return el;
                          },

                          .on_change =
                              [this]() {
                                m_brief_ = false;
                                m_verbose_ = false;
                                m_manual_mode_ = true;
                              },
                      }),

                  }),

                  // Brief content
                  ftxui::Maybe(m_brief_content_ | ftxui::flex, &m_brief_),

                  // Verbose content
                  ftxui::Maybe(m_verbose_content_ | ftxui::flex, &m_verbose_),

                  // Manual mode content
                  ftxui::Maybe(m_spnSettingsForm_ | ftxui::flex, &m_manual_mode_),

              }) | ftxui::border,

              &m_deployed_),
      }),
  });
}

bool CanIDUnit::OnEvent(ftxui::Event event) {
  if (event.is_mouse()) {
    m_hovered_ = m_box_.Contain(event.mouse().x, event.mouse().y);
  }

  return ftxui::ComponentBase::OnEvent(event);
}

void CanIDUnit::update(const can_frame_data_s &data, const can_frame_diff_s &diff,
                       std::shared_ptr<nlohmann::json> verbose, std::shared_ptr<nlohmann::json> brief) {
  m_data_ = data.payload;
  m_diff_ = diff;

  // Update timestamp
  auto t = std::time(nullptr);
  struct tm tm_buf;
  localtime_r(&t, &tm_buf);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%d-%m-%Y %H-%M-%S", &tm_buf);
  m_last_update_time_ = buf;

  bool was_null = m_data_short_->is_null();

  if (verbose) {
    *m_data_verbose_ = std::move(*verbose);
  }

  if (brief) {
    *m_data_short_ = std::move(*brief);
  }

  // Ensure skeleton verbose/brief exist for custom SPN injection
  if (m_spnSettingsMap_ && m_spnSettingsMap_->contains(m_canid_) && !(*m_spnSettingsMap_)[m_canid_].empty()) {
    if (m_data_verbose_->is_null()) {
      *m_data_verbose_ = nlohmann::json{{"SPNs", nlohmann::json::array()}};
    }

    if (m_data_short_->is_null()) {
      *m_data_short_ = nlohmann::json{{"SPNs", nlohmann::json::array()}};
    }

    if (!m_data_verbose_->contains("SPNs")) {
      (*m_data_verbose_)["SPNs"] = nlohmann::json::array();
    }

    if (!m_data_short_->contains("SPNs")) {
      (*m_data_short_)["SPNs"] = nlohmann::json::array();
    }

    for (const auto &[tag_id, settings] : (*m_spnSettingsMap_)[m_canid_]) {
      double resolution = 1.0, offset_val = 0.0;
      try {
        resolution = std::stod(settings.resolution.empty() ? settings.x_coeff : settings.resolution);
      } catch (...) {
      }

      try {
        offset_val = std::stod(settings.offset);
      } catch (...) {
      }

      int64_t result = 0;
      size_t total_bits = 0;
      for (const auto &frag : settings.fragments) {
        int32_t bo = 0, bi = 0, bc = 0;

        try {
          bo = std::stoi(frag.byte_offset);
        } catch (...) {
        }

        try {
          bi = std::stoi(frag.bit_offset);
        } catch (...) {
        }

        try {
          bc = std::stoi(frag.bit_count);
        } catch (...) {
        }

        int32_t byte_cnt = (bc + bi + UINT8_WIDTH - 1) / UINT8_WIDTH;
        if (bc > 0 && bo >= 0 && static_cast<size_t>(bo + byte_cnt) <= m_data_.size()) {
          int64_t frag_val = 0;

          for (int32_t i = 0; i < byte_cnt; ++i) {
            frag_val |= static_cast<int64_t>(m_data_[bo + i]) << (i * UINT8_WIDTH);
          }

          frag_val = (frag_val >> bi) & ((1LL << bc) - 1);
          result |= frag_val << total_bits;
          total_bits += bc;
        }
      }
      if (settings.big_endian && total_bits > 8) {
        int64_t swapped = 0;
        size_t total_bytes = (total_bits + 7) / 8;
        for (size_t i = 0; i < total_bytes; ++i)
          swapped |= ((result >> (i * 8)) & 0xFF) << ((total_bytes - 1 - i) * 8);
        result = swapped;
      }
      double spn_val = static_cast<double>(result) * resolution + offset_val;

      nlohmann::json::array_t frags_json;
      for (size_t fi = 0; fi < settings.fragments.size(); ++fi) {
        const auto &f = settings.fragments[fi];
        int32_t bo = 0, bi = 0, bc = 0;

        try {
          bo = std::stoi(f.byte_offset);
        } catch (...) {
        }

        try {
          bi = std::stoi(f.bit_offset);
        } catch (...) {
        }

        try {
          bc = std::stoi(f.bit_count);
        } catch (...) {
        }

        frags_json.push_back(
            {{fmt::format("Fragment#{}", fi), {{"byte_offset", bo}, {"bit_offset", bi}, {"size_bits", bc}}}});
      }

      nlohmann::json custom_spn = {
          {"SPN (integer)", -static_cast<int32_t>(tag_id)},
          {"SPN name", settings.spn_name + " (custom)"},
          {"Value", spn_val},
          {"Unit", settings.unit},
          {"Resolution", resolution},
          {"Offset", offset_val},
          {"Fragments", frags_json},
      };

      if (m_data_verbose_->contains("SPNs")) {
        (*m_data_verbose_)["SPNs"].push_back(custom_spn);
      }

      if (!m_data_short_->is_null() && m_data_short_->contains("SPNs")) {
        (*m_data_short_)["SPNs"].push_back(
            fmt::format("{}: {:.6g} {}", settings.spn_name + " (custom)", spn_val, settings.unit));
      }
    }
  }

  // Rebuild FromLive when structure changes (first data or SPN count changed)
  size_t verbose_spn_count =
      (!m_data_verbose_->is_null() && m_data_verbose_->contains("SPNs")) ? (*m_data_verbose_)["SPNs"].size() : 0;
  size_t brief_spn_count =
      (!m_data_short_->is_null() && m_data_short_->contains("SPNs")) ? (*m_data_short_)["SPNs"].size() : 0;
  bool structure_changed = verbose_spn_count != m_last_verbose_spn_count_ || brief_spn_count != m_last_brief_spn_count_;

  if (structure_changed || (was_null && !m_data_short_->is_null())) {
    m_brief_content_->DetachAllChildren();
    if (!m_data_short_->is_null()) {
      m_brief_content_->Add(FromLive(m_data_short_, nlohmann::json::json_pointer(), true, -100, ExpanderImpl::Root()));
    }

    m_verbose_content_->DetachAllChildren();
    if (!m_data_verbose_->is_null()) {
      m_verbose_content_->Add(
          FromLive(m_data_verbose_, nlohmann::json::json_pointer(), true, -100, ExpanderImpl::Root()));
    }

    m_last_verbose_spn_count_ = verbose_spn_count;
    m_last_brief_spn_count_ = brief_spn_count;
  }

  // Populate export dialog once when verbose data first becomes available
  if (!m_data_short_->is_null() && !m_data_verbose_->is_null() && m_cansettings_dialog_) {
    if (!s_canbus_parameters_export_map_.contains(m_canid_)) {
      s_canbus_parameters_export_map_.insert({m_canid_, {false, false, {}}});
    }

    if (!std::get<1u>(s_canbus_parameters_export_map_[m_canid_])) {
      std::get<1u>(s_canbus_parameters_export_map_[m_canid_]) = true;
      m_export_selectors_ = ftxui::Container::Vertical({});
      auto &selectors_container = m_export_selectors_;

      for (const auto &[pgn_key, pgn_val] : m_data_verbose_->items()) {
        if (pgn_key == "SPNs") {
          for (const auto &[spns_arr_k, spns_arr_v] : pgn_val.items()) {
            for (const auto &[spn_k, spn_v] : spns_arr_v.items()) {
              if (spn_k == "SPN name") {
                std::string spn_name = spn_v.get<std::string>();
                if (spn_name.find("(custom)") != std::string::npos)
                  continue;
                auto &spn_map = std::get<2u>(s_canbus_parameters_export_map_[m_canid_]);

                spn_map.insert_or_assign(spn_name, std::make_tuple(false, false, spns_arr_v));
                selectors_container
                    ->Add(
                        ftxui::Container::Vertical(
                            {
                                ftxui::Container::Horizontal(
                                    {
                                        ftxui::Checkbox({
                                            .checked = &std::get<0u>(spn_map[spn_name]),
                                            .transform = [this](const ftxui::EntryState &state) -> ftxui::Element {
                                              return ftxui::hbox({ftxui::separatorEmpty(), ftxui::separatorEmpty(),
                                                                  ftxui::text(state.state ? "▼ " : "▶ ")});
                                            },
                                        }),

                                        ftxui::Checkbox(
                                            {
                                                .checked = &std::get<1u>(spn_map[spn_name]),
                                                .transform =
                                                    [spn_name](const ftxui::EntryState state) {
                                                      return ftxui::hbox({
                                                                 ftxui::text(
                                                                     fmt::format("[{}] ", state.state ? "X" : "")) |
                                                                     (state.state ? ftxui::color(ftxui::Color::Red)
                                                                                  : ftxui::color(ftxui::Color::Cyan)),
                                                                 ftxui::text(spn_name),
                                                             }) |
                                                             (state.focused ? ftxui::bold : ftxui::nothing) |
                                                             (state.focused ? ftxui::bgcolor(ftxui::Color::Grey11)
                                                                            : ftxui::nothing) |
                                                             ftxui::flex;
                                                    },
                                            }),
                                    }),

                                ftxui::Maybe(
                                    ftxui::Container::Horizontal({
                                        ftxui::Renderer([]() {
                                          return ftxui::hbox({ftxui::separatorEmpty(), ftxui::separatorEmpty(),
                                                              ftxui::separatorEmpty(), ftxui::separatorEmpty()});
                                        }),
                                        FromLive(m_data_verbose_, nlohmann::json::json_pointer("/SPNs/" + spns_arr_k),
                                                 false, -100, ExpanderImpl::Root()),
                                    }),
                                    &std::get<0u>(spn_map[spn_name])),
                            }));
              }
            }
          }
        }
      }

      auto container = ftxui::Container::Vertical({});
      container->Add(ftxui::Container::Horizontal({
          ftxui::Checkbox({
              .checked = &std::get<0u>(s_canbus_parameters_export_map_[m_canid_]),
              .transform = [this](const ftxui::EntryState &state) -> ftxui::Element {
                return ftxui::hbox(
                    {ftxui::text(std::get<0u>(s_canbus_parameters_export_map_[m_canid_]) ? "▼ " : "▶ ")});
              },
          }),

          ftxui::Checkbox({
              .checked = &std::get<0u>(s_canbus_parameters_export_map_[m_canid_]),
              .transform = [this](const ftxui::EntryState &state) -> ftxui::Element {
                auto &spn_map = std::get<2u>(s_canbus_parameters_export_map_[m_canid_]);
                size_t selected_cnt = std::count_if(spn_map.begin(), spn_map.end(),
                                                    [](auto &e) -> bool { return std::get<1u>(e.second); });
                std::string label =
                    m_data_short_->contains("Label") ? (*m_data_short_)["Label"].get<std::string>() : "";

                return ftxui::hbox({
                           ftxui::text(fmt::format("{:8}", fmt::format("[{}/{}] ", selected_cnt, spn_map.size()))) |
                               (state.focused ? ftxui::bold : ftxui::nothing) | ftxui::color(ftxui::Color::LightGreen),
                           ftxui::text(fmt::format("{} ", m_iface_)) | (state.focused ? ftxui::bold : ftxui::nothing) |
                               ftxui::color(ftxui::Color::Cyan),
                           ftxui::text(fmt::format("{} ", m_canid_)) | (state.focused ? ftxui::bold : ftxui::nothing) |
                               ftxui::color(ftxui::Color::LightGreen),
                           ftxui::text(fmt::format("- {} ", label)) | (state.focused ? ftxui::bold : ftxui::nothing) |
                               ftxui::color(ftxui::Color::Magenta),
                           ftxui::filler(),
                           ftxui::text("J1939 ") | (state.focused ? ftxui::bold : ftxui::nothing) |
                               ftxui::color(ftxui::Color::Red),
                       }) |
                       (state.focused ? ftxui::bgcolor(ftxui::Color::Grey11) : ftxui::nothing) | ftxui::flex;
              },
          }),
      }));

      // SPN list with border if not empty
      container->Add(ftxui::Maybe(selectors_container | ftxui::border, [selectors_container, this]() -> bool {
        return std::get<0u>(s_canbus_parameters_export_map_[m_canid_]) && selectors_container->ChildCount() > 0;
      }));

      // Add empty separator if PG deployed and empty
      container->Add(ftxui::Maybe(ftxui::Renderer([]() -> ftxui::Element { return ftxui::separatorEmpty(); }),
                                  [selectors_container, this]() -> bool {
                                    return std::get<0u>(s_canbus_parameters_export_map_[m_canid_]) &&
                                           !selectors_container->ChildCount();
                                  }));

      m_cansettings_dialog_->Add(container);
    }

    // Add custom SPNs to export dialog if not already present (tracked by tag_id)
    if (m_export_selectors_ && m_spnSettingsMap_ && m_spnSettingsMap_->contains(m_canid_)) {
      auto &spn_map = std::get<2u>(s_canbus_parameters_export_map_[m_canid_]);
      for (const auto &[tag_id, settings] : (*m_spnSettingsMap_)[m_canid_]) {
        std::string key = fmt::format("__custom_{}", tag_id);
        if (!spn_map.contains(key)) {
          nlohmann::json custom_data = {{"SPN name", key}};

          // Find matching SPN entry in verbose JSON
          nlohmann::json spn_verbose;
          if (m_data_verbose_->contains("SPNs")) {
            for (const auto &spn_entry : (*m_data_verbose_)["SPNs"]) {
              if (spn_entry.contains("SPN name") &&
                  spn_entry["SPN name"].get<std::string>().find("(custom)") != std::string::npos) {
                std::string name_in_verbose = spn_entry["SPN name"].get<std::string>();
                std::string expected = settings.spn_name.empty() ? " (custom)" : settings.spn_name + " (custom)";

                if (name_in_verbose == expected) {
                  spn_verbose = spn_entry;
                  break;
                }
              }
            }
          }

          spn_map.insert_or_assign(key,
                                   std::make_tuple(false, false, spn_verbose.is_null() ? custom_data : spn_verbose));
          m_export_selectors_
              ->Add(
                  ftxui::Container::Vertical(
                      {
                          ftxui::Container::Horizontal(
                              {
                                  ftxui::Checkbox({
                                      .checked = &std::get<0u>(spn_map[key]),
                                      .transform = [this](const ftxui::EntryState &state) -> ftxui::Element {
                                        return ftxui::hbox({ftxui::separatorEmpty(), ftxui::separatorEmpty(),
                                                            ftxui::text(state.state ? "▼ " : "▶ ")});
                                      },
                                  }),

                                  ftxui::Checkbox(
                                      {
                                          .checked = &std::get<1u>(spn_map[key]),
                                          .transform =
                                              [this, tag_id](const ftxui::EntryState state) {
                                                std::string display_name = "custom";
                                                if (m_spnSettingsMap_ && m_spnSettingsMap_->contains(m_canid_) &&
                                                    (*m_spnSettingsMap_)[m_canid_].contains(tag_id)) {
                                                  auto &s = (*m_spnSettingsMap_)[m_canid_][tag_id];
                                                  display_name = s.spn_name.empty() ? fmt::format("custom_{}", tag_id)
                                                                                    : s.spn_name + " (custom)";
                                                }

                                                return ftxui::hbox({
                                                           ftxui::text(fmt::format("[{}] ", state.state ? "X" : "")) |
                                                               (state.state ? ftxui::color(ftxui::Color::Red)
                                                                            : ftxui::color(ftxui::Color::Cyan)),
                                                           ftxui::text(display_name),
                                                       }) |
                                                       (state.focused ? ftxui::bold : ftxui::nothing) |
                                                       (state.focused ? ftxui::bgcolor(ftxui::Color::Grey11)
                                                                      : ftxui::nothing) |
                                                       ftxui::flex;
                                              },
                                      }),
                              }),

                          ftxui::Maybe(ftxui::Container::Horizontal({
                                           ftxui::Renderer([]() {
                                             return ftxui::hbox({ftxui::separatorEmpty(), ftxui::separatorEmpty(),
                                                                 ftxui::separatorEmpty(), ftxui::separatorEmpty()});
                                           }),
                                           [this, tag_id]() -> ftxui::Component {
                                             auto wrapper = ftxui::Container::Vertical({});
                                             m_export_custom_containers_[tag_id] = {wrapper, 0};
                                             return wrapper;
                                           }(),
                                       }),

                                       &std::get<0u>(spn_map[key])),
                      }));
        }
      }
    }

    // Rebuild FromLive for custom SPN export entries when fragment count changes
    if (m_spnSettingsMap_ && m_spnSettingsMap_->contains(m_canid_) && !m_data_verbose_->is_null() && m_data_verbose_->contains("SPNs")) {
      for (auto &[tag_id, pair] : m_export_custom_containers_) {
        auto &[wrapper, prev_frag_count] = pair;
        size_t cur_frag_count = 0;
        if ((*m_spnSettingsMap_)[m_canid_].contains(tag_id)) {
          cur_frag_count = (*m_spnSettingsMap_)[m_canid_][tag_id].fragments.size();
        }
        if (cur_frag_count != prev_frag_count) {
          wrapper->DetachAllChildren();
          std::string search_name;
          if ((*m_spnSettingsMap_)[m_canid_].contains(tag_id)) {
            search_name = (*m_spnSettingsMap_)[m_canid_][tag_id].spn_name + " (custom)";
          }
          const auto &spns = (*m_data_verbose_)["SPNs"];
          for (size_t i = 0; i < spns.size(); ++i) {
            if (spns[i].contains("SPN name") && spns[i]["SPN name"].get<std::string>() == search_name) {
              wrapper->Add(FromLive(m_data_verbose_, nlohmann::json::json_pointer("/SPNs/" + std::to_string(i)), false, -100, ExpanderImpl::Root()));
              break;
            }
          }
          prev_frag_count = cur_frag_count;
        }
      }
    }
  }
}
