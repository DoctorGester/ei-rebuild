#pragma once

// WinMain (00456440, app_main.cpp) and the application helpers around it: the window procedure,
// the input message dispatcher, the message pump and the start of the first map.
// Names are provisional; see docs/notes/objects.md ("Rewritten").

#include <windows.h>

#include <cstdarg>
#include <cstddef>
#include <cstdint>

class CDirectInputMouse;   // ui_cursor.h
namespace mfc {
class CString;
} // namespace mfc

namespace app {

// The application's globals (app_main.cpp): what the command line gave, the folder of the movie frames
// ("-moviepath_"), a map (".mpr"), a mob file (".mob"), a script (".scr") and the camera file (".cam"); and the
// mouse object (WinMain keeps one reference).
mfc::CString& MovieFolder();
mfc::CString& CommandLineMap();
mfc::CString& CommandLineMob();
mfc::CString& CommandLineScript();
mfc::CString& CameraFile();
CDirectInputMouse*& Mouse();

// The application's flags and handles (app_main.cpp). The game window's title (from the string table), the
// instance; the window's activation as the window procedure leaves it; whether the interface shows a screen
// (set when a screen is activated, cleared when every window is closed: the main loop runs its frames only
// while it is set); whether QuitGame may post the quit message (cleared while a timed movie plays); a movie of
// the frames being written (the loop then runs a fixed time a frame); the console's switch that prints the
// client speed; what the video memory allows (little memory besides the textures, little texture memory) and
// whether a device was named on the command line; the loading screen's movie file. g_mainWindow is the game window.
extern char g_windowTitle[256];
extern HINSTANCE g_instance;
extern HWND g_mainWindow;
extern std::uint8_t g_active;
extern std::uint8_t g_activationChanged;
extern std::uint8_t g_interfaceHasScreen;
extern std::uint8_t g_quitAllowed;
extern std::uint8_t g_recordingFrames;
extern int g_showClientSpeed;
extern std::uint8_t g_littleOtherMemory;
extern std::uint8_t g_littleTextureMemory;
extern std::uint8_t g_deviceNamed;
extern const char* g_loadingMovieName;

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
// 00458010 (CL draw, DL cursor; app_frame.cpp). One frame: the interface drawn and the scene presented, nothing
// while the application is inactive.
void __fastcall FrameStep(bool draw, bool cursor);
// 00458270 (ECX section, DL clear first; app_frame.cpp): plays the movies listed under `section`, clearing the
// screen before and, with `clearAfter`, after each.
void __fastcall PlayMovies(const char* section, bool clearFirst, bool clearAfter, int a, int b);
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
