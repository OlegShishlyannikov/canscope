#include <algorithm>
// #include <cmath>
#include <boost/regex.hpp>
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
                                            std::string &, std::function<std::vector<uint8_t>()>, ftxui::Component,
                                            bool &, std::map<std::string, std::map<int32_t, ftxui::Component>> &,
                                            spn_settings_map_t &);

ftxui::Component makeCanIDUnit(const std::string &iface, const std::string &canid, const std::string &protocol,
                               size_t &spn_count, const std::vector<uint8_t> &data, ftxui::ScreenInteractive *screen,
                               signals_map_t &smap, ftxui::Component content, ftxui::Component canids_container,
                               ftxui::Component spn_settings_dialog, ftxui::Component cansettings_dialog,
                               bool is_deployed, bool is_verbose, bool is_brief, bool is_manual, bool is_charts,
                               std::string &canid_active, bool &custom_spn_settings_shown,
                               bool &canbus_parameters_export_shown, bool &filedialog_shown,
                               std::map<std::string, std::map<int32_t, ftxui::Component>> &spnSettingsFormMap,
                               spn_settings_map_t &spnSettingsMap) {

  return ftxui::Make<CanIDUnit>(iface, canid, protocol, spn_count, data, screen, smap, content, canids_container,
                                spn_settings_dialog, cansettings_dialog, is_deployed, is_verbose, is_brief, is_manual,
                                is_charts, canid_active, custom_spn_settings_shown, canbus_parameters_export_shown,
                                filedialog_shown, spnSettingsFormMap, spnSettingsMap);
}

