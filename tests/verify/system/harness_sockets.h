#pragma once

// The BSD socket calls the harness's network parts make (harness_net.cpp, harness_pair.cpp, the platform backend's
// socket answers), on Winsock or a POSIX system: the headers, and the few spots where the two differ.

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "platform/platform.h"

#include <cstdint>

namespace ei::verify::sockets {

#if defined(_WIN32)
using Native = SOCKET;
using Length = int;
#else
using Native = int;
using Length = socklen_t;
#endif

inline Native Of(std::uintptr_t socket) { return static_cast<Native>(socket); }

inline void Close(std::uintptr_t socket) {
#if defined(_WIN32)
    closesocket(Of(socket));
#else
    close(Of(socket));
#endif
}

// The errors the harness's answers report as the system's (platform::net::LastError reads them).
enum class Error { None, WouldBlock, AddressInUse, MessageSize, Invalid };

inline void SetError(Error error) {
#if defined(_WIN32)
    const int codes[] = {0, WSAEWOULDBLOCK, WSAEADDRINUSE, WSAEMSGSIZE, WSAEINVAL};
    WSASetLastError(codes[static_cast<int>(error)]);
#else
    const int codes[] = {0, EWOULDBLOCK, EADDRINUSE, EMSGSIZE, EINVAL};
    errno = codes[static_cast<int>(error)];
#endif
}

// The system's last socket error code, as it is, and kept again (logging must not lose it).
inline int LastCode() {
#if defined(_WIN32)
    return WSAGetLastError();
#else
    return errno;
#endif
}
inline void SetCode(int code) {
#if defined(_WIN32)
    WSASetLastError(code);
#else
    errno = code;
#endif
}

// Whether `socket` is bound to no address yet (Winsock's getsockname fails then; elsewhere it names port 0).
inline bool Unbound(std::uintptr_t socket) {
    sockaddr_in local{};
    Length length = sizeof(local);
    if (getsockname(Of(socket), reinterpret_cast<sockaddr*>(&local), &length) != 0) {
#if defined(_WIN32)
        return WSAGetLastError() == WSAEINVAL;
#else
        return false;
#endif
    }
    return local.sin_port == 0;
}

// select's call on the game's socket groups (each group left with the sockets that are ready; null for none).
inline int Select(platform::net::SocketGroup* read, platform::net::SocketGroup* write, platform::net::SocketGroup* failed,
                  std::int32_t microseconds) {
    platform::net::SocketGroup* groups[] = {read, write, failed};
    fd_set sets[3];
    int highest = -1;
    for (int i = 0; i < 3; ++i) {
        FD_ZERO(&sets[i]);
        if (!groups[i]) continue;
        for (int j = 0; j < groups[i]->count; ++j) {
            const Native s = Of(groups[i]->sockets[j]);
#if !defined(_WIN32)
            if (s < 0 || s >= FD_SETSIZE) {
                errno = EBADF;
                return -1;
            }
            if (s > highest) highest = s;
#endif
            FD_SET(s, &sets[i]);
        }
    }
    timeval timeout{microseconds / 1000000, microseconds % 1000000};
    const int ready = select(highest + 1, read ? &sets[0] : nullptr, write ? &sets[1] : nullptr, failed ? &sets[2] : nullptr,
                             &timeout);
    if (ready < 0) return ready;
    for (int i = 0; i < 3; ++i) {
        if (!groups[i]) continue;
        int count = 0;
        for (int j = 0; j < groups[i]->count; ++j)
            if (FD_ISSET(Of(groups[i]->sockets[j]), &sets[i])) groups[i]->sockets[count++] = groups[i]->sockets[j];
        groups[i]->count = count;
    }
    return ready;
}

// Starts the system's sockets for the harness's own probes (Winsock; nothing elsewhere); Stop ends them.
inline bool Start() {
#if defined(_WIN32)
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
#else
    return true;
#endif
}
inline void Stop() {
#if defined(_WIN32)
    WSACleanup();
#endif
}

} // namespace ei::verify::sockets
