// Automated test harness (verify build only).
//
// When ei_launch runs with --harness <scenario>, it sets EI_HARNESS to the scenario path. The
// scenario is a text file of steps, run on the game's main thread from its message loop:
//
//   wait <milliseconds>       let the game run
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
//   wheel <x> <y> <delta>     turn the mouse wheel by <delta> (120 a notch; negative: towards the
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
//   screen <id> [constr]      ask the server for interface screen <id> as the global map's buttons
//                             do (4 the global map, 5 the camp; the battle screen is left). For the
//                             camp, <constr> (default 0: none) is the constructor / trader class it
//                             opens with, as a base dialogue's "constrN" answer (006064E0) sets it
//                             (classes 1, 3, 4, 5 after a new game); fails when there is no such
//                             class template
//   zone <name> [exit]        leave the zone as its exits do: the server's LeaveToZone (0066F310) for
//                             the local player, to map.txt zone <name> (gz2g, bz1g, ...), the party
//                             arriving at that zone's exit <exit> (from 0: map.txt's "#exit n" minus
//                             1; default 0). Needs the hero alive and in the world; fails otherwise
//   waitzone <seconds> <name> wait until zone <name> is loaded and the local player is in its world
//                             again (game and brief zones; the party's units come a little later)
//   check <expression>        print the script expression through the console ("#<expression>")
//                             and require a nonzero result (waits up to 3 s for the output)
//   timeout <seconds>         the whole scenario's time limit (default 60 s); past it the run
//                             fails and the game is closed
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
//   deterministic             virtual game clock, fixed seeds: runs repeat exactly (see below)
//   vanilla <step>, rebirth <step>   run the step only in that install
//   rebirthflags <spec>       turn Rebirth patch flags on for this run (include/ei/rebirth.h): "all",
//                             or patch names, each optionally =0/=1. Read before the hooks are
//                             installed; in a Rebirth launch the flags follow addon.dll anyway.
//   capture <name>            write the frame presented after the next two to frames\<name>.bmp in
//                             the output folder (the render target before the software cursor and
//                             the flip; harness_capture.cpp); fails after 10 s without a frame
//   freeze, unfreeze          hold the game's clocks (timeGetTime, GetTickCount) and the camera for
//                             captures: no world, client or camera updates, no clock-driven
//                             animation, while the frames keep being drawn. Do not "check"/"waitfor"
//                             while frozen (script output needs world updates); a scenario that
//                             freezes also sees a fixed date (as in deterministic mode)
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
//   # comment
//
// Everything the console prints is captured into the report. The report ends with "RESULT: PASS"
// (no verify mismatches, failed checks or rejected output) or "RESULT: FAIL"; ei_launch --wait turns
// that into its exit code.
//
// Harness runs are hands-off and leave the install alone: no startup movies, the tutorial option
// off, saves and config\tutorial.ini kept in harness_saves next to the DLL, every other write under
// the game's root (the composed-skin cache textures\*.mmp, config\keyboard.ini, ...) going to a
// copy-on-write overlay there too, no registry writes.
// A watchdog fails and ends a run that stops making progress (e.g. a message box), and ei_launch
// ends a game that hangs after "quit".
#include "verify.h"
#include "harness_capture.h"

#include "../../src/hook/log.h"
#include "ei/rebirth.h"
#include "../../src/lib/mfc.h"
#include "../../src/game/ui.h"
#include "../../src/game/originals_engine.h"
#include "../../src/game/originals_world.h"
#include "../../src/render/render.h"

#include <windows.h>
#include <psapi.h>
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

// 00578B00: the client asks the server for interface screen `id` (the server answers with message 7,
// 00669D50 -> CInterface3D::CreateScreen 005F5BA0: 1 battle, 3 briefing, 4 global map, 5 camp).
__declspec(naked) void __stdcall RequestScreen(int) EI_ORIGINAL(0x00578B00)
// 00578560: sets player 0's "constr_current" (the constructor the camp opens with; 0 none).
__declspec(naked) void __stdcall SetCurrentConstructor(float) EI_ORIGINAL(0x00578560)
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
DWORD g_waitUntil = 0;
volatile bool g_quitting = false;
bool g_running = false;
bool g_inStep = false;
char g_reportPath[MAX_PATH];
// Whether the PeekMessageA call running the steps is one that hands input to the game (see
// PeekMessageHook): "key", mouse and region steps wait for one.
bool g_inputLoop = false;

