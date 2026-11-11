# Application layer

Start-up, the main loop, the frame step, options, key bindings and small tables. Network code is in `network.md`, the
window and input in `platform.md`.

## WinMain

`main.cpp` runs `app::StartVideo` (a second instance's hand-over: a running game is brought forward and this one
exits; then SDL's video and the renderer's device on its thread) and then `app::GameMain` (`app/main_loop.cpp`), one list
in order: the original's static initialisers that do more than set values, the chunk archives' classes, the command
line ("-moviepath_", "-nocam", .cam / .scr / .mob / .mpr files), the settings (`ei_settings.ini`, `docs/settings.md`),
the two worlds and the camera (placed at the options' start focus; field of view pi * 2 / 7), the free disk space (a
300 MB warning, `platform::FreeDiskSpace` of the data folder itself; its text is line 1 of res\sysmsg.txt, read only
then, else a built-in one), the saves / mp / textures folders, the window, the sound, the databases, the map list
(`MapDatabase::LoadAll`: map.txt, then map-LMP.txt), the renderer and its gamma, the intro movies (list from the .ini),
the loading screen's first frame, the world's graphics, the cursors, the interface, the first map or the main menu,
and the loop.

There is no disc check and no "CD-ROM Path" setting: the movies (`Movies\`) and the voices (`res\speech.res`) are read
under the data folder (`Work path`), the game's own folder.

## Main loop

While no message waits, the time since the last pass (at most 5 ticks; 1000 / fps while frames are recorded) is added
to the server's and the client's backlog (the client's scaled by the client speed, dropped above 0x4000000). The
server runs whole ticks; the client runs one while it has a tick and either server updates wait or its view is not in
mode 2, and moves the camera a tick each time. Then the game flow's request, the tick fraction, the next client speed
from balance = waiting updates - fraction (e^(balance / 10) when negative, 1 up to 1, else sqrt(balance) * 0.1 + 0.9;
kept within 0.1 .. 10; 1 in a local game), the camera for the real time of the pass, the camera file's record /
playback, the interface and a frame (`FrameStep`). A waiting message goes to the interface's input.

A pass (`RunPass`) reads top to bottom: the window's activation taken into `SceneRenderer::running` (nothing more
while the window is inactive), the resolution and cursor confinement, the clock (`LoopClock`: `TimeSinceLastPass`,
`AdvanceClock`), the idle session's recovery, `net::Pump`, `RunWorlds`, the game flow's request and the tick
fraction, `UpdateClientSpeed`, `UpdateCamera`, `DrawFrame`. The places of the `net::Pump` calls are behaviour.
`SceneRenderer::running` is `g_active` as it was at the pass's start: a focus change handled inside the pass (a
loading screen's `PumpMessages`) reaches the frame, the cursor, the client's lighting and the main menu's lightning
only at the next pass.

The harness's hook points (`src/game/app/harness_hooks.h`, `g_harnessHooks`, each called as `CallOr(hook, function,
arguments...)`) sit at the loop's calls: the frame capture between the interface draw and the cursor blit, "freeze"
(the camera's update), "speed" (the server and client updates), the frame step, and the loop's message peeks and
clock reads named in `ei::mainloop::g_site`.

The tools built into the exe (src/tools; `ei-rebuild --effects`, effects.md "The effect viewer") join the loop
through `app::g_toolHooks` (src/game/app/tool_hooks.h). They are empty unless a tool was opened. There are four:
- `takeEvent`: an event before the interface gets it. The tool keeps it when its panel is under the mouse or typed
  into.
- `clientTicked`: after each client tick.
- `drawOverlay`: in the frame step, after the interface and before the harness's capture.
- `worldsEnding`: at the start of StopGame.

Dropped on purpose: the original chose the interface or the window procedure for an event waiting before the first
pass by the top byte of a reused stack slot (it hung depending on where a string lay); ours always gives it to the
interface. Kept: `SetUpCursors`' last argument has only its low byte set; "res\speech.res" is built twice;
`FrameStep`'s movie capture passes two uninitialised words as the rectangle's size when there is no render target.
Fixed: the original's message drain (`PumpMessages`, run by loading screens and window switches) and the movie loop
dispatched a posted quit to the window procedure, which ignored it, so Exit or a window close during a load was lost.
Ours leave the quit posted (the movie ends on it) and the main loop ends on it.

## Options

`GameOptions` (options.h) is a global that `Load` fills at the start of GameMain: every option from the settings
file, a missing one with its catalog row's default (the original's constructor held placeholder defaults, such as a
camera turning about 7 and pitching about 700 times as fast as an install's; they are gone). The master server's name
is read but not saved (EI_Plugin's NewMaster stopped saving it). Settings are `ei_settings.ini` beside the exe,
described by `src/game/app/settings_catalog.inc`, which holds each setting's one default (`docs/settings.md` is
generated from it by `tools/gen_settings_doc.py`, a build check).

The camera's defaults are freer than the original's (P12; Rebirth's starter offered them as its "Камера Чейза" preset):
the pitch -45..90 on the maps and the village screens, the distances 3..50 with hard limits 3..70 on both,
CameraMinHeight 0 and NearClipDistance 1. Every camera value is written at exit (`GameOptions::Save` writes the whole
table), so a file written by an earlier build keeps that build's values (30..60, 10..50, 3, 2, ...) until those lines
are deleted; the defaults reach new files and missing lines. The original's FarClipDistance is not read (the
projection has no far plane and everything in view is drawn: renderer.md, "The view's depth"); a file that holds it
keeps the line. The camera's side: core.md, "Camera".

## Small pieces

- `NamedFloatTable` (named_floats.cpp; party and player variables): a `lib::HashMap<std::string, float>`; Set of zero
  removes the entry (a NaN removes it too), CopyPrefixed, Load, Save.
- Key bindings (key_bindings.cpp): config\keyboard.ini of "command key" lines, key texts from the platform's key
  names; `platform.md` for the keys themselves.
- The lever table (objects/lever.cpp, Levers.ldb).
- `DelayedProtectionCheck` (the original's copy protection on every new object: a 1% `rand()` draw, then a list nothing
  fills) is gone; the harness made the original's return at once, so nothing depends on its draw.
- Caught file errors: `platform::File` throws `platform::FileError` by value; `Dib::Read` / `Write` catch it, the
  script reader reads whole files with `platform::ReadWholeFile`, which does not throw.
