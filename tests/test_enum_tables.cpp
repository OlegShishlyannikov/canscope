#include <catch2/catch_test_macros.hpp>

#include "enum_tables.hpp"

// Coverage: a handful of well-known FMI codes from J1939-73 Appendix A,
// the reserved 24..30 range, and out-of-range guards.

TEST_CASE("enum_tables::fmiName: documented FMI codes", "[enum_tables][fmi]") {
  REQUIRE(enum_tables::fmiName(0) == "Data Valid But Above Normal Operational Range - Most Severe Level");
  REQUIRE(enum_tables::fmiName(1) == "Data Valid But Below Normal Operational Range - Most Severe Level");
  REQUIRE(enum_tables::fmiName(2) == "Data Erratic, Intermittent or Incorrect");
  REQUIRE(enum_tables::fmiName(3) == "Voltage Above Normal, or Shorted to High Source");
  REQUIRE(enum_tables::fmiName(4) == "Voltage Below Normal, or Shorted to Low Source");
  REQUIRE(enum_tables::fmiName(13) == "Out of Calibration");
  REQUIRE(enum_tables::fmiName(14) == "Special Instructions #1");
  REQUIRE(enum_tables::fmiName(19) == "Received Network Data in Error");
  REQUIRE(enum_tables::fmiName(23) == "Request DM60 for More Information");
  REQUIRE(enum_tables::fmiName(31) == "Condition Exists");
}

TEST_CASE("enum_tables::fmiName: reserved range 24..30 returns empty", "[enum_tables][fmi]") {
  for (int32_t i = 24; i <= 30; ++i) {
    REQUIRE(enum_tables::fmiName(i).empty());
  }
}

TEST_CASE("enum_tables::fmiName: out-of-range guards", "[enum_tables][fmi]") {
  REQUIRE(enum_tables::fmiName(-1).empty());
  REQUIRE(enum_tables::fmiName(32).empty());
  REQUIRE(enum_tables::fmiName(255).empty());
}

TEST_CASE("enum_tables::lookup dispatches by table name", "[enum_tables]") {
  REQUIRE(enum_tables::lookup("fmi", 3) == "Voltage Above Normal, or Shorted to High Source");
  REQUIRE(enum_tables::lookup("FMI", 3).empty()); // name is lowercase by convention
  REQUIRE(enum_tables::lookup("unknown_table", 3).empty());
}

TEST_CASE("enum_tables::lookup support table (inverted polarity)", "[enum_tables][support]") {
  // DM24 §5.7.24.2: 0 = Supported, 1 = Not Supported
  REQUIRE(enum_tables::lookup("support", 0) == "supported");
  REQUIRE(enum_tables::lookup("support", 1) == "not supported");
  REQUIRE(enum_tables::lookup("support", 2).empty());
  REQUIRE(enum_tables::lookup("support", -1).empty());
}

TEST_CASE("enum_tables::lookup conversionmethod table", "[enum_tables][conversionmethod]") {
  // DM1/2/12/23/27/28 §5.7.1.14: 0 = current method, 1 = pre-2006 legacy.
  REQUIRE(enum_tables::lookup("conversionmethod", 0) == "current method");
  REQUIRE(enum_tables::lookup("conversionmethod", 1) == "pre-2006 legacy");
  REQUIRE(enum_tables::lookup("conversionmethod", 2).empty());
}

TEST_CASE("enum_tables::lookup ntearea table", "[enum_tables][ntearea]") {
  // DM34 §5.7.34: 00=outside, 01=inside, 10=reserved, 11=not available.
  REQUIRE(enum_tables::lookup("ntearea", 0) == "outside area");
  REQUIRE(enum_tables::lookup("ntearea", 1) == "inside area");
  REQUIRE(enum_tables::lookup("ntearea", 2) == "SAE reserved");
  REQUIRE(enum_tables::lookup("ntearea", 3) == "not available");
  REQUIRE(enum_tables::lookup("ntearea", 4).empty());
}
