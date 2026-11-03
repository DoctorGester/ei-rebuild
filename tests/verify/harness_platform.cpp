// The harness's platform backend (harness_platform.h) on SDL, built in its builds in place of the game's
// (src/platform/sdl/platform_sdl.cpp; CMakeLists.txt). The window is SDL's, never activated; every system event SDL
// takes is dropped (the real keyboard and mouse never reach the game: counted for the report); the game's events are
// the harness's, and so are its clocks, date, mouse position and message box answers.
#include "harness_platform.h"
#include "harness_clock.h"

#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <deque>
#include <string>

namespace ei::verify::harnessplatform {
namespace {

platform::EventHandler g_handler = nullptr;
SDL_Window* g_window = nullptr;
platform::NativeWindow g_native = nullptr;
std::deque<platform::Event> g_waiting;
bool g_quitPosted = false;
int g_quitCode = 0;
std::atomic<std::int32_t> g_motionX{0}, g_motionY{0};
unsigned g_realInput = 0;
// A cursor made by MakeCursor: SDL's, and its pixels for the harness's checks.
struct Cursor {
    SDL_Cursor* sdl = nullptr;
    ShownCursor picture;
};
Cursor* g_cursor = nullptr;   // shown
std::vector<ShownCursor> g_seen;
bool g_holding = false;
Point g_held{};
platform::PointMap g_toGame = nullptr;
std::string g_clipboard;

SDL_Window* WindowOf(platform::NativeWindow window) { return window && window == g_native ? g_window : nullptr; }

bool Minimized() { return g_window && (SDL_GetWindowFlags(g_window) & SDL_WINDOW_MINIMIZED) != 0; }

// SDL's events taken and dropped; the real keyboard's and mouse's counted.
void Pump() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
        case SDL_EVENT_TEXT_INPUT:
        case SDL_EVENT_MOUSE_MOTION:
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        case SDL_EVENT_MOUSE_WHEEL: ++g_realInput; break;
        default: break;
        }
    }
}

bool TakeWaiting(platform::Event* event) {
    if (g_waiting.empty()) return false;
    *event = g_waiting.front();
    g_waiting.pop_front();
    return true;
}

platform::Event QuitEvent() {
    platform::Event quit;
    quit.type = platform::EventType::Quit;
    quit.quitCode = g_quitCode;
    g_quitPosted = false;
    return quit;
}

} // namespace

void Queue(const platform::Event& event) {
    platform::Event queued = event;
    queued.window = g_native;
    g_waiting.push_back(queued);
}

void AddMotion(int dx, int dy) {
    g_motionX += dx;
    g_motionY += dy;
}

void Activate(bool active) {
    platform::Event event;
    event.type = platform::EventType::Activate;
    event.activation = active ? platform::Activation::Active : platform::Activation::Inactive;
    event.window = g_native;
    if (g_handler) g_handler(event);
}

unsigned RealInputLeftOut() { return g_realInput; }

bool CursorShown(ShownCursor* cursor) {
    if (!g_cursor) return false;
    *cursor = g_cursor->picture;
    return true;
}

const std::vector<ShownCursor>& SeenCursors() { return g_seen; }
void ForgetSeenCursors() { g_seen.clear(); }

bool CursorHeld(Point* at) {
    if (!g_holding) return false;
    *at = g_held;
    return true;
}

} // namespace ei::verify::harnessplatform

