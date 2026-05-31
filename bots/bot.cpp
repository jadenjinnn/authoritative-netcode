#include "bot.h"

#include <cmath>
#include <random>

constexpr float kTwoPi = 6.2831853071795864769f;

namespace bots
{

    BotDriver::BotDriver(uint32_t seed) : rng_(seed)
    {
    }

    BotDriver::Emit BotDriver::tick(uint64_t now_us)
    {
        // Wander: hold a heading for a stretch of ticks, then re-roll a new one.
        if (heading_ticks_left_ == 0)
        {
            std::uniform_real_distribution<float> angle{0.0f, kTwoPi};

            float theta = angle(rng_);

            heading_dx_ = std::cos(theta);
            heading_dy_ = std::sin(theta);

            std::uniform_int_distribution<int> hold{30, 60};

            heading_ticks_left_ = hold(rng_);
        }

        std::uniform_real_distribution<float> d{0.0f, 1.0f};

        float distance = d(rng_);

        float dx = distance * heading_dx_;
        float dy = distance * heading_dy_;
        --heading_ticks_left_;

        ++tick_count_;

        std::optional<uint32_t> event;

        if (tick_count_ % 180 == 0)
        {
            event = tick_count_;
        }

        return Emit{sim::Input{dx, dy}, event};
    }
} // namespace bots
