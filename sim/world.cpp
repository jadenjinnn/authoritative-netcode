#include "world.h"

#include <algorithm>

namespace sim
{

    namespace
    {
        constexpr float kBound = 100.0f;
    }

    World::World()
    {
        player_.x = 0.0f;
        player_.y = 50.0f;
        player_.vx = 20.0f;
    }

    void World::step(double dt)
    {
        player_.x += player_.vx * static_cast<float>(dt);
        if (player_.x < 0.0f || player_.x > kBound)
        {
            player_.vx = -player_.vx;
            player_.x = std::clamp(player_.x, 0.0f, kBound);
        }
    }

} // namespace sim
