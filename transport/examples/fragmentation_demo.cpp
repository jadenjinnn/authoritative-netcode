// Fragmentation over a lossy wire. Each ~4 KB packet splits into several
// MTU-sized fragments, and losing ANY one fragment loses the whole packet --
// so per-packet loss is amplified well above the per-fragment rate. That
// amplification is the argument for keeping packets under the MTU.

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

#include "fragment.h"
#include "sim_socket.h"
#include "udp_socket.h"

namespace {

constexpr int kPackets = 500;
constexpr size_t kPacketSize = 4000;

}  // namespace

int main(int argc, char** argv) {
    double loss = (argc > 1) ? std::atof(argv[1]) : 0.1;

    net::UdpSocket server_udp;
    server_udp.bind(0);
    server_udp.set_nonblocking();
    net::Endpoint server_addr = server_udp.local_endpoint();
    server_addr.ip = "127.0.0.1";

    net::UdpSocket client_udp;
    client_udp.bind(0);
    client_udp.set_nonblocking();
    net::SimSocket client_sock(client_udp, {loss, 12345});

    reliable::Reassembler reassembler;

    std::vector<uint8_t> payload(kPacketSize, 0xAB);
    int fragments_sent = 0;
    int packets_reassembled = 0;
    std::array<char, 2048> buf{};

    for (int p = 0; p < kPackets; ++p) {
        std::memcpy(payload.data(), &p, sizeof(p));

        std::vector<std::vector<uint8_t>> frags =
            reliable::fragment(static_cast<uint16_t>(p), payload.data(), payload.size());
        fragments_sent += static_cast<int>(frags.size());
        for (const std::vector<uint8_t>& f : frags) {
            client_sock.send_to(f.data(), f.size(), server_addr);
        }

        std::this_thread::sleep_for(std::chrono::microseconds(200));

        while (auto r = server_udp.try_recv_from(buf.data(), buf.size())) {
            if (reassembler.reassemble(reinterpret_cast<uint8_t*>(buf.data()), r->bytes)) {
                ++packets_reassembled;
            }
        }
    }

    int frags_per_packet =
        static_cast<int>(reliable::fragment(0, payload.data(), payload.size()).size());
    double frag_loss = static_cast<double>(client_sock.dropped()) / client_sock.sent();
    double packet_loss = 1.0 - static_cast<double>(packets_reassembled) / kPackets;

    std::printf("packet size:          %zu bytes (%d fragments each)\n", kPacketSize, frags_per_packet);
    std::printf("packets sent:         %d\n", kPackets);
    std::printf("fragments sent:       %d (shim dropped %llu, %.1f%%)\n", fragments_sent,
                static_cast<unsigned long long>(client_sock.dropped()), frag_loss * 100.0);
    std::printf("per-fragment loss:    %.1f%%\n", loss * 100.0);
    std::printf("packets reassembled:  %d / %d\n", packets_reassembled, kPackets);
    std::printf("effective pkt loss:   %.1f%% (amplified from %.1f%% per fragment)\n",
                packet_loss * 100.0, loss * 100.0);
    return 0;
}
