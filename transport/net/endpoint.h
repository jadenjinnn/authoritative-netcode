#pragma once

#include <cstdint>
#include <string>

namespace net {

struct Endpoint {
    std::string ip = "127.0.0.1";
    uint16_t port = 0;
};

}  // namespace net
