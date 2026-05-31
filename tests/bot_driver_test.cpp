#include <cmath>
#include <set>

#include <gtest/gtest.h>

#include "bot.h"

using bots::BotDriver;

namespace {
constexpr uint64_t kTick = 16'667;  // ~60 Hz
}

// Every tick produces an input -- the unreliable upstream never goes silent.
TEST(BotDriver, EmitsAnInputEveryTick) {
    BotDriver d(1);
    uint64_t now = 0;
    for (int i = 0; i < 300; ++i) {
        BotDriver::Emit e = d.tick(now);
        EXPECT_LE(std::fabs(e.input.dx), 1.0f);
        EXPECT_LE(std::fabs(e.input.dy), 1.0f);
        now += kTick;
    }
}

// Reliable events fire on a cadence: some, but nowhere near every tick.
TEST(BotDriver, EmitsEventsOccasionallyNotEveryTick) {
    BotDriver d(7);
    uint64_t now = 0;
    int events = 0;
    constexpr int kTicks = 600;  // ~10 s
    for (int i = 0; i < kTicks; ++i) {
        if (d.tick(now).event) {
            ++events;
        }
        now += kTick;
    }
    EXPECT_GT(events, 0) << "driver never emits a reliable event";
    EXPECT_LT(events, kTicks / 4) << "driver floods the reliable channel";
}

// The movement pattern varies -- a bot doesn't push one fixed direction forever.
TEST(BotDriver, MovementDirectionVaries) {
    BotDriver d(42);
    uint64_t now = 0;
    std::set<std::pair<int, int>> directions;
    for (int i = 0; i < 600; ++i) {
        BotDriver::Emit e = d.tick(now);
        // Bucket the heading coarsely; a constant direction yields a single bucket.
        directions.insert({static_cast<int>(std::round(e.input.dx * 4)),
                           static_cast<int>(std::round(e.input.dy * 4))});
        now += kTick;
    }
    EXPECT_GT(directions.size(), 1u) << "heading never changes";
}

// Same seed -> same sequence (deterministic), so measurement runs are reproducible.
TEST(BotDriver, SeededRunIsDeterministic) {
    BotDriver a(99);
    BotDriver b(99);
    uint64_t now = 0;
    for (int i = 0; i < 200; ++i) {
        BotDriver::Emit ea = a.tick(now);
        BotDriver::Emit eb = b.tick(now);
        EXPECT_FLOAT_EQ(ea.input.dx, eb.input.dx);
        EXPECT_FLOAT_EQ(ea.input.dy, eb.input.dy);
        EXPECT_EQ(ea.event.has_value(), eb.event.has_value());
        now += kTick;
    }
}
