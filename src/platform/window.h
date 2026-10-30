#pragma once

// The game window and its events (docs/notes/platform.md, "Window and events"). The window is a native one (an HWND on
// Windows) that the game passes around as an opaque pointer; what happens to it reaches the game as Events, through
// the handler given at the class's registration (events the system sends) and through the loop's NextEvent /
// TakeEvent (events waiting in the queue). The values inside an Event are the game's: key codes are the virtual-key
// codes (lib/win_user.h), key flags and mouse buttons are as Windows gives them.

#include <cstdint>

namespace platform {

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

// The system's message an Event came from: what DefaultHandling and DispatchEvent pass on. Not for the game's code.
struct NativeMessage {
    NativeWindow window;
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
    NativeMessage native{};
};

// The game's handling of an event the system sends to its window: what the window procedure returns.
using EventHandler = std::intptr_t (*)(const Event& event);

// ---- The window --------------------------------------------------------------------------------------------------

// Registers the game window's class under `className`, its events going to `handler`; the window keeps the system's
// cursor hidden only as the handler does (no class cursor), has no icon and gets double clicks.
void RegisterWindowClass(NativeInstance instance, const char* className, EventHandler handler);
// A window of the registered class with the title `title` (a game that runs already), or null.
NativeWindow FindClassWindow(const char* title);
// The id of the process that owns `window`.
std::uint32_t WindowProcess(NativeWindow window);
// Brings another program's window to the front, restored from the task bar, unless it is in front already.
void BringToFront(NativeWindow window);
// The game window: a popup of the registered class at the screen's top left, `width` x `height`; null when it could
// not be made.
NativeWindow CreateGameWindow(NativeInstance instance, const char* title, int width, int height);
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

} // namespace platform
