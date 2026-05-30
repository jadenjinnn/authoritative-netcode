#include "channel.h"

#include <algorithm>
#include <iterator>
#include <unordered_set>
#include <utility>

namespace reliable
{

    void ReliableSender::queue(std::vector<uint8_t> payload)
    {
        unacked_.push_back(Pending{Message{Channel::Reliable, next_id_++, std::move(payload)}, 0});
    }

    std::vector<Message> ReliableSender::pack(uint16_t sequence)
    {
        std::vector<MessageId> message_ids;
        std::vector<Message> messages;

        for (auto &unack : unacked_)
        {
            message_ids.push_back(unack.msg.id);

            if (unack.sends > 0)
            {
                ++retransmits_;
            }

            ++unack.sends;

            messages.push_back(unack.msg);
        }

        in_flight_[sequence] = message_ids;

        return messages;
    }

    void ReliableSender::on_acked(uint16_t sequence)
    {
        auto it = in_flight_.find(sequence);
        if (it == in_flight_.end())
        {
            return;
        }

        std::unordered_set<MessageId> acked_ids(it->second.begin(), it->second.end());
        unacked_.erase(std::remove_if(unacked_.begin(), unacked_.end(), [&](const Pending &p)
                                      { return acked_ids.count(p.msg.id) > 0; }),
                       unacked_.end());
        in_flight_.erase(it);
    }

    std::vector<Message> ReliableReceiver::receive(const std::vector<Message> &incoming)
    {
        std::vector<Message> fresh;
        for (const Message &m : incoming)
        {
            if (seen_.insert(m.id).second)
            {
                fresh.push_back(m);
            }
        }
        return fresh;
    }

    void UnreliableSender::queue(std::vector<uint8_t> payload)
    {
        pending_.push_back(Message{Channel::Unreliable, next_id_++, std::move(payload)});
    }

    std::vector<Message> UnreliableSender::pack()
    {
        std::vector<Message> out = std::move(pending_);
        pending_.clear();
        return out;
    }

    std::vector<Message> ChannelMux::pack(uint16_t sequence)
    {
        std::vector<Message> out = reliable_.pack(sequence);
        std::vector<Message> unreliable = unreliable_.pack();
        out.insert(out.end(), std::make_move_iterator(unreliable.begin()),
                   std::make_move_iterator(unreliable.end()));
        return out;
    }

    ChannelDemux::Delivery ChannelDemux::route(const std::vector<Message> &incoming)
    {
        std::vector<Message> reliable_batch;
        Delivery out;
        for (const Message &m : incoming)
        {
            if (m.channel == Channel::Reliable)
            {
                reliable_batch.push_back(m);
            }
            else
            {
                out.unreliable.push_back(m);
            }
        }
        out.reliable = reliable_.receive(reliable_batch);
        return out;
    }

} // namespace reliable
