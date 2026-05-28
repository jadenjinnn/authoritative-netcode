#include "fixed_timestep.h"

#include <gtest/gtest.h>

using sim::FixedTimestep;

TEST(FixedTimestep, NoStepUntilFullDtAccumulates) {
    FixedTimestep ts(1.0);
    EXPECT_EQ(ts.advance(0.4), 0);
    EXPECT_EQ(ts.advance(0.4), 0);
    EXPECT_EQ(ts.advance(0.4), 1);  // 1.2 banked -> one step
}

TEST(FixedTimestep, CarriesRemainderAcrossCalls) {
    FixedTimestep ts(1.0);
    ts.advance(0.7);
    EXPECT_EQ(ts.advance(0.7), 1);  // 1.4 total -> one step
    EXPECT_NEAR(ts.alpha(), 0.4, 1e-9);
}

TEST(FixedTimestep, RunsMultipleStepsForLargeElapsed) {
    FixedTimestep ts(1.0);
    EXPECT_EQ(ts.advance(2.5), 2);
    EXPECT_NEAR(ts.alpha(), 0.5, 1e-9);
}

TEST(FixedTimestep, ExactMultipleLeavesNoRemainder) {
    FixedTimestep ts(1.0);
    EXPECT_EQ(ts.advance(3.0), 3);
    EXPECT_NEAR(ts.alpha(), 0.0, 1e-9);
}

TEST(FixedTimestep, CapsStepsToAvoidSpiral) {
    FixedTimestep ts(1.0, /*max_steps=*/8);
    EXPECT_EQ(ts.advance(100.0), 8);
}

TEST(FixedTimestep, RecoversAfterCapWithoutBacklog) {
    FixedTimestep ts(1.0, /*max_steps=*/8);
    ts.advance(100.0);
    EXPECT_EQ(ts.advance(1.0), 1);  // unpayable backlog must not be banked
}

TEST(FixedTimestep, AlphaStaysInUnitRange) {
    FixedTimestep ts(1.0);
    ts.advance(0.3);
    EXPECT_GE(ts.alpha(), 0.0);
    EXPECT_LT(ts.alpha(), 1.0);
}
