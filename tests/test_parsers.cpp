#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "parsers.hpp"

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// ---------------------------------------------------------------- parseSpnDataRange

TEST_CASE("parseSpnDataRange: numeric range", "[parsers][range]") {
  auto r = parsers::parseSpnDataRange("0 to 100");
  REQUIRE(r.has_value());
  REQUIRE_THAT(r->min, WithinAbs(0.0, 1e-9));
  REQUIRE_THAT(r->max, WithinAbs(100.0, 1e-9));
}

TEST_CASE("parseSpnDataRange: negative bounds", "[parsers][range]") {
  auto r = parsers::parseSpnDataRange("-273.15 to 500");
  REQUIRE(r.has_value());
  REQUIRE_THAT(r->min, WithinRel(-273.15, 1e-9));
  REQUIRE_THAT(r->max, WithinAbs(500.0, 1e-9));
}

TEST_CASE("parseSpnDataRange: comma as decimal separator (European)", "[parsers][range]") {
  auto r = parsers::parseSpnDataRange("-50,0 to 50,0");
  REQUIRE(r.has_value());
  REQUIRE_THAT(r->min, WithinAbs(-500.0, 1e-9)); // commas stripped → "-500" per current semantics
  REQUIRE_THAT(r->max, WithinAbs(500.0, 1e-9));
}

TEST_CASE("parseSpnDataRange: trailing unit text", "[parsers][range]") {
  // The grammar does not consume units; it accepts the numeric pair and any trailing
  // text is left unconsumed by phrase_parse (which returns true for prefix match).
  auto r = parsers::parseSpnDataRange("0 to 100 rpm");
  REQUIRE(r.has_value());
  REQUIRE_THAT(r->min, WithinAbs(0.0, 1e-9));
  REQUIRE_THAT(r->max, WithinAbs(100.0, 1e-9));
}

TEST_CASE("parseSpnDataRange: non-numeric N/A-style input falls through", "[parsers][range]") {
  // The second branch of the grammar matches a short alpha-prefixed token like "N/A" or similar.
  auto r = parsers::parseSpnDataRange("N-A");
  REQUIRE(r.has_value());
}

TEST_CASE("parseSpnDataRange: empty string fails", "[parsers][range]") {
  REQUIRE_FALSE(parsers::parseSpnDataRange("").has_value());
}

// ---------------------------------------------------------------- parseSpnSize

TEST_CASE("parseSpnSize: bytes", "[parsers][size]") {
  auto s = parsers::parseSpnSize("2 bytes");
  REQUIRE(s.has_value());
  REQUIRE(s->size_bytes == 2u);
  REQUIRE(s->size_bits == 16u);
}

TEST_CASE("parseSpnSize: 1 byte (singular)", "[parsers][size]") {
  auto s = parsers::parseSpnSize("1 byte");
  REQUIRE(s.has_value());
  REQUIRE(s->size_bytes == 1u);
  REQUIRE(s->size_bits == 8u);
}

TEST_CASE("parseSpnSize: bits", "[parsers][size]") {
  auto s = parsers::parseSpnSize("4 bits");
  REQUIRE(s.has_value());
  REQUIRE(s->size_bytes == 0u);
  REQUIRE(s->size_bits == 4u);
}

TEST_CASE("parseSpnSize: 1 bit (singular)", "[parsers][size]") {
  auto s = parsers::parseSpnSize("1 bit");
  REQUIRE(s.has_value());
  REQUIRE(s->size_bytes == 0u);
  REQUIRE(s->size_bits == 1u);
}

TEST_CASE("parseSpnSize: malformed inputs", "[parsers][size]") {
  REQUIRE_FALSE(parsers::parseSpnSize("abc").has_value());
  REQUIRE_FALSE(parsers::parseSpnSize("").has_value());
  REQUIRE_FALSE(parsers::parseSpnSize("2 megabytes").has_value());
}

