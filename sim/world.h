#pragma once

namespace sim {

struct Entity {
    float x = 0.0f;
    float y = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
};

class World {
public:
    World();

    void step(double dt);

    const Entity& player() const { return player_; }

private:
    Entity player_;
};

}  // namespace sim
