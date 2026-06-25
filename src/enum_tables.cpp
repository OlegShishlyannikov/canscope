#include "enum_tables.hpp"

#include <array>

namespace enum_tables {
namespace {

// J1939-73 Appendix A.1.2. Slot 24..30 is "Reserved for assignment by SAE"
// per A.1.2.25, intentionally left empty so the lookup falls back to a bare
// numeric render at the call site.
constexpr std::array<std::string_view, 32> kFmi = {
    /*  0 */ "Data Valid But Above Normal Operational Range - Most Severe Level",
    /*  1 */ "Data Valid But Below Normal Operational Range - Most Severe Level",
    /*  2 */ "Data Erratic, Intermittent or Incorrect",
    /*  3 */ "Voltage Above Normal, or Shorted to High Source",
    /*  4 */ "Voltage Below Normal, or Shorted to Low Source",
    /*  5 */ "Current Below Normal or Open Circuit",
    /*  6 */ "Current Above Normal or Grounded Circuit",
    /*  7 */ "Mechanical System Not Responding or Out of Adjustment",
    /*  8 */ "Abnormal Frequency or Pulse Width or Period",
    /*  9 */ "Abnormal Update Rate",
    /* 10 */ "Abnormal Rate of Change",
    /* 11 */ "Root Cause Not Known",
    /* 12 */ "Bad Intelligent Device or Component",
    /* 13 */ "Out of Calibration",
    /* 14 */ "Special Instructions #1",
    /* 15 */ "Data Valid but Above Normal Operating Range - Least Severe Level",
    /* 16 */ "Data Valid but Above Normal Operating Range - Moderately Severe Level",
    /* 17 */ "Data Valid but Below Normal Operating Range - Least Severe Level",
    /* 18 */ "Data Valid but Below Normal Operating Range - Moderately Severe Level",
    /* 19 */ "Received Network Data in Error",
    /* 20 */ "Data Drifted High",
    /* 21 */ "Data Drifted Low",
    /* 22 */ "Special Instructions #2",
    /* 23 */ "Request DM60 for More Information",
    /* 24 */ "",
    /* 25 */ "",
    /* 26 */ "",
    /* 27 */ "",
    /* 28 */ "",
    /* 29 */ "",
    /* 30 */ "",
    /* 31 */ "Condition Exists",
};

// J1939-73 §5.7.24.2.* — DM24 / DM5 / DM26 "supported" bit. Note the
// inverted polarity: 0 means supported, 1 means not supported. Without a
// named table, the raw `0`/`1` value reads ambiguously in the UI.
constexpr std::array<std::string_view, 2> kSupport = {
    /* 0 */ "supported",
    /* 1 */ "not supported",
};

// J1939-73 §5.7.1.14 — SPN Conversion Method (SPN 1706, 1 bit) in DM1/2/
// 12/23/27/28. 0 = current method per §5.7.1.12; 1 = pre-2006 legacy.
// The legacy method preceded the standardised SPN/FMI/CM/OC packing and
// is only emitted by very old ECUs.
constexpr std::array<std::string_view, 2> kConversionMethod = {
    /* 0 */ "current method",
    /* 1 */ "pre-2006 legacy",
};

// J1939-73 §5.7.34 — NTE (Not To Exceed) area status (DM34). All six SPNs
// (4127..4132, both NOx and PM variants of control / carve-out / deficiency)
// share the same 2-bit encoding.
constexpr std::array<std::string_view, 4> kNteArea = {
    /* 0 */ "outside area",
    /* 1 */ "inside area",
    /* 2 */ "SAE reserved",
    /* 3 */ "not available",
};

} // namespace

std::string_view fmiName(int32_t value) {
  if (value < 0 || static_cast<size_t>(value) >= kFmi.size()) {
    return {};
  }
  return kFmi[value];
}

std::string_view lookup(std::string_view table_name, int32_t value) {
  if (table_name == "fmi") {
    return fmiName(value);
  }
  if (table_name == "support") {
    if (value < 0 || static_cast<size_t>(value) >= kSupport.size()) {
      return {};
    }
    return kSupport[value];
  }
  if (table_name == "conversionmethod") {
    if (value < 0 || static_cast<size_t>(value) >= kConversionMethod.size()) {
      return {};
    }
    return kConversionMethod[value];
  }
  if (table_name == "ntearea") {
    if (value < 0 || static_cast<size_t>(value) >= kNteArea.size()) {
      return {};
    }
    return kNteArea[value];
  }
  return {};
}

} // namespace enum_tables