TEST_CASE("parseSpnSize: Variable-length sentinel (ASCII SPNs)", "[parsers][size][variable]") {
  // Full DA string for SPN 234 "Software Identification".
  auto s = parsers::parseSpnSize("Variable - up to 200 bytes followed by an \"*\" delimiter");
  REQUIRE(s.has_value());
  REQUIRE(s->size_bytes == 0u);
  REQUIRE(s->size_bits == 0u);

  // Any trailing text after the literal "Variable" is accepted.
  auto s2 = parsers::parseSpnSize("Variable");
  REQUIRE(s2.has_value());
  REQUIRE(s2->size_bytes == 0u);
  REQUIRE(s2->size_bits == 0u);
}

// ---------------------------------------------------------------- parseSpnOffset

TEST_CASE("parseSpnOffset: zero and integers", "[parsers][offset]") {
  {
    auto o = parsers::parseSpnOffset("0");
    REQUIRE(o.has_value());
    REQUIRE_THAT(o->offset, WithinAbs(0.0, 1e-9));
  }
  {
    auto o = parsers::parseSpnOffset("-50");
    REQUIRE(o.has_value());
    REQUIRE_THAT(o->offset, WithinAbs(-50.0, 1e-9));
  }
}

TEST_CASE("parseSpnOffset: positive sign and fractional", "[parsers][offset]") {
  auto o = parsers::parseSpnOffset("+273.15");
  REQUIRE(o.has_value());
  REQUIRE_THAT(o->offset, WithinRel(273.15, 1e-9));
}

TEST_CASE("parseSpnOffset: comma decimal is stripped (European style)", "[parsers][offset]") {
  // Current string_to_double_s strips commas, so "273,15" -> 27315.0.
  auto o = parsers::parseSpnOffset("273,15");
  REQUIRE(o.has_value());
  REQUIRE_THAT(o->offset, WithinAbs(27315.0, 1e-9));
}

TEST_CASE("parseSpnOffset: rejects non-numeric", "[parsers][offset]") {
  REQUIRE_FALSE(parsers::parseSpnOffset("abc").has_value());
  REQUIRE_FALSE(parsers::parseSpnOffset("").has_value());
}

// ---------------------------------------------------------------- parseSpnResolution

TEST_CASE("parseSpnResolution: plain numeric", "[parsers][resolution]") {
  auto r = parsers::parseSpnResolution("1");
  REQUIRE(r.has_value());
  REQUIRE(r->type == parsers::resolution_s::type_e::numeric);
  REQUIRE_THAT(r->resolution, WithinAbs(1.0, 1e-9));
}

TEST_CASE("parseSpnResolution: numeric with unit trail", "[parsers][resolution]") {
  auto r = parsers::parseSpnResolution("0.125 rpm/bit");
  REQUIRE(r.has_value());
  REQUIRE(r->type == parsers::resolution_s::type_e::numeric);
  REQUIRE_THAT(r->resolution, WithinRel(0.125, 1e-9));
}

TEST_CASE("parseSpnResolution: rational N/M", "[parsers][resolution]") {
  auto r = parsers::parseSpnResolution("1/8 rpm/bit");
  REQUIRE(r.has_value());
  REQUIRE(r->type == parsers::resolution_s::type_e::numeric);
  REQUIRE_THAT(r->resolution, WithinRel(0.125, 1e-9));
}

TEST_CASE("parseSpnResolution: enum states", "[parsers][resolution]") {
  auto r = parsers::parseSpnResolution("4 states/2 bit");
  REQUIRE(r.has_value());
  REQUIRE(r->type == parsers::resolution_s::type_e::enum_states);
  REQUIRE_THAT(r->resolution, WithinAbs(1.0, 1e-9));
}

TEST_CASE("parseSpnResolution: Binary", "[parsers][resolution]") {
  auto r = parsers::parseSpnResolution("Binary");
  REQUIRE(r.has_value());
  REQUIRE(r->type == parsers::resolution_s::type_e::binary);
}

