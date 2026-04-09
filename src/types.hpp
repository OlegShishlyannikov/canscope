#pragma once

#include <string_view>

/**
 * @brief Returns pretty-print name of the type
 */
template <class T> constexpr std::string_view type_name() {
  using namespace std;
#if defined(__clang__)
  std::string_view p = __PRETTY_FUNCTION__;
  return std::string_view(p.data() + 34, p.size() - 34 - 1);
#elif defined(__GNUC__)
  std::string_view p = __PRETTY_FUNCTION__;
  return std::string_view(p.data() + 49, p.find(';', 49) - 49);
#endif
}
