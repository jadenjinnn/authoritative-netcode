#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <vector>

#include "endpoint.h"
#include "fixed_timestep.h"
#include "input.h"
#include "peer_manager.h"
#include "snapshot.h"
#include "udp_socket.h"
#include "world.h"

namespace
{

    constexpr double kTickHz = 60.0;
    constexpr double kDt = 1.0 / kTickHz;
    constexpr uint64_t kReportEveryUs = 1'000'000;  // print egress once a second

    uint64_t now_us()
    {
        return std::chrono::duration_cast<std::chrono::microseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

} // namespace

int main(int argc, char **argv)
{
    uint16_t port = (argc > 1) ? static_cast<uint16_t>(std::atoi(argv[1])) : 9999;

    // Line-buffer stdout so each periodic report lands even when the process is
    // killed (SIGTERM) at the end of a measurement run rather than exiting cleanly.
    std::setvbuf(stdout, nullptr, _IOLBF, 0);

    net::UdpSocket sock;
    sock.bind(port);
    sock.set_nonblocking();

    reliable::PeerManager mgr(sock);
    sim::World world;
    sim::FixedTimestep ts(kDt);
    std::map<net::Endpoint, sim::EntityId> entity_of;
    uint32_t tick = 0;

    std::printf("authoritative server on :%u @ %.0f Hz (Peer protocol, multi-client)\n",
                port, kTickHz);

    uint64_t last = now_us();
    uint64_t report_at = last + kReportEveryUs;
    uint64_t reliable_events = 0;
    uint64_t last_bytes = 0;

    for (;;)
    {
        uint64_t now = now_us();

        // Ingest: route datagrams to per-client Peers, spawn entities on first contact,
        // apply this tick's inputs to each sender's entity.
        std::vector<reliable::PeerManager::Incoming> incoming = mgr.poll(now);
        for (reliable::PeerManager::Incoming &in : incoming)
        {
            if (in.is_new)
            {
                entity_of[in.from] = world.add_player();
            }
            sim::EntityId id = entity_of[in.from];

            for (const reliable::Message &m : in.delivery.unreliable)
            {
                sim::Input input;
                if (sim::decode_input(m.payload, input))
                {
                    world.apply_input(id, input);
                }
            }
            reliable_events += in.delivery.reliable.size();
        }

        double elapsed = static_cast<double>(now - last) / 1'000'000.0;
        last = now;
        int steps = ts.advance(elapsed);
        for (int i = 0; i < steps; ++i)
        {
            world.step(kDt);
            ++tick;
        }

        // Broadcast the naive full state: serialize once, send the identical blob to
        // every client. Cost is O(N entities) x O(N clients) -- the P2 baseline.
        if (steps > 0 && mgr.size() > 0)
        {
            std::vector<uint8_t> snap = sim::encode_snapshot(tick, world.snapshot());
            for (const net::Endpoint &e : mgr.endpoints())
            {
                mgr.peer(e).queue_unreliable(snap);
            }
        }
        mgr.update_all(now);

        if (now >= report_at)
        {
            uint64_t bytes = mgr.total_bytes_sent();
            double bps = static_cast<double>(bytes - last_bytes);  // ~1s window
            std::printf("clients=%zu entities=%zu tick=%u  egress=%.1f KB/s (%.1f KB/s per client)  reliable_events=%llu\n",
                        mgr.size(), world.size(), tick, bps / 1024.0,
                        mgr.size() > 0 ? bps / 1024.0 / mgr.size() : 0.0,
                        static_cast<unsigned long long>(reliable_events));
            last_bytes = bytes;
            report_at += kReportEveryUs;
        }
    }
}
