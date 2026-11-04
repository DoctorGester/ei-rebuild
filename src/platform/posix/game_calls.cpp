// The platform calls a harness run answers itself (its window and desktop answers, its file write
// watch, its sockets on 127.0.0.1): the harness's builds leave this file out and take its platform backend's
// definitions of the same functions (tests/verify/platform; CMakeLists.txt; docs/notes/platform.md, "The harness's
// answers"). Each is the system's call. The paths of the file calls go through one mapping of their own
// (src/platform/paths.h, system_path.cpp).
#include "platform/platform.h"

#include "platform/posix/platform_posix.h"

#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <string>

// ==== Files ==========================================================================================================

namespace platform {

// platform_posix.cpp's file handles are the descriptor + 1.
void File::Write(const void* buffer, unsigned count) {
    const int fd = handle ? static_cast<int>(reinterpret_cast<std::intptr_t>(handle) - 1) : -1;
    const auto* bytes = static_cast<const char*>(buffer);
    while (count) {
        const ssize_t n = write(fd, bytes, count);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) throw FileError(n ? errno : ENOSPC, path);
        bytes += n;
        count -= static_cast<unsigned>(n);
    }
}

} // namespace platform

// ==== The desktop and the other game window ==========================================================================

namespace platform {

bool FontSmoothing() { return true; }

bool BringOtherInstanceForward(const char* name, const char* title) { return posix::OtherInstanceRunning(name, title); }

} // namespace platform

// ==== Sockets ========================================================================================================

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

void Close(Socket s) { close(Fd(s)); }

bool Bind(Socket s, const Address& a) {
    const auto n = Native(a);
    return a.family == kIpv4 && bind(Fd(s), reinterpret_cast<const sockaddr*>(&n), sizeof(n)) == 0;
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

} // namespace platform::net
