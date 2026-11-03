// The network of a harness game: a UDP port of its own, every socket on 127.0.0.1 (harness_net.h).
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>

#include "harness_net.h"
#include "harness_pair.h"

#include "../../src/platform/log.h"
#include "image/image_side.h"
#include "platform/platform.h"
#include "platform/win32/platform_win32.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

namespace ei::verify::netlocal {

namespace {

constexpr int kFirstPort = 20000;                   // below the dynamic range (49152 and up)
constexpr int kPortCount = 20000;

// Whether the game's binds and datagrams stay on 127.0.0.1 (Install), and whether it plays on a pair's network.
bool g_loopback = false;
bool g_pair = false;

// What the binds and the endpoint checks saw (the game's thread binds, the watchdog's checks).
SRWLOCK g_lock = SRWLOCK_INIT;
unsigned g_binds;
unsigned g_scans;
std::vector<std::uint32_t> g_redirected;   // destinations sent to 127.0.0.1 instead (network order), each logged once

// What the endpoint checks keep: the system's tables (grown as asked) and the endpoints found outside loopback. Never
// destroyed: the watchdog may be in a check (Scan) while the process exits and destroys its static objects.
struct Scans {
    std::vector<std::uint8_t> udp, tcp;
    std::vector<std::string> outside;   // "UDP 0.0.0.0:20123", each once
};
Scans& TheScans() {
    static Scans* const scans = new Scans;
    return *scans;
}

bool IsLoopback(std::uint32_t address) { return (ntohl(address) >> 24) == 127; }

std::string Endpoint(const char* kind, std::uint32_t address, std::uint16_t port) {
    const auto* b = reinterpret_cast<const std::uint8_t*>(&address);
    char text[64];
    std::snprintf(text, sizeof(text), "%s %u.%u.%u.%u:%u", kind, b[0], b[1], b[2], b[3], ntohs(port));
    return text;
}

// Records an endpoint outside loopback (once) and logs it.
void Outside(const std::string& endpoint, const char* how) {
    AcquireSRWLockExclusive(&g_lock);
    bool known = false;
    std::vector<std::string>& outside = TheScans().outside;
    for (const std::string& seen : outside) known = known || seen == endpoint;
    if (!known) outside.push_back(endpoint);
    ReleaseSRWLockExclusive(&g_lock);
    if (!known) Log("harness: net: %s is bound outside loopback (%s)", endpoint.c_str(), how);
}

const char* Kind(SOCKET s) {
    int type = 0;
    int length = sizeof(type);
    if (getsockopt(s, SOL_SOCKET, SO_TYPE, reinterpret_cast<char*>(&type), &length) != 0) return "socket";
    return type == SOCK_DGRAM ? "UDP" : type == SOCK_STREAM ? "TCP" : "socket";
}

bool IsUdp(SOCKET s) {
    int type = 0;
    int length = sizeof(type);
    return getsockopt(s, SOL_SOCKET, SO_TYPE, reinterpret_cast<char*>(&type), &length) == 0 && type == SOCK_DGRAM;
}

// The address a bind gave the socket, logged (every bind: a game makes a few).
void LogBind(SOCKET s, const char* why, int result) {
    sockaddr_in bound{};
    int length = sizeof(bound);
    if (result != 0 || getsockname(s, reinterpret_cast<sockaddr*>(&bound), &length) != 0 || bound.sin_family != AF_INET) {
        Log("harness: net: %s %s bind failed (error %d)", Kind(s), why, WSAGetLastError());
        return;
    }
    const std::string endpoint = Endpoint(Kind(s), bound.sin_addr.s_addr, bound.sin_port);
    AcquireSRWLockExclusive(&g_lock);
    ++g_binds;
    ReleaseSRWLockExclusive(&g_lock);
    Log("harness: net: bind %s (%s)", endpoint.c_str(), why);
    if (!IsLoopback(bound.sin_addr.s_addr)) Outside(endpoint, why);
}

// A datagram to `address` (outside loopback, network order) sent to 127.0.0.1 instead: logged once each.
void Redirected(std::uint32_t address, std::uint16_t port) {
    AcquireSRWLockExclusive(&g_lock);
    bool known = false;
    for (std::uint32_t seen : g_redirected) known = known || seen == address;
    if (!known) g_redirected.push_back(address);
    ReleaseSRWLockExclusive(&g_lock);
    if (!known) Log("harness: net: datagrams to %s go to 127.0.0.1 instead", Endpoint("UDP", address, port).c_str());
}

// True when a UDP socket can bind `port` on 127.0.0.1 (as SocketDriver::Open 004409A0 binds in harness runs).
bool PortFree(int port) {
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return false;
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons(static_cast<u_short>(port));
    bool bound = bind(s, reinterpret_cast<const sockaddr*>(&a), sizeof(a)) == 0;
    closesocket(s);
    return bound;
}

void InstallPort() {
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        Log("harness: Winsock did not start; the game keeps its UDP port");
        return;
    }
    // A start that differs per process, so games starting together probe different ports.
    int offset = static_cast<int>((GetCurrentProcessId() / 4) % kPortCount);
    int port = 0;
    for (int i = 0; i < 256 && port == 0; ++i) {
        int candidate = kFirstPort + (offset + i * 7) % kPortCount;
        if (PortFree(candidate)) port = candidate;
    }
    WSACleanup();
    if (port == 0) {
        Log("harness: no free UDP port found; the game keeps its own");
        return;
    }
    int* target = &image::GamePort();
    int before = *target;
    DWORD old;
    if (!VirtualProtect(target, sizeof(int), PAGE_READWRITE, &old)) return;
    *target = port;
    VirtualProtect(target, sizeof(int), old, &old);
    Log("harness: the game's UDP port is %d (was %d): one per game, so parallel runs do not meet", port,
        before);
}

// The table GetExtendedUdpTable / GetExtendedTcpTable fills (grown as asked).
template <class Get>
bool Table(std::vector<std::uint8_t>& buffer, Get get) {
    for (int tries = 0; tries < 4; ++tries) {
        DWORD size = static_cast<DWORD>(buffer.size());
        const DWORD result = get(buffer.empty() ? nullptr : buffer.data(), &size);
        if (result == NO_ERROR) return true;
        if (result != ERROR_INSUFFICIENT_BUFFER) return false;
        buffer.resize(size + 4096);
    }
    return false;
}

} // namespace

