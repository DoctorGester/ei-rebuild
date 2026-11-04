// The game's clocks in harness runs: virtual clock, held clocks, clock trace (harness_clock.h).
#include "harness_clock.h"

#include "../../src/platform/log.h"
#include "game/main_loop.h"
#include "image/image_side.h"

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace ei::verify::clock {

namespace {

sys::ThreadId g_mainThread = 0;
FILE* g_randomFile = nullptr;   // the random trace (InstallRandomTrace)
sys::Image g_self;              // our module's image (with the random trace)

// The harness starts on the main thread (its static constructor runs before main).
void FindMainThreadOnce() {
    if (g_mainThread) return;
    g_mainThread = sys::CurrentThread();
    Log("harness: main thread %llu", static_cast<unsigned long long>(g_mainThread));
}

// ---- Virtual clock -------------------------------------------------------------------------------

// As many as the main loop runs in one pass (kLongestPassTicks, app_main.cpp): fewer frames to draw.
constexpr unsigned kUpdatesPerFrame = 5;
unsigned g_frameMs = 0;   // "frametime": the advance at each frame instead (0: kUpdatesPerFrame updates)

bool g_virtual = false;
std::atomic<bool> g_frozen{false};
std::atomic<std::uint32_t> g_now{1000000};
const unsigned* g_tickLength = nullptr;
bool g_fixedDate = false;

// Whether a timeGetTime call is the main loop's frame time: the rewritten WinMain (src/game/app_main.cpp) names
// its site.
bool IsMainLoopTime() { return ei::mainloop::g_site == ei::mainloop::Site::FrameTime && OnMainThread(); }

// What other threads read. They do not move the clock, but they see time pass while the main thread
// stands still: the new-game load waits on one of them. Nothing those threads time touches game
// state (the music volume, the cursor's frame).
//
// Their clock runs at real time, and jumps ahead to the virtual time when that is further on: from
// an anchor (their time and the real time it was taken at) moved by the main thread's advances to
// max(their time then, the new virtual time). It used to be the virtual time plus the real time
// since the main thread last moved it, never going back: after a long main-thread block (a load, a
// movie) it was the block's real length ahead of the virtual time and stood still until the main
// thread's frames caught up (SV1h: the cursor thread's 41 ms steps took 1.5 s); on a busy machine,
// whose frames take more real time than they advance, it never caught up (SV1f: sandbox_map 72-81 s
// against 17 s on real time).
struct Anchor {
    std::uint32_t time;   // the other threads' time
    std::uint32_t real;   // the system's tick count when it was taken
};
std::atomic<std::uint64_t> g_anchor{0};   // an Anchor, read and written whole
Anchor ReadAnchor() {
    const std::uint64_t packed = g_anchor.load();
    Anchor anchor;
    std::memcpy(&anchor, &packed, sizeof(anchor));
    return anchor;
}
void WriteAnchor(Anchor anchor) {
    std::uint64_t packed;
    std::memcpy(&packed, &anchor, sizeof(packed));
    g_anchor.store(packed);
}
std::uint32_t Later(std::uint32_t a, std::uint32_t b) { return static_cast<std::int32_t>(a - b) > 0 ? a : b; }
std::uint32_t AnchoredNow(std::uint32_t real) {
    const Anchor anchor = ReadAnchor();
    return g_frozen ? anchor.time : anchor.time + (real - anchor.real);
}

void Advance(std::uint32_t ms) {
    if (g_frozen || ms == 0) return;
    g_now += ms;
    const std::uint32_t real = sys::TickCount();
    WriteAnchor({Later(AnchoredNow(real), g_now), real});
}

std::atomic<std::uint32_t> g_otherNow{0};
std::uint32_t OtherThreadsNow() {
    const std::uint32_t candidate = AnchoredNow(sys::TickCount());
    std::uint32_t seen = g_otherNow.load();
    while (static_cast<std::int32_t>(candidate - seen) > 0)
        if (g_otherNow.compare_exchange_weak(seen, candidate)) return candidate;
    return seen;
}

std::uint32_t VirtualTimeGetTime() {
    if (!OnMainThread()) return OtherThreadsNow();
    if (IsMainLoopTime()) {
        Advance(g_frameMs ? g_frameMs : kUpdatesPerFrame * *g_tickLength);
        // The random trace notes every move of the clock (here, sleeps, timed waits), so two runs show where
        // their virtual times part (SV1g).
        RandomNote("clock frame time %lu", static_cast<unsigned long>(g_now.load()));
    }
    return g_now;
}
std::uint32_t VirtualGetTickCount() { return OnMainThread() ? g_now.load() : OtherThreadsNow(); }

// The main thread's sleeps take no real time: the clock moves by what they would have waited. Other threads sleep
// for real (and only read the clock).
void VirtualSleep(std::uint32_t ms) {
    if (!OnMainThread()) return sys::SleepMs(ms);
    if (ms) RandomNote("clock +%lu: sleep", static_cast<unsigned long>(ms));
    Advance(ms);
    sys::SleepMs(0);
}

// ---- Held clocks ("freeze" in a real-time run) ---------------------------------------------------

struct HeldClock {
    std::uint32_t (*base)() = nullptr;   // the system's clock; null while the clocks are not held
    std::uint32_t offset = 0;   // the game's time is base() - offset while running
    std::uint32_t heldAt = 0;
    std::uint32_t Read() const { return g_frozen ? heldAt : base() - offset; }
    void Hold() { heldAt = base() - offset; }
    void Resume() { offset = base() - heldAt; }
};
HeldClock g_heldTimeGetTime, g_heldTickCount;

// ---- Trace ---------------------------------------------------------------------------------------

enum Fn : std::uint8_t { kTimeGetTime, kGetTickCount, kGetLocalTime, kSleep, kFnCount };
constexpr const char* kFnNames[kFnCount] = {"timeGetTime", "GetTickCount", "GetLocalTime", "Sleep"};

struct Entry {
    Fn fn;
    sys::ThreadId thread;
    std::uintptr_t caller;
    std::atomic<unsigned long> count;
    unsigned long dumped;     // count at the last dump (the trace thread's)
    unsigned long long ms;    // the timeouts / sleeps asked for, summed
};
constexpr int kMaxEntries = 4096;
// Append-only: a new entry is filled in before the count that publishes it grows (under the lock),
// and the counts are bumped without the lock (a busy-waiting main thread kept the lock to itself).
Entry g_entries[kMaxEntries];
std::atomic<int> g_entryCount{0};
std::mutex g_traceLock;
bool g_tracing = false;

std::string Where(std::uintptr_t a) {
    std::string where = "?";
    sys::DescribeAddress(a, &where);
    return where;
}

std::string Who(sys::ThreadId thread) {
    return thread == g_mainThread ? std::string("main") : "thread " + std::to_string(thread);
}

void Record(Fn fn, const void* caller, std::uint32_t ms, bool hasMs) {
    const sys::ThreadId thread = sys::CurrentThread();
    const auto at = reinterpret_cast<std::uintptr_t>(caller);
    // An entry belongs to one thread, so only that thread changes its counts.
    Entry* entry = nullptr;
    const int count = g_entryCount;
    for (int i = 0; i < count; ++i) {
        Entry& e = g_entries[i];
        if (e.fn == fn && e.thread == thread && e.caller == at) {
            entry = &e;
            break;
        }
    }
    bool added = false;
    if (!entry) {
        std::lock_guard<std::mutex> lock(g_traceLock);
        if (g_entryCount < kMaxEntries) {
            entry = &g_entries[g_entryCount];
            entry->fn = fn;
            entry->thread = thread;
            entry->caller = at;
            entry->count = 0;
            entry->dumped = 0;
            entry->ms = 0;
            ++g_entryCount;
            added = true;
        }
    }
    if (entry) {
        ++entry->count;
        if (hasMs) entry->ms += ms;
    }
    if (added) {
        if (hasMs)
            Log("clocktrace: new %s(%ld) from %s on %s (virtual %lu)", kFnNames[fn], long(ms), Where(at).c_str(),
                Who(thread).c_str(), static_cast<unsigned long>(g_now.load()));
        else
            Log("clocktrace: new %s from %s on %s (virtual %lu)", kFnNames[fn], Where(at).c_str(), Who(thread).c_str(),
                static_cast<unsigned long>(g_now.load()));
    }
}

void DumpTrace(const char* why) {
    struct Line {
        Fn fn;
        sys::ThreadId thread;
        std::uintptr_t caller;
        unsigned long count, delta;
        unsigned long long ms;
    };
    std::vector<Line> lines;
    const int count = g_entryCount;
    for (int i = 0; i < count; ++i) {
        Entry& e = g_entries[i];
        const unsigned long calls = e.count;
        if (calls != e.dumped) lines.push_back({e.fn, e.thread, e.caller, calls, calls - e.dumped, e.ms});
        e.dumped = calls;
    }
    std::sort(lines.begin(), lines.end(), [](const Line& a, const Line& b) { return a.delta > b.delta; });
    Log("clocktrace: --- %s (virtual %lu, real %lu): %u call sites busy", why, static_cast<unsigned long>(g_now.load()),
        static_cast<unsigned long>(sys::TickCount()), unsigned(lines.size()));
    int shown = 0;
    for (const Line& line : lines) {
        if (++shown > 24) break;
        Log("clocktrace: %8lu %-24s from %-28s on %s (total %lu, %llu ms asked)", line.delta, kFnNames[line.fn],
            Where(line.caller).c_str(), Who(line.thread).c_str(), line.count, line.ms);
    }
}

void TraceThread() {
    for (;;) {
        sys::SleepMs(1000);
        DumpTrace("the last second");
    }
}

} // namespace

sys::ThreadId MainThread() { return g_mainThread; }

bool OnMainThread() { return sys::CurrentThread() == g_mainThread; }

void InstallVirtual(const unsigned* tickLength) {
    FindMainThreadOnce();
    g_virtual = true;
    g_tickLength = tickLength;
    WriteAnchor({g_now, sys::TickCount()});
    g_otherNow = g_now.load();
}

bool Virtual() { return g_virtual; }

void SetFrameTime(unsigned ms) { g_frameMs = ms; }

void InstallHeld() {
    g_heldTimeGetTime.base = &sys::Milliseconds;
    g_heldTickCount.base = &sys::TickCount;
}

// The zone is the one the goldens were recorded in on that day (Central European summer time, UTC+2), without its
// daylight saving: a save's time (info.sav: mktime of the local date) is the goldens' 985600800 on a machine in any
// zone, not 985608000 in UTC.
void FixDate() {
    g_fixedDate = true;
    sys::SetTimeZone("EET-2");
}

std::uint32_t Milliseconds() {
    if (g_tracing) Record(kTimeGetTime, EI_HARNESS_CALLER(), 0, false);
    if (g_virtual) return VirtualTimeGetTime();
    return g_heldTimeGetTime.base ? g_heldTimeGetTime.Read() : sys::Milliseconds();
}

std::uint32_t Ticks() {
    if (g_tracing) Record(kGetTickCount, EI_HARNESS_CALLER(), 0, false);
    if (g_virtual) return VirtualGetTickCount();
    return g_heldTickCount.base ? g_heldTickCount.Read() : sys::TickCount();
}

void Sleep(std::uint32_t ms) {
    if (g_tracing) Record(kSleep, EI_HARNESS_CALLER(), ms, true);
    if (g_virtual) VirtualSleep(ms);
    else sys::SleepMs(ms);
}

sys::Date LocalTime() {
    if (g_tracing) Record(kGetLocalTime, EI_HARNESS_CALLER(), 0, false);
    if (g_fixedDate) return {2001, 3, 1, 26, 12, 0, 0, 0};   // 2001-03-26 12:00, game.exe's build date
    return sys::LocalDate();
}

void Freeze(bool on) {
    if (on == g_frozen) return;
    if (g_virtual) {
        // The other threads' clock stands still while frozen and goes on from there.
        const std::uint32_t real = sys::TickCount();
        WriteAnchor({on ? AnchoredNow(real) : ReadAnchor().time, real});
    } else if (g_heldTimeGetTime.base) {
        if (on) {
            g_heldTimeGetTime.Hold();
            g_heldTickCount.Hold();
        } else {
            g_heldTimeGetTime.Resume();
            g_heldTickCount.Resume();
        }
    }
    g_frozen = on;
}

bool Frozen() { return g_frozen; }

std::uint32_t Now() { return g_virtual ? g_now.load() : sys::TickCount(); }

void InstallTrace() {
    FindMainThreadOnce();
    g_tracing = true;
    sys::StartThread(&TraceThread);
    Log("clocktrace: on (the game's clocks, sleeps and date)");
}

// ---- Random trace ----------------------------------------------------------------------------------

namespace {

const unsigned* g_traceFrames = nullptr;
unsigned g_randomFrame = ~0u;
std::mutex g_randomLock;

// One draw: "<frame> [(other thread) ][ei-rebuild+]<caller> <value>[ <what>] #<where>". <where> is the
// generator's state after the draw: ISAAC's position in its stream (cc * 256 + 255 - count; cc
// counts the refills), rand()'s seed. Where a function ran the original on live state besides ours (the verify
// wrappers until S21d), the generators were put back between them, so one draw could show up twice; the position tells a repeat from a new draw
// (tools/randtrace.py, SV1g).
void WriteDraw(std::uintptr_t caller, std::uint32_t value, const char* what, std::uint32_t where) {
    const bool ours = caller >= g_self.base && caller < g_self.end;
    std::lock_guard<std::mutex> lock(g_randomLock);
    const unsigned frame = *g_traceFrames;
    if (frame != g_randomFrame) {
        std::fflush(g_randomFile);
        g_randomFrame = frame;
    }
    std::fprintf(g_randomFile, "%u %s%s%s%X %08X%s #%X\n", frame, OnMainThread() ? "" : "(other thread) ",
                 ours ? "ei-rebuild" : "", ours ? "+" : "", unsigned(ours ? caller - g_self.base : caller), value,
                 what, unsigned(where));
}

} // namespace

void RandomNote(const char* format, ...) {
    if (!g_randomFile) return;
    std::lock_guard<std::mutex> lock(g_randomLock);
    std::fprintf(g_randomFile, "%u note: ", *g_traceFrames);
    va_list args;
    va_start(args, format);
    std::vfprintf(g_randomFile, format, args);
    va_end(args);
    std::fputc('\n', g_randomFile);
}

void InstallRandomTrace(const unsigned* frames) {
    if (g_randomFile) return;
    FindMainThreadOnce();
    const std::string path = ei::OutputPath("randtrace.txt");
    g_randomFile = sys::OpenFile(path, "w");
    if (!g_randomFile) return;
    std::setvbuf(g_randomFile, nullptr, _IOFBF, 1 << 20);
    g_traceFrames = frames;
    g_self = sys::SelfImage();
    image::ObserveDraws(&WriteDraw);   // the draws of our generator and rand(), through their observers
    Log("harness: random trace to %s", path.c_str());
}

void FlushRandomTrace() {
    if (!g_randomFile) return;
    std::lock_guard<std::mutex> lock(g_randomLock);
    std::fflush(g_randomFile);
}

} // namespace ei::verify::clock
