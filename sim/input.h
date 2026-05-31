#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

namespace sim {

// A client's intended movement for one tick: a direction in [-1, 1] per axis. The
// server applies it to the sender's entity, which is what makes the server
// authoritative -- clients send intent, the server owns the resulting state.
struct Input {
    float dx = 0.0f;
    float dy = 0.0f;
};

inline std::vector<uint8_t> encode_input(const Input& in) {
    std::vector<uint8_t> bytes(sizeof(Input));
    std::memcpy(bytes.data(), &in, sizeof(Input));
    return bytes;
}

inline bool decode_input(const std::vector<uint8_t>& bytes, Input& out) {
    if (bytes.size() < sizeof(Input)) {
        return false;
    }
    std::memcpy(&out, bytes.data(), sizeof(Input));
    return true;
}

}  // namespace sim
