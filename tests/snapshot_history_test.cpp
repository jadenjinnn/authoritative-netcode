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
    std::vector<uint8_t> bytes = h.encode_for(1, 1e9f, 0, 1);  // baseline 0 -> keyframe
    ASSERT_FALSE(bytes.empty());
    EXPECT_EQ(bytes[0], static_cast<uint8_t>(SnapshotType::Keyframe));
}

TEST(SnapshotHistory, EncodeForDeltaWhenBaselinePresent) {
    SnapshotHistory h(8);
    h.push(1, {{1, 1.f, 1.f}});
    h.push(2, {{1, 2.f, 2.f}});
    std::vector<uint8_t> bytes = h.encode_for(1, 1e9f, 1, 2);
    ASSERT_FALSE(bytes.empty());
    EXPECT_EQ(bytes[0], static_cast<uint8_t>(SnapshotType::Delta));
}

TEST(SnapshotHistory, EncodeForKeyframeWhenBaselineAgedOut) {
    SnapshotHistory h(4);
    for (uint32_t t = 1; t <= 6; ++t) {
        h.push(t, {{1, static_cast<float>(t), 0.f}});
    }
    std::vector<uint8_t> bytes = h.encode_for(1, 1e9f, 1, 6);  // baseline 1 aged out -> keyframe
    ASSERT_FALSE(bytes.empty());
    EXPECT_EQ(bytes[0], static_cast<uint8_t>(SnapshotType::Keyframe));
}

TEST(SnapshotHistory, AoiFiltersToRadius) {
    SnapshotHistory h(8);
    // viewer 1 at (50,50); 2 near at (55,50) [dist 5]; 3 far at (90,90)
    h.push(1, {{1, 50.f, 50.f}, {2, 55.f, 50.f}, {3, 90.f, 90.f}});

    std::vector<uint8_t> bytes = h.encode_for(1, 10.0f, 0, 1);  // R=10 keyframe
    WorldSnapshot view;
    ASSERT_TRUE(apply_snapshot(view, bytes.data(), bytes.size(), 100.0f));
    ASSERT_EQ(view.entities.size(), 2u);  // self + near, not far
    EXPECT_EQ(view.entities[0].id, 1u);
    EXPECT_EQ(view.entities[1].id, 2u);
}

TEST(SnapshotHistory, AoiViewerAlwaysIncluded) {
    SnapshotHistory h(8);
    h.push(1, {{1, 0.f, 0.f}, {2, 99.f, 99.f}});  // viewer alone in a corner

    std::vector<uint8_t> bytes = h.encode_for(1, 5.0f, 0, 1);
    WorldSnapshot view;
    ASSERT_TRUE(apply_snapshot(view, bytes.data(), bytes.size(), 100.0f));
    ASSERT_EQ(view.entities.size(), 1u);
    EXPECT_EQ(view.entities[0].id, 1u);  // sees only itself
}

TEST(SnapshotHistory, AoiEntityEntersAndLeavesAcrossDelta) {
    SnapshotHistory h(8);
    // R=15, viewer fixed at (50,50). tick1: 3 near (55,50) in, 2 far (90,50) out.
    h.push(1, {{1, 50.f, 50.f}, {2, 90.f, 50.f}, {3, 55.f, 50.f}});
    // tick2: 2 -> (60,50) ENTERS, 3 -> (80,50) LEAVES.
    h.push(2, {{1, 50.f, 50.f}, {2, 60.f, 50.f}, {3, 80.f, 50.f}});

    std::vector<uint8_t> base = h.encode_for(1, 15.0f, 0, 1);  // keyframe view {1,3}
    WorldSnapshot held;
    ASSERT_TRUE(apply_snapshot(held, base.data(), base.size(), 100.0f));
    ASSERT_EQ(held.entities.size(), 2u);

    std::vector<uint8_t> delta = h.encode_for(1, 15.0f, 1, 2);
    ASSERT_TRUE(apply_snapshot(held, delta.data(), delta.size(), 100.0f));
    ASSERT_EQ(held.entities.size(), 2u);  // {1,2}: 2 entered, 3 left
    EXPECT_EQ(held.entities[0].id, 1u);
    EXPECT_EQ(held.entities[1].id, 2u);
}

TEST(SnapshotHistory, AoiMovingViewerShiftsWindow) {
    SnapshotHistory h(8);
    // R=15, entity 2 fixed at (70,50). Viewer moves (50,50)->(60,50): 2 goes 20 -> 10.
    h.push(1, {{1, 50.f, 50.f}, {2, 70.f, 50.f}});  // dist 20: out
    h.push(2, {{1, 60.f, 50.f}, {2, 70.f, 50.f}});  // dist 10: in

    std::vector<uint8_t> base = h.encode_for(1, 15.0f, 0, 1);  // view {1}
    WorldSnapshot held;
    ASSERT_TRUE(apply_snapshot(held, base.data(), base.size(), 100.0f));
    ASSERT_EQ(held.entities.size(), 1u);

    // The delta must center the baseline filter on the viewer's tick-1 position (50,50),
    // not its current (60,50); otherwise 2 looks already-present and the add is missed.
    std::vector<uint8_t> delta = h.encode_for(1, 15.0f, 1, 2);
    ASSERT_TRUE(apply_snapshot(held, delta.data(), delta.size(), 100.0f));
    ASSERT_EQ(held.entities.size(), 2u);  // 2 entered the moved window
    EXPECT_EQ(held.entities[1].id, 2u);
}
