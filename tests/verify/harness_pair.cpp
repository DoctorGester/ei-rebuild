// Two harness games played together: lockstep, the pair's own network, signals (harness_pair.h).
#include <winsock2.h>
#include <windows.h>

#include "harness_pair.h"

#include "../../src/hook/log.h"

#include <cstring>
#include <vector>

namespace ei::verify::pair {

namespace {

constexpr int kSlots = 1024;              // datagrams in flight (a turn sends a few)
constexpr int kDatagramBytes = 0x900;     // the game reads 0x800 at most (SocketDriver::Receive 00440BE0)
constexpr int kClaims = 64;
constexpr int kSignals = 32;
constexpr int kTagBytes = 56;
constexpr std::uint16_t kOwnPorts = 30000;   // + 1000 * side + n: a socket that sends before any bind
constexpr DWORD kSilentMs = 90000;           // the other game gives no turn for this long: go on alone (a failure)

struct Datagram {
    LONG full;
    std::uint32_t seq;
    std::uint16_t to, from;
    std::int32_t length;
    char data[kDatagramBytes];
};

struct Claim {
    std::uint16_t port;
    std::uint8_t used, side;
};

struct SignalTag {
    LONG used;
    std::int32_t side;
    char tag[kTagBytes];
};

// The pair's shared memory (a named mapping, zeroed by Windows when the first side creates it: the
// zero state is the start).
struct Shared {
    volatile LONG lock;
    volatile LONG done[2];      // main loop passes so far, each side
    volatile LONG pid[2];
    std::uint32_t nextSeq;
    LONG overflows;
    Claim claims[kClaims];
    SignalTag signals[kSignals];
    Datagram slots[kSlots];
};

Shared* g_shared;
int g_side = -1;
HANDLE g_turn[2];          // auto-reset events: "your turn may have come"
HANDLE g_peerProcess;
bool g_alone = false;      // the other game has ended (or never came): no more waiting
char g_name[96];

// This process's sockets and their virtual ports, and the next port of its own.
SRWLOCK g_socketsLock = SRWLOCK_INIT;
std::vector<std::pair<std::uintptr_t, std::uint16_t>> g_sockets;
std::uint16_t g_nextOwn = 0;

// For the report.
unsigned g_turns = 0;
unsigned long long g_waitedMs = 0;
unsigned g_sent = 0, g_unheard = 0, g_received = 0;
long g_peerEndedAt = -1;   // our frame when the other game was seen ended
std::vector<std::string> g_failures;

// The shared state's lock. A side that died holding it (a crash inside a mailbox copy) would hold it
// for ever: after 5 s it is taken anyway.
struct Locked {
    Locked() {
        const DWORD start = GetTickCount();
        while (InterlockedCompareExchange(&g_shared->lock, 1, 0) != 0) {
            if (GetTickCount() - start > 5000) {
                Log("harness: pair: the shared lock was held for 5 s; taken");
                break;
            }
            SwitchToThread();
        }
    }
    ~Locked() { InterlockedExchange(&g_shared->lock, 0); }
};

HANDLE PeerProcess() {
    if (!g_peerProcess && g_shared->pid[1 - g_side])
        g_peerProcess = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(g_shared->pid[1 - g_side]));
    return g_peerProcess;
}

bool Claimed(std::uint16_t port) {
    for (const Claim& c : g_shared->claims)
        if (c.used && c.port == port) return true;
    return false;
}

std::uint16_t PortOf(std::uintptr_t s) {
    AcquireSRWLockShared(&g_socketsLock);
    std::uint16_t port = 0;
    for (const auto& entry : g_sockets)
        if (entry.first == s) port = entry.second;
    ReleaseSRWLockShared(&g_socketsLock);
    return port;
}

} // namespace

bool Active() { return g_shared != nullptr; }
int Side() { return g_side; }

