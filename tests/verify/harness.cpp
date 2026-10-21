// Automated test harness (verify build only).
//
// When ei_launch runs with --harness <scenario>, it sets EI_HARNESS to the scenario path. The
// scenario is a text file of steps, run on the game's main thread from its message loop:
//
//   wait <milliseconds>       let the game run (prefer a step that waits for the state wanted:
//                             waitmenu, waitwindow, waitfor, waitline, waitzone, frames)
//   waitmenu [seconds]        wait until the main menu is up and takes input (as "waitwindow
//                             <seconds> menu", default 30 s): the first step of a scenario, before
//                             its first console, newgame or menu click
//   waitwindow <seconds> <window>
//                             wait until <window> is the window on top of the interface and takes
//                             input: no window queued (005F57C0 activates it on the next frame), no
//                             game request pending (007AFE20), the save/load and options windows
//                             done sliding in (they ignore input meanwhile), on top for two frames;
//                             fails after <seconds>. <window>: menu, battle, briefing, map, camp,
//                             view, trade, escmenu, saveload, options, objectives, help, msgbox,
//                             difficulty, heroes, titles, or a window vtable in hex
//   waitunit <seconds> <name> wait until the unit with script name <name> is drawn and the game's
//                             picking finds it, as "clickunit" needs (a unit just added to the
//                             server reaches the client and the screen a few frames later)
//   waitidle <seconds> [<frames>]
//                             wait until the local player's leader (player 0's first party member in the
//                             server world) has stood idle for <frames> frames in a row (default 10): no
//                             action (a walk that ended or that a fight stopped, a fight over) and no path
//   waitdead <seconds> <id> ...
//                             wait until each of the server world's units <id> is dead or gone
//   waitaward <seconds>       wait until the leader's experience or its party's money changes (an award
//                             applied: ClientSession::ApplyAwards; net_pair_quest): from what they were at
//                             the last "awardfrom" step, or else at this step
//   awardfrom                 note the leader's experience and money for the next waitaward (an award that
//                             may come while the steps between still run)
//                             (these three read the server world after the frame's update, as the
//                             digest does: they end in the same frame every run)
//   frames <n>                let the game run <n> frames of its main loop (after the input posted
//                             so far has been handled): for what follows a posted key or mouse
//                             step on the next frames, whatever the frame rate
//   console <command line>    run a console command (the game's own console: exec, server, give,
//                             ...). After the cheat word ("console thingamabob"), "@<command>" runs a
//                             world script command and "#<expression>" prints a script expression.
//   newgame                   start a new single-player game, as the main menu does (no intro)
//   load <slot>               load a saved game (a folder in harness_saves, see below)
//   key <key>                 press and release a key in the game window: a letter or digit,
//                             or esc, enter, space, tab, f1..f12, up, down, left, right, plus, minus,
//                             grave (the console key), backspace, delete, home, end, pgup, pgdn, ctrl,
//                             enterchar (Enter with its WM_CHAR, as text input such as the console needs).
//                             A letter or digit is typed once (its WM_CHAR, lower case); the game's
//                             TranslateMessage translates as the US layout with no key held, whatever the
//                             user's own keyboard does (HarnessTranslateMessage).
//                             Input steps (key and the mouse steps below) are posted from the game's
//                             main loop only: the pump that window switches run drops waiting input
//                             (see PeekMessageHook)
//   click <x> <y>, rclick <x> <y>, move <x> <y>
//                             left/right click or move the mouse at (x, y) in the 800x600 layout every
//                             interface screen is designed for (scaled to the window's client size)
//   dblclick <x> <y>, rdblclick <x> <y>
//                             a left / right double click there (down, up, WM_xBUTTONDBLCLK, up)
//   ldown <x> <y>, lup <x> <y>, rdown <x> <y>, rup <x> <y>
//                             only the left / right button's press or release there (a button held
//                             over frames, e.g. the camp trade panel's repeated school purchase, the
//                             global map turned by a right-button drag)
//   wheel <x> <y> <delta>    turn the mouse wheel by <delta> (120 a notch; negative: towards the
//                             user) with the mouse at (x, y) in the 800x600 layout
//   clickregion <n>, rclickregion <n>, dblclickregion <n>, moveregion <n>
//                             the same at the centre of clickable region <n> of the window on top
//                             of the interface (its regions as the screen's code numbers them;
//                             placed with the game's own scaling, so Rebirth's widescreen layout too)
//   clickunit <name> [l|r|move|dbl] [ctrl]
//                             left click (default), right click, the second half of a double click
//                             (double click message and release: what follows a first click that
//                             missed the unit) or move the mouse on the unit with
//                             script name <name> (as GetObjectByName) where the game's own picking
//                             (00565E40) finds it in this frame; modifiers as below. Fails when the unit is not drawn or always hidden
//                             behind another object. "#<id>" names a map object by id instead (a
//                             lever, "clickunit #1889"): found by the battle screen's object pick
//                             (005659D0), as waitunit does
//   clickground <x> <y> [click|dblclick|rclick|move] [mods]
//                             that mouse step (default click) at the screen point where the game's ground
//                             pick (0056C350, what a walk order asks) finds map point (x, y), searched over
//                             the 800x600 layout; fails when no point comes within 1.5 units (off screen)
//   groundview                log the map points the ground pick finds at the layout's centre and corners
//   minimap <cx> <cy> <sx> <sy>, lookat <x> <y>
//                             the minimap's centre (layout) and map units per pixel; lookat clicks the
//                             minimap to bring the camera toward map point (x, y) (see LookAt)
//   hold <mods>, release <mods>
//                             press / release the modifier keys <mods> (ctrl, shift, alt; separated by
//                             spaces, "+" or ","), held over frames and further steps. The game takes
//                             them from the window messages (CInterface3D +0x7C Ctrl / +0x7D Shift /
//                             +0x7E Alt), never the real keyboard, so the original game and the image
//                             see the same. Alt is sent as WM_SYSKEYDOWN / WM_SYSKEYUP, as Windows does
//   <mods> as the last words of key, click, rclick, move, dblclick, rdblclick, ldown, lup, rdown, rup,
//   wheel, clickregion, rclickregion, dblclickregion, moveregion, clickunit (e.g. "click 90 320 shift", "key 1 ctrl",
//   "clickunit Wolf0 l ctrl+shift"): the modifiers are pressed just before the step's input and
//   released just after it (a plain "key ctrl" is still the key ctrl)
//   hudstate [minquick]       log the battle screen's pending action, the party's selection and
//                             quick items, the quick item panel and the spell column; with
//                             <minquick>, fail when the quick item panel shows fewer of its unit's
//                             own quick items (ui_quick_items)
//   campputon <type> <count>  with the camp on top: put the first <count> of the player's items
//                             of class type <type> (hex, e.g. 3006 quick items) on the hero, as a
//                             click on them in the item strip does (the camp's slot 42, mode 1)
//   screen <id> [constr]      ask the server for interface screen <id> as the global map's buttons
//                             do (4 the global map, 5 the camp; the battle screen is left). For the
//                             camp, <constr> (default 0: none) is the constructor / trader class it
//                             opens with, as a base dialogue's "constrN" answer (006064E0) sets it
//                             (classes 1, 3, 4, 5 after a new game); fails when there is no such
//                             class template
//   chat 1|0                  open the chat line (the interface's root) as a multiplayer client's Enter
//                             does, so that its typed line and caret are drawn; or close it
//   netcut                    in a pair's game: its network cable pulled from now on (harness_pair.h): nothing
//                             it sends arrives, what the other game sends it is lost; both sessions time out
//   hidefile <path>           an install file (relative to the game folder: maps\z6q1.mq) reads as missing in
//                             this game until the game writes it (then its overlay copy is read): a player
//                             who lacks a file the host has (the shared quest files, net_pair_leave)
//   masterhost <host[:port]>  the options' Master Server Name (0079BED8) for this run, not saved: the
//                             master the connection screen asks and a public game announces itself to
//                             (net_second_master: 127.0.0.1 only)
//   waterwaves 0|1            the options' EnableWaterWaves (0079BD38) for this run, not saved (the
//                             harness's settings have it off); before newgame, which reads it
//   fakemaster <port>         in a pair's game: a master server on the pair's virtual port <port> answers
//                             this game's announcements (harness_pair.h; with "masterhost 127.0.0.1:<port>")
//   peer <name>               ei_launch's: start a second game beside this one on tests/scenarios/peer/<name>.txt,
//                             the two in lockstep on a network of their own (harness_pair.h; net_pair_*.txt)
//   peersignal <tag>, peerwait <seconds> <tag>
//                             in a pair's game: mark <tag> as reached; wait until the other game has marked
//                             <tag> (fails after <seconds>)
//   zone <name> [exit]        leave the zone as its exits do: the server's LeaveToZone (0066F310) for
//                             the local player, to map.txt zone <name> (gz2g, bz1g, ...), the party
//                             arriving at that zone's exit <exit> (from 0: map.txt's "#exit n" minus
//                             1; default 0). Needs the hero alive and in the world; fails otherwise
//   waitzone <seconds> <name> wait until zone <name> is loaded and the local player is in its world
//                             again (game and brief zones; the party's units come a little later)
//   check <expression>        print the script expression through the console ("#<expression>")
//                             and require a nonzero result (waits up to 3 s for the output)
//   timeout <seconds>         the whole scenario's time limit (default 60 s); past it the run
//                             fails and the game is closed (the watchdog thread; if that cannot run,
//                             ei_launch reads this line too and ends the game 20 s later)
//   hang main|dump|all        TEST ONLY, for the time limit's own checks (tests/scenarios/watchdog_*.txt,
//                             "# expect: TIMEOUT"): block the main thread for good; "dump" also blocks
//                             the thread that logs the main thread's stack (a main thread that cannot
//                             be suspended does that), "all" stops the watchdog thread as well, so only
//                             ei_launch can end the game
//   waitline <seconds> <text> wait until the console has printed <text> (at any time in the run)
//   waitexpect <seconds> <text>  like expect, but waits up to <seconds> for <text> to appear (for
//                             output that follows posted keys, which a busy machine handles later)
//   speed <ms>                run one world update per <ms> of real time (game default 55, its own
//                             speed-up key 27; smaller is faster); 0 leaves it to the game
//   waitfor <seconds> <expression>
//                             evaluate the expression every 250 ms until it is nonzero; fail if
//                             that takes longer than <seconds>
//   expect <text>             require <text> in the console output since the previous expect
//   reject <text>             fail if <text> appears in the console output from now on
//   report                    write the verify totals to harness_report.txt
//   diskfull <text>           TEST ONLY (harness_disk_full.txt): writes to files whose path contains
//                             <text> fail with ERROR_DISK_FULL from now on (harness_crash.h)
//   expectcrash <text>        the run passes when the game crashes with a failure line containing
//                             <text>, and fails at "quit" when it has not crashed
//   quit                      write the report and exit the game through the console
//   realtime, deterministic   the game on real time, or (the default) on the virtual game clock
//                             with fixed seeds, where runs repeat exactly (see below)
//   clocktrace, randtrace     log every clock read and wait (ei_hook.log), every random draw
//                             (randtrace.txt; harness_clock.h)
//   vanilla <step>, rebirth <step>   run the step only in that install
//   rebirthflags <spec>       turn Rebirth patch flags on for this run (include/ei/rebirth.h): "all",
//                             or patch names, each optionally =0/=1. Read before the hooks are
//                             installed; in a Rebirth launch the flags follow addon.dll anyway.
//                             "SecondMaster=<host[:port]>" sets NewMaster's second master server.
//   resolution <W>x<H>|desktop, widescreen 0|1
//                             answer the game's VideoResolution and the starter's WideScreen setting
//                             for this run (game.exe's and addon.dll's registry reads; read before
//                             the game starts; harness_starter.h)
//   capture <name>            write the frame presented after the next two to frames\<name>.bmp in
//                             the output folder (the render target before the software cursor and
//                             the flip; harness_capture.cpp); fails after 10 s without a frame
//   freeze, unfreeze          hold the game's clocks (timeGetTime, GetTickCount) and the camera for
//                             captures: no world, client or camera updates, no clock-driven
//                             animation, while the frames keep being drawn. Do not "check"/"waitfor"
//                             while frozen (script output needs world updates); a scenario that
//                             freezes also sees a fixed date (as in deterministic mode)
//   focus off|on              the focus lost (WM_ACTIVATE inactive, the window minimised) or given
//                             back (restored, WM_ACTIVATE click-active), as Windows does it
//   windowsize <w> <h>        resize the game window (its client area) to <w> x <h>
//   fullscreen 0|1            answer the game's FullScreen setting for this run (harness_starter.h)
//   fpscursor 0|1             answer the game's FPSIndependentCursor setting for this run: 1 runs the
//                             cursor's drawing thread 005E5690, 0 does not (harness_starter.h)
//   music <volume>            answer the game's AudioStreaming setting (the streamed music's volume) for
//                             this run; 0: no streamed music, so the music system's random draws do not
//                             follow the real-time stream thread (harness_starter.h)
//   waitpresent <seconds> [gamma]
//                             wait until the switch-over has presented a frame on the game window
//                             (its own swap chain); with "gamma" also until the verify build has
//                             checked a presented frame against a gamma ramp that is not the
//                             identity (one to one only; fails on a difference). Fails after
//                             <seconds> or when the swap chain failed
//   losedevice [seconds]      the new renderer acts as if its Direct3D 11 device were lost at the
//                             next present (render::LoseDevice) and makes the device, the swap
//                             chain and every image again; waits (default 10 s) until it has, and
//                             presenting on the game window, until a frame is presented on the new
//                             device. Fails on a timeout or when the new renderer gave up
//   removedevice [seconds]    as losedevice, but the device is removed for real (render::RemoveDevice:
//                             ID3D12Device5::RemoveDevice on the Direct3D 12 device under a device made
//                             through D3D11On12; the scenario's "render" step names "removable").
//                             Also fails when the loss seen was not that removal (GetDeviceRemovedReason
//                             DXGI_ERROR_DEVICE_REMOVED) or the new device is not removable again
//   mouseglide <ms> <x0> <y0> <x1> <y1>
//                             move the harness's mouse (what the game's GetCursorPos reads) from
//                             (x0, y0) to (x1, y1) in the 800x600 layout over <ms> real time, in 10 ms
//                             steps, from a thread of its own: it goes on while the main thread is
//                             busy (a stall, a movie, a loading screen). No messages are posted
//   stall <ms>                the main thread does nothing for <ms> real time: one long frame
//   cursormark                remember how often the cursor thread has presented (render::CursorPresents)
//   cursorcheck <name>|- <min> [max]
//                             require the cursor thread to have presented the frame again with the
//                             cursor moved (the switch-over without DirectDraw, R9c2) at least <min>
//                             and at most <max> times since "cursormark"; with a name, its last
//                             present's frame is written to frames\<name>.bmp, the verify build's
//                             checks of the composed frames must have found no difference, and its
//                             rectangle must sit at the mouse less the cursor's hot spot (within the
//                             distance the mouse covers in two steps of the cursor thread)
//   render <spec>             the new renderer's options for a run of the image (src/render/render.h;
//                             the format of EI_RENDER: "removable", "checkall"). Read before the game starts.
//                             Ignored in a run of the original code (game.exe), which never runs it
//   samecapture <a> <b> [tolerance] [fraction]
//                             require frames <a> and <b> to match: at most <fraction> (default
//                             0.001) of the pixels may have a channel differing by more than
//                             <tolerance> (default 8), as tools/compare_frames.py counts
//   terrainlevels on|point|off
//                             the image's ground pages draw the texture level their sampler reads
//                             (render::TerrainLevels: a grey per level, marked green / blue), through
//                             the shipped sampler (on) or the old point-mip one (point), or their
//                             colours (off). Ignored in a run of the original code
//   levelsteps <frame> <least> <most>
//                             require the fraction of neighbouring level-map pixel pairs of
//                             frames\<frame>.bmp whose level jumps (capture::LevelSteps: the seams of
//                             R12) to lie within <least>..<most>. Skipped in a run of the original code
//   mmplevels <most>          require the levels of every .mmp texture the image holds, decoded as drawn,
//                             to lie within a mean difference of <most> (0..255 per channel) from their
//                             level 0, both box-filtered to 4 x 4 (render::CheckMmpLevels: levels read
//                             from the wrong bytes drew bright green strips over the trees, R12b).
//                             Skipped in a run of the original code
//   smooth on|off             the image's smooth option (render::SetSmooth, R12a: the world view
//                             multisampled, its models sampled anisotropically), from the next frame;
//                             "render new,present,smooth" turns it on from the start. Ignored in a
//                             run of the original code
//   smoothcheck <plain> <smooth> <least> <most>
//                             require the smooth option on, the last frame's world drawn multisampled
//                             (as many samples as the device gives, at least 2) and resolved, models
//                             in it sampled anisotropically, and frames\<smooth>.bmp to differ from
//                             frames\<plain>.bmp (the same frozen moment without the option) in a
//                             fraction of its pixels (by more than 8) within <least>..<most>: the
//                             world's edges and textures, nothing black or stale. Skipped in a run of
//                             the original code
//   shadows classic|map|rebuild
//                             the image's object shadows (render::SetClassicShadows, R16j): the game's
//                             decals, or the sun's shadow map (the default), from the next frame;
//                             "render new,present,classicshadows" starts with the classic ones. rebuild
//                             (R16e1, render::RebuildShadowTextures): every object shadow's classic
//                             texture made again at its next draw (while the textures are made). Ignored
//                             in a run of the original code
//   shadowbuild skip|full     with the shadow map, the image's object shadows leave their classic texture
//                             and decal's draw out (render::ClassicShadowSkipped, R16e1; the default) or
//                             make them as before R16e1 (full), from the next shadow draw. Ignored in a
//                             run of the original code
//   shadowcheck <casters>     require the last frame's shadow map made with at least <casters> objects
//                             casting into it, the ground and some meshes sampling it, and every object
//                             whose shadow the game draws and that was drawn casting into it (render::
//                             ShadowState). Skipped in a run of the original code
//   terraincache on|off|check the image's terrain pages kept on the GPU (render::SetTerrainCache, R16c;
//                             the default) or built and uploaded every frame (off), from the next
//                             frame; check requires that no check of a kept page against a fresh build
//                             (every 16th frame, render::TerrainCacheState) found it stale. Ignored in
//                             a run of the original code
//   meshcache on|off|check    the image's figure meshes kept on the GPU (render::SetMeshCache, R16d; the
//                             default) or built and uploaded every frame (off), from the next mesh
//                             draw; check requires that no check of a kept mesh against a fresh build
//                             (every 16th frame, render::MeshCacheState) found it stale. Ignored in a
//                             run of the original code
//   meshcorners read|full     the image's device mesh draws (004E9010 / 004E9A50) do only the per-corner
//                             work game code reads (render::MeshCornersSkipped, R16k; the default) or all
//                             the old path's (full: TL vertices, fog, clipped vertices to the screen, as
//                             before R16k), from the next mesh draw. Ignored in a run of the original code
//   memorytextures inplace|remake
//                             the image's textures in the renderer's memory (shadow textures, the global
//                             map's, the CPU-written ones) updated in place with their levels made on
//                             the GPU (render::SetMemoryTexturesInPlace, R16b; the default) or made
//                             again as new images with their levels made on the CPU (remake: as before
//                             R16b); every one is read again at its next use. Ignored in a run of the
//                             original code
//   renderstats <label>|-     log (and add to the report) what the image's renderer did per frame since
//                             the last renderstats (render::TakeFrameStats, R16: hand-over, GPU uploads,
//                             draws and state changes, images made, CPU time, morphed meshes; R16e: the
//                             ground decals' projections, flat draws and object shadow builds) and the
//                             main thread's CPU and wall time per frame; "-" only starts a new
//                             count. Skipped in a run of the original code
//   checkshot [recorder] <n> <frame> [tolerance] [fraction]
//                             require shot<n>.bmp (the game's Scroll Lock screenshot, or with
//                             "recorder" the frame recorder's capture\shot<n>.bmp; in the saves
//                             overlay) to exist and match frames\<frame>.bmp as
//                             samecapture does (16 bits lose the low bits: use tolerance 8 or more)
//   checksave <slot> <frame> [centre] [tolerance] [fraction]
//                             require the save <slot>'s picture (shot.sav, 256x192 5:6:5) to match
//                             frames\<frame>.bmp, both as averages of 8x8 blocks of the picture (the
//                             save's two nearest-pixel stretches sample differently), counted as
//                             samecapture does; catches a picture of nothing (a cleared frame, R13).
//                             "centre": against the frame's centred 4:3 part (the 800x600 layout's
//                             rectangle under Rebirth's WideScreen, which the screen copy takes)
//   record on|off             the game's frame recorder (0079BC54: a "shotNNNN.bmp" of each presented
//                             frame, nothing in the shipped game sets it)
//   digest <N>|off, digest    a record of the game state (world, generators, players, every world
//                             object: harness_digest.h) every <N> world updates from now into
//                             digest.txt, none (off), or one now (no argument); EI_DIGEST=<N> takes
//                             them for the whole run. tools/golden.py compares two runs' records and
//                             names the first field that differs
//   digest water              (a line for the whole run) the records and dumps hold the water layer too:
//                             hashes of each cached sector's water vertices as the waves move them (R16c3)
//   dump <name>               the whole game state now into dump_<name>.txt (harness_digest.h)
//   uicensus                  count how the interface draws into its window surfaces (GDI calls per
//                             surface, call sites, readers, drawing no hook explains) and write
//                             ui_census.csv with the report (ui_census.cpp; also EI_UI_CENSUS=1;
//                             wants "render new,cpuwindows")
//   # comment
//
// Everything the console prints is captured into the report. The report ends with "RESULT: PASS"
// (no failed checks or rejected output) or "RESULT: FAIL"; ei_launch --wait turns
// that into its exit code.
//
// Harness runs are hands-off and leave the install alone: no startup movies, the tutorial option
// off, saves and config\tutorial.ini kept in harness_saves next to the DLL, every other write under
// the game's root (the composed-skin cache textures\*.mmp, config\keyboard.ini, addon.dll's log, ...)
// going to a copy-on-write overlay there too (any other write into an install is refused and fails the
// run: harness_install.h), no registry writes, and a UDP port of its own for the multiplayer
// sessions instead of 8888, every socket on 127.0.0.1 only (harness_net.h).
// A watchdog fails and ends a run that stops making progress (e.g. a message box), and ei_launch
// ends a game that hangs after "quit".
#include "ei/hook.h"
#include "game/world_glue.h"
#include "harness_addon.h"
#include "harness_capture.h"
#include "harness_address_order.h"
#include "harness_clock.h"
#include "harness_crash.h"
#include "harness_digest.h"
#include "harness_heap.h"
#include "harness_leftovers.h"
#include "harness_save_leftovers.h"
#include "harness_shadow_turn.h"
#include "harness_path_cells.h"
#include "harness_savetrace.h"
#include "harness_script_order.h"
#include "harness_watch.h"
#include "harness_hooks.h"
#include "harness_settings.h"
#include "harness_starter.h"
#include "harness_input.h"
#include "harness_install.h"
#include "harness_net.h"
#include "harness_pair.h"
#include "harness_timing.h"
#include "ui_census.h"
#include "../vectors/vectors.h"

#include "../../src/hook/log.h"
#include "ei/hook.h"
#include "ei/main_loop.h"
#include "ei/rebirth.h"
#include "../../src/lib/mfc.h"
#include "../../src/game/device.h"   // device::WithoutDirectDraw
#include "../../src/game/ui.h"
#include "../../src/game/ui_cursor.h"
#include "../../src/game/item.h"
#include "../../src/game/options.h"
#include "../../src/game/spell.h"
#include "../../src/game/unit.h"
#include "../../src/game/unit_client_models.h"
#include "../../src/game/originals_engine.h"
#include "../../src/game/originals_ui.h"
#include "../../src/game/originals_world.h"
#include "../../src/render/render.h"
#include "game/object_tables.h"
#include "game/player_messages.h"
#include "game/ui.h"
#include "game/unit.h"
#include "game/world.h"
#include "game/game_data.h"

