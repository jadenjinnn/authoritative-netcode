// Send-rate governor over a scripted RTT profile. We feed RateGovernor a smoothed RTT
// that starts healthy, spikes into congestion, recovers, then relapses fast, and print
// how the good/bad mode and the resulting send rate respond. No real timer is involved,
// so the run is deterministic and the numbers are reproducible for the README. The send
// rate backs off as RTT crosses the threshold, and the doubled penalty makes the second
// recovery take twice as long after the fast relapse.

#include <cstdint>
#include <cstdio>

#include "rate_governor.h"

namespace {

struct Sample {
    uint64_t t_ms;
    double rtt_ms;
};

const char* mode_name(reliable::RateGovernor::Mode m) {
    return m == reliable::RateGovernor::Mode::Good ? "GOOD" : "BAD ";
}

}  // namespace

int main() {
    const Sample profile[] = {
        {0, 80}, {2000, 85}, {4000, 80}, {6000, 85}, {8000, 80}, {10000, 82},
        {11000, 320}, {12000, 330}, {13000, 90}, {15000, 85}, {16000, 80},
        {17000, 360}, {18000, 340}, {19000, 90}, {22000, 85}, {24000, 80},
        {26000, 80}, {28000, 80},
    };

    reliable::RateGovernor gov;

    std::printf("    time     rtt   mode   interval    rate   penalty\n");
    std::printf("    ----     ---   ----   --------    ----   -------\n");
    reliable::RateGovernor::Mode last = gov.mode();
    for (const Sample& s : profile) {
        gov.update(s.rtt_ms * 1000.0, s.t_ms * 1000u);
        uint64_t interval = gov.send_interval_us();
        double hz = 1'000'000.0 / interval;
        const char* mark = gov.mode() != last ? "  <-- mode change" : "";
        std::printf("  %6llums  %5.0fms  %s  %6llu us  %5.1f/s   %4.1fs%s\n",
                    (unsigned long long)s.t_ms, s.rtt_ms, mode_name(gov.mode()),
                    (unsigned long long)interval, hz, gov.penalty_us() / 1e6, mark);
        last = gov.mode();
    }
    return 0;
}
