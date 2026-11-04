// The platform's sockets on Windows (platform.h, "Sockets"): Winsock 1.1 (ws2_32). The harness's builds leave this file
// out and take their own sockets on 127.0.0.1 and a pair's network (tests/verify/platform/game_sockets.cpp;
// CMakeLists.txt, EI_GAME_CALLS).
#define _WINSOCK_DEPRECATED_NO_WARNINGS   // gethostbyname: the look-up the game made
#include <winsock2.h>   // before anything includes windows.h (and with it winsock.h)

#include "platform/platform.h"

#include <cstring>
#include <string>

#pragma comment(lib, "ws2_32.lib")

namespace platform::net {

namespace {

static_assert(sizeof(Address) == sizeof(sockaddr_in) && sizeof(Socket) == sizeof(SOCKET));
static_assert(SocketGroup::kCapacity == FD_SETSIZE);

constexpr int kAddressLength = sizeof(sockaddr_in);

// Winsock 1.1, started by the first call that needs it and up until the process ends.
void Start() {
    static const bool started = [] {
        WSADATA data;
        return WSAStartup(0x101, &data) == 0;
    }();
    static_cast<void>(started);
}

const sockaddr* Native(const Address& address) { return reinterpret_cast<const sockaddr*>(&address); }

// `set`'s sockets as an fd_set (or null for none).
fd_set* ToNative(const SocketGroup* set, fd_set* native) {
    if (!set) return nullptr;
    native->fd_count = static_cast<u_int>(set->count);
    for (int i = 0; i < set->count; ++i) native->fd_array[i] = set->sockets[i];
    return native;
}

// The sockets select left in `native` back into `set`.
void FromNative(const fd_set& native, SocketGroup* set) {
    if (!set) return;
    set->count = static_cast<int>(native.fd_count);
    for (int i = 0; i < set->count; ++i) set->sockets[i] = native.fd_array[i];
}

} // namespace

Socket Open(Kind kind) {
    Start();
    if (kind == Kind::Udp) return socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    return socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
}

void Close(Socket socket) { closesocket(socket); }

bool Bind(Socket socket, const Address& address) { return bind(socket, Native(address), kAddressLength) == 0; }

void SetNonBlocking(Socket socket, bool on) {
    u_long value = on ? 1 : 0;
    ioctlsocket(socket, static_cast<long>(FIONBIO), &value);
}

void AllowBroadcast(Socket socket) {
    const BOOL on = TRUE;
    setsockopt(socket, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&on), sizeof(on));
}

bool Connect(Socket socket, const Address& address) {
    return connect(socket, Native(address), kAddressLength) == 0;
}

int SendTo(Socket socket, const void* data, int length, const Address& to) {
    return sendto(socket, static_cast<const char*>(data), length, 0, Native(to), kAddressLength);
}

int ReceiveFrom(Socket socket, void* data, int length, Address* from) {
    int fromLength = kAddressLength;
    return recvfrom(socket, static_cast<char*>(data), length, 0, reinterpret_cast<sockaddr*>(from), &fromLength);
}

int Send(Socket socket, const void* data, int length) { return send(socket, static_cast<const char*>(data), length, 0); }

int Receive(Socket socket, void* data, int length) { return recv(socket, static_cast<char*>(data), length, 0); }

Error LastError() {
    switch (WSAGetLastError()) {
    case 0: return Error::None;
    case WSAEWOULDBLOCK: return Error::WouldBlock;
    case WSAEADDRINUSE: return Error::AddressInUse;
    case WSAECONNREFUSED: return Error::Refused;
    default: return Error::Other;
    }
}

// The count is ignored by Winsock.
int Select(SocketGroup* read, SocketGroup* write, SocketGroup* failed, std::int32_t microseconds) {
    fd_set sets[3];
    const timeval timeout = {microseconds / 1000000, microseconds % 1000000};
    const int ready = select(0, ToNative(read, &sets[0]), ToNative(write, &sets[1]), ToNative(failed, &sets[2]), &timeout);
    FromNative(sets[0], read);
    FromNative(sets[1], write);
    FromNative(sets[2], failed);
    return ready;
}

std::string HostName() {
    Start();
    char name[256];   // the longest a host name gets
    return gethostname(name, sizeof(name)) == 0 ? name : "";
}

std::optional<std::uint32_t> LookUp(const char* name) {
    Start();
    const hostent* host = gethostbyname(name);
    if (!host || !host->h_addr_list[0]) return std::nullopt;
    std::uint32_t ip;
    std::memcpy(&ip, host->h_addr_list[0], sizeof(ip));
    return ip;
}

} // namespace platform::net