CanIDUnit::CanIDUnit(const std::string &iface, const std::string &canid, const std::string &protocol, size_t &spn_count,
                     const std::vector<uint8_t> &data, ftxui::ScreenInteractive *screen, signals_map_t &smap,
                     ftxui::Component content, ftxui::Component canids_container, ftxui::Component spn_settings_dialog,
                     ftxui::Component cansettings_dialog, bool is_deployed, bool is_verbose, bool is_brief,
                     bool is_manual, bool is_charts, std::string &canid_active, bool &custom_spn_settings_shown,
                     bool &canbus_parameters_export_shown, bool &filedialog_shown,
                     std::map<std::string, std::map<int32_t, ftxui::Component>> &spnSettingsFormMap,
                     spn_settings_map_t &spnSettingsMap)

    : m_id_{.iface = iface, .canid = canid}, m_payload_{.data = data},
      m_ui_{
          .deployed = is_deployed, .verbose = is_verbose, .brief = is_brief, .manual = is_manual, .charts = is_charts},
      m_spnSettingsForm_(makeSpnSettingsForm(
          screen, smap, canid, canid_active, [this]() { return m_payload_.data; }, spn_settings_dialog,
          custom_spn_settings_shown, spnSettingsFormMap, spnSettingsMap)) {

  m_cansettings_dialog_ = cansettings_dialog;
  m_spnSettingsMap_ = &spnSettingsMap;

  m_brief_content_ = ftxui::Container::Vertical({});
  m_verbose_content_ = ftxui::Container::Vertical({});

  // Chart mode: tab-bar-styled SPN switcher (grows dynamically in update()), scatter plot, full name.
  m_charts_state_.switcher = ftxui::Container::Horizontal({});

  // Non-focusable leading spacer absorbs Container::Horizontal's default selector_=0 so the first
  // real Checkbox's state.active stays false until the user actively navigates there
  // (same trick as the brief/verbose/charts/manual mode switcher).
  m_charts_state_.switcher->Add(ftxui::Renderer([]() { return ftxui::text(""); }));

  auto graph_renderer = ftxui::Renderer([this]() -> ftxui::Element {
    if (m_charts_state_.ids.empty()) {
      return ftxui::text("no numeric SPN to chart") | ftxui::dim | ftxui::center;
    }

    if (m_charts_state_.selected_index < 0 ||
        static_cast<size_t>(m_charts_state_.selected_index) >= m_charts_state_.ids.size()) {
      m_charts_state_.selected_index = 0;
    }

    const int32_t spn_id = m_charts_state_.ids[m_charts_state_.selected_index];
    auto &hist = m_charts_state_.history[spn_id];
    if (hist.empty()) {
      return ftxui::text("waiting for data...") | ftxui::dim | ftxui::center;
    }

    double lo = hist.front(), hi = hist.front();
    for (double v : hist) {
      lo = std::min(lo, v);
      hi = std::max(hi, v);
    }

    double range = hi - lo;
    if (range < 1e-12) {
      range = 1.0; // flat line — avoid div-by-zero
    }

    // Scatter plot: one braille dot per sample. Canvas auto-resizes to the allocated box.
    auto canvas_el = ftxui::canvas([this, spn_id, lo, range](ftxui::Canvas &c) {
      const int32_t canvas_w = c.width();
      const int32_t canvas_h = c.height();
      auto &hh = m_charts_state_.history[spn_id];

      if (hh.empty() || canvas_w <= 0 || canvas_h <= 0) {
        return;
      }

      const size_t n = hh.size();
      const size_t skip = (n > static_cast<size_t>(canvas_w)) ? (n - static_cast<size_t>(canvas_w)) : 0;
      const size_t shown = n - skip;
      const size_t x_offset = (shown < static_cast<size_t>(canvas_w)) ? (canvas_w - shown) : 0;

      for (size_t i = 0; i < shown; ++i) {
        const double v = hh[skip + i];
        const int32_t x = static_cast<int32_t>(x_offset + i);
        const int32_t y = canvas_h - 1 - static_cast<int32_t>(((v - lo) / range) * (canvas_h - 1));
        c.DrawPoint(x, y, true, ftxui::Color::Cyan);
      }
    });

    // Y-axis labels column: max on top, mid in middle, min at bottom.
    const double mid = (lo + hi) / 2.f;
    auto y_axis = ftxui::vbox({
                      ftxui::text(fmt::format("[{:^9.4g}] --", hi)),
                      ftxui::filler(),
                      ftxui::text(fmt::format("[{:^9.4g}] --", mid)),
                      ftxui::filler(),
                      ftxui::text(fmt::format("[{:^9.4g}] --", lo)),
                  }) |
                  ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, sc_chart_height_) | ftxui::color(ftxui::Color::GrayDark);

    const std::string &full_name = static_cast<size_t>(m_charts_state_.selected_index) < m_charts_state_.names.size()
                                       ? m_charts_state_.names[m_charts_state_.selected_index]
                                       : std::string{};
    const std::string &unit = static_cast<size_t>(m_charts_state_.selected_index) < m_charts_state_.units.size()
                                  ? m_charts_state_.units[m_charts_state_.selected_index]
                                  : std::string{};

    return ftxui::vbox({
        ftxui::text(fmt::format("  SPN {}: {}, current value: {:.6g} {}",
                                static_cast<size_t>(m_charts_state_.selected_index) < m_charts_state_.labels.size()
                                    ? m_charts_state_.labels[m_charts_state_.selected_index]
                                    : fmt::format("{}", spn_id),
                                full_name, hist.back(), unit)) |
            ftxui::color(ftxui::Color::Magenta),
        ftxui::hbox({
            ftxui::separatorEmpty(),
            ftxui::separatorEmpty(),
            y_axis,
            canvas_el | ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, sc_chart_height_) | ftxui::flex | ftxui::border,
        }),
    });
  });

  m_charts_content_ = ftxui::Container::Vertical({m_charts_state_.switcher, graph_renderer});

  auto arrow = ftxui::Checkbox({
      .checked = &m_ui_.deployed,
      .transform = [this](const ftxui::EntryState &state) -> ftxui::Element {
        return ftxui::hbox({
            ftxui::text(m_ui_.deployed ? "▼ " : "▶ "),
        });
      },

      .on_change = [&, this]() { canid_active = m_id_.canid; },
  });

  auto contentbox = ftxui::Checkbox({
      .transform = [this](const ftxui::EntryState &state) -> ftxui::Element {
        ftxui::Elements line;

        // Interface
        line.push_back(ftxui::text(m_id_.iface + " ") |
                       (m_payload_.diff.is_new_interface ? (ftxui::color(ftxui::Color::Red) | ftxui::bold)
                                                         : ftxui::color(ftxui::Color::Aquamarine1) | ftxui::bold));

        // CAN ID
        line.push_back(ftxui::text(fmt::format("{:8} ", m_id_.canid)) |
                       (m_payload_.diff.is_new_canid ? (ftxui::color(ftxui::Color::Red) | ftxui::bold)
                                                     : ftxui::color(ftxui::Color::GreenLight) | ftxui::bold));

        // Size
        line.push_back(ftxui::text(fmt::format("{} ", m_payload_.data.size())));

        // Padding for < 8 bytes
        for (size_t i = m_payload_.data.size(); i < 8; ++i)
          line.push_back(ftxui::text("---- "));

        // Payload bytes with diff highlighting
        bool has_updates = false;
        for (size_t idx = 0; idx < m_payload_.data.size(); ++idx) {
          bool changed = idx < m_payload_.diff.payload_changed.size() && m_payload_.diff.payload_changed[idx];

          if (changed) {
            has_updates = true;
          }

          line.push_back(ftxui::text(fmt::format("0x{:02X} ", m_payload_.data[idx])) |
                         (changed ? (ftxui::color(ftxui::Color::Red) | ftxui::bold) : ftxui::nothing));
        }

        // Last update time
        line.push_back(
            ftxui::text(fmt::format("(updated: {})", m_payload_.last_update_time)) |
            (has_updates ? (ftxui::color(ftxui::Color::Red) | ftxui::bold) : ftxui::color(ftxui::Color::Cyan)));

        auto row = ftxui::hbox(std::move(line));
        if (m_ui_.hovered) {
          row = row | ftxui::bold | ftxui::bgcolor(ftxui::Color::Grey11);
        }

        return row | ftxui::reflect(m_ui_.box);
      },

      .on_change =
          [&canid_active, this]() {
            canid_active = m_id_.canid;
            m_ui_.deployed = !m_ui_.deployed;
          },
  });

  auto label = ftxui::Renderer([this, protocol]() -> ftxui::Element {
    if (m_payload_.verbose->contains("Label")) {

      return ftxui::hbox({
          ftxui::text(fmt::format(" - {}", (*m_payload_.verbose)["Label"].get<std::string>())) |
              ftxui::color(ftxui::Color::Magenta) | (m_ui_.deployed ? ftxui::bold : ftxui::nothing),
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
                          .checked = &m_ui_.brief,
                          .transform = [this](const ftxui::EntryState &state) -> ftxui::Element {
                            auto el = ftxui::hbox({
                                ftxui::text("<"),
                                ftxui::text("brief") | (m_ui_.brief ? ftxui::bold : ftxui::nothing) |
                                    ftxui::color(ftxui::Color::Cyan),
                                ftxui::text(" | "),
                            });

                            if (state.focused || state.active)
                              el = el | ftxui::bold | ftxui::bgcolor(ftxui::Color::Grey11);
                            return el;
                          },

                          .on_change =
                              [this]() {
                                m_ui_.brief = true;
                                m_ui_.verbose = false;
                                m_ui_.manual = false;
                                m_ui_.charts = false;
                              },
                      }),

                      ftxui::Checkbox({
                          .checked = &m_ui_.verbose,
                          .transform = [this](const ftxui::EntryState &state) -> ftxui::Element {
                            auto el = ftxui::text("verbose") | (m_ui_.verbose ? ftxui::bold : ftxui::nothing) |
                                      ftxui::color(ftxui::Color::Cyan);

                            if (state.focused || state.active) {
                              el = el | ftxui::bold | ftxui::bgcolor(ftxui::Color::Grey11);
                            }

                            return el;
                          },

                          .on_change =
                              [this]() {
                                m_ui_.brief = false;
                                m_ui_.verbose = true;
                                m_ui_.manual = false;
                                m_ui_.charts = false;
                              },
                      }),

                      ftxui::Checkbox({
                          .checked = &m_ui_.charts,
                          .transform = [this](const ftxui::EntryState &state) -> ftxui::Element {
                            auto el = ftxui::hbox({
                                ftxui::text(" | "),
                                ftxui::text("charts") | (m_ui_.charts ? ftxui::bold : ftxui::nothing) |
                                    ftxui::color(ftxui::Color::Cyan),
                            });

                            if (state.focused || state.active) {
                              el = el | ftxui::bold | ftxui::bgcolor(ftxui::Color::Grey11);
                            }

                            return el;
                          },

                          .on_change =
                              [this]() {
                                m_ui_.brief = false;
                                m_ui_.verbose = false;
                                m_ui_.manual = false;
                                m_ui_.charts = true;
                              },
                      }),

                      ftxui::Checkbox({
                          .checked = &m_ui_.manual,
                          .transform = [this](const ftxui::EntryState &state) -> ftxui::Element {
                            auto el = ftxui::hbox({
                                ftxui::text(" | "),
                                ftxui::text("manual") | (m_ui_.manual ? ftxui::bold : ftxui::nothing) |
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
                                m_ui_.brief = false;
                                m_ui_.verbose = false;
                                m_ui_.manual = true;
                                m_ui_.charts = false;
                              },
                      }),

                  }),

                  // Brief content
                  ftxui::Maybe(m_brief_content_ | ftxui::flex, &m_ui_.brief),

                  // Verbose content
                  ftxui::Maybe(m_verbose_content_ | ftxui::flex, &m_ui_.verbose),

                  // Charts content
                  ftxui::Maybe(m_charts_content_ | ftxui::flex, &m_ui_.charts),

                  // Manual mode content
                  ftxui::Maybe(m_spnSettingsForm_ | ftxui::flex, &m_ui_.manual),

              }) | ftxui::border,

              &m_ui_.deployed),
      }),
  });
}

