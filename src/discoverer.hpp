#pragma once

#include "can_data.hpp"
#include "sqlite_modern_cpp.h"

#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

class DiscovererHandler {
public:
  explicit DiscovererHandler(const std::string &output_file);

  void onDatabaseReady(sqlite::database &db);
  void onBatch(const std::vector<can_frame_update_s> &batch);

private:
  std::string output_file_;
  sqlite::database *database_ = nullptr;
  std::map<std::string, std::pair<nlohmann::json, nlohmann::json>> configuration_map_;
};
