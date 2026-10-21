// The network of a harness game: a UDP port of its own, every socket on 127.0.0.1 (harness_net.h).
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>

#include "harness_net.h"
#include "harness_pair.h"

#include "../../src/hook/log.h"
#include "game/game_data.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

namespace ei::verify::netlocal {

namespace {

constexpr std::uintptr_t kGamePort = 0x00783E30;   // int, 8888 in game.exe's .data
constexpr int kFirstPort = 20000;                   // below the dynamic range (49152 and up)
constexpr int kPortCount = 20000;

// WSOCK32's import slots in game.exe (the thunks 006E6124 and 006E6130; src/lib/crt.cpp).
constexpr std::uintptr_t kBindSlot = 0x0073B6DC;     // bind, ordinal 2
constexpr std::uintptr_t kSendToSlot = 0x0073B70C;   // sendto, ordinal 20
constexpr std::uintptr_t kRecvFromSlot = 0x0073B6CC;      // recvfrom, ordinal 17 (a pair's games only)
constexpr std::uintptr_t kCloseSocketSlot = 0x0073B6E4;   // closesocket, ordinal 3 (a pair's games only)

using BindFn = int(WINAPI*)(SOCKET, const sockaddr*, int);
using SendToFn = int(WINAPI*)(SOCKET, const char*, int, int, const sockaddr*, int);
using RecvFromFn = int(WINAPI*)(SOCKET, char*, int, int, sockaddr*, int*);
using CloseSocketFn = int(WINAPI*)(SOCKET);
BindFn g_bind;
SendToFn g_sendTo;
RecvFromFn g_recvFrom;
CloseSocketFn g_closeSocket;

// What the binds and the endpoint checks saw (the game's thread binds, the watchdog's checks).
SRWLOCK g_lock = SRWLOCK_INIT;
unsigned g_binds;
unsigned g_scans;
std::vector<std::string> g_outside;   // "UDP 0.0.0.0:20123", each once
std::vector<std::uint32_t> g_redirected;   // destinations sent to 127.0.0.1 instead (network order), each logged once

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
    for (const std::string& seen : g_outside) known = known || seen == endpoint;
    if (!known) g_outside.push_back(endpoint);
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

// bind: every address (INADDR_ANY) becomes 127.0.0.1, the port as asked.
int WINAPI LoopbackBind(SOCKET s, const sockaddr* name, int length) {
    sockaddr_in local{};
    const char* why = "the game's";
    if (pair::Active() && name && length == sizeof(local) && name->sa_family == AF_INET && IsUdp(s)) {
        // A pair's game: the port asked is a virtual port on the pair's network (harness_pair.h); the socket
        // itself is bound on 127.0.0.1, any port.
        std::memcpy(&local, name, sizeof(local));
        if (!pair::Bind(s, ntohs(local.sin_port))) {
            WSASetLastError(WSAEADDRINUSE);
            return SOCKET_ERROR;
        }
        sockaddr_in loopback{};
        loopback.sin_family = AF_INET;
        loopback.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        const int result = g_bind(s, reinterpret_cast<const sockaddr*>(&loopback), sizeof(loopback));
        const int error = WSAGetLastError();
        LogBind(s, "the game's, on the pair's network", result);
        if (result != 0) pair::Close(s);
        WSASetLastError(error);
        return result;
    }
    if (name && length == sizeof(local) && name->sa_family == AF_INET) {
        std::memcpy(&local, name, sizeof(local));
        if (local.sin_addr.s_addr == htonl(INADDR_ANY)) {
            local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            name = reinterpret_cast<const sockaddr*>(&local);
            why = "the game's, asked on every address";
        }
    }
    const int result = g_bind(s, name, length);
    const int error = WSAGetLastError();
    LogBind(s, why, result);
    WSASetLastError(error);
    return result;
}

// sendto: a socket not bound yet is bound to 127.0.0.1 first (Winsock would bind it on every address);
// a destination outside loopback (the lobby's broadcast) becomes 127.0.0.1 on the same port.
int WINAPI LoopbackSendTo(SOCKET s, const char* data, int length, int flags, const sockaddr* to, int toLength) {
    sockaddr_in local{};
    int localLength = sizeof(local);
    if (getsockname(s, reinterpret_cast<sockaddr*>(&local), &localLength) != 0 && WSAGetLastError() == WSAEINVAL) {
        sockaddr_in loopback{};
        loopback.sin_family = AF_INET;
        loopback.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        LogBind(s, "before its first datagram, which would bind it on every address",
                g_bind(s, reinterpret_cast<const sockaddr*>(&loopback), sizeof(loopback)));
    }
    sockaddr_in destination{};
    if (pair::Active() && to && toLength == sizeof(destination) && to->sa_family == AF_INET && IsUdp(s)) {
        std::memcpy(&destination, to, sizeof(destination));
        return pair::SendTo(s, data, length, ntohs(destination.sin_port));   // the pair's network
    }
    if (to && toLength == sizeof(destination) && to->sa_family == AF_INET) {
        std::memcpy(&destination, to, sizeof(destination));
        const std::uint32_t address = destination.sin_addr.s_addr;
        if (!IsLoopback(address)) {
            destination.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            to = reinterpret_cast<const sockaddr*>(&destination);
            AcquireSRWLockExclusive(&g_lock);
            bool known = false;
            for (std::uint32_t seen : g_redirected) known = known || seen == address;
            if (!known) g_redirected.push_back(address);
            ReleaseSRWLockExclusive(&g_lock);
            if (!known)
                Log("harness: net: datagrams to %s go to 127.0.0.1 instead",
                    Endpoint("UDP", address, destination.sin_port).c_str());
        }
    }
    return g_sendTo(s, data, length, flags, to, toLength);
}

// recvfrom and closesocket of a pair's game: its UDP sockets read the pair's mailbox.
int WINAPI PairRecvFrom(SOCKET s, char* data, int length, int flags, sockaddr* from, int* fromLength) {
    if (!IsUdp(s)) return g_recvFrom(s, data, length, flags, from, fromLength);
    return pair::RecvFrom(s, data, length, from, fromLength);
}

int WINAPI PairCloseSocket(SOCKET s) {
    pair::Close(s);
    return g_closeSocket(s);
}

template <class T>
void PatchSlot(std::uintptr_t address, T replacement, T& original) {
    auto* slot = reinterpret_cast<T*>(address);
    DWORD old;
    VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old);
    original = *slot;
    *slot = replacement;
    VirtualProtect(slot, sizeof(*slot), old, &old);
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
    auto* target = ei::GamePointer<int>(kGamePort);
    int before = *target;
    DWORD old;
    if (!VirtualProtect(target, sizeof(int), PAGE_READWRITE, &old)) return;
    *target = port;
    VirtualProtect(target, sizeof(int), old, &old);
    Log("harness: the game's UDP port is %d (00783E30, was %d): one per game, so parallel runs do not meet", port,
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
    if (pair::Install()) {
        // The pair's network has ports of its own: the game keeps its 8888 (harness_pair.h).
        PatchSlot(kRecvFromSlot, &PairRecvFrom, g_recvFrom);
        PatchSlot(kCloseSocketSlot, &PairCloseSocket, g_closeSocket);
    } else {
        InstallPort();
    }
    PatchSlot(kBindSlot, &LoopbackBind, g_bind);
    PatchSlot(kSendToSlot, &LoopbackSendTo, g_sendTo);
    Log("harness: net: the game's binds and datagrams stay on 127.0.0.1 (import slots 0073B6DC, 0073B70C)");
}

void Scan() {
    static std::vector<std::uint8_t> udp, tcp;   // the watchdog's, and the report's after it stopped scanning
    static SRWLOCK scanning = SRWLOCK_INIT;
    AcquireSRWLockExclusive(&scanning);
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
    if (g_outside.empty()) {
        std::fprintf(f, "\nNET: loopback only (%u binds, %u endpoint checks: nothing bound outside 127.0.0.1)\n", g_binds,
                     g_scans);
    } else {
        std::string list;
        for (const std::string& endpoint : g_outside) list += (list.empty() ? "" : ", ") + endpoint;
        std::fprintf(f, "\nNET: outside loopback: %s\n", list.c_str());
    }
    ReleaseSRWLockShared(&g_lock);
}

} // namespace ei::verify::netlocal
