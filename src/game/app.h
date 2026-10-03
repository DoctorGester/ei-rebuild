#pragma once

// Application helpers around WinMain (00456440, patched by Rebirth and not rewritten): the window
// procedure, the input message dispatcher, the message pump and the start of the first map.
// Names are provisional; see docs/notes/objects.md ("Fourth round").

#include <windows.h>

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

} // namespace app
