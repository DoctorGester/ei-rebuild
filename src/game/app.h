#pragma once

// WinMain (00456440, app_main.cpp) and the application helpers around it: the window procedure,
// the input message dispatcher, the message pump and the start of the first map.
// Names are provisional; see docs/notes/objects.md ("Rewritten").

#include <windows.h>

#include <cstdarg>
#include <cstddef>

namespace app {

// 00457D40. The game window's procedure (also called directly by the pump and the dispatcher).
LRESULT __stdcall WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
// 00457970 (__fastcall: message, wParam; lParam on the stack). Sends keyboard and mouse messages
// to the interface (007C7D50) and returns 1; anything else goes to WindowProc.
LRESULT __fastcall DispatchInput(UINT message, WPARAM wParam, LPARAM lParam);
// 00458220. Removes every waiting message and passes it to WindowProc (no DispatchMessage).
void __cdecl PumpMessages();
// 00456140. Without a map on the command line (0079BA14 empty) opens the main menu; otherwise
// loads the map into the world server and runs the command line's script file. Returns 1, or
// FatalError's result.
int __cdecl StartFirstMap();
// 004560D0 (ECX text, EDX MessageBox flags; A-E2). The game's message box over the main window,
// under the window's title: in full screen (the device object's flag 1) DirectDraw first flips to
// the GDI surface (a failure is reported through FatalErrorCode: "Can't flip to GDI surface.");
// the cursor that was set is put back afterwards. Returns MessageBoxA's answer.
int __fastcall ShowMessageBox(const char* text, int flags);
// 00455A30 (cdecl; A-E2). The operator new handler WinMain installs (_set_new_handler; the previous
// one is put back when the game ends): "Memory depleted" in the game's message box with the stop
// icon. Returns 0, so the allocation fails. Ghidra has no function there: only WinMain refers to
// it, by address (config/extra_function_sizes.csv).
int __cdecl OutOfMemory(std::size_t size);
// 00456440 (stdcall; A-E2; app_main.cpp). WinMain: the application's start-up, main loop and
// shutdown. Rebirth patches it (NivalRegistry), so the rewrite runs in the vanilla launch only.
int __stdcall GameMain(HINSTANCE instance, HINSTANCE previous, char* commandLine, int show);
// 004800E0 (cdecl, printf style, plain RET; A-S9). A DirectX failure: the formatted text (1 KB),
// then "%s (0x%.8X) %s" with the result and its DirectXErrorText in a message box and to the error
// hook; the error counter is raised and the game exits (exit(-1)). Not patched by Rebirth. Returns 0.
int __cdecl FatalErrorCode(long result, const char* format, ...);
// The same with the arguments as a va_list (the verify wrapper's entry).
int FatalErrorCodeV(long result, const char* format, va_list args);

} // namespace app
