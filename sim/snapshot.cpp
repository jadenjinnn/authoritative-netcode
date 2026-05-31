#include "snapshot.h"

#include <cstring>

namespace sim
{

    std::vector<uint8_t> encode_snapshot(uint32_t tick, const std::vector<EntityState> &entities)
    {
        std::vector<uint8_t> out;
        auto append = [&](const void *p, size_t n)
        {
            const uint8_t *b = static_cast<const uint8_t *>(p);
            out.insert(out.end(), b, b + n);
        };

        append(&tick, sizeof(tick));
        uint16_t count = static_cast<uint16_t>(entities.size());
        append(&count, sizeof(count));
        for (const EntityState &e : entities)
        {
            append(&e.id, sizeof(e.id));
            append(&e.x, sizeof(e.x));
            append(&e.y, sizeof(e.y));
        }
        return out;
    }

    std::optional<WorldSnapshot> decode_snapshot(const uint8_t *data, size_t len)
    {
        size_t off = 0;
        if (len < sizeof(uint32_t) + sizeof(uint16_t))
        {
            return std::nullopt;
        }

        WorldSnapshot snap;
        std::memcpy(&snap.tick, data, sizeof(snap.tick));
        off += sizeof(snap.tick);
        uint16_t count = 0;
        std::memcpy(&count, data + off, sizeof(count));
        off += sizeof(count);

        constexpr size_t kEntrySize = sizeof(EntityId) + sizeof(float) + sizeof(float);
        if (off + static_cast<size_t>(count) * kEntrySize > len)
        {
            return std::nullopt;
        }

        snap.entities.reserve(count);
        for (uint16_t i = 0; i < count; ++i)
        {
            EntityState e;
            std::memcpy(&e.id, data + off, sizeof(e.id));
            off += sizeof(e.id);
            std::memcpy(&e.x, data + off, sizeof(e.x));
            off += sizeof(e.x);
            std::memcpy(&e.y, data + off, sizeof(e.y));
            off += sizeof(e.y);
            snap.entities.push_back(e);
        }
        return snap;
    }

} // namespace sim
