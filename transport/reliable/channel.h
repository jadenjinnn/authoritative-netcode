#pragma once

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace reliable {

using MessageId = uint16_t;

// One connection multiplexes several logical streams; the channel tag on each
// message tells the receiver which reliability policy it belongs to.
enum class Channel : uint8_t {
    Unreliable = 0,
    Reliable = 1,
};

struct Message {
    Channel channel = Channel::Reliable;
    MessageId id = 0;
    std::vector<uint8_t> payload;
};

// Sender side of a reliable, unordered message stream layered on the packet ack
// system. A message stays queued until the packet that carried it is acked, so
// loss just means it rides a later packet. Every outgoing packet carries the
// whole unacked set -- bounding that is congestion control (slice 5).
class ReliableSender {
public:
    // Enqueue a message for guaranteed delivery.
    void queue(std::vector<uint8_t> payload);

    // Messages to attach to the packet being sent with `sequence`; records the
    // sequence -> messages mapping so the matching ack can retire them.
    std::vector<Message> pack(uint16_t sequence);

    // Packet `sequence` was acknowledged by the peer: retire the messages it
    // carried so they stop being resent.
    void on_acked(uint16_t sequence);

    bool empty() const { return unacked_.empty(); }
    int retransmits() const { return retransmits_; }

private:
    struct Pending {
        Message msg;
        int sends = 0;
    };

    MessageId next_id_ = 0;
    int retransmits_ = 0;
    std::vector<Pending> unacked_;
    std::unordered_map<uint16_t, std::vector<MessageId>> in_flight_;
};

// Receiver side: resends mean a message can arrive more than once, so dedupe by
// id and deliver each exactly once.
class ReliableReceiver {
public:
    // Returns the messages seen for the first time, in arrival order.
    std::vector<Message> receive(const std::vector<Message>& incoming);

private:
    std::unordered_set<MessageId> seen_;
};

// Sender side of an unreliable stream: messages are sent once and never tracked
// or resent, so a dropped packet drops them for good -- the right semantic for
// snapshots/input, where a newer message obsoletes a lost one.
class UnreliableSender {
public:
    void queue(std::vector<uint8_t> payload);

    // Messages to attach to the next packet; drains the queue (send once).
    std::vector<Message> pack();

private:
    MessageId next_id_ = 0;
    std::vector<Message> pending_;
};

// Send-side multiplexer: owns one sender per channel and packs them into a
// single packet payload. Acks only concern the reliable channel.
class ChannelMux {
public:
    ReliableSender& reliable() { return reliable_; }
    UnreliableSender& unreliable() { return unreliable_; }

    // Reliable (resent) + unreliable (once) messages for the packet `sequence`.
    std::vector<Message> pack(uint16_t sequence);

    void on_acked(uint16_t sequence) { reliable_.on_acked(sequence); }

private:
    ReliableSender reliable_;
    UnreliableSender unreliable_;
};

// Receive-side demultiplexer: routes a packet's messages by channel. Reliable
// messages are deduped (resends arrive twice); unreliable pass straight through.
class ChannelDemux {
public:
    struct Delivery {
        std::vector<Message> reliable;
        std::vector<Message> unreliable;
    };

    Delivery route(const std::vector<Message>& incoming);

private:
    ReliableReceiver reliable_;
};

}  // namespace reliable
