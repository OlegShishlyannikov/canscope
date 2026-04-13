#include "can_data.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/direction.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>
#include <future>
#include <memory>
#include <set>

#include <linux/if.h>
#include <linux/sockios.h>
#include <mutex>
#include <sstream>
#include <stop_token>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

#include <linux/can.h>
#include <linux/can/raw.h>

#include <boost/regex.hpp>

#define FMT_HEADER_ONLY
#include <fmt/format.h>

#include "signals.hpp"
#include "sqlite_modern_cpp.h"
#include "utils.hpp"
#include "json/expander.hpp"
#include "json/json.hpp"

ftxui::Component makeCanPlayerDialog(ftxui::ScreenInteractive *scr, signals_map_t &smap, bool &is_ready) {

  class Impl : public ftxui::ComponentBase {
  public:
    Impl(ftxui::ScreenInteractive *scr, signals_map_t &smap, bool &is_ready) {
      static sqlite::database *database = nullptr;
      static float canbus_player_focus_relative = 0;
      static std::string player_filter_text;

      auto pgnContainer = ftxui::Container::Vertical({});

      struct pgn_parameters_s {
        bool selected, is_running, pinned, forward = false;
        uint32_t pgn, priority, datalen;
        std::string label, acronym, descr, address, ifname, period_ms;
        std::vector<uint8_t> payload;
        std::string forward_canid;

        struct {
          std::unique_ptr<std::mutex> mtx;
          std::future<void> fut;
          std::unique_ptr<std::stop_source> ss;
        } concurrent;
      };

      struct spn_parameters_s {
        struct fragment_s {
          int32_t byte_offset, bit_offset, size;
        };

        bool checked, little_endian;
        float resolution, offset, min, max, current;
        std::string unit;
        int32_t slider_percent;
        size_t raw;
        std::vector<fragment_s> fragments;
        struct pgn_parameters_s *pg_ref = nullptr;
      };

      static std::map<int32_t, pgn_parameters_s> pgs;
      static std::set<uint32_t> received_pgns;
      static const auto send_frame = [](const pgn_parameters_s &pg) {
        struct can_frame frame = {};

        const int can_socket = ::socket(AF_CAN, SOCK_RAW, CAN_RAW);
        if (can_socket < 0) {
          return;
        }

        ifreq ifr{};

        std::strcpy(ifr.ifr_name, pg.ifname.c_str());
        if (::ioctl(can_socket, SIOCGIFINDEX, &ifr) < 0) {
          ::close(can_socket);
          return;
        }

        sockaddr_can addr = {};
        addr.can_family = AF_CAN;
        addr.can_ifindex = ifr.ifr_ifindex;
        ::setsockopt(can_socket, SOL_CAN_RAW, CAN_RAW_FILTER, nullptr, 0);

        if (::bind(can_socket, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
          ::close(can_socket);
          return;
        }

        uint32_t address;
        std::stringstream{} << std::hex << pg.address >> address;

        frame.can_id = ((pg.priority & 0x7u) << 26u) | ((pg.pgn & 0x3FFFFu) << 8u) | (address & 0xFFu);
        frame.can_dlc = pg.datalen;
        frame.can_id |= CAN_EFF_FLAG;
        std::memcpy(frame.data, pg.payload.data(), pg.payload.size());
        ::write(can_socket, &frame, sizeof(frame));
        ::close(can_socket);
      };

      static const auto calculate_spn = [](spn_parameters_s &spn_params) {
        // Calculate value from slider percentage
        spn_params.current =
            ((spn_params.slider_percent / 100.0f) * (spn_params.max - spn_params.min)) + spn_params.offset;

        // Round and clamp by min/max
        spn_params.current = std::clamp(std::round(spn_params.current), spn_params.min, spn_params.max);
        spn_params.raw = (spn_params.current - spn_params.offset) / spn_params.resolution;
        auto raw = spn_params.raw;

        // Swap bytes if needed
        if (spn_params.little_endian) {
          // Get size
          size_t size = 0;
          for (const auto &frag : spn_params.fragments) {
            size += frag.size;
          }

          size /= UINT8_WIDTH;
          if (size > 1) {
            auto swapped = std::shared_ptr<uint8_t>(new uint8_t[size], [](auto *p) { delete[] p; });
            for (size_t i = 0; i < size; ++i) {
              swapped.get()[i] = reinterpret_cast<uint8_t *>(&raw)[size - i - 1];
            }

            raw = *reinterpret_cast<decltype(raw) *>(swapped.get());
          }
        }

        // Magic here
        {
          std::lock_guard<std::mutex> lock(*spn_params.pg_ref->concurrent.mtx);
          for (const auto &fragment : spn_params.fragments) {
            for (int32_t i = 0; i < fragment.size / UINT8_WIDTH + (fragment.size % UINT8_WIDTH ? 1 : 0); i++) {

              uint8_t &byte = spn_params.pg_ref->payload[i + fragment.byte_offset];

              // Reset bits in this byte depending on fragment size and fragment bit offset
              byte &= fragment.size % UINT8_WIDTH
                          ? (static_cast<uint8_t>(0xffu << (fragment.size % UINT8_WIDTH + fragment.bit_offset)) |
                             static_cast<uint8_t>(~(0xffu << fragment.bit_offset)))
                          : 0x00u;
              byte |= static_cast<uint8_t>((raw >> (i * UINT8_WIDTH)) << fragment.bit_offset);
            }

            raw >>= fragment.size;
          }
        }
      };

      static const auto stop_pg = [](pgn_parameters_s &pg) {
        if (pg.is_running) {
          pg.concurrent.ss->request_stop();
          pg.concurrent.fut.wait();
          pg.concurrent.ss = std::make_unique<std::stop_source>();
          pg.is_running = false;
        }
      };

      // Static connections
      {
        static struct on_stopped_connection_s {
          on_stopped_connection_s(signals_map_t &smap) {
            smap.get<void()>("canplayer_stopped")->connect([]() {
              for (auto &[_, pg] : pgs) {
                stop_pg(pg);
              }
            });
          }
        } on_stopped_connection(smap);

        static struct forward_connection_s {
          forward_connection_s(signals_map_t &smap) {
            smap.get<void(const std::vector<can_frame_update_s> &)>("new_entries_batch")
                ->connect([](const std::vector<can_frame_update_s> &batch) {
                  for (const auto &entry : batch) {
                    uint32_t pgn_num = 0;

                    if (entry.canid.size() >= 6) {
                      auto pgn_str = entry.canid.substr(entry.canid.size() >= 8 ? entry.canid.size() - 6 : 2, 4);
                      std::stringstream ss;
                      ss << std::hex << pgn_str;
                      ss >> pgn_num;
                    }

                    received_pgns.insert(pgn_num);
                    if (pgs.contains(pgn_num) && pgs[pgn_num].forward && pgs[pgn_num].is_running) {
                      auto &pg = pgs[pgn_num];
                      std::lock_guard<std::mutex> lock(*pg.concurrent.mtx);
                      pg.payload = entry.data.payload;
                      pg.payload.resize(pg.datalen, 0);
                      pg.forward_canid = entry.canid;
                      send_frame(pg);
                    }
                  }
                });
          }
        } forward_connection(smap);

        static struct database_ready_connection_s {
          database_ready_connection_s(signals_map_t &smap, ftxui::ScreenInteractive *scr, ftxui::Component pgnContainer,
                                      bool &is_ready) {
            smap.get<void(sqlite::database &)>("j1939_database_ready")
                ->connect([scr, &is_ready, pgnContainer](sqlite::database &db) {
                  scr->Post([scr, &is_ready, pgnContainer, &db]() {
                    std::lock_guard<std::mutex> db_lock(g_j1939_db_mtx);
                    db << "SELECT pgn, pg_label, pg_acronym, pg_descr, pg_datalen, pg_priority FROM pgns" >>
                        [&, pgnContainer](uint32_t pgn, const std::string &label, const std::string &acronym,
                                          const std::string &descr, uint32_t datalen, uint32_t priority) {
                          if (!pgs.contains(pgn)) {
                            pgs.insert(std::pair{
                                pgn,
                                pgn_parameters_s{
                                    .selected = false,
                                    .pinned = false,
                                    .pgn = pgn,
                                    .priority = priority,
                                    .datalen = datalen,
                                    .label = label,
                                    .acronym = acronym,
                                    .descr = descr,
                                    .address = "0xFF",
                                    .ifname = "vcan0",
                                    .period_ms = "1000",
                                    .payload = std::vector<uint8_t>(datalen),
                                    .concurrent =
                                        {
                                            .mtx = std::make_unique<std::mutex>(),
                                            .ss = std::make_unique<std::stop_source>(),
                                        },
                                },
                            });
                          }

                          auto &pg_ref = pgs[pgn];
                          auto spnContainer = ftxui::Container::Vertical({});
                          db << fmt::format("SELECT id, pgn, spn, spn_name FROM spns WHERE pgn = {};", pgn) >>
                              [spnContainer, &db](int32_t id, int32_t pgn, int32_t spn, const std::string &spn_name) {
                                static std::map<int32_t, spn_parameters_s> spns;
                                db << fmt::format(
                                    "SELECT min_value, max_value, resolution, offset, units FROM spns WHERE spn = {};",
                                    spn) >>
                                    [&](float min, float max, float resolution, float offset, const std::string &unit) {
                                      db << fmt::format("SELECT COUNT(*) FROM spn_fragments WHERE spn = {}", spn) >>
                                          [&](int32_t count) {
                                            std::vector<spn_parameters_s::fragment_s> fragments;
                                            fragments.resize(count);
                                            auto *fragment_ptr = fragments.data();

                                            // Fill fragments array
                                            db << fmt::format("SELECT byte_offset, bit_offset, size FROM spn_fragments "
                                                              "WHERE spn = {}",
                                                              spn) >>
                                                [&fragment_ptr](int32_t byte_offset, int32_t bit_offset, int32_t size) {
                                                  *(fragment_ptr++) = {
                                                      .byte_offset = byte_offset,
                                                      .bit_offset = bit_offset,
                                                      .size = size,
                                                  };
                                                };

                                            spns.insert_or_assign(spn, spn_parameters_s{
                                                                           .checked = false,
                                                                           .little_endian = false,
                                                                           .resolution = resolution,
                                                                           .offset = offset,
                                                                           .min = min,
                                                                           .max = max,
                                                                           .current = 0.0f,
                                                                           .unit = unit,
                                                                           .slider_percent = 0,
                                                                           .fragments = fragments,
                                                                           .pg_ref = &pgs[pgn],
                                                                       });
                                          };
                                    };

                                auto &spn_params = spns[spn];
                                spnContainer->Add({
                                    ftxui::Container::Horizontal({
                                        ftxui::Container::Vertical({
                                            ftxui::Checkbox({
                                                .checked = &spns[spn].checked,
                                                .transform =
                                                    [spn_name](const ftxui::EntryState &state) -> ftxui::Element {
                                                  return ftxui::hbox({
                                                             ftxui::separatorEmpty(),
                                                             ftxui::text(state.state ? "▼ " : "▶ "),
                                                             ftxui::text(spn_name),
                                                         }) |
                                                         (state.focused
                                                              ? (ftxui::bold | ftxui::bgcolor(ftxui::Color::Grey11))
                                                              : ftxui::nothing) |
                                                         ftxui::flex;
                                                },
                                            }),

                                            ftxui::Maybe(
                                                ftxui::Container::Horizontal({
                                                    ftxui::Renderer([]() {
                                                      ftxui::Elements separators(4u, ftxui::separatorEmpty());
                                                      return ftxui::hbox(separators);
                                                    }),

                                                    ftxui::Container::Vertical({
                                                        ftxui::Slider(ftxui::SliderOption<int32_t>{
                                                            .value = &spn_params.slider_percent,
                                                            .min = 0,
                                                            .max = 100,
                                                            .increment = 1,
                                                            .on_change =
                                                                [&spn_params =
                                                                     spns[spn]]() { calculate_spn(spn_params); },
                                                        }) | ftxui::Renderer([](ftxui::Element inner) {
                                                          return ftxui::hbox({
                                                                     ftxui::text("Value: ") | ftxui::bold |
                                                                         ftxui::color(ftxui::Color::Yellow),
                                                                     ftxui::text("["),
                                                                     inner,
                                                                     ftxui::text("]"),
                                                                 }) |
                                                                 ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 100u);
                                                        }),

                                                        ftxui::Container::Horizontal({
                                                            ftxui::Renderer([]() {
                                                              return ftxui::text("Endianness: ") | ftxui::bold |
                                                                     ftxui::color(ftxui::Color::Yellow);
                                                            }),

                                                            ftxui::Checkbox({
                                                                .checked = &spn_params.little_endian,
                                                                .transform =
                                                                    [&spn_params](const ftxui::EntryState &state) {
                                                                      auto el = ftxui::hbox({
                                                                          ftxui::text("<"),
                                                                          ftxui::text("little") |
                                                                              (spn_params.little_endian
                                                                                   ? (ftxui::bold |
                                                                                      ftxui::color(ftxui::Color::Red))
                                                                                   : ftxui::nothing),
                                                                          ftxui::text(" | "),
                                                                          ftxui::text("big") |
                                                                              (!spn_params.little_endian
                                                                                   ? (ftxui::bold |
                                                                                      ftxui::color(ftxui::Color::Red))
                                                                                   : ftxui::nothing),
                                                                          ftxui::text(">"),
                                                                      });

                                                                      if (state.focused || state.active) {
                                                                        el = el | ftxui::bold |
                                                                             ftxui::bgcolor(ftxui::Color::Grey11);
                                                                      }

                                                                      return el;
                                                                    },
                                                                .on_change =
                                                                    [&spn_params = spns[spn]]() {
                                                                      calculate_spn(spn_params);
                                                                    },
                                                            }),
                                                        }),

                                                        ftxui::Renderer([]() {
                                                          return ftxui::vbox({
                                                              ftxui::separatorEmpty(),
                                                              ftxui::text("SPN info:") |
                                                                  ftxui::color(ftxui::Color::Cyan) | ftxui::bold,
                                                          });
                                                        }),

                                                        From(
                                                            [spn]() -> nlohmann::json {
                                                              auto &spn_params = spns[spn];
                                                              auto fragments = nlohmann::json::array({});

                                                              for (const auto &frag : spn_params.fragments) {
                                                                fragments.push_back(
                                                                    nlohmann::json{{"byte_offset", frag.byte_offset},
                                                                                   {"bit_offset", frag.bit_offset},
                                                                                   {"size", frag.size}});
                                                              }

                                                              return {
                                                                  {"fragments", fragments},
                                                                  {"min", spn_params.min},
                                                                  {"max", spn_params.max},
                                                                  {"resolution", spn_params.resolution},
                                                                  {"offset", spn_params.offset},
                                                              };
                                                            }(),

                                                            false, -100, ExpanderImpl::Root()) |
                                                            ftxui::Renderer([](ftxui::Element inner) {
                                                              return ftxui::hbox({
                                                                  ftxui::separatorEmpty(),
                                                                  ftxui::separatorEmpty(),
                                                                  inner,
                                                              });
                                                            }),

                                                        ftxui::Renderer([]() { return ftxui::separatorEmpty(); }),
                                                        ftxui::Renderer([&spn_params = spns[spn]]() {
                                                          return ftxui::vbox({
                                                              ftxui::hbox({
                                                                  ftxui::text("Value: ") | ftxui::bold |
                                                                      ftxui::color(ftxui::Color::Cyan),
                                                                  ftxui::text(fmt::format("{} {}", spn_params.current,
                                                                                          spn_params.unit)),
                                                              }),

                                                              ftxui::hbox({
                                                                  ftxui::text("Raw: ") | ftxui::bold |
                                                                      ftxui::color(ftxui::Color::Cyan),
                                                                  ftxui::text(fmt::format(
                                                                      "{} (hex:{}) (bin:{})", spn_params.raw,
                                                                      fmt::format("{0:#x}", spn_params.raw),
                                                                      fmt::format("{0:#b}", spn_params.raw))),
                                                              }),

                                                              [&]() -> ftxui::Element {
                                                                // Build bit mask for this SPN's fragments
                                                                const auto &payload = spn_params.pg_ref->payload;
                                                                std::vector<bool> highlight(payload.size() * 8, false);
                                                                for (const auto &frag : spn_params.fragments) {
                                                                  int32_t start_bit =
                                                                      frag.byte_offset * 8 + frag.bit_offset;

                                                                  for (int32_t b = 0; b < frag.size; ++b) {
                                                                    auto idx = static_cast<size_t>(start_bit + b);

                                                                    if (idx < highlight.size()) {
                                                                      highlight[idx] = true;
                                                                    }
                                                                  }
                                                                }

                                                                ftxui::Elements parts;
                                                                parts.push_back(ftxui::text("PG payload: ") |
                                                                                ftxui::bold |
                                                                                ftxui::color(ftxui::Color::Cyan));
                                                                parts.push_back(ftxui::text("["));

                                                                for (size_t i = 0; i < payload.size(); ++i) {
                                                                  parts.push_back(ftxui::text("0b"));

                                                                  for (int32_t bit = 7; bit >= 0; --bit) {
                                                                    bool is_set = (payload[i] >> bit) & 1;
                                                                    bool is_spn = highlight[i * 8 + bit];
                                                                    auto ch = ftxui::text(is_set ? "1" : "0");

                                                                    if (is_spn) {
                                                                      ch = ch | ftxui::color(ftxui::Color::Red) |
                                                                           ftxui::bold;
                                                                    }

                                                                    parts.push_back(ch);
                                                                  }

                                                                  parts.push_back(ftxui::text(" "));
                                                                }

                                                                parts.push_back(ftxui::text("]"));

                                                                return ftxui::hbox(std::move(parts));
                                                              }(),

                                                              ftxui::separatorEmpty(),
                                                          });
                                                        }),
                                                    }),
                                                }),

                                                &spns[spn].checked),
                                        }),
                                    }),
                                });
                              };

                          auto pgn_entry = ftxui::Container::Vertical({
                              ftxui::Container::Horizontal({
                                  ftxui::Checkbox({
                                      .checked = &pg_ref.selected,
                                      .transform = [&pg_ref =
                                                        pgs[pgn]](const ftxui::EntryState &state) -> ftxui::Element {
                                        return ftxui::hbox({
                                                   ftxui::text(state.state ? "▼ " : "▶ "),
                                                   ftxui::text(fmt::format("0x{:x} - {}", pg_ref.pgn, pg_ref.label)) |
                                                       (pg_ref.is_running ? ftxui::color(ftxui::Color::Green)
                                                                          : ftxui::nothing),
                                                   ftxui::filler(),
                                               }) |
                                               (state.focused ? (ftxui::bold | ftxui::bgcolor(ftxui::Color::Grey11))
                                                              : ftxui::nothing) |
                                               ftxui::flex;
                                      },
                                  }),
                              }),

                              ftxui::Maybe(
                                  ftxui::Container::Vertical({
                                      ftxui::Container::Horizontal({
                                          ftxui::Checkbox({
                                              .transform = [](const ftxui::EntryState &state) -> ftxui::Element {
                                                return ftxui::hbox({
                                                    ftxui::text(" >[send_frame]< ") |
                                                        (state.focused ? ftxui::bold : ftxui::nothing) |
                                                        ftxui::color(ftxui::Color::Cyan) |
                                                        (state.focused ? ftxui::bgcolor(ftxui::Color::Grey11)
                                                                       : ftxui::nothing),
                                                });
                                              },

                                              .on_change =
                                                  [&pg = pgs[pgn]]() {
                                                    std::lock_guard<std::mutex> lock(*pg.concurrent.mtx);
                                                    send_frame(pg);
                                                  },
                                          }),

                                          ftxui::Checkbox({
                                              .transform = [](const ftxui::EntryState &state) -> ftxui::Element {
                                                return ftxui::text(" >[run]< ") |
                                                       (state.focused ? ftxui::bold : ftxui::nothing) |
                                                       ftxui::color(ftxui::Color::Cyan) |
                                                       (state.focused ? ftxui::bgcolor(ftxui::Color::Grey11)
                                                                      : ftxui::nothing);
                                              },

                                              .on_change =
                                                  [&pg = pgs[pgn]]() {
                                                    if (!pg.is_running) {
                                                      pg.concurrent.fut = std::async(
                                                          std::launch::async,
                                                          [&pg](std::stop_token st) {
                                                            int32_t period_ms;
                                                            std::stringstream{} << pg.period_ms >> period_ms;
                                                            period_ms = std::clamp(period_ms, 50, INT32_MAX);

                                                            while (!st.stop_requested()) {
                                                              {
                                                                std::lock_guard<std::mutex> lock(*pg.concurrent.mtx);
                                                                send_frame(pg);
                                                              }

                                                              std::this_thread::sleep_for(
                                                                  std::chrono::milliseconds(period_ms));
                                                            }
                                                          },

                                                          pg.concurrent.ss->get_token());
                                                      pg.is_running = true;
                                                    }
                                                  },
                                          }),

                                          ftxui::Checkbox({
                                              .transform = [](const ftxui::EntryState &state) -> ftxui::Element {
                                                return ftxui::text(" >[stop]< ") |
                                                       (state.focused ? ftxui::bold : ftxui::nothing) |
                                                       ftxui::color(ftxui::Color::Cyan) |
                                                       (state.focused ? ftxui::bgcolor(ftxui::Color::Grey11)
                                                                      : ftxui::nothing);
                                              },

                                              .on_change = [&pg = pgs[pgn]]() { stop_pg(pg); },
                                          }),
                                      }),

                                      ftxui::Renderer([]() { return ftxui::separator(); }),
                                      ftxui::Maybe(
                                          ftxui::Checkbox({
                                              .checked = &pg_ref.forward,
                                              .transform = [&pg_ref](const ftxui::EntryState &state) -> ftxui::Element {
                                                auto el =
                                                    ftxui::text(pg_ref.forward ? " [X] forward " : " [ ] forward ") |
                                                    ftxui::color(pg_ref.forward ? ftxui::Color::Green
                                                                                : ftxui::Color::Cyan);
                                                if (state.focused || state.active)
                                                  el = el | ftxui::bold | ftxui::bgcolor(ftxui::Color::Grey11);
                                                return el;
                                              },
                                          }),

                                          [pgn]() { return received_pgns.contains(pgn); }),
                                      ftxui::Renderer([]() { return ftxui::separatorEmpty(); }),

                                      ftxui::Input({
                                          .content = &pg_ref.address,
                                          .placeholder = "0xFF",
                                          .multiline = false,
                                      }) | ftxui::Renderer([](ftxui::Element inner) {
                                        return ftxui::hbox({
                                            ftxui::separatorEmpty(),
                                            ftxui::text("Address (hex): ") | ftxui::color(ftxui::Color::Magenta) |
                                                ftxui::bold,
                                            ftxui::hbox({
                                                inner,
                                                ftxui::filler(),
                                            }),
                                        });
                                      }),

                                      ftxui::Input({
                                          .content = &pg_ref.ifname,
                                          .placeholder = "vcan0",
                                          .multiline = false,
                                      }) | ftxui::Renderer([](ftxui::Element inner) {
                                        return ftxui::hbox({
                                            ftxui::separatorEmpty(),
                                            ftxui::text("CAN interface name: ") | ftxui::color(ftxui::Color::Magenta) |
                                                ftxui::bold,
                                            ftxui::hbox({
                                                inner,
                                                ftxui::filler(),
                                            }),
                                        });
                                      }),

                                      ftxui::Input({
                                          .content = &pg_ref.period_ms,
                                          .placeholder = "1000",
                                          .multiline = false,
                                      }) | ftxui::Renderer([](ftxui::Element inner) {
                                        return ftxui::hbox({
                                            ftxui::separatorEmpty(),
                                            ftxui::text("Send period (ms): ") | ftxui::color(ftxui::Color::Magenta) |
                                                ftxui::bold,
                                            ftxui::hbox({
                                                inner,
                                                ftxui::filler(),
                                            }),
                                        });
                                      }),

                                      ftxui::Renderer([]() { return ftxui::separatorEmpty(); }),
                                      spnContainer,
                                  }) | ftxui::border,
                                  &pg_ref.selected),
                          });

                          pgnContainer->Add(ftxui::Maybe(pgn_entry, [&pg_ref = pgs[pgn]]() -> bool {
                            if (player_filter_text.empty())
                              return true;
                            try {
                              boost::regex re(player_filter_text, boost::regex_constants::icase);
                              std::string subject = fmt::format("0x{:x} {}", pg_ref.pgn, pg_ref.label);
                              return boost::regex_search(subject, re);
                            } catch (...) {
                              return true;
                            }
                          }));
                        };

                    database = &db;
                    is_ready = true;
                    scr->Post(ftxui::Event::Custom);
                  });
                });
          }
        } database_ready_connection(smap, scr, pgnContainer, is_ready);
      }

      auto main = ftxui::Container::Vertical({
          ftxui::Input({
              .content = &player_filter_text,
              .placeholder = "regex filter ...",
              .transform = [](ftxui::InputState state) -> ftxui::Element {
                bool valid = true;
                if (!player_filter_text.empty()) {
                  try {
                    boost::regex(player_filter_text, boost::regex_constants::icase);
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
                        (state.hovered || state.focused ? ftxui::bgcolor(ftxui::Color::Grey11) : ftxui::nothing) |
                        ftxui::xflex,
                    ftxui::text(" ]"),
                });
              },
              .multiline = false,
          }),

          ftxui::Renderer([]() { return ftxui::separator(); }),

          (pgnContainer | ftxui::Renderer([](ftxui::Element inner) {
             return inner | ftxui::focusPositionRelative(0, canbus_player_focus_relative) | ftxui::vscroll_indicator |
                    ftxui::frame | ftxui::flex;
           })),
      });

      Add({main | ftxui::CatchEvent([pgnContainer](ftxui::Event event) {
             if (!database)
               return true;

             const auto scroll_step = []() -> float {
               size_t visible_lines = 0;
               boost::regex re;
               bool has_filter = !player_filter_text.empty();

               if (has_filter) {
                 try {
                   re = boost::regex(player_filter_text, boost::regex_constants::icase);
                 } catch (...) {
                   has_filter = false;
                 }
               }

               for (const auto &[_, pg] : pgs) {
                 if (has_filter) {
                   std::string subject = fmt::format("0x{:x} {}", pg.pgn, pg.label);
                   if (!boost::regex_search(subject, re)) {
                     continue;
                   }
                 }

                 ++visible_lines;

                 if (pg.selected) {
                   visible_lines += 15;
                 }
               }

               return visible_lines > 0 ? 1.0f / static_cast<float>(visible_lines) : 0.03f;
             };

             if (event.is_mouse()) {
               switch (static_cast<enum ftxui::Mouse::Button>(event.mouse().button)) {
               case ftxui::Mouse::Button::WheelDown: {
                 canbus_player_focus_relative = std::clamp(canbus_player_focus_relative + scroll_step(), 0.0f, 1.0f);
                 return true;
               }

               case ftxui::Mouse::Button::WheelUp: {
                 canbus_player_focus_relative = std::clamp(canbus_player_focus_relative - scroll_step(), 0.0f, 1.0f);
                 return true;
               }

               default:
                 break;
               }
             } else if (!event.is_character()) {
               if (event == ftxui::Event::ArrowDown) {

                 canbus_player_focus_relative = std::clamp(canbus_player_focus_relative + scroll_step(), 0.0f, 1.0f);
                 return true;
               } else if (event == ftxui::Event::ArrowUp) {

                 canbus_player_focus_relative = std::clamp(canbus_player_focus_relative - scroll_step(), 0.0f, 1.0f);
                 return true;
               }
             }

             return false;
           })});
    }
  };

  return ftxui::Make<Impl>(scr, smap, is_ready);
}