#include <windows.h>
#include <dwmapi.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <intrin.h>

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace ei::verify {

namespace {
constexpr std::uintptr_t kPeekMessageASlot = 0x0073B458;   // game.exe import table

// The console's streambuf (class with vtable 00740034, instance 007C5868): overflow() collects the
// text in a CString at +0x50; sync() (slot 1, 005DC1B0) hands the finished line to the console
// window and clears it. Hooking sync gives every line the console prints.
constexpr std::uintptr_t kConsoleStreambufSyncSlot = 0x00740034 + 4;
using SyncFn = int(__thiscall*)(void*);
using OverflowFn = int(__thiscall*)(void*, int);
SyncFn g_sync;

// 005DC620: the console's command executor; the command text is passed in ECX.
__declspec(naked) void __fastcall ConsoleExecute(const char*) EI_ORIGINAL(0x005DC620)

// 00578B00 (GameSendLeave04): the client asks the server for interface screen `id` (the server answers
// with message 7, 00669D50 -> CInterface3D::CreateScreen 005F5BA0: 1 battle, 3 briefing, 4 global map, 5 camp).
void RequestScreen(int id) { GameSendLeave04(id); }
// 00578560 (GameSetConstructionVariable): sets player 0's "constr_current" (the constructor the camp opens with).
void SetCurrentConstructor(float constructor) { GameSetConstructionVariable(std::bit_cast<std::uint32_t>(constructor)); }
// 004A74F0 (__thiscall on the class template vector 007AA5D8): the template of a class, null if none.
__declspec(naked) void* __fastcall FindClassTemplate(void*, void*, int) EI_ORIGINAL(0x004A74F0)
constexpr std::uintptr_t kClassTemplates = 0x007AA5D8;

// 0066F310 (RET 8): CPlayerServer::LeaveToZone(zone, exit), what a zone exit (00670C20) and the
// LeaveToZone script command (0x8A) do. Only in state 1 (+0x20: in the world); in campaign mode 1 or 3
// a dead or missing leader loses the game instead. The party is stored, the world player's +0xD8 /
// +0xDC become the zone and the exit, the state 3, and message 8 goes to the client (00669EB0: game
// request 4, carried out by the main loop through 0057C460). `exit` indexes the target zone's exits
// in map.txt order from 0 (its "#exit n" minus 1, as the map database stores the exits' targets);
// Redeploy (00590700) puts the party at the centre of that exit's deploy rectangle, or at (0, 0)
// when the zone has no such exit.

constexpr std::uintptr_t kServerWorld = 0x007AFF60;
// The server world's +0x5C, which the flow passes to the client world's SetPlayer (00566CE0). It is
// not the server's player number: the first X9 run found no player with it (logged by the steps).
constexpr std::uintptr_t kWorld5C = 0x007AFFBC;
// The single-player hero's player number in the server world: world players' +0x24 is 0 for the
// local player, and the world script commands that act on "the player" (GSSetVar(0, ...),
// LeaveToZone(0, ...); script_exec.cpp case 0x8A: TheWorld()->GetPlayer(Ftol(f[0])) and its +0x20)
// use 0.
constexpr int kLocalPlayer = 0;
constexpr std::uintptr_t kCampaignMode = 0x007C2CC4;    // 1 battle, 2 main menu, 3 base, 4 edge
constexpr std::uintptr_t kMapDatabase = 0x0079BF00;
constexpr int kZoneRecordKind = 0x28;                   // map.txt type: 0 game, 1 edge, 2 brief

// The local world player (CWorldServer::GetPlayer 00594180, thiscall, RET 4) or null.
void* LocalWorldPlayer() { return ei::GamePointer<CWorldServer>(kServerWorld)->GetPlayer(kLocalPlayer); }
// Its server connection (the world player's +0x20, a CPlayerServer in single player; StateOf in
// script_exec.cpp) or null; the connection's +0x20 is its state (1 in the world, 3 leaving the zone).
void* LocalConnection() {
    void* player = LocalWorldPlayer();
    return player ? *reinterpret_cast<void**>(static_cast<char*>(player) + 0x20) : nullptr;
}
int ConnectionState(void* connection) { return *reinterpret_cast<int*>(static_cast<char*>(connection) + 0x20); }
// What the zone steps read, for the log and failure messages.
std::string DescribeLocalPlayer() {
    void* player = LocalWorldPlayer();
    void* connection = LocalConnection();
    char text[200];
    std::snprintf(text, sizeof(text), "player %d -> %p, connection %p, state %d (world +0x5C = %d)", kLocalPlayer, player,
                  connection, connection ? ConnectionState(connection) : -1, ei::GameData<int>(kWorld5C));
    return text;
}
// The server world's zone (+0x120, the map database name NewZone 00595480 stores), or "".
const char* ServerZoneName() {
    const char* name = ei::GameData<const char* const>(kServerWorld + 0x120);
    return name ? name : "";
}

struct Step {
    std::string verb;
    std::string argument;
};

std::vector<Step> g_steps;
std::size_t g_next = 0;

// When a wait ends: after `ms` on the harness's clock (virtual time in deterministic mode), or at a
// real-time safety limit, so that a game which stops drawing frames (the virtual clock stands
// still) still fails the wait instead of the whole run's time limit: four times the wait plus 10 s
// (a busy machine draws slowly, and the virtual clock is then slower than real time).
struct Deadline {
    DWORD start = 0, at = 0;           // harness clock
    DWORD realStart = 0, realAt = 0;   // GetTickCount
    void Set(DWORD ms) {
        start = clock::Now();
        at = start + ms;
        realStart = GetTickCount();
        realAt = realStart + (clock::Virtual() ? 4 * ms + 10000 : ms);
    }
    bool Passed() const {
        return static_cast<LONG>(clock::Now() - at) >= 0 || static_cast<LONG>(GetTickCount() - realAt) >= 0;
    }
    // For a failure message: "" when the wait's own time passed, else what the clock did meanwhile.
    std::string Why() const {
        if (static_cast<LONG>(clock::Now() - at) >= 0) return "";
        char text[120];
        std::snprintf(text, sizeof(text), " (real-time limit: the game's clock moved %lu ms in %lu ms)",
                      clock::Now() - start, GetTickCount() - realStart);
        return text;
    }
};
Deadline g_waitUntil;   // "wait"
volatile bool g_quitting = false;
volatile bool g_watchdogStopped = false;   // "hang all": the in-game watchdog does nothing
volatile bool g_stackDumpHangs = false;    // "hang dump": the thread that logs the main thread's stack never returns
bool g_running = false;
bool g_inStep = false;
char g_reportPath[MAX_PATH];
// Whether the PeekMessageA call running the steps is one that hands input to the game (see
// PeekMessageHook): "key", mouse and region steps wait for one.
bool g_inputLoop = false;
// Whether that call is WinMain's own loop (the first two kInputLoopReturns): "quit" waits for one.
bool g_winMainLoop = false;

std::vector<std::string> g_console;          // every console line so far
std::size_t g_expectFrom = 0;                // first line "expect" looks at
std::vector<std::string> g_rejected;         // texts that must not appear
std::vector<std::string> g_failures;
// A pending "check": the console line index its output starts at, and when to give up.
bool g_checking = false;
std::string g_checkExpr;
std::size_t g_checkFrom = 0;
Deadline g_checkDeadline;
// A pending "waitfor": the expression, when it was last evaluated (its output starts at
// g_waitForFrom), when to evaluate it next, and when to give up.
// A pending "waitline": the text to find anywhere in the console output so far, and when to give up.
bool g_waitingLine = false;
std::string g_waitLineText;
Deadline g_waitLineDeadline;
std::size_t g_waitLineFrom = 0;   // waitline: 0 (any time); waitexpect: since the last expect
bool g_waitLineIsExpect = false;
// A pending "waitzone": the zone and when to give up.
bool g_waitingZone = false;
std::string g_waitZoneName;
Deadline g_waitZoneDeadline;
bool g_waitingFor = false;
bool g_waitForPending = false;
std::string g_waitForExpr;
std::size_t g_waitForFrom = 0;
DWORD g_waitForNext = 0;
Deadline g_waitForDeadline;
// Frames drawn so far: passes of WinMain's loop through its after-frame PeekMessageA (see
// PeekMessageHook). A pending "frames": the count it waits for.
unsigned g_frames = 0;
bool g_waitingFrames = false;
unsigned g_waitFramesUntil = 0;
Deadline g_waitFramesDeadline;   // real time only: 10 s plus 100 ms a frame
// A pending "waitpresent" (R9c): the game window's presents when it started, its deadline, whether it
// waits for a gamma ramp check too, and the ramp checks a previous one has seen.
bool g_waitingPresent = false;
bool g_waitPresentGamma = false;
std::uint32_t g_waitPresentFrom = 0;
Deadline g_waitPresentDeadline;
int g_rampChecksSeen = 0;
// A pending "losedevice" (R9c1) or "removedevice" (R9c5): the new devices made when it started, its
// deadline, whether the device was removed for real.
bool g_waitingNewDevice = false;
bool g_newDeviceRemoved = false;
int g_newDeviceFrom = 0;
DWORD g_newDeviceDeadline = 0;
// A pending "waitwindow": the window's vtables (either of a window's two), its name, when it
// started and gives up, and the frame from which it has been ready (kNotReady: not ready now).
constexpr unsigned kNotReady = ~0u;
bool g_waitingWindow = false;
std::string g_waitWindowName;
std::uintptr_t g_waitWindowVtables[2] = {};
Deadline g_waitWindowDeadline;
unsigned g_waitWindowReadyFrom = kNotReady;
// A pending "waitunit": the unit's script name, when it started and gives up.
bool g_waitingUnit = false;
std::string g_waitUnitName;
Deadline g_waitUnitDeadline;
// A pending "waitidle" / "waitdead" / "waitaward" (SV5d5): which, its deadline; waitidle's frames wanted and the
// frame from which the leader has stood idle; waitdead's unit ids; waitaward's experience and money at its start.
enum class WorldWait { None, Idle, Dead, Award };
WorldWait g_worldWait = WorldWait::None;
Deadline g_worldWaitDeadline;
unsigned g_idleFrames = 0;
unsigned g_idleFrom = kNotReady;
std::vector<int> g_deadIds;
float g_awardExperience = 0;
unsigned g_awardMoney = 0;
bool g_awardFromSet = false;   // "awardfrom" took them (the next waitaward compares with those)
void AwardFrom() {
    const digest::UnitNow unit = digest::Unit(digest::LocalLeader(&g_awardMoney));
    g_awardExperience = unit.experience;
}
// A pending "peerwait": the other game's signal and when to give up (harness_pair.h).
bool g_waitingPeer = false;
std::string g_waitPeerTag;
Deadline g_waitPeerDeadline;
// A pending "capture": its name and when to give up (real time); the frames written so far.
bool g_capturing = false;
std::string g_captureName;
DWORD g_captureDeadline = 0;
std::vector<std::string> g_capturedFrames;

int __fastcall ConsoleSync(void* self, void*) {
    OverflowFn overflow = (*reinterpret_cast<OverflowFn**>(self))[7];
    overflow(self, -1);   // move pending characters into the line (the original does this too)
    const char* text = *reinterpret_cast<const char**>(static_cast<char*>(self) + 0x50);
    if (text && *text) {
        std::string line(text);
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
        Log("console: %s", line.c_str());
        for (const std::string& r : g_rejected)
            if (line.find(r) != std::string::npos) g_failures.push_back("rejected output: " + line);
        g_console.push_back(std::move(line));
    }
    return g_sync(self);
}
decltype(&::PeekMessageA) g_peekMessage;

// 007AFE20: the game's pending state change, carried out by the main loop (0057C700) and then
// reset to 0. +0 kind (2 = load the save in slot <argument>, 3 = new game, 4 = go to zone <argument>, 6 = start
// a multiplayer server on map <argument>), +4 the argument.
struct GameRequest {
    int kind;
    mfc::CString argument;
};
GameRequest& g_request = ei::GameData<GameRequest>(0x007AFE20);

// Crash diagnostics: logs access violations and other fatal exceptions with the faulting address
// and the code addresses found on the stack (game.exe addresses as is, ours as ei_hook+offset).
// First-chance, so exceptions the game handles itself show up too; only the first few are logged.
std::uintptr_t g_selfBase, g_selfEnd;

void LogCodeAddress(const char* label, std::uintptr_t a) {
    if (a >= 0x00401000 && a < 0x00730000) Log("  %s %08X (game.exe)", label, unsigned(a));
    else if (a >= g_selfBase && a < g_selfEnd) Log("  %s ei_hook+%X", label, unsigned(a - g_selfBase));
    else {   // Direct3D and DXGI frames too (R9c5: crashes inside d3d11on12.dll and the driver)
        HMODULE m = nullptr;
        char name[MAX_PATH] = "";
        if (a > 0x10000 && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                              reinterpret_cast<LPCSTR>(a), &m) &&
            GetModuleFileNameA(m, name, MAX_PATH) && (std::strstr(name, "d3d") || std::strstr(name, "dxgi")))
            Log("  %s %s+%X", label, std::strrchr(name, '\\') + 1, unsigned(a - reinterpret_cast<std::uintptr_t>(m)));
    }
}

void LogModuleOf(std::uintptr_t a) {
    HMODULE m = nullptr;
    char name[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(a), &m))
        GetModuleFileNameA(m, name, MAX_PATH);
    Log("  in %s+%X (thread %lu)", name, unsigned(a - reinterpret_cast<std::uintptr_t>(m)), GetCurrentThreadId());
}

LONG CALLBACK CrashLogger(EXCEPTION_POINTERS* e) {
    static int logged = 0;
    DWORD code = e->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION &&
        code != EXCEPTION_STACK_OVERFLOW && code != EXCEPTION_INT_DIVIDE_BY_ZERO &&
        code != EXCEPTION_PRIV_INSTRUCTION)
        return EXCEPTION_CONTINUE_SEARCH;
    if (logged++ >= 5) return EXCEPTION_CONTINUE_SEARCH;
    const CONTEXT* c = e->ContextRecord;
    Log("harness: exception %08lX at %08lX (address %08lX); eax=%08lX ebx=%08lX ecx=%08lX edx=%08lX "
        "esi=%08lX edi=%08lX ebp=%08lX esp=%08lX",
        code, c->Eip,
        e->ExceptionRecord->NumberParameters > 1 ? static_cast<unsigned long>(e->ExceptionRecord->ExceptionInformation[1]) : 0ul,
        c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp);
    LogModuleOf(c->Eip);
    LogCodeAddress("at", c->Eip);
    const auto* stack = reinterpret_cast<const std::uintptr_t*>(c->Esp);
    MEMORY_BASIC_INFORMATION mbi{};
    VirtualQuery(stack, &mbi, sizeof(mbi));
    const auto* stackEnd = reinterpret_cast<const std::uintptr_t*>(static_cast<char*>(mbi.BaseAddress) + mbi.RegionSize);
    for (int i = 0; i < 256 && stack + i < stackEnd; ++i) LogCodeAddress("stack", stack[i]);
    return EXCEPTION_CONTINUE_SEARCH;
}

// WinMain (00456060) looks for another game window with FindWindowA and, when it finds one,
// brings it to the front and exits. Harness runs go in parallel (run_all.py --jobs), each game in
// its own output folder, so in harness runs no other window counts.
constexpr std::uintptr_t kFindWindowASlot = 0x0073B448;
decltype(&::FindWindowA) g_findWindow;
HWND WINAPI NoOtherGameWindow(LPCSTR, LPCSTR) { return nullptr; }

// Harness runs stay out of the user's way: the game window never takes focus or comes to the front,
// it opens at a small size (VideoResolution), and the sound is muted. The game's own code still
// runs (sound included), so scenarios test the same paths.
constexpr std::uintptr_t kSetForegroundWindowSlot = 0x0073B438;
constexpr std::uintptr_t kBringWindowToTopSlot = 0x0073B430;
constexpr std::uintptr_t kSetFocusSlot = 0x0073B584;
constexpr std::uintptr_t kSetActiveWindowSlot = 0x0073B640;
constexpr std::uintptr_t kShowWindowSlot = 0x0073B554;
constexpr std::uintptr_t kCreateWindowExASlot = 0x0073B558;
constexpr std::uintptr_t kSetCursorPosSlot = 0x0073B4A0;
constexpr std::uintptr_t kGetCursorPosSlot = 0x0073B49C;
constexpr std::uintptr_t kRegQueryValueExASlot = 0x0073B028;
constexpr std::uintptr_t kAilWaveOutOpenSlot = 0x0073B7A4;
constexpr const char* kHarnessResolution = "1280x720";   // 16:9 like the usual desktop (Rebirth widescreen)

decltype(&::SetForegroundWindow) g_setForegroundWindow;
decltype(&::BringWindowToTop) g_bringWindowToTop;
decltype(&::SetFocus) g_setFocus;
decltype(&::SetActiveWindow) g_setActiveWindow;
decltype(&::ShowWindow) g_showWindow;
decltype(&::CreateWindowExA) g_createWindowEx;
decltype(&::SetCursorPos) g_setCursorPos;
decltype(&::RegQueryValueExA) g_regQueryValueEx;
using AilWaveOutOpenFn = int(__stdcall*)(void**, void*, int, void*);
AilWaveOutOpenFn g_ailWaveOutOpen;

BOOL WINAPI NoForeground(HWND) { return TRUE; }
BOOL WINAPI NoBringToTop(HWND) { return TRUE; }
HWND WINAPI NoFocus(HWND window) { return window; }
HWND WINAPI NoActivate(HWND window) { return window; }
BOOL WINAPI NoCursorMove(int, int) { return TRUE; }

// The game reads the real cursor with GetCursorPos (edge scrolling among others). The user's cursor
// can be anywhere, e.g. on a second screen to the right, which the game took as panning right and
// which moved the camera under the scenario's clicks. The game sees the harness's own mouse instead:
// the last position a move/click step posted (client coordinates), the window's centre until then.
decltype(&::GetCursorPos) g_getCursorPos;
volatile LONG g_mouseX = -1, g_mouseY = -1;
HWND GameWindow();
BOOL WINAPI HarnessCursorPos(LPPOINT point) {
    if (!point) return FALSE;
    HWND window = GameWindow();
    RECT client{};
    if (!window || !GetClientRect(window, &client)) return g_getCursorPos(point);
    if (g_mouseX < 0) {
        point->x = (client.right - client.left) / 2;
        point->y = (client.bottom - client.top) / 2;
    } else {
        point->x = g_mouseX;
        point->y = g_mouseY;
    }
    ClientToScreen(window, point);
    return TRUE;
}
BOOL WINAPI ShowWithoutActivating(HWND window, int command) {
    switch (command) {
    case SW_SHOWNORMAL: case SW_SHOW: case SW_RESTORE: case SW_SHOWDEFAULT: command = SW_SHOWNOACTIVATE; break;
    case SW_SHOWMAXIMIZED: command = SW_SHOWNA; break;
    case SW_MINIMIZE: case SW_SHOWMINIMIZED: return TRUE;   // the game minimises itself on deactivation
    default: break;
    }
    return g_showWindow(window, command);
}
HWND WINAPI CreateWithoutActivating(DWORD exStyle, LPCSTR cls, LPCSTR name, DWORD style, int x, int y, int w, int h,
                                    HWND parent, HMENU menu, HINSTANCE instance, LPVOID param) {
    if (!parent) exStyle |= WS_EX_NOACTIVATE;
    input::Install();   // the harness's mouse the game's only one, before the game's DirectInput mouse (harness_input.h)
    return g_createWindowEx(exStyle, cls, name, style, x, y, w, h, parent, menu, instance, param);
}
LSTATUS WINAPI SmallVideoResolution(HKEY key, LPCSTR name, LPDWORD reserved, LPDWORD type, LPBYTE data, LPDWORD size) {
    if (name && _stricmp(name, "VideoResolution") == 0 && size) {
        DWORD need = static_cast<DWORD>(std::strlen(kHarnessResolution) + 1);
        if (type) *type = REG_SZ;
        if (!data) { *size = need; return ERROR_SUCCESS; }
        if (*size < need) { *size = need; return ERROR_MORE_DATA; }
        std::memcpy(data, kHarnessResolution, need);
        *size = need;
        return ERROR_SUCCESS;
    }
    // FullScreen = 0: the game runs in its own window mode (BltFast to the primary instead of Flip),
    // so no display mode change. Without this a 1280x720 full screen switched the user's display.
    if (name && _stricmp(name, "FullScreen") == 0 && size) {
        if (type) *type = REG_DWORD;
        if (!data) { *size = 4; return ERROR_SUCCESS; }
        if (*size < 4) { *size = 4; return ERROR_MORE_DATA; }
        std::memset(data, 0, 4);
        *size = 4;
        return ERROR_SUCCESS;
    }
    return g_regQueryValueEx(key, name, reserved, type, data, size);
}
int __stdcall MutedWaveOutOpen(void** driver, void* waveOut, int deviceId, void* format) {
    int result = g_ailWaveOutOpen(driver, waveOut, deviceId, format);
    if (HMODULE mss = GetModuleHandleA("mss32.dll"); mss && driver && *driver) {
        using IntVolume = void(__stdcall*)(void*, int);
        using FloatVolume = void(__stdcall*)(void*, float);
        if (auto v = reinterpret_cast<IntVolume>(GetProcAddress(mss, "_AIL_set_digital_master_volume@8"))) v(*driver, 0);
        if (auto v = reinterpret_cast<FloatVolume>(GetProcAddress(mss, "_AIL_set_digital_master_volume_level@8")))
            v(*driver, 0.0f);
        Log("harness: sound muted");
    }
    return result;
}

// A slot of game.exe (an import slot or a vtable's), through the data seam: in the image its import slots are
// ei_hook.dll's table (S18f).
template <class T>
void PatchSlot(std::uintptr_t address, T replacement, T& original) {
    T* slot = ei::GamePointer<T>(address);
    // A slot outside the import table (a vtable's) is game data the harness changes (harness_addon.h checks them).
    const auto at = reinterpret_cast<std::uintptr_t>(slot);
    const bool importSlot = at - ei::GameAddress(ei::kImportsBegin) < ei::kImportsEnd - ei::kImportsBegin;
    if (!importSlot) ei::NoteHarnessPatch(at, sizeof(T), "a slot the harness takes");
    DWORD old;
    VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old);
    original = *slot;
    *slot = replacement;
    VirtualProtect(slot, sizeof(*slot), old, &old);
    // A vtable slot's original is game.exe's code address: in the image it goes with the code cells (S18i).
    if (!importSlot) ei::FixUpCodePointer(reinterpret_cast<std::uintptr_t*>(&original));
}

// Saves. The game keeps them under <game root>saves\ and rewrites saves\current\ whenever it
// starts or loads a game, so a harness run would overwrite the player's own progress. In harness
// runs every path whose directory is "saves" goes to harness_saves\ next to the DLL instead.
constexpr std::uintptr_t kCreateDirectoryASlot = 0x0073B1B8;   // game.exe import table
constexpr std::uintptr_t kCopyFileASlot = 0x0073B1D8;
constexpr std::uintptr_t kRemoveDirectoryASlot = 0x0073B1F0;
constexpr std::uintptr_t kDeleteFileASlot = 0x0073B20C;
constexpr std::uintptr_t kFindFirstFileASlot = 0x0073B210;
constexpr std::uintptr_t kCreateFileASlot = 0x0073B218;
constexpr std::uintptr_t kGetFileAttributesASlot = 0x0073B2F8;
constexpr std::uintptr_t kSetFileAttributesASlot = 0x0073B304;
constexpr std::uintptr_t kMoveFileASlot = 0x0073B40C;
constexpr std::uintptr_t kWriteFileSlot = 0x0073B270;   // failed writes are reported (harness_crash.h)

char g_savesDir[MAX_PATH];
decltype(&::CreateFileA) g_createFile;
decltype(&::DeleteFileA) g_deleteFile;
decltype(&::CopyFileA) g_copyFile;
decltype(&::MoveFileA) g_moveFile;
decltype(&::FindFirstFileA) g_findFirstFile;
decltype(&::CreateDirectoryA) g_createDirectory;
decltype(&::RemoveDirectoryA) g_removeDirectory;
decltype(&::GetFileAttributesA) g_getFileAttributes;
decltype(&::SetFileAttributesA) g_setFileAttributes;

// Returns `path` with everything up to its "\saves" directory replaced by g_savesDir, or `path`
// itself when it has no such directory. `buffer` holds the rewritten path.
const char* SavesPath(const char* path, char (&buffer)[MAX_PATH]) {
    if (!path) return path;
    // config\tutorial.ini (which tutorial windows were shown) is rewritten on every exit; harness
    // runs keep theirs in harness_saves too.
    std::size_t length = std::strlen(path);
    if (length >= 20 && _stricmp(path + length - 20, "\\config\\tutorial.ini") == 0) {
        if (std::snprintf(buffer, MAX_PATH, "%s\\tutorial.ini", g_savesDir) >= MAX_PATH) return path;
        return buffer;
    }
    for (const char* p = path; (p = std::strchr(p, '\\')) != nullptr; ++p) {
        if (_strnicmp(p, "\\saves", 6) == 0 && (p[6] == '\\' || p[6] == '/' || p[6] == '\0')) {
            if (std::snprintf(buffer, MAX_PATH, "%s%s", g_savesDir, p + 6) >= MAX_PATH) return path;
            return buffer;
        }
    }
    return path;
}

// The rest of the install (textures\ with the composed-skin cache, config\keyboard.ini,
// config\addrbook.ini, mp\, ...) gets a copy-on-write overlay in harness_saves\: a write to a file
// under the game's root goes to harness_saves\<relative path> (a file the write keeps the contents
// of is copied there first), and a read takes that copy when there is one, else the install's file.
// Nothing under the install is ever modified. Deleting or moving an install file only affects the
// overlay copy (a move of an install file becomes a copy).
char g_rootDirs[2][MAX_PATH];   // the working directory at start and the game folder (007AF460)
char g_addonInstall[MAX_PATH];  // EI_ADDON_FLAGS: the other install named in EI_INSTALL_ROOTS (the Rebirth install)

// The install of EI_INSTALL_ROOTS (ei_launch: the game folder, then both installs) that is not the game's.
void FindAddonInstall() {
    char roots[3 * MAX_PATH];
    const DWORD n = GetEnvironmentVariableA("EI_INSTALL_ROOTS", roots, sizeof(roots));
    if (n == 0 || n >= sizeof(roots)) return;
    char* next = nullptr;
    for (char* root = strtok_s(roots, ";", &next); root; root = strtok_s(nullptr, ";", &next)) {
        char full[MAX_PATH];
        if (!GetFullPathNameA(root, MAX_PATH, full, nullptr)) continue;
        std::size_t length = std::strlen(full);
        while (length > 3 && full[length - 1] == '\\') full[--length] = '\0';
        std::size_t own = std::strlen(g_rootDirs[0]);
        while (own > 3 && g_rootDirs[0][own - 1] == '\\') --own;
        if (length == own && _strnicmp(full, g_rootDirs[0], own) == 0) continue;
        strcpy_s(g_addonInstall, full);
        return;
    }
}

bool UnderRoot(const char* full, const char* root, const char** relative) {
    std::size_t n = std::strlen(root);
    if (n == 0 || _strnicmp(full, root, n) != 0) return false;
    if (root[n - 1] != '\\') {
        if (full[n] != '\\') return false;
        ++n;
    }
    if (full[n] == '\0') return false;
    *relative = full + n;
    return true;
}

// The overlay path of `path`, or null when `path` is not under the game's root.
const char* ShadowOf(const char* path, char (&buffer)[MAX_PATH]) {
    if (!path || !*path) return nullptr;
    const char* folder = ei::GameData<const char* const>(0x007AF460);   // CString data, may be empty
    if (folder && *folder && !g_rootDirs[1][0]) {
        if (!GetFullPathNameA(folder, MAX_PATH, g_rootDirs[1], nullptr)) g_rootDirs[1][0] = '\0';
    }
    char full[MAX_PATH];
    DWORD n = GetFullPathNameA(path, MAX_PATH, full, nullptr);
    if (n == 0 || n >= MAX_PATH) return nullptr;
    std::size_t saves = std::strlen(g_savesDir);
    if (_strnicmp(full, g_savesDir, saves) == 0) return nullptr;   // already ours
    for (const char* root : g_rootDirs) {
        const char* relative = nullptr;
        if (!UnderRoot(full, root, &relative)) continue;
        if (std::snprintf(buffer, MAX_PATH, "%s\\%s", g_savesDir, relative) >= MAX_PATH) return nullptr;
        return buffer;
    }
    // addon.dll loaded into the vanilla install's game (harness_addon.h) writes its log and dumps into the Rebirth
    // install: those go to a folder of their own in the overlay.
    const char* relative = nullptr;
    if (g_addonInstall[0] && UnderRoot(full, g_addonInstall, &relative)) {
        if (std::snprintf(buffer, MAX_PATH, "%s\\rebirth_install\\%s", g_savesDir, relative) >= MAX_PATH) return nullptr;
        return buffer;
    }
    return nullptr;
}

// Whether `path` is the game's root folder itself (when the game empties mp\ it deletes every entry
// FindFirstFile lists, "mp\.." too).
bool IsGameRoot(const char* path) {
    char full[MAX_PATH];
    DWORD n = GetFullPathNameA(path, MAX_PATH, full, nullptr);
    if (n == 0 || n >= MAX_PATH) return false;
    while (n > 3 && full[n - 1] == '\\') full[--n] = '\0';
    for (const char* root : g_rootDirs) {
        std::size_t length = std::strlen(root);
        while (length > 3 && root[length - 1] == '\\') --length;
        if (length && length == n && _strnicmp(full, root, length) == 0) return true;
    }
    return false;
}

void MakeParentDirs(const char* file) {
    char dir[MAX_PATH];
    strcpy_s(dir, file);
    for (char* p = dir + std::strlen(g_savesDir) + 1; *p; ++p) {
        if (*p != '\\') continue;
        *p = '\0';
        g_createDirectory(dir, nullptr);
        *p = '\\';
    }
}

void LogInstallWrite(const char* path, const char* shadow) {
    static int logged = 0;
    if (logged++ < 200) Log("harness: install write redirected: %s -> %s", path, shadow);
}

// "hidefile <path>": install files (relative to the game folder, e.g. maps\z6q1.mq) this game reads
// as missing until it writes them itself (the overlay copy is then read): a player who lacks a file
// another has (SV5d2: the multiplayer quest files the server shares). Set by steps on the main
// thread before the game looks for the files; read by the file hooks.
constexpr int kHiddenFiles = 8;
char g_hiddenFiles[kHiddenFiles][MAX_PATH];
volatile LONG g_hiddenCount = 0;

bool Hidden(const char* relative) {
    for (LONG i = 0; i < g_hiddenCount; ++i)
        if (_stricmp(relative, g_hiddenFiles[i]) == 0) return true;
    return false;
}

// Install folders a run sees only in its overlay, never the install's own files: mp\ (the multiplayer
// heroes, mp\<n>.mp; SV5d3: the user's own heroes had been listed and loaded on the hero screen, so goldens
// followed the user's play). A scenario's hero comes from tools/make_mp_hero.py or the game's hero screen.
// `relative` is the path below the game folder.
bool OverlayOnly(const char* relative) {
    return _strnicmp(relative, "mp", 2) == 0 && (relative[2] == '\\' || relative[2] == '/' || relative[2] == '\0');
}

// For a read: the overlay copy when it exists, else `path` (saves always redirect); a hidden file
// without an overlay copy reads as the missing overlay path.
const char* ReadPath(const char* path, char (&buffer)[MAX_PATH]) {
    const char* saves = SavesPath(path, buffer);
    if (saves != path) return saves;
    const char* shadow = ShadowOf(path, buffer);
    if (shadow && OverlayOnly(shadow + std::strlen(g_savesDir) + 1)) return shadow;
    if (shadow && g_getFileAttributes(shadow) != INVALID_FILE_ATTRIBUTES) return shadow;
    if (shadow && g_hiddenCount && Hidden(shadow + std::strlen(g_savesDir) + 1)) {
        static int logged = 0;
        if (logged++ < 20) Log("harness: hidden file read as missing: %s", path);
        return shadow;
    }
    // The user's key bindings and the like: the committed copy (harness_settings.h).
    char pinned[MAX_PATH];
    if (shadow && settings::PinnedFile(shadow + std::strlen(g_savesDir) + 1, pinned)) {
        strcpy_s(buffer, pinned);
        return buffer;
    }
    return path;
}

// Whether a write that keeps its file's contents gets the install's copy first: not a log (a run's log
// starts empty in the overlay; addon.dll appends to starter\addon.log, which grows with the user's play) and
// not a file over 16 MB.
bool CopiedForWrite(const char* original) {
    const std::size_t length = std::strlen(original);
    if (length >= 4 && _stricmp(original + length - 4, ".log") == 0) return false;
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExA(original, GetFileExInfoStandard, &data)) return true;
    if (data.nFileSizeHigh == 0 && data.nFileSizeLow <= 16u << 20) return true;
    Log("harness: %s not copied into the overlay for a write (over 16 MB); the write starts from an empty file", original);
    return false;
}

// For a write: the overlay path (its directories created; with `keep` an install file not yet in
// the overlay is copied first).
const char* WritePath(const char* path, char (&buffer)[MAX_PATH], bool keep) {
    const char* saves = SavesPath(path, buffer);
    if (saves != path) return saves;
    const char* shadow = ShadowOf(path, buffer);
    if (!shadow && path && IsGameRoot(path)) {
        // The root's own write (a delete of "mp\.."): the overlay's root, a folder like the install's.
        LogInstallWrite(path, g_savesDir);
        return g_savesDir;
    }
    if (!shadow) return path;
    MakeParentDirs(shadow);
    char pinned[MAX_PATH];
    const char* original = settings::PinnedFile(shadow + std::strlen(g_savesDir) + 1, pinned) ? pinned : path;
    if (keep && !OverlayOnly(shadow + std::strlen(g_savesDir) + 1) && g_getFileAttributes(shadow) == INVALID_FILE_ATTRIBUTES &&
        g_getFileAttributes(original) != INVALID_FILE_ATTRIBUTES && CopiedForWrite(original))
        g_copyFile(original, shadow, TRUE);
    LogInstallWrite(path, shadow);
    return shadow;
}

