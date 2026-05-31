#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include "endpoint.h"
#include "peer.h"
#include "socket.h"

namespace reliable {

// Server-side fan-out: one Peer per remote endpoint over a single shared socket.
// A Peer drains its own socket and assumes everything it reads came from its one
// remote, so it can't share the server's socket directly. PeerManager owns the real
// socket, drains it once per tick, and routes each datagram by source into that
// peer's private inbox; the Peer then drains the inbox. Sends pass straight through
// to the shared socket (the Peer already addresses its remote). Peer is unmodified.
class PeerManager {
public:
    // One client's demuxed messages for a single poll.
    struct Incoming {
        net::Endpoint from;
        bool is_new;             // this endpoint first appeared on this poll
        Peer::Delivery delivery;
    };

    explicit PeerManager(net::ISocket& socket);

    // Drain the shared socket, route datagrams to per-peer inboxes (creating a Peer
    // on first contact), then poll each peer that received something. Returns one
    // Incoming per such peer, in first-seen order.
    std::vector<Incoming> poll(uint64_t now_us);

    // Pace + flush every peer (the per-tick broadcast goes out here).
    void update_all(uint64_t now_us);

    Peer& peer(const net::Endpoint& who) { return conns_.at(who)->peer; }
    bool has(const net::Endpoint& who) const { return conns_.count(who) > 0; }
    void remove(const net::Endpoint& who) { conns_.erase(who); }

    std::vector<net::Endpoint> endpoints() const;
    size_t size() const { return conns_.size(); }
    uint64_t total_bytes_sent() const;
    uint64_t total_packets_sent() const;

private:
    // Per-peer ISocket: receives from a queue PeerManager fills, sends to the shared
    // socket. The Peer holds a reference to this, so a Conn must never move once made.
    class Inbox : public net::ISocket {
    public:
        Inbox(net::ISocket& shared, net::Endpoint remote)
            : shared_(shared), remote_(std::move(remote)) {}

        size_t send_to(const void* data, size_t len, const net::Endpoint& dest) override {
            return shared_.send_to(data, len, dest);
        }

        std::optional<net::RecvResult> try_recv_from(void* buf, size_t cap) override {
            if (queue_.empty()) {
                return std::nullopt;
            }
            std::vector<uint8_t> d = std::move(queue_.front());
            queue_.pop_front();
            size_t n = std::min(cap, d.size());
            std::memcpy(buf, d.data(), n);
            return net::RecvResult{n, remote_};
        }

        net::Endpoint local_endpoint() const override { return shared_.local_endpoint(); }

        void deliver(const uint8_t* data, size_t len) { queue_.emplace_back(data, data + len); }

    private:
        net::ISocket& shared_;
        net::Endpoint remote_;
        std::deque<std::vector<uint8_t>> queue_;
    };

    struct Conn {
        Inbox inbox;
        Peer peer;
        Conn(net::ISocket& shared, net::Endpoint remote)
            : inbox(shared, remote), peer(inbox, std::move(remote)) {}
        Conn(const Conn&) = delete;
        Conn& operator=(const Conn&) = delete;
    };

    net::ISocket& socket_;
    std::map<net::Endpoint, std::unique_ptr<Conn>> conns_;
    std::array<uint8_t, 8192> rx_buf_{};
};

}  // namespace reliable
