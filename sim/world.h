#pragma once

#include <map>
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
// the server applies them and integrates. Full state is every entity.
class World {
public:
    // Spawn an entity for a newly connected client; returns its stable id.
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
    EntityId next_id_ = 1;
    std::map<EntityId, Entity> entities_;
};

}  // namespace sim
