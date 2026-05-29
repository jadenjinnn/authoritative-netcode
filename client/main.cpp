#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <thread>

#include "udp_socket.h"

int main(int argc, char** argv)
{
    net::Endpoint server;
    server.ip = (argc > 1) ? argv[1] : "127.0.0.1";
    server.port = (argc > 2) ? static_cast<uint16_t>(std::atoi(argv[2])) : 9999;

    constexpr int kPings = 20;

    net::UdpSocket sock;
    sock.bind(0);
    sock.set_recv_timeout(1000);

    double min_ms = 1e9, max_ms = 0.0, sum_ms = 0.0;
    int received = 0;

    std::array<char, 64> buf{};
    for (int i = 0; i < kPings; ++i)
    {
        const char* payload = "ping";
        auto sent_at = std::chrono::steady_clock::now();
        sock.send_to(payload, std::strlen(payload), server);

        try
        {
            sock.recv_from(buf.data(), buf.size());
        }
        catch (const std::exception&)
        {
            std::printf("ping %2d: timeout\n", i);
            continue;
        }

        double rtt = std::chrono::duration<double, std::milli>(
                         std::chrono::steady_clock::now() - sent_at)
                         .count();

        min_ms = std::min(min_ms, rtt);
        max_ms = std::max(max_ms, rtt);
        sum_ms += rtt;
        ++received;
        std::printf("ping %2d: %.3f ms\n", i, rtt);

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (received > 0)
    {
        std::printf("\n%d/%d replies | rtt min %.3f / avg %.3f / max %.3f ms\n",
                    received, kPings, min_ms, sum_ms / received, max_ms);
    }
    return 0;
}
