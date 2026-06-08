#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "quantize.h"
#include "snapshot.h"

namespace sim {

// Rolling buffer of recent full snapshots so the server can delta any client's
// confirmed baseline against the current tick. Fixed depth: a baseline older than
// `depth` ticks has been overwritten and falls back to a keyframe. Indexed by
// tick % depth; each slot stores its own tick, which disambiguates collisions.
class SnapshotHistory {
public:
    explicit SnapshotHistory(size_t depth = 128, float world_max = kDefaultWorldMax)
        : ring_(depth), world_max_(world_max) {}

    void push(uint32_t tick, std::vector<EntityState> entities);

    // The stored snapshot for tick, or nullptr if it never existed or aged out.
    // tick 0 is the "no baseline" sentinel and always returns nullptr.
    const WorldSnapshot* get(uint32_t tick) const;

    // AOI-filtered keyframe/delta for one viewer: only entities within `radius` of the
    // viewer's own position (centered on its position at each tick; the viewer always
    // sees itself). Keyframe if baseline is 0 / aged out / the viewer is absent there.
    std::vector<uint8_t> encode_for(EntityId viewer, float radius,
                                    uint32_t baseline_tick, uint32_t current_tick) const;

    // Entities within `radius` of the viewer at `tick`, including the viewer (avg-AOI metric).
    size_t aoi_count(EntityId viewer, float radius, uint32_t tick) const;

private:
    std::vector<WorldSnapshot> ring_;
    float world_max_;
};

}  // namespace sim
