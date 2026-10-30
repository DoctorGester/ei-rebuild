#pragma once

// The platform layer (docs/notes/platform.md): every call of the game's code into the operating system, one
// section per area. No system type crosses it: UTF-8 text, fixed-width integers, our own enums and structs, a
// native window as an opaque pointer. The backends: SDL3 for the window, its events, the cursor, the mouse, the
// clipboard, the message box, the clocks and the threads (src/platform/sdl/platform_sdl.cpp; in the harness's builds
// Windows' window-procedure path, src/platform/win32/platform_win32_window.cpp), Windows for the rest
// (src/platform/win32/platform_win32.cpp), the sound on SDL3's audio device (src/platform/audio_sdl.cpp), the movies
// in src/platform/bink.

#include "lib/bytes.h"

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

// A local date and time of day.
struct DateTime {
    int year;     // in full
    int month;    // 1-12
    int day;      // 1-31
    int hour;     // 0-23
    int minute;
    int second;
};
// The local date and time now.
DateTime LocalTime();
// strftime of the local time of `seconds` since 1970 UTC (a save's date, the log's time of day) in at most 127
// characters; empty when it has no local time or does not fit.
std::string FormatLocalTime(std::int64_t seconds, const char* format);

// ==== Output for a developer =========================================================================================

// Writes `text` to the debugger's output (OutputDebugString); nothing happens without a debugger.
void DebugOutput(const char* text);

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

// The full path of the running program's file.
std::string ExePath();
// The process's current folder, without a trailing separator but at a drive's root ("C:\").
std::string CurrentFolder();

// For the standalone's own files, which never went through the game's slots (the settings file; a harness run leaves
// them alone): the same, asked of the system directly.
// The full path of the running program's file.
std::string ProgramPath();
// The folder for the current user's program data, with a trailing separator; empty when the user has none.
std::string UserDataFolder();
// Makes `path` the process's current folder; false when it could not, with the system's error code in `error`.
bool SetCurrentFolder(const std::string& path, std::uint32_t* error);

// ==== The machine ====================================================================================================

// The size of a virtual memory page in bytes.
std::uint32_t PageSize();
// Whether the desktop smooths the edges of text; true when it cannot tell.
bool FontSmoothing();
// The size in pixels of the primary monitor's current mode (whatever the process's DPI awareness), straight from the
// system, not the slots the harness fixes; both stay as they are when it cannot tell.
void PrimaryDesktopSize(int* width, int* height);

// For the standalone's start-up, which runs beside the game's slots and asks the system directly.
// The value of the environment variable `name`; empty when it is not set (or empty).
std::string EnvironmentVariable(const char* name);
// Ends the program at once with `exitCode`, without running its exit functions, and never returns.
[[noreturn]] void EndProcess(std::uint32_t exitCode);

// ==== Folders, drives and whole files ================================================================================

// The file objects are the next section. Paths are UTF-8, with backslashes between folders as the game writes them.

// Creates the folder `path` (its parent must exist). False when it could not, also when it exists already.
bool CreateFolder(const char* path);
// The bytes this user may still write on the drive of `path` (a folder on it, or its root); 0 when unknown.
double FreeDiskSpace(const char* path);
// Deletes the file `path`. False when it could not.
bool RemoveFile(const char* path);
// Renames (moves) the file `from` to `to`; false when it could not, also when `to` exists.
bool RenameFile(const char* from, const char* to);
// Copies the file `from` over `to`, replacing it; false when it could not.
bool CopyFileOver(const char* from, const char* to);
// Removes the empty folder `path`. False when it could not.
bool RemoveFolder(const char* path);

// The whole file `path`, shared with any writer (an editor or a sync tool holding it open), for the standalone's own
// files, which the system reads directly and not through the game's slots. `missing` only when the file or its folder
// does not exist; a file of 16 MB or more fails. `error` is the system's error code of a failure.
enum class WholeFile { read, missing, failed };
WholeFile ReadWholeFile(const std::string& path, std::string* bytes, std::uint32_t* error);
// `bytes` written whole to `path`: to a file beside it, which is then moved over it (the path's folder made when it
// is missing). False when that failed, with the system's error code in `error`.
bool WriteWholeFile(const std::string& path, const std::string& bytes, std::uint32_t* error);

// Whether `path` names a file or a folder, asked of the system directly (the standalone's start-up looks at the exe's
// folder beside the game's slots).
bool PathExists(const std::string& path);