namespace platform {

namespace hp = ei::verify::harnessplatform;
namespace clock = ei::verify::clock;

// ---- Clocks, the message box -------------------------------------------------------------------------------------

std::uint32_t Milliseconds() { return clock::Milliseconds(); }

std::uint32_t TickCount() { return clock::Ticks(); }

void Sleep(std::uint32_t ms) { clock::Sleep(ms); }

// The harness's local date (fixed in deterministic runs) as the seconds it is.
std::int64_t SecondsSince1970() {
    const SYSTEMTIME local = clock::LocalTime();
    std::tm at{};
    at.tm_year = local.wYear - 1900;
    at.tm_mon = local.wMonth - 1;
    at.tm_mday = local.wDay;
    at.tm_hour = local.wHour;
    at.tm_min = local.wMinute;
    at.tm_sec = local.wSecond;
    at.tm_isdst = -1;
    return std::mktime(&at);
}

MessageAnswer ShowMessageBox(void*, const char* text, const char* caption, MessageButtons buttons, MessageIcon) {
    return hp::AnswerMessageBox(text, caption, buttons);
}

// ---- The window --------------------------------------------------------------------------------------------------

void RegisterWindowClass(NativeInstance instance, const char* className, EventHandler handler) {
    hp::g_handler = handler;
#if defined(SDL_PLATFORM_WINDOWS)
    SDL_RegisterApp(className, 0, instance);   // the window class's name, as the game's
#endif
    SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, "0");
    SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_RAISED, "0");
    SDL_SetHint(SDL_HINT_MOUSE_AUTO_CAPTURE, "0");
    SDL_InitSubSystem(SDL_INIT_VIDEO);
}

// As the game's: the client area the resolution's, centred in the primary display's usable bounds (made smaller when
// it would not fit), or a borderless window at the screen's top left; its title marked.
NativeWindow CreateGameWindow(NativeInstance, const char* title, int width, int height, bool fullScreen) {
    const std::string marked = std::string(title) + " (harness)";
    const SDL_PropertiesID properties = SDL_CreateProperties();
    SDL_SetStringProperty(properties, SDL_PROP_WINDOW_CREATE_TITLE_STRING, marked.c_str());
    SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_X_NUMBER, 0);
    SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_Y_NUMBER, 0);
    SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, width);
    SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, height);
    SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_BORDERLESS_BOOLEAN, fullScreen);
    SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, !fullScreen);
    SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_HIDDEN_BOOLEAN, true);
    hp::g_window = SDL_CreateWindowWithProperties(properties);
    SDL_DestroyProperties(properties);
    if (!hp::g_window) return nullptr;
    SDL_Rect work{};
    if (!fullScreen && SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &work)) {
        int top = 0, left = 0, bottom = 0, right = 0;
        SDL_GetWindowBordersSize(hp::g_window, &top, &left, &bottom, &right);
        width = (std::min)(width, work.w - left - right);
        height = (std::min)(height, work.h - top - bottom);
        SDL_SetWindowSize(hp::g_window, width, height);
        SDL_SetWindowPosition(hp::g_window, work.x + left + (work.w - left - right - width) / 2,
                              work.y + top + (work.h - top - bottom - height) / 2);
    }
    hp::g_native = SDL_GetPointerProperty(SDL_GetWindowProperties(hp::g_window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    return hp::g_native;
}

void ShowGameWindow(NativeWindow window, int) {
    if (SDL_Window* w = hp::WindowOf(window)) SDL_ShowWindow(w);
}

bool IsMinimized(NativeWindow window) { return hp::WindowOf(window) && hp::Minimized(); }

void MarkPainted(NativeWindow) {}

void RepaintDesktop() {}

void ScreenToClient(NativeWindow window, Point* point) {
    int left = 0, top = 0;
    if (SDL_Window* w = hp::WindowOf(window)) SDL_GetWindowPosition(w, &left, &top);
    point->x -= left;
    point->y -= top;
}

void ClientToScreen(NativeWindow window, Point* point) {
    int left = 0, top = 0;
    if (SDL_Window* w = hp::WindowOf(window)) SDL_GetWindowPosition(w, &left, &top);
    point->x += left;
    point->y += top;
}

bool CoverMonitor(NativeWindow window, Rect* was, Rect* monitor) {
    SDL_Window* w = hp::WindowOf(window);
    if (!w || hp::Minimized()) return false;
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
    SDL_Window* w = hp::WindowOf(window);
    if (!w || hp::Minimized() || !SDL_GetWindowSizeInPixels(w, width, height)) return false;
    return *width > 0 && *height > 0;
}

// ---- Events ------------------------------------------------------------------------------------------------------

bool EventWaits() {
    if (!hp::BeforeEvents()) return false;
    hp::Pump();
    return !hp::g_waiting.empty() || hp::g_quitPosted;
}

bool NextEvent(Event* event) {
    for (;;) {
        hp::Pump();
        if (hp::TakeWaiting(event)) {
            if (event->type == EventType::KeyDown)
                if (const int character = hp::TypedCharacter(event->key)) {
                    Event typed = *event;
                    typed.type = EventType::Character;
                    typed.key = character;
                    hp::g_waiting.push_back(typed);
                }
            return true;
        }
        if (hp::g_quitPosted) {
            *event = hp::QuitEvent();
            return false;
        }
        SDL_WaitEventTimeout(nullptr, 1);
    }
}

bool TakeEvent(Event* event) {
    if (!hp::BeforeEvents()) return false;
    hp::Pump();
    if (hp::TakeWaiting(event)) return true;
    if (!hp::g_quitPosted) return false;
    *event = hp::QuitEvent();
    return true;
}

void DispatchEvent(const Event& event) {
    if (hp::g_handler) hp::g_handler(event);
}

void PostQuit(int code) {
    hp::g_quitPosted = true;
    hp::g_quitCode = code;
}

// ---- The cursor, the mouse, the clipboard ------------------------------------------------------------------------

// SDL's cursors on the window as the game's backend shows them (only seen while the real mouse is over it), their pixels
// kept for the checks.
CursorShape CurrentCursor() { return hp::g_cursor; }

CursorShape MakeCursor(const std::uint8_t* rgba, int width, int height, int hotX, int hotY) {
    SDL_Surface* surface =
        SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_RGBA32, const_cast<std::uint8_t*>(rgba), width * 4);
    if (!surface) return nullptr;
    SDL_Cursor* sdl = SDL_CreateColorCursor(surface, hotX, hotY);
    SDL_DestroySurface(surface);
    if (!sdl) return nullptr;
    auto* cursor = new hp::Cursor{sdl, {width, height, hotX, hotY, {}}};
    cursor->picture.rgba.assign(rgba, rgba + static_cast<std::size_t>(width) * height * 4);
    return cursor;
}

