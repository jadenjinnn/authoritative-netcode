#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "endpoint.h"
#include "socket.h"

namespace net {

// RAII wrapper around a BSD UDP socket (Linux only).
class UdpSocket : public ISocket {
public:
    using RecvResult = net::RecvResult;

    UdpSocket();
    ~UdpSocket() override;

    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;
    UdpSocket(UdpSocket&& other) noexcept;
    UdpSocket& operator=(UdpSocket&& other) noexcept;

    // 0 = let the OS assign an ephemeral port.
    void bind(uint16_t port);

    void set_recv_timeout(int millis);

    // Non-blocking mode: recv calls return immediately when nothing is queued.
    void set_nonblocking();

    size_t send_to(const void* data, size_t len, const Endpoint& dest) override;

    // Blocks until a datagram arrives (or the recv timeout elapses, if set).
    RecvResult recv_from(void* buf, size_t cap);

    // nullopt when no datagram is queued (only meaningful in non-blocking mode).
    std::optional<RecvResult> try_recv_from(void* buf, size_t cap) override;

    Endpoint local_endpoint() const override;

private:
    int fd_ = -1;
};

}  // namespace net
