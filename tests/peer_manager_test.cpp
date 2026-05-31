#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <optional>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "endpoint.h"
#include "peer.h"
#include "peer_manager.h"
#include "socket.h"

using reliable::Peer;
using reliable::PeerManager;

namespace {

using Datagram = std::vector<uint8_t>;

// A shared in-memory network: send_to delivers to the destination's mailbox tagged
// with the sender's endpoint, so the server side sees the true source -- which is
// exactly what PeerManager routes on.
struct Net {
    std::map<net::Endpoint, std::deque<std::pair<net::Endpoint, Datagram>>> mailboxes;
};

class NetSocket : public net::ISocket {
public:
    NetSocket(Net& net, net::Endpoint me) : net_(net), me_(std::move(me)) {}

    size_t send_to(const void* data, size_t len, const net::Endpoint& dest) override {
        const uint8_t* b = static_cast<const uint8_t*>(data);
        net_.mailboxes[dest].emplace_back(me_, Datagram(b, b + len));
        return len;
    }

    std::optional<net::RecvResult> try_recv_from(void* buf, size_t cap) override {
        auto& mb = net_.mailboxes[me_];
        if (mb.empty()) {
            return std::nullopt;
        }
        std::pair<net::Endpoint, Datagram> front = std::move(mb.front());
        mb.pop_front();
        size_t n = std::min(cap, front.second.size());
        std::memcpy(buf, front.second.data(), n);
        return net::RecvResult{n, front.first};
    }

    net::Endpoint local_endpoint() const override { return me_; }

private:
    Net& net_;
    net::Endpoint me_;
};

Datagram encode(uint32_t value) {
    Datagram bytes(sizeof(value));
    std::memcpy(bytes.data(), &value, sizeof(value));
    return bytes;
}

uint32_t decode(const Datagram& bytes) {
    uint32_t value = 0;
    std::memcpy(&value, bytes.data(), sizeof(value));
    return value;
}

const net::Endpoint kServer{"127.0.0.1", 9999};
const net::Endpoint kClientA{"127.0.0.1", 1001};
const net::Endpoint kClientB{"127.0.0.1", 2002};

}  // namespace

// Repeated contact from one endpoint yields exactly one Peer, not one per packet.
TEST(PeerManager, FirstContactCreatesOnePeer) {
    Net net;
    NetSocket server_sock(net, kServer);
    NetSocket a_sock(net, kClientA);
    PeerManager mgr(server_sock);
    Peer a(a_sock, kServer);

    a.queue_reliable(encode(1));
    a.update(0);
    mgr.poll(0);
    a.update(33'333);
    mgr.poll(33'333);

    EXPECT_EQ(mgr.size(), 1u);
    EXPECT_TRUE(mgr.has(kClientA));
}

// Each client's messages surface on that client's Peer, keyed by source endpoint.
TEST(PeerManager, RoutesDatagramsToOwningPeerBySource) {
    Net net;
    NetSocket server_sock(net, kServer);
    NetSocket a_sock(net, kClientA);
    NetSocket b_sock(net, kClientB);
    PeerManager mgr(server_sock);
    Peer a(a_sock, kServer);
    Peer b(b_sock, kServer);

    a.queue_reliable(encode(111));
    b.queue_reliable(encode(222));
    a.update(0);
    b.update(0);

    std::vector<PeerManager::Incoming> in = mgr.poll(0);
    ASSERT_EQ(in.size(), 2u);

    std::map<net::Endpoint, uint32_t> got;
    for (const PeerManager::Incoming& msg : in) {
        EXPECT_TRUE(msg.is_new);
        ASSERT_EQ(msg.delivery.reliable.size(), 1u);
        got[msg.from] = decode(msg.delivery.reliable[0].payload);
    }
    EXPECT_EQ(got[kClientA], 111u);
    EXPECT_EQ(got[kClientB], 222u);
}

// Two distinct endpoints produce two independent Peers; is_new is true only once.
TEST(PeerManager, TwoEndpointsTwoIndependentPeers) {
    Net net;
    NetSocket server_sock(net, kServer);
    NetSocket a_sock(net, kClientA);
    NetSocket b_sock(net, kClientB);
    PeerManager mgr(server_sock);
    Peer a(a_sock, kServer);
    Peer b(b_sock, kServer);

    a.queue_unreliable(encode(1));
    a.update(0);
    mgr.poll(0);
    EXPECT_EQ(mgr.size(), 1u);

    b.queue_unreliable(encode(2));
    b.update(0);
    std::vector<PeerManager::Incoming> in = mgr.poll(0);
    ASSERT_EQ(in.size(), 1u);
    EXPECT_EQ(in[0].from, kClientB);
    EXPECT_TRUE(in[0].is_new);
    EXPECT_EQ(mgr.size(), 2u);
}

// Server egress reported by the manager is the sum over its peers.
TEST(PeerManager, EgressTotalsSumAcrossPeers) {
    Net net;
    NetSocket server_sock(net, kServer);
    NetSocket a_sock(net, kClientA);
    NetSocket b_sock(net, kClientB);
    PeerManager mgr(server_sock);
    Peer a(a_sock, kServer);
    Peer b(b_sock, kServer);

    a.queue_unreliable(encode(1));
    b.queue_unreliable(encode(2));
    a.update(0);
    b.update(0);
    mgr.poll(0);
    ASSERT_EQ(mgr.size(), 2u);

    mgr.peer(kClientA).queue_unreliable(encode(7));
    mgr.peer(kClientB).queue_unreliable(encode(7));
    mgr.update_all(0);

    uint64_t expected = mgr.peer(kClientA).bytes_sent() + mgr.peer(kClientB).bytes_sent();
    EXPECT_GT(mgr.total_bytes_sent(), 0u);
    EXPECT_EQ(mgr.total_bytes_sent(), expected);
}
