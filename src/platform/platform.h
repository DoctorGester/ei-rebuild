#pragma once

// The platform layer (docs/notes/platform.md): every call of the game's code into the operating system, one
// section per area. No system type crosses it: UTF-8 text, fixed-width integers, our own enums and structs, a
// native window as an opaque pointer. The backends: SDL3 for the window, its events, the cursor, the mouse, the
// clipboard, the message box, the clocks and the threads (src/platform/sdl/platform_sdl.cpp), std::filesystem for the
// folders (src/platform/files.cpp), Windows or POSIX for the rest (src/platform/win32, src/platform/posix: one file per
// area).

#include "lib/bytes.h"
#include "lib/rect.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace platform {

// ==== Clocks =========================================================================================================

// Clocks and waiting. Two millisecond clocks, both counting from the system's start and wrapping at 2^32. They are
// two because the original read a precise and a coarse clock in different places and the harness drives each
// separately (its virtual and held clocks), so a call keeps the clock it has.

// The frame clock: the main loop's time and everything timed by it (animations, effects, the interface, sound and
// movies), in 1 ms steps.
std::uint32_t FrameClock();
// The coarse clock: the network's ids, the sound cache's ages, the menu's lightning; the system's steps (about 16 ms).
std::uint32_t CoarseClock();
// Gives up the calling thread for at least `ms` milliseconds; 0 only lets the other ready threads of its priority run.
void Sleep(std::uint32_t ms);
// The calendar's clock: seconds since 1970 UTC now (lib/calendar.h makes a local date of it; the harness fixes it).
std::int64_t SecondsSince1970();

// ==== The message box ================================================================================================

// A native window (an HWND on Windows, else the SDL_Window): "The window and its events" below.
using NativeWindow = void*;

enum class MessageButtons : std::uint8_t { Ok, OkCancel };
enum class MessageIcon : std::uint8_t { Error, Warning };
enum class MessageAnswer : std::uint8_t { Ok, Cancel };

// Shows `text` (UTF-8) under `caption` and waits for the player. `owner` is the native window it belongs to (null:
// none). Cancel only when the player cancelled; a box that could not be shown answers Ok.
MessageAnswer ShowMessageBox(NativeWindow owner, const char* text, const char* caption, MessageButtons buttons,
                             MessageIcon icon);

// ==== Paths ==========================================================================================================

// Where the program runs from. Paths are UTF-8, with backslashes between folders as the game writes them.

// The full path of the running program's file, with the system's separators.
std::string ProgramPath();
// The process's current folder, without a trailing separator but at a drive's root ("C:\").
std::string CurrentFolder();
// Makes `path` the process's current folder; false when it could not, with the system's error code in `error`.
bool SetCurrentFolder(const std::string& path, std::uint32_t* error);
// The folder for the current user's program data, with a trailing separator; empty when the user has none.
std::string UserDataFolder();
// The game's folder when nothing names one (main.cpp's EI_GAME_ROOT), with the system's separators: the program's own
// folder (the exe dropped into a game folder). On macOS, where that folder is inside the app: the folder holding
// ei-rebuild.app when it holds the game's data, else the one chosen last time, else one the player chooses in a folder
// picker (remembered in ~/Library/Application Support/ei-rebuild). Empty when the player chose none.
std::string DefaultGameFolder();
// The folder for the log and a run's other output when nothing names one (log.h's EI_OUT_DIR): the program's own
// folder; on macOS ~/Library/Logs/ei-rebuild.
std::string DefaultOutputFolder();
// The folder that holds ei_settings.ini when nothing pins the file (settings.cpp), with a trailing separator: the
// program's own folder; on macOS, where that is inside the app, ~/Library/Application Support/ei-rebuild (beside
// game_folder.txt). Empty when the program has no folder.
std::string DefaultSettingsFolder();
// What follows the argument "--" on the program's command line: on Windows the command line's own text, as typed;
// elsewhere the arguments (main's) joined by spaces. Empty without one.
std::string ArgumentsAfterSeparator(int argc, char** argv);

// ==== The machine ====================================================================================================

// The size in pixels of the primary monitor's current mode (whatever the process's DPI awareness); none when it cannot
// tell.
std::optional<Size> PrimaryDesktopSize();
// The value of the environment variable `name`; empty when it is not set (or empty).
std::string EnvironmentVariable(const char* name);

// ==== Files and folders ==============================================================================================

