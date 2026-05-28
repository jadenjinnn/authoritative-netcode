#include <cmath>

#include "fixed_timestep.h"

namespace sim
{

    FixedTimestep::FixedTimestep(double fixed_dt, int max_steps)
        : fixed_dt_(fixed_dt), max_steps_(max_steps) {}

    int FixedTimestep::advance(double real_elapsed)
    {
        accumulator_ += real_elapsed;

        int steps_taken = 0;

        while (accumulator_ >= fixed_dt_ && steps_taken < max_steps_)
        {
            accumulator_ -= fixed_dt_;
            steps_taken += 1;
        }

        accumulator_ = ::fmod(accumulator_, fixed_dt_);

        return steps_taken;
    }

    double FixedTimestep::alpha() const
    {
        return accumulator_ / fixed_dt_;
    }

} // namespace sim
