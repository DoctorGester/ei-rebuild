// Checks for WinMain (00456440, src/game/app_main.cpp; A-E2) and the operator new handler it
// installs (00455A30, src/game/app.cpp). Ours only: the application starts once.
//
// WinMain: the rewrite runs the whole session, so every scenario of the vanilla launch runs on it
// (its callees keep their own checks: each is called through its address). What is checked here:
// at entry no window exists yet; at the end the mouse object is released and the result is the quit
// message's wParam as the loop returned it (0 from PostQuitMessage(0), which is what the window
// procedure posts). Rebirth patches the original, so the hook is skipped in the Rebirth launch.
//
// The handler: reached only when the game's operator new fails; no scenario does.
#include "verify.h"

#include "../../src/game/app.h"

#include <windows.h>

#include <cstddef>

namespace {
void* s_unused;

HWND MainWindow() { return *reinterpret_cast<HWND*>(0x0079BC28); }
void* Mouse() { return *reinterpret_cast<void**>(0x0079BA08); }

int __stdcall GameMainVerify(HINSTANCE instance, HINSTANCE previous, char* commandLine, int show) {
    ei::verify::Check("WinMain (start)", true, MainWindow() == nullptr, "window %p", MainWindow());
    const int result = app::GameMain(instance, previous, commandLine, show);
    ei::verify::Check("WinMain (end)", true, Mouse() == nullptr, "mouse %p, result %d", Mouse(), result);
    return result;
}

int __cdecl OutOfMemoryVerify(std::size_t size) {
    const int result = app::OutOfMemory(size);
    ei::verify::Check("OutOfMemory", 0, result, "size %u", static_cast<unsigned>(size));
    return result;
}
} // namespace

EI_VERIFY(0x00456440, GameMainVerify, 0, s_unused);
EI_VERIFY(0x00455A30, OutOfMemoryVerify, 0, s_unused);
