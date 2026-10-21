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
    std::int32_t stamp;   // the sender's segment (Stamp): datagrams are taken in (stamp, seq) order
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
volatile LONG g_handedOff = 0;   // this game's turn has ended (HandOff) and the next has not begun
std::int32_t g_stamp = 0;        // this game's segment: 2 * turn + side (a turn and the frame step after it)
unsigned g_frame = 0;            // the last FrameDone's frame (for the guard's message)
LONG g_outOfTurn = 0;            // mailbox calls and signals outside the turn (the guard)
bool g_cut = false;        // "netcut": nothing sent arrives, nothing sent here is received
std::uint16_t g_fakeMaster = 0;   // "fakemaster": the virtual port this game's master answers on
unsigned g_masterAnswers = 0;
std::uint32_t g_lastToken = 0;    // the token the last announcement carried back
char g_name[96];

// This process's sockets and their virtual ports, and the next port of its own.
SRWLOCK g_socketsLock = SRWLOCK_INIT;
std::vector<std::pair<std::uintptr_t, std::uint16_t>> g_sockets;
std::uint16_t g_nextOwn = 0;

// For the report.
unsigned g_turns = 0;
unsigned long long g_waitedMs = 0;
// Where a frame's time goes (QueryPerformanceCounter ticks): in the turn (from the end of the wait to the
// hand-off) and, for a turn handed off in the frame step, the rest of the frame step after it (outside).
LARGE_INTEGER g_turnStart{}, g_endedAt{};
long long g_inTurnTicks = 0, g_outsideTicks = 0;
unsigned g_turnsTimed = 0, g_handedOffEarly = 0;
unsigned g_sent = 0, g_unheard = 0, g_received = 0, g_lost = 0;
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

// The guard (SV10a). After the hand-off the frame step runs while the other game takes its turn. What
// the frame step does with the pair's network must not depend on how far the other game has got:
// - its receives take only datagrams of earlier segments or its own (RecvFrom's stamp rule), the same
//   ones as when the frame step was inside the turn;
// - it may send only to its own ports (the host's client and server) and to no one (a cut network, the
//   fake master): a datagram to the other game would reach it in this turn or the next, depending on
//   the machine;
// - binds, closes and signals change what the other game sees: never outside the turn.
// A call that breaks a rule fails the run (the first is logged). The pair's network is the main
// thread's only.
void OutOfTurn(const char* call) {
    if (InterlockedIncrement(&g_outOfTurn) != 1) return;
    char what[240];
    std::snprintf(what, sizeof(what),
                  "pair: %s outside this game's turn (in frame %u's frame step after the hand-off, thread %lu): "
                  "the frame step runs beside the other game's turn",
                  call, g_frame + 1, GetCurrentThreadId());
    Log("harness: %s", what);
    g_failures.push_back(what);
}

bool OutsideTurn() { return g_handedOff && !g_alone; }

void InTurn(const char* call) {
    if (OutsideTurn()) OutOfTurn(call);
}

bool ClaimedHere(std::uint16_t port) {
    for (const Claim& c : g_shared->claims)
        if (c.used && c.port == port && c.side == g_side) return true;
    return false;
}

void EndTurn() {
    if (InterlockedExchange(&g_handedOff, 1)) return;
    QueryPerformanceCounter(&g_endedAt);
    if (g_turnStart.QuadPart) {
        g_inTurnTicks += g_endedAt.QuadPart - g_turnStart.QuadPart;
        ++g_turnsTimed;
    }
    InterlockedIncrement(&g_shared->done[g_side]);
    SetEvent(g_turn[1 - g_side]);
}

} // namespace

bool Active() { return g_shared != nullptr; }

bool Overlap() {
    char value[8] = "";
    return !(GetEnvironmentVariableA("EI_PAIR_OVERLAP", value, sizeof(value)) && value[0] == '0');
}
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
    g_stamp = 2 + g_side;
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

void HandOff() {
    if (g_shared) EndTurn();
}

void FrameDone(unsigned frame) {
    if (!g_shared) return;
    const int other = 1 - g_side;
    g_frame = frame;
    if (g_handedOff && g_turnStart.QuadPart) {   // handed off in the frame step
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        g_outsideTicks += now.QuadPart - g_endedAt.QuadPart;
        ++g_handedOffEarly;
    }
    EndTurn();
    const LONG n = g_shared->done[g_side];
    if (g_alone) {
        g_turnStart.QuadPart = 0;   // no more turns to time
        InterlockedExchange(&g_handedOff, 0);
        return;
    }
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
    if (g_alone) g_turnStart.QuadPart = 0;
    else QueryPerformanceCounter(&g_turnStart);
    g_stamp = 2 * (n + 1) + g_side;
    InterlockedExchange(&g_handedOff, 0);   // this game's next turn
}

