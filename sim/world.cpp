#include "world.h"

#include <algorithm>

namespace sim
{

    namespace
    {
        constexpr float kBound = 100.0f;
        constexpr float kSpeed = 20.0f;
        constexpr float kSpawn = kBound / 2.0f;
    }

    EntityId World::add_player()
    {
        EntityId id = next_id_++;
        entities_[id] = Entity{kSpawn, kSpawn, 0.0f, 0.0f};
        return id;
    }

    void World::remove_player(EntityId id)
    {
        entities_.erase(id);
    }

    void World::apply_input(EntityId id, const Input &in)
    {
        auto it = entities_.find(id);
        if (it == entities_.end())
        {
            return;
        }
        it->second.vx = in.dx * kSpeed;
        it->second.vy = in.dy * kSpeed;
    }

    void World::step(double dt)
    {
        float d = static_cast<float>(dt);
        for (auto &entry : entities_)
        {
            Entity &e = entry.second;
            e.x = std::clamp(e.x + e.vx * d, 0.0f, kBound);
            e.y = std::clamp(e.y + e.vy * d, 0.0f, kBound);
        }
    }

    std::vector<EntityState> World::snapshot() const
    {
        std::vector<EntityState> out;
        out.reserve(entities_.size());
        for (const auto &entry : entities_)
        {
            out.push_back(EntityState{entry.first, entry.second.x, entry.second.y});
        }
        return out;
    }

} // namespace sim