// Whether `path` is an install file (harness_install.h: other modules' writes go to the overlay too).
bool InInstall(const char* path) {
    char b[MAX_PATH];
    return ShadowOf(path, b) != nullptr || (path && IsGameRoot(path));
}

HANDLE WINAPI RedirectedCreateFile(LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa,
                                   DWORD disposition, DWORD flags, HANDLE templ) {
    char b[MAX_PATH];
    bool write = install::WritesFile(access, disposition);
    bool keep = disposition == OPEN_EXISTING || disposition == OPEN_ALWAYS;
    const char* target = write ? WritePath(name, b, keep) : ReadPath(name, b);
    // The game opens some data files for reading without sharing them; parallel harness runs read
    // the same install, so reads always let others read too.
    if (!write) share |= FILE_SHARE_READ;
    if (write) savetrace::OnFileWrite(name);   // EI_SAVETRACE (harness_savetrace.h)
    HANDLE file = g_createFile(target, access, share, sa, disposition, flags, templ);
    crash::OnFileOpened(name, target, file, write);   // "diskfull" (harness_crash.h)
    return file;
}

// Registry: harness runs never write the game's settings (it saves its options on exit). Its reads, and
// everything else on its keys, are answered from the committed snapshot (harness_settings.h).
constexpr std::uintptr_t kRegSetValueExASlot = 0x0073B018;   // game.exe import table
decltype(&::RegSetValueExA) g_regSetValue;

LONG WINAPI SkippedRegSetValue(HKEY, LPCSTR name, DWORD, DWORD, const BYTE*, DWORD) {
    static bool logged = false;
    if (!logged) Log("harness: registry writes are skipped (first: %s)", name ? name : "(default)");
    logged = true;
    return ERROR_SUCCESS;
}

// Font smoothing is a setting of the whole desktop: the credits screen (0062B7D0 / 0062BD00) switches
// it off and "restores" a value it never read (off), so a harness game left it off for every program
// on the machine, and text in runs before and after differed (GDI's grey edges or none: the
// widescreen comparison of two runs). Harness runs leave the setting alone, and the game's fonts are
// created without smoothing (NONANTIALIASED_QUALITY), as they look once the titles have switched it
// off, whatever the desktop's setting and other programs do to it during the run.
constexpr std::uintptr_t kSystemParametersInfoASlot = 0x0073B4C0;   // game.exe import table
constexpr std::uintptr_t kCreateFontIndirectASlot = 0x0073B068;     // game.exe import table
constexpr UINT kSetFontSmoothing = 0x4B;                             // SPI_SETFONTSMOOTHING
decltype(&::SystemParametersInfoA) g_systemParametersInfo;
decltype(&::CreateFontIndirectA) g_createFontIndirect;

BOOL WINAPI HarnessSystemParametersInfo(UINT action, UINT uiParam, PVOID pvParam, UINT winIni) {
    if (action != kSetFontSmoothing) return g_systemParametersInfo(action, uiParam, pvParam, winIni);
    static bool logged = false;
    if (!logged) Log("harness: font smoothing left as the desktop has it (the game asked for %u)", uiParam);
    logged = true;
    return TRUE;
}

HFONT WINAPI HarnessCreateFontIndirect(const LOGFONTA* font) {
    if (!font) return g_createFontIndirect(font);
    LOGFONTA plain = *font;
    plain.lfQuality = NONANTIALIASED_QUALITY;
    return g_createFontIndirect(&plain);
}

// The "show tutorial" option (options object 0079BC70 + 0x208, registry value ShowTutorial). With it
// off, tutorial windows open only where the game forces them (005F86F0, 005F8A80). The harness
// clears it every frame, since the options are reloaded when a game starts.
int& g_showTutorial = ei::GameData<int>(0x0079BE78);

// Game speed. The main loop runs one world update per 0078FF34 milliseconds of real time (55 by
// default; the game's own speed-up key, keypad +, sets 27). "speed <ms>" holds it at <ms> every
// frame, since loading a zone resets it, and again after every world update (HeldServerUpdate,
// HeldClientSegment): the battle clock's set-up (00648520) and CBattleScreen::Close (00635E60,
// on leaving a screen such as the camp) set 55 inside the client's tick, and the main loop's client
// loop, which tested its backlog against the held 10 before the tick, subtracts the 55 it reads
// after it: the unsigned backlog (10..54) wraps to about 4.29e9 and the loop runs some 78 million
// client ticks (minutes). Real-time runs mostly got away with it by timing; on the virtual clock
// ui_camp_deals, ui_trade_hold, effects_bow, ... hung every time. 0 = leave it to the game.
unsigned& g_tickLength = ei::GameData<unsigned>(0x0078FF34);
unsigned g_heldTickLength = 0;

// The main loop's calls of the server's update and the client's tick (thiscall, EAX 0 stops the
// frame), redirected here (the rewritten loop calls through the same CALLs' targets, app_main.cpp).
constexpr std::uintptr_t kServerUpdateCall = 0x00457314;
constexpr std::uintptr_t kServerUpdate = 0x0058DFF0;
constexpr std::uintptr_t kClientSegmentCall = 0x00457368;
constexpr std::uintptr_t kClientSegment = 0x005641F0;
using WorldUpdate = int(__fastcall*)(void*, void*);
int __fastcall HeldServerUpdate(void* server, void*) {
    int result;
    {
        timing::Scope scope(timing::Server);
        result = ei::Code<WorldUpdate>(kServerUpdate)(server, nullptr);
    }
    if (g_heldTickLength) g_tickLength = g_heldTickLength;
    {
        timing::Scope scope(timing::Digest);
        digest::AfterWorldUpdate(server);   // a state record when its period says so
    }
    watch::AfterWorldUpdate();          // EI_WATCH_CUNIT (harness_watch.h)
    watch::ScrubStack();                // EI_STACK_SCRUB (harness_watch.h)
    return result;
}
int __fastcall HeldClientSegment(void* client, void*) {
    int result;
    {
        timing::Scope scope(timing::Client);
        result = ei::Code<WorldUpdate>(kClientSegment)(client, nullptr);
    }
    if (g_heldTickLength) g_tickLength = g_heldTickLength;
    return result;
}

// Startup movies: config\movie.ini lists them per section ("Start": 1C.bik, Nival.bik), read with
// GetPrivateProfileStringA. Harness runs read the "movies" keys as empty, so nothing plays.
constexpr std::uintptr_t kGetPrivateProfileStringASlot = 0x0073B23C;   // game.exe import table
decltype(&::GetPrivateProfileStringA) g_getProfileString;

DWORD WINAPI HarnessGetProfileString(LPCSTR app, LPCSTR key, LPCSTR fallback, LPSTR out, DWORD size, LPCSTR file) {
    std::size_t length = file ? std::strlen(file) : 0;
    if (key && _stricmp(key, "movies") == 0 && length >= 9 && _stricmp(file + length - 9, "movie.ini") == 0) {
        if (out && size) out[0] = '\0';
        return 0;
    }
    char b[MAX_PATH];
    return g_getProfileString(app, key, fallback, out, size, ReadPath(file, b));
}

// The other profile calls read the overlay copy too; writes go to it.
constexpr std::uintptr_t kGetPrivateProfileSectionASlot = 0x0073B234;
constexpr std::uintptr_t kGetPrivateProfileIntASlot = 0x0073B238;
constexpr std::uintptr_t kWritePrivateProfileStringASlot = 0x0073B240;
constexpr std::uintptr_t kGetPrivateProfileSectionNamesASlot = 0x0073B268;
decltype(&::GetPrivateProfileSectionA) g_getProfileSection;
decltype(&::GetPrivateProfileIntA) g_getProfileInt;
decltype(&::WritePrivateProfileStringA) g_writeProfileString;
decltype(&::GetPrivateProfileSectionNamesA) g_getProfileSectionNames;

DWORD WINAPI RedirectedGetProfileSection(LPCSTR app, LPSTR out, DWORD size, LPCSTR file) {
    char b[MAX_PATH];
    return g_getProfileSection(app, out, size, ReadPath(file, b));
}
UINT WINAPI RedirectedGetProfileInt(LPCSTR app, LPCSTR key, INT fallback, LPCSTR file) {
    char b[MAX_PATH];
    return g_getProfileInt(app, key, fallback, ReadPath(file, b));
}
BOOL WINAPI RedirectedWriteProfileString(LPCSTR app, LPCSTR key, LPCSTR value, LPCSTR file) {
    char b[MAX_PATH];
    return g_writeProfileString(app, key, value, file ? WritePath(file, b, true) : file);
}
DWORD WINAPI RedirectedGetProfileSectionNames(LPSTR out, DWORD size, LPCSTR file) {
    char b[MAX_PATH];
    return g_getProfileSectionNames(out, size, ReadPath(file, b));
}

BOOL WINAPI RedirectedDeleteFile(LPCSTR name) {
    char b[MAX_PATH];
    return g_deleteFile(WritePath(name, b, false));
}
BOOL WINAPI RedirectedCopyFile(LPCSTR from, LPCSTR to, BOOL failIfExists) {
    char b1[MAX_PATH], b2[MAX_PATH];
    return g_copyFile(ReadPath(from, b1), WritePath(to, b2, false), failIfExists);
}
BOOL WINAPI RedirectedMoveFile(LPCSTR from, LPCSTR to) {
    char b1[MAX_PATH], b2[MAX_PATH], b3[MAX_PATH];
    const char* source = ReadPath(from, b1);
    const char* target = WritePath(to, b2, false);
    // An install file is never moved away: it is copied into the overlay instead.
    if (source == from && ShadowOf(from, b3)) return g_copyFile(source, target, TRUE);
    return g_moveFile(source, target);
}
HANDLE WINAPI RedirectedFindFirstFile(LPCSTR name, LPWIN32_FIND_DATAA data) {
    char b[MAX_PATH];
    return g_findFirstFile(ReadPath(name, b), data);
}
BOOL WINAPI RedirectedCreateDirectory(LPCSTR name, LPSECURITY_ATTRIBUTES sa) {
    char b[MAX_PATH];
    return g_createDirectory(WritePath(name, b, false), sa);
}
BOOL WINAPI RedirectedRemoveDirectory(LPCSTR name) {
    char b[MAX_PATH];
    return g_removeDirectory(WritePath(name, b, false));
}
DWORD WINAPI RedirectedGetFileAttributes(LPCSTR name) {
    char b[MAX_PATH];
    return g_getFileAttributes(ReadPath(name, b));
}
BOOL WINAPI RedirectedSetFileAttributes(LPCSTR name, DWORD attributes) {
    char b[MAX_PATH];
    return g_setFileAttributes(WritePath(name, b, true), attributes);
}

namespace noforeground {
void Report(FILE* f);
}

void WriteReport() {
    clock::FlushRandomTrace();
    digest::Flush();
    ei::vectors::Flush();
    if (census::Enabled()) census::Write();
    FILE* f = nullptr;
    if (fopen_s(&f, g_reportPath, "w") != 0 || !f) return;
    if (!g_capturedFrames.empty()) std::fprintf(f, "\nframes captured:\n");
    for (const std::string& frame : g_capturedFrames) std::fprintf(f, "  %s\n", frame.c_str());
    if (std::string digest = digest::Summary(); !digest.empty()) std::fprintf(f, "\nstate digest: %s\n", digest.c_str());
    noforeground::Report(f);
    netlocal::Report(f);
    pair::Report(f);
    input::Report(f);
    timing::Report(f);
    std::fprintf(f, "\nconsole output:\n");
    for (const std::string& line : g_console) std::fprintf(f, "  %s\n", line.c_str());
    std::vector<std::string> failures = g_failures;
    if (std::string write = crash::FailedWrite(); !write.empty()) failures.push_back(write);   // harness_crash.h
    if (std::string paired = pair::Failure(); !paired.empty()) failures.push_back(paired);    // harness_pair.h
    if (std::string pinned = settings::Failure(); !pinned.empty()) failures.push_back(pinned);   // harness_settings.h
    if (std::string written = install::Failure(); !written.empty()) failures.push_back(written);   // harness_install.h
    if (std::string patched = addon::Failure(); !patched.empty()) failures.push_back("addon: " + patched);   // harness_addon.h
    const char* missed = "";
    if (const int count = render::MissedSurfaceWrites(&missed))   // a writer without a mark (R16a, R16b)
        failures.push_back(std::to_string(count) + " window surface or memory texture change(s) without a SurfaceWritten mark, the first: " + missed);
    if (const int skipped = ei::UnexpectedSkips())   // rewrites the run never reached (SV6)
        failures.push_back(std::to_string(skipped) + " registration(s) left out at hook install (\"(unexpected)\" in ei_hook.log)");
    for (const std::string& failure : failures) std::fprintf(f, "FAILED: %s\n", failure.c_str());
    std::fprintf(f, "RESULT: %s\n", !failures.empty() ? "FAIL" : "PASS");
    std::fclose(f);
    Log("harness: report written (%u failures)", unsigned(failures.size()));
}

// Deterministic mode, the default (a "realtime" line anywhere in the scenario turns it off,
// "deterministic" back on; EI_HARNESS_CLOCK overrides both): the virtual clock (harness_clock.h;
// every frame of the main loop runs exactly five world updates, the main thread's sleeps and timed
// waits take no real time, sample ends come on virtual time), a fixed date and fixed random seeds,
// so two runs of the same scenario take the same course, and a run with and without hooks
// (tools/compare.py) must print the same console output. The harness's waits count virtual time
// too, each with a real-time safety limit. Scenarios that do not repeat yet: docs/open-questions.md.
bool g_deterministic = true;
// "clocktrace": log the game's clock reads and waits (harness_clock.h).
bool g_clockTrace = false;
// "randtrace": log every random draw (harness_clock.h), installed at the first message loop pass.
bool g_randomTrace = false;
// "lfheap": on the virtual clock, leave the CRT's heap as the game makes it (its addresses do not
// repeat; harness_heap.h).
bool g_keepHeap = false;
// "heaptrace": log the CRT heap's calls into heaptrace.txt (harness_heap.h).
bool g_heapTrace = false;
constexpr std::uintptr_t kGetLocalTimeSlot = 0x0073B1DC;   // game.exe import table
constexpr std::uintptr_t kGetSystemTimeSlot = 0x0073B228;
decltype(&::GetLocalTime) g_getLocalTime;
decltype(&::GetSystemTime) g_getSystemTime;
// GetLocalTime/GetSystemTime (the main menu's time of day, save dates) return a fixed date.
void WINAPI FixedTime(LPSYSTEMTIME t) {
    *t = SYSTEMTIME{2001, 3, 1, 26, 12, 0, 0, 0};   // 2001-03-26 12:00, game.exe's build date
}

// Frozen clocks ("freeze" / "unfreeze", for frame captures). While frozen, the game's timeGetTime
// and GetTickCount return what they returned when frozen: the main loop (004571DC) sees no time
// pass, so no world, client or camera update runs, and what the game animates from these clocks
// (interface fades, the frame timer, tooltips) holds still, while every loop still draws and
// presents a frame. Unfreezing resumes both clocks from where they stopped (no jump). Installed
// only when the scenario uses "freeze" (in deterministic mode the virtual clock just stops). Such
// a scenario also gets the fixed date: the main menu's picture follows the time of day.
// The camera is held too: the main loop calls CCamera::Update once per frame even when no time has
// passed (at 00457595, with dt 0), and parts of it move by a fixed fraction per call, not per
// unit of time (the focus drifts a fifth of the way to the tracked object, the focus height a
// tenth of the way to the ground, ApplyView lifts the camera over nearby objects). Measured on
// two frozen captures three frames apart: the whole scene moved about 1 pixel at 3840x2160.
// While frozen that call is skipped (HeldCameraUpdate).
// Limits: what already moves by itself between frames (other threads, other frame-counted
// effects) is not held; the harness's own waits use real time (in deterministic mode the virtual
// clock, which stops too: use "capture", not "wait", while frozen there).
bool g_freezeUsed = false;

// The main loop's per-frame camera update: CALL 0040FB30 at 00457595 (thiscall on the camera
// 0079B2A8, one float, RET 4), redirected here when the scenario freezes.
constexpr std::uintptr_t kFrameCameraUpdateCall = 0x00457595;
constexpr std::uintptr_t kCameraUpdate = 0x0040FB30;
void __fastcall HeldCameraUpdate(void* camera, void*, float dt) {
    if (clock::Frozen()) return;
    ei::Code<void(__fastcall*)(void*, void*, float)>(kCameraUpdate)(camera, nullptr, dt);
}

// A pass's frame step (CL draw, DL cursor): a pair's game ends its turn first, so that it draws while
// the other game takes its turn (harness_pair.h). Only the main loop's call; the frame steps of modal
// loops and screens stay inside the turn.
constexpr std::uintptr_t kFrameStepCall = 0x004575FD;
constexpr std::uintptr_t kFrameStep = 0x00458010;
void __fastcall HandOffThenFrameStep(bool draw, bool cursor) {
    pair::HandOff();
    ei::Code<void(__fastcall*)(bool, bool)>(kFrameStep)(draw, cursor);
}

// Points the rel32 of the CALL at `site` (which must call `expected`) at `to`.
bool RedirectCall(std::uintptr_t site, std::uintptr_t expected, const void* to) {
    auto* p = reinterpret_cast<unsigned char*>(site);
    std::int32_t rel = 0;
    std::memcpy(&rel, p + 1, 4);
    if (p[0] != 0xE8 || site + 5 + rel != expected) {
        Log("harness: unexpected code at %08X (not a call of %08X); not redirected", unsigned(site), unsigned(expected));
        return false;
    }
    rel = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(to) - (site + 5));
    ei::NoteHarnessPatch(site, 5, "a CALL the harness redirects");
    DWORD old;
    VirtualProtect(p + 1, 4, PAGE_EXECUTE_READWRITE, &old);
    std::memcpy(p + 1, &rel, 4);
    VirtualProtect(p + 1, 4, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
    return true;
}

// 0049F660 seeds the ISAAC generator: srand(timeGetTime()), then 0x400 bytes from a randomly
// chosen file under C:\ (!). In deterministic mode the results are a fixed sequence instead, and
// rand() gets a fixed seed; 0049F150 then mixes them exactly as before.
using SrandFn = void(__cdecl*)(unsigned);
void __fastcall FixedIsaacSeed(void* isaac) {
    ei::Code<SrandFn>(0x006E77EA)(12345);
    auto* results = reinterpret_cast<std::uint32_t*>(static_cast<char*>(isaac) + 8);
    std::uint32_t x = 0x2001C0DE;
    for (int i = 0; i < 256; ++i) results[i] = x = x * 1664525u + 1013904223u;
}

// game.exe's function at `from` replaced by `to`: a JMP written there, in the image (no code there, the code section
// guarded) the code for the address (ei::RedirectCode).
void JumpTo(std::uintptr_t from, const void* to) {
    auto* p = reinterpret_cast<unsigned char*>(from);
    ei::NoteHarnessPatch(from, 5, "the harness's replacement");
    if (ei::Standalone()) {
        ei::RedirectCode(from, to);
        return;
    }
    DWORD old;
    VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old);
    p[0] = 0xE9;
    std::int32_t rel = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(to) - (from + 5));
    std::memcpy(p + 1, &rel, 4);
    VirtualProtect(p, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
}

// The clock the harness's waits and timeouts use.
DWORD Now() { return clock::Now(); }

// Input for the interface screens: window messages to the game window (0079BC28; its window
// procedure 00457970 handles the mouse and key messages).
HWND GameWindow() { return ei::GameData<HWND>(0x0079BC28); }

// Modifier keys held around an input step ("hold", "release" and the <mods> words of the mouse and key
// steps). The game reads them from the key messages (CInterface3D::OnKeyDown sets +0x7C / +0x7D / +0x7E
// by scan code, OnKeyUp clears them by virtual key), so these are posted like a keyboard's: Ctrl and
// Shift as WM_KEYDOWN / WM_KEYUP, Alt as the system messages Windows sends for it.
enum Modifier : unsigned { kModCtrl = 1, kModShift = 2, kModAlt = 4 };

// The modifier a word names (0: none).
unsigned ModifierWord(const std::string& word) {
    if (word == "ctrl") return kModCtrl;
    if (word == "shift") return kModShift;
    if (word == "alt") return kModAlt;
    return 0;
}

// Words of `text` separated by spaces, "+" or ","; `bad` gets the first word that is no modifier.
unsigned ParseModifiers(const std::string& text, std::string* bad) {
    unsigned mods = 0;
    std::string word;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        char c = i < text.size() ? text[i] : ' ';
        if (c == ' ' || c == '+' || c == ',' || c == '\t') {
            if (!word.empty()) {
                if (unsigned one = ModifierWord(word)) mods |= one;
                else if (bad && bad->empty()) *bad = word;
                word.clear();
            }
        } else {
            word += c;
        }
    }
    return mods;
}

void PostModifiers(unsigned mods, bool down) {
    struct Key { unsigned mod; UINT vk, scan; bool system; };
    static const Key keys[] = {{kModCtrl, VK_CONTROL, 0x1D, false}, {kModShift, VK_SHIFT, 0x2A, false}, {kModAlt, VK_MENU, 0x38, true}};
    for (const Key& key : keys) {
        if (!(mods & key.mod)) continue;
        LPARAM lparam = 1 | (key.scan << 16) | (key.system ? 0x20000000 : 0);
        if (down) PostMessageA(GameWindow(), key.system ? WM_SYSKEYDOWN : WM_KEYDOWN, key.vk, lparam);
        else PostMessageA(GameWindow(), key.system ? WM_SYSKEYUP : WM_KEYUP, key.vk, lparam | 0xC0000000u);
    }
}

// The modifiers at the end of an input step's argument, split off it (`argument` keeps the rest) and
// held until this goes out of scope. Only words after at least one other word count, so "key ctrl"
// stays the key ctrl. The press and release go through the same message queue as the step's input.
struct HeldModifiers {
    explicit HeldModifiers(std::string* argument) {
        std::string rest = *argument;
        for (;;) {
            std::size_t end = rest.find_last_not_of(" \t+,");
            if (end == std::string::npos) break;
            std::size_t begin = rest.find_last_of(" \t+,", end);
            if (begin == std::string::npos) break;   // a single word is the argument itself
            unsigned one = ModifierWord(rest.substr(begin + 1, end - begin));
            if (!one) break;
            mods_ |= one;
            rest.erase(begin);
        }
        *argument = rest;
        PostModifiers(mods_, true);
    }
    ~HeldModifiers() { PostModifiers(mods_, false); }
    unsigned mods_ = 0;
};

// The game's TranslateMessage (import slot 0073B450; WinMain's loop and the movie loop call it). Windows makes a
// WM_CHAR from a key message with the thread's keyboard layout and key state, which follow the user's own layout
// switches and held keys even though the harness's game never has the focus: in pairs a typed hero name came out
// "ppeEeErR" (Shift held) or "peerr" (a layout without Latin letters) as well as "ppeeeerr" (SV5d2). Harness runs
// translate as the US layout with no key held: letters and digits make no WM_CHAR here (the "key" step posts
// theirs, so a name comes out as typed), the other keys their fixed character (Enter 0x0D, Esc 0x1B, ...).
constexpr std::uintptr_t kTranslateMessageSlot = 0x0073B450;   // game.exe import table
decltype(&::TranslateMessage) g_translateMessage;

char FixedKeyChar(WPARAM vk) {
    switch (vk) {
    case VK_BACK: return 0x08;
    case VK_TAB: return 0x09;
    case VK_RETURN: return 0x0D;
    case VK_ESCAPE: return 0x1B;
    case VK_SPACE: return ' ';
    case VK_MULTIPLY: return '*';
    case VK_ADD: return '+';
    case VK_SUBTRACT: return '-';
    case VK_DIVIDE: return '/';
    case VK_OEM_1: return ';';
    case VK_OEM_PLUS: return '=';
    case VK_OEM_COMMA: return ',';
    case VK_OEM_MINUS: return '-';
    case VK_OEM_PERIOD: return '.';
    case VK_OEM_2: return '/';
    case VK_OEM_3: return '`';
    case VK_OEM_4: return '[';
    case VK_OEM_5: return '\\';
    case VK_OEM_6: return ']';
    case VK_OEM_7: return '\'';
    default: return 0;
    }
}

BOOL WINAPI HarnessTranslateMessage(const MSG* message) {
    if (!message || message->message != WM_KEYDOWN) return FALSE;   // no Alt in the key state: no WM_SYSCHAR
    const char c = FixedKeyChar(message->wParam);
    if (!c) return FALSE;
    PostMessageA(message->hwnd, WM_CHAR, static_cast<unsigned char>(c), message->lParam);
    return TRUE;
}

void PostKey(const std::string& name) {
    UINT vk = 0;
    if (name.size() == 1 && std::isalnum(static_cast<unsigned char>(name[0]))) vk = std::toupper(static_cast<unsigned char>(name[0]));
    else if (name == "esc") vk = VK_ESCAPE;
    else if (name == "enter" || name == "enterchar") vk = VK_RETURN;
    else if (name == "space") vk = VK_SPACE;
    else if (name == "tab") vk = VK_TAB;
    else if (name == "up") vk = VK_UP;
    else if (name == "down") vk = VK_DOWN;
    else if (name == "left") vk = VK_LEFT;
    else if (name == "right") vk = VK_RIGHT;
    else if (name == "plus") vk = VK_ADD;
    else if (name == "minus") vk = VK_SUBTRACT;
    else if (name == "grave") vk = VK_OEM_3;   // the console key (scan code 0x29)
    else if (name == "backspace") vk = VK_BACK;
    else if (name == "delete") vk = VK_DELETE;
    else if (name == "home") vk = VK_HOME;
    else if (name == "end") vk = VK_END;
    else if (name == "pgup") vk = VK_PRIOR;
    else if (name == "pgdn") vk = VK_NEXT;
    else if (name == "scrolllock") vk = VK_SCROLL;   // scan 0x46: the screenshot key (005F78C0)
    else if (name == "ctrl") vk = VK_CONTROL;   // scan 0x1D: sets CInterface3D +0x7C while down
    else if (name == "shift") vk = VK_SHIFT;    // scan 0x2A: +0x7D
    else if (name.size() >= 2 && name[0] == 'f') vk = VK_F1 + std::atoi(name.c_str() + 1) - 1;
    if (!vk) {
        Log("harness: unknown key '%s'", name.c_str());
        return;
    }
    UINT scan = vk == VK_OEM_3 ? 0x29 : MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
    PostMessageA(GameWindow(), WM_KEYDOWN, vk, 1 | (scan << 16));
    if (vk >= '0' && vk <= 'Z') PostMessageA(GameWindow(), WM_CHAR, std::tolower(static_cast<int>(vk)), 1 | (scan << 16));
    if (name == "enterchar") PostMessageA(GameWindow(), WM_CHAR, 0x0D, 1 | (scan << 16));
    PostMessageA(GameWindow(), WM_KEYUP, vk, 1 | (scan << 16) | 0xC0000000u);
}

// The client area mouse steps aim at: the game window's, or in the switch-over the frame's
// (render::FrameInWindow: the frame may be scaled into a larger window, R9c); a point of it reaches
// the window through ToWindow.
RECT GameClient() {
    RECT client{};
    int width = 0, height = 0;
    render::Rect placed{};
    if (render::FrameInWindow(&width, &height, &placed)) return RECT{0, 0, width, height};
    GetClientRect(GameWindow(), &client);
    return client;
}
POINT ToWindow(int x, int y) {
    render::FrameToWindow(&x, &y);
    return POINT{x, y};
}

// "mouseglide": the harness's mouse moved by a thread of its own (the main thread may be busy). A
// newer glide ends an older one.
struct Glide {
    POINT from, to;
    DWORD ms;
    LONG generation;
};
volatile LONG g_glideGeneration = 0;

DWORD WINAPI GlideThread(void* parameter) {
    Glide glide = *static_cast<Glide*>(parameter);
    delete static_cast<Glide*>(parameter);
    const DWORD start = GetTickCount();
    for (;;) {
        if (g_glideGeneration != glide.generation) return 0;
        DWORD elapsed = GetTickCount() - start;
        if (elapsed > glide.ms) elapsed = glide.ms;
        LONG x = glide.from.x + static_cast<LONG>((static_cast<long long>(glide.to.x - glide.from.x) * elapsed) / glide.ms);
        LONG y = glide.from.y + static_cast<LONG>((static_cast<long long>(glide.to.y - glide.from.y) * elapsed) / glide.ms);
        g_mouseX = x;
        g_mouseY = y;
        if (elapsed >= glide.ms) return 0;
        Sleep(10);
    }
}

void StartMouseGlide(int ms, int x0, int y0, int x1, int y1) {
    RECT client = GameClient();
    LONG w = client.right - client.left, h = client.bottom - client.top;
    auto* glide = new Glide{ToWindow(x0 * w / 800, y0 * h / 600), ToWindow(x1 * w / 800, y1 * h / 600), static_cast<DWORD>(ms),
                            InterlockedIncrement(&g_glideGeneration)};
    Log("harness: mouseglide: window (%ld, %ld) to (%ld, %ld) over %d ms", glide->from.x, glide->from.y, glide->to.x,
        glide->to.y, ms);
    if (HANDLE thread = CreateThread(nullptr, 0, &GlideThread, glide, 0, nullptr)) CloseHandle(thread);
    else delete glide;
}

