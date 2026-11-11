// EI_Plugin's NewMaster: the second master server (net_second_master.h). Its state is the second master's
// socket address (the address is -1 until known) and its "a.b.c.d:port" text. The sockets are the platform's.
// Its times are the real clock's, not the game's.
#include "game/net/net_second_master.h"

#include "game/data/settings.h"
#include "game/net/net_session.h"
#include "game/core/coder.h"
#include "game/net/net_messages.h"

#include "lib/ipv4.h"
#include "lib/str.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <optional>
#include <string>
#include <string_view>
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
std::string g_host;                          // the setting's host part, which the thread looks up
int g_port = kDefaultPort;

// The second master's address (family 2, port, address; the address -1 until known) and its text.
NetAddress g_address = {{sockets::kIpv4, 0, kUnknown, {}}};
char g_text[32];

// The harness's address (a function static: the harness sets it before this file's static constructors may have run).
std::string& Override() {
    static std::string address;
    return address;
}

// The address "host[:port]": the harness's, else the settings file's [network] SecondMaster; empty: none.
std::string Setting() {
    return Override().empty() ? ::settings::GetString(::settings::kNetwork, "SecondMaster") : Override();
}

// The setting split into host and port (the port kDefaultPort when it names none).
void SplitSetting() {
    const std::string setting = Setting();
    const std::size_t colon = setting.find(':');
    g_host = setting.substr(0, colon);
    g_port = colon == std::string::npos ? kDefaultPort : str::ToInt(std::string_view(setting).substr(colon + 1));
}

// The look-up thread: the first address of the host, or -1; then the end and the running flag.
void LookUp(std::promise<void> done) {
    g_resolved = sockets::LookUp(g_host).value_or(kUnknown);
    done.set_value();
    g_lookingUp = false;
}

// A look-up thread unless the address is known, one runs, or the last started under 10 s ago.
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
        auto* player = binding->player.Get();
        const char* name = player->name.data();
        std::size_t length = std::strlen(name);
        names.push_back(static_cast<char>(length + 1));
        names.append(name, length);
        names.push_back('\0');
        ++*count;
    }
    return names;
}

// The nonblocking connect: written within 1 s. The second select asks for the error set
// the first one emptied (Winsock leaves only the ready sockets), as the plugin does.
bool Reachable(const NetAddress& master) {
    sockets::Socket s = sockets::Open(sockets::Kind::Tcp);
    if (s == sockets::kNoSocket) return false;
    sockets::SetNonBlocking(s, true);
    sockets::Connect(s, master);
    bool reached = false;
    const Clock::time_point start = Clock::now();
    for (;;) {
        sockets::SocketGroup set = sockets::SocketGroup::Of(s);
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

void SetAddress(std::string_view address) { Override() = address; }

bool Configured() { return !Setting().empty(); }

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
    g_address.port = lib::ToNetwork(static_cast<std::uint16_t>(g_port));
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

bool ParseMaster(NetAddress* master, std::string_view host, int port) {
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
