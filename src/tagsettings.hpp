#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct spn_fragment_s {
  std::string byte_offset;
  std::string bit_offset;
  std::string bit_count;
};

struct spn_settings_s {
  bool selected = false;
  std::string spn_id;
  std::string spn_name;
  std::string resolution;
  std::string offset;
  bool big_endian = false;
  std::string unit;
  std::string uuid;
  std::vector<spn_fragment_s> fragments = {{}};
  int32_t active_fragment = 0;

  // Legacy compat
  std::string x_coeff;
  std::string y_coeff;
  std::string size;
  std::string pos;
  std::string bit_offset;
  std::string bit_count;
  bool le = false;
  bool discrete = false;
};

using spn_settings_map_t = std::map<std::string, std::map<int32_t, spn_settings_s>>;
