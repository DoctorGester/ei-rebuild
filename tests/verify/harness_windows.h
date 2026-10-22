#pragma once

// The interface windows by game.exe's table (C19; docs/notes/standalone.md, "Compiler-made vtables"). The image builds
// the windows with compiled tables (ui_window_class.h, ui_container_class.h); the harness names a window by game.exe's
// table of its kind (`&ei::OriginalGameData<const std::uint8_t>(address)`), which is what game.exe's run (golden
// recording) has at the window's +0, as the digest names the AI modes (harness_digest.cpp, ModeKinds). Verify build
// only.

#include <cstdint>

namespace ei::verify {

// game.exe's table a window's table stands for: game.exe's for a compiled table of the image, else `vtable` itself.
std::uintptr_t RealWindowVtable(std::uintptr_t vtable);

} // namespace ei::verify
