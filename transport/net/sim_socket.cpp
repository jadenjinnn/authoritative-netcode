#include "sim_socket.h"

#include <chrono>
#include <utility>

namespace net {

namespace {

uint64_t steady_now_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

}  // namespace

SimSocket::SimSocket(ISocket& inner, SimParams params, Clock clock)
    : inner_(inner),
      clock_(clock ? std::move(clock) : Clock(steady_now_us)),
      rng_(params.seed),
      drop_(params.loss),
      jitter_(-static_cast<int>(params.jitter_us), static_cast<int>(params.jitter_us)),
      delay_us_(params.delay_us),
      jitter_us_(params.jitter_us) {}

uint64_t SimSocket::next_delay_us() {
    int64_t delay = static_cast<int64_t>(delay_us_);
    if (jitter_us_ > 0) {
        delay += jitter_(rng_);
    }
    return delay < 0 ? 0 : static_cast<uint64_t>(delay);
}

size_t SimSocket::send_to(const void* data, size_t len, const Endpoint& dest) {
    ++sent_;
    if (drop_(rng_)) {
        ++dropped_;
        return len;
    }
    if (delay_us_ == 0) {
        return inner_.send_to(data, len, dest);
    }
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    queue_.push_back(Delayed{clock_() + next_delay_us(), dest, std::vector<uint8_t>(bytes, bytes + len)});
    return len;
}

void SimSocket::pump() {
    if (delay_us_ == 0) {
        return;
    }
    uint64_t now = clock_();
    for (auto it = queue_.begin(); it != queue_.end();) {
        if (it->release_us <= now) {
            inner_.send_to(it->data.data(), it->data.size(), it->dest);
            it = queue_.erase(it);
        } else {
            ++it;
        }
    }
}

std::optional<RecvResult> SimSocket::try_recv_from(void* buf, size_t cap) {
    return inner_.try_recv_from(buf, cap);
}

Endpoint SimSocket::local_endpoint() const {
    return inner_.local_endpoint();
}

}  // namespace net
