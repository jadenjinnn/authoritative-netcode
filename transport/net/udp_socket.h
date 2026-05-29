#pragma once

#include <cstddef>
#include <cstdint>

#include "endpoint.h"

namespace net {

// RAII wrapper around a BSD UDP socket (Linux only).
class UdpSocket {
public:
    struct RecvResult {
        size_t bytes;
        Endpoint from;
    };

    UdpSocket();
    ~UdpSocket();

    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;
    UdpSocket(UdpSocket&& other) noexcept;
    UdpSocket& operator=(UdpSocket&& other) noexcept;

    // 0 = let the OS assign an ephemeral port.
    void bind(uint16_t port);

    void set_recv_timeout(int millis);

    size_t send_to(const void* data, size_t len, const Endpoint& dest);

    // Blocks until a datagram arrives (or the recv timeout elapses, if set).
    RecvResult recv_from(void* buf, size_t cap);

    Endpoint local_endpoint() const;

private:
    int fd_ = -1;
};

}  // namespace net
