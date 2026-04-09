#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <boost/signals2.hpp>

#define FMT_HEADER_ONLY
#include <fmt/format.h>

#include "signals.hpp"
#include <map>
#include <optional>
#include "json/json.hpp"

ftxui::Component makeSpnSelector(ftxui::ScreenInteractive *screen, signals_map_t &smap, nlohmann::json data) {
  class Impl : public ftxui::ComponentBase {
  public:
    Impl(ftxui::ScreenInteractive *screen, signals_map_t &smap, nlohmann::json data) {}
  };

  return ftxui::Make<Impl>(screen, smap, data);
  
}
