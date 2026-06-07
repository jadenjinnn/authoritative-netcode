#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "snapshot.h"

namespace sim {

// Rolling buffer of recent full snapshots so the server can delta any client's
// confirmed baseline against the current tick. Fixed depth: a baseline older than
// `depth` ticks has been overwritten and falls back to a keyframe. Indexed by
// tick % depth; each slot stores its own tick, which disambiguates collisions.
class SnapshotHistory {
public:
    explicit SnapshotHistory(size_t depth = 128) : ring_(depth) {}

    void push(uint32_t tick, std::vector<EntityState> entities);

    // The stored snapshot for tick, or nullptr if it never existed or aged out.
    // tick 0 is the "no baseline" sentinel and always returns nullptr.
    const WorldSnapshot* get(uint32_t tick) const;

    // Keyframe if baseline_tick is 0 (no baseline) or has aged out; otherwise a
    // delta of current_tick against baseline_tick. current_tick must be in history.
    std::vector<uint8_t> encode_for(uint32_t baseline_tick, uint32_t current_tick) const;

private:
    std::vector<WorldSnapshot> ring_;
};

}  // namespace sim
