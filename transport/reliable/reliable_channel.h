#pragma once

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace reliable {

using MessageId = uint16_t;

struct Message {
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

}  // namespace reliable
