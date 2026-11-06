// str::BoundedLength and str::CopyInto (src/lib/str.h): text read from a packet or a file stops at its zero byte or
// at the end of the bytes there are, and text copied into a fixed buffer is cut to fit with its terminator. Also
// libprintf::Arg: a std::string and a std::string_view (not zero-terminated) for a %s.
#include "../../src/lib/str.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

namespace {

int failures = 0;
int checks = 0;

void Check(bool ok, const char* what) {
    ++checks;
    if (ok) return;
    ++failures;
    std::printf("ei_unit_check: bounded text: %s\n", what);
}

} // namespace

int RunBoundedTextChecks() {
    // A terminated text, one without a terminator in its room, an empty room, a zero first byte.
    const char packet[] = {'a', 'b', 'c', '\0', 'd', 'e'};
    Check(str::BoundedLength(packet, sizeof(packet)) == 3, "length up to the zero byte");
    Check(str::BoundedLength(packet + 4, 2) == 2, "no zero byte: the whole room");
    Check(str::BoundedLength(packet, 2) == 2, "zero byte past the room");
    Check(str::BoundedLength(packet, 0) == 0, "empty room");
    Check(str::BoundedLength(packet + 3, 3) == 0, "zero first byte");

    char name[8];
    std::memset(name, 'x', sizeof(name));
    str::CopyInto(name, "short");
    Check(std::strcmp(name, "short") == 0, "a text that fits is copied whole");
    str::CopyInto(name, std::string(300, 'n'));
    Check(std::strlen(name) == 7 && name[7] == '\0', "a long text is cut with its terminator");
    str::CopyInto(name, std::string("ab\0cd", 5));
    Check(std::strcmp(name, "ab") == 0, "a text stops at its zero byte");
    str::CopyInto(name, "");
    Check(name[0] == '\0', "an empty text");
    char one[1] = {'x'};
    str::CopyInto(one, "abc");
    Check(one[0] == '\0', "a one-byte buffer holds the terminator");

    // A std::string_view cut out of a longer text, handed to a printf-style function for a %s: only its own bytes.
    const std::string whole = "abcdef";
    const std::string_view view(whole.data() + 1, 3);
    char text[32];
    std::snprintf(text, sizeof(text), "%d:%s:%s", 5, libprintf::Arg(libprintf::Owned(whole)),
                  libprintf::Arg(libprintf::Owned(view)));
    Check(std::strcmp(text, "5:abcdef:bcd") == 0, "a std::string_view is handed on by its length");

    std::printf("ei_unit_check: bounded text: %d checks, %d failed\n", checks, failures);
    return failures;
}