// Paths are UTF-8 (the wide calls on Windows), with backslashes between folders as the game writes them (elsewhere each
// name is matched with Windows' case rules). Every call takes its path through one mapping, which the harness's builds
// answer with the run's saves and install overlay (src/platform/seams.h): what a call sees depends on its path alone.
// Every open shares the file with other readers and writers.
//   - File: a binary file on a system handle, also written by text lines;
//   - ReadWholeFile / WriteWholeFile: a whole file at once (the text files, split into lines with str::Lines; the
//     settings file);
//   - MappedFile: a whole file mapped into memory, read only (the archives' and the registry files');
//   - FolderEntries: a folder search.
// A File's read, write or seek that fails throws a FileError: what the game catches of it is caught, an uncaught one
// ends the game (a write to a full disk).

// Creates the folder `path` (its parent must exist). False when it could not, also when it exists already.
bool CreateFolder(std::string_view path);
// Removes the file `path` (not a folder). False when it could not.
bool RemoveFile(std::string_view path);
// Removes the empty folder `path`. False when it could not.
bool RemoveFolder(std::string_view path);
// Renames (moves) the file `from` to `to`; false when it could not, also when `to` exists.
bool RenameFile(std::string_view from, std::string_view to);
// Copies the file `from` over `to`, replacing it; false when it could not.
bool CopyFileOver(std::string_view from, std::string_view to);
// Whether `path` names a file or a folder.
bool PathExists(std::string_view path);
// Whether `path` names a file, not a folder.
bool IsFile(std::string_view path);
// The bytes this user may still write on the drive of `path` (a folder on it, or its root); 0 when unknown.
double FreeDiskSpace(std::string_view path);

// The mod the game plays (docs/notes/mods.md): from now on a file in `modFolder`'s folders stands for the one of the
// same path in `gameFolder` (read first, written there when the mod has it; not the mod's .res archives, which its
// config.reg combines with the game's), and the saves are <gameFolder>\saves\<the mod folder's name>\. An empty
// `modFolder`: the game's own files from now on. At the start before the game reads its data, and at a switch of mods
// between the game's stop and its new start (app/main_loop.cpp); false (no mod) when the folder is not there.
bool SwitchMod(std::string_view gameFolder, std::string_view modFolder);
// The mod's folder in full (SwitchMod), empty without one.
std::string ModFolder();

// One entry of a folder search.
struct FolderEntry {
    std::string name;
    std::string path;     // the search pattern's folder made absolute, a separator and the name
    std::uint32_t size = 0;  // a file's size, its low 32 bits
    bool folder = false;

    // The name without its extension (from its last '.').
    std::string Title() const { return name.substr(0, name.find_last_of('.')); }
    // "." or "..".
    bool IsDots() const { return folder && (name == "." || name == ".."); }
};
// The entries `pattern` (a folder, a separator and a name with * and ?) matches, in the system's order ("." and ".."
// too on Windows); none when it matches nothing.
std::vector<FolderEntry> FolderEntries(std::string_view pattern);

// The whole file `path` into `bytes`, shared with any writer (an editor or a sync tool holding it open); false (and
// `bytes` empty) when it does not open or a read fails, with the system's error code in `error`.
bool ReadWholeFile(std::string_view path, std::string* bytes, std::uint32_t* error = nullptr);
// `bytes` written whole to `path`: to a file beside it, which is then moved over it (the path's folder made when it
// is missing). False when that failed, with the system's error code in `error`.
bool WriteWholeFile(std::string_view path, const std::string& bytes, std::uint32_t* error = nullptr);

// A failed file operation: the system's error code and the file's path, both in what().
class FileError : public std::runtime_error {
public:
    FileError(unsigned long code, std::string_view path);
};

// How File::Open opens a file.
enum class FileMode : std::uint8_t {
    Read,            // an existing file, for reading
    Create,          // a new file, or an existing one emptied, for writing
    WriteExisting,   // an existing file, for writing
};
// Where File::Seek counts from.
enum class SeekFrom : std::uint8_t { Begin, Current, End };

// A binary file. Closed by the destructor.
class File {
public:
    File() = default;
    ~File();
    File(const File&) = delete;
    File& operator=(const File&) = delete;

    // False when the file cannot be opened. A file still open is closed first.
    bool Open(std::string_view path, FileMode mode);
    void Close();
    bool IsOpen() const { return handle != -1; }

