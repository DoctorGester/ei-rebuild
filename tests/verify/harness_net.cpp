// The network of a harness game: a UDP port of its own, every socket on 127.0.0.1 (harness_net.h).
#include "harness_net.h"
#include "harness_pair.h"
#include "system/harness_sockets.h"
#include "system/harness_system.h"

#include "../../src/platform/log.h"
#include "image/image_side.h"

#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace ei::verify::netlocal {

namespace {

constexpr int kFirstPort = 20000;                   // below the dynamic range (49152 and up)
constexpr int kPortCount = 20000;

// Whether the game's binds and datagrams stay on 127.0.0.1 (Install), and whether it plays on a pair's network.
bool g_loopback = false;
bool g_pair = false;

// What the binds and the endpoint checks saw (the game's thread binds, the watchdog's checks). Never destroyed: the
// watchdog may be in a check (Scan) while the process exits and destroys its static objects.
struct State {
    std::mutex lock;
    unsigned binds = 0;
    unsigned scans = 0;
    std::vector<std::uint32_t> redirected;   // destinations sent to 127.0.0.1 instead (network order), each logged once
    std::vector<std::string> outside;        // "UDP 0.0.0.0:20123", each once
    std::mutex scanning;
};
State& S() {
    static State* const state = new State;
    return *state;
}

std::string Endpoint(const char* kind, std::uint32_t address, std::uint16_t port) {
    const auto* b = reinterpret_cast<const std::uint8_t*>(&address);
    char text[64];
    std::snprintf(text, sizeof(text), "%s %u.%u.%u.%u:%u", kind, b[0], b[1], b[2], b[3], ntohs(port));
    return text;
}

// Records an endpoint outside loopback (once) and logs it.
void Outside(const std::string& endpoint, const char* how) {
    bool known = false;
    {
        std::lock_guard<std::mutex> lock(S().lock);
        for (const std::string& seen : S().outside) known = known || seen == endpoint;
        if (!known) S().outside.push_back(endpoint);
    }
    if (!known) Log("harness: net: %s is bound outside loopback (%s)", endpoint.c_str(), how);
}

const char* Kind(std::uintptr_t s) {
    int type = 0;
    sockets::Length length = sizeof(type);
    if (getsockopt(sockets::Of(s), SOL_SOCKET, SO_TYPE, reinterpret_cast<char*>(&type), &length) != 0) return "socket";
    return type == SOCK_DGRAM ? "UDP" : type == SOCK_STREAM ? "TCP" : "socket";
}

// True when a UDP socket can bind `port` on 127.0.0.1 (as the game's server session binds in harness runs).
bool PortFree(int port) {
    const auto s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == static_cast<decltype(s)>(-1)) return false;
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons(static_cast<std::uint16_t>(port));
    const bool bound = bind(s, reinterpret_cast<const sockaddr*>(&a), sizeof(a)) == 0;
    sockets::Close(static_cast<std::uintptr_t>(s));
    return bound;
}

void InstallPort() {
    if (!sockets::Start()) {
        Log("harness: the system's sockets did not start; the game keeps its UDP port");
        return;
    }
    // A start that differs per process, so games starting together probe different ports.
    const int offset = static_cast<int>((sys::ProcessId() / 4) % kPortCount);
    int port = 0;
    for (int i = 0; i < 256 && port == 0; ++i) {
        const int candidate = kFirstPort + (offset + i * 7) % kPortCount;
        if (PortFree(candidate)) port = candidate;
    }
    sockets::Stop();
    if (port == 0) {
        Log("harness: no free UDP port found; the game keeps its own");
        return;
    }
    int& target = image::GamePort();
    const int before = target;
    target = port;
    Log("harness: the game's UDP port is %d (was %d): one per game, so parallel runs do not meet", port, before);
}

} // namespace

void Install() {
    // The pair's network has ports of its own: the game keeps its 8888 (harness_pair.h). Its datagrams and the master
    // link's go through the pair.
    g_pair = pair::Install();
    if (!g_pair) InstallPort();
    g_loopback = true;
    Log("harness: net: the game's binds and datagrams stay on 127.0.0.1");
}

bool Loopback() { return g_loopback; }

bool Paired() { return g_pair; }

bool IsUdp(std::uintptr_t s) {
    int type = 0;
    sockets::Length length = sizeof(type);
    return getsockopt(sockets::Of(s), SOL_SOCKET, SO_TYPE, reinterpret_cast<char*>(&type), &length) == 0 &&
           type == SOCK_DGRAM;
}

void LogBind(std::uintptr_t s, const char* why, int result) {
    const int error = sockets::LastCode();
    sockaddr_in bound{};
    sockets::Length length = sizeof(bound);
    if (result != 0 || getsockname(sockets::Of(s), reinterpret_cast<sockaddr*>(&bound), &length) != 0 ||
        bound.sin_family != AF_INET) {
        Log("harness: net: %s %s bind failed (error %d)", Kind(s), why, error);
        sockets::SetCode(error);
        return;
    }
    const std::string endpoint = Endpoint(Kind(s), bound.sin_addr.s_addr, bound.sin_port);
    {
        std::lock_guard<std::mutex> lock(S().lock);
        ++S().binds;
    }
    Log("harness: net: bind %s (%s)", endpoint.c_str(), why);
    if ((ntohl(bound.sin_addr.s_addr) >> 24) != 127) Outside(endpoint, why);
    sockets::SetCode(error);
}

void Redirected(std::uint32_t address, std::uint16_t port) {
    bool known = false;
    {
        std::lock_guard<std::mutex> lock(S().lock);
        for (std::uint32_t seen : S().redirected) known = known || seen == address;
        if (!known) S().redirected.push_back(address);
    }
    if (!known) Log("harness: net: datagrams to %s go to 127.0.0.1 instead", Endpoint("UDP", address, port).c_str());
}

void Scan() {
    std::lock_guard<std::mutex> scanning(S().scanning);
    sys::ScanEndpoints([](const std::string& endpoint) { Outside(endpoint, "found by the endpoint check"); });
    std::lock_guard<std::mutex> lock(S().lock);
    ++S().scans;
}

void Report(FILE* f) {
    Scan();
    std::lock_guard<std::mutex> lock(S().lock);
    const std::vector<std::string>& outside = S().outside;
    if (outside.empty()) {
        std::fprintf(f, "\nNET: loopback only (%u binds, %u endpoint checks: nothing bound outside 127.0.0.1)\n", S().binds,
                     S().scans);
    } else {
        std::string list;
        for (const std::string& endpoint : outside) list += (list.empty() ? "" : ", ") + endpoint;
        std::fprintf(f, "\nNET: outside loopback: %s\n", list.c_str());
    }
}

} // namespace ei::verify::netlocal