// Rebirth's file look-ups, which reached the system the way its own code did and not through the game's import slots
// (the harness's read redirection of the slots does not apply to them): the same calls as the slots' UTF-8 versions,
// made directly.
// Whether `path` names a file, not a folder.
bool IsFile(const std::string& path);
// One entry of a folder search.
struct FolderEntry {
    std::string name;
    bool folder;
};
// The entries `pattern` matches, in the system's search order (none when it matches nothing).
std::vector<FolderEntry> FolderEntries(const std::string& pattern);

// The label of the volume whose root is `root` ("D:\." or "D:\"); empty when it has none or the drive has no disc.
std::string VolumeLabel(const char* root);

// ==== Files ==========================================================================================================

// Paths are UTF-8. The Windows implementation goes through the game's import slots
// (CreateFileA, FindFirstFileA, GetFileAttributesA, ...): a mod's hook in the import table and the harness's save /
// install redirection apply to every file the game opens or lists. Every open shares the file with other readers and
// writers.
//   - File: a binary file on a system handle (the game's CFile uses), also written by text lines;
//   - ReadFile: a whole file at once (the text files, split into lines with str::Lines);
//   - MappedFile: a whole file mapped into memory, read only (the archives' and the registry files');
//   - FileFind: a folder search (its CFileFind uses);
//   - FileExists: _access.
// A read, write or seek that fails throws a FileError, where MFC threw a CFileException: what the game caught it
// catches; an uncaught one ends the game as before (a write to a full disk).

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

// A folder search as MFC's CFileFind: FindFile starts it, then each FindNext moves to the next entry and returns
// false when that entry is the last one (it is still there to read). The paths are the search pattern's folder made
// absolute, a separator and the entry's name.
class FileFind {
public:
    FileFind();
    ~FileFind();
    FileFind(const FileFind&) = delete;
    FileFind& operator=(const FileFind&) = delete;

    // Whether `pattern` matches anything (no entry is current yet).
    bool FindFile(std::string_view pattern);
    bool FindNext();
    void Close();

    std::string GetFileName() const;
    std::string GetFilePath() const;
    // The name without its extension.
    std::string GetTitle() const;
    // The low dword of the size.
    unsigned long GetLength() const;
    bool IsDirectory() const;
    // "." or "..".
    bool IsDots() const;

private:
    struct Search;                               // the system's search handle and the entry read ahead
    std::unique_ptr<Search> search;              // null when none
    // The current entry.
    bool hasFound = false;
    std::string name;
    unsigned long attributes = 0;
    unsigned long size = 0;
    std::string root;
};

// The whole file `path` into `bytes`, shared with other readers and writers; false (and `bytes` empty) when it does not
// open or a read fails.
bool ReadFile(const std::string& path, std::string* bytes);

// Whether `path` names a file or folder (the original's _access with mode 0 or 4).
bool FileExists(const std::string& path);

// ==== The .ini files =================================================================================================

// The system's profile reads. Paths and values are bytes as the file holds them (the config\*.ini files are the game's
// own and their names are ASCII).

// The entries ("key=value", in file order) of `section` in the file `path`; none when the section or file is missing.
// At most 0x5000 bytes of them are read.
std::vector<std::string> IniSectionEntries(const char* section, const char* path);
// The string of `key` in `section` of the file `path`, `defaultValue` when it is not there; at most 1023 bytes.
std::string IniString(const char* section, const char* key, const char* defaultValue, const char* path);

// The number `key` holds in `section` of the file `path`, `defaultValue` when it is not there or is no number; asked of
// the system directly (the standalone's start-up reads EI_Plugin.ini, beside the game's slots).
int IniInt(const char* section, const char* key, int defaultValue, const char* path);

// ==== Threads ========================================================================================================

// Worker threads and events. A lock is a std::recursive_mutex (a critical section is
// recursive); a thread's end is waited for with a Signal, whose calls stay on the import slots the harness traces.

using ThreadProcedure = unsigned (*)(void* parameter);

// A thread made as MFC's AfxBeginThread did: created suspended, the priority set (SetThreadPriority's values: 2 is
// the highest), then running. It ends when the procedure returns. Closing it (or destroying the object) lets go of
// the handle; the thread keeps running.
class Thread {
public:
    Thread() = default;
    ~Thread() { Close(); }
    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;

    // Starts `procedure(parameter)` at `priority`; false when the thread could not be made. A held handle is
    // closed first.
    bool Start(ThreadProcedure procedure, void* parameter, int priority);
    bool Started() const { return handle != nullptr; }
    void Close();

private:
    void* handle = nullptr;
};

// An auto-reset event (a signal from one thread to another), not signalled when made.
class Signal {
public:
    Signal() = default;
    ~Signal() { Close(); }
    Signal(const Signal&) = delete;
    Signal& operator=(const Signal&) = delete;

