#pragma once

// Bit operators for the enum classes that name the bits of a flag word. An enum opts in with
//     template <> inline constexpr bool lib::kBitFlags<MapObjectFlag> = true;
// after which `|`, `&`, `^`, `~` and the assignment forms work on the enum, and a plain std::uint32_t word (a
// saved or sent field, which keeps its number) can be tested and changed with the enum's bits:
//     flags_ |= MapObjectFlag::Hidden;   flags_ &= ~MapObjectFlag::Hidden;   if (lib::Has(flags_, MapObjectFlag::Hidden))

#include <cstdint>
#include <type_traits>

namespace lib {

template <class E> inline constexpr bool kBitFlags = false;

template <class E> concept BitFlags = std::is_enum_v<E> && kBitFlags<E>;

template <BitFlags E> constexpr std::uint32_t Bits(E bit) { return static_cast<std::uint32_t>(bit); }

// Whether the word has any of the bits.
template <BitFlags E> constexpr bool Has(std::uint32_t word, E bits) { return (word & Bits(bits)) != 0; }

// The same for a field typed as the enum.
template <BitFlags E> constexpr bool Has(E flags, E bits) { return (Bits(flags) & Bits(bits)) != 0; }

} // namespace lib

template <lib::BitFlags E> constexpr E operator|(E a, E b) { return static_cast<E>(lib::Bits(a) | lib::Bits(b)); }
template <lib::BitFlags E> constexpr E operator&(E a, E b) { return static_cast<E>(lib::Bits(a) & lib::Bits(b)); }
template <lib::BitFlags E> constexpr E operator^(E a, E b) { return static_cast<E>(lib::Bits(a) ^ lib::Bits(b)); }
template <lib::BitFlags E> constexpr E operator~(E a) { return static_cast<E>(~lib::Bits(a)); }
template <lib::BitFlags E> constexpr E& operator|=(E& a, E b) { return a = a | b; }
template <lib::BitFlags E> constexpr E& operator&=(E& a, E b) { return a = a & b; }
template <lib::BitFlags E> constexpr E& operator^=(E& a, E b) { return a = a ^ b; }

template <lib::BitFlags E> constexpr std::uint32_t operator|(std::uint32_t word, E bits) { return word | lib::Bits(bits); }
template <lib::BitFlags E> constexpr std::uint32_t operator&(std::uint32_t word, E bits) { return word & lib::Bits(bits); }
template <lib::BitFlags E> constexpr std::uint32_t& operator|=(std::uint32_t& word, E bits) { return word |= lib::Bits(bits); }
template <lib::BitFlags E> constexpr std::uint32_t& operator&=(std::uint32_t& word, E bits) { return word &= lib::Bits(bits); }
template <lib::BitFlags E> constexpr std::uint32_t& operator^=(std::uint32_t& word, E bits) { return word ^= lib::Bits(bits); }
