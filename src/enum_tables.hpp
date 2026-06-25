#pragma once

#include <cstdint>
#include <string_view>

// Lookup tables for SPN values whose meaning is enumerated, not numeric.
// Used by processFrame when an SPN's value_encoding column is
// "table:<name>" — produced by the `Table: <NAME>` Resolution syntax in
// .piklema.
namespace enum_tables {

// J1939-73 Appendix A — Failure Mode Identification Codes (FMI 0..31).
// Returns "" for codes 24..30 (Reserved) and out-of-range values.
std::string_view fmiName(int32_t value);

// Generic dispatcher keyed by table name (lowercase, e.g. "fmi").
// Returns "" for unknown table / value.
std::string_view lookup(std::string_view table_name, int32_t value);

} // namespace enum_tables
