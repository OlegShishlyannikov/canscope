#include "j1939_db.hpp"
#include "parsers.hpp"

#include <cmath>

#define FMT_HEADER_ONLY
#include <fmt/format.h>

void initJ1939Database(sqlite::database &db) {
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
      spn_type TEXT,
      value_encoding TEXT
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

  db << "CREATE INDEX IF NOT EXISTS idx_spns_pgn ON spns(pgn);";
  db << "CREATE INDEX IF NOT EXISTS idx_spn_fragments_spn ON spn_fragments(spn);";
  db << "CREATE INDEX IF NOT EXISTS idx_spn_fragments_spn_pgn ON spn_fragments(spn, pgn);";
}

std::string buildPgnInsertSql() {
  const auto &pgn_mapping = J1939MappingTables::pgn();
  std::string cols, placeholders;
  for (const auto &[k, v] : pgn_mapping) {
    if (!cols.empty()) {
      cols += ", ";
      placeholders += ", ";
    }

    cols += std::get<1u>(v);
    placeholders += "?";
  }

  return fmt::format("INSERT OR REPLACE INTO pgns ({}) VALUES ({})", cols, placeholders);
}

std::string buildSpnInsertSql() {
  const auto &spn_mapping = J1939MappingTables::spn();
  std::string cols = "pgn", placeholders = "?";

  for (const auto &[k, v] : spn_mapping) {
    cols += ", ";
    placeholders += ", ";

    if (std::get<1u>(v) == "data_range") {
      cols += "min_value, max_value";
      placeholders += "?, ?";
    } else {
      cols += std::get<1u>(v);
      placeholders += "?";
    }
  }

  // value_encoding is derived from Resolution format, not from a mapped CSV column.
  cols += ", value_encoding";
  placeholders += ", ?";
  return fmt::format("INSERT OR REPLACE INTO spns ({}) VALUES ({})", cols, placeholders);
}

void insertJ1939Row(sqlite::database &db, const std::string &pgn_insert_sql, const std::string &spn_insert_sql,
                    std::map<std::string, std::string> &pgn_row_map, std::map<std::string, std::string> &spn_row_map) {
  const auto &pgn_mapping = J1939MappingTables::pgn();
  const auto &spn_mapping = J1939MappingTables::spn();

  // Insert PGN
  try {
    auto ps = db << pgn_insert_sql;
    for (const auto &[k, v] : pgn_mapping) {
      ps << pgn_row_map[std::get<1u>(v).data()];
    }

    ps.execute();
  } catch (const sqlite::sqlite_exception &e) {
    if (e.get_extended_code() != SQLITE_CONSTRAINT_UNIQUE) {
      throw;
    }
  }

  // Insert SPN
  struct {
    bool parts_inserted_flag = false;
    double min = 0.0, max = 0.0;
    size_t size_bits = 0u;
    parsers::resolution_s::type_e encoding = parsers::resolution_s::type_e::numeric;
  } calc;

  // Pre-compute size_bits
  {
    auto it = spn_mapping.find("SPN Length");
    if (it != spn_mapping.end()) {
      auto size = parsers::parseSpnSize(spn_row_map[std::get<1u>(it->second).data()]);
      calc.size_bits = size.has_value() ? size.value().size_bits : 0u;
    }
  }

  for (const auto &[k, v] : spn_row_map) {
    try {
      auto ps = db << spn_insert_sql;
      ps << pgn_row_map["pgn"];

      for (const auto &[k, v] : spn_mapping) {
        if (std::get<1u>(v) == "data_range") {
          auto range = parsers::parseSpnDataRange(spn_row_map[std::get<1u>(v).data()]);

          if (range.has_value()) {
            calc.min = range.value().min;
            calc.max = range.value().max;
            ps << range.value().min << range.value().max;
          } else {
            ps << nullptr << nullptr;
          }
        } else if (std::get<1u>(v) == "offset") {

          auto offset = parsers::parseSpnOffset(spn_row_map[std::get<1u>(v).data()]);
          ps << (offset.has_value() ? offset.value().offset : 0.0);
        } else if (std::get<1u>(v) == "spn_length") {

          auto size = parsers::parseSpnSize(spn_row_map[std::get<1u>(v).data()]);
          calc.size_bits = size.has_value() ? size.value().size_bits : 0u;
          ps << calc.size_bits;
        } else if (std::get<1u>(v) == "resolution") {

          auto resolution = parsers::parseSpnResolution(spn_row_map[std::get<1u>(v).data()]);
          calc.encoding = resolution.has_value() ? resolution.value().type : parsers::resolution_s::type_e::numeric;
          double calculated = (calc.max - calc.min) / (std::pow(2.0, calc.size_bits) - 1.0);

          if (std::fabs(calculated - 1.0) < 1e-9) {
            ps << calculated;
          } else {
            ps << (resolution.has_value() ? resolution.value().resolution : 1.0);
          }
        } else {
          ps << spn_row_map[std::get<1u>(v).data()];
        }
      }

      ps << parsers::resolutionTypeName(calc.encoding);
      ps.execute();

      // Insert SPN fragments
      if (!calc.parts_inserted_flag) {
        auto size = parsers::parseSpnSize(spn_row_map[std::get<1u>(spn_mapping.at("SPN Length")).data()]);

        if (size.has_value()) {
          auto spn_fragments = parsers::parseSpnPosition(
              size.value().size_bits, spn_row_map[std::get<1u>(spn_mapping.at("SPN Position in PG")).data()]);

          if (spn_fragments.has_value()) {
            auto spn = std::stoll(spn_row_map["spn"]);

            for (const auto &part : spn_fragments.value().spn_fragments) {
              db << R"(INSERT OR REPLACE INTO spn_fragments (spn, pgn, byte_offset, bit_offset, size) VALUES (?, ?, ?, ?, ?);)"
                 << spn << std::stoll(pgn_row_map["pgn"]) << part.byte_offset << part.bit_offset << part.size;
            }

            calc.parts_inserted_flag = true;
          }
        }
      }
    } catch (const sqlite::sqlite_exception &e) {
      if (e.get_extended_code() != SQLITE_CONSTRAINT_UNIQUE) {
        throw;
      }
    }
  }
}
