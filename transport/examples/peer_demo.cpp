// The whole transport stack composed behind one object. Two Peers talk over a
// lossy, latent SimSocket: a client streams reliable "events" and unreliable
// "snapshots", a server polls and acks. One Peer drives seq/ack, channel
// mux/demux, fragmentation, RTT estimation, and the send-rate governor -- the
// pieces P1 built in isolation, now on a single send path. A virtual clock makes
// the run deterministic and the numbers reproducible for the README.
//
// Usage: peer_demo [loss] [one_way_delay_ms]
//   peer_demo            -> 10% loss, 30 ms delay (healthy: governor stays Good)
//   peer_demo 0.1 200    -> 200 ms delay pushes RTT past 250 ms: governor -> Bad

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "peer.h"
#include "sim_socket.h"
#include "udp_socket.h"

namespace {

constexpr int kEvents = 200;
constexpr uint64_t kStepUs = 1'000;          // 1 ms simulation step
constexpr uint64_t kTickUs = 33'333;         // produce one event + snapshot per 30 Hz tick
constexpr uint64_t kCapUs = 120'000'000;     // safety bound on the virtual run

std::vector<uint8_t> encode(uint32_t value) {
    std::vector<uint8_t> bytes(sizeof(value));
    std::memcpy(bytes.data(), &value, sizeof(value));
    return bytes;
}

const char* mode_name(reliable::RateGovernor::Mode m) {
    return m == reliable::RateGovernor::Mode::Good ? "GOOD" : "BAD";
}

}  // namespace

int main(int argc, char** argv) {
    double loss = (argc > 1) ? std::atof(argv[1]) : 0.1;
    uint32_t delay_us = (argc > 2) ? static_cast<uint32_t>(std::atoi(argv[2]) * 1000) : 30'000;
    uint32_t jitter_us = 5'000;

    net::UdpSocket client_udp;
    client_udp.bind(0);
    client_udp.set_nonblocking();
    net::UdpSocket server_udp;
    server_udp.bind(0);
    server_udp.set_nonblocking();

    net::Endpoint client_addr = client_udp.local_endpoint();
    client_addr.ip = "127.0.0.1";
    net::Endpoint server_addr = server_udp.local_endpoint();
    server_addr.ip = "127.0.0.1";

    uint64_t now = 0;
    net::Clock clock = [&now] { return now; };
    net::SimSocket client_wire(client_udp, {loss, 12345, delay_us, jitter_us}, clock);
    net::SimSocket server_wire(server_udp, {loss, 54321, delay_us, jitter_us}, clock);

    reliable::Peer client(client_wire, server_addr);
    reliable::Peer server(server_wire, client_addr);

    int produced = 0;
    int events_delivered = 0;
    int snapshots_delivered = 0;
    uint64_t next_tick = 0;
    int mode_flips = 0;
    reliable::RateGovernor::Mode last_mode = client.mode();

    while (events_delivered < kEvents && now < kCapUs) {
        if (now >= next_tick && produced < kEvents) {
            client.queue_reliable(encode(produced));
            client.queue_unreliable(encode(produced));
            ++produced;
            next_tick += kTickUs;
        }

        client.update(now);
        client_wire.pump();

        reliable::Peer::Delivery from_client = server.poll(now);
        events_delivered += static_cast<int>(from_client.reliable.size());
        snapshots_delivered += static_cast<int>(from_client.unreliable.size());

        server.update(now);
        server_wire.pump();

        client.poll(now);

        if (client.mode() != last_mode) {
            ++mode_flips;
            last_mode = client.mode();
        }
        now += kStepUs;
    }

    double wire_loss = client_wire.sent() > 0
                           ? static_cast<double>(client_wire.dropped()) / client_wire.sent()
                           : 0.0;

    std::printf("configured:   %.0f%% loss, %u ms one-way delay (+/- %u ms jitter)\n",
                loss * 100.0, delay_us / 1000, jitter_us / 1000);
    std::printf("ran:          %llu ms of virtual time\n", (unsigned long long)(now / 1000));
    std::printf("client wire:  %llu datagrams sent, %llu dropped (%.1f%%)\n",
                (unsigned long long)client_wire.sent(),
                (unsigned long long)client_wire.dropped(), wire_loss * 100.0);
    std::printf("client egress: %llu packets, %llu fragments, %llu bytes\n",
                (unsigned long long)client.packets_sent(),
                (unsigned long long)client.fragments_sent(),
                (unsigned long long)client.bytes_sent());
    std::printf("retransmits:  %d\n", client.retransmits());
    std::printf("events    (reliable):   %d / %d delivered (%.1f%%)\n",
                events_delivered, kEvents, 100.0 * events_delivered / kEvents);
    std::printf("snapshots (unreliable): %d / %d delivered (%.1f%%, dropped stay dropped)\n",
                snapshots_delivered, produced,
                produced > 0 ? 100.0 * snapshots_delivered / produced : 0.0);
    std::printf("smoothed RTT: %.1f ms\n", client.rtt().smoothed_us() / 1000.0);
    std::printf("governor:     %s mode, %d flip(s), send interval %llu us\n",
                mode_name(client.mode()), mode_flips,
                (unsigned long long)(client.mode() == reliable::RateGovernor::Mode::Good ? 33'333 : 100'000));
    return 0;
}
