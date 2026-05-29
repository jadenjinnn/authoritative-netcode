#include "udp_socket.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>

namespace net
{

    namespace
    {
        [[noreturn]] void fail(const char *what)
        {
            throw std::runtime_error(std::string(what) + ": " + std::strerror(errno));
        }

        sockaddr_in to_sockaddr(const Endpoint &ep)
        {
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(ep.port);
            inet_pton(AF_INET, ep.ip.c_str(), &addr.sin_addr);

            return addr;
        }

        Endpoint from_sockaddr(const sockaddr_in &addr)
        {
            Endpoint ep{};

            char buf[INET_ADDRSTRLEN];

            inet_ntop(AF_INET, &addr.sin_addr, buf, INET_ADDRSTRLEN);

            ep.ip = std::string(buf);
            ep.port = ntohs(addr.sin_port);

            return ep;
        }
    } // namespace

    UdpSocket::UdpSocket()
    {
        fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (fd_ < 0)
        {
            fail("socket");
        }
    }

    UdpSocket::~UdpSocket()
    {
        if (fd_ >= 0)
        {
            ::close(fd_);
        }
    }

    UdpSocket::UdpSocket(UdpSocket &&other) noexcept : fd_(other.fd_)
    {
        other.fd_ = -1;
    }

    UdpSocket &UdpSocket::operator=(UdpSocket &&other) noexcept
    {
        if (this != &other)
        {
            if (fd_ >= 0)
            {
                ::close(fd_);
            }
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    void UdpSocket::set_recv_timeout(int millis)
    {
        timeval tv{};
        tv.tv_sec = millis / 1000;
        tv.tv_usec = (millis % 1000) * 1000;
        if (::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
        {
            fail("setsockopt(SO_RCVTIMEO)");
        }
    }

    void UdpSocket::bind(uint16_t port)
    {
        Endpoint ep{};

        ep.ip = "0.0.0.0";
        ep.port = port;

        sockaddr_in addr = to_sockaddr(ep);

        if (::bind(fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0)
        {
            fail("bind");
        }
    }

    size_t UdpSocket::send_to(const void *data, size_t len, const Endpoint &dest)
    {
        sockaddr_in addr = to_sockaddr(dest);
        ssize_t n = ::sendto(fd_, data, len, 0,
                             reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
        if (n < 0)
        {
            fail("sendto");
        }
        return static_cast<size_t>(n);
    }

    UdpSocket::RecvResult UdpSocket::recv_from(void *buf, size_t cap)
    {
        sockaddr_in src{};
        socklen_t srclen = sizeof(src);
        ssize_t n = ::recvfrom(fd_, buf, cap, 0,
                               reinterpret_cast<sockaddr *>(&src), &srclen);
        if (n < 0)
        {
            fail("recvfrom");
        }
        return RecvResult{static_cast<size_t>(n), from_sockaddr(src)};
    }

    void UdpSocket::set_nonblocking()
    {
        int flags = ::fcntl(fd_, F_GETFL, 0);
        if (flags < 0)
        {
            fail("fcntl(F_GETFL)");
        }
        if (::fcntl(fd_, F_SETFL, flags | O_NONBLOCK) < 0)
        {
            fail("fcntl(F_SETFL)");
        }
    }

    std::optional<UdpSocket::RecvResult> UdpSocket::try_recv_from(void *buf, size_t cap)
    {
        sockaddr_in src{};
        socklen_t srclen = sizeof(src);
        ssize_t n = ::recvfrom(fd_, buf, cap, 0,
                               reinterpret_cast<sockaddr *>(&src), &srclen);
        if (n < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                return std::nullopt;
            }
            fail("recvfrom");
        }
        return RecvResult{static_cast<size_t>(n), from_sockaddr(src)};
    }

    Endpoint UdpSocket::local_endpoint() const
    {
        sockaddr_in addr{};
        socklen_t len = sizeof(addr);
        if (::getsockname(fd_, reinterpret_cast<sockaddr *>(&addr), &len) < 0)
        {
            fail("getsockname");
        }
        return from_sockaddr(addr);
    }

} // namespace net
