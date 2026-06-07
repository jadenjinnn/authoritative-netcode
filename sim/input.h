#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

namespace sim {

// A client's intended movement for one tick: a direction in [-1, 1] per axis. The
// server applies it to the sender's entity, which is what makes the server
// authoritative -- clients send intent, the server owns the resulting state.
// last_received_tick is the newest snapshot tick the sender has applied; it is the
// app-level ack the server deltas against (0 = none yet).
struct Input {
    float dx = 0.0f;
    float dy = 0.0f;
    uint32_t last_received_tick = 0;
};

// Wire: [uint32 last_received_tick][float dx][float dy]
inline std::vector<uint8_t> encode_input(const Input& in) {
    std::vector<uint8_t> bytes(sizeof(uint32_t) + 2 * sizeof(float));
    size_t off = 0;
    std::memcpy(bytes.data() + off, &in.last_received_tick, sizeof(in.last_received_tick));
    off += sizeof(in.last_received_tick);
    std::memcpy(bytes.data() + off, &in.dx, sizeof(in.dx));
    off += sizeof(in.dx);
    std::memcpy(bytes.data() + off, &in.dy, sizeof(in.dy));
    return bytes;
}

inline bool decode_input(const std::vector<uint8_t>& bytes, Input& out) {
    constexpr size_t kSize = sizeof(uint32_t) + 2 * sizeof(float);
    if (bytes.size() < kSize) {
        return false;
    }
    size_t off = 0;
    std::memcpy(&out.last_received_tick, bytes.data() + off, sizeof(out.last_received_tick));
    off += sizeof(out.last_received_tick);
    std::memcpy(&out.dx, bytes.data() + off, sizeof(out.dx));
    off += sizeof(out.dx);
    std::memcpy(&out.dy, bytes.data() + off, sizeof(out.dy));
    return true;
}

}  // namespace sim
