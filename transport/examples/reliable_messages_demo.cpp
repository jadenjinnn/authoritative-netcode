// Reliable delivery over a lossy wire: pushes N reliable messages client ->
// server through the loss shim, server acks back over a clean path, and the
// sender re-attaches anything unacked to later packets until everything lands.
// Contrast with reliable_demo: that measures raw packet survival (~80% at 20%
// loss); this recovers to 100% delivered, and reports the retransmit cost.

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

#include "connection.h"
#include "packet.h"
#include "reliable_channel.h"
#include "sim_socket.h"
#include "udp_socket.h"

namespace {

constexpr int kMessages = 500;

std::vector<uint8_t> encode(uint32_t value) {
    std::vector<uint8_t> bytes(sizeof(value));
    std::memcpy(bytes.data(), &value, sizeof(value));
    return bytes;
}

// Wire layout: [PacketHeader][uint16 count]{ [uint16 id][uint16 len][bytes] }*
size_t write_packet(char* buf, const reliable::PacketHeader& header,
                    const std::vector<reliable::Message>& msgs) {
    size_t off = 0;
    std::memcpy(buf + off, &header, sizeof(header));
    off += sizeof(header);

    uint16_t count = static_cast<uint16_t>(msgs.size());
    std::memcpy(buf + off, &count, sizeof(count));
    off += sizeof(count);

    for (const reliable::Message& m : msgs) {
        std::memcpy(buf + off, &m.id, sizeof(m.id));
        off += sizeof(m.id);
        uint16_t len = static_cast<uint16_t>(m.payload.size());
        std::memcpy(buf + off, &len, sizeof(len));
        off += sizeof(len);
        std::memcpy(buf + off, m.payload.data(), len);
        off += len;
    }
    return off;
}

std::vector<reliable::Message> read_packet(const char* buf, reliable::PacketHeader& header) {
    size_t off = 0;
    std::memcpy(&header, buf + off, sizeof(header));
    off += sizeof(header);

    uint16_t count = 0;
    std::memcpy(&count, buf + off, sizeof(count));
    off += sizeof(count);

    std::vector<reliable::Message> msgs;
    for (uint16_t i = 0; i < count; ++i) {
        reliable::Message m;
        std::memcpy(&m.id, buf + off, sizeof(m.id));
        off += sizeof(m.id);
        uint16_t len = 0;
        std::memcpy(&len, buf + off, sizeof(len));
        off += sizeof(len);
        m.payload.resize(len);
        std::memcpy(m.payload.data(), buf + off, len);
        off += len;
        msgs.push_back(std::move(m));
    }
    return msgs;
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
    net::SimSocket client_sock(client_udp, {loss, 12345});

    reliable::Connection sender_conn;
    reliable::Connection server_conn;
    reliable::ReliableSender sender;
    reliable::ReliableReceiver receiver;

    int next_to_queue = 0;
    int delivered = 0;
    int guard = 0;
    std::array<char, 8192> buf{};

    while ((next_to_queue < kMessages || !sender.empty()) && guard++ < 200000) {
        if (next_to_queue < kMessages) {
            sender.queue(encode(next_to_queue++));
        }

        reliable::PacketHeader header = sender_conn.next_header();
        std::vector<reliable::Message> msgs = sender.pack(header.sequence);
        size_t n = write_packet(buf.data(), header, msgs);
        client_sock.send_to(buf.data(), n, server_addr);

        std::this_thread::sleep_for(std::chrono::microseconds(200));

        // Server: ingest data packets, deliver fresh messages, ack back clean.
        while (auto r = server_udp.try_recv_from(buf.data(), buf.size())) {
            reliable::PacketHeader in;
            std::vector<reliable::Message> got = read_packet(buf.data(), in);
            server_conn.on_received(in);
            delivered += static_cast<int>(receiver.receive(got).size());

            reliable::PacketHeader ack = server_conn.next_header();
            size_t m = write_packet(buf.data(), ack, {});
            server_udp.send_to(buf.data(), m, r->from);
        }

        // Client: ingest acks, retire messages the ack confirms.
        while (client_sock.try_recv_from(buf.data(), buf.size())) {
            reliable::PacketHeader ack;
            read_packet(buf.data(), ack);
            sender_conn.on_received(ack);
            for (int i = 0; i <= 32; ++i) {
                uint16_t seq = static_cast<uint16_t>(ack.ack - i);
                if (sender_conn.is_acked(seq)) {
                    sender.on_acked(seq);
                }
            }
        }
    }

    double shim_loss = static_cast<double>(client_sock.dropped()) / client_sock.sent();
    double rate = static_cast<double>(delivered) / kMessages;

    std::printf("messages:         %d\n", kMessages);
    std::printf("configured loss:  %.1f%%\n", loss * 100.0);
    std::printf("packets sent:     %llu (shim dropped %llu, %.1f%%)\n",
                static_cast<unsigned long long>(client_sock.sent()),
                static_cast<unsigned long long>(client_sock.dropped()), shim_loss * 100.0);
    std::printf("retransmits:      %d\n", sender.retransmits());
    std::printf("delivered:        %d / %d (%.1f%%)\n", delivered, kMessages, rate * 100.0);
    return 0;
}
