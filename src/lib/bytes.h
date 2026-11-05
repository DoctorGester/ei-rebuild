#pragma once

// Plain values in raw byte buffers (file images, network cursors), where the address may be unaligned: the
// bytes of `value` copied from / to `address` (a float's too, a signalling NaN's bits kept).

#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace lib {

namespace detail {
template <class M>
struct FieldOf;
template <class C, class F>
struct FieldOf<F C::*> {
    using Class = C;
    using Type = F;
};
template <auto a, auto b>
consteval bool SameField() {
    if constexpr (std::is_same_v<decltype(a), decltype(b)>) return a == b;
    else return false;
}
template <auto field, auto... all>
consteval int CountField() {
    return (int{SameField<field, all>()} + ...);
}

// A struct declares its fields (FieldsFill below) with a NoPadding function found by argument-dependent lookup.
template <class T>
concept DeclaresFields = requires(const T* p) { NoPadding(p); };
template <class T>
consteval bool DeclaredFieldsFill() {
    if constexpr (DeclaresFields<T>) return NoPadding(static_cast<const T*>(nullptr));
    else return false;
}
} // namespace detail

// Every byte of a T is a field's: a T copied whole as its bytes writes no padding (whatever was in memory before
// it: a save would differ from run to run). Integers, enums and structs of them: the standard trait. A float, an
// array of such values, or a struct with floats (the trait rejects a float for its two zeros) that declares its
// fields next to its definition, in its own namespace:
//   constexpr bool NoPadding(const Vector2*) { return lib::FieldsFill<&Vector2::x, &Vector2::y>(); }
template <class T>
inline constexpr bool kNoPadding = std::has_unique_object_representations_v<T> || std::is_floating_point_v<T> ||
                                   detail::DeclaredFieldsFill<T>();
template <class T, std::size_t N>
inline constexpr bool kNoPadding<T[N]> = kNoPadding<T>;
template <class T, std::size_t N>
inline constexpr bool kNoPadding<std::array<T, N>> = kNoPadding<T> && sizeof(std::array<T, N>) == sizeof(T[N]);

// True: the fields (pointers to members of one class, each once) have no padding and their sizes add up to the
// class's, so no byte is left over. A struct with a field not listed, a base or padding fails to compile.
template <auto first, auto... rest>
consteval bool FieldsFill() {
    using Class = typename detail::FieldOf<decltype(first)>::Class;
    static_assert((std::is_same_v<typename detail::FieldOf<decltype(rest)>::Class, Class> && ...),
                  "the fields of one class");
    static_assert(detail::CountField<first, first, rest...>() == 1 &&
                      ((detail::CountField<rest, first, rest...>() == 1) && ...),
                  "each field once");
    static_assert(kNoPadding<typename detail::FieldOf<decltype(first)>::Type> &&
                      (kNoPadding<typename detail::FieldOf<decltype(rest)>::Type> && ...),
                  "fields without padding");
    static_assert(sizeof(typename detail::FieldOf<decltype(first)>::Type) +
                          (sizeof(typename detail::FieldOf<decltype(rest)>::Type) + ... + 0) ==
                      sizeof(Class),
                  "the fields fill the class: name its padding (a zero field) or list the missing field");
    return true;
}

// A value a file, a save, a message or an archive holds as its bytes: plain, without padding, and of one size in
// every build. A pointer or an integer wider than 32 bits (std::ssize, size_t and ptrdiff_t in a 64-bit build)
// would change the format with the build; none of the formats has a wider integer. A count goes in as
// std::int32_t.
template <class T>
inline constexpr bool kFormatValue = std::is_trivially_copyable_v<T> && kNoPadding<T> && !std::is_pointer_v<T> &&
                                     !std::is_member_pointer_v<T> && !(std::is_integral_v<T> && sizeof(T) > 4);

// Every format is little-endian, as every build's host: a value's bytes are its memory image.
static_assert(std::endian::native == std::endian::little, "the formats are little-endian images");

// The T whose bytes are the sizeof(T) at `address`.
template <class T>
T Load(const void* address) {
    static_assert(kFormatValue<T>, "plain bytes without padding, one size in every build");
    T value;
    std::memcpy(&value, address, sizeof(T));
    return value;
}

// The sizeof(T) bytes of `value` to `address`.
template <class T>
void Store(void* address, const T& value) {
    static_assert(kFormatValue<T>, "plain bytes without padding, one size in every build");
    std::memcpy(address, &value, sizeof(T));
}

// The float whose bits are `bits`. A float argument would convert to an integer first: callers pass raw bits only.
inline float FloatFromBits(std::uint32_t bits) { return std::bit_cast<float>(bits); }
template <std::floating_point T> float FloatFromBits(T) = delete;

} // namespace lib