// "cursormark" / "cursorcheck": the cursor thread's presents between frames (R9c2). The cursor
// thread places the cursor where the mouse was at its step before (005E52E0), so the rectangle
// trails the mouse by up to two of its 41 ms steps.
std::uint32_t g_cursorMark = 0;
constexpr int kCursorTrail = 24;   // frame pixels: the scenarios glide at most 0.25 px/ms

std::string CursorCheck(const std::string& argument) {
    char name[64] = "";
    unsigned least = 0, most = ~0u;
    if (sscanf_s(argument.c_str(), "%63s %u %u", name, unsigned(sizeof(name)), &least, &most) < 2)
        return "cursorcheck needs <name>|- <min> [max]";
    render::CursorPresentInfo info = render::CursorPresents();
    std::uint32_t count = info.presents - g_cursorMark;
    Log("harness: cursorcheck: %u presents by the cursor thread since cursormark (last at %d,%d-%d,%d); %d composed "
        "frames checked, %d differed; in all %u moves handed over, refused: %u picture, %u no frame, %u recent frame, %u outside",
        count, info.lastTo.left, info.lastTo.top, info.lastTo.right, info.lastTo.bottom, info.checks, info.mismatches, info.calls,
        info.refused[0], info.refused[1], info.refused[2], info.refused[3]);
    const CCursor* game = GameCursor();
    Log("harness: cursorcheck: the cursor: mode %d, thread %s, image shown %d", game->mode_, game->thread_ ? "running" : "none",
        game->imageShown_);
    char what[240];
    if (count < least || count > most) {
        std::snprintf(what, sizeof(what), "cursorcheck: %u presents by the cursor thread, expected %u..%u", count, least, most);
        return what;
    }
    if (info.mismatches) {
        std::snprintf(what, sizeof(what), "cursorcheck: %d of %d frames composed by the cursor thread differ", info.mismatches,
                      info.checks);
        return what;
    }
    if (std::strcmp(name, "-") == 0 || count == 0) return {};
    if (info.checks == 0) return "cursorcheck: the cursor thread's frames were not checked";
    int width = 0, height = 0;
    std::vector<std::uint8_t> bgr;
    std::string path;
    if (!render::ReadCursorPresent(&width, &height, &bgr) || !capture::WriteFrame(name, width, height, bgr, &path))
        return std::string("cursorcheck: cannot write the cursor thread's frame ") + name;
    g_capturedFrames.push_back(path + " (the cursor thread's last present)");
    const CursorImage* image = GameCursor()->image_;
    if (!image) return "cursorcheck: the cursor has no image";
    int fx = g_mouseX, fy = g_mouseY;
    render::WindowToFrame(&fx, &fy);
    int dx = info.lastTo.left - (fx - image->hotX), dy = info.lastTo.top - (fy - image->hotY);
    Log("harness: cursorcheck: %s; the mouse at frame (%d, %d), hot spot (%d, %d), the cursor %d, %d from mouse less hot spot",
        path.c_str(), fx, fy, image->hotX, image->hotY, dx, dy);
    if (dx < -kCursorTrail || dx > kCursorTrail || dy < -kCursorTrail || dy > kCursorTrail) {
        std::snprintf(what, sizeof(what), "cursorcheck: the cursor at %d,%d is %d, %d from the mouse (%d, %d) less its hot spot (%d, %d)",
                      info.lastTo.left, info.lastTo.top, dx, dy, fx, fy, image->hotX, image->hotY);
        return what;
    }
    return {};
}

void PostMouse(const std::string& verb, int x, int y) {
    RECT client = GameClient();
    POINT window = ToWindow(x * (client.right - client.left) / 800, y * (client.bottom - client.top) / 600);
    LPARAM at = MAKELPARAM(window.x, window.y);
    g_mouseX = window.x;
    g_mouseY = window.y;
    PostMessageA(GameWindow(), WM_MOUSEMOVE, 0, at);
    if (verb == "click") {
        PostMessageA(GameWindow(), WM_LBUTTONDOWN, MK_LBUTTON, at);
        PostMessageA(GameWindow(), WM_LBUTTONUP, 0, at);
    } else if (verb == "rclick") {
        PostMessageA(GameWindow(), WM_RBUTTONDOWN, MK_RBUTTON, at);
        PostMessageA(GameWindow(), WM_RBUTTONUP, 0, at);
    } else if (verb == "dblclick") {
        // What Windows sends for a double click: down, up, double click, up.
        PostMessageA(GameWindow(), WM_LBUTTONDOWN, MK_LBUTTON, at);
        PostMessageA(GameWindow(), WM_LBUTTONUP, 0, at);
        PostMessageA(GameWindow(), WM_LBUTTONDBLCLK, MK_LBUTTON, at);
        PostMessageA(GameWindow(), WM_LBUTTONUP, 0, at);
    } else if (verb == "ldown") {
        PostMessageA(GameWindow(), WM_LBUTTONDOWN, MK_LBUTTON, at);
    } else if (verb == "lup") {
        PostMessageA(GameWindow(), WM_LBUTTONUP, 0, at);
    } else if (verb == "rdown") {
        PostMessageA(GameWindow(), WM_RBUTTONDOWN, MK_RBUTTON, at);
    } else if (verb == "rup") {
        PostMessageA(GameWindow(), WM_RBUTTONUP, 0, at);
    } else if (verb == "rdblclick") {
        PostMessageA(GameWindow(), WM_RBUTTONDOWN, MK_RBUTTON, at);
        PostMessageA(GameWindow(), WM_RBUTTONUP, 0, at);
        PostMessageA(GameWindow(), WM_RBUTTONDBLCLK, MK_RBUTTON, at);
        PostMessageA(GameWindow(), WM_RBUTTONUP, 0, at);
    }
}

// A wheel turn at (x, y) in the 800x600 layout. WM_MOUSEWHEEL carries screen coordinates.
void PostWheel(int x, int y, int delta) {
    RECT client = GameClient();
    POINT point = ToWindow(x * (client.right - client.left) / 800, y * (client.bottom - client.top) / 600);
    PostMessageA(GameWindow(), WM_MOUSEMOVE, 0, MAKELPARAM(point.x, point.y));
    ClientToScreen(GameWindow(), &point);
    PostMessageA(GameWindow(), WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<short>(delta)), MAKELPARAM(point.x, point.y));
}

// The centre of region `index` of the top interface window, posted as a mouse event. The region's
// layout rectangle (plus the offset its hit test subtracts) goes through the game's layout-to-screen
// scaling (005F7D20, patched by Rebirth) and then to the window's client size.
void PostRegionMouse(const std::string& verb, int index) {
    auto* ui = ei::GamePointer<CInterface3D>(0x007C7D50);
    CUiWindow* window = ui->GetActiveWindow();
    if (!window || index < 0 || index >= window->regions_.size || !window->regions_.data[index]) {
        Log("harness: %s %d: no such region (window %p, %d regions)", verb.c_str(), index, window,
            window ? window->regions_.size : 0);
        return;
    }
    const CUiRegion* region = window->regions_.data[index];
    POINT point{(region->bounds_.left + region->bounds_.right) / 2 + region->offsetX_,
                (region->bounds_.top + region->bounds_.bottom) / 2 + region->offsetY_};
    Log("harness: %s %d: window %p region (%d,%d)-(%d,%d) enabled %d", verb.c_str(), index, window,
        region->bounds_.left, region->bounds_.top, region->bounds_.right, region->bounds_.bottom, region->enabled_);
    ui->CallScalePointToScreen(&point);
    RECT client = GameClient();
    if (ui->width_ > 0 && ui->height_ > 0) {
        point.x = point.x * (client.right - client.left) / ui->width_;
        point.y = point.y * (client.bottom - client.top) / ui->height_;
    }
    point = ToWindow(point.x, point.y);
    LPARAM at = MAKELPARAM(point.x, point.y);
    PostMessageA(GameWindow(), WM_MOUSEMOVE, 0, at);
    if (verb == "clickregion") {
        PostMessageA(GameWindow(), WM_LBUTTONDOWN, MK_LBUTTON, at);
        PostMessageA(GameWindow(), WM_LBUTTONUP, 0, at);
    } else if (verb == "rclickregion") {
        PostMessageA(GameWindow(), WM_RBUTTONDOWN, MK_RBUTTON, at);
        PostMessageA(GameWindow(), WM_RBUTTONUP, 0, at);
    } else if (verb == "dblclickregion") {
        // As "dblclick": down, up, double click, up.
        PostMessageA(GameWindow(), WM_LBUTTONDOWN, MK_LBUTTON, at);
        PostMessageA(GameWindow(), WM_LBUTTONUP, 0, at);
        PostMessageA(GameWindow(), WM_LBUTTONDBLCLK, MK_LBUTTON, at);
        PostMessageA(GameWindow(), WM_LBUTTONUP, 0, at);
    }
}

// --- clickunit <name> [l|r|move] [mods] (ticket I8; the modifiers are HeldModifiers') -------------------------------------------
// A click on a unit's body, placed where the game's own picking finds it. The client world
// (007AF5B8) keeps a list (+0xF0, header pointer; nodes: next +0, value +8: 0x18 bytes, the
// object id at +0xC and its screen rectangle at +0x10) of the objects drawn this frame with the
// rectangles of their projected vertices (written by the object draw, 004E8900 / 005A7A70).
// 00565E40 (the world's thiscall, RET 4) is the unit at a point that the battle screen's left
// button release (00638B30) asks with the message's own coordinates: those rectangles that hold
// the point, the client object's finer test (+0x94 slot 24), the nearest unit (type 0x50). The
// step tries the rectangle's centre and then a grid inside it until 00565E40 answers with the
// unit, so a click there selects or orders that unit and no other in front of it.
// 00461360 (__fastcall) turns the script name into the id AddUnitToServer gave the unit.
constexpr std::uintptr_t kClientWorld = 0x007AF5B8;
constexpr int kClientPickList = 0xF0;
constexpr std::uint32_t kPickableUnit = 0x400000;   // the flag 00638B30 requires of the unit

// Where a click finds the unit with script name `name` in this frame (client coordinates) and the
// unit the game's pick returns there; "" or why there is no such point. `what` names the step in
// the log and the error (null: nothing logged).
std::string FindUnitPoint(const char* name, const char* what, POINT* point, void** unit) {
    std::string label = std::string(what ? what : "waitunit") + " " + name;
    const bool byId = name[0] == '#';   // a map object: the battle screen's object pick
    int id = byId ? std::atoi(name + 1) : TextKeyFromName(name);
    auto* world = ei::GamePointer<std::uint8_t>(kClientWorld);
    auto* header = *reinterpret_cast<std::uint8_t**>(world + kClientPickList);
    if (!header) return label + ": no pick list (not in a game)";
    // The unit's rectangle in this frame's list (the last entry for the id).
    const RECT* rect = nullptr;
    for (auto* node = *reinterpret_cast<std::uint8_t**>(header); node && node != header;
         node = *reinterpret_cast<std::uint8_t**>(node)) {
        if (*reinterpret_cast<int*>(node + 0xC) == id) rect = reinterpret_cast<const RECT*>(node + 0x10);
    }
    if (!rect) return label + " (id " + std::to_string(id) + "): not drawn this frame (off screen?)";
    if (what)
        Log("harness: %s: id %d, rectangle (%ld,%ld)-(%ld,%ld)", label.c_str(), id, rect->left, rect->top, rect->right,
            rect->bottom);
    // Candidates: the centre, then a 9x9 grid inside the rectangle from the middle outwards.
    std::vector<POINT> candidates;
    long cx = (rect->left + rect->right) / 2, cy = (rect->top + rect->bottom) / 2;
    candidates.push_back({cx, cy});
    constexpr int kSteps = 9;
    for (int ring = 1; ring <= kSteps / 2; ++ring) {
        for (int j = -ring; j <= ring; ++j) {
            for (int i = -ring; i <= ring; ++i) {
                if (i != -ring && i != ring && j != -ring && j != ring) continue;
                long x = cx + i * (rect->right - rect->left) / (kSteps + 1);
                long y = cy + j * (rect->bottom - rect->top) / (kSteps + 1);
                candidates.push_back({x, y});
            }
        }
    }
    for (const POINT& p : candidates) {
        int at[2] = {p.x, p.y};
        void* hit = byId ? static_cast<void*>(reinterpret_cast<CWorldClient*>(world)->PickAt2(reinterpret_cast<const long*>(at))) : reinterpret_cast<CWorldClient*>(world)->UnitAt(reinterpret_cast<const long*>(at));
        if (hit && *reinterpret_cast<int*>(static_cast<std::uint8_t*>(hit) + 0xC) == id) {
            *point = p;
            *unit = hit;
            return "";
        }
    }
    return label + ": the game's pick (00565E40) finds another object everywhere in its rectangle";
}

std::string PostUnitMouse(const std::string& argument) {
    char name[64] = "", button[8] = "l";
    if (sscanf_s(argument.c_str(), "%63s %7s", name, unsigned(sizeof(name)), button, unsigned(sizeof(button))) < 1)
        return "clickunit needs <name> [l|r|move|dbl] [ctrl|shift|alt]";
    POINT at{};
    void* hit = nullptr;
    std::string error = FindUnitPoint(name, "clickunit", &at, &hit);
    if (!error.empty()) return error;
    std::uint32_t flags = *reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(hit) + 8);
    RECT client{};
    GetClientRect(GameWindow(), &client);
    Log("harness: clickunit %s: at (%ld,%ld) (client %ldx%ld), flags %08X%s", name, at.x, at.y,
        client.right - client.left, client.bottom - client.top, flags,
        (flags & kPickableUnit) ? "" : " (no 0x400000: the battle screen ignores it)");
    // The battle screen reads the message's coordinates as they are (the pick space is the client
    // area); the harness's cursor follows them.
    at = ToWindow(at.x, at.y);   // the frame's pixel in the window (R9c)
    g_mouseX = at.x;
    g_mouseY = at.y;
    LPARAM where = MAKELPARAM(at.x, at.y);
    PostMessageA(GameWindow(), WM_MOUSEMOVE, 0, where);
    if (std::strcmp(button, "l") == 0) {
        PostMessageA(GameWindow(), WM_LBUTTONDOWN, MK_LBUTTON, where);
        PostMessageA(GameWindow(), WM_LBUTTONUP, 0, where);
    } else if (std::strcmp(button, "r") == 0) {
        PostMessageA(GameWindow(), WM_RBUTTONDOWN, MK_RBUTTON, where);
        PostMessageA(GameWindow(), WM_RBUTTONUP, 0, where);
    } else if (std::strcmp(button, "dbl") == 0) {
        // The second half of a double click only: the release after it carries the interface's
        // double-click flag (+0x7F) into the battle screen's SelectUnit (rebirth_fixes_ui).
        PostMessageA(GameWindow(), WM_LBUTTONDBLCLK, MK_LBUTTON, where);
        PostMessageA(GameWindow(), WM_LBUTTONUP, 0, where);
    }
    return "";
}

// --- clickground <x> <y> [click|dblclick|rclick|move] (SV5d1) -----------------------------------------
// A mouse step on the ground point (x, y) of the map: the 800x600 layout point whose ground, as the
// game's own pick finds it (CWorldClient::PickSurface 0056C350, what the briefing screen's walk and the
// battle screen's orders ask with the message's coordinates), lies nearest to (x, y). Searched on a
// 10-pixel grid, then pixel by pixel around the best; fails when no point comes within 1.5 units (the
// point is off screen or hidden). The pick only reads the view and the terrain (its pick stack entry
// is popped again), so the step is the plain mouse step at the point found.
__declspec(naked) float* __fastcall HarnessPickSurface(void* /*client world*/, void*, float* /*out*/, int /*x*/, int /*y*/)
    EI_ORIGINAL(0x0056C350)

// --- blink faces|open|close (R11d) ------------------------------------------------------------------
// faces: the client world loads its units' face textures (0056DE80, thiscall; the game does it when
// a zone is entered between zones, not for a new game). open / close: every unit of the client
// world with a face texture shows its face's open or closed state: the game's own blink (00539010 /
// 00538ED0, thiscall RET 4, forced), as the briefing's speakers do. Through the original addresses,
// so a run of the original code takes its own. Fails when no unit has a face.
__declspec(naked) void __fastcall HarnessLoadUnitFaces(void* /*client world*/) EI_ORIGINAL(0x0056DE80)
__declspec(naked) void __fastcall HarnessBlinkOpen(void* /*unit's client half*/, void*, int /*force*/) EI_ORIGINAL(0x00539010)
__declspec(naked) void __fastcall HarnessBlinkClose(void* /*unit's client half*/, void*, int /*force*/) EI_ORIGINAL(0x00538ED0)

std::string BlinkStep(const std::string& argument) {
    if (argument != "faces" && argument != "open" && argument != "close") return "blink needs faces, open or close";
    auto* world = ei::GamePointer<CWorldClient>(kClientWorld);
    if (argument == "faces") HarnessLoadUnitFaces(world);
    SectorListNode* head = world->objects_.head;
    if (!head) return "blink: no client world";
    int faces = 0;
    for (SectorListNode* node = head->next; node != head; node = node->next) {
        CEngineObject* object = node->data;
        if (!object || object->GetClassId() != kClassUnit) continue;
        auto* half = reinterpret_cast<CUnitClientSpecific*>(static_cast<CUnit*>(object)->controller);
        if (!half || !half->faceTexture) continue;
        if (argument == "open") HarnessBlinkOpen(half, nullptr, 1);
        else if (argument == "close") HarnessBlinkClose(half, nullptr, 1);
        ++faces;
    }
    Log("harness: blink %s: %d units with a face", argument.c_str(), faces);
    return faces ? "" : "blink: no unit has a face texture";
}

std::string PostGroundMouse(const std::string& argument) {
    float wx = 0.0f, wy = 0.0f;
    char verb[16] = "click";
    if (sscanf_s(argument.c_str(), "%f %f %15s", &wx, &wy, verb, unsigned(sizeof(verb))) < 2)
        return "clickground needs <x> <y> [click|dblclick|rclick|move]";
    const std::string kind = verb;
    if (kind != "click" && kind != "dblclick" && kind != "rclick" && kind != "move")
        return "clickground: '" + kind + "' is no mouse step (click, dblclick, rclick, move)";
    const RECT client = GameClient();
    const int cw = client.right - client.left, ch = client.bottom - client.top;
    auto distance = [&](int lx, int ly) {
        float out[3] = {};
        HarnessPickSurface(ei::GamePointer<void>(kClientWorld), nullptr, out, lx * cw / 800, ly * ch / 600);
        const double dx = double(out[0]) - wx, dy = double(out[1]) - wy;
        return dx * dx + dy * dy;
    };
    int bestX = -1, bestY = -1;
    double best = 1e30;
    auto consider = [&](int lx, int ly) {
        if (lx < 0 || ly < 0 || lx >= 800 || ly >= 600) return;
        const double d = distance(lx, ly);
        if (d < best) best = d, bestX = lx, bestY = ly;
    };
    for (int ly = 5; ly < 600; ly += 10)
        for (int lx = 5; lx < 800; lx += 10) consider(lx, ly);
    const int cx = bestX, cy = bestY;
    for (int ly = cy - 10; ly <= cy + 10; ++ly)
        for (int lx = cx - 10; lx <= cx + 10; ++lx) consider(lx, ly);
    char text[200];
    std::snprintf(text, sizeof(text), "clickground %.2f %.2f: layout (%d,%d), %.2f away", wx, wy, bestX, bestY,
                  best < 1e29 ? std::sqrt(best) : -1.0);
    Log("harness: %s", text);
    if (!(best <= 1.5 * 1.5)) return std::string(text) + ": no screen point picks the ground there";
    PostMouse(kind, bestX, bestY);
    return "";
}

// --- groundview (SV5d3) ---------------------------------------------------------------------------
// Logs the map points the ground pick finds at the layout's centre and corners (where the camera looks;
// for writing a scenario's clickground steps).
void LogGroundView() {
    const RECT client = GameClient();
    const int cw = client.right - client.left, ch = client.bottom - client.top;
    char text[400] = "groundview:";
    for (const auto& at : {std::pair{400, 300}, std::pair{0, 0}, std::pair{799, 0}, std::pair{0, 599}, std::pair{799, 599}}) {
        float out[3] = {};
        HarnessPickSurface(ei::GamePointer<void>(kClientWorld), nullptr, out, at.first * cw / 800, at.second * ch / 600);
        const std::size_t used = std::strlen(text);
        std::snprintf(text + used, sizeof(text) - used, " (%d,%d) %.1f %.1f;", at.first, at.second, out[0], out[1]);
    }
    Log("harness: %s", text);
}

// --- minimap <cx> <cy> <sx> <sy>, lookat <x> <y> (SV5d3) -----------------------------------------------
// The battle screen's minimap is centred on the camera: a click at layout (cx + dx, cy + dy) moves the
// camera by (dx * sx, -dy * sy) map units ("minimap" gives the install's numbers: its HUD differs). lookat
// clicks the minimap where the camera, as the ground pick at the layout's centre finds it, would look at
// map point (x, y), at most 50 pixels from the centre; nothing when it is within 3 units already. The
// game's camera stops at the map's edge, and it goes back to the hero on its own at times, so a walk
// repeats lookat before its clickground steps.
struct MinimapLayout { float cx = 0, cy = 0, sx = 0, sy = 0; } g_minimap;

std::string LookAt(const std::string& argument) {
    float wx = 0.0f, wy = 0.0f;
    if (sscanf_s(argument.c_str(), "%f %f", &wx, &wy) != 2) return "lookat needs <x> <y>";
    if (!(g_minimap.sx > 0.0f && g_minimap.sy > 0.0f)) return "lookat: no minimap step before it";
    const RECT client = GameClient();
    const int cw = client.right - client.left, ch = client.bottom - client.top;
    float out[3] = {};
    HarnessPickSurface(ei::GamePointer<void>(kClientWorld), nullptr, out, 400 * cw / 800, 300 * ch / 600);
    const float dx = (wx - out[0]) / g_minimap.sx, dy = -(wy - out[1]) / g_minimap.sy;
    if (std::fabs(wx - out[0]) < 3.0f && std::fabs(wy - out[1]) < 3.0f) {
        Log("harness: lookat %.1f %.1f: the camera looks at %.1f %.1f", wx, wy, out[0], out[1]);
        return "";
    }
    const float length = std::sqrt(dx * dx + dy * dy), clamp = length > 50.0f ? 50.0f / length : 1.0f;
    const int lx = int(std::lround(g_minimap.cx + dx * clamp)), ly = int(std::lround(g_minimap.cy + dy * clamp));
    Log("harness: lookat %.1f %.1f: the camera looks at %.1f %.1f, minimap click (%d,%d)", wx, wy, out[0], out[1], lx, ly);
    PostMouse("click", lx, ly);
    return "";
}

// --- hudstate [minquick] (ticket I9) ----------------------------------------------------------
// Logs the battle screen's state (the window on top): the pending action (+0x17C, +0x180), each
// party member's selection (flags_ 0x10) and quick items (CUnit::GetQuickItem 00531920, slots
// 0-3), and the quick item panel's (+0x198) unit, entries shown and region items, the spell
// column's (+0x19C) count and highlight. With <minquick>, fails when the quick item panel shows
// fewer than that many of its unit's own quick items (regions 0-3).
std::string HudState(const std::string& argument) {
    auto* ui = ei::GamePointer<CInterface3D>(0x007C7D50);
    auto* screen = reinterpret_cast<std::uint8_t*>(ui->GetActiveWindow());
    if (!screen) return "hudstate: no window on top";
    auto at = [](const void* base, int offset) {
        return *reinterpret_cast<const int*>(static_cast<const std::uint8_t*>(base) + offset);
    };
    auto* battle = reinterpret_cast<CBattleScreen*>(screen);
    Log("harness: hudstate: window %p, mode %d, slot %d, item %d, %d members", screen, at(screen, 0x17C),
        at(screen, 0x180), at(screen, 0x184), battle->party_.size);
    for (int i = 0; i < battle->party_.size && i < 8; ++i) {
        CUnit* member = battle->party_[i];
        if (!member) continue;
        void* quick[4];
        for (int s = 0; s < 4; ++s) quick[s] = member->GetQuickItem(s);
        Log("harness: hudstate: member %d %p flags %08X, quick items %p %p %p %p", i, member,
            at(member, 0x08), quick[0], quick[1], quick[2], quick[3]);
    }
    auto* quickPanel = *reinterpret_cast<CHudQuickItems**>(screen + 0x198);
    int own = 0;
    if (quickPanel) {
        for (int r = 0; r < 4; ++r) {
            int entry = quickPanel->regionItem_[r];
            if (entry >= 0 && entry < quickPanel->entryCount_ && quickPanel->entries_[entry].own) ++own;
        }
        const int* items = quickPanel->regionItem_;
        Log("harness: hudstate: quick panel unit %p, %d entries (%d own), highlight %d, regions %d %d %d %d %d %d %d %d",
            quickPanel->unit_, quickPanel->entryCount_, own, quickPanel->selected_, items[0], items[1], items[2],
            items[3], items[4], items[5], items[6], items[7]);
        // The player's quest items in the top row: the id a lever's lock matches (SetScience).
        for (int e = 0; e < quickPanel->entryCount_; ++e) {
            const CHudQuickItems::Entry& entry = quickPanel->entries_[e];
            if (!entry.own && entry.item && entry.item->type_ == 0x3009)
                Log("harness: hudstate: quick panel entry %d: quest item id %u (object %d)", e,
                    static_cast<CItemQuest*>(entry.item)->questItemId_, entry.id);
        }
    }
    if (auto* spells = *reinterpret_cast<CHudSpellColumn**>(screen + 0x19C))
    {
        Log("harness: hudstate: spell column unit %p, %d spells, highlight %d", at(spells, 0x154), spells->count_,
            spells->selected_);
        // Each spell slot of the column's unit: the spell's prototype index and target flags.
        if (CUnit* unit = spells->unit_) {
            for (unsigned s = 0; s < 8; ++s) {
                auto* item = static_cast<CItem*>(unit->GetSpell(s));
                if (!item) continue;
                auto* spell = static_cast<CSpell*>(item->spell_);
                Log("harness: hudstate: spell slot %u item %p, prototype %d, flags %08X", s, item,
                    spell ? spell->prototypeIndex_ : -1, spell ? spell->targetFlags_ : 0);
            }
            // Its weapons (their kind: 5 bow, 6 crossbow) and the one in use.
            for (unsigned w = 0; w < 4; ++w)
                if (CEngineObject* weapon = unit->GetWeapon(w))
                    Log("harness: hudstate: weapon slot %u item %p kind %d%s", w, weapon,
                        static_cast<CItemWeapon*>(weapon)->GetType(), unit->activeWeapon == w ? " (in use)" : "");
        }
    }
    if (!argument.empty()) {
        int minimum = std::atoi(argument.c_str());
        if (own < minimum) return "hudstate: the quick item panel shows " + std::to_string(own) + " own items, wanted " +
                                  std::to_string(minimum);
    }
    return "";
}

// --- campputon <type> <count> (ticket I9) -----------------------------------------------------
// With the camp on top (screen 5, page 0): the first <count> items of class type <type> (hex,
// item +0x10: 0x3004 weapon, 0x3005 armour, 0x3006 quick item, 0x3008 spell container) in the
// item strip's source list (camp +0x1E0, strip +0x1AC: the player's items) are put on the hero as
// a click on them in the strip does: the camp's virtual slot 42 (MoveItem 00610AD0) with mode 1
// (PutOnFromStrip -> PutOn 0060FCA0). The strip's tabs do not matter. Logs every source item's
// type; fails when fewer than <count> items of the type are found or the camp is not on top.
std::string CampPutOn(const std::string& argument) {
    unsigned type = 0;
    int count = 0;
    if (sscanf_s(argument.c_str(), "%x %d", &type, &count) != 2) return "campputon needs <type hex> <count>";
    auto* ui = ei::GamePointer<CInterface3D>(0x007C7D50);
    auto* camp = reinterpret_cast<std::uint8_t*>(ui->GetActiveWindow());
    if (!camp || *reinterpret_cast<void**>(camp) != ei::GamePointer<void>(0x0074091C))
        return "campputon: the camp is not on top";
    int page = *reinterpret_cast<int*>(camp + 0x190);
    auto* strip = *reinterpret_cast<std::uint8_t**>(camp + 0x1E0);
    auto* source = strip ? *reinterpret_cast<UiPtrArray<std::uint8_t>**>(strip + 0x1AC) : nullptr;
    if (!source) return "campputon: the camp has no item strip source";
    std::vector<std::uint8_t*> chosen;
    for (int i = 0; i < source->size; ++i) {
        std::uint8_t* item = source->data[i];
        unsigned itemType = item ? *reinterpret_cast<unsigned*>(item + 0x10) : 0;
        Log("harness: campputon: page %d, source item %d %p type %04X", page, i, item, itemType);
        if (item && itemType == type && static_cast<int>(chosen.size()) < count) chosen.push_back(item);
    }
    using MoveFn = void(__thiscall*)(std::uint8_t*, std::uint8_t*, int);
    auto move = reinterpret_cast<MoveFn>((*reinterpret_cast<void***>(camp))[42]);
    for (std::uint8_t* item : chosen) {
        move(camp, item, 1);
        Log("harness: campputon: %p put on (MoveItem mode 1)", item);
    }
    if (static_cast<int>(chosen.size()) < count)
        return "campputon: " + std::to_string(chosen.size()) + " items of the type, wanted " + std::to_string(count);
    return "";
}

