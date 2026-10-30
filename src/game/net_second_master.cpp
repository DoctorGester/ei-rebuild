// EI_Plugin's NewMaster: the second master server (net_second_master.h). The plugin's code, read from
// EI_Plugin.asi: 10005110 (start the look-up), 100050C0 (the thread), 100051B0,
// 10004E20, 100052D0. Its state lives in its block
// (+0xC7 the second master's sockaddr, whose address is -1 until known; +0xD7 the "a.b.c.d:port"
// text); ours is here. The sockets are the platform's (game.exe's import slots, as the plugin's calls
// went but for gethostbyname and select, which it imported itself). Its times are the real clock's, not
// the game's.
#include "net_second_master.h"

#include "net_classes.h"
#include "net_coder.h"
#include "net_messages.h"

#include "ei/rebirth.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <optional>
#include <string>
#include <system_error>
#include <thread>

namespace net::second_master {

namespace {

namespace sockets = platform::net;
using Clock = std::chrono::steady_clock;

constexpr std::uint32_t kUnknown = 0xFFFFFFFFu;            // the plugin's "no address" (also a failed look-up)
constexpr auto kRetry = std::chrono::seconds(10);          // a new look-up at most every 10 s
constexpr auto kClickWait = std::chrono::milliseconds(300);   // the Multiplayer click waits this long for it
constexpr auto kProbe = std::chrono::seconds(1);           // the master request's connect try
constexpr std::int32_t kProbeStepUs = 10000;               // each select waits 10 ms
constexpr std::uint32_t kCopyMark = 0xDEADBEEFu;           // between the announcement and the names

// Written by the look-up thread, read on the main thread (as the plugin's 1001DF80).
std::atomic<std::uint32_t> g_resolved{kUnknown};
std::atomic<bool> g_lookingUp;               // a look-up thread runs (1001F128)
std::optional<Clock::time_point> g_lastStart;   // the last start, none before the first (1001F12C)
std::shared_future<void> g_done;             // ready when the last look-up ended (1001F124)
char g_host[256];                            // the setting's host part, which the thread looks up
int g_port = kDefaultPort;

// The plugin's block +0xC7 (family 2, port, address; the address -1 until known) and +0xD7.
NetAddress g_address = {{sockets::kIpv4, 0, kUnknown, {}}};
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

// 100050C0: the first address of the host, or -1; then the end and the running flag.
void LookUp(std::promise<void> done) {
    std::uint32_t address = kUnknown;
    if (!sockets::LookUp(g_host, &address)) address = kUnknown;
    g_resolved = address;
    done.set_value();
    g_lookingUp = false;
}

// 10005110: a look-up thread unless the address is known, one runs, or the last started under 10 s ago.
void StartLookUp() {
    if (g_resolved != kUnknown) return;
    const Clock::time_point now = Clock::now();
    if (g_lastStart && now - *g_lastStart < kRetry) return;
    if (g_lookingUp.exchange(true)) return;
    g_lastStart = now;
    std::promise<void> done;
    g_done = done.get_future().share();
    try {
        std::thread(LookUp, std::move(done)).detach();
    } catch (const std::system_error&) {
        g_lookingUp = false;   // the promise went with the thread's arguments: the end is ready (broken)
    }
}

// The player's name of each binding of the game server, as the plugin reads them: the
// binding's first pointer, the player record's name, no check of either.
std::string Names(std::uint8_t* count) {
    std::string names;
    *count = 0;
    const auto& bindings = g_gameServer->bindings;
    for (auto& node : bindings) {
        auto* binding = node.Get();
        auto* player = binding->Player();
        const char* name = player->name.data();
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
    sockets::Socket s = sockets::Open(sockets::Kind::Tcp);
    if (s == sockets::kNoSocket) return false;
    sockets::SetNonBlocking(s, true);
    sockets::Connect(s, master);
    bool reached = false;
    const Clock::time_point start = Clock::now();
    for (;;) {
        sockets::SocketGroup set(s);
        if (sockets::Select(nullptr, &set, nullptr, kProbeStepUs) == 1) {
            reached = true;
            break;
        }
        if (sockets::Select(nullptr, nullptr, &set, 0) == 1 || Clock::now() - start >= kProbe) break;
    }
    sockets::Close(s);
    return reached;
}

} // namespace

bool Configured() { return ei::rebirth::PluginSecondMaster()[0] != '\0'; }

void OnMultiplayerClick() {
    if (!Configured()) return;
    if (g_resolved == kUnknown) {
        if (!g_lookingUp) SplitSetting();
        StartLookUp();
        if (g_done.valid()) g_done.wait_for(kClickWait);
    }
    const std::uint32_t address = g_resolved;
    if (address == kUnknown) return;
    std::snprintf(g_text, sizeof(g_text), "%u.%u.%u.%u:%u", address & 0xFF, (address >> 8) & 0xFF,
                  (address >> 16) & 0xFF, address >> 24, static_cast<unsigned>(g_port));
    g_address.ip = address;
    g_address.port = sockets::ToNetwork(static_cast<std::uint16_t>(g_port));
    g_address.family = sockets::kIpv4;
}

void CopyAnnouncement(sockets::Socket socket, const GameBuffer& sent) {
    if (g_address.ip == kUnknown) return;
    const std::int32_t length = sent.PositionOf(sent.high);
    std::uint8_t count;
    std::string names = Names(&count);
    std::string packet(reinterpret_cast<const char*>(sent.begin), static_cast<std::size_t>(length));
    packet.append(reinterpret_cast<const char*>(&kCopyMark), sizeof(kCopyMark));
    packet.push_back(static_cast<char>(count));
    packet += names;
    sockets::SendTo(socket, packet.data(), static_cast<int>(packet.size()), g_address);
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
