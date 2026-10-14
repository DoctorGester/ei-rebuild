// A UDP port of its own for every harness game (harness_net.h).
#include <winsock2.h>
#include <windows.h>

#include "harness_net.h"

#include "../../src/hook/log.h"

#include <cstdint>

#pragma comment(lib, "ws2_32.lib")

namespace ei::verify::net_port {

namespace {

constexpr std::uintptr_t kGamePort = 0x00783E30;   // int, 8888 in game.exe's .data
constexpr int kFirstPort = 20000;                   // below the dynamic range (49152 and up)
constexpr int kPortCount = 20000;

// True when a UDP socket can bind `port` on every address (as SocketDriver::Open 004409A0 binds).
bool PortFree(int port) {
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return false;
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(static_cast<u_short>(port));
    bool bound = bind(s, reinterpret_cast<const sockaddr*>(&a), sizeof(a)) == 0;
    closesocket(s);
    return bound;
}

} // namespace

void Install() {
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
    auto* target = reinterpret_cast<int*>(kGamePort);
    int before = *target;
    DWORD old;
    if (!VirtualProtect(target, sizeof(int), PAGE_READWRITE, &old)) return;
    *target = port;
    VirtualProtect(target, sizeof(int), old, &old);
    Log("harness: the game's UDP port is %d (00783E30, was %d): one per game, so parallel runs do not meet", port,
        before);
}

} // namespace ei::verify::net_port
