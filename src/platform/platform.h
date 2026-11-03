#pragma once

// The platform layer (docs/notes/platform.md): every call of the game's code into the operating system, one
// section per area. No system type crosses it: UTF-8 text, fixed-width integers, our own enums and structs, a
// native window as an opaque pointer. The backends: SDL3 for the window, its events, the cursor, the mouse, the
// clipboard, the message box, the clocks and the threads (src/platform/sdl/platform_sdl.cpp), std::filesystem for the
// folders (src/platform/files.cpp), Windows or POSIX for the rest (src/platform/win32/platform_win32.cpp,
// src/platform/posix/platform_posix.cpp).

#include "lib/bytes.h"
#include "lib/rect.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace platform {

// ==== Clocks =========================================================================================================

// Clocks and waiting. The game reads two millisecond clocks, both counting from the system's start and wrapping at
// 2^32, each where the original read it: the harness drives them as two clocks (its virtual and held clocks), so a call
// keeps the clock it has.

// The multimedia timer (timeGetTime): milliseconds in 1 ms steps.
std::uint32_t Milliseconds();
// The system tick count (GetTickCount): milliseconds in the scheduler's steps (about 16 ms).
std::uint32_t TickCount();
// Gives up the calling thread for at least `ms` milliseconds; 0 only lets the other ready threads of its priority run.
void Sleep(std::uint32_t ms);
// The calendar's clock: seconds since 1970 UTC now (lib/calendar.h makes a local date of it; the harness fixes it).
std::int64_t SecondsSince1970();

// ==== The message box ================================================================================================

enum class MessageButtons : std::uint8_t { Ok, OkCancel };
enum class MessageIcon : std::uint8_t { Error, Warning };
enum class MessageAnswer : std::uint8_t { Ok, Cancel };

