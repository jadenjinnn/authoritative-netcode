#pragma once

#include <array>
#include <cstdint>

#include "packet.h"

namespace reliable {

// Wraparound-safe "is `a` a strictly newer sequence than `b`?"
// (uint16 sequence space wraps 65535 -> 0.)
bool sequence_greater_than(uint16_t a, uint16_t b);

// Per-peer sequence/ack bookkeeping for one reliable-UDP connection.
class Connection {
public:
    // Header to stamp on the next outgoing packet; advances the local sequence.
    PacketHeader next_header();

    // Ingest a received packet's header: record its arrival (for our future acks)
    // and apply the peer's ack/ack_bits to mark our sent packets acknowledged.
    void on_received(const PacketHeader& header);

    // Did the peer acknowledge the packet we sent with this sequence?
    bool is_acked(uint16_t sequence) const;

    uint16_t local_sequence() const { return local_sequence_; }
    uint16_t remote_sequence() const { return remote_sequence_; }

private:
    void record_received(uint16_t sequence);
    uint32_t build_ack_bits() const;
    void process_acks(uint16_t ack, uint32_t ack_bits);

    static constexpr int kBufferSize = 1024;

    uint16_t local_sequence_ = 0;
    uint16_t remote_sequence_ = 0;
    bool received_any_ = false;

    std::array<bool, kBufferSize> received_{};
    std::array<bool, kBufferSize> acked_{};
};

}  // namespace reliable
