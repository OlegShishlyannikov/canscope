#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#define FMT_HEADER_ONLY
#include <fmt/format.h>

#include "j1939_db.hpp"
#include "sqlite_modern_cpp.h"

static std::vector<std::string> parseCsvLine(const std::string &line) {
  std::vector<std::string> fields;
  std::string field;
  bool in_quotes = false;

  for (size_t i = 0; i < line.size(); ++i) {
    char c = line[i];

    if (in_quotes) {
      if (c == '"') {
        if (i + 1 < line.size() && line[i + 1] == '"') {
          field += '"';
          ++i;
        } else {
          in_quotes = false;
        }
      } else {
        field += c;
      }
    } else {

      if (c == '"') {
        in_quotes = true;
      } else if (c == ',') {
        fields.push_back(std::move(field));
        field.clear();
      } else {
        field += c;
      }
    }
  }

  fields.push_back(std::move(field));
  return fields;
}

std::unique_ptr<sqlite::database> parseCsv(const std::string &file) {
  std::ifstream ifs(file);
  if (!ifs.is_open()) {
    throw std::runtime_error(fmt::format("Cannot open file {}", file));
  }

  auto db_ptr = std::make_unique<sqlite::database>(":memory:");
  auto &db = *db_ptr;

  initJ1939Database(db);

  const auto &pgn_mapping = J1939MappingTables::pgn();
  const auto &spn_mapping = J1939MappingTables::spn();

  // Read header line and build column_index -> db_column_name maps
  std::map<size_t, std::string> pgn_col_to_db, spn_col_to_db;

  std::string header_line;
  if (!std::getline(ifs, header_line)) {
    throw std::runtime_error("CSV file is empty");
  }

  auto headers = parseCsvLine(header_line);
  for (size_t i = 0; i < headers.size(); ++i) {
    const auto &h = headers[i];
    if (auto it = pgn_mapping.find(h); it != pgn_mapping.end()) {

      pgn_col_to_db[i] = std::string(std::get<1u>(it->second));
    } else if (auto it2 = spn_mapping.find(h); it2 != spn_mapping.end()) {

      spn_col_to_db[i] = std::string(std::get<1u>(it2->second));
    }
  }

  const auto pgn_insert_sql = buildPgnInsertSql();
  const auto spn_insert_sql = buildSpnInsertSql();

  db << "BEGIN TRANSACTION";

  std::string line;
  while (std::getline(ifs, line)) {
    if (line.empty())
      continue;

    while (std::count(line.begin(), line.end(), '"') % 2 != 0) {
      std::string next;
      if (!std::getline(ifs, next))
        break;
      line += '\n';
      line += next;
    }

    auto fields = parseCsvLine(line);
    std::map<std::string, std::string> pgn_row_map, spn_row_map;

    for (size_t i = 0; i < fields.size(); ++i) {
      if (fields[i].empty())
        continue;

      if (auto it = pgn_col_to_db.find(i); it != pgn_col_to_db.end()) {
        pgn_row_map[it->second] = std::move(fields[i]);
      } else if (auto it2 = spn_col_to_db.find(i); it2 != spn_col_to_db.end()) {
        spn_row_map[it2->second] = std::move(fields[i]);
      }
    }

    if (!pgn_row_map.empty() && !spn_row_map.empty()) {
      insertJ1939Row(db, pgn_insert_sql, spn_insert_sql, pgn_row_map, spn_row_map);
    }
  }

  db << "COMMIT";

  return db_ptr;
}
