#include "snapshot.h"

#include <map>

#include "bitstream.h"
#include "quantize.h"

namespace sim
{

    std::vector<uint8_t> encode_keyframe(const WorldSnapshot &snap, float world_max)
    {
        std::vector<uint8_t> out;
        out.push_back(static_cast<uint8_t>(SnapshotType::Keyframe));

        BitWriter w;
        w.write_bits(snap.tick, 32);
        w.write_bits(static_cast<uint32_t>(snap.entities.size()), 16);
        for (const EntityState &e : snap.entities)
        {
            w.write_bits(e.id, 16);
            w.write_bits(quantize(e.x, world_max), kPosBits);
            w.write_bits(quantize(e.y, world_max), kPosBits);
        }

        std::vector<uint8_t> bits = w.take();
        out.insert(out.end(), bits.begin(), bits.end());
        return out;
    }

    std::vector<uint8_t> encode_delta(const WorldSnapshot &curr, const WorldSnapshot &baseline, float world_max)
    {
        const std::vector<EntityState> &c = curr.entities;
        const std::vector<EntityState> &b = baseline.entities;

        std::vector<EntityState> changed;  // changed + added
        std::vector<EntityId> removed;
        size_t i = 0;
        size_t j = 0;
        while (i < c.size() && j < b.size())
        {
            if (c[i].id < b[j].id)
            {
                changed.push_back(c[i]);  // present now, absent from baseline -> added
                ++i;
            }
            else if (c[i].id > b[j].id)
            {
                removed.push_back(b[j].id);
                ++j;
            }
            else
            {
                if (c[i].x != b[j].x || c[i].y != b[j].y)
                {
                    changed.push_back(c[i]);
                }
                ++i;
                ++j;
            }
        }
        for (; i < c.size(); ++i)
        {
            changed.push_back(c[i]);
        }
        for (; j < b.size(); ++j)
        {
            removed.push_back(b[j].id);
        }

        std::vector<uint8_t> out;
        out.push_back(static_cast<uint8_t>(SnapshotType::Delta));

        BitWriter w;
        w.write_bits(curr.tick, 32);
        w.write_bits(baseline.tick, 32);
        w.write_bits(static_cast<uint32_t>(changed.size()), 16);
        for (const EntityState &e : changed)
        {
            w.write_bits(e.id, 16);
            w.write_bits(quantize(e.x, world_max), kPosBits);
            w.write_bits(quantize(e.y, world_max), kPosBits);
        }
        w.write_bits(static_cast<uint32_t>(removed.size()), 16);
        for (EntityId id : removed)
        {
            w.write_bits(id, 16);
        }

        std::vector<uint8_t> bits = w.take();
        out.insert(out.end(), bits.begin(), bits.end());
        return out;
    }

    bool apply_snapshot(WorldSnapshot &base, const uint8_t *data, size_t len, float world_max)
    {
        if (len < 1)
        {
            return false;
        }
        uint8_t type = data[0];
        BitReader r(data + 1, len - 1);

        if (type == static_cast<uint8_t>(SnapshotType::Keyframe))
        {
            WorldSnapshot decoded;
            decoded.tick = r.read_bits(32);
            uint32_t count = r.read_bits(16);
            decoded.entities.reserve(count);
            for (uint32_t k = 0; k < count; ++k)
            {
                EntityState e;
                e.id = r.read_bits(16);
                e.x = dequantize(r.read_bits(kPosBits), world_max);
                e.y = dequantize(r.read_bits(kPosBits), world_max);
                decoded.entities.push_back(e);
            }
            if (!r.ok())
            {
                return false;
            }
            base = std::move(decoded);
            return true;
        }

        if (type == static_cast<uint8_t>(SnapshotType::Delta))
        {
            uint32_t tick = r.read_bits(32);
            uint32_t baseline_tick = r.read_bits(32);
            if (!r.ok())
            {
                return false;
            }
            if (base.tick != baseline_tick)
            {
                return false;  // delta is against a baseline we don't hold
            }

            uint32_t changed_count = r.read_bits(16);
            std::vector<EntityState> changed;
            changed.reserve(changed_count);
            for (uint32_t k = 0; k < changed_count; ++k)
            {
                EntityState e;
                e.id = r.read_bits(16);
                e.x = dequantize(r.read_bits(kPosBits), world_max);
                e.y = dequantize(r.read_bits(kPosBits), world_max);
                changed.push_back(e);
            }
            uint32_t removed_count = r.read_bits(16);
            std::vector<EntityId> removed;
            removed.reserve(removed_count);
            for (uint32_t k = 0; k < removed_count; ++k)
            {
                removed.push_back(r.read_bits(16));
            }
            if (!r.ok())
            {
                return false;
            }

            // Merge onto the held baseline via a map: keeps the id-sorted invariant
            // the encoder relies on. This is the client path, not the server hot loop.
            std::map<EntityId, EntityState> merged;
            for (const EntityState &e : base.entities)
            {
                merged[e.id] = e;
            }
            for (EntityId id : removed)
            {
                merged.erase(id);
            }
            for (const EntityState &e : changed)
            {
                merged[e.id] = e;
            }
            base.entities.clear();
            base.entities.reserve(merged.size());
            for (const auto &kv : merged)
            {
                base.entities.push_back(kv.second);
            }
            base.tick = tick;
            return true;
        }

        return false;
    }

} // namespace sim
