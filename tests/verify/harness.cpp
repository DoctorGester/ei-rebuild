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
//   clickregion <n>, rclickregion <n>, moveregion <n>
//                             the same at the centre of clickable region <n> of the window on top
//                             of the interface (its regions as the screen's code numbers them;
//                             placed with the game's own scaling, so Rebirth's widescreen layout too)
//   clickunit <name> [l|r|move] [ctrl]
//                             left click (default), right click or move the mouse on the unit with
//                             script name <name> (as GetObjectByName) where the game's own picking
//                             (00565E40) finds it in this frame; "ctrl" holds Ctrl (CInterface3D
//                             +0x7C) around it. Fails when the unit is not drawn or always hidden
//                             behind another object
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
//   quit                      write the report and exit the game through the console
//   realtime, deterministic   the game on real time, or (the default) on the virtual game clock
//                             with fixed seeds, where runs repeat exactly (see below)
//   clocktrace, randtrace     log every clock read and wait (ei_hook.log), every random draw
//                             (randtrace.txt; harness_clock.h)
//   vanilla <step>, rebirth <step>   run the step only in that install
//   rebirthflags <spec>       turn Rebirth patch flags on for this run (include/ei/rebirth.h): "all",
//                             or patch names, each optionally =0/=1. Read before the hooks are
//                             installed; in a Rebirth launch the flags follow addon.dll anyway.
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
//   render <spec>             the new renderer for this run (src/render/render.h; the format of
//                             EI_RENDER, e.g. "new" or "new,split"). Read before the game starts.
//                             With it, "capture" also writes frames\<name>.new.bmp
//   oldterrain on|off         the old Direct3D path draws the terrain or not (for a terrain mask:
//                             capture with and without it at one frozen moment)
//   oldobjects on|off         ... the world's meshes and shadows (with "render new")
//   oldinterface on|off       ... the interface: window surfaces, sprites, 3D previews
//   samecapture <a> <b> [tolerance] [fraction]
//                             require frames <a> and <b> to match: at most <fraction> (default
//                             0.001) of the pixels may have a channel differing by more than
//                             <tolerance> (default 8), as tools/compare_frames.py counts
//   checkshot [recorder] <n> <frame> [tolerance] [fraction]
//                             require shot<n>.bmp (the game's Scroll Lock screenshot, or with
//                             "recorder" the frame recorder's capture\shot<n>.bmp; in the saves
//                             overlay) to exist and match frames\<frame>.bmp as
//                             samecapture does (16 bits lose the low bits: use tolerance 8 or more)
//   record on|off             the game's frame recorder (0079BC54: a "shotNNNN.bmp" of each presented
//                             frame, nothing in the shipped game sets it)
//   digest <N>|off, digest    a record of the game state (world, generators, players, every world
//                             object: harness_digest.h) every <N> world updates from now into
//                             digest.txt, none (off), or one now (no argument); EI_DIGEST=<N> takes
//                             them for the whole run. tools/golden.py compares two runs' records and
//                             names the first field that differs
//   dump <name>               the whole game state now into dump_<name>.txt (harness_digest.h)
//   uicensus                  count how the interface draws into its window surfaces (GDI calls per
//                             surface, call sites, readers, drawing no hook explains) and write
//                             ui_census.csv with the report (ui_census.cpp; also EI_UI_CENSUS=1;
//                             wants "render new,cpuwindows")
//   # comment
//
// Everything the console prints is captured into the report. The report ends with "RESULT: PASS"
// (no verify mismatches, failed checks or rejected output) or "RESULT: FAIL"; ei_launch --wait turns
// that into its exit code.
//
// Harness runs are hands-off and leave the install alone: no startup movies, the tutorial option
// off, saves and config\tutorial.ini kept in harness_saves next to the DLL, every other write under
// the game's root (the composed-skin cache textures\*.mmp, config\keyboard.ini, ...) going to a
// copy-on-write overlay there too, no registry writes, and a UDP port of its own for the multiplayer
// sessions instead of 8888 (harness_net.h).
// A watchdog fails and ends a run that stops making progress (e.g. a message box), and ei_launch
// ends a game that hangs after "quit".
#include "verify.h"
#include "harness_capture.h"
#include "harness_clock.h"
#include "harness_digest.h"
#include "harness_heap.h"
#include "harness_starter.h"
#include "harness_net.h"
#include "ui_census.h"

#include "../../src/hook/log.h"
#include "ei/main_loop.h"
#include "ei/rebirth.h"
#include "../../src/lib/mfc.h"
#include "../../src/game/device.h"   // device::WithoutDirectDraw
#include "../../src/game/ui.h"
#include "../../src/game/item.h"
#include "../../src/game/spell.h"
#include "../../src/game/unit.h"
#include "../../src/game/originals_engine.h"
#include "../../src/game/originals_ui.h"
#include "../../src/game/originals_world.h"
#include "../../src/render/render.h"

