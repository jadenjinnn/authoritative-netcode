#include "quantize.h"

#include <algorithm>
#include <cmath>

namespace sim
{

    namespace
    {
        constexpr uint32_t kMaxLevel = (1u << kPosBits) - 1;
    }

    uint32_t quantize(float v, float max)
    {
        v = std::clamp(v, 0.0f, max);
        float frac = (max > 0.0f) ? v / max : 0.0f;
        return static_cast<uint32_t>(std::lround(frac * kMaxLevel));
    }

    float dequantize(uint32_t q, float max)
    {
        return (static_cast<float>(q) / kMaxLevel) * max;
    }

} // namespace sim
