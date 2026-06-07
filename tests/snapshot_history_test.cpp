#include <gtest/gtest.h>

#include "snapshot.h"
#include "snapshot_history.h"

using namespace sim;

TEST(SnapshotHistory, GetReturnsPushed) {
    SnapshotHistory h(8);
    h.push(1, {{1, 1.f, 1.f}});
    h.push(2, {{1, 2.f, 2.f}});

    ASSERT_NE(h.get(2), nullptr);
    EXPECT_EQ(h.get(2)->tick, 2u);
    ASSERT_EQ(h.get(2)->entities.size(), 1u);
    EXPECT_FLOAT_EQ(h.get(2)->entities[0].x, 2.f);
}

TEST(SnapshotHistory, GetSentinelAndMissing) {
    SnapshotHistory h(8);
    h.push(1, {{1, 1.f, 1.f}});
    EXPECT_EQ(h.get(0), nullptr);    // sentinel never stored
    EXPECT_EQ(h.get(99), nullptr);   // never pushed
}

TEST(SnapshotHistory, AgesOutOnceOverwritten) {
    SnapshotHistory h(4);
    for (uint32_t t = 1; t <= 6; ++t) {
        h.push(t, {{1, static_cast<float>(t), 0.f}});
    }
    // depth 4: tick 5 overwrote slot of tick 1, tick 6 overwrote tick 2.
    EXPECT_EQ(h.get(1), nullptr);
    EXPECT_EQ(h.get(2), nullptr);
    ASSERT_NE(h.get(5), nullptr);
    ASSERT_NE(h.get(6), nullptr);
    EXPECT_EQ(h.get(6)->tick, 6u);
}

TEST(SnapshotHistory, EncodeForKeyframeWhenNoBaseline) {
    SnapshotHistory h(8);
    h.push(1, {{1, 1.f, 1.f}});
    std::vector<uint8_t> bytes = h.encode_for(0, 1);  // baseline 0 -> keyframe
    ASSERT_FALSE(bytes.empty());
    EXPECT_EQ(bytes[0], static_cast<uint8_t>(SnapshotType::Keyframe));
}

TEST(SnapshotHistory, EncodeForDeltaWhenBaselinePresent) {
    SnapshotHistory h(8);
    h.push(1, {{1, 1.f, 1.f}});
    h.push(2, {{1, 2.f, 2.f}});
    std::vector<uint8_t> bytes = h.encode_for(1, 2);
    ASSERT_FALSE(bytes.empty());
    EXPECT_EQ(bytes[0], static_cast<uint8_t>(SnapshotType::Delta));
}

TEST(SnapshotHistory, EncodeForKeyframeWhenBaselineAgedOut) {
    SnapshotHistory h(4);
    for (uint32_t t = 1; t <= 6; ++t) {
        h.push(t, {{1, static_cast<float>(t), 0.f}});
    }
    std::vector<uint8_t> bytes = h.encode_for(1, 6);  // baseline 1 aged out -> keyframe
    ASSERT_FALSE(bytes.empty());
    EXPECT_EQ(bytes[0], static_cast<uint8_t>(SnapshotType::Keyframe));
}
