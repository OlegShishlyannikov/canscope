#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>
#include <unistd.h>

#define FMT_HEADER_ONLY
#include <fmt/format.h>
#include <fmt/ranges.h>

// for XLSX files
#include <xlnt/xlnt.hpp>

// For sqlite
#include "parsers.hpp"
#include "sqlite_modern_cpp.h"

static void init_db(sqlite::database &db) {
  try {
    db << R"(
      CREATE TABLE IF NOT EXISTS pgns (
        id INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,
        pgn INTEGER UNIQUE,
        pg_label TEXT,
        pg_acronym TEXT,
        pg_descr TEXT,
        edp INTEGER,
        dp INTEGER,
        pf INTEGER,
        ps INTEGER,
        pg_datalen INTEGER,
        pg_priority INTEGER
      );
    )";

    db << R"(
      CREATE TABLE IF NOT EXISTS spns (
        id INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,
        pgn INTEGER,
        spn INTEGER UNIQUE,
        spn_name TEXT,
        spn_pos TEXT,
        spn_length INTEGER,
        resolution REAL,
        offset REAL,
        data_range TEXT,
        min_value REAL,
        max_value REAL,
        units TEXT,
        slot_id TEXT,
        slot_name TEXT,
        spn_type TEXT
      );
    )";

    db << R"(
      CREATE TABLE IF NOT EXISTS spn_fragments (
        id INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,
        spn INTEGER,
        pgn INTEGER,
        byte_offset INTEGER,
        bit_offset INTEGER,
        size INTEGER
      );
    )";

    db << "PRAGMA journal_mode = OFF";
    db << "PRAGMA synchronous = OFF";
    db << "PRAGMA foreign_keys = ON;";
  } catch (const sqlite::sqlite_exception &e) {
    throw;
  }
}

