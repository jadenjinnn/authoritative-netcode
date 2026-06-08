#include <gtest/gtest.h>

#include "quantize.h"

using sim::dequantize;
using sim::quantize;

namespace
{
    constexpr uint32_t kMaxLevel = (1u << sim::kPosBits) - 1;
}

TEST(Quantize, BoundariesAreExact) {
    for (float max : {100.0f, 200.0f}) {
        EXPECT_EQ(quantize(0.0f, max), 0u);
        EXPECT_EQ(quantize(max, max), kMaxLevel);
        EXPECT_FLOAT_EQ(dequantize(0, max), 0.0f);
        EXPECT_FLOAT_EQ(dequantize(kMaxLevel, max), max);
    }
}

TEST(Quantize, RoundTripWithinOneStep) {
    for (float max : {100.0f, 200.0f}) {
        float step = max / kMaxLevel;
        for (float v = 0.0f; v <= max; v += max / 270.0f) {
            EXPECT_NEAR(dequantize(quantize(v, max), max), v, step);
        }
    }
}

TEST(Quantize, ClampsOutOfRange) {
    EXPECT_EQ(quantize(-50.0f, 100.0f), 0u);
    EXPECT_EQ(quantize(150.0f, 100.0f), kMaxLevel);
}

TEST(Quantize, TopLevelFitsInBits) {
    // Divisor must be 2^bits - 1, not 2^bits: max maps to the top level, no overflow.
    EXPECT_LE(quantize(100.0f, 100.0f), kMaxLevel);
}
