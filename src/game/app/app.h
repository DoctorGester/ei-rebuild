#pragma once

// WinMain and the application helpers around it: the window's events, the input dispatcher, the event pump and the
// start of the first map (docs/notes/app.md).

#include "platform/platform.h"

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <string>

namespace platform {
struct Event;
}
namespace app {

// The application's globals: what the command line gave, the folder of the movie frames
// ("-moviepath_"), a map (".mpr"), a mob file (".mob"), a script (".scr") and the camera file (".cam").
extern std::string g_movieFolder;
extern std::string g_commandLineMap;
extern std::string g_commandLineMob;
extern std::string g_commandLineScript;
extern std::string g_cameraFile;
// The milliseconds of real time one world update takes (55; the speed-up key sets 27, the main loop reads it).
extern int g_updatePeriod;

// The application's flags and handles. The game window's title (from the string table); the window's activation as
// its focus events leave it (the main loop's pass takes it into SceneRenderer::running); whether the interface shows a screen
// (set when a screen is activated, cleared when every window is closed: the main loop runs its frames only
// while it is set); a movie of
// the frames being written (the loop then runs a fixed time a frame); the console's switch that prints the
// client speed; what the video memory allows (little memory besides the textures, little texture memory) and
// whether a device was named on the command line; the loading screen's movie file. g_mainWindow is the game window.
extern char g_windowTitle[256];
extern platform::NativeWindow g_mainWindow;
extern std::uint8_t g_active;
extern std::uint8_t g_interfaceHasScreen;
extern std::uint8_t g_recordingFrames;
extern int g_showClientSpeed;
extern const char* g_loadingMovieName;

// The game window's own events (the focus, the close request; input is left out), wherever a loop takes them: the main
// loop, the pump, the dispatcher and the movie player.
void HandleWindowEvent(const platform::Event& event);
// Sends keyboard and mouse events to the interface; anything else goes to HandleWindowEvent.
void DispatchInput(const platform::Event& event);
// Takes every waiting event and passes it to HandleWindowEvent (input dropped); a quit request stays for the main
// loop.
void PumpMessages();
// The music stream's steps due by now (SoundManager::FollowClock on the frame clock), from the passes that hold the
// main loop longer than a stream half plays: the loading screen's steps and the system's modal loops.
void StepMusic();
// Without a .mob on the command line opens the main menu (a tool's own scene, app::g_toolScene: a new game instead);
// otherwise loads the map into the world server and runs the command line's script file.
void StartFirstMap();
// One frame: the interface drawn and the scene presented, nothing while the application is inactive.
void FrameStep(bool draw);
// Plays the movies listed under `section` of config\movie.ini.
void PlayMovies(const std::string& section, bool loadingStep);
// The game's error box (OK, the error icon) over the main window, under the window's title; the cursor that was set is
// put back afterwards.
void ShowMessageBox(std::string_view text);
// The box of the game's library code (AfxMessageBox): no owner, the program's file name as its title, OK with the
// warning icon.
void ShowLibraryMessageBox(std::string_view text);
// The rendering resolution changed while the game runs, the one way every change takes (a window resized: the present
// asks through RequestResolution; a resolution chosen in the options: ChooseResolution): the
// device's screen, the interface layout (screen_layout.h: the 4:3 part, the widescreen margins and axis scales), the
// camera's viewport and axis swap, the cursor's bounds, the interface (fonts, surfaces and the windows built for the
// window's shape: Interface3D::Resize) and the settings' VideoResolution (written at exit) take width x height.
// The renderer's frame target follows at the next frame and the window's swap chain at its present; no device is
// made again and the zone goes on. False (nothing changed) for a size outside 1..16384 or the size it has.
bool SetResolution(int width, int height);
// SetResolution asked for at the next main loop pass: the present notices a window's new size in the middle of a
// frame (and the movie players and loading screens present outside the loop, which takes it after them).
void RequestResolution(int width, int height);
// A resolution chosen in the options (their OK): asked for at the next main loop pass (RequestResolution, between
// frames) and, in a window that is not full screen, the window's client area sized to it now
// (present_window::SizeClient). Full screen keeps its window over the monitor: the frame is scaled over it, the
// display mode never changes.
void ChooseResolution(int width, int height);
// A display mode chosen in the options (their OK): full screen (a borderless window over the monitor, the frame scaled
// into it) or a window of the resolution's size; the settings' FullScreen (written with the options) takes it.
void ChooseFullScreen(bool fullScreen);
// At a main loop pass's start: the requested resolution taken.
void TakeResolutionChange();
// The cursor confined to the window (platform::ConfineCursor) while the game's window is the active one and the camera
// pans at the screen's edges (Interface3D::EdgePanLive: the world view is up and the border scroll area is above 0);
// free in the menus, the escape menu and the modal windows. Asked at each main loop pass and at the window's focus
// changes; the request goes to the platform only when the answer changes. The pan at the screen's edge is stopped when
// the window loses the focus, so the camera never pans for a cursor that is outside.
void UpdateCursorConfinement();
// main's first step: SDL's video up and the renderer's device started on its thread, so that most of the device's time
// passes while GameMain starts up.
void StartVideo();
// WinMain: the application's start-up, main loop and shutdown.
int GameMain(char* commandLine);
// The mod the game plays (docs/notes/mods.md): `setting` is the settings' [game] Mod, the mod's folder relative to the
// exe's folder or in full, empty for none (platform::SwitchMod over `gameFolder`); the mod, or its folder missing, is
// logged. main's, before GameMain, and each switch of mods.
void UseMod(const std::string& gameFolder, const std::string& setting);
// A switch of mods (docs/notes/mods.md, "Switching mods"), `setting` as UseMod's: at the main loop's next pass the game
// stops (StopGame), the mod changes, the settings' Mod takes `setting` and the game starts again (StartGame) in its
// main menu; the renderer, the window and the audio device stay. Only in the main menu (its window on top) with no
// network session: false and nothing asked otherwise.
bool RequestModSwitch(const std::string& setting);
// The game folder UseMod was given (the main menu's list of mods scans its starter\Mods).
const std::string& GameFolder();
// Whether a switch asked for is still to be made.
bool ModSwitchPending();

} // namespace app
