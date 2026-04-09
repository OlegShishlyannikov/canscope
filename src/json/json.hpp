#pragma once

#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>

#include "button.hpp"
#include "expander.hpp"
#include "ftxui/component/component.hpp"          // for Renderer, ResizableSplitBottom, ResizableSplitLeft, ResizableSplitRight, ResizableSplitTop
#include "ftxui/component/screen_interactive.hpp" // for ScreenInteractive
#include "ftxui/dom/elements.hpp"                 // for Element, operator|, text, center, border
#include "mytoggle.hpp"

bool ParseJSON(const std::string &input, nlohmann::json &out);

ftxui::Component From(const nlohmann::json &json, bool is_last, int depth, const Expander &expander);
ftxui::Component FromLive(std::shared_ptr<nlohmann::json> root, const nlohmann::json::json_pointer &ptr, bool is_last, int depth, const Expander &expander);
ftxui::Component FromString(const nlohmann::json &json, bool is_last);
ftxui::Component FromNumber(const nlohmann::json &json, bool is_last);
ftxui::Component FromBoolean(const nlohmann::json &json, bool is_last);
ftxui::Component FromNull(bool is_last);
ftxui::Component FromObject(const ftxui::Component &prefix, const nlohmann::json &json, bool is_last, int depth, const Expander &expander);
ftxui::Component FromArrayAny(const ftxui::Component &prefix, const nlohmann::json &json, bool is_last, int depth, const Expander &expander);
ftxui::Component FromArray(const ftxui::Component &prefix, const nlohmann::json &json, bool is_last, int depth, const Expander &expander);
ftxui::Component FromTable(const ftxui::Component &prefix, const nlohmann::json &json, bool is_last, int depth, const Expander &expander);
ftxui::Component FromKeyValue(const std::string &key, const nlohmann::json &value, bool is_last, int depth, const Expander &expander);
ftxui::Component Empty();
ftxui::Component Unimplemented();
ftxui::Component Basic(const std::string &value, ftxui::Color c, bool is_last);
