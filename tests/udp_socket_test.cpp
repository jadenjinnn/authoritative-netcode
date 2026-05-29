#include <array>
#include <string>

#include <gtest/gtest.h>

#include "udp_socket.h"

TEST(UdpSocket, LoopbackRoundTrip)
{
    net::UdpSocket server;
    server.bind(0);
    server.set_recv_timeout(1000);
    net::Endpoint server_addr = server.local_endpoint();
    server_addr.ip = "127.0.0.1";

    net::UdpSocket client;
    client.bind(0);
    client.set_recv_timeout(1000);

    const std::string msg = "hello-udp";
    client.send_to(msg.data(), msg.size(), server_addr);

    std::array<char, 64> sbuf{};
    auto in = server.recv_from(sbuf.data(), sbuf.size());
    ASSERT_EQ(in.bytes, msg.size());
    server.send_to(sbuf.data(), in.bytes, in.from);

    std::array<char, 64> cbuf{};
    auto back = client.recv_from(cbuf.data(), cbuf.size());
    ASSERT_EQ(back.bytes, msg.size());
    EXPECT_EQ(std::string(cbuf.data(), back.bytes), msg);
}
