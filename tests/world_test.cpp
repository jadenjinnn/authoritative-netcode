#include <vector>

#include <gtest/gtest.h>

#include "input.h"
#include "quantize.h"
#include "snapshot.h"
#include "world.h"

using sim::EntityId;
using sim::EntityState;
using sim::Input;
using sim::World;

constexpr float kTol = 100.0f / ((1u << sim::kPosBits) - 1);  // one quantization step (default world)

TEST(World, AddPlayerReturnsDistinctIds) {
    World w;
    EntityId a = w.add_player();
    EntityId b = w.add_player();
    EXPECT_NE(a, b);
    EXPECT_EQ(w.size(), 2u);
    EXPECT_TRUE(w.has(a));
    EXPECT_TRUE(w.has(b));
}

TEST(World, ApplyInputMovesOnlyTargetedEntity) {
    World w;
    EntityId a = w.add_player();
    EntityId b = w.add_player();
    float ax0 = w.entity(a).x;
    float bx0 = w.entity(b).x;

    w.apply_input(a, Input{1.0f, 0.0f});  // a moves +x; b gets nothing
    w.step(0.1);

    EXPECT_GT(w.entity(a).x, ax0);        // a moved right from its own spawn
    EXPECT_FLOAT_EQ(w.entity(b).x, bx0);  // b did not move
}

TEST(World, StepIntegratesEveryEntity) {
    World w;
    EntityId a = w.add_player();
    EntityId b = w.add_player();
    w.apply_input(a, Input{1.0f, 0.0f});
    w.apply_input(b, Input{0.0f, 1.0f});
    float ax0 = w.entity(a).x;
    float by0 = w.entity(b).y;

    w.step(0.1);

    EXPECT_GT(w.entity(a).x, ax0);
    EXPECT_GT(w.entity(b).y, by0);
}

TEST(World, PositionStaysWithinBounds) {
    World w;
    EntityId a = w.add_player();
    w.apply_input(a, Input{1.0f, 0.0f});
    for (int i = 0; i < 1000; ++i) {
        w.step(0.1);  // push hard against the +x bound
    }
    EXPECT_LE(w.entity(a).x, 100.0f);
    EXPECT_GE(w.entity(a).x, 0.0f);
}

TEST(World, RemovePlayerDropsItFromState) {
    World w;
    EntityId a = w.add_player();
    EntityId b = w.add_player();
    w.remove_player(a);

    EXPECT_FALSE(w.has(a));
    EXPECT_EQ(w.size(), 1u);
    std::vector<EntityState> state = w.snapshot();
    ASSERT_EQ(state.size(), 1u);
    EXPECT_EQ(state[0].id, b);
}

TEST(World, FullStateSnapshotRoundTrips) {
    World w;
    for (int i = 0; i < 5; ++i) {
        EntityId id = w.add_player();
        w.apply_input(id, Input{static_cast<float>(i) * 0.1f, -0.2f});
    }
    w.step(0.05);

    std::vector<EntityState> state = w.snapshot();
    std::vector<uint8_t> bytes = sim::encode_keyframe(sim::WorldSnapshot{42, state}, 100.0f);

    sim::WorldSnapshot decoded;
    ASSERT_TRUE(sim::apply_snapshot(decoded, bytes.data(), bytes.size(), 100.0f));
    EXPECT_EQ(decoded.tick, 42u);
    ASSERT_EQ(decoded.entities.size(), state.size());
    for (size_t i = 0; i < state.size(); ++i) {
        EXPECT_EQ(decoded.entities[i].id, state[i].id);
        EXPECT_NEAR(decoded.entities[i].x, state[i].x, kTol);
        EXPECT_NEAR(decoded.entities[i].y, state[i].y, kTol);
    }
}
