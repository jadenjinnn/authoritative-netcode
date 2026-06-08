#include "snapshot_history.h"

namespace sim
{

    namespace
    {
        // Entities within `radius` of (cx, cy), preserving id-sorted order. The center
        // entity (the viewer) is at distance 0, so it is always kept.
        std::vector<EntityState> filter_aoi(const std::vector<EntityState> &entities,
                                            float cx, float cy, float radius)
        {
            float r2 = radius * radius;
            std::vector<EntityState> out;
            for (const EntityState &e : entities)
            {
                float dx = e.x - cx;
                float dy = e.y - cy;
                if (dx * dx + dy * dy <= r2)
                {
                    out.push_back(e);
                }
            }
            return out;
        }

        const EntityState *find_entity(const WorldSnapshot &snap, EntityId id)
        {
            for (const EntityState &e : snap.entities)
            {
                if (e.id == id)
                {
                    return &e;
                }
            }
            return nullptr;
        }
    }

    void SnapshotHistory::push(uint32_t tick, std::vector<EntityState> entities)
    {
        WorldSnapshot &slot = ring_[tick % ring_.size()];
        slot.tick = tick;
        slot.entities = std::move(entities);
    }

    const WorldSnapshot *SnapshotHistory::get(uint32_t tick) const
    {
        if (tick == 0)
        {
            return nullptr;
        }
        const WorldSnapshot &slot = ring_[tick % ring_.size()];
        if (slot.tick != tick)
        {
            return nullptr;
        }
        return &slot;
    }

    std::vector<uint8_t> SnapshotHistory::encode_for(EntityId viewer, float radius,
                                                     uint32_t baseline_tick, uint32_t current_tick) const
    {
        const WorldSnapshot *curr = get(current_tick);
        if (curr == nullptr)
        {
            return {};
        }
        const EntityState *center_now = find_entity(*curr, viewer);
        if (center_now == nullptr)
        {
            return {};  // viewer must exist in the current snapshot
        }

        WorldSnapshot current_view;
        current_view.tick = current_tick;
        current_view.entities = filter_aoi(curr->entities, center_now->x, center_now->y, radius);

        const WorldSnapshot *base = get(baseline_tick);
        if (base != nullptr)
        {
            const EntityState *center_then = find_entity(*base, viewer);
            if (center_then != nullptr)
            {
                // Center the baseline filter on the viewer's position AT the baseline tick,
                // so the recomputed view matches what the client actually holds.
                WorldSnapshot baseline_view;
                baseline_view.tick = baseline_tick;
                baseline_view.entities = filter_aoi(base->entities, center_then->x, center_then->y, radius);
                return encode_delta(current_view, baseline_view, world_max_);
            }
        }
        return encode_keyframe(current_view, world_max_);
    }

    size_t SnapshotHistory::aoi_count(EntityId viewer, float radius, uint32_t tick) const
    {
        const WorldSnapshot *snap = get(tick);
        if (snap == nullptr)
        {
            return 0;
        }
        const EntityState *center = find_entity(*snap, viewer);
        if (center == nullptr)
        {
            return 0;
        }
        return filter_aoi(snap->entities, center->x, center->y, radius).size();
    }

} // namespace sim
