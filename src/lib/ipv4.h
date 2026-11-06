#pragma once

// The network's byte order and IPv4 numbers in text, as Winsock gave them (htons, ntohs, inet_addr), for the sessions'
// addresses (platform.h's net::Address keeps the port and the address in network order).

#include <bit>
#include <cstdint>
#include <string_view>

namespace lib {

// A port between the host's byte order and the network's (htons, ntohs).
constexpr std::uint16_t ToNetwork(std::uint16_t value) {
    if constexpr (std::endian::native == std::endian::big) return value;
    return static_cast<std::uint16_t>((value >> 8) | (value << 8));
}
constexpr std::uint16_t FromNetwork(std::uint16_t value) { return ToNetwork(value); }

// ParseIpv4's answer for a text that is not an IPv4 number (also the broadcast address 255.255.255.255).
inline constexpr std::uint32_t kNoAddress = 0xFFFFFFFFu;

// "a.b.c.d" as an address in network order, or kNoAddress: Winsock's inet_addr. One to four parts, each decimal, octal
// (a leading 0) or hexadecimal (0x); the last part fills the bytes the others leave ("10.1" is 10.0.0.1); a space ends
// the text.
std::uint32_t ParseIpv4(std::string_view text);

} // namespace lib