// --- waitwindow <seconds> <window> (ticket T1) ----------------------------------------------
// The interface windows a scenario waits for, by the two vtables each is built with (the window
// on top has one of them at +0; ui.h, ui_screens.h, ui_camp.h).
struct KnownWindow {
    const char* name;
    std::uintptr_t vtables[2];
};
constexpr KnownWindow kKnownWindows[] = {
    {"menu", {ei::DataId(0x00740688), ei::DataId(0x00740654)}},         // CMainMenu (CreateScreen 2)
    {"battle", {ei::DataId(0x00740764), ei::DataId(0x00740730)}},       // the in-game screen (CreateScreen 1)
    {"briefing", {ei::DataId(0x007405AC), ei::DataId(0x00740578)}},     // the base / briefing screen (CreateScreen 3)
    {"map", {ei::DataId(0x00740B08), ei::DataId(0x00740B3C)}},          // the island map (CreateScreen 4)
    {"camp", {ei::DataId(0x007408E8), ei::DataId(0x0074091C)}},         // CCampScreen (CreateScreen 5)
    {"view", {ei::DataId(0x00740840), ei::DataId(0x0074080C)}},         // CViewScreen (CreateScreen 7)
    {"trade", {ei::DataId(0x00740D18), ei::DataId(0x00740CE4)}},        // trading between players (CreateScreen 9)
    {"escmenu", {ei::DataId(0x00741270), ei::DataId(0x007412A4)}},      // CEscMenu
    {"saveload", {ei::DataId(0x00741388), ei::DataId(0x00741354)}},     // CSaveLoadWindow
    {"options", {ei::DataId(0x00741FAC), ei::DataId(0x00741F78)}},      // COptionsWindow
    {"objectives", {ei::DataId(0x007411BC), ei::DataId(0x00741188)}},   // CObjectivesWindow
    {"help", {ei::DataId(0x00740E28), ei::DataId(0x00740DF4)}},         // the help / message window
    {"msgbox", {ei::DataId(0x00742348), ei::DataId(0x00742314)}},       // CMessageBox
    {"difficulty", {ei::DataId(0x0074225C), ei::DataId(0x00742228)}},   // CDifficultyBox
    {"heroes", {ei::DataId(0x00742118), ei::DataId(0x0074214C)}},       // the multiplayer hero screen (main menu region 5)
    {"titles", {ei::DataId(0x00740C08), ei::DataId(0x00740C3C)}},       // CCreditsScreen
};

// The vtables of `name` (a name above or one vtable in hex, game.exe's address), where they are in this
// process (the data seam); false when unknown.
bool WindowVtables(const std::string& name, std::uintptr_t (&vtables)[2]) {
    for (const KnownWindow& w : kKnownWindows) {
        if (name == w.name) {
            vtables[0] = ei::GameAddress(w.vtables[0]);
            vtables[1] = ei::GameAddress(w.vtables[1]);
            return true;
        }
    }
    char* end = nullptr;
    unsigned long value = std::strtoul(name.c_str(), &end, 16);
    if (name.empty() || *end || value < 0x00401000 || value >= 0x00800000) return false;
    vtables[0] = vtables[1] = ei::GameAddress(value);
    return true;
}

// Whether the awaited window is on top and takes input (see the step's description).
bool WindowReady(std::uintptr_t (&vtables)[2]) {
    auto* ui = ei::GamePointer<CInterface3D>(0x007C7D50);
    CUiWindow* top = ui->GetActiveWindow();
    if (!top || ui->queuedWindow_ || g_request.kind != 0) return false;
    std::uintptr_t vtable = *reinterpret_cast<const std::uintptr_t*>(top);
    if (vtable != vtables[0] && vtable != vtables[1]) return false;
    if (vtable == ei::GameAddress(CSaveLoadWindow::kVtableAddress) || vtable == ei::GameAddress(CSaveLoadWindow::kInputVtableAddress))
        return !reinterpret_cast<CSaveLoadWindow*>(top)->sliding_;
    if (vtable == ei::GameAddress(COptionsWindow::kVtableAddress) || vtable == ei::GameAddress(COptionsWindow::kInputVtableAddress))
        return !reinterpret_cast<COptionsWindow*>(top)->sliding_;
    return true;
}

// For failure messages: the window on top and what keeps it from counting.
std::string DescribeTopWindow() {
    auto* ui = ei::GamePointer<CInterface3D>(0x007C7D50);
    CUiWindow* top = ui->GetActiveWindow();
    std::uintptr_t vtable = top ? *reinterpret_cast<const std::uintptr_t*>(top) : 0;
    const char* name = "?";
    for (const KnownWindow& w : kKnownWindows)
        if (vtable == ei::GameAddress(w.vtables[0]) || vtable == ei::GameAddress(w.vtables[1])) name = w.name;
    char text[200];
    std::snprintf(text, sizeof(text), "top window %p vtable %08X (%s), queued %p, request %d, campaign mode %d", top,
                  unsigned(ei::OriginalAddress(vtable)), top ? name : "none", ui->queuedWindow_, g_request.kind,
                  ei::GameData<int>(kCampaignMode));
    return text;
}

// The steps that post input messages to the game window.
bool IsInputStep(const std::string& verb) {
    return verb == "key" || verb == "click" || verb == "rclick" || verb == "move" || verb == "wheel" ||
           verb == "dblclick" || verb == "rdblclick" || verb == "ldown" || verb == "lup" || verb == "rdown" || verb == "rup" ||
           verb == "clickregion" || verb == "rclickregion" || verb == "dblclickregion" || verb == "moveregion" ||
           verb == "clickunit" ||
           verb == "clickground" || verb == "lookat" || verb == "hold" || verb == "release" || verb == "focus" || verb == "windowsize";
}

// "expectcrash <text>": the crash the scenario is about (its failure line contains <text>).
std::string g_expectedCrash;
// The crash's failure line gives the free space for a C++ throw (a write failing on a full disk throws
// a CFileException) or when it is below kLowDiskMb.
constexpr long long kLowDiskMb = 1024;
constexpr DWORD kMsvcThrow = 0xE06D7363;

// Unhandled exceptions end the run with a failure. Installed at start and re-asserted every step:
// the game (and Rebirth) install their own filters, which can leave a crashed game hanging. The
// failure names a C++ throw's type and contents and, when it is low, the run disk's free space
// (harness_crash.h); ei_hook.log gets the code addresses on the stack.
LONG WINAPI HarnessUnhandled(EXCEPTION_POINTERS* e) {
    char head[64];
    std::snprintf(head, sizeof(head), "the game crashed: exception %08lX at %08lX", e->ExceptionRecord->ExceptionCode,
                  static_cast<unsigned long>(e->ContextRecord->Eip));
    std::string what = head;
    if (std::string thrown = crash::DescribeThrow(e->ExceptionRecord); !thrown.empty()) what += ": " + thrown;
    const long long freeMb = crash::FreeMegabytes(g_savesDir);
    if (freeMb >= 0 && (freeMb < kLowDiskMb || e->ExceptionRecord->ExceptionCode == kMsvcThrow))
        what += "; " + std::to_string(freeMb) + " MB free on the run folder's disk";
    what += " (stack in ei_hook.log)";
    Log("harness: %s", what.c_str());
    Log("harness: %lld MB free on the run folder's disk", freeMb);
    LogModuleOf(e->ContextRecord->Eip);
    const auto* stack = reinterpret_cast<const std::uintptr_t*>(e->ContextRecord->Esp);
    MEMORY_BASIC_INFORMATION mbi{};
    VirtualQuery(stack, &mbi, sizeof(mbi));
    const auto* stackEnd = reinterpret_cast<const std::uintptr_t*>(static_cast<char*>(mbi.BaseAddress) + mbi.RegionSize);
    for (int i = 0; i < 1024 && stack + i < stackEnd; ++i) LogCodeAddress("stack", stack[i]);
    if (!g_expectedCrash.empty() && what.find(g_expectedCrash) != std::string::npos) {
        Log("harness: the crash the scenario expects (expectcrash %s)", g_expectedCrash.c_str());
        g_expectedCrash.clear();
    } else {
        g_failures.push_back(what);
    }
    if (HANDLE writer = CreateThread(nullptr, 0, [](void*) -> DWORD { WriteReport(); return 0; }, nullptr, 0, nullptr)) {
        WaitForSingleObject(writer, 3000);
        CloseHandle(writer);
    }
    TerminateProcess(GetCurrentProcess(), 3);
    return EXCEPTION_EXECUTE_HANDLER;
}

// The standalone image reached game code with no rewrite (src/hook/standalone.cpp, which ends the process
// after this): the run fails naming the address.
void EndRunOnMissingCode(const char* what) {
    g_failures.push_back(std::string("standalone: ") + what + " (ei_hook.log)");
    if (HANDLE writer = CreateThread(nullptr, 0, [](void*) -> DWORD { WriteReport(); return 0; }, nullptr, 0, nullptr)) {
        WaitForSingleObject(writer, 3000);
        CloseHandle(writer);
    }
}

// A message box in a harness run is a failure nobody is there to close: the CRT's abort() ("abnormal
// program termination", from game.exe's own CRT or Rebirth's), a pure virtual call, the game's error
// box. user32's MessageBoxA / MessageBoxW are hot-patched (the CRT finds MessageBoxA through
// GetProcAddress, so an import slot would miss it): the text and the stack go to ei_hook.log and
// the run ends at once. During the run that is a failure; after "quit" the report is already
// written, so an "EXIT:" line is added to it (run_all.py lists those runs).
void EndRunOnMessageBox(const std::string& caption, const std::string& text) {
    std::string what = "message box '" + caption + "': " + text;
    for (char& ch : what) if (ch == '\r' || ch == '\n') ch = ' ';
    Log("harness: %s", what.c_str());
    LogModuleOf(reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
    const auto* stack = reinterpret_cast<const std::uintptr_t*>(_AddressOfReturnAddress());
    MEMORY_BASIC_INFORMATION mbi{};
    VirtualQuery(stack, &mbi, sizeof(mbi));
    const auto* stackEnd = reinterpret_cast<const std::uintptr_t*>(static_cast<char*>(mbi.BaseAddress) + mbi.RegionSize);
    for (int i = 0; i < 256 && stack + i < stackEnd; ++i) LogCodeAddress("stack", stack[i]);
    if (g_quitting) {
        if (FILE* f = nullptr; fopen_s(&f, g_reportPath, "a") == 0 && f) {
            std::fprintf(f, "EXIT: %s (stack in ei_hook.log)\n", what.c_str());
            std::fclose(f);
        }
        TerminateProcess(GetCurrentProcess(), 0);
    }
    g_failures.push_back(what + " (stack in ei_hook.log)");
    WriteReport();
    TerminateProcess(GetCurrentProcess(), 4);
}

int WINAPI HarnessMessageBoxA(HWND, LPCSTR text, LPCSTR caption, UINT) {
    EndRunOnMessageBox(caption ? caption : "", text ? text : "");
    return IDABORT;
}

std::string Narrow(LPCWSTR s) {
    std::string out;
    for (; s && *s; ++s) out += *s < 0x80 ? static_cast<char>(*s) : '?';
    return out;
}

int WINAPI HarnessMessageBoxW(HWND, LPCWSTR text, LPCWSTR caption, UINT) {
    EndRunOnMessageBox(Narrow(caption), Narrow(text));
    return IDABORT;
}

// Hot patch: user32's exports start with "mov edi, edi" after five bytes of padding. The padding
// becomes a JMP to the replacement and the first two bytes a short jump back to it. Returns the
// original function (its code after the "mov edi, edi"), nullptr when the prologue is not that.
void* HotPatch(const char* name, void* replacement) {
    auto* p = reinterpret_cast<unsigned char*>(GetProcAddress(GetModuleHandleA("user32.dll"), name));
    if (p && p[0] == 0xFF && p[1] == 0x25) {
        // Some exports are only "jmp dword ptr [slot]" into win32u: the slot is swapped instead.
        auto** slot = *reinterpret_cast<void***>(p + 2);
        DWORD old;
        VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old);
        void* original = *slot;
        *slot = replacement;
        VirtualProtect(slot, sizeof(*slot), old, &old);
        return original;
    }
    if (!p || p[0] != 0x8B || p[1] != 0xFF) {
        Log("harness: cannot hot-patch %s (unexpected prologue)", name);
        return nullptr;
    }
    DWORD old;
    VirtualProtect(p - 5, 7, PAGE_EXECUTE_READWRITE, &old);
    p[-5] = 0xE9;
    *reinterpret_cast<std::int32_t*>(p - 4) = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(replacement) -
                                                                        reinterpret_cast<std::uintptr_t>(p));
    p[0] = 0xEB;
    p[1] = 0xF9;
    VirtualProtect(p - 5, 7, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p - 5, 7);
    return p + 2;
}

