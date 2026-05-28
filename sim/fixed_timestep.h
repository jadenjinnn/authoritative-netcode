#pragma once

namespace sim {

class FixedTimestep {
public:
    FixedTimestep(double fixed_dt, int max_steps = 8);

    // Banks real_elapsed, returns how many fixed steps are now due.
    // Capped at max_steps to bound catch-up work.
    int advance(double real_elapsed);

    // Leftover fraction toward the next step, in [0, 1).
    double alpha() const;

private:
    double fixed_dt_;
    int max_steps_;
    double accumulator_ = 0.0;
};

}  // namespace sim
