#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace sim {

using EntityId = uint32_t;

// Legacy single-entity snapshot from the P0 skeleton. Kept only so the original
// client/ relic still compiles; the authoritative server now broadcasts the
// multi-entity state below.
struct Snapshot {
    uint32_t tick;
    float x;
    float y;
};

// One entity's authoritative position in a state broadcast.
struct EntityState {
    EntityId id;
    float x;
    float y;
};

struct WorldSnapshot {
    uint32_t tick = 0;
    std::vector<EntityState> entities;  // sorted by id
};

// State sync is sent as one of two tagged blobs. A keyframe is self-contained; a
// delta is meaningful only against the baseline snapshot the client already holds.
enum class SnapshotType : uint8_t { Keyframe = 0, Delta = 1 };

// Keyframe: full state, self-contained. Wire:
// [uint8 type=0][uint32 tick][uint16 count]{ [uint32 id][float x][float y] }*
std::vector<uint8_t> encode_keyframe(const WorldSnapshot& snap, float world_max);

// Delta from a baseline the client confirmed. Entities omitted from the wire are
// unchanged (carry forward). Requires both lists sorted by id. Wire:
// [uint8 type=1][uint32 tick][uint32 baseline_tick]
// [uint16 changed_count]{ [uint32 id][float x][float y] }*   (changed + added)
// [uint16 removed_count]{ [uint32 id] }*
std::vector<uint8_t> encode_delta(const WorldSnapshot& curr, const WorldSnapshot& baseline, float world_max);

// Apply a keyframe or delta onto base. A keyframe replaces it unconditionally; a
// delta applies only if base.tick equals the wire's baseline_tick, else returns
// false and leaves base untouched (also on a truncated buffer).
bool apply_snapshot(WorldSnapshot& base, const uint8_t* data, size_t len, float world_max);

}  // namespace sim
