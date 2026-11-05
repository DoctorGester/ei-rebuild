#pragma once

// Fixed scales of fixed-width values.
namespace lib {

// A byte (a colour channel, a speed in 0..255) scaled to 0..1: the float nearest 1 / 255.
inline constexpr float kByteScale = 1.0f / 255.0f;

// A random word scaled to [0, 1) (random.h).
inline constexpr float kWordScale = 0x1p-32f;

} // namespace lib
