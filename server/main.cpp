#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <vector>

#include <prometheus/counter.h>
#include <prometheus/exposer.h>
#include <prometheus/gauge.h>
#include <prometheus/registry.h>

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

    // Metrics: civetweb serves /metrics on its own thread; the loop only updates the
    // (lock-free) metric objects below. Counters are monotonic; the dashboard derives
    // rates via PromQL. Gauges are set each report.
    prometheus::Exposer exposer{"0.0.0.0:8080"};
    auto registry = std::make_shared<prometheus::Registry>();
    exposer.RegisterCollectable(registry);

    auto& egress_bytes = prometheus::BuildCounter()
        .Name("netcode_egress_bytes_total")
        .Help("Total bytes sent to all clients")
        .Register(*registry).Add({});
    auto& egress_packets = prometheus::BuildCounter()
        .Name("netcode_egress_packets_total")
        .Help("Total packets sent to all clients")
        .Register(*registry).Add({});
    auto& reliable_events_metric = prometheus::BuildCounter()
        .Name("netcode_reliable_events_total")
        .Help("Reliable events received from clients")
        .Register(*registry).Add({});
    auto& clients_gauge = prometheus::BuildGauge()
        .Name("netcode_connected_clients")
        .Help("Currently connected clients")
        .Register(*registry).Add({});
    auto& entities_gauge = prometheus::BuildGauge()
        .Name("netcode_entities")
        .Help("Entities in the world")
        .Register(*registry).Add({});
    auto& tick_rate_gauge = prometheus::BuildGauge()
        .Name("netcode_tick_rate_hz")
        .Help("Authoritative ticks per second in the last report window")
        .Register(*registry).Add({});

    std::printf("authoritative server on :%u @ %.0f Hz (Peer protocol, multi-client); /metrics on :8080\n",
                port, kTickHz);

    uint64_t last = now_us();
    uint64_t report_at = last + kReportEveryUs;
    uint64_t report_last = last;
    uint64_t reliable_events = 0;
    uint64_t last_bytes = 0;
    uint64_t last_packets = 0;
    uint64_t last_reliable_events = 0;
    uint32_t last_tick = 0;

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
            uint64_t packets = mgr.total_packets_sent();
            double window_s = static_cast<double>(now - report_last) / 1'000'000.0;
            double bps = static_cast<double>(bytes - last_bytes);  // ~1s window
            std::printf("clients=%zu entities=%zu tick=%u  egress=%.1f KB/s (%.1f KB/s per client)  reliable_events=%llu\n",
                        mgr.size(), world.size(), tick, bps / 1024.0,
                        mgr.size() > 0 ? bps / 1024.0 / mgr.size() : 0.0,
                        static_cast<unsigned long long>(reliable_events));

            // Feed the same numbers to Prometheus: counters by delta since last report,
            // gauges set to the current value.
            egress_bytes.Increment(static_cast<double>(bytes - last_bytes));
            egress_packets.Increment(static_cast<double>(packets - last_packets));
            reliable_events_metric.Increment(static_cast<double>(reliable_events - last_reliable_events));
            clients_gauge.Set(static_cast<double>(mgr.size()));
            entities_gauge.Set(static_cast<double>(world.size()));
            tick_rate_gauge.Set(window_s > 0.0 ? (tick - last_tick) / window_s : 0.0);

            last_bytes = bytes;
            last_packets = packets;
            last_reliable_events = reliable_events;
            last_tick = tick;
            report_last = now;
            report_at += kReportEveryUs;
        }
    }
}