    // Makes the event (a made one is closed first).
    void Create();
    bool Created() const { return handle != nullptr; }
    void Set();
    // Waits until the event is signalled (no time limit).
    void Wait();
    void Close();

private:
    void* handle = nullptr;
};

// ==== The window and its events ======================================================================================

// docs/notes/platform.md, "Window and events". The window is a native one (an HWND on Windows) that the game passes
// around as an opaque pointer; what happens to it reaches the game as Events, through the handler given at the class's
// registration (events the system sends) and through the loop's NextEvent / TakeEvent (events waiting in the queue).
// The values inside an Event are the game's: key codes are the virtual-key codes (the next section), key flags and
// mouse buttons are as Windows gives them.

// A native window (an HWND on Windows), or a program instance (an HINSTANCE).
using NativeWindow = void*;
using NativeInstance = void*;

struct Point {
    int x;
    int y;
};
struct Rect {
    int left;
    int top;
    int right;
    int bottom;
};

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
    SetCursor,       // the system asks for the cursor's shape over the window
    SystemCommand,   // a command of the window's system menu or a system key (command)
    AutoPlay,        // the system asks whether a disc put in may start its AutoPlay
    Quit,            // the quit request (quitCode): the loop ends
    Other,           // anything else: DefaultHandling
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

// The backend's record of the system event an Event came from (Windows' window-procedure path: the message's id and
// its two values), for DefaultHandling and DispatchEvent only. Not for the game's code.
struct NativeEvent {
    std::uint32_t id;
    std::uintptr_t first;
    std::intptr_t second;
};

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
    NativeEvent native{};
};

// The game's handling of an event the system sends to its window: what the window procedure returns.
using EventHandler = std::intptr_t (*)(const Event& event);

// ---- The window --------------------------------------------------------------------------------------------------

// Registers the game window's class under `className`, its events going to `handler`; the window keeps the system's
// cursor hidden only as the handler does (no class cursor), has no icon and gets double clicks.
void RegisterWindowClass(NativeInstance instance, const char* className, EventHandler handler);
// The running program's own instance (the module the program was started from), asked of the system directly.
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
void Minimize(NativeWindow window);
void Restore(NativeWindow window);
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
// The system's own handling of an event the game does not handle; what the window procedure returns.
std::intptr_t DefaultHandling(const Event& event);
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

// Its shape over the game window (the game hides it and draws its own) and its position, in the game's points.

// A native cursor shape (an HCURSOR on Windows); null shows none.
using CursorShape = void*;

CursorShape CurrentCursor();
void SetCursor(CursorShape shape);

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

// IPv4 UDP and TCP sockets, host names and addresses, waiting for readiness. The
// calls mirror BSD sockets one to one, each failing the way the system's does (the callers rely on it), so a port
// of the layer maps them straight onto its own sockets.

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
// ParseIpv4's answer for a text that is not an IPv4 number (also the broadcast address 255.255.255.255).
inline constexpr std::uint32_t kNoAddress = 0xFFFFFFFFu;

enum class Kind : std::uint8_t { Udp, Tcp };

// Why the last call of this thread failed.
enum class Error : std::uint8_t { None, WouldBlock, AddressInUse, Refused, Other };

// Starts the system's sockets (Winsock 1.1); each Start that succeeded needs a Stop. False when they cannot run.
bool Start();
void Stop();

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
// The name of the host whose address is the `length` bytes at `address` (gethostbyaddr, IPv4: an address is 4
// bytes); empty when there is none.
std::string HostNameAt(const void* address, int length);
// "a.b.c.d" as an address in network order (inet_addr), or kNoAddress.
std::uint32_t ParseIpv4(const char* text);

// A port between the host's byte order and the network's (htons, ntohs).
std::uint16_t ToNetwork(std::uint16_t value);
std::uint16_t FromNetwork(std::uint16_t value);

} // namespace net

// ==== Sound ==========================================================================================================

// docs/notes/platform.md, "Audio and video": the output device, voices playing RIFF WAVE files from
// memory, the music's double-buffered stream, and the MP3 decoders the music and the briefing voices go through.
// Game logic reads back only when a voice has ended (its end callback), so every backend times that end the same
// way: by the wave's length (WaveMilliseconds) on the game's clock (platform::Milliseconds), delivered from the main
// loop (DeliverEnds), whatever the device does. That keeps the harness's runs deterministic (its virtual clock) and
// the same with every backend. Volumes and pans are the game's 0..127 scales.

