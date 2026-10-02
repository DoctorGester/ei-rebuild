#include "verify.h"

#include "../../src/hook/log.h"

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

void Report(const char* name, bool match, const char* detail) {
    static Registry registry;
    Counts& c = registry.counts[name];
    if (++c.calls == 1) Log("verify %s: first call", name);
    if (!match) {
        ++c.mismatches;
        if (c.mismatches <= 20) Log("MISMATCH %s: %s", name, detail);
    }
    if (c.calls % 100000 == 0)
        Log("verify %s: %lu calls, %lu mismatches", name, c.calls, c.mismatches);
}

} // namespace ei::verify
