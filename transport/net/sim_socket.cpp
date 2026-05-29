#include "sim_socket.h"

namespace net {

SimSocket::SimSocket(ISocket& inner, SimParams params)
    : inner_(inner), rng_(params.seed), drop_(params.loss) {}

size_t SimSocket::send_to(const void* data, size_t len, const Endpoint& dest) {
    ++sent_;
    if (drop_(rng_)) {
        ++dropped_;
        return len;
    }
    return inner_.send_to(data, len, dest);
}

std::optional<RecvResult> SimSocket::try_recv_from(void* buf, size_t cap) {
    return inner_.try_recv_from(buf, cap);
}

Endpoint SimSocket::local_endpoint() const {
    return inner_.local_endpoint();
}

}  // namespace net
