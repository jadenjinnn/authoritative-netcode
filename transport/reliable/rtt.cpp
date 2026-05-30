#include "rtt.h"

namespace reliable
{

    void RttEstimator::on_sent(uint16_t sequence, uint64_t now_us)
    {
        size_t i = sequence % kWindow;
        sent_at_us_[i] = now_us;
        awaiting_[i] = true;
    }

    bool RttEstimator::on_acked(uint16_t sequence, uint64_t now_us)
    {
        size_t i = sequence % kWindow;
        if (!awaiting_[i])
        {
            return false;
        }
        awaiting_[i] = false;

        uint64_t sample = now_us - sent_at_us_[i];
        latest_sample_us_ = sample;

        if (!has_estimate_)
        {
            smoothed_us_ = static_cast<double>(sample);
            has_estimate_ = true;
        }
        else
        {
            smoothed_us_ += kGain * (static_cast<double>(sample) - smoothed_us_);
        }
        return true;
    }

} // namespace reliable