// Shows `text` (UTF-8) under `caption` and waits for the player. `owner` is the native window it belongs to (null:
// none). Cancel only when the player cancelled; a box that could not be shown answers Ok.
MessageAnswer ShowMessageBox(void* owner, const char* text, const char* caption, MessageButtons buttons,
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
// What follows the argument "--" on the program's command line: on Windows the command line's own text, as typed;
// elsewhere the arguments (main's) joined by spaces. Empty without one.
std::string ArgumentsAfterSeparator(int argc, char** argv);

// ==== The machine ====================================================================================================

// Whether the desktop smooths the edges of text; true when it cannot tell.
bool FontSmoothing();
// The size in pixels of the primary monitor's current mode (whatever the process's DPI awareness); both stay as they
// are when it cannot tell.
void PrimaryDesktopSize(int* width, int* height);
// The value of the environment variable `name`; empty when it is not set (or empty).
std::string EnvironmentVariable(const char* name);
// Ends the program at once with `exitCode`, without running its exit functions, and never returns.
[[noreturn]] void EndProcess(std::uint32_t exitCode);

// ==== Files and folders ==============================================================================================

// Paths are UTF-8 (the wide calls on Windows), with backslashes between folders as the game writes them (elsewhere each
// name is matched with Windows' case rules). Every call takes its path through one mapping, which the harness's builds
// answer with the run's saves and install overlay (src/platform/paths.h): what a call sees depends on its path alone.
// Every open shares the file with other readers and writers.
//   - File: a binary file on a system handle (the game's CFile uses), also written by text lines;
//   - ReadWholeFile / WriteWholeFile: a whole file at once (the text files, split into lines with str::Lines; the
//     settings file);
//   - MappedFile: a whole file mapped into memory, read only (the archives' and the registry files');
//   - FolderEntries: a folder search (its CFileFind uses).
// A File's read, write or seek that fails throws a FileError, where MFC threw a CFileException: what the game caught it
// catches; an uncaught one ends the game as before (a write to a full disk).

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
// The label of the volume whose root is `root` ("D:\." or "D:\"); empty when it has none or the drive has no disc.
std::string VolumeLabel(const char* root);

// One entry of a folder search.
struct FolderEntry {
    std::string name;
    std::string path;     // the search pattern's folder made absolute, a separator and the name
    std::uint32_t size;   // a file's size, its low 32 bits
    bool folder;

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
    bool IsOpen() const { return handle != nullptr; }

    // The bytes read (fewer at the end of the file).
    unsigned Read(void* buffer, unsigned count);
    // A FileError also when fewer bytes were written.
    void Write(const void* buffer, unsigned count);
    // One value as its bytes (sizeof(T)).
    template <class T>
    unsigned ReadValue(T& value) {
        static_assert(lib::kFormatValue<T>, "a file value is plain bytes of one size in every build");
        return Read(&value, sizeof(T));
    }
    template <class T>
    void WriteValue(const T& value) {
        static_assert(lib::kFormatValue<T>, "a file value is plain bytes of one size in every build");
        Write(&value, sizeof(T));
    }
    // `line` and "\r\n" (a text file's line end on disk).
    void WriteLine(std::string_view line);
    // The new position.
    long Seek(long offset, SeekFrom from);
    std::uint32_t Position() const;
    std::uint32_t Size() const;
    // `count` bytes at `offset` from the start: false when the seek or the read fails or fewer bytes were read (nothing
    // throws).
    bool ReadAt(long offset, void* buffer, unsigned count);

private:
    void* handle = nullptr;   // the system's file handle
    std::string path;         // as opened (for FileError)
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
    void* file = nullptr;      // the system's file handle
    void* mapping = nullptr;   // the system's mapping handle
    const void* view = nullptr;
    std::uint32_t size = 0;
};

// ==== Threads ========================================================================================================

// Threads are std::thread, locks std::mutex (std::recursive_mutex where a critical section was entered again).

// Gives the calling thread the highest priority (the music stream's thread, which AfxBeginThread started at
// THREAD_PRIORITY_TIME_CRITICAL: its buffers must be filled in time). std::thread has no priorities.
void MakeThreadTimeCritical();

// ==== The window and its events ======================================================================================

// docs/notes/platform.md, "Window and events". The window is a native one (an HWND on Windows) that the game passes
// around as an opaque pointer; what happens to it reaches the game as Events, through the handler given at the class's
// registration (events the system sends) and through the loop's NextEvent / TakeEvent (events waiting in the queue).
// The values inside an Event are the game's: key codes are the virtual-key codes (the next section), key flags and
// mouse buttons are as Windows gives them.

// A native window (an HWND on Windows), or a program instance (an HINSTANCE).
using NativeWindow = void*;
using NativeInstance = void*;

enum class EventType : std::uint8_t {
    KeyDown,         // a key pressed (key, repeat, keyFlags)
    KeyUp,           // a key released (the same)
    SystemKeyDown,   // a key pressed with Alt held, or F10 (the same)
    SystemKeyUp,     // such a key released (the same)
    Character,       // a typed character (key: a UTF-16 unit, half of a surrogate pair past U+FFFF; repeat, keyFlags)
    MouseMove,       // the mouse moved (buttons; x, y in the window's client area)
    LeftDown,        // a mouse button pressed, released, or pressed again quickly (the same)
    LeftUp,
    LeftDouble,
    RightDown,
    RightUp,
    RightDouble,
    Wheel,           // the wheel turned (buttons, wheelDelta; x, y on the screen)
    Paint,           // part of the window needs drawing
    Destroy,         // the window is being destroyed
    Activate,        // the window was activated or deactivated (activation, minimized)
    SystemCommand,   // a command of the window's system menu or a system key (command)
    AutoPlay,        // the system asks whether a disc put in may start its AutoPlay
    Quit,            // the quit request (quitCode): the loop ends
    Other,           // anything else
};

enum class Activation : std::uint8_t { Inactive, Active, ClickActive, Unknown };
enum class SystemCommand : std::uint8_t {
    TaskSwitch,    // a switch to another program (its hot key, or the next or previous window)
    ScreenSaver,   // the screen saver or the monitor's power saving wants to start
    Other,
};

// keyFlags: the high half of the key's message data, sign-extended (a released key's flags are negative): the scan
// code in bits 0-8 (bit 8 for an extended key), Alt held (kKeyAltDown), the key down before (0x4000), released
// (0x8000).
constexpr int kKeyAltDown = 0x2000;

struct Event {
    EventType type = EventType::Other;
    int key = 0;          // keys: the virtual-key code; Character: the UTF-16 unit
    int repeat = 0;       // keys and characters: the repeat count
    int keyFlags = 0;     // keys and characters: see kKeyAltDown
    int buttons = 0;      // mouse: the buttons and modifier keys held (Windows' MK_ bits)
    int x = 0;            // mouse: the position (16-bit signed values)
    int y = 0;
    int wheelDelta = 0;   // Wheel: the turn, 120 a notch, positive away from the player
    Activation activation = Activation::Unknown;
    bool minimized = false;   // Activate: the window is minimised
    SystemCommand command = SystemCommand::Other;
    int quitCode = 0;         // Quit: the code the quit request carries
    NativeWindow window = nullptr;   // the window it is for (null: none)
};

// The game's handling of an event the system sends to its window: what the window procedure returns.
using EventHandler = std::intptr_t (*)(const Event& event);

// ---- The window --------------------------------------------------------------------------------------------------

// Registers the game window's class under `className`, its events going to `handler`; the window keeps the system's
// cursor hidden only as the handler does (no class cursor), has no icon and gets double clicks.
void RegisterWindowClass(NativeInstance instance, const char* className, EventHandler handler);
// The running program's own instance (the module the program was started from).
NativeInstance ProgramInstance();
// A window of the class `className` with the title `title` (a game that runs already), or null.
NativeWindow FindClassWindow(const char* className, const char* title);
// The id of the process that owns `window`.
std::uint32_t WindowProcess(NativeWindow window);
// Brings another program's window to the front, restored from the task bar, unless it is in front already.
void BringToFront(NativeWindow window);
// The game window of the registered class, its client area `width` x `height` (the configured resolution, which the
// game keeps whatever the window gets). Full screen: a borderless window at the screen's top left (CoverMonitor puts it
// over its monitor). Windowed: a decorated window the player can move and resize, centred in the primary monitor's
// work area; a client area that would not fit there is made smaller, so no row is off-screen (the renderer scales the
// frame into it). Null when it could not be made.
NativeWindow CreateGameWindow(NativeInstance instance, const char* title, int width, int height, bool fullScreen);
// Shows the window as the program was asked to start (`show`: WinMain's nCmdShow) and has it painted.
void ShowGameWindow(NativeWindow window, int show);
bool IsMinimized(NativeWindow window);
// The window's part waiting to be painted marked as painted (nothing drawn: the renderer presents on its own).
void MarkPainted(NativeWindow window);
// Every window on the desktop repainted.
void RepaintDesktop();
// A point of the screen as one of the window's client area, and back.
void ScreenToClient(NativeWindow window, Point* point);
void ClientToScreen(NativeWindow window, Point* point);
// Full screen without a display mode change: the window moved over the whole monitor it is on, unless minimised or
// there already. True when it moved; `was` and `monitor` then hold its old place and the monitor's.
bool CoverMonitor(NativeWindow window, Rect* was, Rect* monitor);
// The window's client area in pixels (what the renderer draws into); false when it has none (minimised).
bool WindowPixelSize(NativeWindow window, int* width, int* height);

// ---- Events ------------------------------------------------------------------------------------------------------

// Whether an event waits in the queue (it stays there).
bool EventWaits();
// Waits for the next event and takes it, typed keys translated into Character events that follow it. False at the
// quit request (`event` is then a Quit event).
bool NextEvent(Event* event);
// Takes the next waiting event, untranslated; false when none waits.
bool TakeEvent(Event* event);
// An event taken from the queue sent on to its window's procedure (typed keys translated first).
void DispatchEvent(const Event& event);
// Asks the loop to end: NextEvent gives a Quit event with `code` once the waiting events are taken.
void PostQuit(int code);

// ==== Key codes ======================================================================================================

// A key event's `key` is one of these, on every system. The values are the Windows virtual-key codes, which the
// bindings file and the options keep.

inline constexpr int kKeyBack = 0x08;
inline constexpr int kKeyTab = 0x09;
inline constexpr int kKeyReturn = 0x0D;
inline constexpr int kKeyShift = 0x10;
inline constexpr int kKeyControl = 0x11;
inline constexpr int kKeyMenu = 0x12;       // Alt
inline constexpr int kKeyEscape = 0x1B;
inline constexpr int kKeySpace = 0x20;
inline constexpr int kKeyPageUp = 0x21;
inline constexpr int kKeyPageDown = 0x22;
inline constexpr int kKeyEnd = 0x23;
inline constexpr int kKeyHome = 0x24;
inline constexpr int kKeyLeft = 0x25;
inline constexpr int kKeyUp = 0x26;
inline constexpr int kKeyRight = 0x27;
inline constexpr int kKeyDown = 0x28;
inline constexpr int kKeyDelete = 0x2E;
inline constexpr int kKeyF4 = 0x73;

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
// With `at` (a game point) the cursor stays there while the mouse's motion still reaches the game (MouseDevice::Read:
// the camera turned with the right button); null lets it move again. SetCursorPosition lets it go first.
void HoldCursor(const Point* at);

// How the game's points relate to the screen's (the frame scaled into the window): `toGame` turns a screen point into
// the game's, `toScreen` the other way. Without (the start), the game's points are the screen's.
using PointMap = void (*)(Point* point);
void MapCursorPoints(PointMap toGame, PointMap toScreen);
// The cursor's position as a game point; false (and `at` unchanged) when the system could not tell.
bool CursorPosition(Point* at);
// Moves the cursor to the game point `at`.
void SetCursorPosition(Point at);

// ==== The keyboard ===================================================================================================

// The keys' names, the layout typed characters come from and what kind of character one is.

// The name the system gives the key with the scan code `scan` (bit 8 for an extended key) in the active layout's
// language, UTF-8 into `name` (`size` bytes with the terminator, cut at a character). False when it has none.
bool KeyName(int scan, char* name, int size);
// The virtual-key code the active keyboard layout gives the key with the scan code `scan` (set 1, bit 8 for an extended
// key), as the window procedure's key messages carried it; 0 when the system has no such codes (SDL's backend then
// names the key itself: sdl_keys.h).
int LayoutKey(int scan);
// The control character the active layout types for the key `key` (scan code `scan`) with Ctrl held, as
// TranslateMessage made it: 0 for none; -1 when the system cannot say (SDL's backend then applies its own rule).
int LayoutControlCharacter(int key, int scan);
// Activates the keyboard layout of `language` (a Windows language id), else one of `fallback`, else keeps the one in
// use. Each installed layout is active for a moment on the way (only the active one tells its language).
void ActivateKeyboardLayout(std::uint32_t language, std::uint32_t fallback);
// Whether the system calls the typed character `character` (a code point; false past U+FFFF) a letter.
bool IsLetter(std::uint32_t character);

// ==== The mouse ======================================================================================================

// The mouse's motion since the last read: the game's cursor adds it to the position it takes from the cursor calls,
// and the buttons come as window events. SDL3's motion events, or on Windows' window-procedure path DirectInput 7's
// system mouse, non-exclusive, in the background (the harness observes it).

enum class MouseStep : std::uint8_t {
    Create,       // the input object
    Device,       // the mouse device
    Format,       // its data format
    Cooperation,  // its cooperative level
    Acquire,      // taking the device
};

// Told of a step that failed, with the system's result code; the answer is the result of the call that was running
// (0 or 1), except for a failed Acquire in Open, whose answer is passed over.
using MouseReport = int (*)(MouseStep step, std::int32_t code);

class MouseDevice {
public:
    MouseDevice();
    ~MouseDevice();
    MouseDevice(const MouseDevice&) = delete;
    MouseDevice& operator=(const MouseDevice&) = delete;

    // The input object for `instance`, its system mouse with the mouse data format, for `window`, taken. A failed
    // step is reported and ends the call with the report's answer (the earlier steps' objects stay); 1 otherwise.
    int Open(NativeWindow window, NativeInstance instance, MouseReport report);
    void Close();
    // Takes the device or lets it go with the window's activation; 0 without a device, the report's answer when
    // taking it fails, else 1.
    int SetActive(bool active, MouseReport report);
    // The state: the motion in x, y and the wheel, and the buttons (four values; zeroed when the read fails).
    void Read(std::int32_t state[4]);

private:
    struct Device;                   // the backend's
    std::unique_ptr<Device> device;
};

// ==== The clipboard ==================================================================================================

// Text, in UTF-8.

// Puts `text` on the clipboard in place of what it held; nothing changes when the clipboard is busy.
void CopyText(std::string_view text);
// The clipboard's text; empty when it holds none or is busy.
std::string ClipboardText();

// ==== Sockets ========================================================================================================

// IPv4 UDP and TCP sockets, host names, waiting for readiness. The calls mirror BSD sockets one to one, each failing the
// way the system's does (the callers rely on it), so a port of the layer maps them straight onto its own sockets. The
// system's sockets start with the first call that needs them (Winsock 1.1) and stay up until the process ends. Byte
// order and IPv4 numbers are lib/ipv4.h's.

namespace net {

// A socket: the system's handle, pointer-sized (Winsock's SOCKET); kNoSocket when there is none.
using Socket = std::uintptr_t;
inline constexpr Socket kNoSocket = ~Socket{0};

// An IPv4 address with its port, as the game keeps and sends one: sockaddr_in's 16 bytes (the family, then the port
// and the address in network order, 8 zero bytes). Passed to the system as it is: an address whose family is not
// kIpv4 fails there.
struct Address {
    std::uint16_t family;
    std::uint16_t port;
    std::uint32_t ip;
    std::uint8_t zero[8];
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

// Sockets to wait on (an fd_set). Select leaves in each group only its ready sockets: a group asked again after a
// Select that found none ready is empty.
struct SocketGroup {
    static constexpr int kCapacity = 64;   // FD_SETSIZE

    int count = 0;
    Socket sockets[kCapacity];

    SocketGroup() = default;
    explicit SocketGroup(Socket socket) : count(1) { sockets[0] = socket; }
};

// Waits at most `microseconds` for a socket of `read` to have data (or its peer to close), of `write` to take
// writes (a connect done) or of `failed` to fail (a connect refused); null groups are not asked. The number of ready
// sockets, 0 at the timeout, or -1 on an error, which includes asking only empty groups.
int Select(SocketGroup* read, SocketGroup* write, SocketGroup* failed, std::int32_t microseconds);

// This host's name (gethostname) into `name`, `size` bytes with the terminator; false when it fails or does not fit.
bool HostName(char* name, int size);
// The first IPv4 address of the host `name` (gethostbyname: a name or an IPv4 number) in network order into `ip`;
// false when the host is unknown. Blocks while the name is looked up.
bool LookUp(const char* name, std::uint32_t* ip);

} // namespace net

// ==== Floating point =================================================================================================

// Puts the floating-point control state back at the end of a scope that calls code which may change it (the graphics
// driver): MXCSR's control bits. Set back only when they changed.
class FloatingPointGuard {
public:
    FloatingPointGuard();
    ~FloatingPointGuard();
    FloatingPointGuard(const FloatingPointGuard&) = delete;
    FloatingPointGuard& operator=(const FloatingPointGuard&) = delete;

private:
    unsigned int saved;
};

} // namespace platform