TEST_CASE("parseSpnResolution: ASCII", "[parsers][resolution]") {
  auto r = parsers::parseSpnResolution("ASCII");
  REQUIRE(r.has_value());
  REQUIRE(r->type == parsers::resolution_s::type_e::ascii);
}

TEST_CASE("parseSpnResolution: empty fails", "[parsers][resolution]") {
  REQUIRE_FALSE(parsers::parseSpnResolution("").has_value());
}

TEST_CASE("resolutionTypeName maps every enum", "[parsers][resolution]") {
  using T = parsers::resolution_s::type_e;
  REQUIRE(std::string(parsers::resolutionTypeName(T::numeric)) == "numeric");
  REQUIRE(std::string(parsers::resolutionTypeName(T::enum_states)) == "enum");
  REQUIRE(std::string(parsers::resolutionTypeName(T::binary)) == "binary");
  REQUIRE(std::string(parsers::resolutionTypeName(T::ascii)) == "ascii");
}

// ---------------------------------------------------------------- parseSpnPosition (7 variants)

TEST_CASE("parseSpnPosition v0: single byte start (e.g. '3')", "[parsers][position]") {
  auto p = parsers::parseSpnPosition(8, "3");
  REQUIRE(p.has_value());
  REQUIRE(p->spn_fragments.size() == 1);
  REQUIRE(p->spn_fragments[0].byte_offset == 2u); // 1-based input, 0-based output
  REQUIRE(p->spn_fragments[0].bit_offset == 0u);
  REQUIRE(p->spn_fragments[0].size == 8u);
}

TEST_CASE("parseSpnPosition v1: byte.bit form (e.g. '3.4', 2-bit SPN)", "[parsers][position]") {
  auto p = parsers::parseSpnPosition(2, "3.4");
  REQUIRE(p.has_value());
  REQUIRE(p->spn_fragments.size() == 1);
  REQUIRE(p->spn_fragments[0].byte_offset == 2u);
  REQUIRE(p->spn_fragments[0].bit_offset == 3u); // bit 4 is 1-based
  REQUIRE(p->spn_fragments[0].size == 2u);
}

TEST_CASE("parseSpnPosition v2: byte-byte range (e.g. '2-3')", "[parsers][position]") {
  auto p = parsers::parseSpnPosition(16, "2-3");
  REQUIRE(p.has_value());
  REQUIRE(p->spn_fragments.size() == 1);
  REQUIRE(p->spn_fragments[0].byte_offset == 1u);
  REQUIRE(p->spn_fragments[0].bit_offset == 0u);
  REQUIRE(p->spn_fragments[0].size == 16u);
}

TEST_CASE("parseSpnPosition v3: whole bytes + trailing bits (e.g. '2,3.4', 10-bit SPN)", "[parsers][position]") {
  // Full bytes from 2 up to (but not including) byte 3, then a tail with bit offset 4.
  auto p = parsers::parseSpnPosition(10, "2,3.4");
  REQUIRE(p.has_value());
  REQUIRE(p->spn_fragments.size() == 2);
  REQUIRE(p->spn_fragments[0].byte_offset == 1u);
  REQUIRE(p->spn_fragments[0].bit_offset == 0u);
  REQUIRE(p->spn_fragments[0].size == 8u); // (3 - 2) * 8
  REQUIRE(p->spn_fragments[1].byte_offset == 2u);
  REQUIRE(p->spn_fragments[1].bit_offset == 3u);
  REQUIRE(p->spn_fragments[1].size == 2u); // 10 % 8
}

