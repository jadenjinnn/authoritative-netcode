#include <cstdint>

#include <gtest/gtest.h>

#include "rate_governor.h"

using reliable::RateGovernor;
using Mode = reliable::RateGovernor::Mode;

namespace {
constexpr double kGood = 80'000.0;   // 80 ms, well under the 250 ms threshold
constexpr double kBad = 300'000.0;   // 300 ms, over it
constexpr uint64_t kSec = 1'000'000;
}  // namespace

TEST(RateGovernor, SeedsInGoodMode) {
    RateGovernor g;
    g.update(kGood, 0);
    EXPECT_EQ(g.mode(), Mode::Good);
    EXPECT_EQ(g.send_interval_us(), 33'333u);
    EXPECT_EQ(g.penalty_us(), 4u * kSec);
}

TEST(RateGovernor, CrossingThresholdDropsToBad) {
    RateGovernor g;
    g.update(kGood, 0);
    g.update(kBad, 1 * kSec);
    EXPECT_EQ(g.mode(), Mode::Bad);
    EXPECT_EQ(g.send_interval_us(), 100'000u);
}

TEST(RateGovernor, DropAfterLongGoodKeepsBasePenalty) {
    RateGovernor g;
    g.update(kGood, 0);
    g.update(kGood, 11 * kSec);   // still healthy 11 s in
    g.update(kBad, 12 * kSec);    // good lasted >= 10 s, so not a relapse
    EXPECT_EQ(g.mode(), Mode::Bad);
    EXPECT_EQ(g.penalty_us(), 4u * kSec);
}

TEST(RateGovernor, RecoversToGoodOnlyAfterPenaltyElapses) {
    RateGovernor g;
    g.update(kGood, 0);
    g.update(kBad, 12 * kSec);    // -> Bad, base penalty 4 s, streak clock starts here
    g.update(kGood, 14 * kSec);   // healthy 2 s < 4 s
    EXPECT_EQ(g.mode(), Mode::Bad);
    g.update(kGood, 16 * kSec);   // healthy 4 s >= 4 s
    EXPECT_EQ(g.mode(), Mode::Good);
    EXPECT_EQ(g.send_interval_us(), 33'333u);
}

TEST(RateGovernor, CongestionSpikeRestartsEarnBack) {
    RateGovernor g;
    g.update(kGood, 0);
    g.update(kBad, 12 * kSec);    // -> Bad, streak clock @12s
    g.update(kGood, 14 * kSec);   // 2 s of health banked
    g.update(kBad, 15 * kSec);    // spike: streak clock restarts @15s
    g.update(kGood, 18 * kSec);   // only 3 s since the spike < 4 s
    EXPECT_EQ(g.mode(), Mode::Bad);
    g.update(kGood, 19 * kSec);   // 4 s since the spike
    EXPECT_EQ(g.mode(), Mode::Good);
}

TEST(RateGovernor, QuickRelapseDoublesPenalty) {
    RateGovernor g;
    g.update(kGood, 0);
    g.update(kBad, 12 * kSec);    // -> Bad, base penalty 4 s
    g.update(kGood, 16 * kSec);   // earned back after 4 s -> Good @16s
    ASSERT_EQ(g.mode(), Mode::Good);
    g.update(kBad, 17 * kSec);    // good lasted only 1 s < 10 s: relapse
    EXPECT_EQ(g.mode(), Mode::Bad);
    EXPECT_EQ(g.penalty_us(), 8u * kSec);
}

TEST(RateGovernor, PenaltyDoublingIsCapped) {
    RateGovernor::Config cfg;
    cfg.initial_penalty_us = 40 * kSec;   // one double overshoots the 60 s cap
    RateGovernor g(cfg);
    g.update(kGood, 0);
    g.update(kBad, 1 * kSec);     // relapse within dwell -> 80 s, clamped to 60 s
    EXPECT_EQ(g.penalty_us(), 60u * kSec);
}
