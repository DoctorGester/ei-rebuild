#include "verify.h"

#include "../../src/hook/log.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
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

void Report(const char* name, bool match, const char* detail) {
    AcquireSRWLockExclusive(&g_countsLock);
    Counts& c = GetRegistry().counts[name];
    if (++c.calls == 1) Log("verify %s: first call", name);
    if (!match) {
        ++c.mismatches;
        if (c.mismatches <= 20) Log("MISMATCH %s: %s", name, detail);
    }
    // Totals at 1k, 10k, 100k, ... calls: hot functions do not flood the log.
    static constexpr unsigned long kMilestones[] = {1000ul, 10000ul, 100000ul, 1000000ul, 10000000ul, 100000000ul, 1000000000ul};
    bool milestone = false;
    for (unsigned long m : kMilestones) milestone |= c.calls == m;
    if (milestone)
        Log("verify %s: %lu calls, %lu mismatches", name, c.calls, c.mismatches);
    ReleaseSRWLockExclusive(&g_countsLock);
}

} // namespace ei::verify