void Install() {
    // The pair's network has ports of its own: the game keeps its 8888 (harness_pair.h). Its datagrams and the master
    // link's (MasterLink 00440C60 / 00440DE0, SV5d4) go through the pair.
    g_pair = pair::Install();
    if (!g_pair) InstallPort();
    g_loopback = true;
    Log("harness: net: the game's binds and datagrams stay on 127.0.0.1");
}

void Scan() {
    static SRWLOCK scanning = SRWLOCK_INIT;
    AcquireSRWLockExclusive(&scanning);
    std::vector<std::uint8_t>& udp = TheScans().udp;   // the watchdog's, and the report's after it stopped scanning
    std::vector<std::uint8_t>& tcp = TheScans().tcp;
    const DWORD self = GetCurrentProcessId();
    if (Table(udp, [](void* t, DWORD* size) { return GetExtendedUdpTable(t, size, FALSE, AF_INET, UDP_TABLE_OWNER_PID, 0); })) {
        const auto* table = reinterpret_cast<const MIB_UDPTABLE_OWNER_PID*>(udp.data());
        for (DWORD i = 0; i < table->dwNumEntries; ++i) {
            const MIB_UDPROW_OWNER_PID& row = table->table[i];
            if (row.dwOwningPid == self && !IsLoopback(row.dwLocalAddr))
                Outside(Endpoint("UDP", row.dwLocalAddr, static_cast<std::uint16_t>(row.dwLocalPort)), "found by the endpoint check");
        }
    }
    if (Table(tcp, [](void* t, DWORD* size) {
            return GetExtendedTcpTable(t, size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0);
        })) {
        const auto* table = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(tcp.data());
        for (DWORD i = 0; i < table->dwNumEntries; ++i) {
            const MIB_TCPROW_OWNER_PID& row = table->table[i];
            if (row.dwOwningPid == self && !IsLoopback(row.dwLocalAddr))
                Outside(Endpoint("TCP listener", row.dwLocalAddr, static_cast<std::uint16_t>(row.dwLocalPort)),
                        "found by the endpoint check");
        }
    }
    AcquireSRWLockExclusive(&g_lock);
    ++g_scans;
    ReleaseSRWLockExclusive(&g_lock);
    ReleaseSRWLockExclusive(&scanning);
}

void Report(FILE* f) {
    Scan();
    AcquireSRWLockShared(&g_lock);
    const std::vector<std::string>& outside = TheScans().outside;
    if (outside.empty()) {
        std::fprintf(f, "\nNET: loopback only (%u binds, %u endpoint checks: nothing bound outside 127.0.0.1)\n", g_binds,
                     g_scans);
    } else {
        std::string list;
        for (const std::string& endpoint : outside) list += (list.empty() ? "" : ", ") + endpoint;
        std::fprintf(f, "\nNET: outside loopback: %s\n", list.c_str());
    }
    ReleaseSRWLockShared(&g_lock);
}

} // namespace ei::verify::netlocal

// ==== The game's socket calls ========================================================================================

// What game_calls.cpp's calls are in the game's builds, kept on 127.0.0.1 and a pair's network once Install ran.

