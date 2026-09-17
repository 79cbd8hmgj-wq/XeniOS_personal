/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_METAL_METAL_TELEMETRY_H_
#define XENIA_UI_METAL_METAL_TELEMETRY_H_

#include <cstdint>

namespace xe {
namespace ui {
namespace metal {

// Allocation-free aggregation for timing and count samples. Callers supply
// already-measured values so aggregation itself performs no clock reads or
// allocations and is safe to keep adjacent to Metal hot paths.
struct MetalTelemetryAccumulator {
  uint64_t count = 0;
  uint64_t total = 0;
  uint64_t min = 0;
  uint64_t max = 0;

  constexpr bool empty() const noexcept { return count == 0; }

  constexpr void Add(uint64_t value) noexcept {
    if (count == 0) {
      min = value;
      max = value;
    } else {
      if (value < min) {
        min = value;
      }
      if (value > max) {
        max = value;
      }
    }
    ++count;
    total += value;
  }

  constexpr void Reset() noexcept {
    count = 0;
    total = 0;
    min = 0;
    max = 0;
  }
};

}  // namespace metal
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_METAL_METAL_TELEMETRY_H_
