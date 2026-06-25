#include <catch2/catch_test_macros.hpp>

#include "parsers.hpp"

// Verifies that the CSV positions chosen for the .piklema DM1 markup expand
// into the exact byte/bit fragments the runtime decoder expects, per
// J1939-73 §5.7.1. Pure parser-level test — no SQLite required.

using parsers::parseSpnPosition;
using parsers::parseSpnResolution;
using parsers::parseSpnSize;
using parsers::resolution_s;

// -------------------------------------------------------- byte/bit layout

TEST_CASE("DM1 positions: lamp status bit pairs in byte 1", "[dm1][positions]") {
  // SPN 987 Protect Lamp — bits 2-1 of byte 1 → byte_offset=0, bit_offset=0, size=2
  auto p987 = parseSpnPosition(/*size_bits=*/2, "1.1");
  REQUIRE(p987.has_value());
  REQUIRE(p987->spn_fragments.size() == 1);
  REQUIRE(p987->spn_fragments[0].byte_offset == 0);
  REQUIRE(p987->spn_fragments[0].bit_offset == 0);
  REQUIRE(p987->spn_fragments[0].size == 2);

  // SPN 624 Amber Warning Lamp — bits 4-3 of byte 1
  auto p624 = parseSpnPosition(2, "1.3");
  REQUIRE(p624.has_value());
  REQUIRE(p624->spn_fragments[0].byte_offset == 0);
  REQUIRE(p624->spn_fragments[0].bit_offset == 2);
  REQUIRE(p624->spn_fragments[0].size == 2);

  // SPN 623 Red Stop Lamp — bits 6-5 of byte 1
  auto p623 = parseSpnPosition(2, "1.5");
  REQUIRE(p623.has_value());
  REQUIRE(p623->spn_fragments[0].byte_offset == 0);
  REQUIRE(p623->spn_fragments[0].bit_offset == 4);
  REQUIRE(p623->spn_fragments[0].size == 2);

  // SPN 1213 MIL — bits 8-7 of byte 1
  auto p1213 = parseSpnPosition(2, "1.7");
  REQUIRE(p1213.has_value());
  REQUIRE(p1213->spn_fragments[0].byte_offset == 0);
  REQUIRE(p1213->spn_fragments[0].bit_offset == 6);
  REQUIRE(p1213->spn_fragments[0].size == 2);
}

TEST_CASE("DM1 positions: flash lamp bit pairs in byte 2", "[dm1][positions]") {
  // 3041 Flash Protect — 2.1
  REQUIRE(parseSpnPosition(2, "2.1")->spn_fragments[0].byte_offset == 1);
  REQUIRE(parseSpnPosition(2, "2.1")->spn_fragments[0].bit_offset == 0);
  // 3040 Flash AWL — 2.3
  REQUIRE(parseSpnPosition(2, "2.3")->spn_fragments[0].bit_offset == 2);
  // 3039 Flash RSL — 2.5
  REQUIRE(parseSpnPosition(2, "2.5")->spn_fragments[0].bit_offset == 4);
  // 3038 Flash MIL — 2.7
  REQUIRE(parseSpnPosition(2, "2.7")->spn_fragments[0].bit_offset == 6);
}

TEST_CASE("DM1 positions: SPN 1214 (19 bits across bytes 3..5)", "[dm1][positions]") {
  // Position "3,5.6" + 19 bits should produce two fragments:
  //   1) bytes 3-4 whole (byte_offset=2, bit_offset=0, size=16)
  //   2) byte 5 bits 6-8     (byte_offset=4, bit_offset=5, size=3)
  // Together: 16 + 3 = 19 bits, matching the SPN field width.
  auto p = parseSpnPosition(19, "3,5.6");
  REQUIRE(p.has_value());
  REQUIRE(p->spn_fragments.size() == 2);

  REQUIRE(p->spn_fragments[0].byte_offset == 2);
  REQUIRE(p->spn_fragments[0].bit_offset == 0);
  REQUIRE(p->spn_fragments[0].size == 16);

  REQUIRE(p->spn_fragments[1].byte_offset == 4);
  REQUIRE(p->spn_fragments[1].bit_offset == 5);
  REQUIRE(p->spn_fragments[1].size == 3);
}