void Signal(const std::string& tag) {
    if (!g_shared) return;
    InTurn("Signal");
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
    InTurn("Signaled");
    Locked lock;
    for (const SignalTag& s : g_shared->signals)
        if (s.used && s.side != g_side && tag == s.tag) return true;
    return false;
}

bool Bind(std::uintptr_t s, std::uint16_t port) {
    InTurn("Bind");
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
    if (g_cut) {
        ++g_lost;
        return length;
    }
    if (g_fakeMaster && port == g_fakeMaster && length >= 8) {
        // The master's answer: kind -1, the announcement's master key, a token (numbered answers).
        std::uint32_t key = 0;
        std::memcpy(&key, data, 4);
        std::memcpy(&g_lastToken, data + 4, 4);
        const std::uint32_t token = 0x4D530000u + ++g_masterAnswers;
        for (Datagram& d : g_shared->slots) {
            if (d.full) continue;
            d.seq = g_shared->nextSeq++;
            d.stamp = g_stamp;
            d.to = from;
            d.from = port;
            d.length = 9;
            d.data[0] = static_cast<char>(-1);
            std::memcpy(d.data + 1, &key, 4);
            std::memcpy(d.data + 5, &token, 4);
            d.full = 1;
            break;
        }
        Log("harness: pair: fake master %u answered announcement %u (key %08X, token back %08X) with token %08X", port,
            g_masterAnswers, key, g_lastToken, token);
        return length;
    }
    if (OutsideTurn() && !ClaimedHere(port)) OutOfTurn("SendTo to a port of the other game's or nobody's");
    if (!Claimed(port)) {
        ++g_unheard;   // nobody holds the port: gone, as a datagram to a closed port is
        return length;
    }
    for (Datagram& d : g_shared->slots) {
        if (d.full) continue;
        d.seq = g_shared->nextSeq++;
        d.stamp = g_stamp;
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
        for (Datagram& d : g_shared->slots) {
            if (!d.full || d.to != port) continue;
            if (g_cut) {   // lost on the way
                d.full = 0;
                ++g_lost;
                continue;
            }
            // The stamp rule: a datagram of the other game's later segment (sent in its turn beside this
            // frame step) is not there yet. In order of segments, then of sending.
            if (d.stamp > g_stamp) continue;
            if (!oldest || d.stamp < oldest->stamp ||
                (d.stamp == oldest->stamp && static_cast<std::int32_t>(d.seq - oldest->seq) < 0))
                oldest = &d;
        }
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
    InTurn("Close");
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

void Cut() {
    if (!g_shared || g_cut) return;
    Locked lock;
    g_cut = true;
    Log("harness: pair: this game's network is cut");
}

void FakeMaster(std::uint16_t port) {
    g_fakeMaster = port;
    Log("harness: pair: a fake master answers on virtual port %u", port);
}

void Report(FILE* f) {
    if (!g_shared) return;
    char ended[80] = "still running at this report";
    char cut[64] = "";
    if (g_cut) std::snprintf(cut, sizeof(cut), "; cut by netcut (%u datagrams lost)", g_lost);
    if (g_fakeMaster)
        std::fprintf(f, "\nMASTER: the fake master on port %u answered %u announcements; the last carried token %08X back\n",
                     g_fakeMaster, g_masterAnswers, g_lastToken);
    if (g_peerEndedAt >= 0) std::snprintf(ended, sizeof(ended), "ended at this game's frame %ld", g_peerEndedAt);
    std::fprintf(f, "\nPAIR: side %d (%s): %u turns in lockstep, %llu ms waiting for the other game; %u datagrams sent "
                 "(%u to no one), %u received; the other game %s\n",
                 g_side, g_side == 0 ? "the scenario's game" : "the peer", g_turns, g_waitedMs, g_sent, g_unheard,
                 g_received, ended);
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    const double ms = 1000.0 / static_cast<double>(frequency.QuadPart);
    std::fprintf(f, "PAIR: per turn %.2f ms in the turn (from its start to the hand-off); %u of %u turns handed off in "
                 "the frame step (%s), the frame step's rest after it %.2f ms on average, beside the other game's turn\n",
                 g_turnsTimed ? static_cast<double>(g_inTurnTicks) * ms / g_turnsTimed : 0.0, g_handedOffEarly, g_turnsTimed,
                 Overlap() ? "before the frame step" : "EI_PAIR_OVERLAP=0: at the after-frame peek",
                 g_handedOffEarly ? static_cast<double>(g_outsideTicks) * ms / g_handedOffEarly : 0.0);
}

std::string Failure() {
    std::string text;
    for (const std::string& f : g_failures) text += (text.empty() ? "" : "; ") + f;
    return text;
}

} // namespace ei::verify::pair