bool Install() {
    char value[128] = "";
    const DWORD n = GetEnvironmentVariableA("EI_PAIR", value, sizeof(value));
    if (n == 0 || n >= sizeof(value)) return false;
    char* comma = std::strrchr(value, ',');
    if (!comma || (comma[1] != '0' && comma[1] != '1')) {
        Log("harness: pair: EI_PAIR '%s' is not <name>,<side>", value);
        return false;
    }
    *comma = '\0';
    g_side = comma[1] - '0';
    std::snprintf(g_name, sizeof(g_name), "%s", value);
    char name[160];
    std::snprintf(name, sizeof(name), "Local\\%s", g_name);
    HANDLE mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Shared), name);
    if (!mapping) {
        Log("harness: pair: the shared state %s could not be made (error %lu)", name, GetLastError());
        g_failures.push_back("pair: the shared state could not be made");
        return false;
    }
    g_shared = static_cast<Shared*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared)));
    if (!g_shared) {
        g_failures.push_back("pair: the shared state could not be mapped");
        return false;
    }
    for (int side = 0; side < 2; ++side) {
        std::snprintf(name, sizeof(name), "Local\\%s.turn%d", g_name, side);
        g_turn[side] = CreateEventA(nullptr, FALSE, FALSE, name);
    }
    InterlockedExchange(&g_shared->pid[g_side], static_cast<LONG>(GetCurrentProcessId()));
    Log("harness: pair: side %d of %s (%s): lockstep with the other game, UDP on the pair's own network", g_side,
        g_name, g_side == 0 ? "the scenario's game" : "the peer");
    return true;
}

void FrameDone(unsigned frame) {
    if (!g_shared) return;
    const int other = 1 - g_side;
    const LONG n = InterlockedIncrement(&g_shared->done[g_side]);
    SetEvent(g_turn[other]);
    if (g_alone) return;
    ++g_turns;
    auto ready = [&] {
        const LONG theirs = g_shared->done[other];
        return g_side == 0 ? theirs >= n : theirs >= n + 1;
    };
    const DWORD start = GetTickCount();
    while (!ready()) {
        HANDLE peer = PeerProcess();
        HANDLE handles[2] = {g_turn[g_side], peer};
        const DWORD r = WaitForMultipleObjects(peer ? 2 : 1, handles, FALSE, 500);
        if (peer && r == WAIT_OBJECT_0 + 1 && !ready()) {
            Log("harness: pair: the other game has ended (after %ld frames); this one goes on alone from frame %u",
                g_shared->done[other], frame);
            g_peerEndedAt = static_cast<long>(frame);
            g_alone = true;
            break;
        }
        if (GetTickCount() - start > kSilentMs) {
            char what[160];
            std::snprintf(what, sizeof(what), "pair: the other game %s for %lu s (frame %u); going on alone",
                          peer ? "gave no turn" : "did not start", kSilentMs / 1000, frame);
            Log("harness: %s", what);
            g_failures.push_back(what);
            g_alone = true;
            break;
        }
    }
    g_waitedMs += GetTickCount() - start;
}

void Signal(const std::string& tag) {
    if (!g_shared) return;
    Locked lock;
    for (SignalTag& s : g_shared->signals) {
        if (s.used) continue;
        s.side = g_side;
        std::snprintf(s.tag, sizeof(s.tag), "%s", tag.c_str());
        s.used = 1;
        return;
    }
    g_failures.push_back("pair: no room for signal " + tag);
}

bool Signaled(const std::string& tag) {
    if (!g_shared) return false;
    Locked lock;
    for (const SignalTag& s : g_shared->signals)
        if (s.used && s.side != g_side && tag == s.tag) return true;
    return false;
}

bool Bind(std::uintptr_t s, std::uint16_t port) {
    {
        Locked lock;
        if (port == 0) {
            do port = static_cast<std::uint16_t>(kOwnPorts + 1000 * g_side + g_nextOwn++);
            while (Claimed(port));
        } else if (Claimed(port)) {
            Log("harness: pair: virtual port %u is taken", port);
            return false;
        }
        Claim* free = nullptr;
        for (Claim& c : g_shared->claims)
            if (!c.used && !free) free = &c;
        if (!free) {
            g_failures.push_back("pair: no room for another virtual port");
            return false;
        }
        free->port = port;
        free->side = static_cast<std::uint8_t>(g_side);
        free->used = 1;
    }
    AcquireSRWLockExclusive(&g_socketsLock);
    g_sockets.push_back({s, port});
    ReleaseSRWLockExclusive(&g_socketsLock);
    Log("harness: pair: socket %u has virtual port %u", static_cast<unsigned>(s), port);
    return true;
}

