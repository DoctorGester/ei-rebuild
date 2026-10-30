// The platform layer on SDL3 (platform/platform.h; docs/notes/platform.md, "SDL3"): the clocks, the message box, the
// threads, the window and its events, the cursor, the mouse and the clipboard, in platform.h's order. Built into the
// builds without the harness (release, debug, play, x64); the harness's builds take
// src/platform/win32/platform_win32_window.cpp, whose window procedure and import slots the harness drives. The game
// keeps its own loops (each takes SDL's events through EventWaits / NextEvent / TakeEvent); the renderer keeps its own
// swap chain on the window's native handle.
#include "platform/platform.h"

#define SDL_MAIN_HANDLED   // SDL_main.h only for SDL_RegisterApp: the program keeps its own entry
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <thread>

namespace platform {
namespace {

// The game window (one) and its native handle, which the game holds as its NativeWindow.
SDL_Window* g_window = nullptr;
NativeWindow g_native = nullptr;

SDL_Window* WindowOf(NativeWindow window) { return window && window == g_native ? g_window : nullptr; }

} // namespace

// ==== Clocks =========================================================================================================

// Both clocks are SDL's millisecond count, from SDL's first read plus a day: the values the original read on a machine up
// for a day, not the near-zero ones of a fresh count (the game keeps 0 as "never" in times it compares with them).

namespace {

constexpr std::uint64_t kClockStart = 24ull * 60 * 60 * 1000;

std::uint32_t Clock() { return static_cast<std::uint32_t>(SDL_GetTicks() + kClockStart); }

} // namespace

std::uint32_t Milliseconds() { return Clock(); }

std::uint32_t TickCount() { return Clock(); }

void Sleep(std::uint32_t ms) {
    if (ms == 0)
        std::this_thread::yield();
    else
        SDL_Delay(ms);
}

DateTime LocalTime() {
    SDL_Time now = 0;
    SDL_DateTime local{};
    if (!SDL_GetCurrentTime(&now) || !SDL_TimeToDateTime(now, &local, true)) return {};
    return {local.year, local.month, local.day, local.hour, local.minute, local.second};
}

// ==== The message box ================================================================================================

MessageAnswer ShowMessageBox(void* owner, const char* text, const char* caption, MessageButtons buttons,
                             MessageIcon icon) {
    constexpr int kOk = 0;
    const SDL_MessageBoxButtonData choices[] = {
        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, kOk, "OK"},
        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 1, "Cancel"},
    };
    SDL_MessageBoxData box{};
    box.flags = (icon == MessageIcon::Error ? SDL_MESSAGEBOX_ERROR : SDL_MESSAGEBOX_WARNING) |
                SDL_MESSAGEBOX_BUTTONS_LEFT_TO_RIGHT;
    box.window = WindowOf(owner);
    box.title = caption;
    box.message = text;
    box.numbuttons = buttons == MessageButtons::OkCancel ? 2 : 1;
    box.buttons = choices;
    int chosen = kOk;
    if (!SDL_ShowMessageBox(&box, &chosen)) return MessageAnswer::Ok;
    // A box with Cancel closed without a button (Escape, its close box) was cancelled.
    return buttons == MessageButtons::OkCancel && chosen != kOk ? MessageAnswer::Cancel : MessageAnswer::Ok;
}

// ==== Threads ========================================================================================================

// SDL's threads; a thread sets its own priority as it starts (SDL sets only the calling thread's).

