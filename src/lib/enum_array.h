#pragma once

// An array indexed by an enum class: `counts[Count::Draws]` needs no cast, and a plain integer still indexes it where a
// loop or a saved number does.
//     lib::EnumArray<float, MoveFactor, 4> moveFactors;

#include <array>
#include <concepts>
#include <cstddef>
#include <type_traits>

namespace lib {

// The enumerator's number as an index.
template <class E>
    requires std::is_enum_v<E>
constexpr std::size_t Index(E value) {
    return static_cast<std::size_t>(static_cast<std::underlying_type_t<E>>(value));
}

template <class T, class E, std::size_t N>
struct EnumArray {
    std::array<T, N> items{};

    constexpr T& operator[](E index) { return items[Index(index)]; }
    constexpr const T& operator[](E index) const { return items[Index(index)]; }
    template <std::integral I>
    constexpr T& operator[](I index) {
        return items[static_cast<std::size_t>(index)];
    }
    template <std::integral I>
    constexpr const T& operator[](I index) const {
        return items[static_cast<std::size_t>(index)];
    }

    constexpr T* data() { return items.data(); }
    constexpr const T* data() const { return items.data(); }
    constexpr auto begin() { return items.begin(); }
    constexpr auto end() { return items.end(); }
    constexpr auto begin() const { return items.begin(); }
    constexpr auto end() const { return items.end(); }
    static constexpr std::size_t size() { return N; }
};

} // namespace lib
