#include "snapshot.h"

#include <cstring>
#include <map>

namespace sim
{

    constexpr size_t kEntrySize = sizeof(EntityId) + sizeof(float) + sizeof(float);

    std::vector<uint8_t> encode_keyframe(const WorldSnapshot &snap)
    {
        std::vector<uint8_t> out;
        out.reserve(1 + sizeof(uint32_t) + sizeof(uint16_t) + snap.entities.size() * kEntrySize);
        auto append = [&](const void *p, size_t n)
        {
            const uint8_t *b = static_cast<const uint8_t *>(p);
            out.insert(out.end(), b, b + n);
        };

        out.push_back(static_cast<uint8_t>(SnapshotType::Keyframe));
        append(&snap.tick, sizeof(snap.tick));
        uint16_t count = static_cast<uint16_t>(snap.entities.size());
        append(&count, sizeof(count));
        for (const EntityState &e : snap.entities)
        {
            append(&e.id, sizeof(e.id));
            append(&e.x, sizeof(e.x));
            append(&e.y, sizeof(e.y));
        }
        return out;
    }

    std::vector<uint8_t> encode_delta(const WorldSnapshot &curr, const WorldSnapshot &baseline)
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
        out.reserve(1 + 2 * sizeof(uint32_t) + 2 * sizeof(uint16_t) +
                    changed.size() * kEntrySize + removed.size() * sizeof(EntityId));
        auto append = [&](const void *p, size_t n)
        {
            const uint8_t *bytes = static_cast<const uint8_t *>(p);
            out.insert(out.end(), bytes, bytes + n);
        };

        out.push_back(static_cast<uint8_t>(SnapshotType::Delta));
        append(&curr.tick, sizeof(curr.tick));
        append(&baseline.tick, sizeof(baseline.tick));
        uint16_t changed_count = static_cast<uint16_t>(changed.size());
        append(&changed_count, sizeof(changed_count));
        for (const EntityState &e : changed)
        {
            append(&e.id, sizeof(e.id));
            append(&e.x, sizeof(e.x));
            append(&e.y, sizeof(e.y));
        }
        uint16_t removed_count = static_cast<uint16_t>(removed.size());
        append(&removed_count, sizeof(removed_count));
        for (EntityId id : removed)
        {
            append(&id, sizeof(id));
        }
        return out;
    }

    bool apply_snapshot(WorldSnapshot &base, const uint8_t *data, size_t len)
    {
        if (len < 1)
        {
            return false;
        }
        size_t off = 1;
        auto read = [&](void *dst, size_t n) -> bool
        {
            if (off + n > len)
            {
                return false;
            }
            std::memcpy(dst, data + off, n);
            off += n;
            return true;
        };

        if (data[0] == static_cast<uint8_t>(SnapshotType::Keyframe))
        {
            WorldSnapshot decoded;
            uint16_t count = 0;
            if (!read(&decoded.tick, sizeof(decoded.tick)) || !read(&count, sizeof(count)))
            {
                return false;
            }
            decoded.entities.reserve(count);
            for (uint16_t i = 0; i < count; ++i)
            {
                EntityState e;
                if (!read(&e.id, sizeof(e.id)) || !read(&e.x, sizeof(e.x)) || !read(&e.y, sizeof(e.y)))
                {
                    return false;
                }
                decoded.entities.push_back(e);
            }
            base = std::move(decoded);
            return true;
        }

        if (data[0] == static_cast<uint8_t>(SnapshotType::Delta))
        {
            uint32_t tick = 0;
            uint32_t baseline_tick = 0;
            if (!read(&tick, sizeof(tick)) || !read(&baseline_tick, sizeof(baseline_tick)))
            {
                return false;
            }
            if (base.tick != baseline_tick)
            {
                return false;  // delta is against a baseline we don't hold
            }

            uint16_t changed_count = 0;
            if (!read(&changed_count, sizeof(changed_count)))
            {
                return false;
            }
            std::vector<EntityState> changed;
            changed.reserve(changed_count);
            for (uint16_t i = 0; i < changed_count; ++i)
            {
                EntityState e;
                if (!read(&e.id, sizeof(e.id)) || !read(&e.x, sizeof(e.x)) || !read(&e.y, sizeof(e.y)))
                {
                    return false;
                }
                changed.push_back(e);
            }
            uint16_t removed_count = 0;
            if (!read(&removed_count, sizeof(removed_count)))
            {
                return false;
            }
            std::vector<EntityId> removed;
            removed.reserve(removed_count);
            for (uint16_t i = 0; i < removed_count; ++i)
            {
                EntityId id = 0;
                if (!read(&id, sizeof(id)))
                {
                    return false;
                }
                removed.push_back(id);
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
