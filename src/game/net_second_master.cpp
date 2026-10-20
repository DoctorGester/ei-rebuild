// EI_Plugin's NewMaster: the second master server (net_second_master.h). The plugin's code, read from
// EI_Plugin.asi: 10005110 (start the look-up), 100050C0 (the thread), 100051B0 (at 006330A9),
// 10004E20 (at 0041ABDA), 100052D0 (at 0061D138). Its state lives in its block at [0073AFF0]
// (+0xC7 the second master's sockaddr, whose address is -1 until known; +0xD7 the "a.b.c.d:port"
// text); ours is here. Winsock is called directly (the plugin calls gethostbyname and select from
// its own imports, the rest through game.exe's thunks: the same functions).
#include "net_second_master.h"

#define _WINSOCK_DEPRECATED_NO_WARNINGS   // gethostbyname, as the plugin looks the name up
#include <winsock2.h>   // before anything includes windows.h (and with it winsock.h)
#include <windows.h>

#include "net_classes.h"
#include "net_coder.h"
#include "net_messages.h"

#include "ei/rebirth.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#pragma comment(lib, "ws2_32.lib")

namespace net::second_master {

namespace {

constexpr std::uint32_t kUnknown = 0xFFFFFFFFu;    // the plugin's "no address" (also a failed look-up)
constexpr DWORD kRetryMs = 10000;                  // a new look-up at most every 10 s
constexpr DWORD kClickWaitMs = 300;                // the Multiplayer click waits this long for it
constexpr DWORD kProbeMs = 1000;                   // the master request's connect try
constexpr long kProbeStepUs = 10000;               // each select waits 10 ms
constexpr std::uint32_t kCopyMark = 0xDEADBEEFu;   // between the announcement and the names

// Written by the look-up thread, read on the main thread (as the plugin's 1001DF80).
volatile LONG g_resolved = static_cast<LONG>(kUnknown);
volatile LONG g_lookingUp;   // a look-up thread runs (1001F128)
DWORD g_lastStart;           // GetTickCount at the last start, 0 before the first (1001F12C)
HANDLE g_done;               // set when a look-up ends; manual reset (1001F124)
char g_host[256];            // the setting's host part, which the thread looks up
int g_port = kDefaultPort;

// The plugin's block +0xC7 (family 2, port, address; the address -1 until known) and +0xD7.
NetAddress g_address = {2, 0, kUnknown, {}};
char g_text[32];

// The setting split into host and port (the port kDefaultPort when it names none).
void SplitSetting() {
    strncpy_s(g_host, ei::rebirth::PluginSecondMaster(), _TRUNCATE);
    g_port = kDefaultPort;
    if (char* colon = std::strchr(g_host, ':')) {
        *colon = '\0';
        g_port = std::atoi(colon + 1);
    }
}

// 100050C0: the first address of the host, or -1; then the event and the running flag.
DWORD WINAPI LookUp(void*) {
    std::uint32_t address = kUnknown;
    if (const hostent* host = gethostbyname(g_host)) {
        if (host->h_addr_list[0]) std::memcpy(&address, host->h_addr_list[0], sizeof(address));
    }
    InterlockedExchange(&g_resolved, static_cast<LONG>(address));
    if (g_done) SetEvent(g_done);
    InterlockedExchange(&g_lookingUp, 0);
    return 0;
}

// 10005110: a look-up thread unless the address is known, one runs, or the last started under 10 s ago.
void StartLookUp() {
    if (static_cast<std::uint32_t>(g_resolved) != kUnknown) return;
    DWORD now = GetTickCount();
    if (g_lastStart != 0 && now - g_lastStart < kRetryMs) return;
    if (InterlockedExchange(&g_lookingUp, 1) != 0) return;
    g_lastStart = now;
    if (g_done) ResetEvent(g_done);
    HANDLE thread = CreateThread(nullptr, 0, LookUp, nullptr, 0, nullptr);
    if (!thread) {
        if (g_done) SetEvent(g_done);
        InterlockedExchange(&g_lookingUp, 0);
        return;
    }
    CloseHandle(thread);
}

// The player's name of each binding of the game server (+0x25A4), as the plugin reads them: the
// binding's first pointer, the player record's name, no check of either.
std::string Names(std::uint8_t* count) {
    std::string names;
    *count = 0;
    const stlp::SharedRefList& bindings = GameServer()->bindings_;
    for (auto* node = bindings.First(); node != bindings.End(); node = node->next) {
        auto* binding = static_cast<BindingMessage*>(node->data.ptr);
        auto* player = static_cast<PlayerMessage*>(binding->PlayerRef().ptr);
        const char* name = player->name_.data();
        std::size_t length = std::strlen(name);
        names.push_back(static_cast<char>(length + 1));
        names.append(name, length);
        names.push_back('\0');
        ++*count;
    }
    return names;
}

// The nonblocking connect of 100052D0: written within 1 s. The second select asks for the error set
// the first one emptied (Winsock leaves only the ready sockets), as the plugin does.
bool Reachable(const NetAddress& master) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) return false;
    u_long nonBlocking = 1;
    ioctlsocket(s, FIONBIO, &nonBlocking);
    connect(s, reinterpret_cast<const sockaddr*>(&master), sizeof(master));
    const timeval step = {0, kProbeStepUs};
    const timeval none = {0, 0};
    bool reached = false;
    DWORD start = GetTickCount();
    fd_set set;
    for (;;) {
        set.fd_count = 1;
        set.fd_array[0] = s;
        if (select(0, nullptr, &set, nullptr, &step) == 1) {
            reached = true;
            break;
        }
        if (select(0, nullptr, nullptr, &set, &none) == 1 || GetTickCount() - start >= kProbeMs) break;
    }
    closesocket(s);
    return reached;
}

} // namespace

