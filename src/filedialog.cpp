#include <boost/asio.hpp>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <map>
#include <nlohmann/json.hpp>

#define FMT_HEADER_ONLY
#include <fmt/format.h>

#include "signals.hpp"

static std::filesystem::path currentPath = std::filesystem::current_path(),
                             pathToExport = std::filesystem::current_path();
static std::string fileName = "";

static void updateFileDialog(ftxui::Component entryList, signals_map_t &smap) {
  entryList->DetachAllChildren();

  for (const auto &entry : {".", ".."}) {
    entryList->Add({
        ftxui::Button({
            .label = entry,
            .on_click =
                [entryList, entry, &smap]() {
                  if (std::string(entry) == "..") {
                    currentPath = currentPath.parent_path();
                    updateFileDialog(entryList, smap);
                  }
                },

            .transform = [entry](const ftxui::EntryState &state) -> ftxui::Element {
              return ftxui::text(entry) | ftxui::color(ftxui::Color::Blue) |
                     (state.focused ? ftxui::bgcolor(ftxui::Color::Grey7) : ftxui::nothing);
            },
        }),
    });
  }

  for (const auto &entry : std::filesystem::directory_iterator(currentPath)) {
    entryList->Add({
        ftxui::Button({
            .label = entry.path().filename().c_str(),
            .on_click =
                [entry, entryList, &smap]() {
                  if (entry.is_directory()) {
                    currentPath = entry;
                  } else {
                    fileName = entry.path().filename().c_str();
                    pathToExport = entry.path();
                  }

                  updateFileDialog(entryList, smap);
                },

            .transform = [entry](const ftxui::EntryState &state) -> ftxui::Element {
              return ftxui::text(entry.path().filename().c_str()) |
                     (entry.is_directory() ? ftxui::color(ftxui::Color::Blue) : ftxui::nothing) |
                     (state.focused ? ftxui::bgcolor(ftxui::Color::Grey7) : ftxui::nothing);
            },
        }),
    });
  }
};

ftxui::Component makeFileDialog(ftxui::ScreenInteractive *scr, signals_map_t &smap, bool &shown) {
  class FileDialog : public ftxui::ComponentBase {
  public:
    explicit FileDialog(ftxui::ScreenInteractive *scr, signals_map_t &smap, bool &shown) {
      static bool checked = false;
      auto entryList = ftxui::Container::Vertical({});

      Add({
          ftxui::Container::Vertical({
              ftxui::Renderer([&]() -> ftxui::Element { return ftxui::text("Export file"); }) |
                  ftxui::color(ftxui::Color::Red) | ftxui::hcenter,
              ftxui::Renderer([]() { return ftxui::separator(); }),

              ftxui::Container::Horizontal({
                  ftxui::Renderer([]() -> ftxui::Element {
                    return ftxui::text(fmt::format("{}: ", currentPath.c_str())) | ftxui::bold;
                  }),

                  ftxui::Input({
                      .content = &fileName,
                      .multiline = false,
                      .on_change = []() { (pathToExport = currentPath) /= fileName; },
                  }),
              }),

              ftxui::Renderer([]() { return ftxui::separator(); }),
              entryList | ftxui::vscroll_indicator | ftxui::frame | ftxui::flex,
              ftxui::Renderer([]() { return ftxui::separator(); }),

              ftxui::Container::Horizontal({
                  ftxui::Button({
                      .on_click =
                          [&]() {
                            smap.get<void(const std::string &)>("export_file_request")
                                ->operator()(pathToExport.c_str());
                            shown = false;
                          },

                      .transform = [this](const ftxui::EntryState &state) -> ftxui::Element {
                        return ftxui::text(" >[export]< ") | (state.focused ? ftxui::bold : ftxui::nothing) |
                               ftxui::color(ftxui::Color::Cyan) |
                               (state.focused ? ftxui::bgcolor(ftxui::Color::Grey11) : ftxui::nothing);
                      },
                  }),

                  ftxui::Button({
                      .on_click = [&]() { shown = false; },
                      .transform = [this](const ftxui::EntryState &state) -> ftxui::Element {
                        return ftxui::text(" >[cancel]< ") | (state.focused ? ftxui::bold : ftxui::nothing) |
                               ftxui::color(ftxui::Color::Cyan) |
                               (state.focused ? ftxui::bgcolor(ftxui::Color::Grey11) : ftxui::nothing);
                      },
                  }),
              }) | ftxui::hcenter,

          }) | ftxui::border |
              ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 96) | ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, 48),
      });

      updateFileDialog(entryList, smap);
    }
  };

  return ftxui::Make<FileDialog>(scr, smap, shown);
}