bool CanIDUnit::OnEvent(ftxui::Event event) {
  if (event.is_mouse()) {
    m_ui_.hovered = m_ui_.box.Contain(event.mouse().x, event.mouse().y);
  }

  return ftxui::ComponentBase::OnEvent(event);
}

void CanIDUnit::update(const can_frame_data_s &data, const can_frame_diff_s &diff,
                       std::shared_ptr<nlohmann::json> verbose, std::shared_ptr<nlohmann::json> brief) {
  m_payload_.data = data.payload;
  m_payload_.diff = diff;

  // Update timestamp
  auto t = std::time(nullptr);
  struct tm tm_buf;
  localtime_r(&t, &tm_buf);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%d-%m-%Y %H-%M-%S", &tm_buf);
  m_payload_.last_update_time = buf;

  bool was_null = m_payload_.brief->is_null();

  if (verbose) {
    *m_payload_.verbose = std::move(*verbose);
  }

  if (brief) {
    *m_payload_.brief = std::move(*brief);
  }

  // Ensure skeleton verbose/brief exist for custom SPN injection
  if (m_spnSettingsMap_ && m_spnSettingsMap_->contains(m_id_.canid) && !(*m_spnSettingsMap_)[m_id_.canid].empty()) {
    if (m_payload_.verbose->is_null()) {
      *m_payload_.verbose = nlohmann::json{{"SPNs", nlohmann::json::array()}};
    }

    if (m_payload_.brief->is_null()) {
      *m_payload_.brief = nlohmann::json{{"SPNs", nlohmann::json::array()}};
    }

    if (!m_payload_.verbose->contains("SPNs")) {
      (*m_payload_.verbose)["SPNs"] = nlohmann::json::array();
    }

    if (!m_payload_.brief->contains("SPNs")) {
      (*m_payload_.brief)["SPNs"] = nlohmann::json::array();
    }

    for (const auto &[tag_id, settings] : (*m_spnSettingsMap_)[m_id_.canid]) {
      double resolution = 1.f, offset_val = 0.f;
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
        if (bc > 0 && bo >= 0 && static_cast<size_t>(bo + byte_cnt) <= m_payload_.data.size()) {
          int64_t frag_val = 0;

          for (int32_t i = 0; i < byte_cnt; ++i) {
            frag_val |= static_cast<int64_t>(m_payload_.data[bo + i]) << (i * UINT8_WIDTH);
          }

          frag_val = (frag_val >> bi) & ((1LL << bc) - 1);
          result |= frag_val << total_bits;
          total_bits += bc;
        }
      }

      if (settings.big_endian && total_bits > 8) {
        int64_t swapped = 0;
        size_t total_bytes = (total_bits + 7) / 8;

        for (size_t i = 0; i < total_bytes; ++i) {
          swapped |= ((result >> (i * 8)) & 0xFF) << ((total_bytes - 1 - i) * 8);
        }

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

        frags_json.push_back({
            {
                fmt::format("Fragment#{}", fi),
                {
                    {"byte_offset", bo},
                    {"bit_offset", bi},
                    {"size_bits", bc},
                },
            },
        });
      }

      nlohmann::json custom_spn = {
          {"SPN (integer)", -static_cast<int32_t>(tag_id)},
          {"SPN (user id)", settings.spn_id},
          {"SPN name", settings.spn_name + " (custom)"},
          {"Encoding", "numeric"},
          {"Value", spn_val},
          {"Unit", settings.unit},
          {"Resolution", resolution},
          {"Offset", offset_val},
          {"Fragments", frags_json},
      };

      if (m_payload_.verbose->contains("SPNs")) {
        (*m_payload_.verbose)["SPNs"].push_back(custom_spn);
      }

      if (!m_payload_.brief->is_null() && m_payload_.brief->contains("SPNs")) {
        (*m_payload_.brief)["SPNs"].push_back(
            fmt::format("{}: {:.6g} {}", settings.spn_name + " (custom)", spn_val, settings.unit));
      }
    }
  }

  // Accumulate numeric SPN values into per-SPN ring buffers and grow the chart switcher as new SPNs appear.
  // Runs AFTER custom SPN injection so user-added SPNs are picked up in charts too.
  if (m_payload_.verbose->contains("SPNs") && (*m_payload_.verbose)["SPNs"].is_array()) {
    for (const auto &spn : (*m_payload_.verbose)["SPNs"]) {
      if (spn.value("Encoding", std::string{"numeric"}) != "numeric")
        continue;
      if (!spn.contains("SPN (integer)") || !spn.contains("Value") || !spn["Value"].is_number())
        continue;

      const int32_t id = spn["SPN (integer)"].get<int32_t>();

      auto &dq = m_charts_state_.history[id];
      dq.push_back(spn["Value"].get<double>());
      if (dq.size() > sc_chart_history_max_)
        dq.pop_front();

      // Recompute label & name every frame — user may edit "SPN id"/"SPN name" fields after the
      // SPN is registered in the switcher, so static snapshots would be stale.
      auto make_label = [&]() {
        std::string s = spn.value("SPN (user id)", std::string{});
        if (s.empty()) {
          s = fmt::format("{}", id);
        }

        return s;
      };
      auto make_name = [&]() { return spn.value("SPN name", fmt::format("SPN {}", id)); };
      auto make_unit = [&]() { return spn.value("Unit", std::string{}); };

      auto existing_it = std::find(m_charts_state_.ids.begin(), m_charts_state_.ids.end(), id);
      if (existing_it != m_charts_state_.ids.end()) {
        const size_t found_i = static_cast<size_t>(existing_it - m_charts_state_.ids.begin());
        m_charts_state_.labels[found_i] = make_label();
        m_charts_state_.names[found_i] = make_name();
        m_charts_state_.units[found_i] = make_unit();
        continue;
      }

      // New SPN — append to parallel vectors and attach a Checkbox to the switcher.
      const size_t i = m_charts_state_.ids.size();
      m_charts_state_.ids.push_back(id);
      m_charts_state_.names.push_back(make_name());
      m_charts_state_.labels.push_back(make_label());
      m_charts_state_.units.push_back(make_unit());

      m_charts_state_.flags.push_back(i == 0); // select first-added by default

      m_charts_state_.switcher->Add(ftxui::Checkbox({
          .checked = &m_charts_state_.flags[i],
          .transform = [this, i](const ftxui::EntryState &state) -> ftxui::Element {
            const bool is_first = (i == 0);
            const bool is_last = (i + 1 == m_charts_state_.ids.size());
            const bool selected = (m_charts_state_.selected_index == static_cast<int>(i));
            ftxui::Elements parts;

            parts.push_back(ftxui::text(is_first ? "  <" : " | "));
            parts.push_back(ftxui::text(m_charts_state_.labels[i]) | (selected ? ftxui::bold : ftxui::nothing) |
                            ftxui::color(ftxui::Color::Cyan));
            if (is_last) {
              parts.push_back(ftxui::text(">"));
            }

            auto el = ftxui::hbox(std::move(parts));

            if (state.focused || state.active) {
              el = el | ftxui::bold | ftxui::bgcolor(ftxui::Color::Grey11);
            }

            return el;
          },
          .on_change =
              [this, i]() {
                for (auto it = m_charts_state_.flags.begin(); it != m_charts_state_.flags.end(); ++it) {
                  *it = false;
                }

                m_charts_state_.flags[i] = true;
                m_charts_state_.selected_index = static_cast<int32_t>(i);
              },
      }));
    }
  }

  // Rebuild FromLive when structure changes (first data or SPN count changed)
  size_t verbose_spn_count = (!m_payload_.verbose->is_null() && m_payload_.verbose->contains("SPNs"))
                                 ? (*m_payload_.verbose)["SPNs"].size()
                                 : 0;
  size_t brief_spn_count =
      (!m_payload_.brief->is_null() && m_payload_.brief->contains("SPNs")) ? (*m_payload_.brief)["SPNs"].size() : 0;
  bool structure_changed =
      verbose_spn_count != m_payload_.last_verbose_spn_count || brief_spn_count != m_payload_.last_brief_spn_count;

  if (structure_changed || (was_null && !m_payload_.brief->is_null())) {
    m_brief_content_->DetachAllChildren();

    if (!m_payload_.brief->is_null()) {
      m_brief_content_->Add(
          FromLive(m_payload_.brief, nlohmann::json::json_pointer(), true, -100, ExpanderImpl::Root()));
    }

    m_verbose_content_->DetachAllChildren();

    if (!m_payload_.verbose->is_null()) {
      m_verbose_content_->Add(
          FromLive(m_payload_.verbose, nlohmann::json::json_pointer(), true, -100, ExpanderImpl::Root()));
    }

    m_payload_.last_verbose_spn_count = verbose_spn_count;
    m_payload_.last_brief_spn_count = brief_spn_count;
  }

  // Populate export dialog once when verbose data first becomes available
  if (!m_payload_.brief->is_null() && !m_payload_.verbose->is_null() && m_cansettings_dialog_) {
    if (!s_canbus_parameters_export_map_.contains(m_id_.canid)) {
      s_canbus_parameters_export_map_.insert({m_id_.canid, {false, false, {}}});
    }

    if (!std::get<1u>(s_canbus_parameters_export_map_[m_id_.canid])) {
      std::get<1u>(s_canbus_parameters_export_map_[m_id_.canid]) = true;
      m_export_selectors_ = ftxui::Container::Vertical({});
      auto &selectors_container = m_export_selectors_;

      for (const auto &[pgn_key, pgn_val] : m_payload_.verbose->items()) {
        if (pgn_key == "SPNs") {
          for (const auto &[spns_arr_k, spns_arr_v] : pgn_val.items()) {
            for (const auto &[spn_k, spn_v] : spns_arr_v.items()) {
              if (spn_k == "SPN name") {
                std::string spn_name = spn_v.get<std::string>();
                if (spn_name.find("(custom)") != std::string::npos) {
                  continue;
                }

                auto &spn_map = std::get<2u>(s_canbus_parameters_export_map_[m_id_.canid]);

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

                                ftxui::Maybe(ftxui::Container::Horizontal({
                                                 ftxui::Renderer([]() {
                                                   return ftxui::hbox({ftxui::separatorEmpty(), ftxui::separatorEmpty(),
                                                                       ftxui::separatorEmpty(),
                                                                       ftxui::separatorEmpty()});
                                                 }),
                                                 FromLive(m_payload_.verbose,
                                                          nlohmann::json::json_pointer("/SPNs/" + spns_arr_k), false,
                                                          -100, ExpanderImpl::Root()),
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
              .checked = &std::get<0u>(s_canbus_parameters_export_map_[m_id_.canid]),
              .transform = [this](const ftxui::EntryState &state) -> ftxui::Element {
                return ftxui::hbox(
                    {ftxui::text(std::get<0u>(s_canbus_parameters_export_map_[m_id_.canid]) ? "▼ " : "▶ ")});
              },
          }),

          ftxui::Checkbox({
              .checked = &std::get<0u>(s_canbus_parameters_export_map_[m_id_.canid]),
              .transform = [this](const ftxui::EntryState &state) -> ftxui::Element {
                auto &spn_map = std::get<2u>(s_canbus_parameters_export_map_[m_id_.canid]);
                size_t selected_cnt = std::count_if(spn_map.begin(), spn_map.end(),
                                                    [](auto &e) -> bool { return std::get<1u>(e.second); });
                std::string label =
                    m_payload_.brief->contains("Label") ? (*m_payload_.brief)["Label"].get<std::string>() : "";

                return ftxui::hbox({
                           ftxui::text(fmt::format("{:8}", fmt::format("[{}/{}] ", selected_cnt, spn_map.size()))) |
                               (state.focused ? ftxui::bold : ftxui::nothing) | ftxui::color(ftxui::Color::LightGreen),
                           ftxui::text(fmt::format("{} ", m_id_.iface)) |
                               (state.focused ? ftxui::bold : ftxui::nothing) | ftxui::color(ftxui::Color::Cyan),
                           ftxui::text(fmt::format("{} ", m_id_.canid)) |
                               (state.focused ? ftxui::bold : ftxui::nothing) | ftxui::color(ftxui::Color::LightGreen),
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
        return std::get<0u>(s_canbus_parameters_export_map_[m_id_.canid]) && selectors_container->ChildCount() > 0;
      }));

      // Add empty separator if PG deployed and empty
      container->Add(ftxui::Maybe(ftxui::Renderer([]() -> ftxui::Element { return ftxui::separatorEmpty(); }),
                                  [selectors_container, this]() -> bool {
                                    return std::get<0u>(s_canbus_parameters_export_map_[m_id_.canid]) &&
                                           !selectors_container->ChildCount();
                                  }));

      m_cansettings_dialog_->Add(ftxui::Maybe(container, [this]() -> bool {
        if (!s_export_filter_text_ || s_export_filter_text_->empty()) {
          return true;
        }

        try {
          boost::regex re(*s_export_filter_text_, boost::regex_constants::icase);
          std::string subject = m_id_.canid + " " + getLabel();
          return boost::regex_search(subject, re);
        } catch (...) {
          return true;
        }
      }));
    }

    // Add custom SPNs to export dialog if not already present (tracked by tag_id)
    if (m_export_selectors_ && m_spnSettingsMap_ && m_spnSettingsMap_->contains(m_id_.canid)) {
      auto &spn_map = std::get<2u>(s_canbus_parameters_export_map_[m_id_.canid]);
      for (const auto &[tag_id, settings] : (*m_spnSettingsMap_)[m_id_.canid]) {
        std::string key = fmt::format("__custom_{}", tag_id);
        if (!spn_map.contains(key)) {
          nlohmann::json custom_data = {{"SPN name", key}};

          // Find matching SPN entry in verbose JSON
          nlohmann::json spn_verbose;
          if (m_payload_.verbose->contains("SPNs")) {
            for (const auto &spn_entry : (*m_payload_.verbose)["SPNs"]) {
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

                                                if (m_spnSettingsMap_ && m_spnSettingsMap_->contains(m_id_.canid) &&
                                                    (*m_spnSettingsMap_)[m_id_.canid].contains(tag_id)) {
                                                  auto &s = (*m_spnSettingsMap_)[m_id_.canid][tag_id];
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
    if (m_spnSettingsMap_ && m_spnSettingsMap_->contains(m_id_.canid) && !m_payload_.verbose->is_null() &&
        m_payload_.verbose->contains("SPNs")) {
      for (auto &[tag_id, pair] : m_export_custom_containers_) {
        auto &[wrapper, prev_frag_count] = pair;

        size_t cur_frag_count = 0;
        if ((*m_spnSettingsMap_)[m_id_.canid].contains(tag_id)) {
          cur_frag_count = (*m_spnSettingsMap_)[m_id_.canid][tag_id].fragments.size();
        }

        if (cur_frag_count != prev_frag_count) {
          wrapper->DetachAllChildren();
          std::string search_name;

          if ((*m_spnSettingsMap_)[m_id_.canid].contains(tag_id)) {
            search_name = (*m_spnSettingsMap_)[m_id_.canid][tag_id].spn_name + " (custom)";
          }

          const auto &spns = (*m_payload_.verbose)["SPNs"];
          for (size_t i = 0; i < spns.size(); ++i) {
            if (spns[i].contains("SPN name") && spns[i]["SPN name"].get<std::string>() == search_name) {
              wrapper->Add(FromLive(m_payload_.verbose, nlohmann::json::json_pointer("/SPNs/" + std::to_string(i)),
                                    false, -100, ExpanderImpl::Root()));
              break;
            }
          }

          prev_frag_count = cur_frag_count;
        }
      }
    }
  }
}
