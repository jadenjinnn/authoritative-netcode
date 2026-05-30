#include <cstdint>

#include <gtest/gtest.h>

#include "rtt.h"

using reliable::RttEstimator;

TEST(Rtt, FirstAckSeedsEstimate) {
    RttEstimator rtt;
    EXPECT_FALSE(rtt.has_estimate());

    rtt.on_sent(1, 1000);
    EXPECT_TRUE(rtt.on_acked(1, 1500));

    EXPECT_TRUE(rtt.has_estimate());
    EXPECT_DOUBLE_EQ(rtt.smoothed_us(), 500.0);
    EXPECT_EQ(rtt.latest_sample_us(), 500u);
}

TEST(Rtt, EwmaLagsTowardNewSamples) {
    RttEstimator rtt;

    rtt.on_sent(1, 0);
    rtt.on_acked(1, 100);  // seed at 100
    rtt.on_sent(2, 0);
    rtt.on_acked(2, 200);  // 100 + 0.1*(200-100) = 110

    EXPECT_NEAR(rtt.smoothed_us(), 110.0, 1e-9);
    EXPECT_EQ(rtt.latest_sample_us(), 200u);
}

TEST(Rtt, DuplicateAckSampledOnce) {
    RttEstimator rtt;

    rtt.on_sent(5, 0);
    EXPECT_TRUE(rtt.on_acked(5, 300));
    EXPECT_FALSE(rtt.on_acked(5, 99999));  // already sampled

    EXPECT_DOUBLE_EQ(rtt.smoothed_us(), 300.0);
    EXPECT_EQ(rtt.latest_sample_us(), 300u);
}

TEST(Rtt, UnknownAckIgnored) {
    RttEstimator rtt;
    EXPECT_FALSE(rtt.on_acked(42, 500));  // never sent
    EXPECT_FALSE(rtt.has_estimate());
}
