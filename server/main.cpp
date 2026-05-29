#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <thread>

#include "fixed_timestep.h"
#include "snapshot.h"
#include "udp_socket.h"
#include "world.h"

namespace
{

    constexpr double kTickHz = 60.0;
    constexpr double kDt = 1.0 / kTickHz;

    void send_state(net::UdpSocket &sock, const net::Endpoint &dest,
                    const sim::World &world, uint32_t tick)
    {
        sim::Snapshot snap{tick, world.player().x, world.player().y};
        sock.send_to(&snap, sizeof(snap), dest);
    }

} // namespace

int main(int argc, char **argv)
{
    uint16_t port = (argc > 1) ? static_cast<uint16_t>(std::atoi(argv[1])) : 9999;

    net::UdpSocket sock;
    sock.bind(port);
    sock.set_nonblocking();
    std::printf("authoritative server on :%u @ %.0f Hz\n", port, kTickHz);

    sim::World world;
    sim::FixedTimestep ts(kDt);
    std::optional<net::Endpoint> client;
    uint32_t tick = 0;

    auto last = std::chrono::steady_clock::now();
    std::array<char, 2048> buf{};

    for (;;)
    {
        std::optional<net::UdpSocket::RecvResult> r = sock.try_recv_from(buf.data(), buf.size());
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - last).count();
        last = now;
        int steps = ts.advance(elapsed);

        if (r)
        {
            client = r->from;
        }

        for (int i = 0; i < steps; ++i)
        {
            world.step(kDt);
            ++tick;
        }

        if (client && steps > 0)
        {
            send_state(sock, *client, world, tick);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
