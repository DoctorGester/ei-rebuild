// lib::ParseIpv4 (src/lib/ipv4.h) against Winsock's inet_addr: the answers below were printed by inet_addr itself.
#include "../../src/lib/ipv4.h"

#include <cstdint>
#include <cstdio>
#include <iterator>

namespace {

struct Case {
    const char* text;
    std::uint32_t address;   // in memory as inet_addr returns it on a little-endian machine
};

constexpr Case kCases[] = {
    {"127.0.0.1", 0x0100007Fu},
    {"1.2.3.4", 0x04030201u},
    {"255.255.255.255", 0xFFFFFFFFu},
    {"0.0.0.0", 0x00000000u},
    {"", 0xFFFFFFFFu},
    {" ", 0x00000000u},
    {"  ", 0xFFFFFFFFu},
    {"\t", 0xFFFFFFFFu},
    {" 1.2.3.4", 0xFFFFFFFFu},
    {"1.2.3.4 ", 0x04030201u},
    {"1.2.3.4 junk", 0x04030201u},
    {"1.2.3.4\tx", 0x04030201u},
    {"1.2.3.4\n", 0x04030201u},
    {"1.2.3.4x", 0xFFFFFFFFu},
    {"1.2.3.4.", 0xFFFFFFFFu},
    {"1.2.3.4.5", 0xFFFFFFFFu},
    {".1.2.3", 0xFFFFFFFFu},
    {"1..2", 0xFFFFFFFFu},
    {"256.1.1.1", 0xFFFFFFFFu},
    {"1.2.3.256", 0xFFFFFFFFu},
    {"10.1", 0x0100000Au},
    {"10.65535", 0xFFFF000Au},
    {"10.65536", 0x0000010Au},
    {"10.1.65535", 0xFFFF010Au},
    {"10.1.65536", 0xFFFFFFFFu},
    {"10.16777215", 0xFFFFFF0Au},
    {"10.16777216", 0xFFFFFFFFu},
    {"4294967295", 0xFFFFFFFFu},
    {"4294967296", 0xFFFFFFFFu},
    {"6018379481", 0xD92EB966u},
    {"8743829356", 0xFFFFFFFFu},
    {"0x7f.1", 0x0100007Fu},
    {"0X7F.0.0.1", 0x0100007Fu},
    {"017.0.0.1", 0x0100000Fu},
    {"08.1.1.1", 0xFFFFFFFFu},
    {"0x", 0xFFFFFFFFu},
    {"0x.1", 0xFFFFFFFFu},
    {"1.0x", 0xFFFFFFFFu},
    {"0xFFFFFFFF", 0xFFFFFFFFu},
    {"0x100000000", 0xFFFFFFFFu},
    {"0x49a4f224bc", 0xBC24F2A4u},
    {"0xcf2a270b54", 0xFFFFFFFFu},
    {"00000000000000017", 0x0F000000u},
    {"a.b.c.d", 0xFFFFFFFFu},
    {"+1.2.3.4", 0xFFFFFFFFu},
    {"-1.2.3.4", 0xFFFFFFFFu},
    {"1.-2.3.4", 0xFFFFFFFFu},
    {"1 .2.3.4", 0x01000000u},
};

} // namespace

int RunIpv4Checks() {
    int failures = 0;
    for (const Case& c : kCases) {
        const std::uint32_t got = lib::ParseIpv4(c.text);
        if (got == c.address) continue;
        ++failures;
        std::printf("ei_unit_check: ipv4: \"%s\" gave 0x%08X, inet_addr 0x%08X\n", c.text, static_cast<unsigned>(got),
                    static_cast<unsigned>(c.address));
    }
    if (lib::ToNetwork(0x1234) != 0x3412 || lib::FromNetwork(0x3412) != 0x1234) {
        ++failures;
        std::printf("ei_unit_check: ipv4: port byte order\n");
    }
    std::printf("ei_unit_check: ipv4: %d checks, %d failed\n", static_cast<int>(std::size(kCases)) + 1, failures);
    return failures;
}
