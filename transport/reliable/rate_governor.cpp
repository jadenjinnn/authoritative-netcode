#include "rate_governor.h"

#include <algorithm>

namespace reliable
{

    RateGovernor::RateGovernor() : RateGovernor(Config{})
    {
    }

    RateGovernor::RateGovernor(Config config)
        : config_(config), penalty_us_(config.initial_penalty_us)
    {
    }

    void RateGovernor::update(double smoothed_rtt_us, uint64_t now_us)
    {
        if (!started_)
        {
            good_entered_us_ = now_us;
            healthy_since_us_ = now_us;
            started_ = true;

            return;
        }

        if (smoothed_rtt_us > config_.threshold_us)
        {
            if (mode_ == Mode::Good && now_us - good_entered_us_ < config_.good_dwell_us)
            {
                penalty_us_ = std::min(penalty_us_ * 2, config_.max_penalty_us);
            }

            mode_ = Mode::Bad;
            healthy_since_us_ = now_us;
        }
        else if (mode_ == Mode::Bad && now_us - healthy_since_us_ >= penalty_us_)
        {
            mode_ = Mode::Good;
            good_entered_us_ = now_us;
        }
    }

} // namespace reliable
