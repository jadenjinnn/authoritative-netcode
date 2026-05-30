#include <cstdint>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include "fragment.h"

using reliable::fragment;
using reliable::kMaxFragmentSize;
using reliable::Reassembler;

namespace {

std::vector<uint8_t> make_payload(size_t n) {
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) {
        v[i] = static_cast<uint8_t>(i * 31 + 7);
    }
    return v;
}

}  // namespace

TEST(Fragment, SmallPacketIsOneFragmentAndRoundTrips) {
    std::vector<uint8_t> payload = make_payload(200);
    std::vector<std::vector<uint8_t>> frags = fragment(5, payload.data(), payload.size());
    ASSERT_EQ(frags.size(), 1u);

    Reassembler r;
    std::optional<std::vector<uint8_t>> packet = r.reassemble(frags[0].data(), frags[0].size());
    ASSERT_TRUE(packet.has_value());
    EXPECT_EQ(*packet, payload);
}

TEST(Fragment, LargePacketSplitsAndReassembles) {
    std::vector<uint8_t> payload = make_payload(5000);
    std::vector<std::vector<uint8_t>> frags = fragment(9, payload.data(), payload.size());
    ASSERT_EQ(frags.size(), (5000 + kMaxFragmentSize - 1) / kMaxFragmentSize);

    Reassembler r;
    std::optional<std::vector<uint8_t>> packet;
    for (const std::vector<uint8_t>& f : frags) {
        packet = r.reassemble(f.data(), f.size());
    }
    ASSERT_TRUE(packet.has_value());
    EXPECT_EQ(*packet, payload);
}

TEST(Fragment, ReassemblesOutOfOrder) {
    std::vector<uint8_t> payload = make_payload(5000);
    std::vector<std::vector<uint8_t>> frags = fragment(9, payload.data(), payload.size());

    Reassembler r;
    std::optional<std::vector<uint8_t>> packet;
    for (auto it = frags.rbegin(); it != frags.rend(); ++it) {
        packet = r.reassemble(it->data(), it->size());
    }
    ASSERT_TRUE(packet.has_value());
    EXPECT_EQ(*packet, payload);
}

TEST(Fragment, IncompleteGroupNeverDelivers) {
    std::vector<uint8_t> payload = make_payload(5000);
    std::vector<std::vector<uint8_t>> frags = fragment(9, payload.data(), payload.size());

    Reassembler r;
    for (size_t i = 0; i + 1 < frags.size(); ++i) {
        EXPECT_FALSE(r.reassemble(frags[i].data(), frags[i].size()).has_value());
    }
}

TEST(Fragment, DuplicateFragmentDoesNotDoubleCount) {
    std::vector<uint8_t> payload = make_payload(3000);
    std::vector<std::vector<uint8_t>> frags = fragment(9, payload.data(), payload.size());
    ASSERT_EQ(frags.size(), 3u);

    Reassembler r;
    EXPECT_FALSE(r.reassemble(frags[0].data(), frags[0].size()).has_value());
    // A duplicate of fragment 0 must not advance completion.
    EXPECT_FALSE(r.reassemble(frags[0].data(), frags[0].size()).has_value());
    EXPECT_FALSE(r.reassemble(frags[1].data(), frags[1].size()).has_value());
    std::optional<std::vector<uint8_t>> packet = r.reassemble(frags[2].data(), frags[2].size());
    ASSERT_TRUE(packet.has_value());
    EXPECT_EQ(*packet, payload);
}

TEST(Fragment, IncompleteGroupsStayBounded) {
    std::vector<uint8_t> payload = make_payload(3000);

    Reassembler r;
    for (int seq = 0; seq < 100; ++seq) {
        std::vector<std::vector<uint8_t>> frags =
            fragment(static_cast<uint16_t>(seq), payload.data(), payload.size());
        // Feed only the first fragment of each, so every group stays incomplete.
        r.reassemble(frags[0].data(), frags[0].size());
    }
    EXPECT_LE(r.tracked_groups(), Reassembler::kMaxGroups);
}
