#pragma once

#include <cstdint>

namespace sim {

struct Snapshot {
    uint32_t tick;
    float x;
    float y;
};

}  // namespace sim