std::vector<std::string> g_console;          // every console line so far
std::size_t g_expectFrom = 0;                // first line "expect" looks at
std::vector<std::string> g_rejected;         // texts that must not appear
std::vector<std::string> g_failures;
// A pending "check": the console line index its output starts at, and when to give up.
bool g_checking = false;
std::string g_checkExpr;
std::size_t g_checkFrom = 0;
DWORD g_checkDeadline = 0;
// A pending "waitfor": the expression, when it was last evaluated (its output starts at
// g_waitForFrom), when to evaluate it next, and when to give up.
// A pending "waitline": the text to find anywhere in the console output so far, and when to give up.
bool g_waitingLine = false;
std::string g_waitLineText;
DWORD g_waitLineDeadline = 0;
std::size_t g_waitLineFrom = 0;   // waitline: 0 (any time); waitexpect: since the last expect
bool g_waitLineIsExpect = false;
// A pending "waitzone": the zone and when to give up.
bool g_waitingZone = false;
std::string g_waitZoneName;
DWORD g_waitZoneDeadline = 0;
DWORD g_waitZoneStart = 0;
bool g_waitingFor = false;
bool g_waitForPending = false;
std::string g_waitForExpr;
std::size_t g_waitForFrom = 0;
DWORD g_waitForNext = 0;
DWORD g_waitForDeadline = 0;
DWORD g_waitForStart = 0;
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

// The "show tutorial" option (options object 0079BC70 + 0x208, registry value ShowTutorial). With it
// off, tutorial windows open only where the game forces them (005F86F0, 005F8A80). The harness
// clears it every frame, since the options are reloaded when a game starts.
int& g_showTutorial = *reinterpret_cast<int*>(0x0079BE78);

// Game speed. The main loop runs one world update per 0078FF34 milliseconds of real time (55 by
// default; the game's own speed-up key, keypad +, sets 27). "speed <ms>" holds it at <ms> every
// frame, since loading a zone resets it. 0 = leave it to the game.
unsigned& g_tickLength = *reinterpret_cast<unsigned*>(0x0078FF34);
unsigned g_heldTickLength = 0;

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
    std::fprintf(f, "\nconsole output:\n");
    for (const std::string& line : g_console) std::fprintf(f, "  %s\n", line.c_str());
    for (const std::string& failure : g_failures) std::fprintf(f, "FAILED: %s\n", failure.c_str());
    std::fprintf(f, "RESULT: %s\n", mismatches || !g_failures.empty() ? "FAIL" : "PASS");
    std::fclose(f);
    Log("harness: report written (%lu mismatches, %u failures)", mismatches, unsigned(g_failures.size()));
}

// Deterministic mode (a "deterministic" line anywhere in the scenario). The game's clock becomes
// virtual: timeGetTime returns a counter that starts at a fixed value and advances by four world
// updates each time the main loop reads it (004571DC), so every frame runs exactly four updates,
// whatever the real frame rate. The random generators are seeded from timeGetTime too, so two runs
// of the same scenario take the same course, and a run with and without hooks (tools/compare.py)
// must print the same console output. The harness's own waits use the same clock.
// EXPERIMENTAL: runs do not repeat yet. Zone loading still depends on real time (a new game sometimes
// never finishes loading under the virtual clock) and the sound thread, which feeds AI hearing,
// runs on its own; see docs/open-questions.md.
constexpr std::uintptr_t kTimeGetTimeSlot = 0x0073B6A0;      // game.exe import table
constexpr std::uintptr_t kMainLoopTimeReturn = 0x004571E2;   // after the main loop's timeGetTime call
constexpr unsigned kUpdatesPerFrame = 4;                     // the main loop runs at most 5 per frame
bool g_deterministic = false;
volatile DWORD g_virtualNow = 1000000;
volatile unsigned g_callsSinceFrame = 0;
decltype(&::timeGetTime) g_timeGetTime;
volatile bool g_frozen = false;   // "freeze" (see HeldClock below)

