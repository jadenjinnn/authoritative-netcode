#pragma once

#include <cstddef>
#include <optional>

#include "endpoint.h"

namespace net {

struct RecvResult {
    size_t bytes;
    Endpoint from;
};

// Minimal send/receive seam shared by the real socket and the artificial-network
// shim, so transport code can run against a lossy socket without knowing it.
class ISocket {
public:
    virtual ~ISocket() = default;

    virtual size_t send_to(const void* data, size_t len, const Endpoint& dest) = 0;
    virtual std::optional<RecvResult> try_recv_from(void* buf, size_t cap) = 0;
    virtual Endpoint local_endpoint() const = 0;
};

}  // namespace net
