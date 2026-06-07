#include <gtest/gtest.h>

#include "input.h"

using sim::Input;

TEST(Input, RoundTripWithTick) {
    Input in{0.25f, -0.5f, 4242u};
    std::vector<uint8_t> bytes = sim::encode_input(in);

    Input out;
    ASSERT_TRUE(sim::decode_input(bytes, out));
    EXPECT_FLOAT_EQ(out.dx, 0.25f);
    EXPECT_FLOAT_EQ(out.dy, -0.5f);
    EXPECT_EQ(out.last_received_tick, 4242u);
}

TEST(Input, RejectsShortBuffer) {
    std::vector<uint8_t> too_short(3, 0);
    Input out;
    EXPECT_FALSE(sim::decode_input(too_short, out));
}
