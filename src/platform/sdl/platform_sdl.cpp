// The platform layer on SDL3 (platform/platform.h; docs/notes/platform.md, "SDL3"): the clocks, the message box, the
// threads, the window and its events, the cursor, the mouse and the clipboard, in platform.h's order. Built into the
// builds without the harness (release, debug, play, x64); the harness's builds take its own
// (tests/verify/harness_platform.cpp). The game keeps its own loops (each takes SDL's events through EventWaits / NextEvent / TakeEvent); the renderer keeps its own
// swap chain on the window's native handle.
#include "platform/platform.h"
#include "platform/sdl/sdl_keys.h"

#define SDL_MAIN_HANDLED   // SDL_main.h only for SDL_RegisterApp: the program keeps its own entry
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
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

// sdl_threads.cpp.

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

// ---- Keys (sdl_keys.cpp) ------------------------------------------------------------------------------------

void QueueCharacter(int unit) {
    Event event;
    event.type = EventType::Character;
    event.key = unit;
    event.repeat = 1;
    event.keyFlags = g_typedFlags;
    Queue(event);
}

void TakeKey(const SDL_KeyboardEvent& k) {
    const sdl_keys::KeyCode code = sdl_keys::KeyOf(k.scancode, k.key, k.mod, LayoutKey);
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
    if (const int character = sdl_keys::ControlCharacter(code.key, code.scan, k.mod, LayoutControlCharacter))
        QueueCharacter(character);
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
#if defined(_WIN32)
    SDL_RegisterApp(className, 0, instance);
#endif
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
        DebugOutput((std::string("platform: no SDL video: ") + SDL_GetError() + "\n").c_str());
}

NativeWindow CreateGameWindow(NativeInstance, const char* title, int width, int height, bool fullScreen) {
    const SDL_PropertiesID properties = SDL_CreateProperties();
    SDL_SetStringProperty(properties, SDL_PROP_WINDOW_CREATE_TITLE_STRING, title);
    SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_X_NUMBER, 0);
    SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_Y_NUMBER, 0);
    SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, width);
    SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, height);
    SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_BORDERLESS_BOOLEAN, fullScreen);
    SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, !fullScreen);
    SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_HIDDEN_BOOLEAN, true);
#if !defined(_WIN32)
    SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_OPENGL_BOOLEAN, true);   // graphics_gl.cpp's context
#endif
    g_window = SDL_CreateWindowWithProperties(properties);
    SDL_DestroyProperties(properties);
    if (!g_window) return nullptr;
    SDL_Rect work{};
    if (!fullScreen && SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &work)) {
        int top = 0, left = 0, bottom = 0, right = 0;   // the borders and the title bar (none where SDL cannot tell)
        SDL_GetWindowBordersSize(g_window, &top, &left, &bottom, &right);
        width = std::min(width, work.w - left - right);
        height = std::min(height, work.h - top - bottom);
        SDL_SetWindowSize(g_window, width, height);
        SDL_SetWindowPosition(g_window, work.x + left + (work.w - left - right - width) / 2,
                              work.y + top + (work.h - top - bottom - height) / 2);
    }
#if defined(_WIN32)
    g_native = SDL_GetPointerProperty(SDL_GetWindowProperties(g_window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#else
    g_native = g_window;
#endif
    SDL_StartTextInput(g_window);
    return g_native;
}

void ShowGameWindow(NativeWindow window, int) {
    if (SDL_Window* w = WindowOf(window)) SDL_ShowWindow(w);
}

bool IsMinimized(NativeWindow window) { return WindowOf(window) && Minimized(); }

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

bool WindowPixelSize(NativeWindow window, int* width, int* height) {
    SDL_Window* w = WindowOf(window);
    if (!w || Minimized() || !SDL_GetWindowSizeInPixels(w, width, height)) return false;
    return *width > 0 && *height > 0;
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
