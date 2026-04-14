#include <boost/lexical_cast.hpp>
#include <boost/signals2.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <cstdint>
#include <functional>

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

#include "canid_unit.hpp"
#include "signals.hpp"
#include "tagsettings.hpp"

ftxui::Component makeSpnSettingsForm(ftxui::ScreenInteractive *screen, signals_map_t &smap, const std::string &canid,
                                     std::string &canid_active, std::function<std::vector<uint8_t>()> data_getter,
                                     ftxui::Component dialog, bool &modal_shown,
                                     std::map<std::string, std::map<int32_t, ftxui::Component>> &spnSettingsFormMap,
                                     spn_settings_map_t &spnSettingsMap) {
  class Impl : public ftxui::ComponentBase {
  public:
    explicit Impl(ftxui::ScreenInteractive *screen, signals_map_t &smap, const std::string &canid,
                  std::string &canid_active, std::function<std::vector<uint8_t>()> data_getter, ftxui::Component dialog,
                  bool &modal_shown, std::map<std::string, std::map<int32_t, ftxui::Component>> &spnSettingsFormMap,
                  spn_settings_map_t &spnSettingsMap)
        : m_canid_(canid), m_screen_(screen), m_smap_(smap), m_data_getter_(std::move(data_getter)),
          m_spnSettingsFormMap_(spnSettingsFormMap), m_spnSettingsMap_(spnSettingsMap) {
      static size_t spns_count = 0u;

      for (auto &[k, v] : m_spnSettingsFormMap_[m_canid_]) {
        addSpnComponent(k);
      }

      smap.get<void(const std::string &, size_t)>("new_tag_request")
          ->connect([this](const std::string &canid, size_t tag_id) {
            if (canid == m_canid_) {
              addSpnComponent(static_cast<int32_t>(tag_id));
            }
          });

      Add(ftxui::Container::Vertical({
              ftxui::Container::Vertical({
                  ftxui::Checkbox({
                      .transform = [this](const ftxui::EntryState &state) -> ftxui::Element {
                        return ftxui::hbox({
                            ftxui::separatorEmpty(),
                            ftxui::text(" >[add_parameter]< ") | (state.focused ? ftxui::bold : ftxui::nothing) |
                                ftxui::color(ftxui::Color::Cyan) |
                                (state.focused ? ftxui::bgcolor(ftxui::Color::Grey11) : ftxui::nothing),
                            ftxui::filler(),
                        });
                      },

                      .on_change =
                          [&, this]() {
                            canid_active = m_canid_;
                            m_spnSettingsMap_[m_canid_].insert_or_assign(
                                static_cast<int32_t>(spns_count),
                                spn_settings_s{
                                    .uuid = boost::lexical_cast<std::string>(boost::uuids::random_generator()()),
                                    .le = true,
                                });
                            addSpnComponent(static_cast<int32_t>(spns_count));
                            spns_count++;
                          },
                  }),

                  ftxui::Checkbox({
                      .transform = [this](const ftxui::EntryState &state) -> ftxui::Element {
                        return ftxui::hbox({
                            ftxui::separatorEmpty(),
                            ftxui::text(" >[remove_selected_params]< ") |
                                (state.focused ? ftxui::bold : ftxui::nothing) | ftxui::color(ftxui::Color::Cyan) |
                                (state.focused ? ftxui::bgcolor(ftxui::Color::Grey11) : ftxui::nothing),
                            ftxui::filler(),
                        });
                      },

                      .on_change =
                          [&, this]() {
                            std::vector<int32_t> keys;

                            for (const auto &[k, v] : m_spnSettingsMap_[m_canid_]) {
                              if (v.selected) {
                                keys.push_back(k);
                              }
                            }

                            for (auto i : keys) {
                              m_spnSettingsFormMap_[m_canid_].erase(i);
                              m_spnSettingsMap_[m_canid_].erase(i);
                            }

                            m_current_spn_settings_->DetachAllChildren();
                            for (auto &[k, v] : m_spnSettingsFormMap_[m_canid_]) {
                              m_current_spn_settings_->Add(v);
                            }
                          },
                  }),
              }),

              m_current_spn_settings_ | ftxui::xflex,
          }) |
          ftxui::flex);
    }

  private:
    void addSpnComponent(int32_t k) {
      auto &s = m_spnSettingsMap_[m_canid_][k];

      auto make_field = [](const char *label, const char *placeholder, std::string *content) {
        return ftxui::Container::Horizontal({
            ftxui::Renderer([label]() {
              return ftxui::text(fmt::format("{:24}", label)) | ftxui::bold | ftxui::color(ftxui::Color::Yellow);
            }),

            ftxui::Renderer([]() { return ftxui::text("[ "); }),
            ftxui::Input({
                .content = content,
                .placeholder = placeholder,
                .transform = [](ftxui::InputState state) -> ftxui::Element {
                  auto el = state.element;

                  if (state.focused) {
                    el = el | ftxui::bgcolor(ftxui::Color::Grey27) | ftxui::focusCursorBarBlinking;
                  } else if (state.hovered) {
                    el = el | ftxui::bgcolor(ftxui::Color::Grey11);
                  }

                  return el;
                },

                .multiline = false,
            }) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 32),
            ftxui::Renderer([]() { return ftxui::text(" ]"); }),
        });
      };

      auto get_data = [this]() -> std::vector<uint8_t> {
        return m_data_getter_ ? m_data_getter_() : std::vector<uint8_t>{};
      };

      // Fragment tab container — shows fields for active fragment
      auto frag_fields_container = ftxui::Container::Vertical({});

      auto rebuild_frag_fields = [&s, frag_fields_container, make_field]() {
        frag_fields_container->DetachAllChildren();
        if (s.active_fragment >= 0 && s.active_fragment < static_cast<int32_t>(s.fragments.size())) {

          auto &f = s.fragments[s.active_fragment];
          frag_fields_container->Add(make_field("Byte offset:", "enter byte offset here ...", &f.byte_offset));
          frag_fields_container->Add(make_field("Bit offset:", "enter bit offset here ...", &f.bit_offset));
          frag_fields_container->Add(make_field("Bit count:", "enter bit count here ...", &f.bit_count));
        }
      };

      rebuild_frag_fields();

      // Fragment tab switcher
      auto frag_tabs = ftxui::Renderer([&s]() {
        ftxui::Elements tabs;
        tabs.push_back(ftxui::text("<"));

        for (size_t i = 0; i < s.fragments.size(); ++i) {
          if (i > 0) {
            tabs.push_back(ftxui::text(" | "));
          }

          auto label = ftxui::text(fmt::format("fragment#{}", i));
          if (static_cast<int32_t>(i) == s.active_fragment) {
            label = label | ftxui::bold | ftxui::color(ftxui::Color::Red);
          } else {
            label = label | ftxui::color(ftxui::Color::Cyan);
          }

          tabs.push_back(label);
        }

        tabs.push_back(ftxui::text(">"));
        return ftxui::hbox(std::move(tabs));
      });

      // Payload view — highlights ALL fragments simultaneously
      auto payload_view = ftxui::Renderer([&s, get_data]() {
        auto data = get_data();

        double resolution = 1.0, offset_val = 0.0;
        try {
          resolution = std::stod(s.resolution);
        } catch (...) {
        }

        try {
          offset_val = std::stod(s.offset);
        } catch (...) {
        }

        // Collect all bit ranges from all fragments
        struct bit_range {
          int32_t global_start, global_end;
          int32_t byte_off, byte_cnt;
        };

        std::vector<bit_range> ranges;
        for (const auto &f : s.fragments) {
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

          if (bc > 0) {
            int32_t byte_cnt = (bc + bi + UINT8_WIDTH - 1) / UINT8_WIDTH;
            ranges.push_back({bo * UINT8_WIDTH + bi, bo * UINT8_WIDTH + bi + bc, bo, byte_cnt});
          }
        }

        auto byte_in_range = [&ranges](int32_t idx) {
          for (const auto &r : ranges) {
            if (idx >= r.byte_off && idx < r.byte_off + r.byte_cnt) {
              return true;
            }
          }

          return false;
        };

        auto bit_in_range = [&ranges](int32_t global_bit) {
          for (const auto &r : ranges) {
            if (global_bit >= r.global_start && global_bit < r.global_end) {
              return true;
            }
          }

          return false;
        };

        // Hex
        ftxui::Elements hex_els;
        hex_els.push_back(ftxui::text("{ "));
        for (size_t i = 0; i < data.size(); ++i) {
          auto val = fmt::format("0x{:02X}", data[i]);
          auto t = ftxui::text(i + 1 < data.size() ? fmt::format("{:<15s}", val + ",") : fmt::format("{:<15s}", val));
          hex_els.push_back(byte_in_range(static_cast<int32_t>(i)) ? (t | ftxui::color(ftxui::Color::Red) | ftxui::bold)
                                                                   : t);
        }

        hex_els.push_back(ftxui::text(" }"));

        // Dec
        ftxui::Elements dec_els;
        dec_els.push_back(ftxui::text("{ "));

        for (size_t i = 0; i < data.size(); ++i) {
          auto val = fmt::format("{}", data[i]);
          auto t = ftxui::text(i + 1 < data.size() ? fmt::format("{:<15s}", val + ",") : fmt::format("{:<15s}", val));
          dec_els.push_back(byte_in_range(static_cast<int32_t>(i)) ? (t | ftxui::color(ftxui::Color::Red) | ftxui::bold)
                                                                   : t);
        }

        dec_els.push_back(ftxui::text(" }"));

        // Bin — per-bit highlighting across all fragments
        ftxui::Elements bin_els;
        bin_els.push_back(ftxui::text("{ "));

        for (size_t i = 0; i < data.size(); ++i) {
          ftxui::Elements bits;
          bits.push_back(ftxui::text("0b"));

          for (int32_t b = 7; b >= 0; --b) {
            int32_t global_bit = static_cast<int32_t>(i) * UINT8_WIDTH + b;
            char ch = (data[i] >> b) & 1 ? '1' : '0';
            auto t = ftxui::text(std::string(1, ch));
            bits.push_back(bit_in_range(global_bit) ? (t | ftxui::color(ftxui::Color::Red) | ftxui::bold) : t);
          }

          if (i + 1 < data.size()) {
            bits.push_back(ftxui::text(","));
          }

          // Force each byte cell to the same 15-char width as the hex/dec rows so the closing `}` aligns.
          bin_els.push_back(ftxui::hbox(std::move(bits)) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 15));
        }

        bin_els.push_back(ftxui::text(" }"));

        // Extract value from all fragments (always LE within each fragment)
        int64_t result = 0;
        size_t total_bits = 0;
        for (const auto &f : s.fragments) {
          int32_t bo = 0, bi = 0, bc = 0;

          try {
            bo = std::stoi(f.byte_offset);
            bi = std::stoi(f.bit_offset);
            bc = std::stoi(f.bit_count);
          } catch (...) {
          }

          int32_t byte_cnt = (bc + bi + UINT8_WIDTH - 1) / UINT8_WIDTH;

          if (bc > 0 && bo >= 0 && static_cast<size_t>(bo + byte_cnt) <= data.size()) {
            int64_t frag_val = 0;

            for (int32_t i = 0; i < byte_cnt; ++i) {
              frag_val |= static_cast<int64_t>(data[bo + i]) << (i * UINT8_WIDTH);
            }

            frag_val = (frag_val >> bi) & ((1LL << bc) - 1);
            result |= frag_val << total_bits;
            total_bits += bc;
          }
        }
        // Byteswap the assembled result if big endian
        if (s.big_endian && total_bits > 8u) {
          int64_t swapped = 0;
          size_t total_bytes = (total_bits + 7u) / 8u;

          for (size_t i = 0; i < total_bytes; ++i) {
            swapped |= ((result >> (i * 8u)) & 0xFFu) << ((total_bytes - 1 - i) * 8u);
          }

          result = swapped;
        }

        double value = static_cast<double>(result) * resolution + offset_val;

        return ftxui::vbox({
            ftxui::separatorEmpty(),
            ftxui::separatorEmpty(),
            ftxui::vbox({
                ftxui::hbox(hex_els),
                ftxui::hbox(dec_els),
                ftxui::hbox(bin_els),
            }) | ftxui::xframe,
            ftxui::hbox({
                ftxui::text("Value: ") | ftxui::bold,
                ftxui::text(fmt::format("({:0{}} | 0x{:0{}X} | 0b{:0{}b}) * {} + {} = ", result,
                                        static_cast<int32_t>(fmt::format("{}", (1LL << total_bits) - 1).size()), result,
                                        static_cast<int32_t>((total_bits + 3) / 4), result,
                                        static_cast<int32_t>(total_bits), resolution, offset_val)),

                ftxui::text(fmt::format("{:.6g} {}", value, s.unit)) | ftxui::color(ftxui::Color::IndianRed) |
                    ftxui::bold,
            }),
        });
      });

      // Upper bound on fragments a single custom (manual-mode) SPN may have.
      static constexpr auto max_fragments = 5u;

      // Fragment buttons + switcher (full width, above the main horizontal layout)
      auto frag_switcher = ftxui::Container::Vertical({
          ftxui::Container::Horizontal({
              ftxui::Checkbox({
                  .transform = [&s](const ftxui::EntryState &state) -> ftxui::Element {
                    const bool at_limit = s.fragments.size() >= max_fragments;
                    auto el = ftxui::text(">[add_fragment]<") |
                              ftxui::color(at_limit ? ftxui::Color::GrayDark : ftxui::Color::Cyan);
                    if (state.focused || state.active) {
                      el = el | ftxui::bold | ftxui::bgcolor(ftxui::Color::Grey11);
                    }

                    return el;
                  },

                  .on_change =
                      [&s, rebuild_frag_fields]() {
                        if (s.fragments.size() >= max_fragments)
                          return;
                        s.fragments.push_back({});
                        s.active_fragment = static_cast<int32_t>(s.fragments.size()) - 1;
                        rebuild_frag_fields();
                      },
              }),
              ftxui::Renderer([]() { return ftxui::text("  "); }),
              ftxui::Checkbox({
                  .transform = [&s](const ftxui::EntryState &state) -> ftxui::Element {
                    const bool at_limit = s.fragments.size() <= 1;
                    auto el = ftxui::text(">[remove_fragment]<") |
                              ftxui::color(at_limit ? ftxui::Color::GrayDark : ftxui::Color::Cyan);
                    if (state.focused || state.active)
                      el = el | ftxui::bold | ftxui::bgcolor(ftxui::Color::Grey11);
                    return el;
                  },
                  .on_change =
                      [&s, rebuild_frag_fields]() {
                        if (s.fragments.size() > 1) {
                          s.fragments.erase(s.fragments.begin() + s.active_fragment);
                          if (s.active_fragment >= static_cast<int32_t>(s.fragments.size()))
                            s.active_fragment = static_cast<int32_t>(s.fragments.size()) - 1;
                          rebuild_frag_fields();
                        }
                      },
              }),
          }),

          ftxui::Container::Horizontal({
              ftxui::Checkbox({
                  .transform = [](const ftxui::EntryState &state) -> ftxui::Element {
                    auto el = ftxui::text("[<]") | ftxui::color(ftxui::Color::Cyan);

                    if (state.focused || state.active) {
                      el = el | ftxui::bold | ftxui::bgcolor(ftxui::Color::Grey11);
                    }

                    return el;
                  },

                  .on_change =
                      [&s, rebuild_frag_fields]() {
                        if (s.active_fragment > 0) {
                          s.active_fragment--;
                          rebuild_frag_fields();
                        }
                      },
              }),

              ftxui::Renderer([&s]() {
                ftxui::Elements tabs;
                tabs.push_back(ftxui::text(" "));
                for (size_t i = 0; i < s.fragments.size(); ++i) {
                  if (i > 0) {
                    tabs.push_back(ftxui::text(" | "));
                  }

                  auto label = ftxui::text(fmt::format("fragment#{}", i));

                  if (static_cast<int32_t>(i) == s.active_fragment) {
                    label = label | ftxui::bold | ftxui::color(ftxui::Color::Red);

                  } else {
                    label = label | ftxui::color(ftxui::Color::Cyan);
                  }

                  tabs.push_back(label);
                }

                tabs.push_back(ftxui::text(" "));
                return ftxui::hbox(std::move(tabs));
              }),

              ftxui::Checkbox({
                  .transform = [](const ftxui::EntryState &state) -> ftxui::Element {
                    auto el = ftxui::text("[>]") | ftxui::color(ftxui::Color::Cyan);
                    if (state.focused || state.active)
                      el = el | ftxui::bold | ftxui::bgcolor(ftxui::Color::Grey11);
                    return el;
                  },
                  .on_change =
                      [&s, rebuild_frag_fields]() {
                        if (s.active_fragment < static_cast<int32_t>(s.fragments.size()) - 1) {
                          s.active_fragment++;
                          rebuild_frag_fields();
                        }
                      },
              }),
          }),
      });

      // Main horizontal: checkbox | settings | payload
      auto main_row = ftxui::Container::Horizontal({
          ftxui::Checkbox({
              .checked = &s.selected,
              .transform = [&s](const ftxui::EntryState &state) -> ftxui::Element {
                return ftxui::text(s.selected ? "[X]" : "[ ]") |
                       (s.selected ? ftxui::color(ftxui::Color::Red) : ftxui::color(ftxui::Color::Cyan));
              },
          }) | ftxui::vcenter |
              ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 5),

          ftxui::Container::Vertical({
              make_field("SPN id:", "enter id here ...", &s.spn_id),
              make_field("SPN name:", "enter name here ...", &s.spn_name),
              make_field("SPN resolution:", "enter resolution here ...", &s.resolution),
              make_field("SPN offset:", "enter offset here ...", &s.offset),
              make_field("SPN unit:", "enter unit here ...", &s.unit),
              frag_fields_container,
              ftxui::Container::Horizontal({
                  ftxui::Renderer([]() {
                    return ftxui::text(fmt::format("{:24}", "Endianness:")) | ftxui::bold |
                           ftxui::color(ftxui::Color::Yellow);
                  }),

                  ftxui::Checkbox({
                      .checked = &s.big_endian,
                      .transform = [&s](const ftxui::EntryState &state) -> ftxui::Element {
                        auto el = ftxui::hbox({
                            ftxui::text("< "),
                            ftxui::text("little") |
                                (!s.big_endian ? (ftxui::bold | ftxui::color(ftxui::Color::Red)) : ftxui::nothing),
                            ftxui::text(" | "),
                            ftxui::text("big") |
                                (s.big_endian ? (ftxui::bold | ftxui::color(ftxui::Color::Red)) : ftxui::nothing),
                            ftxui::text(" >"),
                        });

                        if (state.focused || state.active) {
                          el = el | ftxui::bold | ftxui::bgcolor(ftxui::Color::Grey11);
                        }

                        return el;
                      },
                  }),
              }),
          }) | ftxui::flex,

          ftxui::Renderer([]() { return ftxui::text("    "); }),
          payload_view | ftxui::flex,
      });

      auto component = ftxui::Container::Vertical({frag_switcher, main_row}) | ftxui::border | ftxui::flex;

      m_current_spn_settings_->Add({component});
      m_spnSettingsFormMap_[m_canid_].insert_or_assign(k, component);
    }

  private:
    ftxui::Component m_current_spn_settings_ = ftxui::Container::Vertical({});
    std::string m_canid_;
    ftxui::ScreenInteractive *m_screen_;
    signals_map_t &m_smap_;
    std::function<std::vector<uint8_t>()> m_data_getter_;
    std::map<std::string, std::map<int32_t, ftxui::Component>> &m_spnSettingsFormMap_;
    spn_settings_map_t &m_spnSettingsMap_;
  };

  return ftxui::Make<Impl>(screen, smap, canid, canid_active, std::move(data_getter), dialog, modal_shown,
                           spnSettingsFormMap, spnSettingsMap);
}