    // The bytes read (fewer at the end of the file).
    unsigned Read(void* buffer, unsigned count);
    // A FileError also when fewer bytes were written.
    void Write(const void* buffer, unsigned count);
    // One value as its bytes (sizeof(T)).
    template <class T>
    unsigned ReadValue(T& value) {
        static_assert(lib::kFormatValue<T>, "a file value: plain bytes without padding, one size in every build");
        return Read(&value, sizeof(T));
    }
    template <class T>
    void WriteValue(const T& value) {
        static_assert(lib::kFormatValue<T>, "a file value: plain bytes without padding, one size in every build");
        Write(&value, sizeof(T));
    }
    // `line` and "\r\n" (a text file's line end on disk).
    void WriteLine(std::string_view line);
    // Positions and sizes are bytes from the start (a file is under 4 GB); Seek's offset counts from `from` and returns
    // the new position.
    std::uint32_t Seek(std::int32_t offset, SeekFrom from);
    std::uint32_t Position() const;
    std::uint32_t Size() const;
    // `count` bytes at `position`: false when the seek or the read fails or fewer bytes were read (nothing throws).
    bool ReadAt(std::uint32_t position, void* buffer, unsigned count);

private:
    std::intptr_t handle = -1;   // the system's file handle or descriptor; -1: none
    std::string path;            // as opened (for FileError)
};

// A whole file mapped into memory, read only. Closed by the destructor.
class MappedFile {
public:
    MappedFile() = default;
    ~MappedFile();
    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    // False when the file does not open or cannot be mapped (an empty one cannot). A file still open is closed first.
    bool Open(std::string_view path);
    void Close();
    const void* Data() const { return view; }
    std::uint32_t Size() const { return size; }

private:
    std::intptr_t file = -1;   // the system's file handle or descriptor; -1: none
    void* mapping = nullptr;   // Windows' mapping handle
    const void* view = nullptr;
    std::uint32_t size = 0;
};

// ==== Threads ========================================================================================================

// Threads are std::thread, locks std::mutex (std::recursive_mutex where a critical section was entered again).

// ==== Key codes ======================================================================================================

// A key event's key: what the active layout makes the key (layouts move the letters, the digits and the punctuation:
// AZERTY's key where a US keyboard has Q is A), named as Windows' US layout names it. The values are Windows'
// virtual-key codes: Windows' layouts answer in them (win32/keyboard.cpp, tools/input_check), the harness's recordings
// keep them, and a layout can give a value that has no name here (Windows' VK_OEM_8), which passes through as it is.
enum class Key : std::uint8_t {
    None = 0x00,
    Backspace = 0x08,
    Tab = 0x09,
    Clear = 0x0C,          // the keypad's 5 with Num Lock off
    Return = 0x0D,         // Enter, the main block's and the keypad's
    Shift = 0x10,
    Control = 0x11,
    Alt = 0x12,
    Pause = 0x13,
    CapsLock = 0x14,
    Escape = 0x1B,
    Space = 0x20,
    PageUp = 0x21,
    PageDown = 0x22,
    End = 0x23,
    Home = 0x24,
    Left = 0x25,
    Up = 0x26,
    Right = 0x27,
    Down = 0x28,
    PrintScreen = 0x2C,
    Insert = 0x2D,
    Delete = 0x2E,
    Digit0 = 0x30,         // Digit0 + n (KeyAfter): the digit n's key, to Digit9
    Digit9 = 0x39,
    A = 0x41,              // A + n: the n-th letter's key after A, to Z
    Z = 0x5A,
    LeftWindows = 0x5B,
    RightWindows = 0x5C,
    Application = 0x5D,
    Keypad0 = 0x60,        // Keypad0 + n: the keypad's digit n with Num Lock on
    KeypadMultiply = 0x6A,
    KeypadPlus = 0x6B,
    KeypadMinus = 0x6D,
    KeypadPeriod = 0x6E,   // with Num Lock on
    KeypadDivide = 0x6F,
    F1 = 0x70,             // F1 + n: the function key n + 1, to F12
    F4 = 0x73,
    F10 = 0x79,
    F12 = 0x7B,
    NumLock = 0x90,
    ScrollLock = 0x91,
    Semicolon = 0xBA,      // the punctuation keys by what they type on a US keyboard
    Equals = 0xBB,
    Comma = 0xBC,
    Minus = 0xBD,
    Period = 0xBE,
    Slash = 0xBF,
    Grave = 0xC0,          // the key under Escape (the console's)
    LeftBracket = 0xDB,
    Backslash = 0xDC,
    RightBracket = 0xDD,
    Apostrophe = 0xDE,
    NonUsBackslash = 0xE2, // the key left of Z on a 102-key keyboard
};