namespace {

struct ThreadStart {
    ThreadProcedure procedure;
    void* parameter;
    SDL_ThreadPriority priority;
};

int SDLCALL RunThread(void* data) {
    const ThreadStart start = *static_cast<ThreadStart*>(data);
    delete static_cast<ThreadStart*>(data);
    SDL_SetCurrentThreadPriority(start.priority);
    return static_cast<int>(start.procedure(start.parameter));
}

// SetThreadPriority's values (15 time-critical, 1 and 2 above normal, below 0 below it) as SDL's.
SDL_ThreadPriority PriorityOf(int priority) {
    if (priority >= 15) return SDL_THREAD_PRIORITY_TIME_CRITICAL;
    if (priority > 0) return SDL_THREAD_PRIORITY_HIGH;
    if (priority < 0) return SDL_THREAD_PRIORITY_LOW;
    return SDL_THREAD_PRIORITY_NORMAL;
}

// A Signal's state: set until a Wait takes it.
struct SignalState {
    SDL_Mutex* lock = SDL_CreateMutex();
    SDL_Condition* changed = SDL_CreateCondition();
    bool set = false;

    ~SignalState() {
        SDL_DestroyCondition(changed);
        SDL_DestroyMutex(lock);
    }
};

} // namespace

bool Thread::Start(ThreadProcedure procedure, void* parameter, int priority) {
    Close();
    auto* start = new ThreadStart{procedure, parameter, PriorityOf(priority)};
    SDL_Thread* thread = SDL_CreateThread(RunThread, "game", start);
    if (!thread) {
        delete start;
        return false;
    }
    handle = thread;
    return true;
}

void Thread::Close() {
    if (handle) SDL_DetachThread(static_cast<SDL_Thread*>(handle));
    handle = nullptr;
}

void Signal::Create() {
    Close();
    handle = new SignalState;
}

void Signal::Set() {
    auto* state = static_cast<SignalState*>(handle);
    SDL_LockMutex(state->lock);
    state->set = true;
    SDL_SignalCondition(state->changed);
    SDL_UnlockMutex(state->lock);
}

void Signal::Wait() {
    auto* state = static_cast<SignalState*>(handle);
    SDL_LockMutex(state->lock);
    while (!state->set) SDL_WaitCondition(state->changed, state->lock);
    state->set = false;
    SDL_UnlockMutex(state->lock);
}

void Signal::Close() {
    delete static_cast<SignalState*>(handle);
    handle = nullptr;
}

// ==== The window and its events ======================================================================================

// SDL's window and events turned into the game's Events (docs/notes/platform.md, "SDL3"): keys with the virtual-key
// codes and the key flags Windows gave (the scan code, Alt, repeat, release), the characters a key types (SDL's text,
// and the control characters Windows' TranslateMessage made: Backspace, Tab, Enter, Escape, Ctrl+letter), the mouse
// with Windows' button bits and double clicks. Window events (activation, painting, the close request) go to the
// handler at once, as the window procedure's messages did; input waits in the queue for the game's loops.

