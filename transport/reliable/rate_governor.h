#pragma once

#include <cstdint>

namespace reliable {

// Gaffer-style good/bad send-rate governor. It thresholds on the smoothed RTT from
// RttEstimator: a healthy link runs in Good mode at the fast send interval, a
// congested one (RTT over threshold) drops to Bad and the slow interval. Dropping to
// Bad arms an adaptive penalty -- the link must then stay healthy for that long before
// it earns its way back to Good, and a fast relapse doubles the penalty so a flapping
// link can't thrash the send rate. Pure policy driven by a caller-supplied microsecond
// clock, so it stays decoupled from Connection and deterministic under test.
class RateGovernor {
public:
    enum class Mode : uint8_t { Good, Bad };

    struct Config {
        uint64_t good_interval_us = 33'333;   // 30 Hz when healthy
        uint64_t bad_interval_us = 100'000;   // 10 Hz when congested
        double threshold_us = 250'000.0;      // RTT setpoint (Gaffer's 250 ms)
        uint64_t initial_penalty_us = 4'000'000;
        uint64_t max_penalty_us = 60'000'000;
        uint64_t good_dwell_us = 10'000'000;  // a drop sooner than this counts as a relapse
    };

    RateGovernor();
    explicit RateGovernor(Config config);

    // Feed the latest smoothed RTT and the current monotonic time (microseconds).
    // Updates the mode and the adaptive penalty; the first call only seeds the clock
    // baseline and stays in Good.
    void update(double smoothed_rtt_us, uint64_t now_us);

    Mode mode() const { return mode_; }
    uint64_t penalty_us() const { return penalty_us_; }
    uint64_t send_interval_us() const { return mode_ == Mode::Good ? config_.good_interval_us : config_.bad_interval_us; }

private:
    Config config_;
    Mode mode_ = Mode::Good;
    uint64_t penalty_us_;  // current earn-back time; grows on relapse, capped at max

    // Timestamp we last entered Good. now - this = how long we've been healthy, which
    // decides whether the next drop counts as a relapse (and doubles the penalty).
    uint64_t good_entered_us_ = 0;
    // Start of the current uninterrupted healthy streak while in Bad; restamped to now
    // on every still-congested sample, so now - this = how long we've earned back.
    uint64_t healthy_since_us_ = 0;
    bool started_ = false;
};

}  // namespace reliable
