#include "quantize.h"

#include <algorithm>
#include <cmath>

namespace sim
{

    namespace
    {
        constexpr uint32_t kMaxLevel = (1u << kPosBits) - 1;
    }

    uint32_t quantize(float v)
    {
        v = std::clamp(v, kPosMin, kPosMax);
        float frac = (v - kPosMin) / (kPosMax - kPosMin);
        return static_cast<uint32_t>(std::lround(frac * kMaxLevel));
    }

    float dequantize(uint32_t q)
    {
        return kPosMin + (static_cast<float>(q) / kMaxLevel) * (kPosMax - kPosMin);
    }

} // namespace sim
