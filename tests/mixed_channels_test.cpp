#include <cstdint>
#include <cstring>
#include <random>
#include <unordered_set>
#include <vector>

#include <gtest/gtest.h>

#include "connection.h"
#include "channel.h"

using reliable::Channel;
using reliable::ChannelDemux;
using reliable::ChannelMux;
using reliable::Connection;
using reliable::Message;
using reliable::PacketHeader;

namespace {

std::vector<uint8_t> encode(uint32_t value) {
    std::vector<uint8_t> bytes(sizeof(value));
    std::memcpy(bytes.data(), &value, sizeof(value));
    return bytes;
}

uint32_t decode(const std::vector<uint8_t>& bytes) {
    uint32_t value = 0;
    std::memcpy(&value, bytes.data(), sizeof(value));
    return value;
}

}  // namespace

// The demux splits a packet's messages by channel. Reliable is deduped (a resend
// arrives twice); unreliable is never deduped -- it's the sender's job to send
// each once.
TEST(MixedChannels, RoutesByChannelAndDedupesReliableOnly) {
    ChannelMux mux;
    mux.reliable().queue(encode(1));
    mux.reliable().queue(encode(2));
    mux.unreliable().queue(encode(100));

    std::vector<Message> packet = mux.pack(0);
    ASSERT_EQ(packet.size(), 3u);

    ChannelDemux demux;
    ChannelDemux::Delivery first = demux.route(packet);
    EXPECT_EQ(first.reliable.size(), 2u);
    EXPECT_EQ(first.unreliable.size(), 1u);

    // Same packet again (as if resent): reliable is suppressed, unreliable isn't.
    ChannelDemux::Delivery again = demux.route(packet);
    EXPECT_EQ(again.reliable.size(), 0u);
    EXPECT_EQ(again.unreliable.size(), 1u);
}

// One connection carries both streams over a 30%-lossy path. Reliable events
// must all arrive exactly once; unreliable snapshots just thin out -- no resend,
// so drops are permanent and none arrive twice.
TEST(MixedChannels, ReliableDeliveredUnreliableDroppedUnderLoss) {
    Connection sender_conn;
    Connection receiver_conn;
    ChannelMux mux;
    ChannelDemux demux;

    std::mt19937 rng(7);
    std::bernoulli_distribution drop(0.3);

    constexpr int kEvents = 200;
    std::vector<int> event_count(kEvents, 0);
    std::unordered_set<uint32_t> seen_snapshots;

    int events_queued = 0;
    int snapshots_sent = 0;
    int snapshots_delivered = 0;
    bool snapshot_dup = false;
    int guard = 0;

    while ((events_queued < kEvents || !mux.reliable().empty()) && guard++ < 100'000) {
        if (events_queued < kEvents) {
            mux.reliable().queue(encode(events_queued++));
        }
        mux.unreliable().queue(encode(snapshots_sent++));

        PacketHeader header = sender_conn.next_header();
        std::vector<Message> msgs = mux.pack(header.sequence);

        if (!drop(rng)) {
            receiver_conn.on_received(header);
            ChannelDemux::Delivery d = demux.route(msgs);
            for (const Message& m : d.reliable) {
                ++event_count[decode(m.payload)];
            }
            for (const Message& m : d.unreliable) {
                ++snapshots_delivered;
                if (!seen_snapshots.insert(decode(m.payload)).second) {
                    snapshot_dup = true;
                }
            }
            PacketHeader ack = receiver_conn.next_header();
            sender_conn.on_received(ack);
            for (int i = 0; i <= 32; ++i) {
                uint16_t seq = static_cast<uint16_t>(ack.ack - i);
                if (sender_conn.is_acked(seq)) {
                    mux.on_acked(seq);
                }
            }
        }
    }

    EXPECT_TRUE(mux.reliable().empty());
    for (int i = 0; i < kEvents; ++i) {
        EXPECT_EQ(event_count[i], 1) << "event " << i;
    }
    EXPECT_LT(snapshots_delivered, snapshots_sent) << "expected some snapshots dropped";
    EXPECT_FALSE(snapshot_dup) << "an unreliable snapshot was delivered more than once";
}
