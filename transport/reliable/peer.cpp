#include "peer.h"

#include <cstring>
#include <iterator>
#include <utility>

namespace reliable
{

    namespace
    {

        // Wire layout: [PacketHeader][uint16 count]{ [uint8 channel][uint16 id][uint16 len][bytes] }*
        std::vector<uint8_t> serialize(const PacketHeader &header, const std::vector<Message> &msgs)
        {
            std::vector<uint8_t> out;
            auto append = [&](const void *p, size_t n)
            {
                const uint8_t *b = static_cast<const uint8_t *>(p);
                out.insert(out.end(), b, b + n);
            };

            append(&header, sizeof(header));
            uint16_t count = static_cast<uint16_t>(msgs.size());
            append(&count, sizeof(count));

            for (const Message &m : msgs)
            {
                uint8_t channel = static_cast<uint8_t>(m.channel);
                uint16_t len = static_cast<uint16_t>(m.payload.size());
                append(&channel, sizeof(channel));
                append(&m.id, sizeof(m.id));
                append(&len, sizeof(len));
                append(m.payload.data(), len);
            }
            return out;
        }

        // Returns false on a truncated/malformed blob; a corrupt packet is dropped
        // like wire loss, so the reliable layer just resends it.
        bool deserialize(const std::vector<uint8_t> &buf, PacketHeader &header, std::vector<Message> &msgs)
        {
            size_t off = 0;
            if (buf.size() < sizeof(header) + sizeof(uint16_t))
            {
                return false;
            }

            std::memcpy(&header, buf.data(), sizeof(header));
            off += sizeof(header);
            uint16_t count = 0;
            std::memcpy(&count, buf.data() + off, sizeof(count));
            off += sizeof(count);

            for (uint16_t i = 0; i < count; ++i)
            {
                if (off + sizeof(uint8_t) + sizeof(MessageId) + sizeof(uint16_t) > buf.size())
                {
                    return false;
                }

                Message m;
                uint8_t channel = 0;
                std::memcpy(&channel, buf.data() + off, sizeof(channel));
                off += sizeof(channel);
                m.channel = static_cast<Channel>(channel);
                std::memcpy(&m.id, buf.data() + off, sizeof(m.id));
                off += sizeof(m.id);
                uint16_t len = 0;
                std::memcpy(&len, buf.data() + off, sizeof(len));
                off += sizeof(len);

                if (off + len > buf.size())
                {
                    return false;
                }
                m.payload.assign(buf.data() + off, buf.data() + off + len);
                off += len;
                msgs.push_back(std::move(m));
            }
            return true;
        }

    } // namespace

    Peer::Peer(net::ISocket &socket, net::Endpoint remote)
        : socket_(socket), remote_(std::move(remote))
    {
    }

    void Peer::queue_reliable(std::vector<uint8_t> payload)
    {
        mux_.reliable().queue(std::move(payload));
    }

    void Peer::queue_unreliable(std::vector<uint8_t> payload)
    {
        mux_.unreliable().queue(std::move(payload));
    }

    bool Peer::update(uint64_t now_us)
    {
        // The first flush seeds the connection; after that we send no faster than the
        // governor's current interval.
        if (flushed_once_ && now_us - last_flush_us_ < governor_.send_interval_us())
        {
            return false;
        }

        PacketHeader header = connection_.next_header();
        std::vector<Message> msgs = mux_.pack(header.sequence);
        std::vector<uint8_t> blob = serialize(header, msgs);
        rtt_.on_sent(header.sequence, now_us);

        std::vector<std::vector<uint8_t>> datagrams = fragment(header.sequence, blob.data(), blob.size());
        for (const std::vector<uint8_t> &datagram : datagrams)
        {
            socket_.send_to(datagram.data(), datagram.size(), remote_);
            bytes_sent_ += datagram.size();
            ++fragments_sent_;
        }
        ++packets_sent_;
        flushed_once_ = true;
        last_flush_us_ = now_us;

        // Pace on RTT from prior acks; the packet just sent has no sample yet.
        governor_.update(rtt_.smoothed_us(), now_us);
        return true;
    }

    Peer::Delivery Peer::poll(uint64_t now_us)
    {
        std::vector<Message> incoming;

        while (std::optional<net::RecvResult> r = socket_.try_recv_from(rx_buf_.data(), rx_buf_.size()))
        {
            std::optional<std::vector<uint8_t>> packet = reassembler_.reassemble(rx_buf_.data(), r->bytes);
            if (!packet)
            {
                continue;
            }

            PacketHeader header;
            std::vector<Message> msgs;
            if (!deserialize(*packet, header, msgs))
            {
                continue;
            }

            connection_.on_received(header);
            for (int i = 0; i <= 32; ++i)
            {
                uint16_t seq = static_cast<uint16_t>(header.ack - i);
                if (connection_.is_acked(seq))
                {
                    mux_.on_acked(seq);
                    rtt_.on_acked(seq, now_us);
                }
            }

            incoming.insert(incoming.end(), std::make_move_iterator(msgs.begin()),
                            std::make_move_iterator(msgs.end()));
        }

        return demux_.route(incoming);
    }

} // namespace reliable
