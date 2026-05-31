#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace sim {

using EntityId = uint32_t;

// Legacy single-entity snapshot from the P0 skeleton. Kept only so the original
// client/ relic still compiles; the authoritative server now broadcasts the
// multi-entity full state below.
struct Snapshot {
    uint32_t tick;
    float x;
    float y;
};

// One entity's authoritative position in a full-state broadcast.
struct EntityState {
    EntityId id;
    float x;
    float y;
};

// Naive full state: every entity, every tick, to every client. This is the P2
// baseline P3 (delta/quantization/AOI) exists to beat.
// Wire: [uint32 tick][uint16 count]{ [uint32 id][float x][float y] }*
std::vector<uint8_t> encode_snapshot(uint32_t tick, const std::vector<EntityState>& entities);

struct WorldSnapshot {
    uint32_t tick = 0;
    std::vector<EntityState> entities;
};

// nullopt on a truncated/malformed buffer.
std::optional<WorldSnapshot> decode_snapshot(const uint8_t* data, size_t len);

}  // namespace sim
