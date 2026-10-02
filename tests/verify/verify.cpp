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

void Report(const char* name, bool match, const char* detail) {
    static std::map<std::string, Counts> counts;
    Counts& c = counts[name];
    ++c.calls;
    if (!match) {
        ++c.mismatches;
        if (c.mismatches <= 20) Log("MISMATCH %s: %s", name, detail);
    }
    if (c.calls % 100000 == 0)
        Log("verify %s: %lu calls, %lu mismatches", name, c.calls, c.mismatches);
}

} // namespace ei::verify
