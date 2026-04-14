#include "parsers.hpp"

namespace parsers {
std::optional<struct range_s> parseSpnDataRange(const std::string &str) {
  struct range_s result;
  auto begin = str.begin(), end = str.end();
  return phrase_parse(begin, end, range::range_parser_s<decltype(begin)>{}, ascii::space, result)
             ? std::optional<range_s>(result)
             : std::nullopt;
}

std::optional<struct size_s> parseSpnSize(const std::string &str) {
  struct size_s result;
  auto begin = str.begin(), end = str.end();
  return phrase_parse(begin, end, size::size_parser_s<decltype(begin)>{}, ascii::space, result)
             ? std::optional<size_s>(result)
             : std::nullopt;
}

std::optional<struct offset_s> parseSpnOffset(const std::string &str) {
  struct offset_s result;
  auto begin = str.begin(), end = str.end();
  return phrase_parse(begin, end, offset::offset_parser_s<decltype(begin)>{}, ascii::space, result)
             ? std::optional<struct offset_s>(result)
             : std::nullopt;
}

std::optional<struct resolution_s> parseSpnResolution(const std::string &str) {
  struct resolution_s result;
  auto begin = str.begin(), end = str.end();
  return phrase_parse(begin, end, resolution::resolution_parser_s<decltype(begin)>{}, ascii::space, result)
             ? std::optional<struct resolution_s>(result)
             : std::nullopt;
}

std::optional<struct spn_fragments_s> parseSpnPosition(size_t spn_size_bits, const std::string &str) {
  struct spn_fragments_s result;
  auto begin = str.begin(), end = str.end();
  return phrase_parse(begin, end, position::position_parser_s<decltype(begin)>(spn_size_bits), ascii::space, result)
             ? std::optional<struct spn_fragments_s>(result)
             : std::nullopt;
}
}; // namespace parsers
