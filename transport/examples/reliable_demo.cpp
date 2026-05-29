// Drives reliable-connection traffic over a real loopback UDP path with a
// configurable artificial loss rate, then reports the delivery rate two
// independent ways: what the shim dropped, and what the ack system observed.
// The two should agree, which is the point -- it shows the seq/ack layer
// identifies exactly which packets survived a lossy wire.

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#include "connection.h"
#include "packet.h"
#include "sim_socket.h"
#include "udp_socket.h"

namespace {

// The seq/ack ring tracks the most recent 1024 packets; staying under that keeps
// every sequence in a distinct slot so the acked count is exact.
constexpr int kPackets = 1000;

void pump(net::UdpSocket& server_udp, reliable::Connection& server_conn,
          net::SimSocket& client_sock, reliable::Connection& client_conn) {
    std::array<char, 64> buf{};

    // Server: ingest each data packet and reply with an ack-carrying header.
    while (auto r = server_udp.try_recv_from(buf.data(), buf.size())) {
        reliable::PacketHeader in;
        std::memcpy(&in, buf.data(), sizeof(in));
        server_conn.on_received(in);

        reliable::PacketHeader ack = server_conn.next_header();
        std::memcpy(buf.data(), &ack, sizeof(ack));
        server_udp.send_to(buf.data(), sizeof(ack), r->from);
    }

    // Client: ingest the acks flowing back.
    while (client_sock.try_recv_from(buf.data(), buf.size())) {
        reliable::PacketHeader in;
        std::memcpy(&in, buf.data(), sizeof(in));
        client_conn.on_received(in);
    }
}

}  // namespace

int main(int argc, char** argv) {
    double loss = (argc > 1) ? std::atof(argv[1]) : 0.2;

    net::UdpSocket server_udp;
    server_udp.bind(0);
    server_udp.set_nonblocking();
    net::Endpoint server_addr = server_udp.local_endpoint();
    server_addr.ip = "127.0.0.1";

    net::UdpSocket client_udp;
    client_udp.bind(0);
    client_udp.set_nonblocking();

    // Loss on the client->server (data) path; server->client (acks) stays clean.
    net::SimSocket client_sock(client_udp, {loss, 12345});

    reliable::Connection client_conn;
    reliable::Connection server_conn;

    std::array<char, 64> buf{};
    for (int i = 0; i < kPackets; ++i) {
        reliable::PacketHeader h = client_conn.next_header();
        std::memcpy(buf.data(), &h, sizeof(h));
        client_sock.send_to(buf.data(), sizeof(h), server_addr);

        std::this_thread::sleep_for(std::chrono::microseconds(200));
        pump(server_udp, server_conn, client_sock, client_conn);
    }

    // Flush any acks still in flight.
    for (int i = 0; i < 100; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        pump(server_udp, server_conn, client_sock, client_conn);
    }

    int acked = 0;
    for (uint16_t seq = 0; seq < kPackets; ++seq) {
        if (client_conn.is_acked(seq)) {
            ++acked;
        }
    }

    double shim_loss = static_cast<double>(client_sock.dropped()) / client_sock.sent();
    double delivered = static_cast<double>(acked) / kPackets;

    std::printf("packets sent:     %d\n", kPackets);
    std::printf("configured loss:  %.1f%%\n", loss * 100.0);
    std::printf("shim dropped:     %llu (%.1f%% loss)\n",
                static_cast<unsigned long long>(client_sock.dropped()), shim_loss * 100.0);
    std::printf("acked by peer:    %d (%.1f%% delivered)\n", acked, delivered * 100.0);
    return 0;
}
