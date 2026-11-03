// IPv4 numbers in text (ipv4.h).
#include "ipv4.h"

#include <cstring>

namespace lib {

std::uint32_t ParseIpv4(const char* text) {
    // Winsock's answer for a single space.
    if (std::strcmp(text, " ") == 0) return 0;
    std::uint32_t parts[4] = {};
    int count = 0;
    const char* p = text;
    // A part's digits in 32 bits. As Winsock's, a digit that makes the value smaller is an overflow (a number past 2^32
    // that this does not catch is taken modulo 2^32).
    std::uint32_t value = 0;
    for (;;) {
        if (*p < '0' || *p > '9') return kNoAddress;
        unsigned base = 10;
        if (*p == '0') {
            ++p;
            base = 8;
            if (*p == 'x' || *p == 'X') {
                base = 16;
                ++p;
            }
        }
        value = 0;
        const char* digits = p;
        for (;; ++p) {
            unsigned digit;
            if (*p >= '0' && *p <= '9') digit = static_cast<unsigned>(*p - '0');
            else if (base == 16 && *p >= 'a' && *p <= 'f') digit = static_cast<unsigned>(*p - 'a' + 10);
            else if (base == 16 && *p >= 'A' && *p <= 'F') digit = static_cast<unsigned>(*p - 'A' + 10);
            else break;
            const std::uint32_t next = value * base + digit;
            if (digit >= base || next < value) return kNoAddress;
            value = next;
        }
        if (base == 16 && p == digits) return kNoAddress;   // "0x" without a digit
        if (*p != '.') break;
        if (count == 3 || value > 0xFF) return kNoAddress;
        parts[count++] = value;
        ++p;
    }
    // White space ends the text, whatever follows it.
    if (*p != '\0' && *p != ' ' && (*p < '\t' || *p > '\r')) return kNoAddress;
    // The last part fills the bytes the others leave.
    static constexpr std::uint32_t kLastLimit[4] = {0xFFFFFFFFu, 0xFFFFFFu, 0xFFFFu, 0xFFu};
    if (value > kLastLimit[count]) return kNoAddress;
    std::uint32_t host = value;
    for (int i = 0; i < count; ++i) host |= parts[i] << (24 - 8 * i);
    // Network order: the first part in the first byte.
    const std::uint8_t bytes[4] = {static_cast<std::uint8_t>(host >> 24), static_cast<std::uint8_t>(host >> 16),
                                   static_cast<std::uint8_t>(host >> 8), static_cast<std::uint8_t>(host)};
    std::uint32_t address;
    std::memcpy(&address, bytes, sizeof(address));
    return address;
}

} // namespace lib