DWORD WINAPI VirtualTimeGetTime() {
    if (g_frozen) return g_virtualNow;
    if (reinterpret_cast<std::uintptr_t>(_ReturnAddress()) == kMainLoopTimeReturn) {
        g_virtualNow += kUpdatesPerFrame * g_tickLength;
        g_callsSinceFrame = 0;
    } else if (++g_callsSinceFrame % 10000 == 0) {
        ++g_virtualNow;   // a spin-wait outside the main loop (loading screens) still sees time pass
    }
    return g_virtualNow;
}

// The other clocks the game reads: GetTickCount (network, UI, sound) follows the virtual clock;
// GetLocalTime/GetSystemTime (the main menu's time of day, save dates) return a fixed date.
constexpr std::uintptr_t kGetTickCountSlot = 0x0073B344;
constexpr std::uintptr_t kGetLocalTimeSlot = 0x0073B1DC;
constexpr std::uintptr_t kGetSystemTimeSlot = 0x0073B228;
decltype(&::GetTickCount) g_getTickCount;
decltype(&::GetLocalTime) g_getLocalTime;
decltype(&::GetSystemTime) g_getSystemTime;
DWORD WINAPI VirtualGetTickCount() { return g_virtualNow; }
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
struct HeldClock {
    DWORD(WINAPI* base)() = nullptr;
    DWORD offset = 0;   // the game's time is base() - offset while running
    DWORD heldAt = 0;
    DWORD Read() const { return g_frozen ? heldAt : base() - offset; }
    void Hold() { heldAt = base() - offset; }
    void Resume() { offset = base() - heldAt; }
};
HeldClock g_heldTimeGetTime, g_heldTickCount;
DWORD WINAPI HeldTimeGetTime() { return g_heldTimeGetTime.Read(); }
DWORD WINAPI HeldGetTickCount() { return g_heldTickCount.Read(); }

void Freeze(bool on) {
    if (on == g_frozen) return;
    if (!g_deterministic && g_heldTimeGetTime.base) {
        if (on) {
            g_heldTimeGetTime.Hold();
            g_heldTickCount.Hold();
        } else {
            g_heldTimeGetTime.Resume();
            g_heldTickCount.Resume();
        }
    }
    g_frozen = on;
}

