#include <cstdint>
#include <random>
#include <set>

#include <gtest/gtest.h>

#include "connection.h"

using reliable::Connection;
using reliable::PacketHeader;
using reliable::sequence_greater_than;

TEST(Sequence, WraparoundComparison)
{
    EXPECT_TRUE(sequence_greater_than(1, 0));
    EXPECT_FALSE(sequence_greater_than(0, 1));

    // Across the 65535 -> 0 wrap, 0 is the newer sequence.
    EXPECT_TRUE(sequence_greater_than(0, 65535));
    EXPECT_FALSE(sequence_greater_than(65535, 0));

    EXPECT_TRUE(sequence_greater_than(20, 65530));
    EXPECT_FALSE(sequence_greater_than(65530, 20));
}

// Drive packets from a -> b with random loss; b's acks flow back to a reliably.
// Afterwards a should know exactly which of its packets b received.
TEST(Connection, AcksReportDeliveredPackets)
{
    Connection a;
    Connection b;

    std::mt19937 rng(12345);
    std::bernoulli_distribution drop(0.2);

    std::set<uint16_t> delivered;
    constexpr int kPackets = 500;

    for (int i = 0; i < kPackets; ++i)
    {
        PacketHeader to_b = a.next_header();
        if (!drop(rng))
        {
            b.on_received(to_b);
            delivered.insert(to_b.sequence);
        }
        a.on_received(b.next_header());
    }

    for (uint16_t seq = 0; seq < kPackets; ++seq)
    {
        EXPECT_EQ(a.is_acked(seq), delivered.count(seq) > 0) << "sequence " << seq;
    }
}
