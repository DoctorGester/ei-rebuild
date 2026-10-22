#pragma once

// The interface windows by game.exe's table (C19, C28; docs/notes/standalone.md, "Compiler-made vtables"). The image
// builds the windows as C++ classes (some still over compiled tags, ui_window_class.h, ui_container_class.h); the
// harness names a window by game.exe's table of its kind (`&ei::OriginalGameData<const std::uint8_t>(address)`),
// which is what game.exe's run (golden recording) has at the window's +0, as the digest names the AI modes
// (harness_digest.cpp, ModeKinds). Verify build only.

#include <cstdint>

namespace ei::verify {

// game.exe's table of `window`'s kind: its +0 in game.exe's run; in the image the table of its class (by its C++
// type, or its tag's table), else its +0.
std::uintptr_t WindowKind(const void* window);

} // namespace ei::verify