std::unique_ptr<sqlite::database> parseXlsx(const std::string &file) {
  auto db_ptr = std::make_unique<sqlite::database>(":memory:");
  auto &db = *db_ptr;

  init_db(db);

  static const std::map<std::string_view, std::tuple<size_t, std::string_view>>
      pgn_mapping_table =
          {
              {"PGN", {5u, "pgn"}},
              {"Parameter Group Label", {6u, "pg_label"}},
              {"PG Acronym", {7u, "pg_acronym"}},
              {"PG Description", {8u, "pg_descr"}},
              {"EDP", {9u, "edp"}},
              {"DP", {10u, "dp"}},
              {"PF", {11u, "pf"}},
              {"PS", {12u, "ps"}},
              {"PG Data Length", {15u, "pg_datalen"}},
              {"Default Priority", {16u, "pg_priority"}},
          },

      spn_mapping_table = {
          {"SPN", {19u, "spn"}},
          {"SPN Name", {20u, "spn_name"}},
          {"SPN Position in PG", {18u, "spn_pos"}},
          {"SPN Length", {22u, "spn_length"}},
          {"Offset", {24u, "offset"}},
          {"Data Range", {25u, "data_range"}},
          {"Resolution", {23u, "resolution"}},
          {"Units", {27u, "units"}},
          {"SLOT Identifier", {28u, "slot_id"}},
          {"SLOT Name", {29u, "slot_name"}},
          {"SPN Type", {30u, "spn_type"}},
      };

  std::map<size_t, std::string> pgn_headers, spn_headers;
  xlnt::workbook wb;

  if (xlnt::path(file).exists()) {
    wb = xlnt::workbook(xlnt::path(file));

    if (wb.sheet_count() == 0u) {
      throw std::runtime_error(fmt::format("Workbook {} is empty", file));
    }

    wb.active_sheet(0);
  } else {

    throw std::runtime_error(fmt::format("File {} does not exists", file));
  }

  xlnt::worksheet ws = wb.active_sheet();
  for (const auto &col : ws.columns()) {
    if (auto [cell, value] = std::pair{col[0], col[0].to_string()}; !value.empty()) {
      if (pgn_mapping_table.contains(value)) {

        auto it = pgn_headers.insert_or_assign(cell.column_index(), value);
      } else if (spn_mapping_table.contains(value)) {

        auto it = spn_headers.insert_or_assign(cell.column_index(), value);
      }
    }
  }

  for (const auto &row : ws.rows()) {
    std::map<std::string, std::string> pgn_row_map, spn_row_map;
    for (const auto &cell : row) {
      if (cell.row() != 1u) {

        if (pgn_headers.contains(cell.column_index())) {
          if (!cell.to_string().empty()) {
            pgn_row_map.insert_or_assign(
                [&]() {
                  std::string ret;

                  for (const auto &[k, v] : pgn_mapping_table) {
                    if (k == pgn_headers[cell.column_index()] && cell.column_index() == std::get<0u>(v)) {

                      ret = std::get<1u>(v);
                    }
                  }

                  return ret;
                }(),

                cell.to_string());
          }
        } else if (spn_headers.contains(cell.column_index())) {
          if (!cell.to_string().empty()) {
            spn_row_map.insert_or_assign(
                [&]() {
                  std::string ret;

                  for (const auto &[k, v] : spn_mapping_table) {
                    if (k == spn_headers[cell.column_index()] && cell.column_index() == std::get<0u>(v)) {

                      ret = std::get<1u>(v);
                    }
                  }

                  return ret;
                }(),

                cell.to_string());
          }
        }
      }
    }

    // Add rows to database
    if (!pgn_row_map.empty() && !spn_row_map.empty()) {
      for (const auto &[k, v] : pgn_row_map) {
        try {
          auto ps = db << fmt::format(
                        "INSERT OR REPLACE INTO pgns ({}) VALUES ({})",
                        [&]() {
                          std::string ret;

                          for (const auto &[k, v] : pgn_mapping_table) {
                            auto end = pgn_mapping_table.end();
                            bool is_last = k == (--end)->first;
                            ret += fmt::format("{}{}", std::get<1u>(v), !is_last ? ", " : "");
                          }

                          return ret;
                        }(),

                        [&]() {
                          std::string ret;
                          for (const auto &[k, _] : pgn_mapping_table) {
                            auto end = pgn_mapping_table.end();
                            bool is_last = k == (--end)->first;
                            ret += !is_last ? "?, " : "?";
                          }

                          return ret;
                        }());

          for (const auto &[k, v] : pgn_mapping_table) {
            ps << pgn_row_map[std::get<1u>(v).data()];
          }

          ps.execute();
        } catch (const sqlite::sqlite_exception &e) {
          if (e.get_extended_code() == SQLITE_CONSTRAINT_UNIQUE) {
            continue;
          }

          throw;
        }
      }

      struct {
        bool parts_inserted_flag = false;
        double min = 0.0, max = 0.0;
        size_t size_bits = 0u;
      } spn_settings_calculated;

      // Pre-compute size_bits before the main loop (resolution calculation depends on it)
      {
        auto it = spn_mapping_table.find("SPN Length");
        if (it != spn_mapping_table.end()) {
          auto size = parsers::parseSpnSize(spn_row_map[std::get<1u>(it->second).data()]);
          spn_settings_calculated.size_bits = size.has_value() ? size.value().size_bits : 0u;
        }
      }

      for (const auto &[k, v] : spn_row_map) {
        try {
          auto ps = db << fmt::format(
                        "INSERT OR REPLACE INTO spns ({}) VALUES ({})",
                        [&]() {
                          std::string ret;

                          ret += "pgn, ";
                          for (const auto &[k, v] : spn_mapping_table) {
                            auto end = spn_mapping_table.end();
                            bool is_last = k == (--end)->first;

                            // Split 'data range' to two columns 'min' and 'max'
                            if (std::get<1u>(v) == "data_range") {

                              ret += is_last ? ", min_value, max_value" : "min_value, max_value, ";
                            } else {

                              ret += fmt::format("{}{}", std::get<1u>(v), !is_last ? ", " : "");
                            }
                          }

                          return ret;
                        }(),

                        [&]() {
                          std::string ret;

                          ret += "?, ";
                          for (const auto &[k, v] : spn_mapping_table) {
                            auto end = spn_mapping_table.end();
                            bool is_last = k == (--end)->first;

                            // Split 'data range' to two columns 'min' and 'max'
                            if (std::get<1u>(v) == "data_range") {

                              ret += is_last ? ", ?, ?" : "?, ?, ";
                            } else {

                              ret += !is_last ? "?, " : "?";
                            }
                          }

                          return ret;
                        }());

          ps << pgn_row_map["pgn"];
          for (const auto &[k, v] : spn_mapping_table) {
            if (std::get<1u>(v) == "data_range") { // Parse 'data range' string and split it to two columns 'min' and 'max'

              auto range = parsers::parseSpnDataRange(spn_row_map[std::get<1u>(v).data()]);
              if (range.has_value()) {
                (spn_settings_calculated.min = range.value().min, spn_settings_calculated.max = range.value().max);

                for (const auto &val : {range.value().min, range.value().max}) {
                  ps << val;
                }
              } else {
                ps << nullptr << nullptr;
              }
            } else if (std::get<1u>(v) == "offset") {

              auto offset = parsers::parseSpnOffset(spn_row_map[std::get<1u>(v).data()]);
              if (offset.has_value()) {
                ps << offset.value().offset;
              } else {
                ps << nullptr;
              }
            } else if (std::get<1u>(v) == "spn_length") {

              auto size = parsers::parseSpnSize(spn_row_map[std::get<1u>(v).data()]);
              spn_settings_calculated.size_bits = size.has_value() ? size.value().size_bits : 0u;
              ps << (size.has_value() ? size.value().size_bits : 0u);
            } else if (std::get<1u>(v) == "resolution") {
              double calculated = (spn_settings_calculated.max - spn_settings_calculated.min) / (std::pow(2.0f, spn_settings_calculated.size_bits) - 1u);

              // If is discrete -- use calculated resolution
              if (std::fabs(calculated - 1.0) < 1e-9) {

                ps << calculated;
              } else {

                auto resolution = parsers::parseSpnResolution(spn_row_map[std::get<1u>(v).data()]);
                ps << (resolution.has_value() ? resolution.value().resolution : 1.0f);
              }
            } else if (std::get<1u>(v) == "spn_pos") {
              ps << spn_row_map[std::get<1u>(v).data()];
            } else {

              ps << spn_row_map[std::get<1u>(v).data()];
            }
          }

          ps.execute();

          // Insert SPN fragments after successful spns INSERT
          if (!spn_settings_calculated.parts_inserted_flag) {
            auto size = parsers::parseSpnSize(spn_row_map[std::get<1u>(spn_mapping_table.at("SPN Length")).data()]);

            if (size.has_value()) {
              auto spn_fragments = parsers::parseSpnPosition(size.value().size_bits, spn_row_map[std::get<1u>(spn_mapping_table.at("SPN Position in PG")).data()]);

              if (spn_fragments.has_value()) {
                auto spn = std::stoll(spn_row_map["spn"]);
                for (const auto &part : spn_fragments.value().spn_fragments) {
                  db << R"(INSERT OR REPLACE INTO spn_fragments (spn, pgn, byte_offset, bit_offset, size) VALUES (?, ?, ?, ?, ?);)" << spn << std::stoll(pgn_row_map["pgn"])
                     << part.byte_offset << part.bit_offset << part.size;
                }

                spn_settings_calculated.parts_inserted_flag = true;
              }
            }
          }
        } catch (const sqlite::sqlite_exception &e) {
          if (e.get_extended_code() == SQLITE_CONSTRAINT_UNIQUE) {
            continue;
          }

          throw;
        }
      }
    }
  }

  return db_ptr;
}
