#include "canid_unit.hpp"
#include "signals.hpp"
#include "tagsettingrow.hpp"
#include "tagsettings.hpp"
#include <atomic>
#include <boost/regex.hpp>
#include <cstdint>
#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/terminal.hpp>
#include <map>
#include <ranges>
#include <unordered_map>
#include <vector>

// For sqlite
#include "process.hpp"
#include "signals.hpp"
#include "sqlite_modern_cpp.h"

ftxui::Component makeMainForm(ftxui::ScreenInteractive *screen, signals_map_t &smap) {
  class Impl : public ftxui::ComponentBase {
  public:
    explicit Impl(ftxui::ScreenInteractive *screen, signals_map_t &smap) {
      static bool canbus_params_export_dialog_shown = false, file_dialog_shown = false,
                  canbus_player_dialog_shown = false, canplayer_is_ready = false;

      static float focus_relative = 0.15f;
      static float canbus_params_focus_relative = 0;
      static std::string canid_active;
      static std::string filter_text;
      static std::string export_filter_text;
      static size_t tags_count = 0u;
      static std::unordered_map<std::string, std::shared_ptr<CanIDUnit>> canid_lookup;
      static std::atomic<sqlite::database *> database_atomic{nullptr};

      extern ftxui::Component makeCanIDUnit(
          const std::string &, const std::string &, const std::string &, size_t &, const std::vector<uint8_t> &,
          ftxui::ScreenInteractive *, signals_map_t &, ftxui::Component, ftxui::Component, ftxui::Component,
          ftxui::Component, bool, bool, bool, bool, std::string &, bool &, bool &, bool &,
          std::map<std::string, std::map<int32_t, ftxui::Component>> &, spn_settings_map_t &);
      extern ftxui::Component makeFileDialog(ftxui::ScreenInteractive * scr, signals_map_t & smap, bool &shown);
      extern ftxui::Component makeCanPlayerDialog(ftxui::ScreenInteractive * scr, signals_map_t & smap, bool &is_ready);

      static auto canidsCont = ftxui::Container::Vertical({});
      static spn_settings_map_t tagSettingsMap;
      static std::map<std::string, std::map<int32_t, ftxui::Component>> spnSettingsFormMap;
      static auto spn_export_dialog = ftxui::Container::Vertical({});
      static auto canbus_params_export_dialog = ftxui::Container::Vertical({});
      CanIDUnit::s_export_filter_text_ = &export_filter_text;
      static auto canbus_player_dialog = ftxui::Container::Vertical({});

      static const auto convertParametersMapToJson = []() {
        nlohmann::json ret;

        if (canidsCont->ChildCount()) {
          auto &map = std::static_pointer_cast<CanIDUnit>(canidsCont->ChildAt(0))->getParametersExportMap();

          for (const auto &[canid_k, canid_v] : map) {
            nlohmann::json::array_t spns_selected;
            for (const auto &[selected_spn_k, selected_spn_v] : std::get<2u>(canid_v)) {
              if (std::get<1u>(selected_spn_v)) {
                const auto &spn_data = std::get<2u>(selected_spn_v);
                nlohmann::json spn = {
                    {"name", spn_data.value("SPN name", "")},
                    {"offset", spn_data.value("Offset", 0)},
                    {"resolution", spn_data.value("Resolution", 0.0)},
                    {"value", spn_data.value("Value", 0.0)},
                    {"unit", spn_data.value("Unit", "")},
                };
                if (spn_data.contains("Fragments")) {
                  nlohmann::json::array_t frags;
                  for (const auto &[k, frag] : spn_data["Fragments"].items()) {
                    auto frag_key = fmt::format("Fragment#{}", k);
                    if (frag.contains(frag_key)) {
                      frags.push_back({{fmt::format("fragment#{}", k),
                                        {
                                            {"byte_pos", frag[frag_key].value("byte_offset", 0)},
                                            {"bit_pos", frag[frag_key].value("bit_offset", 0)},
                                            {"bit_size", frag[frag_key].value("size_bits", 0)},
                                        }}});
                    }
                  }
                  spn["fragments"] = std::move(frags);
                }
                spns_selected.push_back(std::move(spn));
              }
            }

            if (!spns_selected.empty()) {
              ret[canid_k] = spns_selected;
            }
          }
        }

        return ret;
      };

      // Connections
      {
        static struct export_file_request_connection_s {
          export_file_request_connection_s(signals_map_t &smap) {
            smap.get<void(const std::string &)>("export_file_request")->connect([](const std::string &path) {
              std::ofstream ofs(path);
              ofs << convertParametersMapToJson().dump();
              ofs.close();
            });
          }
        } export_file_request_connection(smap);

        static struct database_ready_connection_s {
          database_ready_connection_s(signals_map_t &smap) {
            smap.get<void(sqlite::database &)>("j1939_database_ready")->connect([](sqlite::database &db) {
              database_atomic.store(&db);
            });
          }
        } database_ready_connection(smap);

        static struct new_entries_batch_connection_s {
          new_entries_batch_connection_s(signals_map_t &smap, ftxui::ScreenInteractive *screen) {
            smap.get<void(const std::vector<can_frame_update_s> &)>("new_entries_batch")
                ->connect([screen, &smap](const std::vector<can_frame_update_s> &batch) {
                  screen->Post([screen, &smap, batch]() {
                    for (const auto &entry : batch) {
                      auto it = canid_lookup.find(entry.canid);
                      if (it != canid_lookup.end()) {
                        it->second->update(entry.data, entry.diff, entry.verbose, entry.brief);
                      } else {
                        auto component = ftxui::Renderer([]() { return ftxui::text(""); });
                        auto new_cmp = makeCanIDUnit(
                            entry.iface, entry.canid, "J1939", tags_count, entry.data.payload, screen, smap, component,
                            canidsCont, ftxui::Container::Vertical({}), canbus_params_export_dialog, false, false, true,
                            false, canid_active, file_dialog_shown, canbus_params_export_dialog_shown,
                            file_dialog_shown, spnSettingsFormMap, tagSettingsMap);

                        auto unit = std::static_pointer_cast<CanIDUnit>(new_cmp);
                        unit->update(entry.data, entry.diff, entry.verbose, entry.brief);
                        canid_lookup[entry.canid] = unit;

                        canidsCont->Add(ftxui::Maybe(new_cmp, [unit]() -> bool {
                          if (filter_text.empty()) {
                            return true;
                          }

                          try {
                            boost::regex re(filter_text, boost::regex_constants::icase);
                            std::string subject = unit->getCanID() + " " + unit->getLabel();
                            return boost::regex_search(subject, re);
                          } catch (...) {
                            return true;
                          }
                        }));
                      }
                    }
                  });

                  screen->Post(ftxui::Event::Custom);
                });
          }
        } new_entries_batch_connection(smap, screen);
      }

      Add({
          ftxui::Container::Vertical({

              // Tab bar
              ftxui::Container::Horizontal({
                  ftxui::Renderer([]() {
                    return ftxui::hbox({
                        ftxui::text(" J1939 database is: "),
                        ftxui::text(fmt::format("{} ", database_atomic.load() ? "ready" : "not ready")) |
                            ftxui::color(database_atomic.load() ? ftxui::Color::Green : ftxui::Color::Red),
                    });
                  }),

                  ftxui::Renderer([]() {
                    static auto start = std::chrono::steady_clock::now();
                    auto now = std::chrono::steady_clock::now();
                    std::chrono::duration<double> elapsed = now - start;

                    // Convert elapsed time to hours, minutes, and seconds
                    int32_t hours = static_cast<int32_t>(elapsed.count()) / 3600u;
                    int32_t minutes = (static_cast<int32_t>(elapsed.count()) % 3600u) / 60u;
                    int32_t seconds = static_cast<int32_t>(elapsed.count()) % 60u;

                    return ftxui::hbox({ftxui::text(
                        fmt::format(" Uptime: {} ", fmt::format("{:02}:{:02}:{:02}", hours, minutes, seconds)))});
                  }),

                  ftxui::Renderer([]() {
                    const auto errors = g_error_frame_count.load(std::memory_order_relaxed);
                    return ftxui::hbox({
                        ftxui::text(" Errors: "),
                        ftxui::text(fmt::format("{} ", errors)) |
                            ftxui::color(errors ? ftxui::Color::Red : ftxui::Color::GrayDark),
                    });
                  }),

                  ftxui::Renderer([]() { return ftxui::separator(); }),
                  ftxui::Container::Horizontal({
                      ftxui::Input({
                          .content = &filter_text,
                          .placeholder = "regex filter ...",
                          .transform = [](ftxui::InputState state) -> ftxui::Element {
                            bool valid = true;

                            if (!filter_text.empty()) {
                              try {
                                boost::regex(filter_text, boost::regex_constants::icase);
                              } catch (...) {
                                valid = false;
                              }
                            }

                            state.element |= (!valid ? ftxui::color(ftxui::Color::Red) : ftxui::nothing) |
                                             (state.focused ? ftxui::color(ftxui::Color::Cyan) : ftxui::nothing) |
                                             (state.hovered ? ftxui::bold : ftxui::nothing);

                            return ftxui::hbox({
                                       ftxui::text(" Search: [ "),
                                       state.element |
                                           (state.hovered || state.focused ? ftxui::bgcolor(ftxui::Color::Grey11)
                                                                           : ftxui::nothing) |
                                           ftxui::flex,
                                       ftxui::text(" ]"),
                                   }) |
                                   ftxui::flex;
                          },
                      }),
                  }) | ftxui::flex,

                  ftxui::Renderer([]() { return ftxui::separator(); }),

                  ftxui::Checkbox({
                      .transform = [this](const ftxui::EntryState &state) -> ftxui::Element {
                        return ftxui::text(" >[can_player]< ") |
                               (canbus_player_dialog_shown || state.focused ? ftxui::bold : ftxui::nothing) |
                               (canplayer_is_ready ? ftxui::color(ftxui::Color::Cyan)
                                                   : ftxui::color(ftxui::Color::Grey23)) |
                               (state.focused ? ftxui::bgcolor(ftxui::Color::Grey11) : ftxui::nothing);
                      },

                      .on_change = [screen, &smap]() { canbus_player_dialog_shown = canplayer_is_ready; },
                  }),

                  ftxui::Checkbox({
                      .checked = &canbus_params_export_dialog_shown,

                      .transform = [this](const ftxui::EntryState &state) -> ftxui::Element {
                        return ftxui::text(" >[export_parameters]< ") |
                               (canbus_params_export_dialog_shown || state.focused ? ftxui::bold : ftxui::nothing) |
                               ftxui::color(ftxui::Color::Cyan) |
                               (state.focused ? ftxui::bgcolor(ftxui::Color::Grey11) : ftxui::nothing);
                      },

                      .on_change = [screen, &smap]() { canbus_params_export_dialog_shown = true; },
                  }),

                  ftxui::Checkbox({
                      .transform = [this](const ftxui::EntryState &state) -> ftxui::Element {
                        return ftxui::text(" >[exit]< ") | (state.focused ? ftxui::bold : ftxui::nothing) |
                               ftxui::color(ftxui::Color::Cyan) |
                               (state.focused ? ftxui::bgcolor(ftxui::Color::Grey11) : ftxui::nothing);
                      },
                      .on_change = [screen]() { screen->Exit(); },
                  }),
              }),

              ftxui::Renderer([]() { return ftxui::separator(); }),

              // Body
              (canidsCont | ftxui::Renderer([](ftxui::Element inner) {
                 return inner | ftxui::focusPositionRelative(0, focus_relative) | ftxui::vscroll_indicator |
                        ftxui::frame | ftxui::flex;
               })) |
                  ftxui::CatchEvent([](ftxui::Event event) {
                    static constexpr float scroll_step = 0.03f;
                    static const auto increment_focus = []() {
                      focus_relative = std::clamp(focus_relative + scroll_step, 0.0f, 1.0f);
                    };

                    static const auto decrement_focus = []() {
                      focus_relative = std::clamp(focus_relative - scroll_step, 0.0f, 1.0f);
                    };

                    if (event.is_mouse()) {
                      switch (static_cast<enum ftxui::Mouse::Button>(event.mouse().button)) {
                      case ftxui::Mouse::Button::WheelDown: {
                        increment_focus();
                        goto done;
                      } break;

                      case ftxui::Mouse::Button::WheelUp: {
                        decrement_focus();
                        goto done;
                      } break;

                      default:
                        break;
                      }
                    } else if (!event.is_character()) {
                      if (event == ftxui::Event::ArrowDown) {

                        increment_focus();
                        goto done;
                      } else if (event == ftxui::Event::ArrowUp) {

                        decrement_focus();
                        goto done;
                      }
                    }

                  forward:
                    return false;

                  done:
                    return true;
                  }) |

                  ftxui::Modal(
                      ftxui::Container::Vertical({
                          ftxui::Renderer([]() {
                            return ftxui::text("Export parameters") | ftxui::color(ftxui::Color::Red);
                          }) | ftxui::hcenter,
                          ftxui::Renderer([]() { return ftxui::separator(); }),

                          ftxui::Input({
                              .content = &export_filter_text,
                              .placeholder = "regex filter ...",
                              .transform = [](ftxui::InputState state) -> ftxui::Element {
                                bool valid = true;
                                if (!export_filter_text.empty()) {
                                  try {
                                    boost::regex(export_filter_text, boost::regex_constants::icase);
                                  } catch (...) {
                                    valid = false;
                                  }
                                }

                                state.element |= (!valid ? ftxui::color(ftxui::Color::Red) : ftxui::nothing) |
                                                 (state.focused ? ftxui::color(ftxui::Color::Cyan) : ftxui::nothing) |
                                                 (state.hovered ? ftxui::bold : ftxui::nothing);

                                return ftxui::hbox({
                                           ftxui::text(" Search: [ "),
                                           state.element |
                                               (state.hovered || state.focused ? ftxui::bgcolor(ftxui::Color::Grey11)
                                                                               : ftxui::nothing) |
                                               ftxui::xflex,
                                           ftxui::text(" ]"),
                                       });
                              },
                              .multiline = false,
                          }),

                          ftxui::Renderer([]() { return ftxui::separator(); }),

                          (canbus_params_export_dialog | ftxui::Renderer([](ftxui::Element inner) {
                             return inner | ftxui::focusPositionRelative(0, canbus_params_focus_relative) |
                                    ftxui::vscroll_indicator | ftxui::frame | ftxui::flex;
                           })),

                          ftxui::Renderer([]() { return ftxui::separator(); }),

                          ftxui::Container::Horizontal({
                              ftxui::Checkbox({
                                  .transform = [this](const ftxui::EntryState &state) -> ftxui::Element {
                                    return ftxui::text(" >[export_to_file]< ") |
                                           (state.focused ? ftxui::bold : ftxui::nothing) |
                                           ftxui::color(ftxui::Color::Cyan) |
                                           (state.focused ? ftxui::bgcolor(ftxui::Color::Grey11) : ftxui::nothing);
                                  },

                                  .on_change = []() { file_dialog_shown = true; },
                              }),

                              ftxui::Checkbox({
                                  .transform = [this](const ftxui::EntryState &state) -> ftxui::Element {
                                    return ftxui::text(" >[copy_to_clipboard]< ") |
                                           (state.focused ? ftxui::bold : ftxui::nothing) |
                                           ftxui::color(ftxui::Color::Cyan) |
                                           (state.focused ? ftxui::bgcolor(ftxui::Color::Grey11) : ftxui::nothing);
                                  },

                                  .on_change =
                                      [this]() {
                                        TinyProcessLib::Process(
                                            fmt::format("echo '{}' | xsel -bi", convertParametersMapToJson().dump()))
                                            .get_exit_status();
                                      },
                              }),

                              ftxui::Checkbox({
                                  .transform = [this](const ftxui::EntryState &state) -> ftxui::Element {
                                    return ftxui::text(" >[close]< ") | (state.focused ? ftxui::bold : ftxui::nothing) |
                                           ftxui::color(ftxui::Color::Cyan) |
                                           (state.focused ? ftxui::bgcolor(ftxui::Color::Grey11) : ftxui::nothing);
                                  },

                                  .on_change = []() { canbus_params_export_dialog_shown = false; },
                              }),
                          }) | ftxui::hcenter,
                      }) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 96) |
                          ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, 48) | ftxui::border |
                          ftxui::CatchEvent([](ftxui::Event event) {
                            const auto scroll_step = []() -> float {
                              size_t visible = 0;
                              for (const auto &[_, entry] : CanIDUnit::s_canbus_parameters_export_map_) {
                                if (export_filter_text.empty()) {
                                  ++visible;
                                } else {
                                  // Filtering is handled by Maybe in canid_unit.cpp,
                                  // just count all for scroll step estimation
                                  ++visible;
                                }
                                if (std::get<0u>(entry))
                                  visible += 5;
                              }
                              return visible > 0 ? 1.0f / static_cast<float>(visible) : 0.03f;
                            };

                            if (event.is_mouse()) {
                              switch (static_cast<enum ftxui::Mouse::Button>(event.mouse().button)) {
                              case ftxui::Mouse::Button::WheelDown:
                                canbus_params_focus_relative =
                                    std::clamp(canbus_params_focus_relative + scroll_step(), 0.0f, 1.0f);
                                return true;
                              case ftxui::Mouse::Button::WheelUp:
                                canbus_params_focus_relative =
                                    std::clamp(canbus_params_focus_relative - scroll_step(), 0.0f, 1.0f);
                                return true;
                              default:
                                break;
                              }
                            } else if (!event.is_character()) {
                              if (event == ftxui::Event::ArrowDown) {
                                canbus_params_focus_relative =
                                    std::clamp(canbus_params_focus_relative + scroll_step(), 0.0f, 1.0f);
                                return true;
                              } else if (event == ftxui::Event::ArrowUp) {
                                canbus_params_focus_relative =
                                    std::clamp(canbus_params_focus_relative - scroll_step(), 0.0f, 1.0f);
                                return true;
                              }
                            }

                            return false;
                          }),
                      &canbus_params_export_dialog_shown) |
                  ftxui::Modal(makeFileDialog(screen, smap, file_dialog_shown), &file_dialog_shown) |

                  ftxui::Modal(
                      ftxui::Container::Vertical({
                          ftxui::Renderer([]() {
                            return ftxui::text("CAN player") | ftxui::color(ftxui::Color::Red);
                          }) | ftxui::hcenter,
                          ftxui::Renderer([]() { return ftxui::separator(); }),

                          // Render window here
                          makeCanPlayerDialog(screen, smap, canplayer_is_ready) | ftxui::flex,

                          ftxui::Renderer([]() { return ftxui::separator(); }),

                          ftxui::Container::Horizontal({
                              ftxui::Checkbox({
                                  .transform = [this](const ftxui::EntryState &state) -> ftxui::Element {
                                    return ftxui::text(" >[stop_all]< ") |
                                           (state.focused ? ftxui::bold : ftxui::nothing) |
                                           ftxui::color(ftxui::Color::Cyan) |
                                           (state.focused ? ftxui::bgcolor(ftxui::Color::Grey11) : ftxui::nothing);
                                  },

                                  .on_change = [&smap]() { smap.get<void()>("canplayer_stopped")->operator()(); },
                              }),

                              ftxui::Checkbox({
                                  .transform = [this](const ftxui::EntryState &state) -> ftxui::Element {
                                    return ftxui::text(" >[close]< ") | (state.focused ? ftxui::bold : ftxui::nothing) |
                                           ftxui::color(ftxui::Color::Cyan) |
                                           (state.focused ? ftxui::bgcolor(ftxui::Color::Grey11) : ftxui::nothing);
                                  },

                                  .on_change = []() { canbus_player_dialog_shown = false; },
                              }),
                          }) | ftxui::hcenter,
                      }) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, ftxui::Terminal::Size().dimx) |
                          ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, ftxui::Terminal::Size().dimy) | ftxui::border,
                      &canbus_player_dialog_shown),
          }),
      });
    }

    ~Impl() override = default;
  };

  return ftxui::Make<Impl>(screen, smap);
}
