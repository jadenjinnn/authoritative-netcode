#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "channel.h"
#include "connection.h"
#include "endpoint.h"
#include "fragment.h"
#include "packet.h"
#include "rate_governor.h"
#include "rtt.h"
#include "socket.h"

namespace reliable {

// One reliable-UDP connection to a single remote, over any net::ISocket. Peer is
// the transport's front door: it composes the seq/ack connection, the channel
// mux/demux, fragmentation, RTT estimation, and the send-rate governor into one
// send/receive path. Times are caller-supplied microseconds (matching the rest of
// this layer), so pacing and RTT stay deterministic under test.
class Peer {
public:
    using Delivery = ChannelDemux::Delivery;

    Peer(net::ISocket& socket, net::Endpoint remote);

    // Enqueue a message; queue freely, update() decides when it goes on the wire.
    void queue_reliable(std::vector<uint8_t> payload);
    void queue_unreliable(std::vector<uint8_t> payload);

    // Flush one packet (header + queued messages, fragmented as needed) iff the
    // governor's send interval has elapsed since the last flush. The first call
    // always flushes to seed the connection. Returns true if a packet went out.
    bool update(uint64_t now_us);

    // Drain the socket: reassemble fragments, apply acks (retire reliable messages
    // and sample RTT), then route received messages by channel. Pull model.
    Delivery poll(uint64_t now_us);

    uint64_t bytes_sent() const { return bytes_sent_; }
    uint64_t packets_sent() const { return packets_sent_; }
    uint64_t fragments_sent() const { return fragments_sent_; }
    int retransmits() { return mux_.reliable().retransmits(); }

    const RttEstimator& rtt() const { return rtt_; }
    RateGovernor::Mode mode() const { return governor_.mode(); }

private:
    net::ISocket& socket_;
    net::Endpoint remote_;

    Connection connection_;
    ChannelMux mux_;
    ChannelDemux demux_;
    Reassembler reassembler_;
    RttEstimator rtt_;
    RateGovernor governor_;

    uint64_t last_flush_us_ = 0;
    bool flushed_once_ = false;

    uint64_t bytes_sent_ = 0;
    uint64_t packets_sent_ = 0;
    uint64_t fragments_sent_ = 0;

    std::array<uint8_t, 8192> rx_buf_{};
};

}  // namespace reliable
