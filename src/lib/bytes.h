#pragma once

// Plain values in raw byte buffers (file images, network cursors), where the address may be unaligned: the
// bytes of `value` copied from / to `address` (a float's too, a signalling NaN's bits kept).

#include <cstddef>
#include <cstring>
#include <type_traits>

namespace lib {

// sizeof(T) bytes from `address` into `value`.
template <class T>
void LoadBytes(T& value, const void* address) {
    static_assert(std::is_trivially_copyable_v<T>, "plain values");
    std::memcpy(&value, address, sizeof(T));
}

// sizeof(T) bytes of `value` to `address`.
template <class T>
void StoreBytes(void* address, const T& value) {
    static_assert(std::is_trivially_copyable_v<T>, "plain values");
    std::memcpy(address, &value, sizeof(T));
}

} // namespace lib
