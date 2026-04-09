// #include "src/canid_unit.hpp"
#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <memory>

#define FMT_HEADER_ONLY
#include <fmt/format.h>

#include "signals.hpp"
#include <map>
#include <optional>

ftxui::Component makeCanSettingsExportDialog(ftxui::ScreenInteractive *scr, signals_map_t &smap, ftxui::Component canids, bool &shown, bool &file_export_shown) {
  class Impl : public ftxui::ComponentBase {
  public:
    Impl(ftxui::ScreenInteractive *scr, signals_map_t &smap, ftxui::Component canids, bool &shown, bool &file_export_shown) {
      auto cmps = ftxui::Container::Vertical({});

      for (uint32_t i = 0; i < canids->ChildCount(); ++i) {
        cmps->Add(canids->ChildAt(i));
      }

      Add({
          ftxui::Container::Vertical({
              cmps,

              ftxui::Container::Horizontal({
                  ftxui::Button({.label = "Export", .on_click = [&file_export_shown]() { file_export_shown = true; }}),
                  ftxui::Button({.label = "Close", .on_click = [&shown]() { shown = false; }}),
              }),
          }),
      });
    }
  };

  return ftxui::Make<Impl>(scr, smap, canids, shown, file_export_shown);
}
