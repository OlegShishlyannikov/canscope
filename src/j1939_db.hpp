#pragma once

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>

#include "sqlite_modern_cpp.h"

struct J1939MappingTables {
  static const std::map<std::string_view, std::tuple<size_t, std::string_view>> &pgn() {
    static const std::map<std::string_view, std::tuple<size_t, std::string_view>> t = {
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
    };
    return t;
  }

  static const std::map<std::string_view, std::tuple<size_t, std::string_view>> &spn() {
    static const std::map<std::string_view, std::tuple<size_t, std::string_view>> t = {
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
    return t;
  }
};

void initJ1939Database(sqlite::database &db);

// Build INSERT SQL for pgns table (call once, reuse)
std::string buildPgnInsertSql();

// Build INSERT SQL for spns table (call once, reuse)
std::string buildSpnInsertSql();

// Insert a row into the database. pgn_row_map/spn_row_map are column_name -> value.
void insertJ1939Row(sqlite::database &db, const std::string &pgn_insert_sql, const std::string &spn_insert_sql,
                    std::map<std::string, std::string> &pgn_row_map, std::map<std::string, std::string> &spn_row_map);
