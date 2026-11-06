// IPv4 numbers in text (ipv4.h).
#include "ipv4.h"

#include <cstring>

namespace lib {

std::uint32_t ParseIpv4(std::string_view text) {
    // Winsock's answer for a single space.
    text = text.substr(0, text.find('\0'));   // a C string's end
    if (text == " ") return 0;
    std::uint32_t parts[4] = {};
    int count = 0;
    // The text's byte at `p`; a zero byte past the end, where the C string ended.
    std::size_t p = 0;
    auto at = [&](std::size_t i) { return i < text.size() ? text[i] : '\0'; };
    // A part's digits in 32 bits. As Winsock's, a digit that makes the value smaller is an overflow (a number past 2^32
    // that this does not catch is taken modulo 2^32).
    std::uint32_t value = 0;
    for (;;) {
        if (at(p) < '0' || at(p) > '9') return kNoAddress;
        unsigned base = 10;
        if (at(p) == '0') {
            ++p;
            base = 8;
            if (at(p) == 'x' || at(p) == 'X') {
                base = 16;
                ++p;
            }
        }
        value = 0;
        const std::size_t digits = p;
        for (;; ++p) {
            unsigned digit;
            if (at(p) >= '0' && at(p) <= '9') digit = static_cast<unsigned>(at(p) - '0');
            else if (base == 16 && at(p) >= 'a' && at(p) <= 'f') digit = static_cast<unsigned>(at(p) - 'a' + 10);
            else if (base == 16 && at(p) >= 'A' && at(p) <= 'F') digit = static_cast<unsigned>(at(p) - 'A' + 10);
            else break;
            const std::uint32_t next = value * base + digit;
            if (digit >= base || next < value) return kNoAddress;
            value = next;
        }
        if (base == 16 && p == digits) return kNoAddress;   // "0x" without a digit
        if (at(p) != '.') break;
        if (count == 3 || value > 0xFF) return kNoAddress;
        parts[count++] = value;
        ++p;
    }
    // White space ends the text, whatever follows it.
    if (at(p) != '\0' && at(p) != ' ' && (at(p) < '\t' || at(p) > '\r')) return kNoAddress;
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
