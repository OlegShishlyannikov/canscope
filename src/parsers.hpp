#pragma once

#include <fmt/base.h>
#include <algorithm>
#include <boost/bind.hpp>
#include <boost/spirit/home/qi/numeric/uint.hpp>
#include <boost/spirit/include/phoenix.hpp>
#include <boost/spirit/include/qi.hpp>
#include <optional>
#include <string>

#define FMT_HEADER_ONLY
#include <fmt/format.h>
#include <fmt/ranges.h>

namespace parsers {
using namespace boost::spirit;

// Parse data range string to this struct
struct range_s {
  double min = .0f, max = .0f;
  std::string other;
};

// Parse data range string to this struct
struct size_s {
  size_t size_bytes = 0u, size_bits = 0u;
};

// Parse data range string to this struct
struct offset_s {
  double offset = .0f;
};

// SPN position here
struct spn_fragments_s {
  struct spn_part_s {
    size_t byte_offset, bit_offset, size;
  };

  std::vector<spn_part_s> spn_fragments;
};

// Parse resolution string to this struct
struct resolution_s {
  double resolution;
};

namespace _detail {
template <typename It, typename Res> struct parser_s : qi::grammar<It, Res(), ascii::space_type> {
  parser_s() : parser_s::base_type(rule) {}
  qi::rule<It, Res(), ascii::space_type> rule; // Main rule
  qi::rule<It, std::string(), ascii::space_type> strnum;
  qi::rule<It, double(), ascii::space_type> num;
};

// String to double converter
struct string_to_double_s {
  double operator()(const std::string &str) const {
    std::string num_str = str;
    num_str.erase(std::remove(num_str.begin(), num_str.end(), ','), num_str.end());
    return std::stod(num_str);
  }
};

// String to int32 converter
struct string_to_int_s {
  int32_t operator()(const std::string &str) const {
    std::string num_str = str;
    num_str.erase(std::remove(num_str.begin(), num_str.end(), ','), num_str.end());
    return std::stoi(num_str);
  }
};
}; // namespace _detail

// Data range parsers
namespace range {
template <typename It> struct range_parser_s : _detail::parser_s<It, range_s> {
  range_parser_s() : _detail::parser_s<It, range_s>() {

    this->strnum = lexeme[-(qi::char_('+') | qi::char_('-')) >> +(qi::digit | qi::char_(',') | qi::char_('.'))];
    this->num = this->strnum[_val = boost::phoenix::function<_detail::string_to_double_s>{}(qi::_1)];
    this->rule = (this->num >> "to" >> this->num) | (-qi::digit >> -qi::digit >> +qi::char_("a-zA-Z-"));
  }
};
}; // namespace range

namespace size {
template <typename It> struct size_parser_s : _detail::parser_s<It, size_s> {
  size_parser_s() : _detail::parser_s<It, size_s>() {

    struct bytes_to_bits_s {
      size_s operator()(size_t bytes) const {
        return {
            .size_bytes = bytes,
            .size_bits = bytes * UINT8_WIDTH,
        };
      }
    };

    struct as_bits_s {
      size_s operator()(size_t bits) const {
        return {
            .size_bytes = 0u,
            .size_bits = bits,
        };
      }
    };

    this->rule = ((qi::uint_ >> "byte") | (qi::uint_ >> "bytes"))[_val = boost::phoenix::function<bytes_to_bits_s>{}(qi::_1)] |
                 ((qi::uint_ >> "bit") | (qi::uint_ >> "bits"))[_val = boost::phoenix::function<as_bits_s>{}(qi::_1)];
  }
};
}; // namespace size

namespace position {
template <typename It> struct position_parser_s : _detail::parser_s<It, spn_fragments_s> {
  position_parser_s(size_t size_bits) : _detail::parser_s<It, spn_fragments_s>() {

    // just start byte
    struct rule_v0_handler_s {
      spn_fragments_s operator()(uint32_t start_byte) const {
        return {
            .spn_fragments =
                {
                    {
                        .byte_offset = (start_byte - 1u), // From zero
                        .bit_offset = 0,
                        .size = UINT8_WIDTH,
                    },
                },
        };
      }
    };

    // start byte and bit offset (1 part of size - up to 1 byte)
    struct rule_v1_handler_s {
      spn_fragments_s operator()(size_t size_bits, uint32_t start_byte, uint32_t bit_offset) const {
        return {
            .spn_fragments =
                {
                    {
                        .byte_offset = (start_byte - 1u), // From zero
                        .bit_offset = (bit_offset - 1u),
                        .size = (size_bits % UINT8_WIDTH) ? (size_bits % UINT8_WIDTH) : UINT8_WIDTH,
                    },
                },
        };
      }
    };

    // start byte and last_byte (1 part multiple of 1 byte)
    struct rule_v2_handler_s {
      spn_fragments_s operator()(uint32_t start_byte, uint32_t last_byte) const {
        return {
            .spn_fragments =
                {
                    {
                        .byte_offset = (start_byte - 1u),
                        .bit_offset = 0u,
                        .size = ((last_byte - start_byte) + 1u) * UINT8_WIDTH,
                    },
                },
        };
      }
    };

    // start byte and last byte with bit offset (2 parts - first is integer bytes and second with bit offset)
    struct rule_v3_handler_s {
      spn_fragments_s operator()(size_t size_bits, uint32_t start_byte, uint32_t last_byte, uint32_t bit_offset) const {
        return {
            .spn_fragments =
                {
                    // First - integer bytes (from start_byte to last_byte-1)
                    {
                        .byte_offset = (start_byte - 1u),
                        .bit_offset = 0u,
                        .size = ((last_byte - start_byte)) * UINT8_WIDTH,
                    },

                    // Second - with bit mask
                    {
                        .byte_offset = (last_byte - 1u),
                        .bit_offset = (bit_offset - 1u),
                        .size = (size_bits % UINT8_WIDTH) ? (size_bits % UINT8_WIDTH) : UINT8_WIDTH,
                    },
                },
        };
      }
    };

    // start with bit offset, last byte (2 parts - first with bit offset and second integer byte)
    struct rule_v4_handler_s {
      spn_fragments_s operator()(uint32_t start_byte, uint32_t bit_offset, uint32_t last_byte) const {
        return {
            .spn_fragments =
                {
                    // First - with bit mask
                    {
                        .byte_offset = (start_byte - 1u),
                        .bit_offset = (bit_offset - 1u),
                        .size = UINT8_WIDTH - (bit_offset - 1u),
                    },

                    // second - integer byte
                    {
                        .byte_offset = (last_byte - 1u),
                        .bit_offset = 0u,
                        .size = UINT8_WIDTH, // Size is equal to 1 byte
                    },
                },
        };
      }
    };

    // start byte, last integer byte and last byte with bit offset
    struct rule_v5_handler_s {
      spn_fragments_s operator()(size_t size_bits, uint32_t start_byte, uint32_t last_integer_byte, uint32_t last_byte, uint32_t bit_offset) const {
        return {
            .spn_fragments =
                {
                    {
                        .byte_offset = (start_byte - 1u),
                        .bit_offset = 0u,
                        .size = ((last_integer_byte - start_byte) + 1u) * UINT8_WIDTH,
                    },

                    // Second - with bit mask
                    {
                        .byte_offset = (last_byte - 1u),
                        .bit_offset = (bit_offset - 1u),
                        .size = (size_bits % UINT8_WIDTH) ? (size_bits % UINT8_WIDTH) : UINT8_WIDTH,
                    },
                },
        };
      }
    };

    // start byte with bit offset, first integer byte and last integer byte
    struct rule_v6_handler_s {
      spn_fragments_s operator()(uint32_t start_byte, uint32_t bit_offset, uint32_t first_integer_byte, uint32_t last_byte) const {
        return {
            .spn_fragments =
                {
                    // First - with bit mask
                    {
                        .byte_offset = (start_byte - 1u),
                        .bit_offset = (bit_offset - 1u),
                        .size = UINT8_WIDTH - (bit_offset - 1u),
                    },

                    // Second - integer bytes
                    {
                        .byte_offset = (first_integer_byte - 1u),
                        .bit_offset = 0u,
                        .size = ((last_byte - first_integer_byte) + 1u) * UINT8_WIDTH,
                    },
                },
        };
      }
    };

    position_rule_v0 = (qi::uint_)[qi::_val = boost::phoenix::function<rule_v0_handler_s>{}(qi::_1)];
    position_rule_v1 = (qi::uint_ >> '.' >> qi::uint_)[qi::_val = boost::phoenix::function<rule_v1_handler_s>{}(size_bits, qi::_1, qi::_2)];
    position_rule_v2 = (qi::uint_ >> '-' >> qi::uint_)[qi::_val = boost::phoenix::function<rule_v2_handler_s>{}(qi::_1, qi::_2)];
    position_rule_v3 = (qi::uint_ >> ',' >> qi::uint_ >> '.' >> qi::uint_)[qi::_val = boost::phoenix::function<rule_v3_handler_s>{}(size_bits, qi::_1, qi::_2, qi::_3)];
    position_rule_v4 = (qi::uint_ >> '.' >> qi::uint_ >> ',' >> qi::uint_)[qi::_val = boost::phoenix::function<rule_v4_handler_s>{}(qi::_1, qi::_2, qi::_3)];
    position_rule_v5 = (qi::uint_ >> '-' >> qi::uint_ >> ',' >> qi::uint_ >> '.' >>
                        qi::uint_)[qi::_val = boost::phoenix::function<rule_v5_handler_s>{}(size_bits, qi::_1, qi::_2, qi::_3, qi::_4)];
    position_rule_v6 =
        (qi::uint_ >> '.' >> qi::uint_ >> ',' >> qi::uint_ >> '-' >> qi::uint_)[qi::_val = boost::phoenix::function<rule_v6_handler_s>{}(qi::_1, qi::_2, qi::_3, qi::_4)];

    // If one of rules works
    this->rule = position_rule_v6 | position_rule_v5 | position_rule_v4 | position_rule_v3 | position_rule_v2 | position_rule_v1 | position_rule_v0;
  }

  qi::rule<It, spn_fragments_s(), ascii::space_type> position_rule_v0, position_rule_v1, position_rule_v2, position_rule_v3, position_rule_v4, position_rule_v5, position_rule_v6;
};
}; // namespace position

namespace offset {
template <typename It> struct offset_parser_s : _detail::parser_s<It, offset_s> {
  offset_parser_s() : _detail::parser_s<It, offset_s>() {
    this->strnum = lexeme[-(qi::char_('+') | qi::char_('-')) >> +(qi::digit | qi::char_(',') | qi::char_('.'))];
    this->num = this->strnum[_val = boost::phoenix::function<_detail::string_to_double_s>{}(qi::_1)];
    this->rule = this->num;
  }
};
}; // namespace offset

// Resolution parsers
namespace resolution {
template <typename It> struct resolution_parser_s : _detail::parser_s<It, resolution_s> {
  resolution_parser_s() : _detail::parser_s<It, resolution_s>() {
    struct resolution_rule_v0_handler_s {
      resolution_s operator()(float number) const {
        return {
            .resolution = number,
        };
      }
    };

    struct resolution_rule_v1_handler_s {
      resolution_s operator()(double first, double second) const {
        return {
            .resolution = first / second,
        };
      }
    };

    this->strnum = lexeme[-(qi::char_('+') | qi::char_('-')) >> +(qi::digit | qi::char_(',') | qi::char_('.'))];
    this->num = this->strnum[_val = boost::phoenix::function<_detail::string_to_double_s>{}(qi::_1)];

    resolution_rule_v0 = (this->num >> *qi::char_)[qi::_val = boost::phoenix::function<resolution_rule_v0_handler_s>{}(qi::_1)];
    resolution_rule_v1 = (qi::uint_ >> '/' >> qi::uint_ >> *qi::char_)[qi::_val = boost::phoenix::function<resolution_rule_v1_handler_s>{}(qi::_1, qi::_2)];

    this->rule = resolution_rule_v1 | resolution_rule_v0;
  }

  qi::rule<It, resolution_s(), ascii::space_type> resolution_rule_v0, resolution_rule_v1;
};
}; // namespace resolution

std::optional<struct range_s> parseSpnDataRange(const std::string &str);
std::optional<struct size_s> parseSpnSize(const std::string &str);
std::optional<struct offset_s> parseSpnOffset(const std::string &str);
std::optional<struct spn_fragments_s> parseSpnPosition(size_t spn_size_bits, const std::string &str);
std::optional<struct resolution_s> parseSpnResolution(const std::string &str);
}; // namespace parsers

BOOST_FUSION_ADAPT_STRUCT(parsers::range_s, (double, min)(double, max)(std::string, other));
BOOST_FUSION_ADAPT_STRUCT(parsers::size_s, (size_t, size_bytes)(size_t, size_bits));
BOOST_FUSION_ADAPT_STRUCT(parsers::spn_fragments_s::spn_part_s, (size_t, byte_offset)(size_t, bit_offset)(size_t, size));
BOOST_FUSION_ADAPT_STRUCT(parsers::spn_fragments_s, (std::vector<struct parsers::spn_fragments_s::spn_part_s>, spn_fragments));
BOOST_FUSION_ADAPT_STRUCT(parsers::offset_s, (double, offset));
BOOST_FUSION_ADAPT_STRUCT(parsers::resolution_s, (double, resolution));
