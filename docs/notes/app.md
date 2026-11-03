# Application layer

Start-up, the main loop, the frame step, options, key bindings and small tables. Network code is in `network.md`, the
window and input in `platform.md`.

## WinMain

`app_main.cpp` (`app::GameMain`). Start-up order: the system messages (res\sysmsg.txt, two lines, built-in ones as
a fallback), the command line ("-moviepath_", "-nocam", .cam / .scr / .mob / .mpr files, "-devicetarget_"), a second
instance's hand-over (a running game is brought forward and this one exits), the settings (`ei_settings.ini`,
`docs/settings.md`), the two worlds and the camera (field of view pi * 2 / 7), the free disk space (a 300 MB warning,
`platform::FreeDiskSpace` of the data folder itself), the mouse object, the saves / mp /
textures folders, the window, the renderer, the sound, the intro movies (list from the .ini), the databases, the
cursors, the interface, the map list (`MapDatabase::LoadAll`: map.txt, then map-LMP.txt) and the first map or the main
menu.

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

The harness's hook points (`src/game/main_loop.h`, `g_harnessHooks`) sit at the loop's calls: the frame capture
between the interface draw and the cursor blit, "freeze" (the camera's update), "speed" (the server and client
updates), and the loop's message peeks and clock reads named in `ei::mainloop::g_site`.

Dropped on purpose: the original chose the interface or the window procedure for a message waiting before the first
pass by the top byte of a reused stack slot (it hung depending on where a string lay); ours always gives it to the
interface. Kept: `SetUpCursors`' last argument has only its low byte set; "res\speech.res" is built twice;
`FrameStep`'s movie capture passes two uninitialised words as the rectangle's size when there is no render target.

## Options

`GameOptions` (options.h) is built by a static initialiser before WinMain. Its constructor's values are the defaults
of every value the settings file lacks (the original's own defaults function ran before the registry and is gone).
What stays from the constructor: the "Network Settings" subkey, the CD labels, the font's character set, the keyboard
layout, both lights' radius; the master server's name is empty (EI_Plugin's NewMaster stopped saving it). Settings
are `ei_settings.ini` beside the exe, described by `src/game/settings_catalog.inc` (`docs/settings.md` is generated
from it by `tools/gen_settings_doc.py`, a build check).

## Small pieces

- `NamedFloatTable` (object_tables.cpp; party and player variables): a `lib::HashMap<std::string, float>`; Set of zero
  removes the entry (a NaN removes it too), CopyPrefixed, Load, Save.
- Key bindings (key_bindings.cpp): config\keyboard.ini of "command key" lines, key texts from the platform's key
  names; `platform.md` for the keys themselves.
- The lever table (lever_table.cpp, Levers.ldb) and the class factories (app_frame.cpp: type registration, vtable to
  id to factory).
- `DelayedProtectionCheck` (the original's copy protection on every new object: a 1% `rand()` draw, then a list nothing
  fills) is gone; the harness made the original's return at once, so nothing depends on its draw.
- Caught file errors: `platform::File` throws `platform::FileError` by value; `Dib::Read` / `Write` catch it, the
  script reader reads whole files with `platform::ReadWholeFile`, which does not throw.
