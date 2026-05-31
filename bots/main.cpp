// Headless bot swarm: spawns N bots in one process, each driving a Peer to the
// authoritative server. Every bot sends an unreliable Input each tick (its
// BotDriver decides the movement) plus an occasional reliable event, and receives
// the server's full-state broadcasts. The point is load: run it at increasing N to
// drive the server and read its egress baseline.
//
// Usage: bots [count] [server_ip] [server_port] [seconds]

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <thread>
#include <vector>

#include "bot.h"
#include "input.h"
#include "peer.h"
#include "snapshot.h"
#include "udp_socket.h"

namespace
{

    constexpr uint64_t kTickUs = 16'667;  // ~60 Hz input cadence

    uint64_t now_us()
    {
        return std::chrono::duration_cast<std::chrono::microseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    struct Bot
    {
        std::unique_ptr<net::UdpSocket> sock;
        std::unique_ptr<reliable::Peer> peer;
        bots::BotDriver driver;
        uint32_t event_seq = 0;
        uint64_t snapshots_recv = 0;

        explicit Bot(uint32_t seed) : driver(seed) {}
    };

} // namespace

int main(int argc, char **argv)
{
    int count = (argc > 1) ? std::atoi(argv[1]) : 10;
    net::Endpoint server;
    server.ip = (argc > 2) ? argv[2] : "127.0.0.1";
    server.port = (argc > 3) ? static_cast<uint16_t>(std::atoi(argv[3])) : 9999;
    double seconds = (argc > 4) ? std::atof(argv[4]) : 10.0;

    std::vector<std::unique_ptr<Bot>> swarm;
    swarm.reserve(count);
    for (int i = 0; i < count; ++i)
    {
        auto bot = std::make_unique<Bot>(static_cast<uint32_t>(i + 1));
        bot->sock = std::make_unique<net::UdpSocket>();
        bot->sock->bind(0);
        bot->sock->set_nonblocking();
        bot->peer = std::make_unique<reliable::Peer>(*bot->sock, server);
        bot->peer->queue_reliable(sim::encode_input(sim::Input{}));  // join marker
        swarm.push_back(std::move(bot));
    }
    std::printf("spawned %d bots -> %s:%u for %.0fs\n", count, server.ip.c_str(),
                server.port, seconds);

    uint64_t start = now_us();
    uint64_t deadline = start + static_cast<uint64_t>(seconds * 1'000'000);
    uint64_t next_tick = start;

    while (now_us() < deadline)
    {
        uint64_t now = now_us();
        if (now >= next_tick)
        {
            for (auto &bot : swarm)
            {
                bots::BotDriver::Emit e = bot->driver.tick(now);
                bot->peer->queue_unreliable(sim::encode_input(e.input));
                if (e.event)
                {
                    bot->peer->queue_reliable(sim::encode_input(sim::Input{}));
                }
            }
            next_tick += kTickUs;
        }

        for (auto &bot : swarm)
        {
            bot->peer->update(now);
            reliable::Peer::Delivery d = bot->peer->poll(now);
            bot->snapshots_recv += d.unreliable.size();
        }

        std::this_thread::sleep_for(std::chrono::microseconds(500));
    }

    uint64_t total_snaps = 0;
    uint64_t total_bytes = 0;
    for (const auto &bot : swarm)
    {
        total_snaps += bot->snapshots_recv;
        total_bytes += bot->peer->bytes_sent();
    }
    std::printf("done: %llu snapshots received across %d bots; %llu bytes sent upstream\n",
                static_cast<unsigned long long>(total_snaps), count,
                static_cast<unsigned long long>(total_bytes));
    return 0;
}