// The main loop's per-frame camera update: CALL 0040FB30 at 00457595 (thiscall on the camera
// 0079B2A8, one float, RET 4), redirected here when the scenario freezes.
constexpr std::uintptr_t kFrameCameraUpdateCall = 0x00457595;
constexpr std::uintptr_t kCameraUpdate = 0x0040FB30;
void __fastcall HeldCameraUpdate(void* camera, void*, float dt) {
    if (g_frozen) return;
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
DWORD Now() { return g_deterministic ? g_virtualNow : GetTickCount(); }

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

void PostMouse(const std::string& verb, int x, int y) {
    RECT client{};
    GetClientRect(GameWindow(), &client);
    LPARAM at = MAKELPARAM(x * (client.right - client.left) / 800, y * (client.bottom - client.top) / 600);
    g_mouseX = x * (client.right - client.left) / 800;
    g_mouseY = y * (client.bottom - client.top) / 600;
    PostMessageA(GameWindow(), WM_MOUSEMOVE, 0, at);
    if (verb == "click") {
        PostMessageA(GameWindow(), WM_LBUTTONDOWN, MK_LBUTTON, at);
        PostMessageA(GameWindow(), WM_LBUTTONUP, 0, at);
    } else if (verb == "rclick") {
        PostMessageA(GameWindow(), WM_RBUTTONDOWN, MK_RBUTTON, at);
        PostMessageA(GameWindow(), WM_RBUTTONUP, 0, at);
    }
}

// A wheel turn at (x, y) in the 800x600 layout. WM_MOUSEWHEEL carries screen coordinates.
void PostWheel(int x, int y, int delta) {
    RECT client{};
    GetClientRect(GameWindow(), &client);
    POINT point{x * (client.right - client.left) / 800, y * (client.bottom - client.top) / 600};
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
    if (!window || index < 0 || index >= window->regions_.size_ || !window->regions_.data_[index]) {
        Log("harness: %s %d: no such region (window %p, %d regions)", verb.c_str(), index, window,
            window ? window->regions_.size_ : 0);
        return;
    }
    const CUiRegion* region = window->regions_.data_[index];
    POINT point{(region->bounds_.left + region->bounds_.right) / 2 + region->offsetX_,
                (region->bounds_.top + region->bounds_.bottom) / 2 + region->offsetY_};
    Log("harness: %s %d: window %p region (%d,%d)-(%d,%d) enabled %d", verb.c_str(), index, window,
        region->bounds_.left, region->bounds_.top, region->bounds_.right, region->bounds_.bottom, region->enabled_);
    ui->CallScalePointToScreen(&point);
    RECT client{};
    GetClientRect(GameWindow(), &client);
    if (ui->width_ > 0 && ui->height_ > 0) {
        point.x = point.x * (client.right - client.left) / ui->width_;
        point.y = point.y * (client.bottom - client.top) / ui->height_;
    }
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

std::string PostUnitMouse(const std::string& argument) {
    char name[64] = "", button[8] = "l", modifier[8] = "";
    if (sscanf_s(argument.c_str(), "%63s %7s %7s", name, unsigned(sizeof(name)), button, unsigned(sizeof(button)),
                 modifier, unsigned(sizeof(modifier))) < 1)
        return "clickunit needs <name> [l|r|move] [ctrl]";
    std::string what = std::string("clickunit ") + name;
    int id = HarnessObjectIdFromName(name);
    auto* world = reinterpret_cast<std::uint8_t*>(kClientWorld);
    auto* header = *reinterpret_cast<std::uint8_t**>(world + kClientPickList);
    if (!header) return what + ": no pick list (not in a game)";
    // The unit's rectangle in this frame's list (the last entry for the id).
    const RECT* rect = nullptr;
    for (auto* node = *reinterpret_cast<std::uint8_t**>(header); node && node != header;
         node = *reinterpret_cast<std::uint8_t**>(node)) {
        if (*reinterpret_cast<int*>(node + 0xC) == id) rect = reinterpret_cast<const RECT*>(node + 0x10);
    }
    if (!rect) return what + " (id " + std::to_string(id) + "): not drawn this frame (off screen?)";
    Log("harness: %s: id %d, rectangle (%ld,%ld)-(%ld,%ld)", what.c_str(), id, rect->left, rect->top, rect->right,
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
    void* hit = nullptr;
    POINT at{};
    for (const POINT& p : candidates) {
        int point[2] = {p.x, p.y};
        hit = HarnessUnitAtPoint(world, nullptr, point);
        if (hit && *reinterpret_cast<int*>(static_cast<std::uint8_t*>(hit) + 0xC) == id) {
            at = p;
            break;
        }
        hit = nullptr;
    }
    if (!hit) return what + ": the game's pick (00565E40) finds another object everywhere in its rectangle";
    std::uint32_t flags = *reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(hit) + 8);
    RECT client{};
    GetClientRect(GameWindow(), &client);
    Log("harness: %s: at (%ld,%ld) (client %ldx%ld), flags %08X%s", what.c_str(), at.x, at.y, client.right - client.left,
        client.bottom - client.top, flags, (flags & kPickableUnit) ? "" : " (no 0x400000: the battle screen ignores it)");
    // The battle screen reads the message's coordinates as they are (the pick space is the client
    // area); the harness's cursor follows them.
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

// The steps that post input messages to the game window.
bool IsInputStep(const std::string& verb) {
    return verb == "key" || verb == "click" || verb == "rclick" || verb == "move" || verb == "wheel" ||
           verb == "clickregion" || verb == "rclickregion" || verb == "moveregion" || verb == "clickunit";
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
        } else if (Now() >= g_checkDeadline) {
            Log("harness: check %s printed nothing", g_checkExpr.c_str());
            g_failures.push_back("check " + g_checkExpr + " printed nothing");
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
                Log("harness: waitfor %s done after %lu ms", g_waitForExpr.c_str(), now - g_waitForStart);
                g_waitingFor = false;
            } else {
                g_waitForNext = now + 250;
            }
        }
        if (g_waitingFor && now >= g_waitForDeadline) {
            Log("harness: waitfor %s timed out", g_waitForExpr.c_str());
            g_failures.push_back("waitfor " + g_waitForExpr + " timed out");
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
        } else if (Now() >= g_waitLineDeadline) {
            Log("harness: %s '%s' timed out", verb, g_waitLineText.c_str());
            g_failures.push_back(g_waitLineIsExpect ? "expected console output '" + g_waitLineText + "' (waited)"
                                                    : "waitline " + g_waitLineText + " timed out");
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
                Now() - g_waitZoneStart, *reinterpret_cast<int*>(kCampaignMode));
            g_waitingZone = false;
        } else if (Now() >= g_waitZoneDeadline) {
            char what[400];
            std::snprintf(what, sizeof(what), "waitzone %s timed out (zone '%s', request %d, campaign mode %d, %s)",
                          g_waitZoneName.c_str(), ServerZoneName(), g_request.kind, *reinterpret_cast<int*>(kCampaignMode),
                          DescribeLocalPlayer().c_str());
            Log("harness: %s", what);
            g_failures.push_back(what);
            g_waitingZone = false;
        } else {
            return;
        }
    }
    while (g_next < g_steps.size()) {
        if (Now() < g_waitUntil) return;
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
        Log("harness: %s %s", s.verb.c_str(), s.argument.c_str());
        if (s.verb == "wait") {
            g_waitUntil = Now() + std::strtoul(s.argument.c_str(), nullptr, 10);
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
        } else if (s.verb == "click" || s.verb == "rclick" || s.verb == "move") {
            int x = 0, y = 0;
            if (sscanf_s(s.argument.c_str(), "%d %d", &x, &y) == 2) PostMouse(s.verb, x, y);
            else Log("harness: %s needs <x> <y>", s.verb.c_str());
        } else if (s.verb == "wheel") {
            int x = 0, y = 0, delta = 0;
            if (sscanf_s(s.argument.c_str(), "%d %d %d", &x, &y, &delta) == 3) PostWheel(x, y, delta);
            else Log("harness: wheel needs <x> <y> <delta>");
        } else if (s.verb == "clickregion" || s.verb == "rclickregion" || s.verb == "moveregion") {
            PostRegionMouse(s.verb, std::atoi(s.argument.c_str()));
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
            g_waitZoneStart = Now();
            g_waitZoneDeadline = g_waitZoneStart + seconds * 1000;
            return;
        } else if (s.verb == "check") {
            g_checking = true;
            g_checkExpr = s.argument;
            g_checkFrom = g_console.size();
            g_checkDeadline = Now() + 3000;
            ConsoleExecute(("#" + s.argument).c_str());
            return;
        } else if (s.verb == "speed") {
            g_heldTickLength = std::strtoul(s.argument.c_str(), nullptr, 10);
        } else if (s.verb == "waitline" || s.verb == "waitexpect") {
            char* rest = nullptr;
            unsigned long seconds = std::strtoul(s.argument.c_str(), &rest, 10);
            while (rest && *rest == ' ') ++rest;
            g_waitingLine = true;
            g_waitLineIsExpect = s.verb == "waitexpect";
            g_waitLineFrom = g_waitLineIsExpect ? g_expectFrom : 0;
            g_waitLineText = rest ? rest : "";
            g_waitLineDeadline = Now() + seconds * 1000;
            return;
        } else if (s.verb == "waitfor") {
            char* rest = nullptr;
            unsigned long seconds = std::strtoul(s.argument.c_str(), &rest, 10);
            while (rest && *rest == ' ') ++rest;
            g_waitingFor = true;
            g_waitForPending = false;
            g_waitForExpr = rest ? rest : "";
            g_waitForStart = g_waitForNext = Now();
            g_waitForDeadline = g_waitForStart + seconds * 1000;
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
        } else if (s.verb == "oldterrain") {
            render::SetDrawOldTerrain(s.argument != "off");
        } else if (s.verb == "oldobjects") {
            render::SetDrawOldObjects(s.argument != "off");
        } else if (s.verb == "oldinterface") {
            render::SetDrawOldInterface(s.argument != "off");
        } else if (s.verb == "freeze" || s.verb == "unfreeze") {
            Freeze(s.verb == "freeze");
        } else if (s.verb == "reject") {
            g_rejected.push_back(s.argument);
        } else if (s.verb == "report") {
            WriteReport();
        } else if (s.verb == "quit") {
            WriteReport();
            // The game can hang on the way out (Rebirth does). The quit console command calls exit(),
            // which stops our watchdog thread too, so ei_launch ends the process when it sees
            // harness_quit and the game is still running 10 s later.
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

DWORD WINAPI Watchdog(void*) {
    const DWORD start = GetTickCount();
    for (;;) {
        Sleep(250);
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
LRESULT CALLBACK AlwaysActiveWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_ACTIVATE && (LOWORD(wParam) == WA_INACTIVE || HIWORD(wParam) != 0)) return 0;
    if (message == WM_ACTIVATEAPP && !wParam) return 0;
    return CallWindowProcA(g_gameWindowProc, window, message, wParam, lParam);
}

void KeepGameWindowActive() {
    HWND window = GameWindow();
    if (g_gameWindowProc || !window) return;
    g_gameWindowProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrA(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&AlwaysActiveWindowProc)));
    Log("harness: the game window stays active when it loses focus");
}