namespace platform::net {

namespace {

using namespace ei::verify::netlocal;
namespace pair = ei::verify::pair;

constexpr int kAddressLength = sizeof(sockaddr_in);

sockaddr_in Native(const Address& address) {
    sockaddr_in native;
    std::memcpy(&native, &address, sizeof(native));
    return native;
}

const sockaddr* Generic(const sockaddr_in& address) { return reinterpret_cast<const sockaddr*>(&address); }

int BindLoopback(SOCKET s) {
    sockaddr_in loopback{};
    loopback.sin_family = AF_INET;
    loopback.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return bind(s, Generic(loopback), kAddressLength);
}

} // namespace

// Every address (INADDR_ANY) becomes 127.0.0.1, the port as asked. A pair's game: the port asked is a virtual port
// on the pair's network (harness_pair.h); the socket itself is bound on 127.0.0.1, any port.
bool Bind(Socket socket, const Address& address) {
    sockaddr_in local = Native(address);
    if (!g_loopback) return bind(socket, Generic(local), kAddressLength) == 0;
    if (g_pair && local.sin_family == AF_INET && IsUdp(socket)) {
        if (!pair::Bind(socket, ntohs(local.sin_port))) {
            WSASetLastError(WSAEADDRINUSE);
            return false;
        }
        const int result = BindLoopback(socket);
        const int error = WSAGetLastError();
        LogBind(socket, "the game's, on the pair's network", result);
        if (result != 0) pair::Close(socket);
        WSASetLastError(error);
        return result == 0;
    }
    const char* why = "the game's";
    if (local.sin_family == AF_INET && local.sin_addr.s_addr == htonl(INADDR_ANY)) {
        local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        why = "the game's, asked on every address";
    }
    const int result = bind(socket, Generic(local), kAddressLength);
    const int error = WSAGetLastError();
    LogBind(socket, why, result);
    WSASetLastError(error);
    return result == 0;
}

// A socket not bound yet is bound to 127.0.0.1 first (Winsock would bind it on every address); a destination outside
// loopback (the lobby's broadcast) becomes 127.0.0.1 on the same port; a pair's game sends on the pair's network.
int SendTo(Socket socket, const void* data, int length, const Address& to) {
    sockaddr_in destination = Native(to);
    const char* bytes = static_cast<const char*>(data);
    if (!g_loopback) return sendto(socket, bytes, length, 0, Generic(destination), kAddressLength);
    sockaddr_in local{};
    int localLength = sizeof(local);
    if (getsockname(socket, reinterpret_cast<sockaddr*>(&local), &localLength) != 0 && WSAGetLastError() == WSAEINVAL)
        LogBind(socket, "before its first datagram, which would bind it on every address", BindLoopback(socket));
    if (g_pair && destination.sin_family == AF_INET && IsUdp(socket)) {
        const bool broadcast = (ntohl(destination.sin_addr.s_addr) & 0xFF) == 0xFF;
        return pair::SendTo(socket, bytes, length, ntohs(destination.sin_port), broadcast);   // the pair's network
    }
    if (destination.sin_family == AF_INET && !IsLoopback(destination.sin_addr.s_addr)) {
        Redirected(destination.sin_addr.s_addr, destination.sin_port);
        destination.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    }
    return sendto(socket, bytes, length, 0, Generic(destination), kAddressLength);
}

// A pair's game: its UDP sockets read the pair's mailbox.
int ReceiveFrom(Socket socket, void* data, int length, Address* from) {
    int fromLength = kAddressLength;
    auto* sender = reinterpret_cast<sockaddr*>(from);
    if (!g_pair || !IsUdp(socket)) return recvfrom(socket, static_cast<char*>(data), length, 0, sender, &fromLength);
    return pair::RecvFrom(socket, static_cast<char*>(data), length, sender, &fromLength);
}

void Close(Socket socket) {
    if (g_pair) pair::Close(socket);
    closesocket(socket);
}

// A pair's game: a TCP link to the fake master is the harness's (SV5d4).
bool Connect(Socket socket, const Address& address) {
    const sockaddr_in native = Native(address);
    if (g_pair && native.sin_family == AF_INET && !IsUdp(socket) && pair::MasterConnect(socket, ntohs(native.sin_port))) {
        WSASetLastError(WSAEWOULDBLOCK);   // as a nonblocking connect under way
        return false;
    }
    return connect(socket, Generic(native), kAddressLength) == 0;
}

int Send(Socket socket, const void* data, int length) {
    if (g_pair && pair::MasterSend(socket, static_cast<const char*>(data), length)) return length;
    return send(socket, static_cast<const char*>(data), length, 0);
}

int Receive(Socket socket, void* data, int length) {
    int received = 0;
    if (g_pair && pair::MasterRecv(socket, static_cast<char*>(data), length, &received)) return received;
    return recv(socket, static_cast<char*>(data), length, 0);
}

} // namespace platform::net

int platform::win32::Select(fd_set* read, fd_set* write, fd_set* failed, const timeval* timeout) {
    using ei::verify::netlocal::g_pair;
    int ready = 0;
    if (g_pair && ei::verify::pair::MasterSelect(read, write, failed, &ready)) return ready;
    return select(0, read, write, failed, timeout);
}
