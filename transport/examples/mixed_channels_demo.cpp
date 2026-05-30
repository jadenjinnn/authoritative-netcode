// Two channels on one connection over a lossy wire: unreliable "snapshots"
// (one per tick, dropped ones stay dropped) ride the same packets as reliable
// "events" (resent until acked). Shows the payoff of multiplexing -- the event
// stream reaches 100% while the snapshot stream just thins out, no resend cost.

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
#include "channel.h"
#include "sim_socket.h"
#include "udp_socket.h"

namespace {

constexpr int kEvents = 200;

std::vector<uint8_t> encode(uint32_t value) {
    std::vector<uint8_t> bytes(sizeof(value));
    std::memcpy(bytes.data(), &value, sizeof(value));
    return bytes;
}

// Wire layout: [PacketHeader][uint16 count]{ [uint8 channel][uint16 id][uint16 len][bytes] }*
size_t write_packet(char* buf, const reliable::PacketHeader& header,
                    const std::vector<reliable::Message>& msgs) {
    size_t off = 0;
    std::memcpy(buf + off, &header, sizeof(header));
    off += sizeof(header);

    uint16_t count = static_cast<uint16_t>(msgs.size());
    std::memcpy(buf + off, &count, sizeof(count));
    off += sizeof(count);

    for (const reliable::Message& m : msgs) {
        uint8_t channel = static_cast<uint8_t>(m.channel);
        std::memcpy(buf + off, &channel, sizeof(channel));
        off += sizeof(channel);
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
        uint8_t channel = 0;
        std::memcpy(&channel, buf + off, sizeof(channel));
        off += sizeof(channel);
        m.channel = static_cast<reliable::Channel>(channel);
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
    reliable::ChannelMux mux;
    reliable::ChannelDemux demux;

    int events_queued = 0;
    int events_delivered = 0;
    int snapshots_sent = 0;
    int snapshots_delivered = 0;
    int guard = 0;
    std::array<char, 8192> buf{};

    while ((events_queued < kEvents || !mux.reliable().empty()) && guard++ < 200000) {
        if (events_queued < kEvents) {
            mux.reliable().queue(encode(events_queued++));
        }
        mux.unreliable().queue(encode(snapshots_sent++));

        reliable::PacketHeader header = sender_conn.next_header();
        std::vector<reliable::Message> msgs = mux.pack(header.sequence);
        size_t n = write_packet(buf.data(), header, msgs);
        client_sock.send_to(buf.data(), n, server_addr);

        std::this_thread::sleep_for(std::chrono::microseconds(200));

        // Server: ingest data packets, route by channel, ack back clean.
        while (auto r = server_udp.try_recv_from(buf.data(), buf.size())) {
            reliable::PacketHeader in;
            std::vector<reliable::Message> got = read_packet(buf.data(), in);
            server_conn.on_received(in);
            reliable::ChannelDemux::Delivery d = demux.route(got);
            events_delivered += static_cast<int>(d.reliable.size());
            snapshots_delivered += static_cast<int>(d.unreliable.size());

            reliable::PacketHeader ack = server_conn.next_header();
            size_t m = write_packet(buf.data(), ack, {});
            server_udp.send_to(buf.data(), m, r->from);
        }

        // Client: ingest acks, retire the events they confirm.
        while (client_sock.try_recv_from(buf.data(), buf.size())) {
            reliable::PacketHeader ack;
            read_packet(buf.data(), ack);
            sender_conn.on_received(ack);
            for (int i = 0; i <= 32; ++i) {
                uint16_t seq = static_cast<uint16_t>(ack.ack - i);
                if (sender_conn.is_acked(seq)) {
                    mux.on_acked(seq);
                }
            }
        }
    }

    double shim_loss = static_cast<double>(client_sock.dropped()) / client_sock.sent();
    double event_rate = static_cast<double>(events_delivered) / kEvents;
    double snap_rate = static_cast<double>(snapshots_delivered) / snapshots_sent;

    std::printf("configured loss:        %.1f%%\n", loss * 100.0);
    std::printf("packets sent:           %llu (shim dropped %llu, %.1f%%)\n",
                static_cast<unsigned long long>(client_sock.sent()),
                static_cast<unsigned long long>(client_sock.dropped()), shim_loss * 100.0);
    std::printf("retransmits:            %d\n", mux.reliable().retransmits());
    std::printf("events    (reliable):   %d / %d delivered (%.1f%%)\n",
                events_delivered, kEvents, event_rate * 100.0);
    std::printf("snapshots (unreliable): %d / %d delivered (%.1f%%, dropped stay dropped)\n",
                snapshots_delivered, snapshots_sent, snap_rate * 100.0);
    return 0;
}