namespace audio {

// ---- Waves in memory ----

inline constexpr std::int32_t kPcm = 1;
inline constexpr std::int32_t kImaAdpcm = 0x11;

// What a RIFF WAVE's header says.
struct WaveInfo {
    std::int32_t format = 0;          // the format tag: kPcm or kImaAdpcm
    std::uint32_t rate = 0;           // samples per second
    std::int32_t bits = 0;            // per sample (4 for IMA ADPCM)
    std::int32_t channels = 0;
    std::uint32_t samples = 0;        // per channel: the fact chunk's count for IMA ADPCM, else what the data holds
    std::uint32_t dataLength = 0;     // bytes of the data chunk
    std::uint32_t blockSize = 0;      // the fmt chunk's block align
    const std::uint8_t* data = nullptr;   // the data chunk; null when the file is not a RIFF WAVE
};
// The wave's header; walks the chunks only inside the `size` bytes and clamps the data chunk to them. A buffer with no
// data chunk in it comes back empty (data null).
WaveInfo ReadWaveInfo(const void* wave, std::size_t size);
// How long the wave plays once, in whole milliseconds: the data's bytes over its byte rate (for IMA ADPCM the rate
// times the block size over the samples in a block, truncated), truncated. Miles' rule, which the harness's runs
// timed every end by (checked on all 19k waves and briefing voices of the installs); 0 without a wave.
std::uint32_t WaveMilliseconds(const WaveInfo& info);

// ---- The device ----

// Opens the default output device for 16-bit stereo at `rate` (at most 44100), `reverb` making room for the voices'
// reverb. False with the reason in `error` when it cannot; then no voice or stream can be made. A backend may also
// go on without a device (UseNoDevice, or none to be had): the game then runs as with one, silently.
bool OpenOutput(NativeWindow window, unsigned rate, bool reverb, std::string* error);
// From now on OpenOutput opens no device: nothing is heard, and what the game reads back keeps to the game's clock
// as it does with one (the harness's runs: silent and the same on every machine).
void UseNoDevice();
// Closes the device; every voice and stream must be destroyed first.
void CloseOutput();

// ---- Voices ----

struct Voice;
using EndCallback = void (*)(void* user);

// A voice of the open device; null when none is open.
Voice* CreateVoice();
void DestroyVoice(Voice* voice);
// Stopped and empty: no wave, loop once, volume 127, centred, no reverb, no end callback, at the beginning.
void ResetVoice(Voice* voice);
// The RIFF WAVE (PCM or IMA ADPCM) the voice plays, from the beginning. The memory stays the caller's while the
// voice has it.
void SetWave(Voice* voice, const void* wave, std::size_t size);
// How often the wave plays: 0 for ever.
void SetLoopCount(Voice* voice, int count);
void SetVolume(Voice* voice, int volume);   // 0..127
void SetPan(Voice* voice, int pan);         // 0 left, 64 centre, 127 right
void SetReverb(Voice* voice, float room, float level, float time);
// Where in the wave it plays next: in milliseconds, or bytes into the data.
void SetPositionMs(Voice* voice, std::uint32_t ms);
void SetPosition(Voice* voice, std::int32_t bytes);
std::int32_t Position(const Voice* voice);
void Start(Voice* voice);
// Paused; Resume goes on from there (its end comes as much later).
void Stop(Voice* voice);
void Resume(Voice* voice);
// Stopped for good: no end callback.
void End(Voice* voice);
// `callback(user)` once the started wave has played (all its loops), from DeliverEnds.
void SetEndCallback(Voice* voice, EndCallback callback, void* user);
// Calls the end callbacks due at `now` (platform::Milliseconds), in the order the voices were made. The main loop
// calls it once a pass, right after reading the frame's time.
void DeliverEnds(std::uint32_t now);

// ---- The music stream ----

// Two halves played one after the other: the game fills whichever half is free (ReadyHalf) with the next buffer.
struct Stream;

// A stream of the open device; null when none is open.
Stream* CreateStream();
void DestroyStream(Stream* stream);
// The bytes a half should hold at least for the format to play without gaps.
int MinimumHalfBytes(unsigned rate, int channels, int bits);
// Stopped and empty, both halves free.
void ResetStream(Stream* stream);
// The format of the halves queued from now on (16 bits signed, else 8 bits unsigned).
void SetStreamFormat(Stream* stream, unsigned rate, int channels, int bits);
void SetStreamVolume(Stream* stream, int volume);   // 0..127
void SetStreamPan(Stream* stream, int pan);         // 0..127
// The half to fill next (0 or 1), or -1 while both wait to play.
int ReadyHalf(Stream* stream);
// Queues `bytes` of `data` in `half`; the memory stays the caller's until the half is ready again.
void QueueHalf(Stream* stream, int half, const void* data, int bytes);
// Paused; Resume goes on.
void StopStream(Stream* stream);
void ResumeStream(Stream* stream);

// ---- MP3 ----

// The music's decoder reads its file through `read`: `bytes` into `to`, from `offset` when it is not negative,
// else from where the last read ended. Returns the bytes read.
using ReadFn = int (*)(void* user, char* to, int bytes, int offset);
struct Mp3Stream;

// Opens the MP3 file `read` gives; null when it holds no MP3 frame.
Mp3Stream* OpenMp3(ReadFn read, void* user);
void CloseMp3(Mp3Stream* mp3);
// The decoded samples' format.
unsigned Mp3Rate(const Mp3Stream* mp3);
int Mp3Channels(const Mp3Stream* mp3);
int Mp3Bits(const Mp3Stream* mp3);   // 16
// Decodes up to `bytes` into `to`; the bytes written, fewer at the end of the file.
int DecodeMp3(Mp3Stream* mp3, void* to, int bytes);
// From the file's beginning again.
void RewindMp3(Mp3Stream* mp3);

// An MP3 file decoded whole into a 16-bit PCM RIFF WAVE (44-byte header) at the file's rate and channels, with every
// whole frame's samples (1152 a frame for MPEG-1, 576 for MPEG-2 and 2.5: what Miles gave, so the voice's length is
// the same). False when it holds no MP3 frame.
bool DecodeMp3File(const void* data, std::size_t size, std::vector<std::uint8_t>* wave);

} // namespace audio

