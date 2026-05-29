#include "connection.h"

namespace reliable
{

    bool sequence_greater_than(uint16_t a, uint16_t b)
    {
        return (a > b && (a - b) <= 32768) || (a < b && (b - a) > 32768);
    }

    PacketHeader Connection::next_header()
    {
        PacketHeader header;
        header.sequence = local_sequence_;
        header.ack = remote_sequence_;
        header.ack_bits = build_ack_bits();

        acked_[local_sequence_ % kBufferSize] = false;
        ++local_sequence_;
        return header;
    }

    void Connection::on_received(const PacketHeader &header)
    {
        record_received(header.sequence);
        process_acks(header.ack, header.ack_bits);
    }

    bool Connection::is_acked(uint16_t sequence) const
    {
        return acked_[sequence % kBufferSize];
    }

    void Connection::record_received(uint16_t sequence)
    {
        if (!received_any_ || sequence_greater_than(sequence, remote_sequence_))
        {
            remote_sequence_ = sequence;
        }
        received_[sequence % kBufferSize] = true;
        received_any_ = true;
    }

    uint32_t Connection::build_ack_bits() const
    {
        uint32_t bits = 0;

        for (int i = 0; i < 32; ++i)
        {
            uint16_t seq = remote_sequence_ - (i + 1);
            if (received_[seq % kBufferSize])
            {
                bits |= (1u << i);
            }
        }

        return bits;
    }

    void Connection::process_acks(uint16_t ack, uint32_t ack_bits)
    {
        acked_[ack % kBufferSize] = true;

        for (int i = 0; i < 32; ++i)
        {
            if (ack_bits & (1u << i))
            {
                uint16_t seq = ack - (i + 1);
                acked_[seq % kBufferSize] = true;
            }
        }
    }

} // namespace reliable
