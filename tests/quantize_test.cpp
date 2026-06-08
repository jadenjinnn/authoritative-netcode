#include <gtest/gtest.h>

#include "quantize.h"

using sim::dequantize;
using sim::quantize;

namespace
{
    constexpr uint32_t kMaxLevel = (1u << sim::kPosBits) - 1;
    constexpr float kStep = (sim::kPosMax - sim::kPosMin) / static_cast<float>(kMaxLevel);
}

TEST(Quantize, BoundariesAreExact) {
    EXPECT_EQ(quantize(sim::kPosMin), 0u);
    EXPECT_EQ(quantize(sim::kPosMax), kMaxLevel);
    EXPECT_FLOAT_EQ(dequantize(0), sim::kPosMin);
    EXPECT_FLOAT_EQ(dequantize(kMaxLevel), sim::kPosMax);
}

TEST(Quantize, RoundTripWithinOneStep) {
    for (float v = sim::kPosMin; v <= sim::kPosMax; v += 0.37f) {
        EXPECT_NEAR(dequantize(quantize(v)), v, kStep);
    }
}

TEST(Quantize, ClampsOutOfRange) {
    EXPECT_EQ(quantize(sim::kPosMin - 50.0f), 0u);
    EXPECT_EQ(quantize(sim::kPosMax + 50.0f), kMaxLevel);
}

TEST(Quantize, TopLevelFitsInBits) {
    // Divisor must be 2^bits - 1, not 2^bits: the max position maps to the top level
    // and no higher (otherwise it overflows kPosBits).
    EXPECT_LE(quantize(sim::kPosMax), kMaxLevel);
}
