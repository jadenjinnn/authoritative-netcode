// Smoothed RTT over a latency + jitter link. The client pings the server through
// SimSockets that add a one-way delay in each direction; the server echoes via
// the ack system, and the RttEstimator samples each ack and keeps an EWMA. The
// smoothed estimate tracks ~2x the configured one-way delay while staying steadier
// than the raw jittered samples -- the grown-up version of P0's raw RTT print.

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#include "connection.h"
#include "packet.h"
#include "rtt.h"
#include "sim_socket.h"
#include "udp_socket.h"

namespace {

constexpr int kPings = 100;
constexpr uint64_t kIntervalUs = 5000;

uint64_t now_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

}  // namespace

int main(int argc, char** argv) {
    uint32_t delay_us = static_cast<uint32_t>((argc > 1 ? std::atof(argv[1]) : 30.0) * 1000);
    uint32_t jitter_us = static_cast<uint32_t>((argc > 2 ? std::atof(argv[2]) : 5.0) * 1000);

    net::UdpSocket server_udp;
    server_udp.bind(0);
    server_udp.set_nonblocking();
    net::Endpoint server_addr = server_udp.local_endpoint();
    server_addr.ip = "127.0.0.1";

    net::UdpSocket client_udp;
    client_udp.bind(0);
    client_udp.set_nonblocking();

    net::SimSocket client_sock(client_udp, {0.0, 1, delay_us, jitter_us});
    net::SimSocket server_sock(server_udp, {0.0, 2, delay_us, jitter_us});

    reliable::Connection client_conn;
    reliable::Connection server_conn;
    reliable::RttEstimator rtt;

    int sent = 0;
    int acked = 0;
    uint64_t min_sample = UINT64_MAX;
    uint64_t max_sample = 0;
    uint64_t next_ping = now_us();
    uint64_t deadline = now_us() + 10'000'000;
    std::array<char, 256> buf{};

    while (acked < kPings && now_us() < deadline) {
        uint64_t now = now_us();

        if (sent < kPings && now >= next_ping) {
            reliable::PacketHeader header = client_conn.next_header();
            rtt.on_sent(header.sequence, now);
            std::memcpy(buf.data(), &header, sizeof(header));
            client_sock.send_to(buf.data(), sizeof(header), server_addr);
            ++sent;
            next_ping += kIntervalUs;
        }

        client_sock.pump();
        server_sock.pump();

        // Server: receive pings, echo an ack-bearing header back.
        while (auto r = server_udp.try_recv_from(buf.data(), buf.size())) {
            reliable::PacketHeader in;
            std::memcpy(&in, buf.data(), sizeof(in));
            server_conn.on_received(in);
            reliable::PacketHeader ack = server_conn.next_header();
            std::memcpy(buf.data(), &ack, sizeof(ack));
            server_sock.send_to(buf.data(), sizeof(ack), r->from);
        }

        // Client: receive acks, sample RTT for every newly-acked sequence.
        while (auto r = client_udp.try_recv_from(buf.data(), buf.size())) {
            reliable::PacketHeader ack;
            std::memcpy(&ack, buf.data(), sizeof(ack));
            client_conn.on_received(ack);
            for (int i = 0; i <= 32; ++i) {
                uint16_t seq = static_cast<uint16_t>(ack.ack - i);
                if (client_conn.is_acked(seq) && rtt.on_acked(seq, now_us())) {
                    ++acked;
                    uint64_t sample = rtt.latest_sample_us();
                    min_sample = sample < min_sample ? sample : min_sample;
                    max_sample = sample > max_sample ? sample : max_sample;
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }

    std::printf("one-way delay:    %.1f ms (+/- %.1f ms jitter), each direction\n",
                delay_us / 1000.0, jitter_us / 1000.0);
    std::printf("pings:            %d sent, %d acked\n", sent, acked);
    std::printf("RTT sample min:   %.1f ms\n", min_sample / 1000.0);
    std::printf("RTT sample max:   %.1f ms\n", max_sample / 1000.0);
    std::printf("RTT smoothed:     %.1f ms  (EWMA gain 0.1, expected ~%.1f ms)\n",
                rtt.smoothed_us() / 1000.0, 2.0 * delay_us / 1000.0);
    return 0;
}
