#pragma once

#include <cstdint>

namespace sim {

// Position quantization for the wire. World clamps positions to [kPosMin, kPosMax],
// and the codec maps that range onto kPosBits-bit integers. The range here must
// cover World's clamp bound (world.cpp kBound) or values clip at the top.
constexpr float kPosMin = 0.0f;
constexpr float kPosMax = 100.0f;
constexpr int kPosBits = 12;  // 4096 levels; step = (kPosMax - kPosMin) / (2^kPosBits - 1)

// Map a position in [kPosMin, kPosMax] onto [0, 2^kPosBits - 1], clamping
// out-of-range input. dequantize is the inverse; the round-trip is lossy to within
// one step.
uint32_t quantize(float v);
float dequantize(uint32_t q);

}  // namespace sim