TEST_CASE("parseSpnPosition v4: bit offset prefix + full byte (e.g. '1.5,2')", "[parsers][position]") {
  auto p = parsers::parseSpnPosition(12, "1.5,2");
  REQUIRE(p.has_value());
  REQUIRE(p->spn_fragments.size() == 2);
  REQUIRE(p->spn_fragments[0].byte_offset == 0u);
  REQUIRE(p->spn_fragments[0].bit_offset == 4u);
  REQUIRE(p->spn_fragments[0].size == 4u); // 8 - 4
  REQUIRE(p->spn_fragments[1].byte_offset == 1u);
  REQUIRE(p->spn_fragments[1].bit_offset == 0u);
  REQUIRE(p->spn_fragments[1].size == 8u);
}

TEST_CASE("parseSpnPosition v5: whole bytes + byte with bit offset (e.g. '1-2,3.4', 20-bit SPN)",
          "[parsers][position]") {
  auto p = parsers::parseSpnPosition(20, "1-2,3.4");
  REQUIRE(p.has_value());
  REQUIRE(p->spn_fragments.size() == 2);
  REQUIRE(p->spn_fragments[0].byte_offset == 0u);
  REQUIRE(p->spn_fragments[0].bit_offset == 0u);
  REQUIRE(p->spn_fragments[0].size == 16u); // (2 - 1 + 1) * 8
  REQUIRE(p->spn_fragments[1].byte_offset == 2u);
  REQUIRE(p->spn_fragments[1].bit_offset == 3u);
  REQUIRE(p->spn_fragments[1].size == 4u); // 20 % 8
}

TEST_CASE("parseSpnPosition v6: byte.bit + byte-byte (e.g. '1.3,2-4')", "[parsers][position]") {
  auto p = parsers::parseSpnPosition(32, "1.3,2-4");
  REQUIRE(p.has_value());
  REQUIRE(p->spn_fragments.size() == 2);
  REQUIRE(p->spn_fragments[0].byte_offset == 0u);
  REQUIRE(p->spn_fragments[0].bit_offset == 2u);
  REQUIRE(p->spn_fragments[0].size == 6u); // 8 - 2
  REQUIRE(p->spn_fragments[1].byte_offset == 1u);
  REQUIRE(p->spn_fragments[1].bit_offset == 0u);
  REQUIRE(p->spn_fragments[1].size == 24u); // (4 - 2 + 1) * 8
}

TEST_CASE("parseSpnPosition: rejects garbage", "[parsers][position]") {
  REQUIRE_FALSE(parsers::parseSpnPosition(8, "xyz").has_value());
  REQUIRE_FALSE(parsers::parseSpnPosition(8, "").has_value());
}

TEST_CASE("parseSpnPosition to-end: '2-N' (Software Identification style)", "[parsers][position][to_end]") {
  auto p = parsers::parseSpnPosition(0, "2-N");
  REQUIRE(p.has_value());
  REQUIRE(p->spn_fragments.size() == 1);
  REQUIRE(p->spn_fragments[0].byte_offset == 1u); // byte 2 is offset 1 (0-based)
  REQUIRE(p->spn_fragments[0].bit_offset == 0u);
  REQUIRE(p->spn_fragments[0].size == 0u); // sentinel: to end of frame
}

TEST_CASE("parseSpnPosition to-end: '1-N' (whole-frame ASCII)", "[parsers][position][to_end]") {
  auto p = parsers::parseSpnPosition(0, "1-N");
  REQUIRE(p.has_value());
  REQUIRE(p->spn_fragments.size() == 1);
  REQUIRE(p->spn_fragments[0].byte_offset == 0u);
  REQUIRE(p->spn_fragments[0].bit_offset == 0u);
  REQUIRE(p->spn_fragments[0].size == 0u);
}

TEST_CASE("parseSpnPosition to-end: doesn't collide with numeric v2", "[parsers][position][to_end]") {
  // '2-3' must still go through the numeric v2 handler and produce a finite size.
  auto p = parsers::parseSpnPosition(16, "2-3");
  REQUIRE(p.has_value());
  REQUIRE(p->spn_fragments.size() == 1);
  REQUIRE(p->spn_fragments[0].size == 16u); // not the to-end sentinel
}
