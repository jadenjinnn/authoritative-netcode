#include "fragment.h"

#include <algorithm>
#include <cstring>
#include <iterator>

#include "connection.h"

namespace reliable
{

    std::vector<std::vector<uint8_t>> fragment(uint16_t packet_seq, const uint8_t *data, size_t len)
    {
        size_t count = (len + kMaxFragmentSize - 1) / kMaxFragmentSize;
        if (count == 0)
        {
            count = 1;
        }

        std::vector<std::vector<uint8_t>> datagrams;
        datagrams.reserve(count);

        for (size_t i = 0; i < count; ++i)
        {
            size_t offset = i * kMaxFragmentSize;
            size_t chunk = std::min(kMaxFragmentSize, len - offset);

            FragmentHeader header;
            header.packet_seq = packet_seq;
            header.fragment_id = static_cast<uint8_t>(i);
            header.fragment_count = static_cast<uint8_t>(count);

            std::vector<uint8_t> datagram(sizeof(header) + chunk);
            std::memcpy(datagram.data(), &header, sizeof(header));
            std::memcpy(datagram.data() + sizeof(header), data + offset, chunk);
            datagrams.push_back(std::move(datagram));
        }

        return datagrams;
    }

    std::optional<std::vector<uint8_t>> Reassembler::reassemble(const uint8_t *data, size_t len)
    {
        if (len < sizeof(FragmentHeader))
        {
            return std::nullopt;
        }

        FragmentHeader header;
        std::memcpy(&header, data, sizeof(header));
        const uint8_t *chunk = data + sizeof(header);
        size_t chunk_len = len - sizeof(header);

        if (header.fragment_count == 0 || header.fragment_id >= header.fragment_count)
        {
            return std::nullopt;
        }

        // A whole packet in one fragment never touches the group table.
        if (header.fragment_count == 1)
        {
            return std::vector<uint8_t>(chunk, chunk + chunk_len);
        }

        auto it = groups_.find(header.packet_seq);
        if (it == groups_.end())
        {
            if (groups_.size() >= kMaxGroups)
            {
                evict_oldest();
            }
            Partial fresh;
            fresh.count = header.fragment_count;
            fresh.chunks.resize(header.fragment_count);
            it = groups_.emplace(header.packet_seq, std::move(fresh)).first;
        }

        Partial &partial = it->second;

        // chunks are never empty for count >= 2, so empty() means "slot unfilled":
        // this rejects duplicates and fragments whose count disagrees with the group.
        if (header.fragment_count != partial.count || !partial.chunks[header.fragment_id].empty())
        {
            return std::nullopt;
        }

        partial.chunks[header.fragment_id].assign(chunk, chunk + chunk_len);
        ++partial.received;

        if (partial.received < partial.count)
        {
            return std::nullopt;
        }

        std::vector<uint8_t> packet;
        for (const std::vector<uint8_t> &piece : partial.chunks)
        {
            packet.insert(packet.end(), piece.begin(), piece.end());
        }
        groups_.erase(it);
        return packet;
    }

    void Reassembler::evict_oldest()
    {
        auto oldest = groups_.begin();
        for (auto it = std::next(groups_.begin()); it != groups_.end(); ++it)
        {
            if (sequence_greater_than(oldest->first, it->first))
            {
                oldest = it;
            }
        }
        groups_.erase(oldest);
    }

} // namespace reliable