// ==== Movies =========================================================================================================

// docs/notes/platform.md, "Audio and video": Bink 1 files (.bik) decoded a frame at a time into a CPU buffer,
// which the renderer takes (render::MovieFrame). The game reads back the frame numbers (the movie ends when the last
// frame was decoded; the loading screen's movie stops at its last frame) and asks whether the next frame is due,
// which the movie's own rate decides on a real-time clock (not the game's: the blocking player's loop does not move
// the harness's virtual clock). Frames are numbered from 1.

namespace video {

struct Movie;

// Opens the movie file `path` (UTF-8); null when it cannot be opened or is not a Bink 1 movie. With `sound` its
// sound track plays on the audio output.
Movie* Open(const char* path, bool sound);
void Close(Movie* movie);   // null: nothing

int Width(const Movie* movie);
int Height(const Movie* movie);
unsigned FrameCount(const Movie* movie);
// The frame DecodeFrame decodes next.
unsigned NextFrameNumber(const Movie* movie);
// The frame decoded last (0 before the first).
unsigned DecodedFrameNumber(const Movie* movie);

// True while the next frame is not due yet.
bool Waiting(Movie* movie);
// Decodes the next frame (it becomes the decoded one).
void DecodeFrame(Movie* movie);
// The decoded frame as R, G, B, X bytes, Width x Height, rows `pitch` bytes apart.
void CopyFrame(const Movie* movie, void* to, int pitch);
// Moves on a frame; after the last, back to the first.
void Advance(Movie* movie);
// The frame decoded next becomes `frame` (at most the last).
void GoToFrame(Movie* movie, unsigned frame);
void SetVolume(Movie* movie, int volume);   // 0..32768

} // namespace video

// ==== Floating point =================================================================================================

// Puts the floating-point control state back at the end of a scope that calls code which may change it (the graphics
// driver): the x87 control word on 32-bit x86 (the game's main thread runs at 24-bit precision,
// docs/notes/rebirth.md), MXCSR's control bits on x64. Set back only when it changed.
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

// ==== Caller capture =================================================================================================

// For the observers that note who called (the harness's records). Used in a function's body: EI_RETURN_ADDRESS() is
// that function's return address, EI_CALLER_FRAME() the stack from the word that holds it (the observers scan on
// from there). Macros, not functions: the debug build inlines nothing, and a helper would return into itself.
#if defined(_MSC_VER)
extern "C" void* _ReturnAddress();            // declared as <intrin.h> declares them, without the rest of it
extern "C" void* _AddressOfReturnAddress();
#pragma intrinsic(_ReturnAddress, _AddressOfReturnAddress)
#define EI_RETURN_ADDRESS() _ReturnAddress()
#define EI_CALLER_FRAME() static_cast<void* const*>(_AddressOfReturnAddress())
#else
#define EI_RETURN_ADDRESS() __builtin_extract_return_addr(__builtin_return_address(0))
#define EI_CALLER_FRAME() (static_cast<void* const*>(__builtin_frame_address(0)) + 1)
#endif