TEST_CASE("DM1 positions: FMI (5 bits) at byte 5", "[dm1][positions]") {
  auto p = parseSpnPosition(5, "5.1");
  REQUIRE(p.has_value());
  REQUIRE(p->spn_fragments.size() == 1);
  REQUIRE(p->spn_fragments[0].byte_offset == 4);
  REQUIRE(p->spn_fragments[0].bit_offset == 0);
  REQUIRE(p->spn_fragments[0].size == 5);
}

TEST_CASE("DM1 positions: OC (7 bits) and CM (1 bit) in byte 6", "[dm1][positions]") {
  // Occurrence Count — bits 7-1 of byte 6
  auto oc = parseSpnPosition(7, "6.1");
  REQUIRE(oc.has_value());
  REQUIRE(oc->spn_fragments[0].byte_offset == 5);
  REQUIRE(oc->spn_fragments[0].bit_offset == 0);
  REQUIRE(oc->spn_fragments[0].size == 7);

  // Conversion Method — bit 8 of byte 6
  auto cm = parseSpnPosition(1, "6.8");
  REQUIRE(cm.has_value());
  REQUIRE(cm->spn_fragments[0].byte_offset == 5);
  REQUIRE(cm->spn_fragments[0].bit_offset == 7);
  REQUIRE(cm->spn_fragments[0].size == 1);
}

// -------------------------------------------------------- length / resolution

TEST_CASE("DM1 lengths: bit-sized SPN strings", "[dm1][size]") {
  REQUIRE(parseSpnSize("2 bits")->size_bits == 2);
  REQUIRE(parseSpnSize("5 bits")->size_bits == 5);
  REQUIRE(parseSpnSize("7 bits")->size_bits == 7);
  REQUIRE(parseSpnSize("19 bits")->size_bits == 19);
  REQUIRE(parseSpnSize("1 bit")->size_bits == 1);
}

TEST_CASE("DM1 resolutions: lamp enum / DTC numeric / CM binary", "[dm1][resolution]") {
  // Lamp 2-bit pairs are enumerated states (Off/On/Reserved/N-A).
  auto lamp = parseSpnResolution("4 states/2 bit");
  REQUIRE(lamp.has_value());
  REQUIRE(lamp->type == resolution_s::type_e::enum_states);

  // Numeric SPN/OC scale at 1/bit.
  auto numeric = parseSpnResolution("1/bit");
  REQUIRE(numeric.has_value());
  REQUIRE(numeric->type == resolution_s::type_e::numeric);
  REQUIRE(numeric->resolution == 1.0);

  // SPN Conversion Method is a single-bit boolean.
  auto bin = parseSpnResolution("Binary");
  REQUIRE(bin.has_value());
  REQUIRE(bin->type == resolution_s::type_e::binary);
}

TEST_CASE("DM1 resolutions: Table: FMI named-lookup syntax", "[dm1][resolution][table]") {
  auto r = parseSpnResolution("Table: FMI");
  REQUIRE(r.has_value());
  REQUIRE(r->type == resolution_s::type_e::enum_states_table);
  REQUIRE(r->table_name == "fmi"); // lowercased by parser
}

TEST_CASE("DM1 resolutions: Table: name is case-insensitive at parse time", "[dm1][resolution][table]") {
  REQUIRE(parseSpnResolution("Table: fmi")->table_name == "fmi");
  REQUIRE(parseSpnResolution("Table: FMI")->table_name == "fmi");
  REQUIRE(parseSpnResolution("Table: Fmi")->table_name == "fmi");
}
