#pragma once

// The game's key codes (docs/notes/platform.md, "Window and events"): a key event's `key` is one of these, on every
// system. The values are the Windows virtual-key codes, which the bindings file and the options keep.

namespace platform {

inline constexpr int kKeyBack = 0x08;
inline constexpr int kKeyTab = 0x09;
inline constexpr int kKeyReturn = 0x0D;
inline constexpr int kKeyShift = 0x10;
inline constexpr int kKeyControl = 0x11;
inline constexpr int kKeyMenu = 0x12;       // Alt
inline constexpr int kKeyEscape = 0x1B;
inline constexpr int kKeySpace = 0x20;
inline constexpr int kKeyPageUp = 0x21;
inline constexpr int kKeyPageDown = 0x22;
inline constexpr int kKeyEnd = 0x23;
inline constexpr int kKeyHome = 0x24;
inline constexpr int kKeyLeft = 0x25;
inline constexpr int kKeyUp = 0x26;
inline constexpr int kKeyRight = 0x27;
inline constexpr int kKeyDown = 0x28;
inline constexpr int kKeyDelete = 0x2E;
inline constexpr int kKeyF4 = 0x73;

} // namespace platform
