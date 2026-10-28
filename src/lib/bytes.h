#pragma once

// Plain values in raw byte buffers (file images, network cursors), where the address may be unaligned: the
// bytes of `value` copied from / to `address`. A float or double is not taken here: copying one through an
// x87 register quietens a signalling NaN, so a float that arrives as bytes goes through x87::CopyBits
// (include/ei/x87.h).

#include <cstddef>
#include <cstring>
#include <type_traits>

namespace lib {

// sizeof(T) bytes from `address` into `value`.
template <class T>
void LoadBytes(T& value, const void* address) {
    static_assert(std::is_trivially_copyable_v<T> && !std::is_floating_point_v<T>, "plain integers and structs");
    std::memcpy(&value, address, sizeof(T));
}

// sizeof(T) bytes of `value` to `address`.
template <class T>
void StoreBytes(void* address, const T& value) {
    static_assert(std::is_trivially_copyable_v<T> && !std::is_floating_point_v<T>, "plain integers and structs");
    std::memcpy(address, &value, sizeof(T));
}

} // namespace lib
