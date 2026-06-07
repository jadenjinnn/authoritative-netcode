#include "snapshot_history.h"

namespace sim
{

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

    std::vector<uint8_t> SnapshotHistory::encode_for(uint32_t baseline_tick, uint32_t current_tick) const
    {
        const WorldSnapshot *curr = get(current_tick);
        if (curr == nullptr)
        {
            return {};
        }
        const WorldSnapshot *base = get(baseline_tick);
        if (base == nullptr)
        {
            return encode_keyframe(*curr);
        }
        return encode_delta(*curr, *base);
    }

} // namespace sim
