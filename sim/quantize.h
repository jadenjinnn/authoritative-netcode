#pragma once

#include <cstdint>

namespace sim {

constexpr int kPosBits = 12;               // bits per axis; 4096 levels
constexpr float kDefaultWorldMax = 100.0f;  // default world bound when none is given

// Map a position in [0, max] onto [0, 2^kPosBits - 1], clamping out-of-range input.
// dequantize is the inverse; the round-trip is lossy to within one step. `max` is the
// world bound (it scales with the world in the density sweep), so it is a parameter
// rather than a constant; kPosBits stays fixed so per-entity bytes don't change.
uint32_t quantize(float v, float max);
float dequantize(uint32_t q, float max);

}  // namespace sim