// No window of a harness game takes the foreground (SV1c). The game's own calls are kept from it
// by its import table (NoForeground, ShowWithoutActivating, CreateWithoutActivating above), but
// dgVoodoo's ddraw.dll, loaded into the process, activates the game window at SetCooperativeLevel
// with its own calls, and a game that had the foreground and lost it to a starting game is
// minimised by dgVoodoo (RestoreAfterMinimise): the device restores that follow draw random numbers
// and shifted a golden. So the user32 functions themselves are patched, for every caller in the
// process: the activating calls do nothing, and windows are made and shown without being activated.
// A window that cannot be activated is not given the foreground when the one in front closes.
namespace noforeground {
BOOL WINAPI SetForeground(HWND) { return TRUE; }
BOOL WINAPI BringToTop(HWND) { return TRUE; }
HWND WINAPI SetActive(HWND window) { return window; }
HWND WINAPI SetFocusTo(HWND window) { return window; }
void WINAPI SwitchTo(HWND, BOOL) {}

using ShowWindowFn = decltype(&::ShowWindow);
using SetWindowPosFn = decltype(&::SetWindowPos);
using SetWindowLongFn = LONG(WINAPI*)(HWND, int, LONG);
using CreateWindowExAFn = decltype(&::CreateWindowExA);
using CreateWindowExWFn = decltype(&::CreateWindowExW);
ShowWindowFn g_show;
SetWindowPosFn g_setWindowPos;
SetWindowLongFn g_setWindowLongA, g_setWindowLongW;
CreateWindowExAFn g_createA;
CreateWindowExWFn g_createW;

void Conceal(HWND window);
void NoteWindowThread(DWORD thread);
bool TopLevel(DWORD style, HWND parent);

BOOL WINAPI Show(HWND window, int command) {
    switch (command) {
    case SW_SHOWNORMAL: case SW_SHOW: case SW_RESTORE: case SW_SHOWDEFAULT: command = SW_SHOWNOACTIVATE; break;
    case SW_SHOWMAXIMIZED: command = SW_SHOWNA; break;
    case SW_MINIMIZE: case SW_SHOWMINIMIZED: return TRUE;   // a harness game is never minimised, except by its focus step
    default: break;
    }
    if (command != SW_HIDE) Conceal(window);   // also a window not made through CreateWindowEx
    if (window && TopLevel(static_cast<DWORD>(GetWindowLongA(window, GWL_STYLE)), GetParent(window)))
        NoteWindowThread(GetWindowThreadProcessId(window, nullptr));
    return g_show(window, command);
}
BOOL WINAPI SetPos(HWND window, HWND after, int x, int y, int w, int h, UINT flags) {
    return g_setWindowPos(window, after, x, y, w, h, flags | SWP_NOACTIVATE);
}
bool TopLevel(DWORD style, HWND parent) { return !parent && !(style & WS_CHILD); }

// WB12, WB12a: in every harness run (the image's and the original's) the game's windows are out of
// sight as well. Each top-level window is cloaked (DWMWA_CLOAK: Windows still composes it, so its swap chains present as before,
// but nothing of it reaches the screen) and made a tool window (no taskbar button, not in Alt+Tab).
// Its position, size, visibility and messages stay as they were, so the game, the harness's posted
// input and the frames (read from the renderer's target, harness_capture.cpp) do not change.
// The original (game.exe on dgVoodoo, windowed) presents through dgVoodoo's swap chain into its cloaked
// window the same way, and its frames are read from the emulated render target, not the window (WB12a).
// EI_HARNESS_SHOW_WINDOW=1 leaves the windows in sight (to watch a run).
bool g_conceal = false;
decltype(&::DwmSetWindowAttribute) g_dwmSet;
decltype(&::DwmGetWindowAttribute) g_dwmGet;
DWORD ConcealedExStyle(DWORD exStyle) {
    exStyle |= WS_EX_NOACTIVATE;
    if (g_conceal) exStyle = (exStyle | WS_EX_TOOLWINDOW) & ~DWORD(WS_EX_APPWINDOW);
    return exStyle;
}
// (Message-only windows, Miles' and DirectSound's among them, are never seen and cannot be cloaked:
// their parent is HWND_MESSAGE, which GetParent does not report.)
void Conceal(HWND window) {
    if (!g_conceal || !window || GetAncestor(window, GA_PARENT) != GetDesktopWindow() ||
        !TopLevel(static_cast<DWORD>(GetWindowLongA(window, GWL_STYLE)), nullptr))
        return;
    DWORD cloaked = 0;
    if (g_dwmGet && SUCCEEDED(g_dwmGet(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) return;
    BOOL cloak = TRUE;
    const HRESULT result = g_dwmSet ? g_dwmSet(window, DWMWA_CLOAK, &cloak, sizeof(cloak)) : E_NOTIMPL;
    char cls[64] = "";
    GetClassNameA(window, cls, sizeof(cls));
    if (FAILED(result))
        Log("harness: window %p (class %s) not cloaked (%08X): it may be seen", static_cast<void*>(window), cls, unsigned(result));
    else
        Log("harness: window %p (class %s) cloaked", static_cast<void*>(window), cls);
}
LONG KeepNoActivate(HWND window, int index, LONG value) {
    if (index == GWL_EXSTYLE && TopLevel(static_cast<DWORD>(GetWindowLongA(window, GWL_STYLE)), GetParent(window)))
        value = static_cast<LONG>(ConcealedExStyle(static_cast<DWORD>(value)));
    return value;
}
LONG WINAPI SetLongA(HWND window, int index, LONG value) {
    return g_setWindowLongA(window, index, KeepNoActivate(window, index, value));
}
LONG WINAPI SetLongW(HWND window, int index, LONG value) {
    return g_setWindowLongW(window, index, KeepNoActivate(window, index, value));
}
HWND WINAPI CreateA(DWORD exStyle, LPCSTR cls, LPCSTR name, DWORD style, int x, int y, int w, int h, HWND parent,
                    HMENU menu, HINSTANCE instance, LPVOID param) {
    if (TopLevel(style, parent)) exStyle = ConcealedExStyle(exStyle);
    HWND window = g_createA(exStyle, cls, name, style, x, y, w, h, parent, menu, instance, param);
    Conceal(window);
    if (window && TopLevel(style, parent)) NoteWindowThread(GetCurrentThreadId());
    return window;
}
HWND WINAPI CreateW(DWORD exStyle, LPCWSTR cls, LPCWSTR name, DWORD style, int x, int y, int w, int h, HWND parent,
                    HMENU menu, HINSTANCE instance, LPVOID param) {
    if (TopLevel(style, parent)) exStyle = ConcealedExStyle(exStyle);
    HWND window = g_createW(exStyle, cls, name, style, x, y, w, h, parent, menu, instance, param);
    Conceal(window);
    if (window && TopLevel(style, parent)) NoteWindowThread(GetCurrentThreadId());
    return window;
}

template <typename T>
void Patch(const char* name, T replacement, T* original = nullptr) {
    void* trampoline = HotPatch(name, reinterpret_cast<void*>(replacement));
    if (original) *original = reinterpret_cast<T>(trampoline);
}

void Install() {
    char show[8] = "";
    g_conceal = !(GetEnvironmentVariableA("EI_HARNESS_SHOW_WINDOW", show, sizeof(show)) && show[0] == '1');
    if (HMODULE dwm = LoadLibraryA("dwmapi.dll")) {
        g_dwmSet = reinterpret_cast<decltype(&::DwmSetWindowAttribute)>(GetProcAddress(dwm, "DwmSetWindowAttribute"));
        g_dwmGet = reinterpret_cast<decltype(&::DwmGetWindowAttribute)>(GetProcAddress(dwm, "DwmGetWindowAttribute"));
    }
    if (g_conceal)
        Log("harness: the game's windows are kept out of sight (cloaked tool windows; EI_HARNESS_SHOW_WINDOW=1 shows them)");
    Patch("SetForegroundWindow", &SetForeground);
    Patch("BringWindowToTop", &BringToTop);
    Patch("SetActiveWindow", &SetActive);
    Patch("SetFocus", &SetFocusTo);
    Patch("SwitchToThisWindow", &SwitchTo);
    Patch("ShowWindow", &Show, &g_show);
    Patch("SetWindowPos", &SetPos, &g_setWindowPos);
    Patch("SetWindowLongA", &SetLongA, &g_setWindowLongA);
    Patch("SetWindowLongW", &SetLongW, &g_setWindowLongW);
    Patch("CreateWindowExA", &CreateA, &g_createA);
    Patch("CreateWindowExW", &CreateW, &g_createW);
}

// The windows' cloak states as DWM last reported them. The query is a call into DWM, which waits
// while DWM is busy (SV10: in net_pair_quest some 6 s of the main thread's 150 s, five queries a
// second): it is made on the sweeps (Watch) and for a window not asked about yet.
std::vector<std::pair<HWND, DWORD>> g_cloakStates;
DWORD CloakState(HWND window, bool ask) {
    auto it = std::find_if(g_cloakStates.begin(), g_cloakStates.end(), [&](const auto& s) { return s.first == window; });
    if (it != g_cloakStates.end() && !ask) return it->second;
    DWORD cloaked = 0;
    if (g_dwmGet) g_dwmGet(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    if (it != g_cloakStates.end()) it->second = cloaked;
    else g_cloakStates.emplace_back(window, cloaked);
    return cloaked;
}

// What of a top-level window of the game the user could see, empty when nothing: shown (visible,
// not cloaked or minimised, on a monitor), a taskbar button, the foreground window. `ask`: DWM is
// asked for the cloak state, else its last answer for the window counts.
std::string Seen(HWND window, bool ask) {
    std::string seen;
    if (!IsWindowVisible(window)) return seen;
    const DWORD exStyle = static_cast<DWORD>(GetWindowLongA(window, GWL_EXSTYLE));
    const DWORD cloaked = CloakState(window, ask);
    RECT rect{}, both{};
    const RECT screen{GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
                      GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
                      GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN)};
    const bool onScreen = GetWindowRect(window, &rect) && IntersectRect(&both, &rect, &screen);
    if (!cloaked && !IsIconic(window) && onScreen) seen += " shown";
    if ((exStyle & WS_EX_APPWINDOW) || (!(exStyle & WS_EX_TOOLWINDOW) && !GetWindow(window, GW_OWNER)))
        seen += " on the taskbar";
    if (GetForegroundWindow() == window) seen += " in front";
    if (seen.empty()) return seen;
    char text[200];
    char cls[64] = "";
    GetClassNameA(window, cls, sizeof(cls));
    std::snprintf(text, sizeof(text), "window %p (class %s, (%ld,%ld)-(%ld,%ld), ex style %08X, cloaked %lu):",
                  static_cast<void*>(window), cls, rect.left, rect.top, rect.right, rect.bottom, unsigned(exStyle),
                  static_cast<unsigned long>(cloaked));
    return text + seen;
}

unsigned g_checks;          // checks of the game's windows so far
std::string g_firstSeen;    // the first thing seen, for the report
std::string g_lastSeen;     // logged when it changes

// The threads that own top-level windows of the game: each that made or showed one (CreateA, CreateW,
// Show) and each a sweep of the desktop found one of. (A thread id can be reused by another process's
// thread once its thread ends: the windows' process is checked too.)
constexpr int kMaxWindowThreads = 64;
SRWLOCK g_windowThreadsLock = SRWLOCK_INIT;
DWORD g_windowThreads[kMaxWindowThreads];
int g_windowThreadCount = 0;
void NoteWindowThread(DWORD thread) {
    AcquireSRWLockExclusive(&g_windowThreadsLock);
    bool known = false;
    for (int i = 0; i < g_windowThreadCount && !known; ++i) known = g_windowThreads[i] == thread;
    if (!known && g_windowThreadCount < kMaxWindowThreads) g_windowThreads[g_windowThreadCount++] = thread;
    ReleaseSRWLockExclusive(&g_windowThreadsLock);
}

// One check: the first thing seen, and whether it is a sweep (DWM asked again).
struct Check {
    std::string seen;
    bool sweep = false;
};

BOOL CALLBACK CheckWindow(HWND window, LPARAM parameter) {
    DWORD process = 0;
    const DWORD thread = GetWindowThreadProcessId(window, &process);
    if (process != GetCurrentProcessId()) return TRUE;
    NoteWindowThread(thread);
    Check& check = *reinterpret_cast<Check*>(parameter);
    if (check.seen.empty()) check.seen = Seen(window, check.sweep);
    return TRUE;
}

// Every top-level window of the process checked (at most every 200 ms of real time, from the main
// loop's peeks; logged when what is seen changes). Without concealment only the foreground matters,
// as before WB12: a harness game never takes it (SV1c). A check walks the windows of the threads
// that own the game's windows with the cloak states last reported; every tenth (every 2 s) is a sweep:
// every top-level window of the desktop (for a window of a thread not known yet) and DWM asked again
// (SV10: a sweep at every check cost 0.4-2 ms of wall time a frame in the original's runs and some 6%
// of net_pair_quest's main thread in the image's, mostly waiting for DWM's answers).
constexpr unsigned kSweepEvery = 10;
void Watch(HWND game) {
    static ULONGLONG next = 0;
    const ULONGLONG now = GetTickCount64();
    if (now < next) return;
    next = now + 200;
    Check check;
    check.sweep = g_checks % kSweepEvery == 0;
    if (g_conceal && check.sweep) {
        EnumWindows(&CheckWindow, reinterpret_cast<LPARAM>(&check));
    } else if (g_conceal) {
        DWORD threads[kMaxWindowThreads];
        AcquireSRWLockShared(&g_windowThreadsLock);
        const int count = g_windowThreadCount;
        std::copy(g_windowThreads, g_windowThreads + count, threads);
        ReleaseSRWLockShared(&g_windowThreadsLock);
        for (int i = 0; i < count; ++i) EnumThreadWindows(threads[i], &CheckWindow, reinterpret_cast<LPARAM>(&check));
    } else if (GetForegroundWindow() == game) {
        check.seen = "the game window is the foreground window";
    }
    const std::string seen = check.seen;
    ++g_checks;
    if (seen == g_lastSeen) return;
    g_lastSeen = seen;
    if (seen.empty()) {
        Log("harness: no window of the game is in sight any more");
        return;
    }
    if (g_firstSeen.empty()) g_firstSeen = seen;
    Log("harness: in sight: %s", seen.c_str());
}

// The report's line (concealed runs only; run_all lists the runs that had a window in sight).
void Report(FILE* f) {
    if (!g_conceal) return;
    if (g_firstSeen.empty())
        std::fprintf(f, "\nWINDOW: out of sight (%u checks: no window of the game shown, on the taskbar or in front)\n",
                     g_checks);
    else
        std::fprintf(f, "\nWINDOW: in sight: %s\n", g_firstSeen.c_str());
}
}  // namespace noforeground

std::string FocusStep(const std::string& argument);
std::string WindowSizeStep(const std::string& argument);

void RunSteps() {
    SetUnhandledExceptionFilter(&HarnessUnhandled);
    g_showTutorial = 0;
    if (g_heldTickLength) g_tickLength = g_heldTickLength;
    if (g_checking) {
        // The script prints its value on a later frame; take the first new line.
        if (g_console.size() > g_checkFrom) {
            const std::string& out = g_console[g_checkFrom];
            char* end = nullptr;
            double value = std::strtod(out.c_str(), &end);
            bool ok = end != out.c_str() && value != 0;
            Log("harness: check %s -> %s (%s)", g_checkExpr.c_str(), out.c_str(), ok ? "ok" : "FAILED");
            if (!ok) g_failures.push_back("check " + g_checkExpr + " printed '" + out + "'");
            g_checking = false;
        } else if (g_checkDeadline.Passed()) {
            Log("harness: check %s printed nothing%s", g_checkExpr.c_str(), g_checkDeadline.Why().c_str());
            g_failures.push_back("check " + g_checkExpr + " printed nothing" + g_checkDeadline.Why());
            g_checking = false;
        } else {
            return;
        }
    }
    if (g_waitingFor) {
        DWORD now = Now();
        if (g_waitForPending && g_console.size() > g_waitForFrom) {
            const std::string& out = g_console[g_waitForFrom];
            char* end = nullptr;
            double value = std::strtod(out.c_str(), &end);
            g_waitForPending = false;
            if (end != out.c_str() && value != 0) {
                Log("harness: waitfor %s done after %lu ms", g_waitForExpr.c_str(), now - g_waitForDeadline.start);
                g_waitingFor = false;
            } else {
                g_waitForNext = now + 250;
            }
        }
        if (g_waitingFor && g_waitForDeadline.Passed()) {
            Log("harness: waitfor %s timed out%s", g_waitForExpr.c_str(), g_waitForDeadline.Why().c_str());
            g_failures.push_back("waitfor " + g_waitForExpr + " timed out" + g_waitForDeadline.Why());
            g_waitingFor = false;
        }
        if (g_waitingFor) {
            if (!g_waitForPending && now >= g_waitForNext) {
                g_waitForFrom = g_console.size();
                g_waitForPending = true;
                ConsoleExecute(("#" + g_waitForExpr).c_str());
            }
            return;
        }
    }
    if (g_capturing) {
        const char* detail = "";
        capture::State state = capture::Poll(&detail);
        if (state == capture::State::Pending && GetTickCount() < g_captureDeadline) return;
        if (state == capture::State::Done) {
            Log("harness: capture %s: %s", g_captureName.c_str(), detail);
            g_capturedFrames.push_back(g_captureName + ": " + detail);
        } else {
            std::string why = state == capture::State::Pending ? "no frame presented within 10 s" : detail;
            Log("harness: capture %s failed: %s", g_captureName.c_str(), why.c_str());
            g_failures.push_back("capture " + g_captureName + ": " + why);
        }
        g_capturing = false;
    }
    if (g_waitingLine) {
        bool found = false;
        for (std::size_t i = g_waitLineFrom; i < g_console.size() && !found; ++i)
            found = g_console[i].find(g_waitLineText) != std::string::npos;
        const char* verb = g_waitLineIsExpect ? "waitexpect" : "waitline";
        if (found) {
            Log("harness: %s '%s' found", verb, g_waitLineText.c_str());
            g_waitingLine = false;
            if (g_waitLineIsExpect) g_expectFrom = g_console.size();
        } else if (g_waitLineDeadline.Passed()) {
            Log("harness: %s '%s' timed out%s", verb, g_waitLineText.c_str(), g_waitLineDeadline.Why().c_str());
            g_failures.push_back((g_waitLineIsExpect ? "expected console output '" + g_waitLineText + "' (waited)"
                                                     : "waitline " + g_waitLineText + " timed out") +
                                 g_waitLineDeadline.Why());
            g_waitingLine = false;
            if (g_waitLineIsExpect) g_expectFrom = g_console.size();
        } else {
            return;
        }
    }
    if (g_waitingZone) {
        // Done in a main loop pass only (steps also run in the pump that window switches call while
        // the zone loads): no request pending, not the main menu, the server world holds the zone
        // and the local player is back in the world (state 1; LeaveToZone left it at 3).
        bool ready = false;
        if (g_inputLoop && g_request.kind == 0 && ei::GameData<int>(kCampaignMode) != 2 &&
            _stricmp(ServerZoneName(), g_waitZoneName.c_str()) == 0) {
            void* connection = LocalConnection();
            ready = connection && ConnectionState(connection) == 1;
        }
        if (ready) {
            Log("harness: waitzone %s done after %lu ms (campaign mode %d)", g_waitZoneName.c_str(),
                Now() - g_waitZoneDeadline.start, ei::GameData<int>(kCampaignMode));
            g_waitingZone = false;
        } else if (g_waitZoneDeadline.Passed()) {
            char what[500];
            std::snprintf(what, sizeof(what), "waitzone %s timed out (zone '%s', request %d, campaign mode %d, %s)%s",
                          g_waitZoneName.c_str(), ServerZoneName(), g_request.kind, ei::GameData<int>(kCampaignMode),
                          DescribeLocalPlayer().c_str(), g_waitZoneDeadline.Why().c_str());
            Log("harness: %s", what);
            g_failures.push_back(what);
            g_waitingZone = false;
        } else {
            return;
        }
    }
    if (g_waitingFrames) {
        if (g_frames < g_waitFramesUntil && static_cast<LONG>(GetTickCount() - g_waitFramesDeadline.realAt) < 0) return;
        if (g_frames < g_waitFramesUntil) {
            std::string what = "frames: " + std::to_string(g_waitFramesUntil - g_frames) + " frames still to go after " +
                   std::to_string(GetTickCount() - g_waitFramesDeadline.realStart) + " ms";
            Log("harness: %s", what.c_str());
            g_failures.push_back(what);
        }
        g_waitingFrames = false;
    }
    if (g_waitingPresent) {
        render::WindowPresentInfo now = render::WindowPresents();
        bool presented = now.presents > g_waitPresentFrom;
        bool checked = !g_waitPresentGamma ||
                       (now.rampChecks > g_rampChecksSeen && !now.rampCheckPending && !now.rampIdentity);
        if (presented && checked) {
            Log("harness: waitpresent: %u frames presented on the game window%s", now.presents - g_waitPresentFrom,
                g_waitPresentGamma ? " and the gamma ramp checked" : "");
            if (g_waitPresentGamma) {
                g_rampChecksSeen = now.rampChecks;
                if (now.rampDiffering) {
                    char what[160];
                    std::snprintf(what, sizeof(what), "waitpresent: %d of %d points of the presented frame differ from the gamma ramp",
                                  now.rampDiffering, now.rampPoints);
                    g_failures.push_back(what);
                }
            }
            g_waitingPresent = false;
        } else if (now.failed || g_waitPresentDeadline.Passed()) {
            char what[200];
            std::snprintf(what, sizeof(what), "waitpresent: %s (%u presented, %d ramp checks, one pending %d, identity %d)",
                          now.failed ? "the game window's swap chain failed" : "timed out", now.presents - g_waitPresentFrom,
                          now.rampChecks, now.rampCheckPending, now.rampIdentity);
            Log("harness: %s", what);
            g_failures.push_back(what);
            g_waitingPresent = false;
        } else {
            return;
        }
    }
    if (g_waitingNewDevice) {
        render::WindowPresentInfo now = render::WindowPresents();
        // Made again, and (presenting on the game window) a frame presented on the new device.
        bool presented = !render::PresentsItself() || now.presents > now.presentsAtRecovery;
        const char* verb = g_newDeviceRemoved ? "removedevice" : "losedevice";
        if (now.deviceRecoveries > g_newDeviceFrom && presented && !now.failed) {
            Log("harness: %s: a new Direct3D 11 device (%d lost, %d made again; the loss: %08lX, removed reason %08lX%s)%s",
                verb, now.deviceLosses, now.deviceRecoveries, static_cast<unsigned long>(now.lastLossResult),
                static_cast<unsigned long>(now.lastLossReason), now.lastLossSimulated ? ", simulated" : "",
                render::PresentsItself() ? " presents on the game window" : "");
            // A real removal: reported by the device itself (not LoseDevice's stand-in), and the new
            // device removable again.
            if (g_newDeviceRemoved &&
                (now.lastLossSimulated || now.lastLossReason != DXGI_ERROR_DEVICE_REMOVED || !now.removable)) {
                char what[200];
                std::snprintf(what, sizeof(what), "removedevice: not a real removal (reason %08lX, simulated %d) or the new device "
                              "is not removable (%d)", static_cast<unsigned long>(now.lastLossReason), now.lastLossSimulated,
                              now.removable);
                Log("harness: %s", what);
                g_failures.push_back(what);
            }
            g_waitingNewDevice = false;
        } else if (now.failed || !render::Active() || Now() >= g_newDeviceDeadline) {
            char what[200];
            std::snprintf(what, sizeof(what), "%s: %s (%d lost, %d made again, %u presented since)", verb,
                          now.failed || !render::Active() ? "the new renderer failed" : "timed out", now.deviceLosses,
                          now.deviceRecoveries, now.presents - now.presentsAtRecovery);
            Log("harness: %s", what);
            g_failures.push_back(what);
            g_waitingNewDevice = false;
        } else {
            return;
        }
    }
    if (g_waitingWindow) {
        // Ready on two frames in a row, and done in an input loop pass (input steps follow).
        if (!WindowReady(g_waitWindowVtables)) g_waitWindowReadyFrom = kNotReady;
        else if (g_waitWindowReadyFrom == kNotReady) g_waitWindowReadyFrom = g_frames;
        if (g_waitWindowReadyFrom != kNotReady && g_frames >= g_waitWindowReadyFrom + 2 && g_inputLoop) {
            const CScreenFade* fade = ei::GamePointer<CInterface3D>(0x007C7D50)->fade_;
            Log("harness: waitwindow %s done after %lu ms (frame %u; screen fade %s, alpha %.0f)", g_waitWindowName.c_str(),
                Now() - g_waitWindowDeadline.start, g_frames, fade && fade->active_ ? "running" : "off", fade ? fade->alpha_ : 0.0f);
            g_waitingWindow = false;
        } else if (g_waitWindowDeadline.Passed()) {
            std::string what = "waitwindow " + g_waitWindowName + " timed out (" + DescribeTopWindow() + ")" +
                               g_waitWindowDeadline.Why();
            Log("harness: %s", what.c_str());
            g_failures.push_back(what);
            g_waitingWindow = false;
        } else {
            return;
        }
    }
    if (g_waitingPeer) {
        if (pair::Signaled(g_waitPeerTag)) {
            Log("harness: peerwait %s done after %lu ms (frame %u)", g_waitPeerTag.c_str(), Now() - g_waitPeerDeadline.start,
                g_frames);
            g_waitingPeer = false;
        } else if (g_waitPeerDeadline.Passed()) {
            std::string what = "peerwait " + g_waitPeerTag + " timed out" + g_waitPeerDeadline.Why();
            Log("harness: %s", what.c_str());
            g_failures.push_back(what);
            g_waitingPeer = false;
        } else {
            return;
        }
    }
    if (g_waitingUnit) {
        // Checked in input loop passes: the pick list is the last frame's, and clickunit follows.
        POINT at{};
        void* unit = nullptr;
        std::string why = g_inputLoop ? FindUnitPoint(g_waitUnitName.c_str(), nullptr, &at, &unit) : "not checked";
        if (why.empty()) {
            Log("harness: waitunit %s done after %lu ms (at %ld,%ld)", g_waitUnitName.c_str(), Now() - g_waitUnitDeadline.start,
                at.x, at.y);
            g_waitingUnit = false;
        } else if (g_waitUnitDeadline.Passed()) {
            why += g_waitUnitDeadline.Why();
            Log("harness: waitunit %s timed out: %s", g_waitUnitName.c_str(), why.c_str());
            g_failures.push_back("waitunit " + g_waitUnitName + " timed out: " + why);
            g_waitingUnit = false;
        } else {
            return;
        }
    }
    if (g_worldWait != WorldWait::None) {
        // The server world as the last world update left it (steps run after the frame), so the wait
        // ends in the same frame every run.
        std::string what;
        bool done = false;
        if (g_worldWait == WorldWait::Idle) {
            const int leader = digest::LocalLeader();
            const digest::UnitNow unit = digest::Unit(leader);
            if (!unit.found || unit.dead || !unit.idle) g_idleFrom = kNotReady;
            else if (g_idleFrom == kNotReady) g_idleFrom = g_frames;
            done = g_idleFrom != kNotReady && g_frames >= g_idleFrom + g_idleFrames;
            what = "waitidle " + std::to_string(g_idleFrames) + " (leader " + std::to_string(leader) +
                   (unit.found ? unit.dead ? ": dead)" : unit.idle ? ": idle)" : ": busy)" : ": not in the world)");
        } else if (g_worldWait == WorldWait::Dead) {
            std::string alive;
            for (int id : g_deadIds) {
                const digest::UnitNow unit = digest::Unit(id);
                if (unit.found && !unit.dead) alive += " " + std::to_string(id);
            }
            done = alive.empty();
            what = "waitdead (alive:" + alive + ")";
        } else {
            unsigned money = 0;
            const digest::UnitNow unit = digest::Unit(digest::LocalLeader(&money));
            done = unit.found && (unit.experience != g_awardExperience || money != g_awardMoney);
            char text[160];
            std::snprintf(text, sizeof(text), "waitaward (experience %.1f -> %.1f, money %u -> %u)", g_awardExperience,
                          unit.experience, g_awardMoney, money);
            what = text;
        }
        if (done) {
            Log("harness: %s done after %lu ms (frame %u)", what.c_str(), Now() - g_worldWaitDeadline.start, g_frames);
            g_worldWait = WorldWait::None;
        } else if (g_worldWaitDeadline.Passed()) {
            what += " timed out" + g_worldWaitDeadline.Why();
            Log("harness: %s", what.c_str());
            g_failures.push_back(what);
            g_worldWait = WorldWait::None;
        } else {
            return;
        }
    }
    while (g_next < g_steps.size()) {
        if (!g_waitUntil.Passed()) return;
        if (std::string why = g_waitUntil.Why(); !why.empty()) {
            g_failures.push_back("wait" + why);
            g_waitUntil = Deadline{};
        }
        Step s = g_steps[g_next++];
        // "vanilla <step>" / "rebirth <step>": only in that install (Rebirth changes some behaviour
        // itself, e.g. script error messages). Rebirth is recognised by its addon.dll, or the standalone image's flags.
        if (s.verb == "vanilla" || s.verb == "rebirth") {
            bool rebirth = starter::RebirthInstall();
            if ((s.verb == "rebirth") != rebirth) {
                Log("harness: skipped (%s only): %s", s.verb.c_str(), s.argument.c_str());
                continue;
            }
            std::size_t space = s.argument.find(' ');
            s.verb = s.argument.substr(0, space);
            s.argument = space == std::string::npos ? "" : s.argument.substr(space + 1);
        }
        if (!g_inputLoop && IsInputStep(s.verb)) {
            --g_next;   // posted from the next main loop pass instead (see PeekMessageHook)
            return;
        }
        // "quit" posts WM_QUIT (PostQuitMessage) on this thread: only WinMain's loop ends the game on
        // it. The interface's message drain (00458220, run when a window is activated, e.g. as the
        // battle screen comes back after a load) dispatches every message and drops WM_QUIT, which
        // left magic_traps running after its quit (ticket X23).
        if (!g_winMainLoop && s.verb == "quit") {
            --g_next;
            return;
        }
        Log("harness: %s %s", s.verb.c_str(), s.argument.c_str());
        clock::RandomNote("step %s %s", s.verb.c_str(), s.argument.c_str());
        if (s.verb == "wait") {
            g_waitUntil.Set(std::strtoul(s.argument.c_str(), nullptr, 10));
        } else if (s.verb == "frames") {
            g_waitingFrames = true;
            g_waitFramesUntil = g_frames + std::strtoul(s.argument.c_str(), nullptr, 10);
            g_waitFramesDeadline.Set(0);
            g_waitFramesDeadline.realAt = g_waitFramesDeadline.realStart + 10000 + 100 * (g_waitFramesUntil - g_frames);
            return;
        } else if (s.verb == "waitwindow" || s.verb == "waitmenu") {
            unsigned long seconds = 30;
            std::string name = "menu";
            if (s.verb == "waitmenu") {
                if (!s.argument.empty()) seconds = std::strtoul(s.argument.c_str(), nullptr, 10);
            } else {
                char* rest = nullptr;
                seconds = std::strtoul(s.argument.c_str(), &rest, 10);
                while (rest && *rest == ' ') ++rest;
                name = rest ? rest : "";
            }
            if (!WindowVtables(name, g_waitWindowVtables)) {
                g_failures.push_back("waitwindow: unknown window '" + name + "'");
                continue;
            }
            g_waitingWindow = true;
            g_waitWindowName = name;
            g_waitWindowReadyFrom = kNotReady;
            g_waitWindowDeadline.Set(seconds * 1000);
            return;
        } else if (s.verb == "waitunit") {
            char* rest = nullptr;
            unsigned long seconds = std::strtoul(s.argument.c_str(), &rest, 10);
            while (rest && *rest == ' ') ++rest;
            g_waitingUnit = true;
            g_waitUnitName = rest ? rest : "";
            g_waitUnitDeadline.Set(seconds * 1000);
            return;
        } else if (s.verb == "waitidle" || s.verb == "waitdead" || s.verb == "waitaward") {
            char* rest = nullptr;
            const unsigned long seconds = std::strtoul(s.argument.c_str(), &rest, 10);
            if (s.verb == "waitidle") {
                g_worldWait = WorldWait::Idle;
                g_idleFrames = 10;
                if (rest && *rest) g_idleFrames = std::strtoul(rest, nullptr, 10);
                g_idleFrom = kNotReady;
            } else if (s.verb == "waitdead") {
                g_worldWait = WorldWait::Dead;
                g_deadIds.clear();
                for (char* at = rest; at && *at;) {
                    char* end = nullptr;
                    const long id = std::strtol(at, &end, 10);
                    if (end == at) break;
                    g_deadIds.push_back(static_cast<int>(id));
                    at = end;
                }
            } else {
                g_worldWait = WorldWait::Award;
                if (!g_awardFromSet) AwardFrom();
                g_awardFromSet = false;
            }
            g_worldWaitDeadline.Set(seconds * 1000);
            return;
        } else if (s.verb == "awardfrom") {
            AwardFrom();
            g_awardFromSet = true;
        } else if (s.verb == "console") {
            ConsoleExecute(s.argument.c_str());
        } else if (s.verb == "newgame") {
            // What the main menu's new-game button does after the intro movie (00632940).
            ei::GameData<unsigned char>(0x00784750) = 0;
            g_request.kind = 3;
        } else if (s.verb == "load") {
            g_request.argument = s.argument.c_str();
            g_request.kind = 2;
        } else if (s.verb == "hold" || s.verb == "release") {
            std::string bad;
            unsigned mods = ParseModifiers(s.argument, &bad);
            if (!bad.empty() || !mods) {
                std::string error = s.verb + ": '" + (bad.empty() ? s.argument : bad) + "' is no modifier (ctrl, shift, alt)";
                Log("harness: %s", error.c_str());
                g_failures.push_back(error);
            } else {
                PostModifiers(mods, s.verb == "hold");
            }
        } else if (s.verb == "key") {
            HeldModifiers held(&s.argument);
            PostKey(s.argument);
        } else if (s.verb == "click" || s.verb == "rclick" || s.verb == "move" || s.verb == "dblclick" ||
                   s.verb == "rdblclick" || s.verb == "ldown" || s.verb == "lup" || s.verb == "rdown" || s.verb == "rup") {
            int x = 0, y = 0;
            HeldModifiers held(&s.argument);
            if (sscanf_s(s.argument.c_str(), "%d %d", &x, &y) == 2) PostMouse(s.verb, x, y);
            else Log("harness: %s needs <x> <y>", s.verb.c_str());
        } else if (s.verb == "wheel") {
            int x = 0, y = 0, delta = 0;
            HeldModifiers held(&s.argument);
            if (sscanf_s(s.argument.c_str(), "%d %d %d", &x, &y, &delta) == 3) PostWheel(x, y, delta);
            else Log("harness: wheel needs <x> <y> <delta>");
        } else if (s.verb == "clickregion" || s.verb == "rclickregion" || s.verb == "dblclickregion" ||
                   s.verb == "moveregion") {
            HeldModifiers held(&s.argument);
            PostRegionMouse(s.verb, std::atoi(s.argument.c_str()));
        } else if (s.verb == "hudstate" || s.verb == "campputon" || s.verb == "modfile") {
            std::string error = s.verb == "hudstate" ? HudState(s.argument)
                                : s.verb == "campputon" ? CampPutOn(s.argument) : starter::ModFileStep(s.argument);
            if (!error.empty()) {
                Log("harness: %s", error.c_str());
                g_failures.push_back(error);
            }
        } else if (s.verb == "groundview") {
            LogGroundView();
        } else if (s.verb == "minimap") {
            MinimapLayout m;
            if (sscanf_s(s.argument.c_str(), "%f %f %f %f", &m.cx, &m.cy, &m.sx, &m.sy) != 4) {
                g_failures.push_back("minimap needs <cx> <cy> <sx> <sy>");
                Log("harness: %s", g_failures.back().c_str());
            } else {
                g_minimap = m;
            }
        } else if (s.verb == "lookat") {
            std::string error = LookAt(s.argument);
            if (!error.empty()) {
                Log("harness: %s", error.c_str());
                g_failures.push_back(error);
            }
        } else if (s.verb == "clickunit" || s.verb == "clickground") {
            HeldModifiers held(&s.argument);
            std::string error = s.verb == "clickunit" ? PostUnitMouse(s.argument) : PostGroundMouse(s.argument);
            if (!error.empty()) {
                Log("harness: %s", error.c_str());
                g_failures.push_back(error);
            }
        } else if (s.verb == "screen") {
            // What the global map's buttons do (0062F950): ask the server for interface screen <id>.
            // For the camp (5) the map's button first sets "constr_current" to 0; a base dialogue's
            // "constrN" answer (006064E0) sets N instead, when the class template exists.
            int id = 0, constr = 0;
            sscanf_s(s.argument.c_str(), "%d %d", &id, &constr);
            if (id == 5 && constr > 0 &&
                !FindClassTemplate(ei::GamePointer<void>(kClassTemplates), nullptr, constr)) {
                std::string error = "screen: no class template " + std::to_string(constr);
                Log("harness: %s", error.c_str());
                g_failures.push_back(error);
            } else {
                if (id == 5) SetCurrentConstructor(static_cast<float>(constr));
                RequestScreen(id);
            }
        } else if (s.verb == "chat") {
            // The chat line (the interface's root, CChatOverlay) opened as a multiplayer client's Enter
            // opens it (005F7C60: SetOpen 005E36C0, which draws it, 005E3700), or closed.
            auto* ui = ei::GamePointer<CInterface3D>(0x007C7D50);
            if (!ui->root_) {
                g_failures.push_back("chat: the interface has no chat overlay");
            } else if (std::atoi(s.argument.c_str()) != 0) {
                ui->ChatKeyEnter();
            } else {
                ui->root_->SetOpen(false);
            }
        } else if (s.verb == "peersignal") {
            if (!pair::Active()) g_failures.push_back("peersignal: not a pair's game (a \"peer\" line starts the other)");
            pair::Signal(s.argument);
        } else if (s.verb == "peerwait") {
            char* rest = nullptr;
            unsigned long seconds = std::strtoul(s.argument.c_str(), &rest, 10);
            while (rest && *rest == ' ') ++rest;
            if (!pair::Active()) {
                g_failures.push_back("peerwait: not a pair's game (a \"peer\" line starts the other)");
                continue;
            }
            g_waitingPeer = true;
            g_waitPeerTag = rest ? rest : "";
            g_waitPeerDeadline.Set(seconds * 1000);
            return;
        } else if (s.verb == "netcut") {
            if (!pair::Active()) g_failures.push_back("netcut: not a pair's game (a \"peer\" line starts the other)");
            pair::Cut();
        } else if (s.verb == "hidefile") {
            if (g_hiddenCount >= kHiddenFiles || s.argument.empty() || s.argument.size() >= MAX_PATH) {
                g_failures.push_back("hidefile: no room for " + s.argument);
            } else {
                strcpy_s(g_hiddenFiles[g_hiddenCount], s.argument.c_str());
                for (char* p = g_hiddenFiles[g_hiddenCount]; *p; ++p)
                    if (*p == '/') *p = '\\';
                InterlockedIncrement(&g_hiddenCount);
                Log("harness: %s reads as missing until this game writes it", s.argument.c_str());
            }
        } else if (s.verb == "masterhost") {
            options::TheOptions()->masterServerName_ = s.argument.c_str();
        } else if (s.verb == "waterwaves") {
            if (s.argument == "0" || s.argument == "1") options::TheOptions()->enableWaterWaves_ = s.argument == "1";
            else g_failures.push_back("waterwaves needs 0 or 1");
        } else if (s.verb == "fakemaster") {
            const int port = std::atoi(s.argument.c_str());
            if (!pair::Active()) g_failures.push_back("fakemaster: not a pair's game (a \"peer\" line starts the other)");
            else if (port <= 0 || port > 0xFFFF) g_failures.push_back("fakemaster needs <port>");
            else pair::FakeMaster(static_cast<std::uint16_t>(port));
        } else if (s.verb == "zone") {
            // What a zone exit does (00670C20): the server's LeaveToZone for the local player.
            char name[64] = "";
            int exitIndex = 0;
            if (sscanf_s(s.argument.c_str(), "%63s %d", name, unsigned(sizeof(name)), &exitIndex) < 1) {
                g_failures.push_back("zone needs <name> [exit]");
                continue;
            }
            const auto* record = reinterpret_cast<const std::uint8_t*>(ei::GamePointer<CMapDatabase>(kMapDatabase)->FindMap(name));
            void* connection = LocalConnection();
            Log("harness: zone %s: %s", name, DescribeLocalPlayer().c_str());
            if (!record) {
                // StartZone would show a fatal error and go to the main menu.
                g_failures.push_back(std::string("zone: no zone '") + name + "' in map.txt");
            } else if (!connection || ConnectionState(connection) != 1) {
                g_failures.push_back(std::string("zone ") + name + ": the local player is not in the world (" + DescribeLocalPlayer() + ")");
            } else {
                int kind = *reinterpret_cast<const int*>(record + kZoneRecordKind);
                Log("harness: zone %s (map.txt type %s), exit %d", name, kind == 0 ? "game" : kind == 2 ? "brief" : "edge", exitIndex);
                static_cast<CPlayerServer*>(connection)->LeaveToZone(name, exitIndex);
                if (ConnectionState(connection) != 3)
                    g_failures.push_back(std::string("zone ") + name + ": LeaveToZone did not leave (the leader dead or missing?; " +
                                         DescribeLocalPlayer() + ")");
            }
        } else if (s.verb == "waitzone") {
            char* rest = nullptr;
            unsigned long seconds = std::strtoul(s.argument.c_str(), &rest, 10);
            while (rest && *rest == ' ') ++rest;
            g_waitingZone = true;
            g_waitZoneName = rest ? rest : "";
            g_waitZoneDeadline.Set(seconds * 1000);
            return;
        } else if (s.verb == "check") {
            g_checking = true;
            g_checkExpr = s.argument;
            g_checkFrom = g_console.size();
            g_checkDeadline.Set(3000);
            ConsoleExecute(("#" + s.argument).c_str());
            return;
        } else if (s.verb == "speed") {
            g_heldTickLength = std::strtoul(s.argument.c_str(), nullptr, 10);
            if (g_heldTickLength) g_tickLength = g_heldTickLength;
        } else if (s.verb == "waitline" || s.verb == "waitexpect") {
            char* rest = nullptr;
            unsigned long seconds = std::strtoul(s.argument.c_str(), &rest, 10);
            while (rest && *rest == ' ') ++rest;
            g_waitingLine = true;
            g_waitLineIsExpect = s.verb == "waitexpect";
            g_waitLineFrom = g_waitLineIsExpect ? g_expectFrom : 0;
            g_waitLineText = rest ? rest : "";
            g_waitLineDeadline.Set(seconds * 1000);
            return;
        } else if (s.verb == "waitfor") {
            char* rest = nullptr;
            unsigned long seconds = std::strtoul(s.argument.c_str(), &rest, 10);
            while (rest && *rest == ' ') ++rest;
            g_waitingFor = true;
            g_waitForPending = false;
            g_waitForExpr = rest ? rest : "";
            g_waitForDeadline.Set(seconds * 1000);
            g_waitForNext = g_waitForDeadline.start;
            return;
        } else if (s.verb == "expect") {
            bool found = false;
            for (std::size_t i = g_expectFrom; i < g_console.size() && !found; ++i)
                found = g_console[i].find(s.argument) != std::string::npos;
            Log("harness: expect '%s': %s", s.argument.c_str(), found ? "found" : "MISSING");
            if (!found) g_failures.push_back("expected console output '" + s.argument + "'");
            g_expectFrom = g_console.size();
        } else if (s.verb == "capture") {
            // The name becomes a file name: letters, digits, '_', '-' and '.' only.
            bool valid = !s.argument.empty() && s.argument.size() < 64;
            for (char c : s.argument)
                valid = valid && (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.');
            if (!valid) {
                g_failures.push_back("capture: bad frame name '" + s.argument + "'");
                continue;
            }
            // Two frames go by first, so that what changed before (a freeze, the mouse) is drawn.
            capture::Request(s.argument.c_str(), 2);
            g_capturing = true;
            g_captureName = s.argument;
            g_captureDeadline = GetTickCount() + 10000;
            return;
        } else if (s.verb == "samecapture") {
            char a[64] = "", b[64] = "";
            int tolerance = 8;
            double limit = 0.001;
            if (sscanf_s(s.argument.c_str(), "%63s %63s %d %lf", a, unsigned(sizeof(a)), b, unsigned(sizeof(b)),
                         &tolerance, &limit) < 2) {
                g_failures.push_back("samecapture needs two frame names");
                continue;
            }
            double differing = 0;
            int largest = 0;
            const char* why = "";
            if (!capture::Compare(a, b, tolerance, &differing, &largest, &why)) {
                g_failures.push_back(std::string("samecapture ") + a + " " + b + ": " + why);
                continue;
            }
            char text[160];
            std::snprintf(text, sizeof(text), "samecapture %s %s: %.3f%% of the pixels differ by more than %d (largest %d)", a,
                          b, 100.0 * differing, tolerance, largest);
            Log("harness: %s", text);
            g_capturedFrames.push_back(text);
            if (differing > limit) g_failures.push_back(text);
        } else if (s.verb == "diffcapture") {
            // Two captures that must differ in a share of their pixels from `least` to `most` (R11d).
            char a[64] = "", b[64] = "";
            double least = 0, most = 1;
            int tolerance = 8;
            if (sscanf_s(s.argument.c_str(), "%63s %63s %lf %lf %d", a, unsigned(sizeof(a)), b, unsigned(sizeof(b)), &least,
                         &most, &tolerance) < 4) {
                g_failures.push_back("diffcapture needs two frame names, a least and a most fraction");
                continue;
            }
            double differing = 0;
            int largest = 0;
            const char* why = "";
            if (!capture::Compare(a, b, tolerance, &differing, &largest, &why)) {
                g_failures.push_back(std::string("diffcapture ") + a + " " + b + ": " + why);
                continue;
            }
            char text[200];
            std::snprintf(text, sizeof(text),
                          "diffcapture %s %s: %.4f%% of the pixels differ by more than %d (largest %d; allowed %.4f%% .. %.4f%%)",
                          a, b, 100.0 * differing, tolerance, largest, 100.0 * least, 100.0 * most);
            Log("harness: %s", text);
            g_capturedFrames.push_back(text);
            if (differing < least || differing > most) g_failures.push_back(text);
        } else if (s.verb == "blink") {
            std::string error = BlinkStep(s.argument);
            if (!error.empty()) g_failures.push_back(error);
        } else if (s.verb == "terrainlevels") {
            if (!render::Active()) {
                Log("harness: terrainlevels %s: not the new renderer (the original code): ignored", s.argument.c_str());
                continue;
            }
            if (s.argument == "on") render::TerrainLevels(render::TerrainLevelMode::Levels);
            else if (s.argument == "point") render::TerrainLevels(render::TerrainLevelMode::PointMips);
            else if (s.argument == "off") render::TerrainLevels(render::TerrainLevelMode::Off);
            else g_failures.push_back("terrainlevels needs on, point or off");
        } else if (s.verb == "levelsteps") {
            char frame[64] = "";
            double least = 0, most = 1;
            if (sscanf_s(s.argument.c_str(), "%63s %lf %lf", frame, unsigned(sizeof(frame)), &least, &most) < 3) {
                g_failures.push_back("levelsteps needs a frame name, a least and a most fraction");
                continue;
            }
            if (!render::Active()) {
                Log("harness: levelsteps %s: not the new renderer (the original code): no level map", frame);
                continue;
            }
            double steps = 0;
            long long pairs = 0;
            const char* why = "";
            if (!capture::LevelSteps(frame, &steps, &pairs, &why)) {
                g_failures.push_back(std::string("levelsteps ") + frame + ": " + why);
                continue;
            }
            char text[200];
            std::snprintf(text, sizeof(text), "levelsteps %s: %.4f%% of %lld neighbouring pairs step a level (allowed %.4f%% .. %.4f%%)",
                          frame, 100.0 * steps, pairs, 100.0 * least, 100.0 * most);
            Log("harness: %s", text);
            g_capturedFrames.push_back(text);
            if (steps < least || steps > most) g_failures.push_back(text);
        } else if (s.verb == "mmplevels") {
            double most = 0;
            if (sscanf_s(s.argument.c_str(), "%lf", &most) < 1) {
                g_failures.push_back("mmplevels needs the largest mean difference allowed");
                continue;
            }
            if (!render::Active()) {
                Log("harness: mmplevels: not the new renderer (the original code): no decoded textures");
                continue;
            }
            render::MmpLevelCheck check = render::CheckMmpLevels();
            char text[240];
            std::snprintf(text, sizeof(text),
                          "mmplevels: %d textures, %d levels; largest mean difference %.2f (allowed %.2f; level %d of a "
                          "%dx%d texture, format %08X)",
                          check.textures, check.levels, check.worst, most, check.worstLevel, check.worstWidth,
                          check.worstHeight, static_cast<unsigned>(check.worstFormat));
            Log("harness: %s", text);
            g_capturedFrames.push_back(text);
            if (check.levels == 0 || check.worst > most) g_failures.push_back(text);
        } else if (s.verb == "smooth") {
            if (!render::Active()) {
                Log("harness: smooth %s: not the new renderer (the original code): ignored", s.argument.c_str());
                continue;
            }
            if (s.argument == "on" || s.argument == "off") render::SetSmooth(s.argument == "on");
            else g_failures.push_back("smooth needs on or off");
        } else if (s.verb == "smoothcheck") {
            char plain[64] = "", smooth[64] = "";
            double least = 0, most = 1;
            if (sscanf_s(s.argument.c_str(), "%63s %63s %lf %lf", plain, unsigned(sizeof(plain)), smooth, unsigned(sizeof(smooth)),
                         &least, &most) < 4) {
                g_failures.push_back("smoothcheck needs two frame names, a least and a most fraction");
                continue;
            }
            if (!render::Active()) {
                Log("harness: smoothcheck %s %s: not the new renderer (the original code): skipped", plain, smooth);
                continue;
            }
            render::SmoothInfo info = render::SmoothState();
            double differing = 0;
            int largest = 0;
            const char* why = "";
            bool compared = capture::Compare(plain, smooth, 8, &differing, &largest, &why);
            char text[320];
            std::snprintf(text, sizeof(text),
                          "smoothcheck %s %s: option %s, %d samples (the device gives %d), last frame %d multisampled "
                          "passes, %d models anisotropic, %d as their stage; %.3f%% of the pixels differ by more than 8 "
                          "(largest %d; allowed %.3f%% .. %.3f%%)",
                          plain, smooth, info.on ? "on" : "off", info.targetSamples, info.deviceSamples, info.multisampledPasses,
                          info.anisotropicModels, info.plainModels, 100.0 * differing, largest, 100.0 * least, 100.0 * most);
            Log("harness: %s", text);
            g_capturedFrames.push_back(text);
            if (!compared) g_failures.push_back(std::string("smoothcheck ") + plain + " " + smooth + ": " + why);
            else if (!info.on || info.targetSamples < 2 || info.targetSamples != info.deviceSamples ||
                     info.multisampledPasses < 1 || info.anisotropicModels < 1 || differing < least || differing > most)
                g_failures.push_back(text);
        } else if (s.verb == "shadows") {
            if (!render::Active()) {
                Log("harness: shadows %s: not the new renderer (the original code): ignored", s.argument.c_str());
                continue;
            }
            if (s.argument == "classic" || s.argument == "map") render::SetClassicShadows(s.argument == "classic");
            else if (s.argument == "rebuild") render::RebuildShadowTextures();
            else g_failures.push_back("shadows needs classic, map or rebuild");
        } else if (s.verb == "shadowbuild") {
            if (!render::Active()) {
                Log("harness: shadowbuild %s: not the new renderer (the original code): ignored", s.argument.c_str());
                continue;
            }
            if (s.argument == "skip" || s.argument == "full") render::SetShadowBuildFull(s.argument == "full");
            else g_failures.push_back("shadowbuild needs skip or full");
        } else if (s.verb == "terraincache") {
            if (!render::Active()) {
                Log("harness: terraincache %s: not the new renderer (the original code): ignored", s.argument.c_str());
                continue;
            }
            if (s.argument == "on" || s.argument == "off") {
                render::SetTerrainCache(s.argument == "on");
            } else if (s.argument == "check") {
                render::TerrainCacheInfo info = render::TerrainCacheState();
                char text[200];
                std::snprintf(text, sizeof(text), "terraincache: %s, %zu pages kept, %llu checks against a fresh build, %llu stale",
                              info.on ? "on" : "off", info.pages, static_cast<unsigned long long>(info.checks),
                              static_cast<unsigned long long>(info.stale));
                Log("harness: %s", text);
                g_capturedFrames.push_back(text);
                if (info.stale > 0) g_failures.push_back(text);
            } else {
                g_failures.push_back("terraincache needs on, off or check");
            }
        } else if (s.verb == "meshcache") {
            if (!render::Active()) {
                Log("harness: meshcache %s: not the new renderer (the original code): ignored", s.argument.c_str());
                continue;
            }
            if (s.argument == "on" || s.argument == "off") {
                render::SetMeshCache(s.argument == "on");
            } else if (s.argument == "check") {
                render::MeshCacheInfo info = render::MeshCacheState();
                char text[200];
                std::snprintf(text, sizeof(text), "meshcache: %s, %zu meshes kept, %llu checks against a fresh build, %llu stale",
                              info.on ? "on" : "off", info.meshes, static_cast<unsigned long long>(info.checks),
                              static_cast<unsigned long long>(info.stale));
                Log("harness: %s", text);
                g_capturedFrames.push_back(text);
                if (info.stale > 0) g_failures.push_back(text);
            } else {
                g_failures.push_back("meshcache needs on, off or check");
            }
        } else if (s.verb == "meshcorners") {
            if (!render::Active()) {
                Log("harness: meshcorners %s: not the new renderer (the original code): ignored", s.argument.c_str());
                continue;
            }
            if (s.argument == "read" || s.argument == "full") render::SetMeshCornersFull(s.argument == "full");
            else g_failures.push_back("meshcorners needs read or full");
        } else if (s.verb == "memorytextures") {
            if (!render::Active()) {
                Log("harness: memorytextures %s: not the new renderer (the original code): ignored", s.argument.c_str());
                continue;
            }
            if (s.argument == "inplace" || s.argument == "remake") render::SetMemoryTexturesInPlace(s.argument == "inplace");
            else g_failures.push_back("memorytextures needs inplace or remake");
        } else if (s.verb == "shadowcheck") {
            int least = 1;
            if (sscanf_s(s.argument.c_str(), "%d", &least) < 1) {
                g_failures.push_back("shadowcheck needs the least number of casters");
                continue;
            }
            if (!render::Active()) {
                Log("harness: shadowcheck: not the new renderer (the original code): skipped");
                continue;
            }
            render::ShadowInfo info = render::ShadowState();
            char text[320];
            std::snprintf(text, sizeof(text),
                          "shadowcheck: %s, map %s (%d texels over %.0f units), %d casters in %d draws, %d meshes and %d "
                          "ground batches sampling it (at least %d casters); shadowed objects not drawn %d, drawn but "
                          "not cast %d",
                          info.classic ? "classic" : "shadow map", info.made ? "made" : "not made", info.size,
                          static_cast<double>(info.extent), info.casters, info.casterDraws, info.receivingMeshes,
                          info.receivingTerrain, least, info.offScreen, info.lost);
            Log("harness: %s", text);
            g_capturedFrames.push_back(text);
            if (info.classic || !info.made || info.casters < least || info.receivingMeshes < 1 || info.receivingTerrain < 1 ||
                info.lost > 0)
                g_failures.push_back(text);
        } else if (s.verb == "renderstats") {
            if (!render::Active()) {
                Log("harness: renderstats %s: not the new renderer (the original code): skipped", s.argument.c_str());
                continue;
            }
            render::FrameStats t = render::TakeFrameStats();
            // The main thread's CPU time (its cycles, in milliseconds by the thread's CPU time so far, as the
            // TIMING lines) and the wall time since the last renderstats (R16h: what a frame costs the main
            // thread, the game's work with the renderer's). Differences of whole counts first: the main thread
            // computes at the game's 24-bit precision.
            static unsigned long long lastCycles = 0;
            static long long lastTicks = 0;
            unsigned long long cycles = 0;
            double msPerCycle = 0.0, msPerTick = 0.0;
            LARGE_INTEGER now{}, frequency{};
            {
                FILETIME created{}, exited{}, kernel{}, user{};
                QueryThreadCycleTime(GetCurrentThread(), &cycles);
                if (cycles && GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user)) {
                    auto ms = [](const FILETIME& f) {
                        return static_cast<double>((static_cast<unsigned long long>(f.dwHighDateTime) << 32) | f.dwLowDateTime) / 1e4;
                    };
                    msPerCycle = (ms(kernel) + ms(user)) / static_cast<double>(cycles);
                }
                QueryPerformanceCounter(&now);
                QueryPerformanceFrequency(&frequency);
                if (frequency.QuadPart) msPerTick = 1000.0 / static_cast<double>(frequency.QuadPart);
            }
            const double frameCpuMs = static_cast<double>(cycles - lastCycles) * msPerCycle;
            const double frameWallMs = static_cast<double>(now.QuadPart - lastTicks) * msPerTick;
            lastCycles = cycles;
            lastTicks = now.QuadPart;
            if (s.argument == "-" || t.frames == 0) continue;   // a new count begins
            double n = static_cast<double>(t.frames);
            auto kb = [n](std::uint64_t bytes) { return static_cast<double>(bytes) / 1024.0 / n; };
            auto per = [n](std::uint64_t count) { return static_cast<double>(count) / n; };
            using F = render::FrameStats;
            char text[2400];
            std::snprintf(
                text, sizeof(text),
                "renderstats %s: %u frames of %dx%d, per frame: hand-over %.0f terrain pages -> %.0f batches, %.0f meshes / %.0f "
                "passes (%.0f flat, %.1f KB), %.0f pre-transformed, %.0f blits, %.0f depth clears, %.0f state reads, "
                "%.0f matrices; against the last frame (same / relit / moved / new): terrain pages %.0f / %.0f / %.0f / %.0f, meshes %.0f / %.0f / "
                "%.0f / %.0f, %.0f meshes share another's geometry; upload KB "
                "terrain %.1f v + %.1f i, meshes %.1f v + %.1f i, uniforms %.1f; %.1f passes, %.0f draws, pipelines %.0f "
                "applied / %.0f changed, bindings %.0f (first texture changed %.0f); images made (count / KB): mmp %.2f / "
                "%.1f, cpu pixels %.2f / %.1f, memory textures %.2f / %.1f, window surfaces %.2f / %.1f, movie %.2f / %.1f, "
                "small %.2f / %.1f; largest image KB: memory texture %.1f, window surface %.1f; memory compared %.1f KB; "
                "CPU ms terrain %.3f, hand-over %.3f, textures %.3f (window surfaces %.3f), end of frame %.3f (counting %.3f), device mesh draws %.3f; "
                "decals projected: %.1f shadows %.3f ms, %.1f prints %.3f ms (%.0f vertices); flat draws: %.1f shadows %.3f ms, "
                "%.1f others %.3f ms (%.0f vertices); shadow builds %.1f (textures %.1f, %.1f only for the texture), %.3f ms; "
                "shadow map %.1f draws, CPU ms %.3f; GPU ms (%u frames) frame %.3f, shadow map %.3f; morphed meshes %.1f "
                "(%.0f corners); main thread ms CPU %.2f / wall %.2f",
                s.argument.c_str(), t.frames, t.width, t.height, per(t.terrainPages), per(t.terrainBatches), per(t.meshes),
                per(t.meshPasses), per(t.flatMeshes), kb(t.flatVertexBytes),
                per(t.transformedDraws), per(t.blits), per(t.depthClears), per(t.stateReads), per(t.matrices),
                per(t.terrainReuse[F::Same]), per(t.terrainReuse[F::Relit]), per(t.terrainReuse[F::Moved]),
                per(t.terrainReuse[F::New]), per(t.meshReuse[F::Same]), per(t.meshReuse[F::Relit]), per(t.meshReuse[F::Moved]),
                per(t.meshReuse[F::New]), per(t.sharedMeshes),
                kb(t.terrainVertexBytes), kb(t.terrainIndexBytes), kb(t.meshVertexBytes), kb(t.meshIndexBytes), kb(t.uniformBytes),
                per(t.passes), per(t.draws), per(t.pipelineApplies), per(t.pipelineChanges), per(t.bindings),
                per(t.textureChanges), per(t.uploads[F::Mmp]), kb(t.uploadBytes[F::Mmp]), per(t.uploads[F::CpuPixels]),
                kb(t.uploadBytes[F::CpuPixels]), per(t.uploads[F::MemoryTexture]), kb(t.uploadBytes[F::MemoryTexture]),
                per(t.uploads[F::WindowSurface]), kb(t.uploadBytes[F::WindowSurface]), per(t.uploads[F::Movie]),
                kb(t.uploadBytes[F::Movie]), per(t.uploads[F::Small]), kb(t.uploadBytes[F::Small]),
                static_cast<double>(t.largestUpload[F::MemoryTexture]) / 1024.0,
                static_cast<double>(t.largestUpload[F::WindowSurface]) / 1024.0, kb(t.comparedBytes),
                t.terrainMs / n, t.handOverMs / n, (t.textureMs + t.windowMs) / n, t.windowMs / n, t.endFrameMs / n, t.statsMs / n, t.deviceMeshMs / n,
                per(t.decalShadows), t.decalShadowMs / n, per(t.decalPrints), t.decalPrintMs / n, per(t.decalVertices),
                per(t.flatShadowDraws), t.flatShadowMs / n, per(t.flatOtherDraws), t.flatOtherMs / n,
                per(t.flatDrawVertices), per(t.shadowBuilds), per(t.shadowTextures), per(t.staleShadowTextures),
                t.shadowBuildMs / n,
                per(t.shadowDraws), t.shadowMs / n, t.gpuFrames, t.gpuFrames ? t.gpuFrameMs / t.gpuFrames : 0.0,
                t.gpuFrames ? t.gpuShadowMs / t.gpuFrames : 0.0, per(t.morphMeshes), per(t.morphCorners), frameCpuMs / n,
                frameWallMs / n);
            Log("harness: %s", text);
            g_capturedFrames.push_back(text);
        } else if (s.verb == "checkshot") {
            int index = -1, tolerance = 8;
            char frame[64] = "";
            double limit = 0.001;
            // "recorder" first: the frame recorder's file (the movie folder ".\capture\"), not Scroll Lock's.
            bool recorder = s.argument.compare(0, 9, "recorder ") == 0;
            if (sscanf_s(s.argument.c_str() + (recorder ? 9 : 0), "%d %63s %d %lf", &index, frame, unsigned(sizeof(frame)),
                         &tolerance, &limit) < 2 ||
                index < 0) {
                g_failures.push_back("checkshot needs [recorder] <n> <frame>");
                continue;
            }
            char path[MAX_PATH];
            std::snprintf(path, sizeof(path), "%s\\%sshot%.4d.bmp", g_savesDir, recorder ? "capture\\" : "", index);
            double differing = 0;
            int largest = 0;
            std::string how;
            const char* why = "";
            if (!capture::CompareFile(path, frame, tolerance, &differing, &largest, &how, &why)) {
                g_failures.push_back(std::string("checkshot ") + std::to_string(index) + " " + frame + ": " + why);
                Log("harness: checkshot %d %s: %s (%s)", index, frame, why, path);
                continue;
            }
            char text[240];
            std::snprintf(text, sizeof(text), "checkshot %s%d %s: %sshot%.4d.bmp %s; %.3f%% of the pixels differ by more than %d (largest %d)",
                          recorder ? "recorder " : "", index, frame, recorder ? "capture\\" : "", index, how.c_str(),
                          100.0 * differing, tolerance, largest);
            Log("harness: %s", text);
            g_capturedFrames.push_back(text);
            if (differing > limit) g_failures.push_back(text);
        } else if (s.verb == "checksave") {
            int tolerance = 8;
            char slot[64] = "", frame[64] = "";
            double limit = 0.001;
            int used = 0;
            if (sscanf_s(s.argument.c_str(), "%63s %63s%n", slot, unsigned(sizeof(slot)), frame, unsigned(sizeof(frame)),
                         &used) < 2) {
                g_failures.push_back("checksave needs <slot> <frame>");
                continue;
            }
            const char* rest = s.argument.c_str() + used;
            while (*rest == ' ') ++rest;
            bool centre = std::strncmp(rest, "centre", 6) == 0;
            if (centre) rest += 6;
            sscanf_s(rest, "%d %lf", &tolerance, &limit);
            char path[MAX_PATH];
            std::snprintf(path, sizeof(path), "%s\\%s\\shot.sav", g_savesDir, slot);
            double differing = 0;
            int largest = 0;
            const char* why = "";
            if (!capture::CompareSavePicture(path, frame, centre, tolerance, &differing, &largest, &why)) {
                g_failures.push_back(std::string("checksave ") + slot + " " + frame + ": " + why);
                Log("harness: checksave %s %s: %s (%s)", slot, frame, why, path);
                continue;
            }
            char text[200];
            std::snprintf(text, sizeof(text), "checksave %s %s: %.3f%% of the pixels differ by more than %d (largest %d)", slot,
                          frame, 100.0 * differing, tolerance, largest);
            Log("harness: %s", text);
            g_capturedFrames.push_back(text);
            if (differing > limit) g_failures.push_back(text);
        } else if (s.verb == "record") {
            ei::GameData<unsigned char>(0x0079BC54) = s.argument == "on" ? 1 : 0;
        } else if (s.verb == "waitpresent") {
            int seconds = 10;
            char what[16] = "";
            sscanf_s(s.argument.c_str(), "%d %15s", &seconds, what, unsigned(sizeof(what)));
            g_waitPresentFrom = render::WindowPresents().presents;
            g_waitPresentGamma = std::strcmp(what, "gamma") == 0;
            g_waitPresentDeadline.Set(static_cast<DWORD>(seconds) * 1000);
            g_waitingPresent = true;
            return;
        } else if (s.verb == "losedevice" || s.verb == "removedevice") {
            int seconds = 10;
            sscanf_s(s.argument.c_str(), "%d", &seconds);
            if (!render::Active()) {
                g_failures.push_back(s.verb + ": the new renderer is not on");
                continue;
            }
            g_newDeviceFrom = render::WindowPresents().deviceRecoveries;
            g_newDeviceRemoved = s.verb == "removedevice";
            if (!g_newDeviceRemoved) {
                render::LoseDevice();
            } else if (!render::RemoveDevice()) {
                g_failures.push_back("removedevice: the device is not removable (\"render new,present,removable\")");
                continue;
            }
            g_newDeviceDeadline = Now() + static_cast<DWORD>(seconds) * 1000;
            g_waitingNewDevice = true;
            return;
        } else if (s.verb == "mouseglide") {
            int ms = 0, x0 = 0, y0 = 0, x1 = 0, y1 = 0;
            if (sscanf_s(s.argument.c_str(), "%d %d %d %d %d", &ms, &x0, &y0, &x1, &y1) != 5 || ms <= 0) {
                g_failures.push_back("mouseglide needs <ms> <x0> <y0> <x1> <y1>");
                continue;
            }
            StartMouseGlide(ms, x0, y0, x1, y1);
        } else if (s.verb == "stall") {
            DWORD ms = std::strtoul(s.argument.c_str(), nullptr, 10);
            Sleep(ms);   // ei_hook's own Sleep: real time, the game's clocks are not touched
            Log("harness: stall: the main thread was away for %lu ms", ms);
        } else if (s.verb == "cursormark") {
            g_cursorMark = render::CursorPresents().presents;
            Log("harness: cursormark: %u presents by the cursor thread so far", g_cursorMark);
        } else if (s.verb == "cursorcheck") {
            std::string error = CursorCheck(s.argument);
            if (!error.empty()) {
                Log("harness: %s", error.c_str());
                g_failures.push_back(error);
            }
        } else if (s.verb == "focus" || s.verb == "windowsize") {
            std::string error = s.verb == "focus" ? FocusStep(s.argument) : WindowSizeStep(s.argument);
            if (!error.empty()) g_failures.push_back(error);
        } else if (s.verb == "hang") {
            g_stackDumpHangs = s.argument == "dump" || s.argument == "all";
            g_watchdogStopped = s.argument == "all";
            Log("harness: hang %s (a test of the time limit): the main thread blocks for good", s.argument.c_str());
            Sleep(INFINITE);
        } else if (s.verb == "freeze" || s.verb == "unfreeze") {
            clock::Freeze(s.verb == "freeze");
        } else if (s.verb == "digest" || s.verb == "dump") {
            std::string error = s.verb == "digest" ? digest::Step(s.argument) : digest::Dump(s.argument);
            if (!error.empty()) {
                Log("harness: %s", error.c_str());
                g_failures.push_back(error);
            }
        } else if (s.verb == "reject") {
            g_rejected.push_back(s.argument);
        } else if (s.verb == "diskfull") {
            crash::FailWritesTo(s.argument.c_str());
        } else if (s.verb == "expectcrash") {
            g_expectedCrash = s.argument;
        } else if (s.verb == "report") {
            WriteReport();
        } else if (s.verb == "quit") {
            if (!g_expectedCrash.empty()) g_failures.push_back("the game did not crash (expectcrash " + g_expectedCrash + ")");
            WriteReport();
            // The game can hang on the way out (Rebirth did: harness_starter.cpp). The quit console command calls exit(),
            // which stops our watchdog thread too, so ei_launch ends the process when it sees
            // harness_quit and the game is still running 3 s later.
            char marker[MAX_PATH];
            strcpy_s(marker, g_reportPath);
            if (char* slash = std::strrchr(marker, '\\')) strcpy_s(slash + 1, marker + MAX_PATH - (slash + 1), "harness_quit");
            if (FILE* f = nullptr; fopen_s(&f, marker, "w") == 0 && f) std::fclose(f);
            g_quitting = true;
            ConsoleExecute("quit");
            return;
        } else {
            Log("harness: unknown step '%s'", s.verb.c_str());
        }
    }
}

// Watchdog thread: makes sure every harness run ends without Task Manager. After "quit" the game
// gets 5 s to exit by itself. A scenario that has not
// finished within its time limit (60 s from game start unless it says "timeout <seconds>") fails:
// the report is written with the failure and the game is closed. Keep scenarios well inside it.
constexpr DWORD kDefaultTimeLimitMs = 60000;
DWORD g_budget = kDefaultTimeLimitMs;

// A timed-out run: where the main thread (the process's oldest thread) is, for ei_hook.log: its
// instruction pointer and the return addresses on its stack (words after a CALL, in any loaded
// module), as module+offset.
bool DescribeAddress(std::uintptr_t a, char* out, std::size_t size) {
    HMODULE m = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCSTR>(a), &m))
        return false;
    char name[MAX_PATH] = "";
    GetModuleFileNameA(m, name, MAX_PATH);
    const char* file = std::strrchr(name, '\\');
    std::snprintf(out, size, "%s+%X", file ? file + 1 : name, unsigned(a - reinterpret_cast<std::uintptr_t>(m)));
    return true;
}

