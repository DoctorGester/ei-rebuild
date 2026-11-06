// The platform's sockets on Linux and macOS (platform.h, "Sockets"): BSD sockets failing as Winsock's did where the
// game relies on it. The harness's builds leave this file out and take their own sockets on 127.0.0.1 and a pair's
// network (tests/verify/platform/game_sockets.cpp; CMakeLists.txt, EI_GAME_CALLS).
#include "platform/platform.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <string>

namespace platform::net {

namespace {

int Fd(Socket s) { return s == kNoSocket ? -1 : static_cast<int>(s); }

sockaddr_in Native(const Address& a) {
    sockaddr_in n{};
#if defined(__APPLE__)
    n.sin_len = sizeof(n);   // BSD's sockaddr carries its length
#endif
    n.sin_family = AF_INET;
    n.sin_port = a.port;
    n.sin_addr.s_addr = a.ip;
    return n;
}

Address Public(const sockaddr_in& n) { return {kIpv4, n.sin_port, n.sin_addr.s_addr, {}}; }

} // namespace

Socket Open(Kind kind) {
    const int fd = socket(AF_INET, kind == Kind::Udp ? SOCK_DGRAM : SOCK_STREAM, 0);
    if (fd < 0) return kNoSocket;
#ifdef SO_NOSIGPIPE
    const int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
#endif
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    return static_cast<Socket>(fd);
}

void Close(Socket s) { close(Fd(s)); }

bool Bind(Socket s, const Address& a) {
    const auto n = Native(a);
    return a.family == kIpv4 && bind(Fd(s), reinterpret_cast<const sockaddr*>(&n), sizeof(n)) == 0;
}

void SetNonBlocking(Socket s, bool on) {
    const int flags = fcntl(Fd(s), F_GETFL);
    if (flags >= 0) fcntl(Fd(s), F_SETFL, on ? flags | O_NONBLOCK : flags & ~O_NONBLOCK);
}

void AllowBroadcast(Socket s) {
    const int yes = 1;
    setsockopt(Fd(s), SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
}

bool Connect(Socket s, const Address& a) {
    const auto n = Native(a);
    return a.family == kIpv4 && connect(Fd(s), reinterpret_cast<const sockaddr*>(&n), sizeof(n)) == 0;
}

int SendTo(Socket s, const void* p, int n, const Address& a) {
    if (n < 0 || a.family != kIpv4) {
        errno = EINVAL;
        return -1;
    }
    const auto to = Native(a);
    return static_cast<int>(sendto(Fd(s), p, n, 0, reinterpret_cast<const sockaddr*>(&to), sizeof(to)));
}

int ReceiveFrom(Socket s, void* p, int n, Address* from) {
    if (n < 0) {
        errno = EINVAL;
        return -1;
    }
    sockaddr_in a{};
    socklen_t len = sizeof(a);
    const int got = static_cast<int>(recvfrom(Fd(s), p, n, 0, reinterpret_cast<sockaddr*>(&a), &len));
    if (got >= 0 && from) *from = Public(a);
    return got;
}

int Send(Socket s, const void* p, int n) {
    if (n < 0) {
        errno = EINVAL;
        return -1;
    }
#ifdef MSG_NOSIGNAL
    return static_cast<int>(send(Fd(s), p, n, MSG_NOSIGNAL));
#else
    return static_cast<int>(send(Fd(s), p, n, 0));   // macOS: the socket's SO_NOSIGPIPE (Open)
#endif
}

int Receive(Socket s, void* p, int n) {
    if (n < 0) {
        errno = EINVAL;
        return -1;
    }
    return static_cast<int>(recv(Fd(s), p, n, 0));
}

Error LastError() {
    switch (errno) {
    case 0:
        return Error::None;
    case EAGAIN:
    case EINPROGRESS:
    case EALREADY:
        return Error::WouldBlock;
    case EADDRINUSE:
        return Error::AddressInUse;
    case ECONNREFUSED:
        return Error::Refused;
    default:
        return Error::Other;
    }
}

int Select(SocketGroup* read, SocketGroup* write, SocketGroup* failed, std::int32_t microseconds) {
    SocketGroup* groups[] = {read, write, failed};
    fd_set sets[3];
    int highest = -1;
    for (int i = 0; i < 3; ++i) {
        FD_ZERO(&sets[i]);
        if (!groups[i]) continue;
        for (int j = 0; j < groups[i]->count; ++j) {
            const int fd = Fd(groups[i]->sockets[j]);
            if (fd < 0 || fd >= FD_SETSIZE) {
                errno = EBADF;
                return -1;
            }
            FD_SET(fd, &sets[i]);
            highest = (std::max)(highest, fd);
        }
    }
    if (highest < 0 || microseconds < 0) {
        errno = EINVAL;
        return -1;
    }
    timeval timeout{microseconds / 1000000, microseconds % 1000000};
    const int ready = select(highest + 1, read ? &sets[0] : nullptr, write ? &sets[1] : nullptr,
                             failed ? &sets[2] : nullptr, &timeout);
    if (ready < 0) return ready;
    for (int i = 0; i < 3; ++i)
        if (groups[i]) {
            int count = 0;
            for (int j = 0; j < groups[i]->count; ++j)
                if (FD_ISSET(Fd(groups[i]->sockets[j]), &sets[i])) groups[i]->sockets[count++] = groups[i]->sockets[j];
            groups[i]->count = count;
        }
    return ready;
}

std::string HostName() {
    char name[256] = {};   // the longest a host name gets, and a terminator gethostname may leave out
    return gethostname(name, sizeof(name) - 1) == 0 ? name : "";
}

std::optional<std::uint32_t> LookUp(const std::string& name) {
    addrinfo hint{};
    hint.ai_family = AF_INET;
    hint.ai_socktype = SOCK_DGRAM;
    addrinfo* found = nullptr;
    if (getaddrinfo(name.c_str(), nullptr, &hint, &found) || !found) return std::nullopt;
    const std::uint32_t ip = reinterpret_cast<sockaddr_in*>(found->ai_addr)->sin_addr.s_addr;
    freeaddrinfo(found);
    return ip;
}

} // namespace platform::net