// The key `n` places after `first` (KeyAfter(Key::A, 2) is C).
constexpr Key KeyAfter(Key first, int n) { return static_cast<Key>(static_cast<int>(first) + n); }

// ==== The window and its events ======================================================================================

// docs/notes/platform.md, "Window and events". The window is a native one (an HWND on Windows, else the SDL_Window)
// that the game passes around as an opaque pointer. What happens to it and the keyboard's and the mouse's input reach
// the game as Events, which the game's loops take from the queue (EventWaits, NextEvent, TakeEvent).

enum class EventType : std::uint8_t {
    KeyDown,          // a key pressed, or held and repeating (key, scan, repeat, modifiers)
    KeyUp,            // a key released (key, scan, modifiers)
    Text,             // text typed (text; scan: the last key pressed)
    MouseMove,        // the mouse moved over the window (x, y)
    MouseDown,        // a mouse button pressed (button, clicks, x, y)
    MouseUp,          // a mouse button released (button, x, y)
    Wheel,            // the wheel turned (wheel, x, y)
    Focus,            // the window gained or lost the focus, or was minimised or restored (active, minimized)
    CloseRequested,   // the player closes the window, or the system ends the session
    Quit,             // PostQuit's request (quitCode): the loop ends
};

enum class MouseButton : std::uint8_t { Left, Right, Middle, X1, X2 };

// The modifier keys held (Event::modifiers).
inline constexpr std::uint8_t kModifierShift = 0x01;
inline constexpr std::uint8_t kModifierControl = 0x02;
inline constexpr std::uint8_t kModifierAlt = 0x04;

struct Event {
    EventType type = EventType::KeyDown;
    Key key = Key::None;          // keys: the key ("Key codes" above)
    int scan = 0;                 // keys: the key's place as its set-1 scan code (0x100 for an extended key); Text: the
                                  // last key pressed's
    bool repeat = false;          // KeyDown: the key was down already (the keyboard's repeat)
    std::uint8_t modifiers = 0;   // keys: kModifierShift, kModifierControl, kModifierAlt held
    std::string text;             // Text: UTF-8, also the control characters a key types (Backspace, Tab, Enter,
                                  // Escape, Ctrl+letter 1..26; "The keyboard" below)
    MouseButton button = MouseButton::Left;
    int clicks = 0;               // MouseDown: 1; 2 for the second press of a double click, 3, ...
    int x = 0;                    // mouse: the position in the window, in pixels
    int y = 0;
    float wheel = 0.0f;           // Wheel: the turn in notches (fractions from a smooth wheel), positive away from the
                                  // player
    bool active = false;          // Focus: the window has the focus
    bool minimized = false;       // Focus: the window is minimised
    int quitCode = 0;             // Quit: PostQuit's code
};

// ---- The window --------------------------------------------------------------------------------------------------

// Starts the window system for the program `name` (on Windows the game window's class name, by which
// BringOtherInstanceForward finds a running game).
void StartWindowSystem(const char* name);
// Whether another copy of the game (started under `name`, its window titled `title`) runs already; when one does, its
// window is brought to the front, restored from the task bar.
bool BringOtherInstanceForward(const char* name, const char* title);
// The game window, hidden, its client area `width` x `height` (the configured resolution, which the game keeps
// whatever the window gets). Full screen: a borderless window at the screen's top left (CoverMonitor puts it over its
// monitor); on macOS SDL's desktop full screen, a Space of its own (CoverMonitor does nothing). Windowed: a decorated
// window the player can move and resize, centred in the primary monitor's work area; a client area that would not fit
// there is made smaller, so no row is off-screen (the renderer scales the frame into it). Null when it could not be
// made.
NativeWindow CreateGameWindow(const char* title, int width, int height, bool fullScreen);
void ShowGameWindow(NativeWindow window);
// A point of the screen as one of the window's client area, and back.
Point ScreenToClient(NativeWindow window, Point point);
Point ClientToScreen(NativeWindow window, Point point);
// Where CoverMonitor moved a window from, and the monitor it now covers.
struct MonitorCover {
    Rect was;
    Rect monitor;
};
// Full screen without a display mode change: the window moved over the whole monitor it is on, unless minimised or
// there already; none when it did not move.
std::optional<MonitorCover> CoverMonitor(NativeWindow window);
// The window switched to full screen (borderless, unsizeable: CoverMonitor puts it over its monitor at the next present;
// on macOS SDL's desktop full screen) or back to a decorated, resizable window (SetClientSize then places it). Never a
// display mode change.
void SetFullScreen(NativeWindow window, bool fullScreen);
// The window's client area in pixels (what the renderer draws into); none when it has none (minimised).
std::optional<Size> WindowPixelSize(NativeWindow window);
// A window that is not full screen: its client area `width` x `height` pixels, placed as CreateGameWindow places it
// (made smaller when it and its borders would not fit in the usable bounds of the display it is on, centred there); a
// maximised window is restored first. Returns once the system has applied it (X11 resizes asynchronously).
void SetClientSize(NativeWindow window, int width, int height);
// The sizes of the display `window` is on that the game offers as its resolution, narrowest first, each once (the
// display's modes' sizes in pixels, none under 800x600 or above the desktop's: display_sizes.h). Sizes only: no display
// mode is ever set, full screen scales the frame. The harness's backend returns a fixed list.
std::vector<Size> DisplaySizes(NativeWindow window);
// The cursor kept inside the window (the window grabs the mouse) or let go; a window that has no focus is let go by the
// system, and the game asks again as it gets it back. The harness's backend only logs the request.
void ConfineCursor(NativeWindow window, bool confined);