// Posted input reaches the game only through its main loop (WinMain 00456440: PeekMessageA, then
// GetMessageA and the input dispatcher 00457970) or the movie player's loop (005DB3F0: Esc and
// Space). Every other PeekMessageA call comes from the game's pump 00458220, which interface window
// switches run (CInterface3D::Activate 005F71C0 when the battle screen opens after a new game,
// PushWindow, closing windows) to discard the input waiting meanwhile: what it removes goes to the
// window procedure, which ignores keys and mouse. Steps can run inside it, so input steps posted
// there were lost (the console scenario's keys, when the battle screen opened after its waitfor);
// they wait for one of these calls instead (identified by the return address).
constexpr std::uintptr_t kInputLoopReturns[] = {
    0x00457119,   // WinMain, before the frame
    0x00457610,   // WinMain, after the frame
    0x005DB788,   // the movie player
};
bool IsInputLoop(const void* returnAddress) {
    for (std::uintptr_t r : kInputLoopReturns)
        if (reinterpret_cast<std::uintptr_t>(returnAddress) == r) return true;
    return false;
}

BOOL WINAPI PeekMessageHook(LPMSG msg, HWND hwnd, UINT min, UINT max, UINT remove) {
    SetMainThread();
    TraceFpu("in the message loop");
    KeepGameWindowActive();
    if (g_running && !g_inStep) {
        g_inStep = true;   // console commands can pump messages themselves
        g_inputLoop = IsInputLoop(_ReturnAddress());
        RunSteps();
        g_inStep = false;
    }
    return g_peekMessage(msg, hwnd, min, max, remove);
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
        if (g_steps.back().verb == "deterministic") {
            g_deterministic = true;
            g_steps.pop_back();
            continue;
        }
        if (g_steps.back().verb == "rebirthflags") {
            ei::rebirth::QueueSpec(g_steps.back().argument.c_str());
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
        PatchSlot(kGetPrivateProfileStringASlot, &HarnessGetProfileString, g_getProfileString);
        PatchSlot(kGetPrivateProfileSectionASlot, &RedirectedGetProfileSection, g_getProfileSection);
        PatchSlot(kGetPrivateProfileIntASlot, &RedirectedGetProfileInt, g_getProfileInt);
        PatchSlot(kWritePrivateProfileStringASlot, &RedirectedWriteProfileString, g_writeProfileString);
        PatchSlot(kGetPrivateProfileSectionNamesASlot, &RedirectedGetProfileSectionNames, g_getProfileSectionNames);
        if (g_deterministic) {
            PatchSlot(kTimeGetTimeSlot, &VirtualTimeGetTime, g_timeGetTime);
            PatchSlot(kGetTickCountSlot, &VirtualGetTickCount, g_getTickCount);
            PatchSlot(kGetLocalTimeSlot, &FixedTime, g_getLocalTime);
            PatchSlot(kGetSystemTimeSlot, &FixedTime, g_getSystemTime);
            JumpTo(0x0049F660, reinterpret_cast<const void*>(&FixedIsaacSeed));
            Log("harness: deterministic mode (virtual clock, %u world updates per frame)", kUpdatesPerFrame);
        } else if (g_freezeUsed) {
            PatchSlot(kTimeGetTimeSlot, &HeldTimeGetTime, g_heldTimeGetTime.base);
            PatchSlot(kGetTickCountSlot, &HeldGetTickCount, g_heldTickCount.base);
            PatchSlot(kGetLocalTimeSlot, &FixedTime, g_getLocalTime);
            PatchSlot(kGetSystemTimeSlot, &FixedTime, g_getSystemTime);
            Log("harness: the game's clocks can be frozen (fixed date)");
        }
        if (g_freezeUsed) RedirectCall(kFrameCameraUpdateCall, kCameraUpdate, reinterpret_cast<const void*>(&HeldCameraUpdate));
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
