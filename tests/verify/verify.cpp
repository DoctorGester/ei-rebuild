#include "verify.h"

#include "../../src/hook/log.h"

#include <windows.h>

#include <atomic>
#include <vector>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

namespace ei::verify {

struct Counts {
    unsigned long calls = 0;
    unsigned long mismatches = 0;
};

void FormatContext(char* buf, std::size_t size, const char* fmt, void* args) {
    std::vsnprintf(buf, size, fmt, *static_cast<va_list*>(args));
}

// Totals are also written when the DLL unloads, so rarely called functions still show up.
struct Registry {
    std::map<std::string, Counts> counts;
    // Report() runs hundreds of millions of times in a long scenario (terrain lighting): the names are
    // string literals, so a pointer-keyed table finds the entry without building a std::string. A hit is
    // confirmed by comparing the text (a name formatted into a reused buffer falls back to `counts`).
    struct Slot {
        const char* key = nullptr;
        const std::string* name = nullptr;
        Counts* counts = nullptr;
    };
    static constexpr std::size_t kSlots = 16384;   // a power of two; about 2500 names are in use
    static constexpr std::size_t kProbes = 8;
    std::vector<Slot> byPointer = std::vector<Slot>(kSlots);

    Counts& Find(const char* name) {
        std::size_t home = static_cast<std::size_t>((reinterpret_cast<std::uintptr_t>(name) >> 2) * 2654435761u) &
                           (kSlots - 1);
        for (std::size_t probe = 0; probe < kProbes; ++probe) {
            Slot& slot = byPointer[(home + probe) & (kSlots - 1)];
            if (slot.key == name && std::strcmp(slot.name->c_str(), name) == 0) return *slot.counts;
            if (!slot.key) break;
        }
        auto entry = counts.try_emplace(name).first;
        // The first free (or this pointer's own) slot of the probe run, else the home slot.
        Slot* target = &byPointer[home];
        for (std::size_t probe = 0; probe < kProbes; ++probe) {
            Slot& slot = byPointer[(home + probe) & (kSlots - 1)];
            if (!slot.key || slot.key == name) {
                target = &slot;
                break;
            }
        }
        *target = Slot{name, &entry->first, &entry->second};
        return entry->second;
    }
    ~Registry() {
        for (auto& [name, c] : counts)
            Log("verify total %s: %lu calls, %lu mismatches", name.c_str(), c.calls, c.mismatches);
    }
};

Registry& GetRegistry() {
    static Registry registry;
    return registry;
}

// Wrappers also run on the game's other threads (the sound manager's stream thread): the counts
// are kept under a lock.
SRWLOCK g_countsLock = SRWLOCK_INIT;

void ForEachCount(const std::function<void(const char*, unsigned long, unsigned long)>& fn) {
    AcquireSRWLockExclusive(&g_countsLock);
    for (auto& [name, c] : GetRegistry().counts) fn(name.c_str(), c.calls, c.mismatches);
    ReleaseSRWLockExclusive(&g_countsLock);
}

// ---- x87 precision seen by the wrappers -------------------------------------------------------

std::atomic<DWORD> g_mainThread{0};

void SetMainThread() {
    DWORD none = 0;
    g_mainThread.compare_exchange_strong(none, GetCurrentThreadId());
}

struct RaisedPrecision {
    std::string name;
    unsigned cw;
    bool mainThread;
};
std::vector<RaisedPrecision> g_raised;   // under g_countsLock
constexpr std::size_t kMaxRaisedReports = 30;

// Module + offset of the return addresses on the stack (a few frames), for the log.
void DescribeStack(char* out, std::size_t size) {
    void* frames[14];
    USHORT n = RtlCaptureStackBackTrace(2, 14, frames, nullptr);
    std::size_t used = 0;
    out[0] = 0;
    for (USHORT i = 0; i < n && used + 48 < size; ++i) {
        HMODULE module = nullptr;
        char path[MAX_PATH] = "?";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               static_cast<LPCSTR>(frames[i]), &module))
            GetModuleFileNameA(module, path, MAX_PATH);
        const char* base = std::strrchr(path, '\\');
        int w = std::snprintf(out + used, size - used, " %s+%X", base ? base + 1 : path,
                              static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(frames[i]) -
                                                    reinterpret_cast<std::uintptr_t>(module)));
        if (w <= 0) break;
        used += static_cast<std::size_t>(w);
    }
}

void TracePrecision(const char* name) {
    unsigned short cw;
    __asm fnstcw cw
    if ((cw & 0x0300) == 0) return;   // 24-bit: the game's state
    bool mainThread = GetCurrentThreadId() == g_mainThread.load();
    AcquireSRWLockExclusive(&g_countsLock);
    bool known = false;
    for (const RaisedPrecision& r : g_raised) known |= r.name == name && r.cw == cw && r.mainThread == mainThread;
    bool report = !known && g_raised.size() < kMaxRaisedReports;
    if (report) g_raised.push_back({name, cw, mainThread});
    ReleaseSRWLockExclusive(&g_countsLock);
    if (!report) return;
    char stack[512];
    DescribeStack(stack, sizeof(stack));
    Log("FPU precision %s (control word %04X) in %s on %s thread %lu; stack:%s", (cw & 0x0300) == 0x0300 ? "64-bit" : "53-bit",
        cw, name, mainThread ? "the main" : "another", GetCurrentThreadId(), stack);
}

void ForEachRaisedPrecision(const std::function<void(const char*, unsigned, bool)>& fn) {
    AcquireSRWLockExclusive(&g_countsLock);
    std::vector<RaisedPrecision> copy = g_raised;
    ReleaseSRWLockExclusive(&g_countsLock);
    for (const RaisedPrecision& r : copy) fn(r.name.c_str(), r.cw, r.mainThread);
}

void Report(const char* name, bool match, const char* detail) {
    TracePrecision(name);
    AcquireSRWLockExclusive(&g_countsLock);
    Counts& c = GetRegistry().Find(name);
    if (++c.calls == 1) Log("verify %s: first call", name);
    if (!match) {
        ++c.mismatches;
        if (c.mismatches <= 20) Log("MISMATCH %s: %s", name, detail);
    }
    // Totals at 1k, 10k, 100k, ... calls: hot functions do not flood the log.
    static constexpr unsigned long kMilestones[] = {1000ul, 10000ul, 100000ul, 1000000ul, 10000000ul, 100000000ul, 1000000000ul};
    bool milestone = false;
    if (c.calls >= 1000 && c.calls % 1000 == 0)
        for (unsigned long m : kMilestones) milestone |= c.calls == m;
    if (milestone)
        Log("verify %s: %lu calls, %lu mismatches", name, c.calls, c.mismatches);
    ReleaseSRWLockExclusive(&g_countsLock);
}

} // namespace ei::verify