bool Configured() { return ei::rebirth::PluginSecondMaster()[0] != '\0'; }

void OnMultiplayerClick() {
    if (!Configured()) return;
    if (static_cast<std::uint32_t>(g_resolved) == kUnknown) {
        if (!g_done) g_done = CreateEventW(nullptr, TRUE, FALSE, nullptr);   // the plugin's, made at its start
        if (!g_lookingUp) SplitSetting();
        StartLookUp();
        if (g_done) WaitForSingleObject(g_done, kClickWaitMs);
    }
    auto address = static_cast<std::uint32_t>(g_resolved);
    if (address == kUnknown) return;
    std::snprintf(g_text, sizeof(g_text), "%u.%u.%u.%u:%u", address & 0xFF, (address >> 8) & 0xFF,
                  (address >> 16) & 0xFF, address >> 24, static_cast<unsigned>(g_port));
    g_address.ip = address;
    g_address.port = htons(static_cast<u_short>(g_port));
    g_address.family = AF_INET;
}

void CopyAnnouncement(std::uint32_t socket, const GameBuffer& sent) {
    if (g_address.ip == kUnknown) return;
    const std::int32_t length = sent.base - static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(sent.begin)) +
                                static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(sent.high));
    std::uint8_t count;
    std::string names = Names(&count);
    std::string packet(reinterpret_cast<const char*>(sent.begin), static_cast<std::size_t>(length));
    packet.append(reinterpret_cast<const char*>(&kCopyMark), sizeof(kCopyMark));
    packet.push_back(static_cast<char>(count));
    packet += names;
    sendto(socket, packet.data(), static_cast<int>(packet.size()), 0, reinterpret_cast<const sockaddr*>(&g_address),
           sizeof(g_address));
}

bool ParseMaster(NetAddress* master, const char* host, int port) {
    bool tried = false;
    for (;;) {
        bool parsed = master->Parse(host, port);
        if (parsed && tried) return true;
        if (!parsed && tried) return false;
        if (parsed && Reachable(*master)) return true;
        if (g_address.ip == kUnknown) return false;
        host = g_text;
        port = kDefaultPort;
        tried = true;
    }
}

} // namespace net::second_master