bool AfterCallInstruction(std::uintptr_t a) {
    MEMORY_BASIC_INFORMATION mbi{};
    constexpr DWORD kExecutable = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    if (!VirtualQuery(reinterpret_cast<const void*>(a - 7), &mbi, sizeof(mbi)) || !(mbi.Protect & kExecutable)) return false;
    const auto* p = reinterpret_cast<const unsigned char*>(a);
    if (p[-5] == 0xE8) return true;   // CALL rel32
    for (int n = 2; n <= 7; ++n)
        if (p[-n] == 0xFF && ((p[-n + 1] >> 3) & 7) == 2) return true;   // CALL r/m32
    return false;
}

void LogMainThreadStack() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return;
    DWORD mainId = 0;
    ULONGLONG oldest = ~0ull;
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry)) {
        if (entry.th32OwnerProcessID != GetCurrentProcessId()) continue;
        HANDLE t = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ThreadID);
        if (!t) continue;
        FILETIME created, exited, kernel, user;
        if (GetThreadTimes(t, &created, &exited, &kernel, &user)) {
            const ULONGLONG when = (ULONGLONG(created.dwHighDateTime) << 32) | created.dwLowDateTime;
            if (when < oldest) {
                oldest = when;
                mainId = entry.th32ThreadID;
            }
        }
        CloseHandle(t);
    }
    CloseHandle(snapshot);
    HANDLE thread = mainId ? OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, mainId) : nullptr;
    if (!thread) return;
    std::vector<std::uintptr_t> stack(4096);
    std::size_t words = 0;
    CONTEXT context{};
    context.ContextFlags = CONTEXT_CONTROL;
    if (SuspendThread(thread) != DWORD(-1)) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (GetThreadContext(thread, &context) && VirtualQuery(reinterpret_cast<const void*>(context.Esp), &mbi, sizeof(mbi))) {
            const std::uintptr_t end = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
            words = (end - context.Esp) / sizeof(std::uintptr_t);
            if (words > stack.size()) words = stack.size();
            std::memcpy(stack.data(), reinterpret_cast<const void*>(context.Esp), words * sizeof(std::uintptr_t));
        }
        ResumeThread(thread);
    }
    CloseHandle(thread);
    char where[MAX_PATH + 16] = "?";
    DescribeAddress(context.Eip, where, sizeof(where));
    Log("harness: main thread %lu at %08X %s; return addresses on its stack:", mainId, unsigned(context.Eip), where);
    int logged = 0;
    for (std::size_t i = 0; i < words && logged < 48; ++i) {
        const std::uintptr_t a = stack[i];
        if (a < 0x10000 || !DescribeAddress(a, where, sizeof(where)) || !AfterCallInstruction(a)) continue;
        Log("  %08X %s", unsigned(a), where);
        ++logged;
    }
}