int SendTo(std::uintptr_t s, const char* data, int length, std::uint16_t port) {
    if (length < 0 || length > kDatagramBytes) {
        WSASetLastError(WSAEMSGSIZE);
        return SOCKET_ERROR;
    }
    std::uint16_t from = PortOf(s);
    if (!from) {
        if (!Bind(s, 0)) {
            WSASetLastError(WSAEADDRINUSE);
            return SOCKET_ERROR;
        }
        from = PortOf(s);
    }
    // The lobby's packets (first byte: 3 ping, 4 join, 5 info, 6 accepted, 7 refused, 1 leave; not 0, the
    // updates) are logged, the first 100 of them.
    static unsigned logged = 0;
    if (length > 0 && data[0] != 0 && logged < 100) {
        ++logged;
        Log("harness: pair: datagram %u -> %u, packet %d, %d bytes", from, port, static_cast<signed char>(data[0]), length);
    }
    Locked lock;
    ++g_sent;
    if (!Claimed(port)) {
        ++g_unheard;   // nobody holds the port: gone, as a datagram to a closed port is
        return length;
    }
    for (Datagram& d : g_shared->slots) {
        if (d.full) continue;
        d.seq = g_shared->nextSeq++;
        d.to = port;
        d.from = from;
        d.length = length;
        std::memcpy(d.data, data, static_cast<std::size_t>(length));
        d.full = 1;
        return length;
    }
    if (g_shared->overflows++ == 0) g_failures.push_back("pair: the mailbox overflowed (a datagram was dropped)");
    return length;
}

int RecvFrom(std::uintptr_t s, char* data, int length, sockaddr* from, int* fromLength) {
    const std::uint16_t port = PortOf(s);
    Datagram* oldest = nullptr;
    Locked lock;
    if (port) {
        for (Datagram& d : g_shared->slots)
            if (d.full && d.to == port && (!oldest || static_cast<std::int32_t>(d.seq - oldest->seq) < 0)) oldest = &d;
    }
    if (!oldest) {
        WSASetLastError(WSAEWOULDBLOCK);
        return SOCKET_ERROR;
    }
    ++g_received;
    const int n = oldest->length < length ? oldest->length : length;
    std::memcpy(data, oldest->data, static_cast<std::size_t>(n));
    if (from && fromLength && *fromLength >= static_cast<int>(sizeof(sockaddr_in))) {
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(oldest->from);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        std::memcpy(from, &address, sizeof(address));
        *fromLength = sizeof(address);
    }
    const bool truncated = oldest->length > length;
    oldest->full = 0;
    if (truncated) {
        WSASetLastError(WSAEMSGSIZE);
        return SOCKET_ERROR;
    }
    return n;
}

void Close(std::uintptr_t s) {
    const std::uint16_t port = PortOf(s);
    if (!port) return;
    AcquireSRWLockExclusive(&g_socketsLock);
    for (std::size_t i = 0; i < g_sockets.size(); ++i)
        if (g_sockets[i].first == s) {
            g_sockets.erase(g_sockets.begin() + static_cast<std::ptrdiff_t>(i));
            break;
        }
    ReleaseSRWLockExclusive(&g_socketsLock);
    Locked lock;
    for (Claim& c : g_shared->claims)
        if (c.used && c.port == port && c.side == g_side) c.used = 0;
}

void Report(FILE* f) {
    if (!g_shared) return;
    char ended[80] = "still running at this report";
    if (g_peerEndedAt >= 0) std::snprintf(ended, sizeof(ended), "ended at this game's frame %ld", g_peerEndedAt);
    std::fprintf(f, "\nPAIR: side %d (%s): %u turns in lockstep, %llu ms waiting for the other game; %u datagrams sent "
                 "(%u to no one), %u received; the other game %s\n",
                 g_side, g_side == 0 ? "the scenario's game" : "the peer", g_turns, g_waitedMs, g_sent, g_unheard,
                 g_received, ended);
}

std::string Failure() {
    std::string text;
    for (const std::string& f : g_failures) text += (text.empty() ? "" : "; ") + f;
    return text;
}

} // namespace ei::verify::pair