// ---- Events ------------------------------------------------------------------------------------------------------

// Whether an event waits in the queue (it stays there).
bool EventWaits();
// The system's messages taken into the queue without taking any event off it, so that the window answers while a long
// load holds the main thread (the loading screen's steps). Nothing in the harness's backend (its events are the scenario's).
void PumpEvents();
// Waits for the next event and takes it: a Quit event at the quit request.
Event NextEvent();
// Takes the next waiting event (a Quit event at the quit request); none when none waits.
std::optional<Event> TakeEvent();
// Asks the loop to end: NextEvent gives a Quit event with `code` once the waiting events are taken.
void PostQuit(int code);
// `step` called on the main thread every 10 to 17 ms while a modal loop of the system holds the main loop (Windows'
// and macOS's moving or sizing of the window: SDL's live-resize redraws); null: none. The harness's backend has no
// such loop.
void SetModalLoopStep(void (*step)());

// ==== The cursor =====================================================================================================

// The system's cursor shows the game's pictures over the game window (docs/notes/renderer.md section 9); its position is
// in the game's points.

// A cursor shape (an SDL_Cursor); null shows none.
using CursorShape = void*;

// A shape of `width` x `height` pixels (R, G, B, A bytes, alpha not premultiplied, rows from the top) with its hot spot
// at (hotX, hotY); null when the system cannot make one. FreeCursor: one no longer shown let go.
CursorShape MakeCursor(const std::uint8_t* rgba, int width, int height, int hotX, int hotY);
void FreeCursor(CursorShape shape);
CursorShape CurrentCursor();
void SetCursor(CursorShape shape);
// The cursor stays at `at` (a game point) while the mouse's motion still reaches the game (TakeMouseMotion: the camera
// turned with the right button), until ReleaseCursor lets it move again. SetCursorPosition lets it go first.
void HoldCursor(Point at);
void ReleaseCursor();

// How the game's points relate to the screen's (the frame scaled into the window): `toGame` turns a screen point into
// the game's, `toScreen` the other way. Without (the start), the game's points are the screen's.
using PointMap = Point (*)(Point point);
void MapCursorPoints(PointMap toGame, PointMap toScreen);
// The cursor's position as a game point; none when the system could not tell.
std::optional<Point> CursorPosition();
// Moves the cursor to the game point `at`.
void SetCursorPosition(Point at);

// ==== The keyboard ===================================================================================================

// The keys' names, the layout typed characters come from and what kind of character one is.

// The name the system gives the key with the scan code `scan` (bit 8 for an extended key) in the active layout's
// language, UTF-8; empty when it has none.
std::string KeyName(int scan);
// Activates the keyboard layout of `language` (a Windows language id), else one of `fallback`, else keeps the one in
// use. Each installed layout is active for a moment on the way (only the active one tells its language).
void ActivateKeyboardLayout(std::uint32_t language, std::uint32_t fallback);
// Whether the system calls the typed character `character` (a code point; false past U+FFFF) a letter.
bool IsLetter(std::uint32_t character);

// ==== The mouse ======================================================================================================

