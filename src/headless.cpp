#include "headless.hpp"

#include <cstdint>
#include <exception>
#include <fstream>

#define FMT_HEADER_ONLY
#include <fmt/format.h>
#include <fmt/ranges.h>

HeadlessHandler::HeadlessHandler(const std::string &output_file)
    : output_file_(output_file) {}

void HeadlessHandler::onDatabaseReady(sqlite::database &db) {
  database_ = &db;
}

void HeadlessHandler::onBatch(const std::vector<can_frame_update_s> &batch) {
  if (!database_) return;

  extern std::pair<nlohmann::json, nlohmann::json> processFrame(sqlite::database &db, const std::string &iface, const std::string &canid, const std::vector<uint8_t> &data);

  for (const auto &entry : batch) {
    const auto &iface = entry.iface;
    const auto &canid = entry.canid;
    const auto &frame_data = entry.data;

    if (configuration_map_.contains(canid)) continue;

    auto [verbose, brief] = processFrame(*database_, iface, canid, frame_data.payload);
    configuration_map_.insert({
        canid,
        {std::move(verbose), std::move(brief)},
    });

    nlohmann::json j;
    j[canid] = verboseToExportJson(configuration_map_.at(canid).first);

    if (!output_file_.empty()) {
      nlohmann::json file_j;

      {
        std::ifstream fin(output_file_);
        if (fin.is_open() && fin.peek() != std::ifstream::traits_type::eof()) {
          try {
            fin >> file_j;
          } catch (const std::exception &e) {
            fmt::print("{}\r\n", e.what());
            return;
          }
        }
      }

      for (const auto &[k, v] : j.items()) {
        file_j[k] = v;
      }

      {
        std::ofstream fout(output_file_, std::ofstream::out | std::ofstream::trunc);
        if (fout.is_open()) {
          fout << file_j;
        }
      }
    } else {
      fmt::print("{}\r\n\r\n", j.dump());
    }
  }
}
