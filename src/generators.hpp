#pragma once

#include <cstdint>

// Abstract value generator. Derived classes live in generators.cpp; the factory
// `makeValueGenerator` (declared extern at call sites) builds the right subclass
// from a YAML config node.
class ValueGenerator {
public:
  virtual ~ValueGenerator() = default;

  // Return the current physical value at `elapsed_ms` relative to playback start.
  virtual double nextValue(int64_t elapsed_ms) = 0;
};
