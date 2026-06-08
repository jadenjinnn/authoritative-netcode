#pragma once

#include <cstdint>
#include <map>
#include <random>
#include <vector>

#include "input.h"
#include "snapshot.h"

namespace sim {

struct Entity {
    float x = 0.0f;
    float y = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
};

// Authoritative game state: one entity per connected client. Clients send inputs;
// the server applies them and integrates. Full state is every entity. The world is a
// [0, bound] x [0, bound] box; bound scales with population in the density sweep.
class World {
public:
    explicit World(float bound = 100.0f, uint32_t seed = 1u);

    // Spawn an entity for a newly connected client at a uniform-random point in the
    // box; returns its stable id.
    EntityId add_player();
    void remove_player(EntityId id);

    // Set the entity's velocity from the client's intended direction.
    void apply_input(EntityId id, const Input& in);

    // Integrate every entity one fixed step.
    void step(double dt);

    std::vector<EntityState> snapshot() const;

    bool has(EntityId id) const { return entities_.count(id) > 0; }
    size_t size() const { return entities_.size(); }
    const Entity& entity(EntityId id) const { return entities_.at(id); }

private:
    float bound_;
    std::mt19937 rng_;
    EntityId next_id_ = 1;
    std::map<EntityId, Entity> entities_;
};

}  // namespace sim
