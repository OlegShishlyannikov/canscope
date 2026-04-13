#pragma once

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
#include <map>
#include <memory>
#include <optional>

class CanIDUnit : public ftxui::ComponentBase {
public:
  CanIDUnit(const std::string &iface, const std::string &canid, const std::string &protocol, size_t &spn_count,
            const std::vector<uint8_t> &data, ftxui::ScreenInteractive *screen, signals_map_t &smap,
            ftxui::Component content, ftxui::Component container, ftxui::Component spn_settings_dialog,
            ftxui::Component cansettings_dialog, bool is_deployed, bool is_verbose, bool is_brief, bool is_manual,
            std::string &, bool &, bool &canbus_parameters_export_shown, bool &filedialog_shown,
            std::map<std::string, std::map<int32_t, ftxui::Component>> &spnSettingsFormMap,
            spn_settings_map_t &spnSettingsMap);

  inline const std::string &getIfaceName() const { return m_iface_; }
  inline const std::string &getCanID() const { return m_canid_; }

  inline std::string getLabel() const {
    if (m_data_verbose_ && !m_data_verbose_->is_null() && m_data_verbose_->contains("Label")) {
      return (*m_data_verbose_)["Label"].get<std::string>();
    }

    return {};
  }

  inline size_t getDataSize() const { return m_data_.size(); }
  inline const std::vector<uint8_t> &getData() const { return m_data_; }

  inline bool getDeployed() const { return m_deployed_; }
  inline bool getVerbose() const { return m_verbose_; }
  inline bool getBrief() const { return m_brief_; }
  inline bool getManual() const { return m_manual_mode_; }
  inline ftxui::Component getSpnSettingsForm() { return m_spnSettingsForm_; }
  inline const auto &getParametersExportMap() const { return s_canbus_parameters_export_map_; }

  void update(const can_frame_data_s &data, const can_frame_diff_s &diff, std::shared_ptr<nlohmann::json> verbose,
              std::shared_ptr<nlohmann::json> brief);

  bool OnEvent(ftxui::Event event) override;

private:
  const ftxui::Component m_spnSettingsForm_;
  const std::string m_canid_, m_iface_;
  std::vector<uint8_t> m_data_;
  mutable bool m_deployed_ = false, m_verbose_ = false, m_brief_ = true, m_manual_mode_ = false;
  can_frame_diff_s m_diff_;
  std::string m_last_update_time_;
  bool m_hovered_ = false;
  ftxui::Box m_box_ = {};
  std::shared_ptr<nlohmann::json> m_data_verbose_ = std::make_shared<nlohmann::json>(nullptr);
  std::shared_ptr<nlohmann::json> m_data_short_ = std::make_shared<nlohmann::json>(nullptr);
  ftxui::Component m_brief_content_, m_verbose_content_;
  ftxui::Component m_cansettings_dialog_;
  spn_settings_map_t *m_spnSettingsMap_ = nullptr;
  size_t m_last_verbose_spn_count_ = 0;
  size_t m_last_brief_spn_count_ = 0;
  ftxui::Component m_export_selectors_;
  std::map<int32_t, std::pair<ftxui::Component, size_t>> m_export_custom_containers_;

  static inline std::map<
      /* canid */ std::string,
      std::tuple</* deployed flag */ bool, /* has data flag */ bool,
                 /* Selected spns to export  */
                 std::map</* spn name */ std::string,
                          std::tuple</* deployed  */ bool, /* selected */ bool, /* data */ nlohmann::json>>>>
      s_canbus_parameters_export_map_ = {};
};
