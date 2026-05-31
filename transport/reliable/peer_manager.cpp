#include "peer_manager.h"

#include <set>

namespace reliable
{

    PeerManager::PeerManager(net::ISocket &socket) : socket_(socket)
    {
    }

    std::vector<PeerManager::Incoming> PeerManager::poll(uint64_t now_us)
    {
        std::vector<net::Endpoint> order;  // touched peers, in first-seen order
        std::set<net::Endpoint> seen;
        std::set<net::Endpoint> fresh;

        while (std::optional<net::RecvResult> r = socket_.try_recv_from(rx_buf_.data(), rx_buf_.size()))
        {
            net::Endpoint from = r->from;
            auto it = conns_.find(from);
            if (it == conns_.end())
            {
                it = conns_.emplace(from, std::make_unique<Conn>(socket_, from)).first;
                fresh.insert(from);
            }
            it->second->inbox.deliver(rx_buf_.data(), r->bytes);
            if (seen.insert(from).second)
            {
                order.push_back(from);
            }
        }

        std::vector<Incoming> out;
        out.reserve(order.size());
        for (const net::Endpoint &e : order)
        {
            Peer::Delivery d = conns_.at(e)->peer.poll(now_us);
            out.push_back(Incoming{e, fresh.count(e) > 0, std::move(d)});
        }
        return out;
    }

    void PeerManager::update_all(uint64_t now_us)
    {
        for (auto &entry : conns_)
        {
            entry.second->peer.update(now_us);
        }
    }

    std::vector<net::Endpoint> PeerManager::endpoints() const
    {
        std::vector<net::Endpoint> out;
        out.reserve(conns_.size());
        for (const auto &entry : conns_)
        {
            out.push_back(entry.first);
        }
        return out;
    }

    uint64_t PeerManager::total_bytes_sent() const
    {
        uint64_t total = 0;
        for (const auto &entry : conns_)
        {
            total += entry.second->peer.bytes_sent();
        }
        return total;
    }

    uint64_t PeerManager::total_packets_sent() const
    {
        uint64_t total = 0;
        for (const auto &entry : conns_)
        {
            total += entry.second->peer.packets_sent();
        }
        return total;
    }

} // namespace reliable