#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <intrin.h>

#include <cctype>
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

// 00578B00 (GameRequestScreenOriginal): the client asks the server for interface screen `id` (the server answers
// with message 7, 00669D50 -> CInterface3D::CreateScreen 005F5BA0: 1 battle, 3 briefing, 4 global map, 5 camp).
void RequestScreen(int id) { GameRequestScreenOriginal(nullptr, nullptr, id); }
// 00578560 (GameSetConstructorOriginal): sets player 0's "constr_current" (the constructor the camp opens with).
void SetCurrentConstructor(float constructor) { GameSetConstructorOriginal(nullptr, nullptr, constructor); }
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
__declspec(naked) void __fastcall LeaveToZoneOriginal(void* /*connection*/, void*, const char* /*zone*/, int /*exit*/)
    EI_ORIGINAL(0x0066F310)

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
void* LocalWorldPlayer() { return WorldGetPlayer(reinterpret_cast<void*>(kServerWorld), nullptr, kLocalPlayer); }
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
                  connection, connection ? ConnectionState(connection) : -1, *reinterpret_cast<int*>(kWorld5C));
    return text;
}
// The server world's zone (+0x120, the map database name NewZone 00595480 stores), or "".
const char* ServerZoneName() {
    const char* name = *reinterpret_cast<const char* const*>(kServerWorld + 0x120);
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
// A pending "losedevice" (R9c1): the new devices made when it started, its deadline.
bool g_waitingNewDevice = false;
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
GameRequest& g_request = *reinterpret_cast<GameRequest*>(0x007AFE20);

// Crash diagnostics: logs access violations and other fatal exceptions with the faulting address
// and the code addresses found on the stack (game.exe addresses as is, ours as ei_hook+offset).
// First-chance, so exceptions the game handles itself show up too; only the first few are logged.
std::uintptr_t g_selfBase, g_selfEnd;

void LogCodeAddress(const char* label, std::uintptr_t a) {
    if (a >= 0x00401000 && a < 0x00730000) Log("  %s %08X (game.exe)", label, unsigned(a));
    else if (a >= g_selfBase && a < g_selfEnd) Log("  %s ei_hook+%X", label, unsigned(a - g_selfBase));
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

template <class T>
void PatchSlot(std::uintptr_t address, T replacement, T& original) {
    auto* slot = reinterpret_cast<T*>(address);
    DWORD old;
    VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old);
    original = *slot;
    *slot = replacement;
    VirtualProtect(slot, sizeof(*slot), old, &old);
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
    const char* folder = *reinterpret_cast<const char* const*>(0x007AF460);   // CString data, may be empty
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
    return nullptr;
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

// For a read: the overlay copy when it exists, else `path` (saves always redirect).
const char* ReadPath(const char* path, char (&buffer)[MAX_PATH]) {
    const char* saves = SavesPath(path, buffer);
    if (saves != path) return saves;
    const char* shadow = ShadowOf(path, buffer);
    if (shadow && g_getFileAttributes(shadow) != INVALID_FILE_ATTRIBUTES) return shadow;
    return path;
}

// For a write: the overlay path (its directories created; with `keep` an install file not yet in
// the overlay is copied first).
const char* WritePath(const char* path, char (&buffer)[MAX_PATH], bool keep) {
    const char* saves = SavesPath(path, buffer);
    if (saves != path) return saves;
    const char* shadow = ShadowOf(path, buffer);
    if (!shadow) return path;
    MakeParentDirs(shadow);
    if (keep && g_getFileAttributes(shadow) == INVALID_FILE_ATTRIBUTES &&
        g_getFileAttributes(path) != INVALID_FILE_ATTRIBUTES)
        g_copyFile(path, shadow, TRUE);
    LogInstallWrite(path, shadow);
    return shadow;
}

HANDLE WINAPI RedirectedCreateFile(LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa,
                                   DWORD disposition, DWORD flags, HANDLE templ) {
    char b[MAX_PATH];
    constexpr DWORD kWriteAccess = GENERIC_WRITE | GENERIC_ALL | FILE_WRITE_DATA | FILE_APPEND_DATA | DELETE |
                                   FILE_WRITE_ATTRIBUTES | FILE_WRITE_EA;
    bool write = (access & kWriteAccess) != 0 || disposition == CREATE_ALWAYS || disposition == CREATE_NEW ||
                 disposition == TRUNCATE_EXISTING || disposition == OPEN_ALWAYS;
    bool keep = disposition == OPEN_EXISTING || disposition == OPEN_ALWAYS;
    const char* target = write ? WritePath(name, b, keep) : ReadPath(name, b);
    // The game opens some data files for reading without sharing them; parallel harness runs read
    // the same install, so reads always let others read too.
    if (!write) share |= FILE_SHARE_READ;
    return g_createFile(target, access, share, sa, disposition, flags, templ);
}

// Registry: harness runs never write the game's settings (it saves its options on exit).
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
int& g_showTutorial = *reinterpret_cast<int*>(0x0079BE78);

// Game speed. The main loop runs one world update per 0078FF34 milliseconds of real time (55 by
// default; the game's own speed-up key, keypad +, sets 27). "speed <ms>" holds it at <ms> every
// frame, since loading a zone resets it, and again after every world update (HeldServerUpdate,
// HeldClientSegment): the battle clock's set-up (00648520) and CBattleScreenSetup::Close (00635E60,
// on leaving a screen such as the camp) set 55 inside the client's tick, and the main loop's client
// loop, which tested its backlog against the held 10 before the tick, subtracts the 55 it reads
// after it: the unsigned backlog (10..54) wraps to about 4.29e9 and the loop runs some 78 million
// client ticks (minutes). Real-time runs mostly got away with it by timing; on the virtual clock
// ui_camp_deals, ui_trade_hold, effects_bow, ... hung every time. 0 = leave it to the game.
unsigned& g_tickLength = *reinterpret_cast<unsigned*>(0x0078FF34);
unsigned g_heldTickLength = 0;

// The main loop's calls of the server's update and the client's tick (thiscall, EAX 0 stops the
// frame), redirected here (the rewritten loop calls through the same CALLs' targets, app_main.cpp).
constexpr std::uintptr_t kServerUpdateCall = 0x00457314;
constexpr std::uintptr_t kServerUpdate = 0x0058DFF0;
constexpr std::uintptr_t kClientSegmentCall = 0x00457368;
constexpr std::uintptr_t kClientSegment = 0x005641F0;
using WorldUpdate = int(__fastcall*)(void*, void*);
int __fastcall HeldServerUpdate(void* server, void*) {
    const int result = reinterpret_cast<WorldUpdate>(kServerUpdate)(server, nullptr);
    if (g_heldTickLength) g_tickLength = g_heldTickLength;
    digest::AfterWorldUpdate(server);   // a state record when its period says so
    return result;
}
int __fastcall HeldClientSegment(void* client, void*) {
    const int result = reinterpret_cast<WorldUpdate>(kClientSegment)(client, nullptr);
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

void WriteReport() {
    clock::FlushRandomTrace();
    digest::Flush();
    if (census::Enabled()) census::Write();
    FILE* f = nullptr;
    if (fopen_s(&f, g_reportPath, "w") != 0 || !f) return;
    unsigned long mismatches = 0;
    ForEachCount([&](const char* name, unsigned long calls, unsigned long bad) {
        std::fprintf(f, "%-48s %10lu calls %8lu mismatches\n", name, calls, bad);
        mismatches += bad;
    });
    bool raised = false;
    ForEachRaisedPrecision([&](const char* name, unsigned cw, bool mainThread) {
        if (!raised) std::fprintf(f, "\nx87 precision above 24 bits (control word) in:\n");
        raised = true;
        std::fprintf(f, "  %-46s %04X %s\n", name, cw, mainThread ? "main thread" : "other thread");
    });
    if (!g_capturedFrames.empty()) std::fprintf(f, "\nframes captured:\n");
    for (const std::string& frame : g_capturedFrames) std::fprintf(f, "  %s\n", frame.c_str());
    if (std::string digest = digest::Summary(); !digest.empty()) std::fprintf(f, "\nstate digest: %s\n", digest.c_str());
    std::fprintf(f, "\nconsole output:\n");
    for (const std::string& line : g_console) std::fprintf(f, "  %s\n", line.c_str());
    for (const std::string& failure : g_failures) std::fprintf(f, "FAILED: %s\n", failure.c_str());
    std::fprintf(f, "RESULT: %s\n", mismatches || !g_failures.empty() ? "FAIL" : "PASS");
    std::fclose(f);
    Log("harness: report written (%lu mismatches, %u failures)", mismatches, unsigned(g_failures.size()));
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
    reinterpret_cast<void(__fastcall*)(void*, void*, float)>(kCameraUpdate)(camera, nullptr, dt);
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
    reinterpret_cast<SrandFn>(0x006E77EA)(12345);
    auto* results = reinterpret_cast<std::uint32_t*>(static_cast<char*>(isaac) + 8);
    std::uint32_t x = 0x2001C0DE;
    for (int i = 0; i < 256; ++i) results[i] = x = x * 1664525u + 1013904223u;
}

void JumpTo(std::uintptr_t from, const void* to) {
    auto* p = reinterpret_cast<unsigned char*>(from);
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
HWND GameWindow() { return *reinterpret_cast<HWND*>(0x0079BC28); }

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
    auto* ui = reinterpret_cast<CInterface3D*>(0x007C7D50);
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
    }
}

// --- clickunit <name> [l|r|move] [ctrl] (ticket I8) -------------------------------------------
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
__declspec(naked) int __fastcall HarnessObjectIdFromName(const char*) EI_ORIGINAL(0x00461360)
__declspec(naked) void* __fastcall HarnessUnitAtPoint(void* /*client world*/, void*, int* /*x, y*/) EI_ORIGINAL(0x00565E40)
constexpr std::uintptr_t kClientWorld = 0x007AF5B8;
constexpr int kClientPickList = 0xF0;
constexpr std::uint32_t kPickableUnit = 0x400000;   // the flag 00638B30 requires of the unit

// Where a click finds the unit with script name `name` in this frame (client coordinates) and the
// unit the game's pick returns there; "" or why there is no such point. `what` names the step in
// the log and the error (null: nothing logged).
std::string FindUnitPoint(const char* name, const char* what, POINT* point, void** unit) {
    std::string label = std::string(what ? what : "waitunit") + " " + name;
    int id = HarnessObjectIdFromName(name);
    auto* world = reinterpret_cast<std::uint8_t*>(kClientWorld);
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
        void* hit = HarnessUnitAtPoint(world, nullptr, at);
        if (hit && *reinterpret_cast<int*>(static_cast<std::uint8_t*>(hit) + 0xC) == id) {
            *point = p;
            *unit = hit;
            return "";
        }
    }
    return label + ": the game's pick (00565E40) finds another object everywhere in its rectangle";
}

std::string PostUnitMouse(const std::string& argument) {
    char name[64] = "", button[8] = "l", modifier[8] = "";
    if (sscanf_s(argument.c_str(), "%63s %7s %7s", name, unsigned(sizeof(name)), button, unsigned(sizeof(button)),
                 modifier, unsigned(sizeof(modifier))) < 1)
        return "clickunit needs <name> [l|r|move] [ctrl]";
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
    bool ctrl = std::strcmp(modifier, "ctrl") == 0;
    constexpr UINT kCtrlScan = 0x1D;   // CInterface3D::OnKeyDown: +0x7C while down
    if (ctrl) PostMessageA(GameWindow(), WM_KEYDOWN, VK_CONTROL, 1 | (kCtrlScan << 16));
    PostMessageA(GameWindow(), WM_MOUSEMOVE, 0, where);
    if (std::strcmp(button, "l") == 0) {
        PostMessageA(GameWindow(), WM_LBUTTONDOWN, MK_LBUTTON, where);
        PostMessageA(GameWindow(), WM_LBUTTONUP, 0, where);
    } else if (std::strcmp(button, "r") == 0) {
        PostMessageA(GameWindow(), WM_RBUTTONDOWN, MK_RBUTTON, where);
        PostMessageA(GameWindow(), WM_RBUTTONUP, 0, where);
    }
    if (ctrl) PostMessageA(GameWindow(), WM_KEYUP, VK_CONTROL, 1 | (kCtrlScan << 16) | 0xC0000000u);
    return "";
}

// --- hudstate [minquick] (ticket I9) ----------------------------------------------------------
// Logs the battle screen's state (the window on top): the pending action (+0x17C, +0x180), each
// party member's selection (flags_ 0x10) and quick items (CUnit::GetQuickItem 00531920, slots
// 0-3), and the quick item panel's (+0x198) unit, entries shown and region items, the spell
// column's (+0x19C) count and highlight. With <minquick>, fails when the quick item panel shows
// fewer than that many of its unit's own quick items (regions 0-3).
__declspec(naked) void* __fastcall HarnessUnitQuickItem(void* /*unit*/, void*, int) EI_ORIGINAL(0x00531920)

std::string HudState(const std::string& argument) {
    auto* ui = reinterpret_cast<CInterface3D*>(0x007C7D50);
    auto* screen = reinterpret_cast<std::uint8_t*>(ui->GetActiveWindow());
    if (!screen) return "hudstate: no window on top";
    auto at = [](const void* base, int offset) {
        return *reinterpret_cast<const int*>(static_cast<const std::uint8_t*>(base) + offset);
    };
    auto* battle = reinterpret_cast<CBattleScreen*>(screen);
    Log("harness: hudstate: window %p, mode %d, slot %d, item %d, %d members", screen, at(screen, 0x17C),
        at(screen, 0x180), at(screen, 0x184), battle->partyCount_);
    for (int i = 0; i < battle->partyCount_ && i < 8; ++i) {
        CUnit* member = battle->party_[i];
        if (!member) continue;
        void* quick[4];
        for (int s = 0; s < 4; ++s) quick[s] = HarnessUnitQuickItem(member, nullptr, s);
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
    auto* ui = reinterpret_cast<CInterface3D*>(0x007C7D50);
    auto* camp = reinterpret_cast<std::uint8_t*>(ui->GetActiveWindow());
    if (!camp || *reinterpret_cast<void**>(camp) != reinterpret_cast<void*>(0x0074091C))
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
    {"menu", {0x00740688, 0x00740654}},         // CMainMenu (CreateScreen 2)
    {"battle", {0x00740764, 0x00740730}},       // the in-game screen (CreateScreen 1)
    {"briefing", {0x007405AC, 0x00740578}},     // the base / briefing screen (CreateScreen 3)
    {"map", {0x00740B08, 0x00740B3C}},          // the island map (CreateScreen 4)
    {"camp", {0x007408E8, 0x0074091C}},         // CCampScreen (CreateScreen 5)
    {"view", {0x00740840, 0x0074080C}},         // CViewScreen (CreateScreen 7)
    {"trade", {0x00740D18, 0x00740CE4}},        // trading between players (CreateScreen 9)
    {"escmenu", {0x00741270, 0x007412A4}},      // CEscMenu
    {"saveload", {0x00741388, 0x00741354}},     // CSaveLoadWindow
    {"options", {0x00741FAC, 0x00741F78}},      // COptionsWindow
    {"objectives", {0x007411BC, 0x00741188}},   // CObjectivesWindow
    {"help", {0x00740E28, 0x00740DF4}},         // the help / message window
    {"msgbox", {0x00742348, 0x00742314}},       // CMessageBox
    {"difficulty", {0x0074225C, 0x00742228}},   // CDifficultyBox
    {"heroes", {0x00742118, 0x0074214C}},       // the multiplayer hero screen (main menu region 5)
    {"titles", {0x00740C08, 0x00740C3C}},       // CCreditsScreen
};

// The vtables of `name` (a name above or one vtable in hex); false when unknown.
bool WindowVtables(const std::string& name, std::uintptr_t (&vtables)[2]) {
    for (const KnownWindow& w : kKnownWindows) {
        if (name == w.name) {
            vtables[0] = w.vtables[0];
            vtables[1] = w.vtables[1];
            return true;
        }
    }
    char* end = nullptr;
    unsigned long value = std::strtoul(name.c_str(), &end, 16);
    if (name.empty() || *end || value < 0x00401000 || value >= 0x00800000) return false;
    vtables[0] = vtables[1] = value;
    return true;
}

// Whether the awaited window is on top and takes input (see the step's description).
bool WindowReady(std::uintptr_t (&vtables)[2]) {
    auto* ui = reinterpret_cast<CInterface3D*>(0x007C7D50);
    CUiWindow* top = ui->GetActiveWindow();
    if (!top || ui->queuedWindow_ || g_request.kind != 0) return false;
    std::uintptr_t vtable = *reinterpret_cast<const std::uintptr_t*>(top);
    if (vtable != vtables[0] && vtable != vtables[1]) return false;
    if (vtable == 0x00741388 || vtable == 0x00741354) return !reinterpret_cast<CSaveLoadWindow*>(top)->sliding_;
    if (vtable == 0x00741FAC || vtable == 0x00741F78) return !reinterpret_cast<COptionsWindow*>(top)->sliding_;
    return true;
}

// For failure messages: the window on top and what keeps it from counting.
std::string DescribeTopWindow() {
    auto* ui = reinterpret_cast<CInterface3D*>(0x007C7D50);
    CUiWindow* top = ui->GetActiveWindow();
    std::uintptr_t vtable = top ? *reinterpret_cast<const std::uintptr_t*>(top) : 0;
    const char* name = "?";
    for (const KnownWindow& w : kKnownWindows)
        if (vtable == w.vtables[0] || vtable == w.vtables[1]) name = w.name;
    char text[200];
    std::snprintf(text, sizeof(text), "top window %p vtable %08X (%s), queued %p, request %d, campaign mode %d", top,
                  unsigned(vtable), top ? name : "none", ui->queuedWindow_, g_request.kind,
                  *reinterpret_cast<int*>(kCampaignMode));
    return text;
}

// The steps that post input messages to the game window.
bool IsInputStep(const std::string& verb) {
    return verb == "key" || verb == "click" || verb == "rclick" || verb == "move" || verb == "wheel" ||
           verb == "dblclick" || verb == "rdblclick" || verb == "ldown" || verb == "lup" || verb == "rdown" || verb == "rup" ||
           verb == "clickregion" || verb == "rclickregion" || verb == "moveregion" || verb == "clickunit" ||
           verb == "focus" || verb == "windowsize";
}

// Unhandled exceptions end the run with a failure. Installed at start and re-asserted every step:
// the game (and Rebirth) install their own filters, which can leave a crashed game hanging.
LONG WINAPI HarnessUnhandled(EXCEPTION_POINTERS* e) {
    char what[96];
    std::snprintf(what, sizeof(what), "the game crashed: exception %08lX at %08lX (stack in ei_hook.log)",
                  e->ExceptionRecord->ExceptionCode, static_cast<unsigned long>(e->ContextRecord->Eip));
    Log("harness: %s", what);
    g_failures.push_back(what);
    if (HANDLE writer = CreateThread(nullptr, 0, [](void*) -> DWORD { WriteReport(); return 0; }, nullptr, 0, nullptr)) {
        WaitForSingleObject(writer, 3000);
        CloseHandle(writer);
    }
    TerminateProcess(GetCurrentProcess(), 3);
    return EXCEPTION_EXECUTE_HANDLER;
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
// becomes a JMP to the replacement and the first two bytes a short jump back to it.
void HotPatch(const char* name, void* replacement) {
    auto* p = reinterpret_cast<unsigned char*>(GetProcAddress(GetModuleHandleA("user32.dll"), name));
    if (!p || p[0] != 0x8B || p[1] != 0xFF) {
        Log("harness: cannot hot-patch %s (unexpected prologue)", name);
        return;
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
}

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
        if (g_inputLoop && g_request.kind == 0 && *reinterpret_cast<int*>(kCampaignMode) != 2 &&
            _stricmp(ServerZoneName(), g_waitZoneName.c_str()) == 0) {
            void* connection = LocalConnection();
            ready = connection && ConnectionState(connection) == 1;
        }
        if (ready) {
            Log("harness: waitzone %s done after %lu ms (campaign mode %d)", g_waitZoneName.c_str(),
                Now() - g_waitZoneDeadline.start, *reinterpret_cast<int*>(kCampaignMode));
            g_waitingZone = false;
        } else if (g_waitZoneDeadline.Passed()) {
            char what[500];
            std::snprintf(what, sizeof(what), "waitzone %s timed out (zone '%s', request %d, campaign mode %d, %s)%s",
                          g_waitZoneName.c_str(), ServerZoneName(), g_request.kind, *reinterpret_cast<int*>(kCampaignMode),
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
        if (now.deviceRecoveries > g_newDeviceFrom && presented && !now.failed) {
            Log("harness: losedevice: a new Direct3D 11 device (%d lost, %d made again)%s", now.deviceLosses,
                now.deviceRecoveries, render::PresentsItself() ? " presents on the game window" : "");
            g_waitingNewDevice = false;
        } else if (now.failed || !render::Active() || Now() >= g_newDeviceDeadline) {
            char what[200];
            std::snprintf(what, sizeof(what), "losedevice: %s (%d lost, %d made again, %u presented since)",
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
            const CScreenFade* fade = reinterpret_cast<CInterface3D*>(0x007C7D50)->fade_;
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
    while (g_next < g_steps.size()) {
        if (!g_waitUntil.Passed()) return;
        if (std::string why = g_waitUntil.Why(); !why.empty()) {
            g_failures.push_back("wait" + why);
            g_waitUntil = Deadline{};
        }
        Step s = g_steps[g_next++];
        // "vanilla <step>" / "rebirth <step>": only in that install (Rebirth changes some behaviour
        // itself, e.g. script error messages). Rebirth is recognised by its addon.dll.
        if (s.verb == "vanilla" || s.verb == "rebirth") {
            bool rebirth = GetModuleHandleA("addon.dll") != nullptr;
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
        } else if (s.verb == "console") {
            ConsoleExecute(s.argument.c_str());
        } else if (s.verb == "newgame") {
            // What the main menu's new-game button does after the intro movie (00632940).
            *reinterpret_cast<unsigned char*>(0x00784750) = 0;
            g_request.kind = 3;
        } else if (s.verb == "load") {
            g_request.argument = s.argument.c_str();
            g_request.kind = 2;
        } else if (s.verb == "key") {
            PostKey(s.argument);
        } else if (s.verb == "click" || s.verb == "rclick" || s.verb == "move" || s.verb == "dblclick" ||
                   s.verb == "rdblclick" || s.verb == "ldown" || s.verb == "lup" || s.verb == "rdown" || s.verb == "rup") {
            int x = 0, y = 0;
            if (sscanf_s(s.argument.c_str(), "%d %d", &x, &y) == 2) PostMouse(s.verb, x, y);
            else Log("harness: %s needs <x> <y>", s.verb.c_str());
        } else if (s.verb == "wheel") {
            int x = 0, y = 0, delta = 0;
            if (sscanf_s(s.argument.c_str(), "%d %d %d", &x, &y, &delta) == 3) PostWheel(x, y, delta);
            else Log("harness: wheel needs <x> <y> <delta>");
        } else if (s.verb == "clickregion" || s.verb == "rclickregion" || s.verb == "moveregion") {
            PostRegionMouse(s.verb, std::atoi(s.argument.c_str()));
        } else if (s.verb == "hudstate" || s.verb == "campputon") {
            std::string error = s.verb == "hudstate" ? HudState(s.argument) : CampPutOn(s.argument);
            if (!error.empty()) {
                Log("harness: %s", error.c_str());
                g_failures.push_back(error);
            }
        } else if (s.verb == "clickunit") {
            std::string error = PostUnitMouse(s.argument);
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
                !FindClassTemplate(reinterpret_cast<void*>(kClassTemplates), nullptr, constr)) {
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
            auto* ui = reinterpret_cast<CInterface3D*>(0x007C7D50);
            if (!ui->root_) {
                g_failures.push_back("chat: the interface has no chat overlay");
            } else if (std::atoi(s.argument.c_str()) != 0) {
                ChatKeyEnterOriginal(ui);
            } else {
                ui->root_->SetOpen(false);
            }
        } else if (s.verb == "zone") {
            // What a zone exit does (00670C20): the server's LeaveToZone for the local player.
            char name[64] = "";
            int exitIndex = 0;
            if (sscanf_s(s.argument.c_str(), "%63s %d", name, unsigned(sizeof(name)), &exitIndex) < 1) {
                g_failures.push_back("zone needs <name> [exit]");
                continue;
            }
            const std::uint8_t* record = MapDatabaseFind(reinterpret_cast<void*>(kMapDatabase), nullptr, name);
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
                LeaveToZoneOriginal(connection, nullptr, name, exitIndex);
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
        } else if (s.verb == "record") {
            *reinterpret_cast<unsigned char*>(0x0079BC54) = s.argument == "on" ? 1 : 0;
        } else if (s.verb == "oldterrain") {
            render::SetDrawOldTerrain(s.argument != "off");
        } else if (s.verb == "oldobjects") {
            render::SetDrawOldObjects(s.argument != "off");
        } else if (s.verb == "oldinterface") {
            render::SetDrawOldInterface(s.argument != "off");
        } else if (s.verb == "waitpresent") {
            int seconds = 10;
            char what[16] = "";
            sscanf_s(s.argument.c_str(), "%d %15s", &seconds, what, unsigned(sizeof(what)));
            g_waitPresentFrom = render::WindowPresents().presents;
            g_waitPresentGamma = std::strcmp(what, "gamma") == 0;
            g_waitPresentDeadline.Set(static_cast<DWORD>(seconds) * 1000);
            g_waitingPresent = true;
            return;
        } else if (s.verb == "losedevice") {
            int seconds = 10;
            sscanf_s(s.argument.c_str(), "%d", &seconds);
            if (!render::Active()) {
                g_failures.push_back("losedevice: the new renderer is not on");
                continue;
            }
            g_newDeviceFrom = render::WindowPresents().deviceRecoveries;
            render::LoseDevice();
            g_newDeviceDeadline = Now() + static_cast<DWORD>(seconds) * 1000;
            g_waitingNewDevice = true;
            return;
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
        } else if (s.verb == "report") {
            WriteReport();
        } else if (s.verb == "quit") {
            WriteReport();
            // The game can hang on the way out (Rebirth does). The quit console command calls exit(),
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
// gets 5 s to exit by itself (under Rebirth it hangs on the way out). A scenario that has not
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
    for (;;) {
        Sleep(250);
        if (g_watchdogStopped) Sleep(INFINITE);
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
// A game that Windows gave the foreground (when the window in front closed) and that then loses it
// to a starting game is minimised by dgVoodoo, in the middle of a pass, and its surfaces are lost:
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
        Log("harness: the game window was minimised (it had the foreground and lost it): shown again");
    }
    CTextureDevice* device = RenderDeviceObject();
    if (device && device->ddraw_) {
        auto* lock = reinterpret_cast<CRITICAL_SECTION*>(0x007AEE78);   // the render device's lock
        EnterCriticalSection(lock);
        HRESULT result = static_cast<IDirectDraw7*>(device->ddraw_)->RestoreAllSurfaces();
        device->lastLoadResult_ = result;
        LeaveCriticalSection(lock);
        if (result == DDERR_NOEXCLUSIVEMODE) {
            // A second minimise soon after the first also takes the exclusive mode away, and getting
            // it back (the renderer's restore after a switch back, 00504780) hangs while the window
            // is in the background: the run ends on the game's lost-surface box, and run_all.py runs
            // it again.
            Log("harness: the exclusive mode was lost with the foreground; the run ends on the game's lost-surface "
                "box and run_all.py runs it again");
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
    return CallWindowProcA(g_gameWindowProc, window, message, wParam, lParam);
}

// The window is shown and made without activating it (ShowWithoutActivating); under dgVoodoo its
// exclusive cooperative level activates the window all the same (WM_ACTIVATE, which sets the game's
// active flag 0079BC5C and starts its frames). Without DirectDraw (the switch-over, device.h) nothing
// does, and the game never draws a frame: once the device object exists and the game is not active,
// the harness sends the activation a foreground start would have given (ticket R9a2).
void ActivateWithoutDirectDraw(HWND window) {
    static bool sent = false;
    if (sent || !device::WithoutDirectDraw() || !*reinterpret_cast<void**>(0x007AF080)) return;
    sent = true;
    if (*reinterpret_cast<std::uint8_t*>(0x0079BC5C)) return;
    Log("harness: the game window activated (the switch-over has no DirectDraw to activate it)");
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
    if (!g_gameWindowProc) {
        g_gameWindowProc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrA(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&AlwaysActiveWindowProc)));
        Log("harness: the game window stays active when it loses focus");
    }
    ActivateWithoutDirectDraw(window);
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

BOOL WINAPI PeekMessageHook(LPMSG msg, HWND hwnd, UINT min, UINT max, UINT remove) {
    SetMainThread();
    if (g_randomTrace) clock::InstallRandomTrace(&g_frames);
    TraceFpu("in the message loop");
    KeepGameWindowActive();
    // A frame has been drawn: the original's after-frame call (00457610) or the rewritten loop's.
    const bool afterFrame = reinterpret_cast<std::uintptr_t>(_ReturnAddress()) == kInputLoopReturns[1] ||
                            ei::mainloop::g_site == ei::mainloop::Site::PeekAfterFrame;
    if (afterFrame) {
        ++g_frames;
        if (census::Enabled()) census::Frame();
    }
    // On the virtual clock the steps run once a frame, after it: how often the game peeks besides
    // depends on the real messages it gets (Miles' 50 ms WM_TIMER, WM_PAINT), and a step that
    // follows a console command's output would otherwise run in this frame or the next.
    if (g_running && !g_inStep && (afterFrame || !clock::Virtual())) {
        g_inStep = true;   // console commands can pump messages themselves
        g_inputLoop = IsInputLoop(_ReturnAddress());
        g_winMainLoop = IsWinMainLoop(_ReturnAddress());
        RunSteps();
        g_inStep = false;
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
        if (g_steps.back().verb == "uicensus") {
            census::Enable();
            g_steps.pop_back();
            continue;
        }
        if (g_steps.back().verb == "render") {
            render::Configure(g_steps.back().argument.c_str());
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
        HotPatch("MessageBoxA", reinterpret_cast<void*>(&HarnessMessageBoxA));
        HotPatch("MessageBoxW", reinterpret_cast<void*>(&HarnessMessageBoxW));
        // A crash must end the run: no Windows Error Reporting dialog, which freezes every thread
        // (the watchdog included) until someone closes it.
        SetErrorMode(GetErrorMode() | SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);

        strcpy_s(g_savesDir, g_reportPath);
        if (char* slash = std::strrchr(g_savesDir, '\\')) strcpy_s(slash + 1, g_savesDir + MAX_PATH - (slash + 1), "harness_saves");
        CreateDirectoryA(g_savesDir, nullptr);
        if (!GetCurrentDirectoryA(MAX_PATH, g_rootDirs[0])) g_rootDirs[0][0] = '\0';

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
        ei::verify::starter::Install();
        net_port::Install();   // a UDP port of its own (harness_net.h)
        digest::Init(&g_frames);   // EI_DIGEST
        if (census::Enabled()) census::Install();   // "uicensus" or EI_UI_CENSUS (ui_census.cpp)
        PatchSlot(kAilWaveOutOpenSlot, reinterpret_cast<AilWaveOutOpenFn>(&MutedWaveOutOpen), g_ailWaveOutOpen);
        PatchSlot(kPeekMessageASlot, &PeekMessageHook, g_peekMessage);
        PatchSlot(kAilStartupSlot, &TracedAilStartup, g_ailStartup);
        PatchSlot(kConsoleStreambufSyncSlot, reinterpret_cast<SyncFn>(&ConsoleSync), g_sync);
        PatchSlot(kCreateFileASlot, &RedirectedCreateFile, g_createFile);
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
        if (g_deterministic) {
            clock::InstallVirtual(&g_tickLength);
            if (!g_keepHeap) heap::Install();   // the CRT's heap gives the same addresses every run (harness_heap.h)
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
        if (g_freezeUsed) RedirectCall(kFrameCameraUpdateCall, kCameraUpdate, reinterpret_cast<const void*>(&HeldCameraUpdate));
        RedirectCall(kServerUpdateCall, kServerUpdate, reinterpret_cast<const void*>(&HeldServerUpdate));
        RedirectCall(kClientSegmentCall, kClientSegment, reinterpret_cast<const void*>(&HeldClientSegment));
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

} // namespace ei::verify
