#pragma once

#include <cstdint>
#include <string>
#include <tuple>

namespace net {

struct Endpoint {
    std::string ip = "127.0.0.1";
    uint16_t port = 0;
};

inline bool operator==(const Endpoint& a, const Endpoint& b) {
    return a.port == b.port && a.ip == b.ip;
}

// Lets Endpoint key an ordered map (server-side per-client routing).
inline bool operator<(const Endpoint& a, const Endpoint& b) {
    return std::tie(a.ip, a.port) < std::tie(b.ip, b.port);
}

}  // namespace net
