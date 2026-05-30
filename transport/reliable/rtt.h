#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace reliable {

// Round-trip-time estimator driven by the ack system. Tell it when each packet
// sequence was sent and when its ack came back; it samples RTT on the first ack
// of a sequence and keeps an exponentially-weighted moving average (Gaffer's 10%
// gain). Times are caller-supplied microseconds, so it stays clock-agnostic and
// deterministic under test.
class RttEstimator {
public:
    void on_sent(uint16_t sequence, uint64_t now_us);

    // Samples RTT the first time a sequence is acked; returns true if a sample
    // was taken. Duplicate or never-sent sequences are ignored (return false).
    bool on_acked(uint16_t sequence, uint64_t now_us);

    bool has_estimate() const { return has_estimate_; }
    double smoothed_us() const { return smoothed_us_; }
    uint64_t latest_sample_us() const { return latest_sample_us_; }

private:
    static constexpr double kGain = 0.1;
    static constexpr size_t kWindow = 1024;

    std::array<uint64_t, kWindow> sent_at_us_{};
    std::array<bool, kWindow> awaiting_{};
    bool has_estimate_ = false;
    double smoothed_us_ = 0.0;
    uint64_t latest_sample_us_ = 0;
};

}  // namespace reliable
