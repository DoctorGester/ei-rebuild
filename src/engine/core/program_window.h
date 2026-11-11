#pragma once

// The program's window and its title, and the message boxes the engine shows: the error box over the window
// (lib/crt.h's game::FatalExit, the renderer's fatal errors) and the library code's box. program_window.cpp also
// defines lib/crt.h's game::FatalExit and game::Warning.

#include "platform/platform.h"

#include <string_view>

// The program's window (null until the program makes it: app/main_loop.cpp) and its title.
extern platform::NativeWindow g_mainWindow;
extern char g_windowTitle[256];

// The error box (OK, the error icon) over the program's window, under the window's title; the cursor that was set is
// put back afterwards.
void ShowErrorBox(std::string_view text);
// The box of the game's library code (AfxMessageBox): no owner, the program's file name as its title, OK with the
// warning icon.
void ShowLibraryMessageBox(std::string_view text);
