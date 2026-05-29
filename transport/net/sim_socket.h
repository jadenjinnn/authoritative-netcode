#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>

#include "endpoint.h"
#include "socket.h"

namespace net {

struct SimParams {
    double loss = 0.0;     // per-packet drop probability on send, [0,1]
    uint32_t seed = 0;
};

// Decorator over an ISocket that injects controlled network impairment so the
// transport can be measured under known loss. Loss is modeled on the send path:
// a dropped packet is discarded but still reported as sent, mirroring real UDP
// where send succeeds and the datagram vanishes in transit.
class SimSocket : public ISocket {
public:
    SimSocket(ISocket& inner, SimParams params);

    size_t send_to(const void* data, size_t len, const Endpoint& dest) override;
    std::optional<RecvResult> try_recv_from(void* buf, size_t cap) override;
    Endpoint local_endpoint() const override;

    uint64_t sent() const { return sent_; }
    uint64_t dropped() const { return dropped_; }

private:
    ISocket& inner_;
    std::mt19937 rng_;
    std::bernoulli_distribution drop_;
    uint64_t sent_ = 0;
    uint64_t dropped_ = 0;
};

}  // namespace net
