#include <map>
#include <memory>
#include <stdexcept>
#include <string>

#define FMT_HEADER_ONLY
#include <fmt/format.h>

#include <xlnt/xlnt.hpp>

#include "j1939_db.hpp"
#include "sqlite_modern_cpp.h"

std::unique_ptr<sqlite::database> parseXlsx(const std::string &file) {
  auto db_ptr = std::make_unique<sqlite::database>(":memory:");
  auto &db = *db_ptr;

  initJ1939Database(db);

  const auto &pgn_mapping = J1939MappingTables::pgn();
  const auto &spn_mapping = J1939MappingTables::spn();

  // Pre-build reverse lookup: column_index -> db_column_name
  std::map<size_t, std::string> pgn_col_to_db, spn_col_to_db;

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
      auto col_idx = cell.column_index();
      if (auto it = pgn_mapping.find(value); it != pgn_mapping.end() && col_idx == std::get<0u>(it->second)) {
        pgn_col_to_db[col_idx] = std::string(std::get<1u>(it->second));
      } else if (auto it2 = spn_mapping.find(value); it2 != spn_mapping.end() && col_idx == std::get<0u>(it2->second)) {
        spn_col_to_db[col_idx] = std::string(std::get<1u>(it2->second));
      }
    }
  }

  const auto pgn_insert_sql = buildPgnInsertSql();
  const auto spn_insert_sql = buildSpnInsertSql();

  db << "BEGIN TRANSACTION";

  for (const auto &row : ws.rows()) {
    std::map<std::string, std::string> pgn_row_map, spn_row_map;
    for (const auto &cell : row) {
      if (cell.row() != 1u) {
        auto col_idx = cell.column_index();
        auto value = cell.to_string();
        if (value.empty()) continue;

        if (auto it = pgn_col_to_db.find(col_idx); it != pgn_col_to_db.end()) {
          pgn_row_map[it->second] = std::move(value);
        } else if (auto it2 = spn_col_to_db.find(col_idx); it2 != spn_col_to_db.end()) {
          spn_row_map[it2->second] = std::move(value);
        }
      }
    }

    if (!pgn_row_map.empty() && !spn_row_map.empty()) {
      insertJ1939Row(db, pgn_insert_sql, spn_insert_sql, pgn_row_map, spn_row_map);
    }
  }

  db << "COMMIT";

  return db_ptr;
}
