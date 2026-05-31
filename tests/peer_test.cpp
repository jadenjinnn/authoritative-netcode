#include <cstdint>
#include <cstring>
#include <deque>
#include <optional>
#include <random>
#include <vector>

#include <gtest/gtest.h>

#include "endpoint.h"
#include "fragment.h"
#include "peer.h"
#include "socket.h"

using reliable::Peer;

namespace {

using Datagram = std::vector<uint8_t>;

// In-memory ISocket over a shared pair of queues, so two Peers can talk without
// real networking. Loss is applied on send (a dropped datagram reports as sent
// but never lands), matching how SimSocket models the wire; a seeded RNG keeps it
// reproducible.
class FakeSocket : public net::ISocket {
public:
    FakeSocket(std::deque<Datagram>& out, std::deque<Datagram>& in, net::Endpoint peer,
               double loss, uint32_t seed)
        : out_(out), in_(in), peer_(std::move(peer)), rng_(seed), drop_(loss) {}

    size_t send_to(const void* data, size_t len, const net::Endpoint&) override {
        if (drop_(rng_)) {
            return len;
        }
        const uint8_t* b = static_cast<const uint8_t*>(data);
        out_.emplace_back(b, b + len);
        return len;
    }

    std::optional<net::RecvResult> try_recv_from(void* buf, size_t cap) override {
        if (in_.empty()) {
            return std::nullopt;
        }
        Datagram d = std::move(in_.front());
        in_.pop_front();
        size_t n = std::min(cap, d.size());
        std::memcpy(buf, d.data(), n);
        return net::RecvResult{n, peer_};
    }

    net::Endpoint local_endpoint() const override { return peer_; }

private:
    std::deque<Datagram>& out_;
    std::deque<Datagram>& in_;
    net::Endpoint peer_;
    std::mt19937 rng_;
    std::bernoulli_distribution drop_;
};

Datagram encode(uint32_t value) {
    Datagram bytes(sizeof(value));
    std::memcpy(bytes.data(), &value, sizeof(value));
    return bytes;
}

constexpr uint64_t kTick = 33'333;

}  // namespace

// Lossless: every reliable message is delivered, and the demux dedupes so the
// count lands exactly at N (no double-counted resends).
TEST(Peer, LosslessDeliversAllReliableExactlyOnce) {
    std::deque<Datagram> c2s, s2c;
    net::Endpoint ca{"127.0.0.1", 1};
    net::Endpoint sa{"127.0.0.1", 2};
    FakeSocket client_sock(c2s, s2c, sa, 0.0, 1);
    FakeSocket server_sock(s2c, c2s, ca, 0.0, 2);
    Peer client(client_sock, sa);
    Peer server(server_sock, ca);

    constexpr int kN = 50;
    for (int i = 0; i < kN; ++i) {
        client.queue_reliable(encode(i));
    }

    int reliable_seen = 0;
    uint64_t now = 0;
    for (int t = 0; t < 200 && reliable_seen < kN; ++t) {
        client.update(now);
        Peer::Delivery from_client = server.poll(now);
        reliable_seen += static_cast<int>(from_client.reliable.size());
        server.update(now);
        client.poll(now);
        now += kTick;
    }

    EXPECT_EQ(reliable_seen, kN);
}

// Under loss the reliable stream still completes (resent until acked) while the
// unreliable stream just thins out -- dropped snapshots stay dropped.
TEST(Peer, UnderLossReliableCompletesUnreliableThins) {
    std::deque<Datagram> c2s, s2c;
    net::Endpoint ca{"127.0.0.1", 1};
    net::Endpoint sa{"127.0.0.1", 2};
    FakeSocket client_sock(c2s, s2c, sa, 0.3, 7);
    FakeSocket server_sock(s2c, c2s, ca, 0.3, 9);
    Peer client(client_sock, sa);
    Peer server(server_sock, ca);

    constexpr int kTicks = 100;
    int reliable_seen = 0;
    int unreliable_seen = 0;
    uint64_t now = 0;

    for (int t = 0; t < kTicks; ++t) {
        client.queue_reliable(encode(t));
        client.queue_unreliable(encode(1000 + t));
        client.update(now);
        Peer::Delivery from_client = server.poll(now);
        reliable_seen += static_cast<int>(from_client.reliable.size());
        unreliable_seen += static_cast<int>(from_client.unreliable.size());
        server.update(now);
        client.poll(now);
        now += kTick;
    }

    // Drain whatever reliable traffic is still in flight after production stops.
    for (int t = 0; t < 500 && reliable_seen < kTicks; ++t) {
        client.update(now);
        Peer::Delivery from_client = server.poll(now);
        reliable_seen += static_cast<int>(from_client.reliable.size());
        server.update(now);
        client.poll(now);
        now += kTick;
    }

    EXPECT_EQ(reliable_seen, kTicks);
    EXPECT_LT(unreliable_seen, kTicks);
    EXPECT_GT(unreliable_seen, 0);
}

// update() flushes only when the governor's send interval has elapsed; the first
// call always flushes to seed the connection.
TEST(Peer, UpdateRespectsSendInterval) {
    std::deque<Datagram> out, in;
    net::Endpoint remote{"127.0.0.1", 2};
    FakeSocket sock(out, in, remote, 0.0, 1);
    Peer peer(sock, remote);

    EXPECT_TRUE(peer.update(0));
    EXPECT_FALSE(peer.update(1'000));
    EXPECT_FALSE(peer.update(kTick - 1));
    EXPECT_TRUE(peer.update(kTick));
    EXPECT_EQ(peer.packets_sent(), 2u);
}

// A payload larger than one fragment is split on send and rejoined on receive,
// arriving byte-for-byte identical.
TEST(Peer, MultiFragmentPacketRoundTrips) {
    std::deque<Datagram> c2s, s2c;
    net::Endpoint ca{"127.0.0.1", 1};
    net::Endpoint sa{"127.0.0.1", 2};
    FakeSocket client_sock(c2s, s2c, sa, 0.0, 1);
    FakeSocket server_sock(s2c, c2s, ca, 0.0, 2);
    Peer client(client_sock, sa);
    Peer server(server_sock, ca);

    Datagram big(3000);
    for (size_t i = 0; i < big.size(); ++i) {
        big[i] = static_cast<uint8_t>(i * 31 + 7);
    }
    client.queue_reliable(big);

    client.update(0);
    EXPECT_GE(client.fragments_sent(), 3u);  // 3000 + headers spans 3 of the 1024-byte fragments

    Peer::Delivery from_client = server.poll(0);
    ASSERT_EQ(from_client.reliable.size(), 1u);
    EXPECT_EQ(from_client.reliable[0].payload, big);
}

// Egress counters match the exact serialized size of a known single-message packet.
TEST(Peer, EgressCountersMatchSerializedSize) {
    std::deque<Datagram> out, in;
    net::Endpoint remote{"127.0.0.1", 2};
    FakeSocket sock(out, in, remote, 0.0, 1);
    Peer peer(sock, remote);

    peer.queue_reliable({1, 2, 3});
    peer.update(0);

    const size_t blob = sizeof(reliable::PacketHeader) + sizeof(uint16_t) +
                        (sizeof(uint8_t) + sizeof(reliable::MessageId) + sizeof(uint16_t) + 3);
    const size_t expected = sizeof(reliable::FragmentHeader) + blob;

    EXPECT_EQ(peer.packets_sent(), 1u);
    EXPECT_EQ(peer.fragments_sent(), 1u);
    EXPECT_EQ(peer.bytes_sent(), expected);
}
