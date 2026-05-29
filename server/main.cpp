#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "udp_socket.h"

int main(int argc, char** argv)
{
    uint16_t port = (argc > 1) ? static_cast<uint16_t>(std::atoi(argv[1])) : 9999;

    net::UdpSocket sock;
    sock.bind(port);
    std::printf("echo server listening on :%u\n", port);

    std::array<char, 2048> buf{};
    for (;;)
    {
        auto r = sock.recv_from(buf.data(), buf.size());
        sock.send_to(buf.data(), r.bytes, r.from);
    }
}
