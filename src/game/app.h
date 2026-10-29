#pragma once

// WinMain and the application helpers around it: the window procedure,
// the input message dispatcher, the message pump and the start of the first map.
// Names are provisional; see docs/notes/objects.md ("Rewritten").

#include "lib/win_types.h"

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <string>

class DirectInputMouse;
namespace app {

// The application's globals: what the command line gave, the folder of the movie frames
// ("-moviepath_"), a map (".mpr"), a mob file (".mob"), a script (".scr") and the camera file (".cam"); and the
// mouse object (WinMain keeps one reference).
extern std::string g_movieFolder;
extern std::string g_commandLineMap;
extern std::string g_commandLineMob;
extern std::string g_commandLineScript;
extern std::string g_cameraFile;
extern DirectInputMouse* g_mouse;

// The application's flags and handles. The game window's title (from the string table), the
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
extern const char* g_loadingMovieName;

// The game window's procedure (also called directly by the pump and the dispatcher).
LRESULT __stdcall WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
// Sends keyboard and mouse messages to the interface and returns 1; anything else goes to WindowProc.
LRESULT DispatchInput(UINT message, WPARAM wParam, LPARAM lParam);
// Removes every waiting message and passes it to WindowProc (no DispatchMessage).
void PumpMessages();
// Without a map on the command line opens the main menu; otherwise loads the map into the
// world server and runs the command line's script file. Returns 1, or FatalError's result.
int StartFirstMap();
// One frame: the interface drawn and the scene presented, nothing while the application is inactive.
void FrameStep(bool draw, bool cursor);
// Plays the movies listed under `section` of config\movie.ini.
void PlayMovies(const std::string& section, bool loadingStep);
// The game's message box over the main window, under the window's title; the cursor that was set is put back
// afterwards. Returns MessageBoxA's answer.
int ShowMessageBox(const char* text, int flags);
// The box of the game's library code (AfxMessageBox): no owner, the module's file name as its title, OK with an
// exclamation icon. Returns MessageBoxA's answer.
int ShowLibraryMessageBox(const char* text);
// WinMain: the application's start-up, main loop and shutdown. Rebirth patches
// it (NivalRegistry), so the rewrite runs in the vanilla launch only.
int GameMain(HINSTANCE instance, HINSTANCE previous, char* commandLine, int show);
// A DirectX failure: the formatted text (1 KB), then "%s (0x%.8X) %s" with the result and its
// DirectXErrorText in a message box and to the error hook; the error counter is raised and the
// game exits (exit(-1)). Not patched by Rebirth. Returns 0.
int FatalErrorCode(long result, const char* format, ...);
int FatalErrorCodeV(long result, const char* format, va_list args);

} // namespace app