namespace {

EventHandler g_handler = nullptr;
// The events taken from SDL and not yet from us.
std::deque<Event> g_waiting;
// PostQuit's request: NextEvent gives it once nothing else waits.
bool g_quitPosted = false;
int g_quitCode = 0;
// The activation the handler was told last (an Activate event only on a change).
bool g_told = false;
bool g_toldActive = false;
bool g_toldMinimized = false;
// The mouse buttons held, as Windows' MK_ bits.
int g_buttons = 0;
// The key flags of the last key pressed: the characters it types carry them (WM_CHAR had the key's).
int g_typedFlags = 0;
// The mouse's motion and wheel since MouseDevice::Read took them (the cursor's thread reads them).
std::atomic<std::int32_t> g_motionX{0};
std::atomic<std::int32_t> g_motionY{0};
std::atomic<std::int32_t> g_motionWheel{0};

constexpr int kKeyRepeated = 0x4000;   // the key was down before
constexpr int kKeyReleased = 0xC000;   // a release: down before, going up
constexpr int kMouseLeft = 0x01;       // MK_LBUTTON ...
constexpr int kMouseRight = 0x02;
constexpr int kMouseShift = 0x04;
constexpr int kMouseControl = 0x08;
constexpr int kMouseMiddle = 0x10;
constexpr int kMouseX1 = 0x20;
constexpr int kMouseX2 = 0x40;
constexpr int kWheelNotch = 120;

void Send(Event event) {
    event.window = g_native;
    if (g_handler) g_handler(event);
}

void Queue(Event event) {
    event.window = g_native;
    g_waiting.push_back(event);
}

bool Minimized() { return g_window && (SDL_GetWindowFlags(g_window) & SDL_WINDOW_MINIMIZED) != 0; }

void Activation(bool active, bool minimized) {
    if (g_told && active == g_toldActive && minimized == g_toldMinimized) return;
    g_told = true;
    g_toldActive = active;
    g_toldMinimized = minimized;
    Event event;
    event.type = EventType::Activate;
    event.activation = active ? Activation::Active : Activation::Inactive;
    event.minimized = minimized;
    Send(event);
}

// ---- Keys --------------------------------------------------------------------------------------------------------

// A key as Windows named it: the virtual-key code and the scan code (set 1, 0x100 for an extended key).
struct KeyCode {
    int key;
    int scan;
};

// The virtual-key codes of the keys the layout moves: a letter or digit key gives the code of what it types (SDL's key
// code, Latin letters on every layout: SDL_HINT_KEYCODE_OPTIONS), as Windows' layouts did; 0 for another.
int LayoutKey(SDL_Keycode typed) {
    if (typed >= 'a' && typed <= 'z') return static_cast<int>(typed - 'a' + 'A');
    if (typed >= '0' && typed <= '9') return static_cast<int>(typed);
    return 0;
}

// The key of `scancode` (its place on the keyboard), as Windows' US layout names it; {0, 0} for a key the game has no
// code for. The number pad gives its digits with Num Lock on, else the keys printed below them.
KeyCode KeyOf(SDL_Scancode scancode, SDL_Keycode typed, SDL_Keymod mod) {
    static constexpr int kLetterScans[26] = {0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32,
                                             0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C};
    if (scancode >= SDL_SCANCODE_A && scancode <= SDL_SCANCODE_Z) {
        const int index = scancode - SDL_SCANCODE_A;
        const int key = LayoutKey(typed);
        return {key ? key : 'A' + index, kLetterScans[index]};
    }
    if (scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_0) {
        const int index = scancode - SDL_SCANCODE_1;   // 1..9, then 0
        const int key = LayoutKey(typed);
        return {key ? key : (index == 9 ? '0' : '1' + index), 0x02 + index};
    }
    if (scancode >= SDL_SCANCODE_F1 && scancode <= SDL_SCANCODE_F10)
        return {0x70 + (scancode - SDL_SCANCODE_F1), 0x3B + (scancode - SDL_SCANCODE_F1)};
    const bool numbers = (mod & SDL_KMOD_NUM) != 0;
    switch (scancode) {
    case SDL_SCANCODE_RETURN: return {kKeyReturn, 0x1C};
    case SDL_SCANCODE_ESCAPE: return {kKeyEscape, 0x01};
    case SDL_SCANCODE_BACKSPACE: return {kKeyBack, 0x0E};
    case SDL_SCANCODE_TAB: return {kKeyTab, 0x0F};
    case SDL_SCANCODE_SPACE: return {kKeySpace, 0x39};
    case SDL_SCANCODE_MINUS: return {0xBD, 0x0C};          // VK_OEM_MINUS
    case SDL_SCANCODE_EQUALS: return {0xBB, 0x0D};         // VK_OEM_PLUS
    case SDL_SCANCODE_LEFTBRACKET: return {0xDB, 0x1A};    // VK_OEM_4
    case SDL_SCANCODE_RIGHTBRACKET: return {0xDD, 0x1B};   // VK_OEM_6
    case SDL_SCANCODE_BACKSLASH: return {0xDC, 0x2B};      // VK_OEM_5
    case SDL_SCANCODE_NONUSHASH: return {0xDC, 0x2B};
    case SDL_SCANCODE_SEMICOLON: return {0xBA, 0x27};      // VK_OEM_1
    case SDL_SCANCODE_APOSTROPHE: return {0xDE, 0x28};     // VK_OEM_7
    case SDL_SCANCODE_GRAVE: return {0xC0, 0x29};          // VK_OEM_3: the key under Escape (the console's)
    case SDL_SCANCODE_COMMA: return {0xBC, 0x33};          // VK_OEM_COMMA
    case SDL_SCANCODE_PERIOD: return {0xBE, 0x34};         // VK_OEM_PERIOD
    case SDL_SCANCODE_SLASH: return {0xBF, 0x35};          // VK_OEM_2
    case SDL_SCANCODE_NONUSBACKSLASH: return {0xE2, 0x56}; // VK_OEM_102
    case SDL_SCANCODE_CAPSLOCK: return {0x14, 0x3A};
    case SDL_SCANCODE_F11: return {0x7A, 0x57};
    case SDL_SCANCODE_F12: return {0x7B, 0x58};
    case SDL_SCANCODE_PRINTSCREEN: return {0x2C, 0x137};
    case SDL_SCANCODE_SCROLLLOCK: return {0x91, 0x46};
    case SDL_SCANCODE_PAUSE: return {0x13, 0x45};
    case SDL_SCANCODE_INSERT: return {0x2D, 0x152};
    case SDL_SCANCODE_HOME: return {kKeyHome, 0x147};
    case SDL_SCANCODE_PAGEUP: return {kKeyPageUp, 0x149};
    case SDL_SCANCODE_DELETE: return {kKeyDelete, 0x153};
    case SDL_SCANCODE_END: return {kKeyEnd, 0x14F};
    case SDL_SCANCODE_PAGEDOWN: return {kKeyPageDown, 0x151};
    case SDL_SCANCODE_RIGHT: return {kKeyRight, 0x14D};
    case SDL_SCANCODE_LEFT: return {kKeyLeft, 0x14B};
    case SDL_SCANCODE_DOWN: return {kKeyDown, 0x150};
    case SDL_SCANCODE_UP: return {kKeyUp, 0x148};
    case SDL_SCANCODE_NUMLOCKCLEAR: return {0x90, 0x145};
    case SDL_SCANCODE_KP_DIVIDE: return {0x6F, 0x135};
    case SDL_SCANCODE_KP_MULTIPLY: return {0x6A, 0x37};
    case SDL_SCANCODE_KP_MINUS: return {0x6D, 0x4A};
    case SDL_SCANCODE_KP_PLUS: return {0x6B, 0x4E};
    case SDL_SCANCODE_KP_ENTER: return {kKeyReturn, 0x11C};
    case SDL_SCANCODE_KP_1: return {numbers ? 0x61 : kKeyEnd, 0x4F};
    case SDL_SCANCODE_KP_2: return {numbers ? 0x62 : kKeyDown, 0x50};
    case SDL_SCANCODE_KP_3: return {numbers ? 0x63 : kKeyPageDown, 0x51};
    case SDL_SCANCODE_KP_4: return {numbers ? 0x64 : kKeyLeft, 0x4B};
    case SDL_SCANCODE_KP_5: return {numbers ? 0x65 : 0x0C, 0x4C};   // VK_CLEAR
    case SDL_SCANCODE_KP_6: return {numbers ? 0x66 : kKeyRight, 0x4D};
    case SDL_SCANCODE_KP_7: return {numbers ? 0x67 : kKeyHome, 0x47};
    case SDL_SCANCODE_KP_8: return {numbers ? 0x68 : kKeyUp, 0x48};
    case SDL_SCANCODE_KP_9: return {numbers ? 0x69 : kKeyPageUp, 0x49};
    case SDL_SCANCODE_KP_0: return {numbers ? 0x60 : 0x2D, 0x52};
    case SDL_SCANCODE_KP_PERIOD: return {numbers ? 0x6E : kKeyDelete, 0x53};
    case SDL_SCANCODE_APPLICATION: return {0x5D, 0x15D};
    case SDL_SCANCODE_LCTRL: return {kKeyControl, 0x1D};
    case SDL_SCANCODE_RCTRL: return {kKeyControl, 0x11D};
    case SDL_SCANCODE_LSHIFT: return {kKeyShift, 0x2A};
    case SDL_SCANCODE_RSHIFT: return {kKeyShift, 0x36};
    case SDL_SCANCODE_LALT: return {kKeyMenu, 0x38};
    case SDL_SCANCODE_RALT: return {kKeyMenu, 0x138};
    case SDL_SCANCODE_LGUI: return {0x5B, 0x15B};
    case SDL_SCANCODE_RGUI: return {0x5C, 0x15C};
    default: return {0, 0};
    }
}

// The control character TranslateMessage made of a key press (a WM_CHAR), or 0. Alt without Ctrl made none for the
// game (WM_SYSCHAR); SDL's text has no control characters.
int ControlCharacter(int key, SDL_Keymod mod) {
    const bool control = (mod & SDL_KMOD_CTRL) != 0;
    const bool alt = (mod & SDL_KMOD_ALT) != 0;
    if (alt) return 0;
    if (control) {
        if (key >= 'A' && key <= 'Z') return key - 'A' + 1;
        if (key == kKeyBack) return 0x7F;
        if (key == kKeyReturn) return 0x0A;
        return 0;
    }
    switch (key) {
    case kKeyBack: return 0x08;
    case kKeyTab: return 0x09;
    case kKeyReturn: return 0x0D;
    case kKeyEscape: return 0x1B;
    default: return 0;
    }
}

void QueueCharacter(int unit) {
    Event event;
    event.type = EventType::Character;
    event.key = unit;
    event.repeat = 1;
    event.keyFlags = g_typedFlags;
    Queue(event);
}

void TakeKey(const SDL_KeyboardEvent& k) {
    const KeyCode code = KeyOf(k.scancode, k.key, k.mod);
    if (code.key == 0) return;
    const bool alt = (k.mod & SDL_KMOD_ALT) != 0;
    // Windows' system keys: with Alt held but not Ctrl (AltGr is both), and F10.
    const bool system = (alt && (k.mod & SDL_KMOD_CTRL) == 0) || code.key == 0x79;
    int flags = code.scan | (alt ? kKeyAltDown : 0);
    if (!k.down)
        flags |= kKeyReleased;
    else if (k.repeat)
        flags |= kKeyRepeated;
    Event event;
    if (k.down)
        event.type = system ? EventType::SystemKeyDown : EventType::KeyDown;
    else
        event.type = system ? EventType::SystemKeyUp : EventType::KeyUp;
    event.key = code.key;
    event.repeat = 1;
    event.keyFlags = static_cast<std::int16_t>(flags);   // sign-extended, as the message data's high half
    Queue(event);
    if (!k.down) return;
    g_typedFlags = event.keyFlags;
    if (const int character = ControlCharacter(code.key, k.mod)) QueueCharacter(character);
}

// SDL's typed text as UTF-16 units (a character past U+FFFF as a surrogate pair), each a Character event.
void TakeText(const char* text) {
    std::size_t left = SDL_strlen(text);
    while (left > 0) {
        const Uint32 character = SDL_StepUTF8(&text, &left);
        if (character == 0) break;
        if (character > 0xFFFF) {
            QueueCharacter(static_cast<int>(0xD800 + ((character - 0x10000) >> 10)));
            QueueCharacter(static_cast<int>(0xDC00 + ((character - 0x10000) & 0x3FF)));
        } else {
            QueueCharacter(static_cast<int>(character));
        }
    }
}

// ---- The mouse ---------------------------------------------------------------------------------------------------

int ButtonBit(Uint8 button) {
    switch (button) {
    case SDL_BUTTON_LEFT: return kMouseLeft;
    case SDL_BUTTON_RIGHT: return kMouseRight;
    case SDL_BUTTON_MIDDLE: return kMouseMiddle;
    case SDL_BUTTON_X1: return kMouseX1;
    case SDL_BUTTON_X2: return kMouseX2;
    default: return 0;
    }
}

// The buttons held and Shift and Ctrl, as a mouse message's MK_ bits.
int Buttons() {
    const SDL_Keymod mod = SDL_GetModState();
    return g_buttons | ((mod & SDL_KMOD_SHIFT) ? kMouseShift : 0) | ((mod & SDL_KMOD_CTRL) ? kMouseControl : 0);
}

int Whole(float coordinate) { return static_cast<int>(std::floor(coordinate)); }

void QueueMouse(EventType type, float x, float y) {
    Event event;
    event.type = type;
    event.buttons = Buttons();
    event.x = static_cast<std::int16_t>(Whole(x));
    event.y = static_cast<std::int16_t>(Whole(y));
    Queue(event);
}

void TakeButton(const SDL_MouseButtonEvent& b) {
    const int bit = ButtonBit(b.button);
    if (b.down)
        g_buttons |= bit;
    else
        g_buttons &= ~bit;
    // Windows' second click of a double click (the fourth, ...) came as its own message.
    const bool twice = b.down && b.clicks % 2 == 0;
    if (bit == kMouseLeft)
        QueueMouse(b.down ? (twice ? EventType::LeftDouble : EventType::LeftDown) : EventType::LeftUp, b.x, b.y);
    else if (bit == kMouseRight)
        QueueMouse(b.down ? (twice ? EventType::RightDouble : EventType::RightDown) : EventType::RightUp, b.x, b.y);
}

// A wheel turn: 120 a notch, positive away from the player; at the cursor's screen position, as WM_MOUSEWHEEL.
void TakeWheel(const SDL_MouseWheelEvent& w) {
    const float notches = w.direction == SDL_MOUSEWHEEL_FLIPPED ? -w.y : w.y;
    const int delta = static_cast<int>(std::lround(notches * kWheelNotch));
    if (delta == 0) return;
    g_motionWheel += delta;
    int left = 0, top = 0;
    SDL_GetWindowPosition(g_window, &left, &top);
    Event event;
    event.type = EventType::Wheel;
    event.buttons = Buttons();
    event.wheelDelta = delta;
    event.x = static_cast<std::int16_t>(Whole(w.mouse_x) + left);
    event.y = static_cast<std::int16_t>(Whole(w.mouse_y) + top);
    Queue(event);
}

// ---- SDL's events ------------------------------------------------------------------------------------------------

void Take(const SDL_Event& e) {
    switch (e.type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: TakeKey(e.key); break;
    case SDL_EVENT_TEXT_INPUT: TakeText(e.text.text); break;
    case SDL_EVENT_MOUSE_MOTION:
        g_motionX += Whole(e.motion.xrel);
        g_motionY += Whole(e.motion.yrel);
        QueueMouse(EventType::MouseMove, e.motion.x, e.motion.y);
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: TakeButton(e.button); break;
    case SDL_EVENT_MOUSE_WHEEL: TakeWheel(e.wheel); break;
    case SDL_EVENT_WINDOW_EXPOSED: {
        Event paint;
        paint.type = EventType::Paint;
        Send(paint);
        break;
    }
    case SDL_EVENT_WINDOW_FOCUS_GAINED: Activation(true, Minimized()); break;
    case SDL_EVENT_WINDOW_FOCUS_LOST: Activation(false, Minimized()); break;
    case SDL_EVENT_WINDOW_MINIMIZED: Activation(false, true); break;
    case SDL_EVENT_WINDOW_RESTORED:
        if (g_window && (SDL_GetWindowFlags(g_window) & SDL_WINDOW_INPUT_FOCUS)) Activation(true, false);
        break;
    // The player closes the window (or the system ends the session): the game decides, as on WM_DESTROY.
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
    case SDL_EVENT_QUIT: {
        Event destroy;
        destroy.type = EventType::Destroy;
        Send(destroy);
        break;
    }
    default: break;
    }
}

// SDL's events taken until one of ours waits (or none is left).
void Fill() {
    SDL_Event e;
    while (g_waiting.empty() && SDL_PollEvent(&e)) Take(e);
}

Event QuitEvent() {
    Event quit;
    quit.type = EventType::Quit;
    quit.quitCode = g_quitCode;
    g_quitPosted = false;
    return quit;
}

} // namespace

