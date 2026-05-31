#pragma once

#include <cstdint>
#include <optional>
#include <random>

#include "input.h"

namespace bots {

// Per-bot input driver: a small state machine that decides what one bot puts on the
// wire each tick. It emits an unreliable Input every tick (a movement pattern, the
// realistic dominant upstream) and an occasional reliable event (a discrete action,
// exercising Peer's reliable channel). Pure decision logic -- no sockets, no Peer --
// driven by a caller-supplied microsecond clock so a test can drive it deterministically.
class BotDriver {
public:
    struct Emit {
        sim::Input input;                  // unreliable, every tick
        std::optional<uint32_t> event;     // reliable, only on the driver's cadence
    };

    explicit BotDriver(uint32_t seed);

    // Decide this tick's traffic. Called once per bot per tick.
    Emit tick(uint64_t now_us);

private:
    std::mt19937 rng_;
    uint64_t tick_count_ = 0;

    // Current wander heading, held for a number of ticks then re-rolled.
    float heading_dx_ = 0.0f;
    float heading_dy_ = 0.0f;
    int heading_ticks_left_ = 0;
};

}  // namespace bots
