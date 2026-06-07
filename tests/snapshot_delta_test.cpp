#include <gtest/gtest.h>

#include "snapshot.h"

using namespace sim;

namespace
{
    WorldSnapshot snap(uint32_t tick, std::vector<EntityState> entities)
    {
        return WorldSnapshot{tick, std::move(entities)};
    }
}

TEST(SnapshotDelta, KeyframeRoundTripEmpty) {
    WorldSnapshot s = snap(7, {});
    std::vector<uint8_t> bytes = encode_keyframe(s);

    WorldSnapshot base;
    ASSERT_TRUE(apply_snapshot(base, bytes.data(), bytes.size()));
    EXPECT_EQ(base.tick, 7u);
    EXPECT_TRUE(base.entities.empty());
}

TEST(SnapshotDelta, KeyframeRoundTripReplacesPriorState) {
    WorldSnapshot s = snap(10, {{1, 1.f, 2.f}, {2, 3.f, 4.f}, {5, 9.f, -9.f}});
    std::vector<uint8_t> bytes = encode_keyframe(s);

    WorldSnapshot base = snap(99, {{42, 0.f, 0.f}});  // pre-existing, must be replaced
    ASSERT_TRUE(apply_snapshot(base, bytes.data(), bytes.size()));
    EXPECT_EQ(base.tick, 10u);
    ASSERT_EQ(base.entities.size(), 3u);
    EXPECT_EQ(base.entities[0].id, 1u);
    EXPECT_EQ(base.entities[2].id, 5u);
    EXPECT_FLOAT_EQ(base.entities[2].x, 9.f);
}

TEST(SnapshotDelta, DeltaNoChange) {
    WorldSnapshot baseline = snap(10, {{1, 1.f, 2.f}, {2, 3.f, 4.f}});
    WorldSnapshot curr = snap(11, {{1, 1.f, 2.f}, {2, 3.f, 4.f}});
    std::vector<uint8_t> bytes = encode_delta(curr, baseline);

    WorldSnapshot held = baseline;
    ASSERT_TRUE(apply_snapshot(held, bytes.data(), bytes.size()));
    EXPECT_EQ(held.tick, 11u);
    ASSERT_EQ(held.entities.size(), 2u);
    EXPECT_FLOAT_EQ(held.entities[0].x, 1.f);
    EXPECT_FLOAT_EQ(held.entities[1].y, 4.f);
}

TEST(SnapshotDelta, DeltaChangeAddRemove) {
    WorldSnapshot baseline = snap(10, {{1, 1.f, 1.f}, {2, 2.f, 2.f}, {3, 3.f, 3.f}});
    // id 1 changed, id 2 removed, id 3 unchanged, id 4 added
    WorldSnapshot curr = snap(12, {{1, 1.5f, 1.f}, {3, 3.f, 3.f}, {4, 4.f, 4.f}});
    std::vector<uint8_t> bytes = encode_delta(curr, baseline);

    WorldSnapshot held = baseline;
    ASSERT_TRUE(apply_snapshot(held, bytes.data(), bytes.size()));
    EXPECT_EQ(held.tick, 12u);
    ASSERT_EQ(held.entities.size(), 3u);
    EXPECT_EQ(held.entities[0].id, 1u);
    EXPECT_FLOAT_EQ(held.entities[0].x, 1.5f);
    EXPECT_EQ(held.entities[1].id, 3u);
    EXPECT_EQ(held.entities[2].id, 4u);
    EXPECT_FLOAT_EQ(held.entities[2].x, 4.f);
}

TEST(SnapshotDelta, DeltaRejectedOnBaselineMismatch) {
    WorldSnapshot baseline = snap(10, {{1, 1.f, 1.f}});
    WorldSnapshot curr = snap(11, {{1, 2.f, 2.f}});
    std::vector<uint8_t> bytes = encode_delta(curr, baseline);

    WorldSnapshot held = snap(9, {{1, 1.f, 1.f}});  // holds the wrong baseline tick
    EXPECT_FALSE(apply_snapshot(held, bytes.data(), bytes.size()));
    EXPECT_EQ(held.tick, 9u);  // untouched
    EXPECT_FLOAT_EQ(held.entities[0].x, 1.f);
}

TEST(SnapshotDelta, RejectsTruncatedBuffer) {
    WorldSnapshot s = snap(10, {{1, 1.f, 2.f}, {2, 3.f, 4.f}});
    std::vector<uint8_t> bytes = encode_keyframe(s);

    WorldSnapshot base;
    EXPECT_FALSE(apply_snapshot(base, bytes.data(), bytes.size() - 2));
    EXPECT_EQ(base.tick, 0u);  // untouched
    EXPECT_TRUE(base.entities.empty());
}
