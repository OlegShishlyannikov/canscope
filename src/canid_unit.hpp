#pragma once

#include <cstdint>
#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <boost/signals2.hpp>

#define FMT_HEADER_ONLY
#include <fmt/format.h>

#include "signals.hpp"
#include "tagsettings.hpp"
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <vector>

class CanIDUnit : public ftxui::ComponentBase {
public:
  CanIDUnit(const std::string &iface, const std::string &canid, const std::string &protocol, size_t &spn_count,
            const std::vector<uint8_t> &data, ftxui::ScreenInteractive *screen, signals_map_t &smap,
            ftxui::Component content, ftxui::Component container, ftxui::Component spn_settings_dialog,
            ftxui::Component cansettings_dialog, bool is_deployed, bool is_verbose, bool is_brief, bool is_manual,
            bool is_charts, std::string &, bool &, bool &canbus_parameters_export_shown, bool &filedialog_shown,
            std::map<std::string, std::map<int32_t, ftxui::Component>> &spnSettingsFormMap,
            spn_settings_map_t &spnSettingsMap);

  inline const std::string &getIfaceName() const { return m_id_.iface; }
  inline const std::string &getCanID() const { return m_id_.canid; }

  inline std::string getLabel() const {
    if (m_payload_.verbose && !m_payload_.verbose->is_null() && m_payload_.verbose->contains("Label")) {
      return (*m_payload_.verbose)["Label"].get<std::string>();
    }

    return {};
  }

  inline size_t getDataSize() const { return m_payload_.data.size(); }
  inline const std::vector<uint8_t> &getData() const { return m_payload_.data; }

  inline bool getDeployed() const { return m_ui_.deployed; }
  inline bool getVerbose() const { return m_ui_.verbose; }
  inline bool getBrief() const { return m_ui_.brief; }
  inline bool getManual() const { return m_ui_.manual; }
  inline ftxui::Component getSpnSettingsForm() { return m_spnSettingsForm_; }
  inline const auto &getParametersExportMap() const { return s_canbus_parameters_export_map_; }

  static inline std::string *s_export_filter_text_ = nullptr;

  static inline std::map<
      /* canid */ std::string,
      std::tuple</* deployed flag */ bool, /* has data flag */ bool,
                 /* Selected spns to export  */
                 std::map</* spn name */ std::string,
                          std::tuple</* deployed  */ bool, /* selected */ bool, /* data */ nlohmann::json>>>>
      s_canbus_parameters_export_map_ = {};

  void update(const can_frame_data_s &data, const can_frame_diff_s &diff, std::shared_ptr<nlohmann::json> verbose,
              std::shared_ptr<nlohmann::json> brief);

  bool OnEvent(ftxui::Event event) override;

private:
  static constexpr auto sc_chart_history_max_ = 8192u, sc_chart_height_ = 16u, sc_chart_width_ = 120u;

  // Identity.
  struct {
    std::string iface;
    std::string canid;
  } m_id_;

  // Latest payload and its derived representations.
  struct {
    std::vector<uint8_t> data;
    can_frame_diff_s diff;
    std::string last_update_time;
    std::shared_ptr<nlohmann::json> verbose = std::make_shared<nlohmann::json>(nullptr);
    std::shared_ptr<nlohmann::json> brief = std::make_shared<nlohmann::json>(nullptr);
    size_t last_verbose_spn_count = 0;
    size_t last_brief_spn_count = 0;
  } m_payload_;

  // Display-mode flags (mutually exclusive) + deployed/hover UI state.
  struct {
    bool deployed = false;
    bool verbose = false;
    bool brief = true;
    bool manual = false;
    bool charts = false;
    bool hovered = false;
    ftxui::Box box = {};
  } mutable m_ui_;

  // Charts tab: per-SPN rolling buffers and switcher state.
  struct {
    std::map<int32_t, std::deque<double>> history;
    std::vector<std::string> names;   // full names, shown below graph
    std::vector<int32_t> ids;         // unique key for history
    std::vector<std::string> labels;  // short labels shown in switcher
    std::vector<std::string> units;   // appended to current-value line
    std::deque<bool> flags;           // stable bool* per switcher checkbox
    ftxui::Component switcher;
    int selected_index = 0;
  } m_charts_state_;

  // Standalone components / non-grouped state.
  const ftxui::Component m_spnSettingsForm_;
  ftxui::Component m_brief_content_, m_verbose_content_, m_charts_content_;
  ftxui::Component m_cansettings_dialog_;
  ftxui::Component m_export_selectors_;
  std::map<int32_t, std::pair<ftxui::Component, size_t>> m_export_custom_containers_;
  spn_settings_map_t *m_spnSettingsMap_ = nullptr;
};
