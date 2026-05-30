#include <cstddef>
#include <optional>

#include <gtest/gtest.h>

#include "sim_socket.h"

namespace {

// Stand-in inner socket: counts the packets SimSocket chose to forward.
class CountingSocket : public net::ISocket {
public:
    size_t send_to(const void*, size_t len, const net::Endpoint&) override {
        ++forwarded;
        return len;
    }
    std::optional<net::RecvResult> try_recv_from(void*, size_t) override {
        return std::nullopt;
    }
    net::Endpoint local_endpoint() const override { return {}; }

    int forwarded = 0;
};

}  // namespace

TEST(SimSocket, DropsAtConfiguredRate) {
    CountingSocket inner;
    net::SimSocket sim(inner, {0.3, 42});

    constexpr int kSends = 100000;
    char byte = 0;
    net::Endpoint dest;
    for (int i = 0; i < kSends; ++i) {
        sim.send_to(&byte, 1, dest);
    }

    EXPECT_EQ(sim.sent(), static_cast<uint64_t>(kSends));
    // Every send is either dropped or forwarded, never both.
    EXPECT_EQ(sim.dropped() + inner.forwarded, kSends);

    double observed = static_cast<double>(sim.dropped()) / kSends;
    EXPECT_NEAR(observed, 0.3, 0.01);
}

TEST(SimSocket, ZeroLossForwardsEverything) {
    CountingSocket inner;
    net::SimSocket sim(inner, {0.0, 1});

    char byte = 0;
    net::Endpoint dest;
    for (int i = 0; i < 1000; ++i) {
        sim.send_to(&byte, 1, dest);
    }

    EXPECT_EQ(sim.dropped(), 0u);
    EXPECT_EQ(inner.forwarded, 1000);
}

TEST(SimSocket, ZeroDelayForwardsWithoutPump) {
    CountingSocket inner;
    net::SimSocket sim(inner, {0.0, 0});

    char byte = 0;
    net::Endpoint dest;
    sim.send_to(&byte, 1, dest);
    EXPECT_EQ(inner.forwarded, 1);  // immediate, no pump needed
}

TEST(SimSocket, LatencyHoldsUntilReleaseTime) {
    CountingSocket inner;
    uint64_t clock = 0;
    net::SimParams params;
    params.delay_us = 1000;
    net::SimSocket sim(inner, params, [&clock] { return clock; });

    char byte = 0;
    net::Endpoint dest;
    sim.send_to(&byte, 1, dest);   // queued at t=0, release t=1000
    EXPECT_EQ(inner.forwarded, 0);

    clock = 999;
    sim.pump();
    EXPECT_EQ(inner.forwarded, 0);  // still held

    clock = 1000;
    sim.pump();
    EXPECT_EQ(inner.forwarded, 1);  // released at its deadline
}