// The mouse's position comes from the cursor calls, its buttons and wheel as events; the game's cursor adds the motion
// to its own position while it holds the cursor (the camera turned with the right button).

// The mouse's motion in pixels since the last call (SDL's relative motion, fractions carried to the next).
Point TakeMouseMotion();

// ==== The clipboard ==================================================================================================

// Text, in UTF-8.

// Puts `text` on the clipboard in place of what it held; nothing changes when the clipboard is busy.
void CopyText(std::string_view text);
// The clipboard's text; empty when it holds none or is busy.
std::string ClipboardText();

// ==== Sockets ========================================================================================================

// IPv4 UDP and TCP sockets, host names, waiting for readiness. The calls mirror BSD sockets one to one, each failing the
// way the system's does (the callers rely on it), so a port of the layer maps them straight onto its own sockets. The
// system's sockets start with the first call that needs them and stay up until the process ends. Byte
// order and IPv4 numbers are lib/ipv4.h's.

namespace net {

// A socket: the system's handle or descriptor, pointer-sized; kNoSocket when there is none.
using Socket = std::uintptr_t;
inline constexpr Socket kNoSocket = ~Socket{0};

// An IPv4 address with its port, as the game keeps and sends one: sockaddr_in's 16 bytes (the family, then the port
// and the address in network order, 8 zero bytes). Passed to the system as it is: an address whose family is not
// kIpv4 fails there.
struct Address {
    std::uint16_t family = 0;
    std::uint16_t port = 0;
    std::uint32_t ip = 0;
    std::uint8_t zero[8] = {};
};
static_assert(sizeof(Address) == 16);

inline constexpr std::uint16_t kIpv4 = 2;

enum class Kind : std::uint8_t { Udp, Tcp };

// Why the last call of this thread failed.
enum class Error : std::uint8_t { None, WouldBlock, AddressInUse, Refused, Other };

// A new socket of `kind` (IPv4), blocking; kNoSocket when none could be made.
Socket Open(Kind kind);
void Close(Socket socket);
// Binds `socket` to `address` (ip 0: every address of the host; port 0: any). False when it fails (LastError:
// AddressInUse when another socket holds it).
bool Bind(Socket socket, const Address& address);
// Makes the reads, writes and connects of `socket` return at once (true) or wait (false).
void SetNonBlocking(Socket socket, bool on);
// Lets a UDP socket send to broadcast addresses.
void AllowBroadcast(Socket socket);
// Connects a TCP socket to `address`. A non-blocking socket starts the connect and fails with WouldBlock; Select's
// write group then tells when it is done.
bool Connect(Socket socket, const Address& address);

// Sends `length` bytes to `to` (UDP); the bytes sent, or -1.
int SendTo(Socket socket, const void* data, int length, const Address& to);
// The next datagram (up to `length` bytes; the rest of a longer one is lost) into `data`, its sender into `from`;
// the bytes read, or -1 (nothing waiting on a non-blocking socket, or an error).
int ReceiveFrom(Socket socket, void* data, int length, Address* from);
// Sends `length` bytes on a connected socket; the bytes sent, or -1.
int Send(Socket socket, const void* data, int length);
// Reads up to `length` bytes from a connected socket; the bytes read, 0 when the peer closed it, or -1.
int Receive(Socket socket, void* data, int length);

Error LastError();

// Sockets to wait on. Select leaves in each group only its ready sockets: a group asked again after a
// Select that found none ready is empty.
struct SocketGroup {
    static constexpr int kCapacity = 64;   // as many as Windows' select takes

    int count = 0;
    Socket sockets[kCapacity];

    // A group of the one socket.
    static SocketGroup Of(Socket socket) {
        SocketGroup group;
        group.count = 1;
        group.sockets[0] = socket;
        return group;
    }
};

// Waits at most `microseconds` for a socket of `read` to have data (or its peer to close), of `write` to take
// writes (a connect done) or of `failed` to fail (a connect refused); null groups are not asked. The number of ready
// sockets, 0 at the timeout, or -1 on an error, which includes asking only empty groups.
int Select(SocketGroup* read, SocketGroup* write, SocketGroup* failed, std::int32_t microseconds);

// This host's name; empty when the system cannot tell.
std::string HostName();
// The first IPv4 address of the host `name` (a name or an IPv4 number) in network order; none when the host is
// unknown. Blocks while the name is looked up.
std::optional<std::uint32_t> LookUp(const std::string& name);

} // namespace net

} // namespace platform
