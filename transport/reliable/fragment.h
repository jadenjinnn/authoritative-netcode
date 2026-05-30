#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace reliable {

// Prepended to every fragment datagram. A packet that fits in one datagram ships
// as a single fragment (count == 1); larger packets split into `count` fragments
// sharing `packet_seq`, reassembled in `fragment_id` order.
struct FragmentHeader {
    uint16_t packet_seq = 0;
    uint8_t fragment_id = 0;
    uint8_t fragment_count = 1;
};

// Largest slice of packet data per fragment; datagram = header + this, kept well
// under a typical 1500-byte MTU. The uint8 count caps a packet at 255 of these
// (~260 KB), far above anything this transport sends.
constexpr size_t kMaxFragmentSize = 1024;

// Split a serialized packet into 1..N fragment datagrams (FragmentHeader + a
// <= kMaxFragmentSize chunk). One datagram when it already fits.
std::vector<std::vector<uint8_t>> fragment(uint16_t packet_seq, const uint8_t* data, size_t len);

// Reassembles fragments into whole packets. Fragments aren't individually
// reliable: lose one of a group and the group never completes and ages out --
// the layer above resends the whole packet.
class Reassembler {
public:
    // At most this many partial groups are tracked; the oldest is evicted to make
    // room, so groups orphaned by a lost fragment can't leak.
    static constexpr size_t kMaxGroups = 16;

    // Feed one received fragment; returns the full packet once its last fragment
    // arrives, else nullopt. Malformed or duplicate fragments return nullopt.
    std::optional<std::vector<uint8_t>> reassemble(const uint8_t* data, size_t len);

    size_t tracked_groups() const { return groups_.size(); }

private:
    struct Partial {
        uint8_t count = 0;
        uint8_t received = 0;
        std::vector<std::vector<uint8_t>> chunks;  // indexed by fragment_id
    };

    void evict_oldest();

    std::unordered_map<uint16_t, Partial> groups_;
};

}  // namespace reliable
