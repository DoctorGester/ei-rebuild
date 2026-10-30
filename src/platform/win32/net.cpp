// Sockets on Windows (platform/net.h): Winsock 1.1 through game.exe's import slots (WSOCK32), which the harness
// routes (binds and datagrams kept on 127.0.0.1, a pair's network: tests/verify/harness_net.h).
#include <winsock2.h>   // before anything includes windows.h (and with it winsock.h)

#include "platform/net.h"

#include "lib/gameapi.h"

#include <cstring>

#pragma comment(lib, "ws2_32.lib")   // WSAGetLastError, which game.exe does not import

namespace platform::net {

namespace {

static_assert(sizeof(Address) == sizeof(sockaddr_in) && sizeof(Socket) == sizeof(SOCKET));
static_assert(SocketGroup::kCapacity == FD_SETSIZE);

constexpr int kAddressLength = sizeof(sockaddr_in);

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

bool Start() {
    WSADATA data;
    return gameapi::WSAStartup(0x101, &data) == 0;
}

void Stop() { gameapi::WSACleanup(); }

Socket Open(Kind kind) {
    if (kind == Kind::Udp) return gameapi::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    return gameapi::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
}

void Close(Socket socket) { gameapi::closesocket(socket); }

bool Bind(Socket socket, const Address& address) { return gameapi::bind(socket, &address, kAddressLength) == 0; }

void SetNonBlocking(Socket socket, bool on) {
    std::uint32_t value = on ? 1 : 0;
    gameapi::ioctlsocket(socket, static_cast<long>(FIONBIO), &value);
}

void AllowBroadcast(Socket socket) {
    const std::uint32_t on = 1;
    gameapi::setsockopt(socket, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));
}

bool Connect(Socket socket, const Address& address) {
    return gameapi::connect(socket, &address, kAddressLength) == 0;
}

int SendTo(Socket socket, const void* data, int length, const Address& to) {
    return gameapi::sendto(socket, data, length, 0, &to, kAddressLength);
}

int ReceiveFrom(Socket socket, void* data, int length, Address* from) {
    int fromLength = kAddressLength;
    return gameapi::recvfrom(socket, data, length, 0, from, &fromLength);
}

int Send(Socket socket, const void* data, int length) { return gameapi::send(socket, data, length, 0); }

int Receive(Socket socket, void* data, int length) { return gameapi::recv(socket, data, length, 0); }

Error LastError() {
    switch (WSAGetLastError()) {
    case 0: return Error::None;
    case WSAEWOULDBLOCK: return Error::WouldBlock;
    case WSAEADDRINUSE: return Error::AddressInUse;
    case WSAECONNREFUSED: return Error::Refused;
    default: return Error::Other;
    }
}

int Select(SocketGroup* read, SocketGroup* write, SocketGroup* failed, std::int32_t microseconds) {
    fd_set sets[3];
    const timeval timeout = {microseconds / 1000000, microseconds % 1000000};
    const int ready = gameapi::select(0, ToNative(read, &sets[0]), ToNative(write, &sets[1]),
                                      ToNative(failed, &sets[2]), &timeout);
    FromNative(sets[0], read);
    FromNative(sets[1], write);
    FromNative(sets[2], failed);
    return ready;
}

bool HostName(char* name, int size) { return gameapi::gethostname(name, size) == 0; }

bool LookUp(const char* name, std::uint32_t* ip) {
    const auto* host = static_cast<const hostent*>(gameapi::gethostbyname(name));
    if (!host || !host->h_addr_list[0]) return false;
    std::memcpy(ip, host->h_addr_list[0], sizeof(*ip));
    return true;
}

std::string HostNameAt(const void* address, int length) {
    const auto* host = static_cast<const hostent*>(gameapi::gethostbyaddr(address, length, AF_INET));
    if (!host || !host->h_name) return {};
    return host->h_name;
}

std::uint32_t ParseIpv4(const char* text) { return gameapi::inet_addr(text); }

std::uint16_t ToNetwork(std::uint16_t value) { return gameapi::htons(value); }

std::uint16_t FromNetwork(std::uint16_t value) { return gameapi::ntohs(value); }

} // namespace platform::net
