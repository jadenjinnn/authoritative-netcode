#pragma once

#include <cstdint>

namespace reliable {

// Header prepended to every packet on a reliable connection. `ack` plus the
// 32-bit mask redundantly acknowledge the last 33 sequence numbers seen from
// the peer, so acknowledgements survive packet loss.
struct PacketHeader {
    uint16_t sequence = 0;
    uint16_t ack = 0;
    uint32_t ack_bits = 0;
};

}  // namespace reliable
