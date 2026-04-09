#include "tagsettingrow.hpp"

ftxui::Component makeSpnSettingsRow(ftxui::ScreenInteractive *screen, signals_map_t &smap, const std::string &canid, ftxui::Component cmpCont, size_t spn_id,
                                    spn_settings_map_t &tagSettingsMap) {
  return ftxui::Make<SpnSettingRow>(screen, smap, canid, cmpCont, spn_id, tagSettingsMap);
}