void FreeCursor(CursorShape shape) {
    auto* cursor = static_cast<hp::Cursor*>(shape);
    if (!cursor) return;
    SDL_DestroyCursor(cursor->sdl);
    delete cursor;
}

void SetCursor(CursorShape shape) {
    hp::g_cursor = static_cast<hp::Cursor*>(shape);
    if (!hp::g_cursor) {
        SDL_HideCursor();
        return;
    }
    if (hp::CollectCursors()) {
        const hp::ShownCursor& picture = hp::g_cursor->picture;
        bool known = false;
        for (const hp::ShownCursor& seen : hp::SeenCursors())
            known = known || (seen.width == picture.width && seen.height == picture.height && seen.hotX == picture.hotX &&
                              seen.hotY == picture.hotY && seen.rgba == picture.rgba);
        if (!known) hp::g_seen.push_back(picture);
    }
    SDL_SetCursor(hp::g_cursor->sdl);
    SDL_ShowCursor();
}

// The harness's mouse moves only by its steps: a hold is only recorded, a move ends it.
void HoldCursor(const Point* at) {
    hp::g_holding = at != nullptr;
    if (at) hp::g_held = *at;
}

void MapCursorPoints(PointMap toGame, PointMap) { hp::g_toGame = toGame; }

bool CursorPosition(Point* at) {
    Point point{};
    if (!hp::MousePosition(&point)) return false;
    if (hp::g_toGame) hp::g_toGame(&point);
    *at = point;
    return true;
}

void SetCursorPosition(Point) { HoldCursor(nullptr); }

struct MouseDevice::Device {};

MouseDevice::MouseDevice() = default;
MouseDevice::~MouseDevice() = default;

int MouseDevice::Open(NativeWindow, NativeInstance, MouseReport) { return 1; }

void MouseDevice::Close() {}

int MouseDevice::SetActive(bool, MouseReport) { return 1; }

// The harness's motion only (its buttons and wheel come as events).
void MouseDevice::Read(std::int32_t state[4]) {
    state[0] = hp::g_motionX.exchange(0);
    state[1] = hp::g_motionY.exchange(0);
    state[2] = 0;
    state[3] = 0;
}

void CopyText(std::string_view text) { hp::g_clipboard = std::string(text); }

std::string ClipboardText() { return hp::g_clipboard; }

} // namespace platform
