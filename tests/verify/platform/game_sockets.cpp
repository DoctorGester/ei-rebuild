// The harness's sockets area (platform.h, "Sockets"): the harness's builds leave the game's
// (src/platform/<system>/sockets.cpp; CMakeLists.txt, EI_GAME_CALLS) out and take this file, one for both systems. The
// game's sockets on 127.0.0.1 only, and a pair's game on the pair's network (harness_net.h, harness_pair.h); opening,
// the socket options, the errors and the host look-ups as the game's own make them.
#define _WINSOCK_DEPRECATED_NO_WARNINGS   // gethostbyname: the look-up the game made on Windows

#include "harness_net.h"
#include "harness_pair.h"
#include "system/harness_sockets.h"

#include "platform/platform.h"

#if defined(_WIN32)
#pragma comment(lib, "ws2_32.lib")
#else
#include <fcntl.h>
#include <netdb.h>
#include <unistd.h>
#endif

#include <cerrno>
#include <cstring>
#include <string>

namespace platform::net {

namespace {

namespace netlocal = ei::verify::netlocal;
namespace pair = ei::verify::pair;
namespace sockets = ei::verify::sockets;

#if defined(_WIN32)
// Winsock 1.1, as the game's: started by the first call that needs it and up until the process ends.
void Start() {
    static const bool started = [] {
        WSADATA data;
        return WSAStartup(0x101, &data) == 0;
    }();
    static_cast<void>(started);
}
#endif

sockaddr_in Native(const Address& address) {
    sockaddr_in native{};
    native.sin_family = AF_INET;
    native.sin_port = address.port;
    native.sin_addr.s_addr = address.ip;
    return native;
}

Address Public(const sockaddr_in& native) { return {kIpv4, native.sin_port, native.sin_addr.s_addr, {}}; }

const sockaddr* Generic(const sockaddr_in& address) { return reinterpret_cast<const sockaddr*>(&address); }

int BindTo(Socket socket, const sockaddr_in& address) {
    return bind(sockets::Of(socket), Generic(address), sizeof(address));
}

int BindLoopback(Socket socket) {
    sockaddr_in loopback{};
    loopback.sin_family = AF_INET;
    loopback.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return BindTo(socket, loopback);
}

bool IsLoopback(std::uint32_t address) { return (ntohl(address) >> 24) == 127; }

int SendNative(Socket socket, const char* data, int length, const sockaddr_in& to) {
    return static_cast<int>(sendto(sockets::Of(socket), data, length, 0, Generic(to), sizeof(to)));
}

} // namespace

// ---- As the game's own ---------------------------------------------------------------------------------------------

Socket Open(Kind kind) {
#if defined(_WIN32)
    Start();
    if (kind == Kind::Udp) return socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    return socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#else
    const int fd = socket(AF_INET, kind == Kind::Udp ? SOCK_DGRAM : SOCK_STREAM, 0);
    if (fd < 0) return kNoSocket;
#ifdef SO_NOSIGPIPE
    const int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
#endif
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    return static_cast<Socket>(fd);
#endif
}

void SetNonBlocking(Socket socket, bool on) {
#if defined(_WIN32)
    u_long value = on ? 1 : 0;
    ioctlsocket(sockets::Of(socket), static_cast<long>(FIONBIO), &value);
#else
    const int flags = fcntl(sockets::Of(socket), F_GETFL);
    if (flags >= 0) fcntl(sockets::Of(socket), F_SETFL, on ? flags | O_NONBLOCK : flags & ~O_NONBLOCK);
#endif
}

void AllowBroadcast(Socket socket) {
#if defined(_WIN32)
    const BOOL on = TRUE;
    setsockopt(sockets::Of(socket), SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&on), sizeof(on));
#else
    const int yes = 1;
    setsockopt(sockets::Of(socket), SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
#endif
}

Error LastError() {
    switch (sockets::LastCode()) {
    case 0: return Error::None;
#if defined(_WIN32)
    case WSAEWOULDBLOCK: return Error::WouldBlock;
    case WSAEADDRINUSE: return Error::AddressInUse;
    case WSAECONNREFUSED: return Error::Refused;
#else
    case EAGAIN:
    case EINPROGRESS:
    case EALREADY: return Error::WouldBlock;
    case EADDRINUSE: return Error::AddressInUse;
    case ECONNREFUSED: return Error::Refused;
#endif
    default: return Error::Other;
    }
}

std::string HostName() {
    char name[256] = {};
#if defined(_WIN32)
    Start();
#endif
    return gethostname(name, sizeof(name) - 1) == 0 ? name : "";
}

std::optional<std::uint32_t> LookUp(const std::string& name) {
#if defined(_WIN32)
    Start();
    const hostent* host = gethostbyname(name.c_str());
    if (!host || !host->h_addr_list[0]) return std::nullopt;
    std::uint32_t ip;
    std::memcpy(&ip, host->h_addr_list[0], sizeof(ip));
    return ip;
#else
    addrinfo hint{};
    hint.ai_family = AF_INET;
    hint.ai_socktype = SOCK_DGRAM;
    addrinfo* found = nullptr;
    if (getaddrinfo(name.c_str(), nullptr, &hint, &found) || !found) return std::nullopt;
    const std::uint32_t ip = reinterpret_cast<sockaddr_in*>(found->ai_addr)->sin_addr.s_addr;
    freeaddrinfo(found);
    return ip;
#endif
}

// ---- The harness's answers ---------------------------------------------------------------------------------------

void Close(Socket socket) {
    if (netlocal::Paired()) pair::Close(socket);
    sockets::Close(socket);
}

// Every address (INADDR_ANY) becomes 127.0.0.1, the port as asked. A pair's game: the port asked is a virtual port
// on the pair's network (harness_pair.h); the socket itself is bound on 127.0.0.1, any port.
bool Bind(Socket socket, const Address& address) {
    sockaddr_in local = Native(address);
    if (!netlocal::Loopback()) return BindTo(socket, local) == 0;
    if (netlocal::Paired() && netlocal::IsUdp(socket)) {
        if (!pair::Bind(socket, ntohs(local.sin_port))) {
            sockets::SetError(sockets::Error::AddressInUse);
            return false;
        }
        const int result = BindLoopback(socket);
        const int error = sockets::LastCode();
        netlocal::LogBind(socket, "the game's, on the pair's network", result);
        if (result != 0) pair::Close(socket);
        sockets::SetCode(error);
        return result == 0;
    }
    const char* why = "the game's";
    if (local.sin_addr.s_addr == htonl(INADDR_ANY)) {
        local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        why = "the game's, asked on every address";
    }
    const int result = BindTo(socket, local);
    const int error = sockets::LastCode();
    netlocal::LogBind(socket, why, result);
    sockets::SetCode(error);
    return result == 0;
}

// A socket not bound yet is bound to 127.0.0.1 first (the system would bind it on every address); a destination
// outside loopback (the lobby's broadcast) becomes 127.0.0.1 on the same port; a pair's game sends on the pair's
// network.
int SendTo(Socket socket, const void* data, int length, const Address& to) {
    sockaddr_in destination = Native(to);
    const char* bytes = static_cast<const char*>(data);
    if (!netlocal::Loopback()) return SendNative(socket, bytes, length, destination);
    if (sockets::Unbound(socket))
        netlocal::LogBind(socket, "before its first datagram, which would bind it on every address", BindLoopback(socket));
    if (netlocal::Paired() && netlocal::IsUdp(socket)) {
        const bool broadcast = (ntohl(destination.sin_addr.s_addr) & 0xFF) == 0xFF;
        return pair::SendTo(socket, bytes, length, ntohs(destination.sin_port), broadcast);   // the pair's network
    }
    if (!IsLoopback(destination.sin_addr.s_addr)) {
        netlocal::Redirected(destination.sin_addr.s_addr, destination.sin_port);
        destination.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    }
    return SendNative(socket, bytes, length, destination);
}

// A pair's game: its UDP sockets read the pair's mailbox.
int ReceiveFrom(Socket socket, void* data, int length, Address* from) {
    sockaddr_in sender{};
    int received;
    if (netlocal::Paired() && netlocal::IsUdp(socket)) {
        received = pair::RecvFrom(socket, static_cast<char*>(data), length, &sender);
    } else {
        sockets::Length senderLength = sizeof(sender);
        received = static_cast<int>(recvfrom(sockets::Of(socket), static_cast<char*>(data), length, 0,
                                             reinterpret_cast<sockaddr*>(&sender), &senderLength));
    }
    if (received >= 0 && from) *from = Public(sender);
    return received;
}

// A pair's game: a TCP link to the fake master is the harness's (SV5d4).
bool Connect(Socket socket, const Address& address) {
    const sockaddr_in native = Native(address);
    if (netlocal::Paired() && !netlocal::IsUdp(socket) && pair::MasterConnect(socket, ntohs(native.sin_port))) {
        sockets::SetError(sockets::Error::WouldBlock);   // as a nonblocking connect under way
        return false;
    }
    return connect(sockets::Of(socket), Generic(native), sizeof(native)) == 0;
}

int Send(Socket socket, const void* data, int length) {
    if (netlocal::Paired() && pair::MasterSend(socket, static_cast<const char*>(data), length)) return length;
#if defined(_WIN32)
    constexpr int kFlags = 0;
#else
    constexpr int kFlags = MSG_NOSIGNAL;
#endif
    return static_cast<int>(send(sockets::Of(socket), static_cast<const char*>(data), length, kFlags));
}

int Receive(Socket socket, void* data, int length) {
    int received = 0;
    if (netlocal::Paired() && pair::MasterRecv(socket, static_cast<char*>(data), length, &received)) return received;
    return static_cast<int>(recv(sockets::Of(socket), static_cast<char*>(data), length, 0));
}

int Select(SocketGroup* read, SocketGroup* write, SocketGroup* failed, std::int32_t microseconds) {
    int ready = 0;
    if (netlocal::Paired() && pair::MasterSelect(read, write, failed, &ready)) return ready;
    return sockets::Select(read, write, failed, microseconds);
}

} // namespace platform::net
