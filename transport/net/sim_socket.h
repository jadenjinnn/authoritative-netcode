#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <random>
#include <vector>

#include "endpoint.h"
#include "socket.h"

namespace net {

struct SimParams {
    double loss = 0.0;       // per-packet drop probability on send, [0,1]
    uint32_t seed = 0;
    uint32_t delay_us = 0;   // one-way latency added on the send path, microseconds
    uint32_t jitter_us = 0;  // uniform +/- jitter around delay, microseconds
};

// Monotonic clock in microseconds; injectable so tests can drive time directly.
using Clock = std::function<uint64_t()>;

// Decorator over an ISocket that injects controlled network impairment so the
// transport can be measured under known conditions. Loss is modeled on the send
// path: a dropped packet is reported as sent but discarded, like real UDP where
// send succeeds and the datagram vanishes in transit. Latency holds each sent
// datagram in a queue until its release time; pump() flushes ready datagrams to
// the inner socket. With delay_us == 0 the queue is bypassed and sends forward
// immediately, so loss-only callers need no pump(). Jitter lets a later datagram
// release first -- i.e. it models reordering for free.
class SimSocket : public ISocket {
public:
    SimSocket(ISocket& inner, SimParams params, Clock clock = {});

    size_t send_to(const void* data, size_t len, const Endpoint& dest) override;
    std::optional<RecvResult> try_recv_from(void* buf, size_t cap) override;
    Endpoint local_endpoint() const override;

    // Forward every queued datagram whose release time has passed. No-op when
    // delay is zero; otherwise call once per loop iteration.
    void pump();

    uint64_t sent() const { return sent_; }
    uint64_t dropped() const { return dropped_; }

private:
    struct Delayed {
        uint64_t release_us;
        Endpoint dest;
        std::vector<uint8_t> data;
    };

    uint64_t next_delay_us();

    ISocket& inner_;
    Clock clock_;
    std::mt19937 rng_;
    std::bernoulli_distribution drop_;
    std::uniform_int_distribution<int> jitter_;
    uint32_t delay_us_;
    uint32_t jitter_us_;
    std::vector<Delayed> queue_;
    uint64_t sent_ = 0;
    uint64_t dropped_ = 0;
};

}  // namespace net