// The stack dump runs on a thread of its own and the watchdog waits 2 s for it: suspending the main
// thread and reading its context waits until the thread is actually suspended, which a thread stuck
// in a driver call (DirectSound's device open inside Miles' waveOutOpen under load) never is. The
// watchdog itself must reach TerminateProcess whatever the main thread is doing (SV1d).
void LogMainThreadStackBounded() {
    HANDLE dump = CreateThread(nullptr, 0, [](void*) -> DWORD {
        if (g_stackDumpHangs) Sleep(INFINITE);
        LogMainThreadStack();
        return 0;
    }, nullptr, 0, nullptr);
    if (!dump) return;
    if (WaitForSingleObject(dump, 2000) == WAIT_TIMEOUT)
        Log("harness: the main thread's stack could not be read in 2 s (it cannot be suspended)");
    CloseHandle(dump);
}

DWORD WINAPI Watchdog(void*) {
    const DWORD start = GetTickCount();
    for (unsigned beat = 1;; ++beat) {
        Sleep(250);
        if (g_watchdogStopped) Sleep(INFINITE);
        if (beat % 4 == 0) netlocal::Scan();   // binds outside loopback, whoever made them (harness_net.h)
        if (g_quitting) {
            Sleep(5000);
            Log("harness: the game did not exit 5 s after quit; terminating it");
            TerminateProcess(GetCurrentProcess(), 0);
        }
        if (GetTickCount() - start >= g_budget) {
            Log("harness: scenario timed out after %lu s (step %u of %u); terminating the game",
                (GetTickCount() - start) / 1000, unsigned(g_next), unsigned(g_steps.size()));
            char what[160];
            std::snprintf(what, sizeof(what), "scenario exceeded its %lu s time limit at step %u of %u",
                          g_budget / 1000, unsigned(g_next), unsigned(g_steps.size()));
            g_failures.push_back(what);
            LogMainThreadStackBounded();
            // Write the report from another thread and end the game whether or not that finishes:
            // a crashed or deadlocked main thread may hold a lock the report needs.
            if (HANDLE writer = CreateThread(nullptr, 0, [](void*) -> DWORD { WriteReport(); return 0; }, nullptr, 0, nullptr)) {
                WaitForSingleObject(writer, 3000);
                CloseHandle(writer);
            }
            TerminateProcess(GetCurrentProcess(), 1);
        }
    }
}

// The x87 control word of the calling thread (precision, rounding). The game runs its world on the
// main thread at whatever precision Direct3D and the DLLs loaded later leave (24-bit in the vanilla
// install, more under Rebirth): logged when it changes, to find out who changes it.
unsigned short FpuControlWord() {
    unsigned short cw;
    __asm fnstcw cw
    return cw;
}
void TraceFpu(const char* where) {
    static unsigned short last = 0xFFFF;
    static int changes = 0;
    unsigned short cw = FpuControlWord();
    if (cw == last) return;
    if (++changes <= 20) Log("harness: FPU control word %04X (was %04X) %s", cw, last, where);
    else if (changes == 21) Log("harness: FPU control word keeps changing; no more reports");
    last = cw;
}
using AilStartupFn = int(WINAPI*)();
AilStartupFn g_ailStartup;
constexpr std::uintptr_t kAilStartupSlot = 0x0073B7D4;   // mss32.dll!_AIL_startup@0 (loads the *.asi)
int WINAPI TracedAilStartup() {
    TraceFpu("before AIL_startup");
    int result = g_ailStartup();
    TraceFpu("after AIL_startup (Miles loads the *.asi providers here: mp3dec.asi, EI_Plugin.asi)");
    return result;
}

// The game pauses itself when its window loses focus (WM_ACTIVATE inactive: it minimises the
// window and stops the renderer, sound and network). Parallel harness runs leave only one game in
// front, so the harness keeps every game active: it subclasses the window and drops the
// deactivation (and minimise) messages.
WNDPROC g_gameWindowProc;
// (Harness games no longer take the foreground, noforeground:: above, so this is now only the focus off
// step's minimise.) A game that Windows gave the foreground (when the window in front closed) and that
// then lost it to a starting game was minimised by dgVoodoo, in the middle of a pass, and its surfaces are lost:
// the pass's BeginScene failed with DDERR_SURFACELOST ("Can't begin scene") and the run ended on the
// game's error box. The game itself restores its surfaces only when it is activated again (the main
// loop's RestoreAllSurfaces, 00457140), which the harness's always-active window never sees. So the
// harness notes the minimise (WM_SIZE, sent from inside a PeekMessageA while dgVoodoo handles the
// deactivation) and, once that PeekMessageA has returned and dgVoodoo is done, puts the window back
// without activating it and restores the surfaces as that code does (under the device lock, the
// result in the device's lastLoadResult_), after every peek for the next 50 (surfaces still lost
// right after the minimise were seen to fail the next BeginScene even so). Seen so far, though, the
// minimise always came twice within ~100 ms, and the second took the exclusive mode as well.
HWND g_minimised;
int g_restoresLeft;
void RestoreAfterMinimise() {
    if (!g_minimised) return;
    if (g_restoresLeft == 50) {
        g_showWindow(g_minimised, SW_SHOWNOACTIVATE);
        Log("harness: the game window was minimised (a focus off step, or the user): shown again");
    }
    CTextureDevice* device = RenderDeviceObject();
    if (device && device->ddraw_) {
        auto* lock = ei::GamePointer<CRITICAL_SECTION>(0x007AEE78);   // the render device's lock
        EnterCriticalSection(lock);
        HRESULT result = static_cast<IDirectDraw7*>(device->ddraw_)->RestoreAllSurfaces();
        device->lastLoadResult_ = result;
        LeaveCriticalSection(lock);
        if (result == DDERR_NOEXCLUSIVEMODE) {
            // A second minimise soon after the first also takes the exclusive mode away, and getting
            // it back (the renderer's restore after a switch back, 00504780) hangs while the window
            // is in the background: the run ends on the game's lost-surface box. (Harness games no longer
            // take the foreground, noforeground::, so this should not be seen.)
            Log("harness: the exclusive mode was lost with the foreground; the run ends on the game's lost-surface "
                "box");
            g_minimised = nullptr;
            return;
        }
        if (g_restoresLeft == 50 || result != DD_OK) Log("harness: surfaces restored (%08X)", unsigned(result));
    }
    if (--g_restoresLeft <= 0) g_minimised = nullptr;
}

LRESULT CALLBACK AlwaysActiveWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_SIZE && wParam == SIZE_MINIMIZED) {
        g_minimised = window;
        g_restoresLeft = 50;
        return 0;
    }
    if (message == WM_ACTIVATE && (LOWORD(wParam) == WA_INACTIVE || HIWORD(wParam) != 0)) return 0;
    if (message == WM_ACTIVATEAPP && !wParam) return 0;
    if (LRESULT result; input::RealMouse(window, message, wParam, lParam, &result)) return result;
    return CallWindowProcA(g_gameWindowProc, window, message, wParam, lParam);
}

// The window is shown and made without activating it, and nothing in the process brings it to the
// front (ShowWithoutActivating, noforeground::), so Windows never sends it the WM_ACTIVATE that
// sets the game's active flag 0079BC5C and starts its frames. (Under dgVoodoo its exclusive
// cooperative level used to activate the window; without DirectDraw, the switch-over in device.h,
// nothing did.) Once the device object exists and the game is not active, the harness sends the
// activation a foreground start would have given (tickets R9a2, SV1c).
void ActivateWithoutForeground(HWND window) {
    static bool sent = false;
    if (sent || !ei::GameData<void*>(0x007AF080)) return;
    sent = true;
    if (ei::GameData<std::uint8_t>(0x0079BC5C)) return;
    Log("harness: the game window activated (nothing activates it: harness games never take the foreground)");
    SendMessageA(window, WM_ACTIVATE, WA_ACTIVE, 0);
}

// focus off: what Windows sends the game when another window takes the focus (WM_ACTIVATE
// WA_INACTIVE, which AlwaysActiveWindowProc otherwise keeps from it): the game deactivates and asks
// to be minimised, which ShowWithoutActivating leaves out, so the step minimises the window itself
// (activating nothing). focus on: the window restored (not activated) and the activation Windows
// sends when the user comes back (WA_CLICKACTIVE). Ticket R9c.
std::string FocusStep(const std::string& argument) {
    HWND window = GameWindow();
    if (!window || !g_gameWindowProc) return "focus: no game window yet";
    if (argument == "off") {
        Log("harness: focus off (WM_ACTIVATE inactive, the window minimised)");
        CallWindowProcA(g_gameWindowProc, window, WM_ACTIVATE, WA_INACTIVE, 0);
        g_showWindow(window, SW_SHOWMINNOACTIVE);
        return "";
    }
    if (argument == "on") {
        Log("harness: focus on (the window restored, WM_ACTIVATE click-active)");
        g_showWindow(window, SW_SHOWNOACTIVATE);
        CallWindowProcA(g_gameWindowProc, window, WM_ACTIVATE, WA_CLICKACTIVE, 0);
        return "";
    }
    return "focus needs off or on";
}

// windowsize <width> <height>: the game window resized (it is a WS_POPUP: its size is the client
// area's), as the user or a display change could; the switch-over scales the frame into it.
std::string WindowSizeStep(const std::string& argument) {
    int width = 0, height = 0;
    if (sscanf_s(argument.c_str(), "%d %d", &width, &height) != 2 || width <= 0 || height <= 0)
        return "windowsize needs <width> <height>";
    HWND window = GameWindow();
    if (!window) return "windowsize: no game window yet";
    Log("harness: windowsize %dx%d", width, height);
    SetWindowPos(window, nullptr, 0, 0, width, height, SWP_NOMOVE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE);
    return "";
}

void KeepGameWindowActive() {
    HWND window = GameWindow();
    if (!window) return;
    {
        timing::Scope scope(timing::WindowChecks);
        noforeground::Watch(window);
    }
    if (!g_gameWindowProc) {
        g_gameWindowProc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrA(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&AlwaysActiveWindowProc)));
        Log("harness: the game window stays active when it loses focus");
    }
    ActivateWithoutForeground(window);
}

// Posted input reaches the game only through its main loop (WinMain 00456440: PeekMessageA, then
// GetMessageA and the input dispatcher 00457970) or the movie player's loop (005DB3F0: Esc and
// Space). Every other PeekMessageA call comes from the game's pump 00458220, which interface window
// switches run (CInterface3D::Activate 005F71C0 when the battle screen opens after a new game,
// PushWindow, closing windows) to discard the input waiting meanwhile: what it removes goes to the
// window procedure, which ignores keys and mouse. Steps can run inside it, so input steps posted
// there were lost (the console scenario's keys, when the battle screen opened after its waitfor);
// they wait for one of these calls instead (identified by the return address; the rewritten WinMain,
// src/game/app_main.cpp, names its two calls through ei::mainloop::g_site instead).
constexpr std::uintptr_t kInputLoopReturns[] = {
    0x00457119,   // WinMain, before the frame
    0x00457610,   // WinMain, after the frame
    0x005DB788,   // the movie player
};
bool IsWinMainLoop(const void* returnAddress);
bool IsInputLoop(const void* returnAddress) {
    for (std::uintptr_t r : kInputLoopReturns)
        if (reinterpret_cast<std::uintptr_t>(returnAddress) == r) return true;
    return IsWinMainLoop(returnAddress);
}

bool IsWinMainLoop(const void* returnAddress) {
    auto r = reinterpret_cast<std::uintptr_t>(returnAddress);
    if (r == kInputLoopReturns[0] || r == kInputLoopReturns[1]) return true;
    using ei::mainloop::Site;
    return ei::mainloop::g_site == Site::PeekBeforeFrame || ei::mainloop::g_site == Site::PeekAfterFrame;
}

// A pending close-all (CInterface3D +0x44, set by a screen request 00578B00 / 00578B20 or a leave
// answer) is carried out by whichever comes first: the input dispatcher's first step
// (CloseAllPending 005F5AB0) for the next message the main loop takes, or the next pass (the
// server's answer through Activate, or Draw). Whether a message waits then (Miles' WM_TIMER,
// WM_PAINT) is real time, and the close stops the playing sounds, which Terrain::PlayAmbience's
// draws depend on (SV1j: ui_view_screen's `screen 7` closed in its own frame or the next). On the
// virtual clock the main loop's peeks report no message while one is pending, so the next pass
// always does it; after two frames (no pass took it up) messages go through again.
unsigned g_closeAllHeldFrom = kNotReady;
bool HoldMessagesForCloseAll(const void* returnAddress) {
    if (!clock::Virtual() || !IsWinMainLoop(returnAddress) ||
        !ei::GamePointer<CInterface3D>(0x007C7D50)->closeAll_) {
        g_closeAllHeldFrom = kNotReady;
        return false;
    }
    if (g_closeAllHeldFrom == kNotReady) {
        g_closeAllHeldFrom = g_frames;
        clock::RandomNote("close-all pending: messages held until the next pass");
    }
    return g_frames < g_closeAllHeldFrom + 2;
}

BOOL WINAPI PeekMessageHook(LPMSG msg, HWND hwnd, UINT min, UINT max, UINT remove) {
    heap::SettleCrt();
    script_order::PatchOriginal();
    address_order::PatchOriginal();
    if (g_randomTrace) clock::InstallRandomTrace(&g_frames);
    TraceFpu("in the message loop");
    KeepGameWindowActive();
    // A frame has been drawn: the original's after-frame call (00457610) or the rewritten loop's.
    const bool afterFrame = reinterpret_cast<std::uintptr_t>(_ReturnAddress()) == kInputLoopReturns[1] ||
                            ei::mainloop::g_site == ei::mainloop::Site::PeekAfterFrame;
    if (afterFrame) {
        ++g_frames;
        if (census::Enabled()) census::Frame();
        timing::FrameEnd();
        timing::Scope scope(timing::PairWait);
        pair::FrameDone(g_frames);   // a pair's games take turns a frame at a time (harness_pair.h)
    }
    // On the virtual clock the steps run once a frame, after it: how often the game peeks besides
    // depends on the real messages it gets (Miles' 50 ms WM_TIMER, WM_PAINT), and a step that
    // follows a console command's output would otherwise run in this frame or the next.
    if (g_running && !g_inStep && (afterFrame || !clock::Virtual())) {
        g_inStep = true;   // console commands can pump messages themselves
        g_inputLoop = IsInputLoop(_ReturnAddress());
        g_winMainLoop = IsWinMainLoop(_ReturnAddress());
        timing::Scope scope(timing::Steps);
        RunSteps();
        g_inStep = false;
    }
    if (HoldMessagesForCloseAll(_ReturnAddress())) {
        RestoreAfterMinimise();
        return FALSE;
    }
    const BOOL result = g_peekMessage(msg, hwnd, min, max, remove);
    RestoreAfterMinimise();
    return result;
}

bool LoadScenario(const char* path) {
    FILE* f = nullptr;
    if (fopen_s(&f, path, "r") != 0 || !f) return false;
    char line[1024];
    while (std::fgets(line, sizeof(line), f)) {
        std::string text(line);
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.pop_back();
        std::size_t start = text.find_first_not_of(" \t");
        if (start == std::string::npos || text[start] == '#') continue;
        text = text.substr(start);
        std::size_t space = text.find(' ');
        g_steps.push_back({text.substr(0, space), space == std::string::npos ? "" : text.substr(space + 1)});
        if (g_steps.back().verb == "randtrace") {
            g_randomTrace = true;
            g_steps.pop_back();
            continue;
        }
        if (g_steps.back().verb == "deterministic" || g_steps.back().verb == "realtime" ||
            g_steps.back().verb == "clocktrace") {
            if (g_steps.back().verb == "clocktrace") g_clockTrace = true;
            else g_deterministic = g_steps.back().verb == "deterministic";
            g_steps.pop_back();
            continue;
        }
        if (g_steps.back().verb == "rebirthflags") {
            ei::rebirth::QueueSpec(g_steps.back().argument.c_str());
            g_steps.pop_back();
            continue;
        }
        if (ei::verify::starter::ParseStep(g_steps.back().verb, g_steps.back().argument)) {
            g_steps.pop_back();
            continue;
        }
        if (g_steps.back().verb == "digest" && g_steps.back().argument == "water") {
            digest::WithWater();   // named in the digest's title, written before the main menu's first record
            g_steps.pop_back();
            continue;
        }
        if (g_steps.back().verb == "uicensus") {
            census::Enable();
            g_steps.pop_back();
            continue;
        }
        if (g_steps.back().verb == "render") {
            // The original code draws through dgVoodoo; our renderer runs only in the image.
            if (ei::Standalone()) render::Configure(g_steps.back().argument.c_str());
            else ei::Log("harness: 'render %s' ignored: a run of the original code", g_steps.back().argument.c_str());
            g_steps.pop_back();
            continue;
        }
        if (g_steps.back().verb == "peer") {   // ei_launch's: the other game of a pair (harness_pair.h)
            g_steps.pop_back();
            continue;
        }
        if (g_steps.back().verb == "timeout") {
            g_budget = 1000 * std::strtoul(g_steps.back().argument.c_str(), nullptr, 10);
            g_steps.pop_back();
            continue;
        }
        // The clocks are hooked from the start when the scenario freezes them (see HeldClock).
        const Step& last = g_steps.back();
        if (last.verb == "freeze" || ((last.verb == "vanilla" || last.verb == "rebirth") && last.argument == "freeze"))
            g_freezeUsed = true;
    }
    std::fclose(f);
    return true;
}

struct Harness {
    Harness() {
        char path[MAX_PATH];
        DWORD n = GetEnvironmentVariableA("EI_HARNESS", path, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) return;
        // Report next to ei_hook.log. It exists from the start ("RESULT: RUNNING"), so ei_launch can
        // tell a run whose harness never started from one that is still going.
        ei::OutputPath("harness_report.txt", g_reportPath);
        FILE* report = nullptr;
        fopen_s(&report, g_reportPath, "w");
        if (!LoadScenario(path)) {
            Log("harness: cannot read scenario %s", path);
            if (report) {
                std::fprintf(report, "FAILED: cannot read scenario %s\nRESULT: FAIL\n", path);
                std::fclose(report);
            }
            TerminateProcess(GetCurrentProcess(), 2);   // never leave a game waiting in the menu
        }
        // EI_HARNESS_CLOCK (run_all.py / compare.py --clock): "virtual" or "real" overrides the
        // scenario's clock, "trace" adds the clock trace.
        char clockMode[32] = "";
        GetEnvironmentVariableA("EI_HARNESS_CLOCK", clockMode, sizeof(clockMode));
        if (std::strstr(clockMode, "virtual")) g_deterministic = true;
        if (std::strstr(clockMode, "real")) g_deterministic = false;
        if (std::strstr(clockMode, "clocktrace")) g_clockTrace = true;
        if (std::strstr(clockMode, "randtrace")) g_randomTrace = true;
        if (std::strstr(clockMode, "lfheap")) g_keepHeap = true;
        if (std::strstr(clockMode, "heaptrace")) g_heapTrace = true;
        if (report) {
            std::fputs("RESULT: RUNNING\n", report);
            std::fclose(report);
        }

        HMODULE self = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&LoadScenario), &self);
        MODULEINFO selfInfo{};
        GetModuleInformation(GetCurrentProcess(), self, &selfInfo, sizeof(selfInfo));
        g_selfBase = reinterpret_cast<std::uintptr_t>(selfInfo.lpBaseOfDll);
        g_selfEnd = g_selfBase + selfInfo.SizeOfImage;
        AddVectoredExceptionHandler(1, &CrashLogger);
        ei::SetMissingCodeHandler(&EndRunOnMissingCode);
        HotPatch("MessageBoxA", reinterpret_cast<void*>(&HarnessMessageBoxA));
        HotPatch("MessageBoxW", reinterpret_cast<void*>(&HarnessMessageBoxW));
        noforeground::Install();
        // A crash must end the run: no Windows Error Reporting dialog, which freezes every thread
        // (the watchdog included) until someone closes it.
        SetErrorMode(GetErrorMode() | SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);

        strcpy_s(g_savesDir, g_reportPath);
        if (char* slash = std::strrchr(g_savesDir, '\\')) strcpy_s(slash + 1, g_savesDir + MAX_PATH - (slash + 1), "harness_saves");
        CreateDirectoryA(g_savesDir, nullptr);
        Log("harness: %lld MB free on the run folder's disk", crash::FreeMegabytes(g_savesDir));
        if (!GetCurrentDirectoryA(MAX_PATH, g_rootDirs[0])) g_rootDirs[0][0] = '\0';
        if (addon::Active()) FindAddonInstall();

        PatchSlot(kFindWindowASlot, &NoOtherGameWindow, g_findWindow);
        PatchSlot(kSetForegroundWindowSlot, &NoForeground, g_setForegroundWindow);
        PatchSlot(kBringWindowToTopSlot, &NoBringToTop, g_bringWindowToTop);
        PatchSlot(kSetFocusSlot, &NoFocus, g_setFocus);
        PatchSlot(kSetActiveWindowSlot, &NoActivate, g_setActiveWindow);
        PatchSlot(kShowWindowSlot, &ShowWithoutActivating, g_showWindow);
        PatchSlot(kCreateWindowExASlot, &CreateWithoutActivating, g_createWindowEx);
        PatchSlot(kSetCursorPosSlot, &NoCursorMove, g_setCursorPos);
        PatchSlot(kGetCursorPosSlot, &HarnessCursorPos, g_getCursorPos);
        // VideoResolution / FullScreen overrides (SmallVideoResolution) are off: a 1280x720 full screen
        // switched the display mode, and the game's window mode never got past the menu. See open-questions.
        // A scenario's "resolution" / "widescreen" (or EI_RESOLUTION / EI_WIDESCREEN): harness_starter.cpp.
        ei::verify::starter::Install(g_savesDir);
        settings::Install();   // the registry snapshot and the pinned install files, never the user's (harness_settings.h)
        netlocal::Install();   // a UDP port of its own, sockets on 127.0.0.1 only (harness_net.h)
        digest::Init(&g_frames);   // EI_DIGEST
        if (savetrace::Enabled()) savetrace::Install(g_selfBase, g_selfEnd);   // EI_SAVETRACE (harness_savetrace.h)
        leftovers::Install();   // never-set blocks zeroed in the original too (harness_leftovers.h, SV7b3, SV7b2)
        save_leftovers::Install();   // what the saves carried from memory, canonical too (harness_save_leftovers.h, SV7b2a)
        shadow_turn::Install();   // the shadow's turn undone exactly in the original too (harness_shadow_turn.h, SV7b4a)
        if (watch::Enabled()) watch::Install();   // EI_WATCH_CUNIT (harness_watch.h)
        path_cells::Install();   // the original's path cells get plain speed words too (harness_path_cells.h, SV7d2)
        if (census::Enabled()) census::Install();   // "uicensus" or EI_UI_CENSUS (ui_census.cpp)
        PatchSlot(kAilWaveOutOpenSlot, reinterpret_cast<AilWaveOutOpenFn>(&MutedWaveOutOpen), g_ailWaveOutOpen);
        PatchSlot(kPeekMessageASlot, &PeekMessageHook, g_peekMessage);
        PatchSlot(kTranslateMessageSlot, &HarnessTranslateMessage, g_translateMessage);
        PatchSlot(kAilStartupSlot, &TracedAilStartup, g_ailStartup);
        PatchSlot(kConsoleStreambufSyncSlot, reinterpret_cast<SyncFn>(&ConsoleSync), g_sync);
        PatchSlot(kCreateFileASlot, &RedirectedCreateFile, g_createFile);
        crash::WatchWrites(kWriteFileSlot);
        PatchSlot(kDeleteFileASlot, &RedirectedDeleteFile, g_deleteFile);
        PatchSlot(kCopyFileASlot, &RedirectedCopyFile, g_copyFile);
        PatchSlot(kMoveFileASlot, &RedirectedMoveFile, g_moveFile);
        PatchSlot(kFindFirstFileASlot, &RedirectedFindFirstFile, g_findFirstFile);
        PatchSlot(kCreateDirectoryASlot, &RedirectedCreateDirectory, g_createDirectory);
        PatchSlot(kRemoveDirectoryASlot, &RedirectedRemoveDirectory, g_removeDirectory);
        PatchSlot(kGetFileAttributesASlot, &RedirectedGetFileAttributes, g_getFileAttributes);
        PatchSlot(kSetFileAttributesASlot, &RedirectedSetFileAttributes, g_setFileAttributes);
        PatchSlot(kRegSetValueExASlot, &SkippedRegSetValue, g_regSetValue);
        PatchSlot(kSystemParametersInfoASlot, &HarnessSystemParametersInfo, g_systemParametersInfo);
        PatchSlot(kCreateFontIndirectASlot, &HarnessCreateFontIndirect, g_createFontIndirect);
        PatchSlot(kGetPrivateProfileStringASlot, &HarnessGetProfileString, g_getProfileString);
        PatchSlot(kGetPrivateProfileSectionASlot, &RedirectedGetProfileSection, g_getProfileSection);
        PatchSlot(kGetPrivateProfileIntASlot, &RedirectedGetProfileInt, g_getProfileInt);
        PatchSlot(kWritePrivateProfileStringASlot, &RedirectedWriteProfileString, g_writeProfileString);
        PatchSlot(kGetPrivateProfileSectionNamesASlot, &RedirectedGetProfileSectionNames, g_getProfileSectionNames);
        // Every other module's writes into the install go to the overlay too, and what still reaches an install
        // is refused and fails the run (harness_install.h).
        install::Install({&WritePath, &ReadPath, &InInstall});
        addon::Install();   // EI_ADDON_FLAGS: addon.dll's patches outside the run's Rebirth flags undone (harness_addon.h)
        if (g_deterministic) {
            clock::InstallVirtual(&g_tickLength);
            if (!g_keepHeap) heap::Install();   // the CRT's heap gives the same addresses every run (harness_heap.h)
            if (!g_keepHeap) script_order::Install();   // script groups by object id (harness_script_order.h)
            if (!g_keepHeap) address_order::Install();   // object sets by id, SetSun by sector (harness_address_order.h)
            if (!g_keepHeap && g_heapTrace) heap::StartTrace(g_selfBase, g_selfEnd);
            if (!g_keepHeap && !g_heapTrace) heap::StartSequence(g_selfBase, g_selfEnd);
            render::SetWaitForVsync(false);   // frames need not wait for the display (run\native)
            PatchSlot(kGetLocalTimeSlot, &FixedTime, g_getLocalTime);
            PatchSlot(kGetSystemTimeSlot, &FixedTime, g_getSystemTime);
            JumpTo(0x0049F660, reinterpret_cast<const void*>(&FixedIsaacSeed));
            Log("harness: deterministic mode (virtual clock)");
        } else if (g_freezeUsed) {
            clock::InstallHeld();
            PatchSlot(kGetLocalTimeSlot, &FixedTime, g_getLocalTime);
            PatchSlot(kGetSystemTimeSlot, &FixedTime, g_getSystemTime);
            Log("harness: the game's clocks can be frozen (fixed date)");
        }
        if (g_clockTrace) clock::InstallTrace();
        // The main loop's calls (harness_hooks.h): in game.exe the original's CALLs unless only the
        // hook points are wanted, and the rewritten loop's hook point for each one taken.
        ei::mainloop::HarnessHooks& hooks = ei::mainloop::g_harnessHooks;
        const bool hookPointsOnly = HookPointsOnly();
        if (hookPointsOnly) Log("harness: the main loop and the frame step through the rewritten callers' hook points only");
        if (g_freezeUsed && (hookPointsOnly || RedirectCall(kFrameCameraUpdateCall, kCameraUpdate,
                                                            reinterpret_cast<const void*>(&HeldCameraUpdate))))
            hooks.frameCameraUpdate = &HeldCameraUpdate;
        if (hookPointsOnly || RedirectCall(kServerUpdateCall, kServerUpdate, reinterpret_cast<const void*>(&HeldServerUpdate)))
            hooks.serverUpdate = &HeldServerUpdate;
        if (hookPointsOnly || RedirectCall(kClientSegmentCall, kClientSegment, reinterpret_cast<const void*>(&HeldClientSegment)))
            hooks.clientSegment = &HeldClientSegment;
        if (pair::Active() && pair::Overlap() &&
            (hookPointsOnly ||
             RedirectCall(kFrameStepCall, kFrameStep, reinterpret_cast<const void*>(&HandOffThenFrameStep))))
            hooks.frameStep = &HandOffThenFrameStep;
        for (const Step& step : g_steps)
            if (step.verb == "capture") {
                capture::Install();
                break;
            }
        Log("harness: saves and install writes redirected to %s (root %s)", g_savesDir, g_rootDirs[0]);
        if (HANDLE watchdog = CreateThread(nullptr, 0, &Watchdog, nullptr, 0, nullptr)) CloseHandle(watchdog);
        g_running = true;
        Log("harness: %u steps from %s", unsigned(g_steps.size()), path);
    }
} g_harness;
} // namespace

bool HookPointsOnly() {
    static const bool only = [] {
        char value[8] = "";
        return ei::Standalone() || (GetEnvironmentVariableA("EI_HARNESS_HOOK_POINTS", value, sizeof(value)) &&
                                    value[0] == '1');
    }();
    return only;
}

} // namespace ei::verify
