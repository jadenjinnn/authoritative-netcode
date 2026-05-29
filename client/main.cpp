#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>

#include "snapshot.h"
#include "udp_socket.h"

int main(int argc, char** argv)
{
    net::Endpoint server;
    server.ip = (argc > 1) ? argv[1] : "127.0.0.1";
    server.port = (argc > 2) ? static_cast<uint16_t>(std::atoi(argv[2])) : 9999;

    net::UdpSocket sock;
    sock.bind(0);
    sock.set_recv_timeout(2000);

    const char* join = "join";
    sock.send_to(join, std::strlen(join), server);
    std::printf("joined %s:%u — listening for state\n", server.ip.c_str(), server.port);

    constexpr int kFrames = 120;
    std::array<char, 2048> buf{};
    for (int i = 0; i < kFrames; ++i)
    {
        try
        {
            auto r = sock.recv_from(buf.data(), buf.size());
            if (r.bytes >= sizeof(sim::Snapshot))
            {
                sim::Snapshot snap{};
                std::memcpy(&snap, buf.data(), sizeof(snap));
                std::printf("tick %5u  pos = (%.2f, %.2f)\n", snap.tick, snap.x, snap.y);
            }
        }
        catch (const std::exception&)
        {
            std::printf("no state (timeout)\n");
            break;
        }
    }
    return 0;
}
