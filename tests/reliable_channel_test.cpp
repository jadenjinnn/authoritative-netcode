#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

#include <gtest/gtest.h>

#include "connection.h"
#include "reliable_channel.h"

using reliable::Connection;
using reliable::Message;
using reliable::PacketHeader;
using reliable::ReliableReceiver;
using reliable::ReliableSender;

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

// Queue messages over time and push them sender -> receiver over a 30%-lossy
// data path; acks flow back clean. Every message must arrive exactly once.
TEST(ReliableChannel, DeliversEveryMessageExactlyOnceDespiteLoss) {
    Connection sender_conn;
    Connection receiver_conn;
    ReliableSender sender;
    ReliableReceiver receiver;

    std::mt19937 rng(999);
    std::bernoulli_distribution drop(0.3);

    constexpr int kMessages = 300;
    std::vector<int> delivery_count(kMessages, 0);

    int next_to_queue = 0;
    int guard = 0;
    while ((next_to_queue < kMessages || !sender.empty()) && guard++ < 100'000) {
        if (next_to_queue < kMessages) {
            sender.queue(encode(next_to_queue++));
        }

        PacketHeader header = sender_conn.next_header();
        std::vector<Message> msgs = sender.pack(header.sequence);

        if (!drop(rng)) {
            receiver_conn.on_received(header);
            for (const Message& m : receiver.receive(msgs)) {
                ++delivery_count[decode(m.payload)];
            }
            PacketHeader ack = receiver_conn.next_header();
            sender_conn.on_received(ack);
            // The ack confirms ack.ack plus the 32 sequences behind it (the
            // bitmask); retire that window. on_acked ignores already-gone ids.
            for (int i = 0; i <= 32; ++i) {
                uint16_t seq = static_cast<uint16_t>(ack.ack - i);
                if (sender_conn.is_acked(seq)) {
                    sender.on_acked(seq);
                }
            }
        }
    }

    EXPECT_TRUE(sender.empty());
    for (int i = 0; i < kMessages; ++i) {
        EXPECT_EQ(delivery_count[i], 1) << "message " << i;
    }
}