// ---- The window --------------------------------------------------------------------------------------------------

void RegisterWindowClass(NativeInstance instance, const char* className, EventHandler handler) {
    g_handler = handler;
    SDL_SetHint(SDL_HINT_WINDOWS_CLOSE_ON_ALT_F4, "0");      // the game takes Alt+F4 itself
    SDL_SetHint(SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE, "0");    // closing the window is a request (Destroy)
    SDL_SetHint(SDL_HINT_MOUSE_AUTO_CAPTURE, "0");           // mouse events only over the window, as Windows gave them
    // The window class's name (FindClassWindow finds a running game by it); SDL counts double clicks itself.
    SDL_RegisterApp(className, 0, instance);
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
        DebugOutput((std::string("platform: no SDL video: ") + SDL_GetError() + "\n").c_str());
}

NativeWindow CreateGameWindow(NativeInstance, const char* title, int width, int height) {
    const SDL_PropertiesID properties = SDL_CreateProperties();
    SDL_SetStringProperty(properties, SDL_PROP_WINDOW_CREATE_TITLE_STRING, title);
    SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_X_NUMBER, 0);
    SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_Y_NUMBER, 0);
    SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, width);
    SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, height);
    SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_BORDERLESS_BOOLEAN, true);
    SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_HIDDEN_BOOLEAN, true);
    g_window = SDL_CreateWindowWithProperties(properties);
    SDL_DestroyProperties(properties);
    if (!g_window) return nullptr;
    g_native = SDL_GetPointerProperty(SDL_GetWindowProperties(g_window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    SDL_StartTextInput(g_window);
    return g_native;
}

void ShowGameWindow(NativeWindow window, int) {
    if (SDL_Window* w = WindowOf(window)) SDL_ShowWindow(w);
}

bool IsMinimized(NativeWindow window) { return WindowOf(window) && Minimized(); }

void Minimize(NativeWindow window) {
    if (SDL_Window* w = WindowOf(window)) SDL_MinimizeWindow(w);
}

void Restore(NativeWindow window) {
    if (SDL_Window* w = WindowOf(window)) SDL_RestoreWindow(w);
}

// SDL marks the window painted itself.
void MarkPainted(NativeWindow) {}

// The desktop's compositor repaints it (no display mode ever changes).
void RepaintDesktop() {}

void ScreenToClient(NativeWindow window, Point* point) {
    int left = 0, top = 0;
    if (SDL_Window* w = WindowOf(window)) SDL_GetWindowPosition(w, &left, &top);
    point->x -= left;
    point->y -= top;
}

void ClientToScreen(NativeWindow window, Point* point) {
    int left = 0, top = 0;
    if (SDL_Window* w = WindowOf(window)) SDL_GetWindowPosition(w, &left, &top);
    point->x += left;
    point->y += top;
}

bool CoverMonitor(NativeWindow window, Rect* was, Rect* monitor) {
    SDL_Window* w = WindowOf(window);
    if (!w || Minimized()) return false;
    SDL_Rect bounds{};
    if (!SDL_GetDisplayBounds(SDL_GetDisplayForWindow(w), &bounds)) return false;
    int left = 0, top = 0, width = 0, height = 0;
    SDL_GetWindowPosition(w, &left, &top);
    SDL_GetWindowSize(w, &width, &height);
    if (left == bounds.x && top == bounds.y && width == bounds.w && height == bounds.h) return false;
    *was = Rect{left, top, left + width, top + height};
    *monitor = Rect{bounds.x, bounds.y, bounds.x + bounds.w, bounds.y + bounds.h};
    SDL_SetWindowPosition(w, bounds.x, bounds.y);
    SDL_SetWindowSize(w, bounds.w, bounds.h);
    return true;
}

// ---- Events ------------------------------------------------------------------------------------------------------

bool EventWaits() {
    Fill();
    return !g_waiting.empty() || g_quitPosted;
}

bool NextEvent(Event* event) {
    for (;;) {
        Fill();
        if (!g_waiting.empty()) {
            *event = g_waiting.front();
            g_waiting.pop_front();
            return true;
        }
        if (g_quitPosted) {
            *event = QuitEvent();
            return false;
        }
        SDL_WaitEvent(nullptr);
    }
}

bool TakeEvent(Event* event) {
    Fill();
    if (!g_waiting.empty()) {
        *event = g_waiting.front();
        g_waiting.pop_front();
        return true;
    }
    if (!g_quitPosted) return false;
    *event = QuitEvent();
    return true;
}

// SDL has handled the system's side of every event already.
std::intptr_t DefaultHandling(const Event&) { return 0; }

void DispatchEvent(const Event& event) {
    if (g_handler) g_handler(event);
}

void PostQuit(int code) {
    g_quitPosted = true;
    g_quitCode = code;
}

// ==== The cursor =====================================================================================================

namespace {

PointMap g_toGame = nullptr;
PointMap g_toScreen = nullptr;

} // namespace

// The shape shown over the window; null while hidden (the game draws its own).
CursorShape CurrentCursor() { return SDL_CursorVisible() ? SDL_GetCursor() : nullptr; }

void SetCursor(CursorShape shape) {
    if (!shape) {
        SDL_HideCursor();
        return;
    }
    SDL_SetCursor(static_cast<SDL_Cursor*>(shape));
    SDL_ShowCursor();
}

void MapCursorPoints(PointMap toGame, PointMap toScreen) {
    g_toGame = toGame;
    g_toScreen = toScreen;
}

bool CursorPosition(Point* at) {
    float x = 0.0f, y = 0.0f;
    SDL_GetGlobalMouseState(&x, &y);
    Point point{Whole(x), Whole(y)};
    if (g_toGame) g_toGame(&point);
    *at = point;
    return true;
}

void SetCursorPosition(Point at) {
    if (g_toScreen) g_toScreen(&at);
    SDL_WarpMouseGlobal(static_cast<float>(at.x), static_cast<float>(at.y));
}

// ==== The mouse ======================================================================================================

// The motion SDL's events carried since the last Read (the event loop adds it up), the buttons as SDL holds them.

struct MouseDevice::Device {};

MouseDevice::MouseDevice() = default;
MouseDevice::~MouseDevice() = default;

int MouseDevice::Open(NativeWindow, NativeInstance, MouseReport) { return 1; }

void MouseDevice::Close() {}

int MouseDevice::SetActive(bool, MouseReport) { return 1; }

// DIMOUSESTATE: x, y and the wheel, then a byte a button (0x80 held: left, right, middle, the first side button).
void MouseDevice::Read(std::int32_t state[4]) {
    state[0] = g_motionX.exchange(0);
    state[1] = g_motionY.exchange(0);
    state[2] = g_motionWheel.exchange(0);
    const SDL_MouseButtonFlags held = SDL_GetMouseState(nullptr, nullptr);
    std::uint32_t buttons = 0;
    if (held & SDL_BUTTON_LMASK) buttons |= 0x80u;
    if (held & SDL_BUTTON_RMASK) buttons |= 0x80u << 8;
    if (held & SDL_BUTTON_MMASK) buttons |= 0x80u << 16;
    if (held & SDL_BUTTON_X1MASK) buttons |= 0x80u << 24;
    state[3] = static_cast<std::int32_t>(buttons);
}

// ==== The clipboard ==================================================================================================

void CopyText(std::string_view text) { SDL_SetClipboardText(std::string(text).c_str()); }

std::string ClipboardText() {
    char* held = SDL_GetClipboardText();
    std::string text = held ? held : "";
    SDL_free(held);
    return text;
}

} // namespace platform
