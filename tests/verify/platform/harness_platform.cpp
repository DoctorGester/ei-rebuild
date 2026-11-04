// The harness's platform backend (harness_platform.h) on SDL, built in its builds in place of the game's
// (src/platform/sdl/platform_sdl.cpp; CMakeLists.txt). The window is SDL's, never activated and out of sight
// (sys::ConcealWindow); every system event SDL takes is dropped (the real keyboard and mouse never reach the game:
// counted for the report); the game's events are the harness's, and so are its clocks, date, mouse position and
// message box answers.
#include "harness_platform.h"
#include "platform/sdl/sdl_input.h"
#include "platform/sdl/sdl_window.h"
#include "harness_clock.h"
#include "harness_record.h"
#include "system/harness_system.h"

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

SDL_Window* g_window = nullptr;
platform::NativeWindow g_native = nullptr;
// The events waiting for the game's loops, each with whether NextEvent adds the character a key press types (the
// harness's keys; real play's come with SDL's own text).
struct Waiting {
    platform::Event event;
    bool typesCharacter = true;
};
std::deque<Waiting> g_waiting;
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
std::string g_className;   // StartWindowSystem's
platform::PointMap g_toScreen = nullptr;
// Real play (TakeRealInput): SDL's keyboard and mouse events through the game's own translation, waiting for the
// harness's next pass; the window shown, activated, of a fixed size; the cursor held for real.
bool g_live = false;
platform::sdl_input::Translator g_real;
std::deque<platform::Event> g_realWaiting;
bool g_closeRequested = false;
SDL_Rect g_heldRect{};

SDL_Window* WindowOf(platform::NativeWindow window) { return window && window == g_native ? g_window : nullptr; }

bool Minimized() { return g_window && (SDL_GetWindowFlags(g_window) & SDL_WINDOW_MINIMIZED) != 0; }

// SDL's events taken: in real play the keyboard's and the mouse's translated and kept for the harness, else dropped and
// counted.
void Pump() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (g_live) {
            if (!g_real.Take(e, 1.0f, &g_realWaiting) &&
                (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED || e.type == SDL_EVENT_QUIT))
                g_closeRequested = true;
            continue;
        }
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

bool TakeWaiting(platform::Event* event, bool* typesCharacter = nullptr) {
    if (g_waiting.empty()) return false;
    *event = g_waiting.front().event;
    if (typesCharacter) *typesCharacter = g_waiting.front().typesCharacter;
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

void Queue(const platform::Event& event, bool typesCharacter) {
    g_waiting.push_back({event, typesCharacter});
    record::Queued(event, typesCharacter);
}

void AddMotion(int dx, int dy) {
    g_motionX += dx;
    g_motionY += dy;
    record::Motion(dx, dy);
}

void TakeRealInput() { g_live = true; }

bool TakeReal(std::deque<platform::Event>* events, Point* motion) {
    *motion = g_real.TakeMotion();
    events->swap(g_realWaiting);
    g_realWaiting.clear();
    return !events->empty() || motion->x || motion->y;
}

bool CloseRequested() { return g_closeRequested; }

void InjectRealKey(const char* name) {
    const SDL_Scancode scancode = SDL_GetScancodeFromName(name);
    for (const bool down : {true, false}) {
        SDL_Event e{};
        e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        e.key.scancode = scancode;
        e.key.key = SDL_GetKeyFromScancode(scancode, SDL_KMOD_NONE, false);
        e.key.mod = SDL_KMOD_NONE;
        e.key.down = down;
        g_real.Take(e, 1.0f, &g_realWaiting);
    }
}

void InjectRealText(const std::string& text) {
    SDL_Event e{};
    e.type = SDL_EVENT_TEXT_INPUT;
    e.text.text = text.c_str();
    g_real.Take(e, 1.0f, &g_realWaiting);
}

void InjectRealMouse(RealMouse what, Point at) {
    static Point last{};
    SDL_Event e{};
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.x = static_cast<float>(at.x);
    e.motion.y = static_cast<float>(at.y);
    e.motion.xrel = static_cast<float>(at.x - last.x);
    e.motion.yrel = static_cast<float>(at.y - last.y);
    last = at;
    g_real.Take(e, 1.0f, &g_realWaiting);
    if (what == RealMouse::Move) return;
    for (const bool down : {true, false}) {
        SDL_Event b{};
        b.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        b.button.button = what == RealMouse::RightClick ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT;
        b.button.down = down;
        b.button.clicks = 1;
        b.button.x = static_cast<float>(at.x);
        b.button.y = static_cast<float>(at.y);
        g_real.Take(b, 1.0f, &g_realWaiting);
    }
}

// Ahead of the input waiting: the game takes it before anything posted earlier, as when the system told it at once.
void Activate(bool active) {
    platform::Event event;
    event.type = platform::EventType::Focus;
    event.active = active;
    g_waiting.push_front({event, true});
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

bool ClientSize(int* width, int* height) {
    return g_window && SDL_GetWindowSizeInPixels(g_window, width, height);
}

void ShowMinimized(bool minimized) {
    if (!g_window || sys::ShowWithoutActivating(g_native, minimized)) return;
    if (minimized) SDL_MinimizeWindow(g_window);
    else SDL_RestoreWindow(g_window);
}

void ResizeClient(int width, int height) {
    if (g_window) SDL_SetWindowSize(g_window, width, height);
}

std::string WindowTitle() {
    const char* title = g_window ? SDL_GetWindowTitle(g_window) : nullptr;
    return title ? title : "";
}

std::string WindowClass() {
    const std::string name = g_native ? sys::WindowClass(g_native) : std::string();
    return name.empty() ? g_className : name;
}

} // namespace ei::verify::harnessplatform

namespace platform {

namespace hp = ei::verify::harnessplatform;
namespace clock = ei::verify::clock;

SDL_Window* SdlWindow(NativeWindow window) { return hp::WindowOf(window); }

// ---- Clocks, the message box -------------------------------------------------------------------------------------

std::uint32_t Milliseconds() { return clock::Milliseconds(); }

std::uint32_t TickCount() { return clock::Ticks(); }

void Sleep(std::uint32_t ms) { clock::Sleep(ms); }

// The harness's local date (fixed in deterministic runs) as the seconds it is.
std::int64_t SecondsSince1970() {
    const ei::verify::sys::Date local = clock::LocalTime();
    std::tm at{};
    at.tm_year = local.year - 1900;
    at.tm_mon = local.month - 1;
    at.tm_mday = local.day;
    at.tm_hour = local.hour;
    at.tm_min = local.minute;
    at.tm_sec = local.second;
    at.tm_isdst = -1;
    return std::mktime(&at);
}

MessageAnswer ShowMessageBox(void*, const char* text, const char* caption, MessageButtons buttons, MessageIcon) {
    return hp::AnswerMessageBox(text, caption, buttons);
}

// ---- The window --------------------------------------------------------------------------------------------------

void StartWindowSystem(const char* name) {
    hp::g_className = name;
#if defined(SDL_PLATFORM_WINDOWS)
    SDL_RegisterApp(name, 0, nullptr);   // the window class's name, as the game's
#endif
    SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, hp::g_live ? "1" : "0");
    SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_RAISED, hp::g_live ? "1" : "0");
    SDL_SetHint(SDL_HINT_MOUSE_AUTO_CAPTURE, "0");
    // Real play's held cursor, as the game's backend holds it (platform_sdl.cpp).
    SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_CURSOR_VISIBLE, "1");
    SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_MODE_CENTER, "0");
    SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_SYSTEM_SCALE, "1");
    SDL_InitSubSystem(SDL_INIT_VIDEO);
}

// As the game's: the client area the resolution's, centred in the primary display's usable bounds (made smaller when
// it would not fit), or a borderless window at the screen's top left; its title marked. Concealed before it is first
// shown (sys::ConcealWindow: never activated, no taskbar button, cloaked).
NativeWindow CreateGameWindow(const char* title, int width, int height, bool fullScreen) {
    const std::string marked = std::string(title) + " (harness)";
    const SDL_PropertiesID properties = SDL_CreateProperties();
    SDL_SetStringProperty(properties, SDL_PROP_WINDOW_CREATE_TITLE_STRING, marked.c_str());
    SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_X_NUMBER, 0);
    SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_Y_NUMBER, 0);
    SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, width);
    SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, height);
    SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_BORDERLESS_BOOLEAN, fullScreen);
    SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, !fullScreen && !hp::g_live);
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
#if defined(SDL_PLATFORM_WINDOWS)
    hp::g_native = SDL_GetPointerProperty(SDL_GetWindowProperties(hp::g_window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#else
    hp::g_native = hp::g_window;   // as platform_sdl.cpp's: graphics_gl.cpp takes the SDL window
#endif
    if (hp::g_live) SDL_StartTextInput(hp::g_window);
    else ei::verify::sys::ConcealWindow(hp::g_native);
    return hp::g_native;
}

void ShowGameWindow(NativeWindow window) {
    SDL_Window* w = hp::WindowOf(window);
    if (!w) return;
    SDL_ShowWindow(w);
    if (hp::g_live) SDL_RaiseWindow(w);
}

bool IsMinimized(NativeWindow window) { return hp::WindowOf(window) && hp::Minimized(); }

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
        bool typesCharacter = true;
        if (hp::TakeWaiting(event, &typesCharacter)) {
            if (event->type == EventType::KeyDown && typesCharacter)
                if (const int character = hp::TypedCharacter(event->key)) {
                    Event typed;
                    typed.type = EventType::Text;
                    typed.scan = event->scan;
                    typed.text = std::string(1, static_cast<char>(character));
                    hp::g_waiting.push_back({typed, true});
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

void PostQuit(int code) {
    hp::g_quitPosted = true;
    hp::g_quitCode = code;
    hp::GameQuits();
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

// The harness's mouse moves only by its steps: a hold is only recorded, a move ends it. In real play the pointer is
// held as the game's backend holds it (platform_sdl.cpp): relative mode, a one-pixel mouse rectangle at the point.
void HoldCursor(const Point* at) {
    const bool was = hp::g_holding;
    hp::g_holding = at != nullptr;
    if (at) hp::g_held = *at;
    if (!hp::g_live || !hp::g_window) return;
    if (!at) {
        if (!was) return;
        SDL_SetWindowMouseRect(hp::g_window, nullptr);
        SDL_SetWindowRelativeMouseMode(hp::g_window, false);
        return;
    }
    Point screen = *at;
    if (hp::g_toScreen) hp::g_toScreen(&screen);
    int left = 0, top = 0;
    SDL_GetWindowPosition(hp::g_window, &left, &top);
    const SDL_Rect place{screen.x - left, screen.y - top, 1, 1};
    if (was && place.x == hp::g_heldRect.x && place.y == hp::g_heldRect.y) return;
    SDL_SetWindowMouseRect(hp::g_window, &place);
    SDL_SetWindowRelativeMouseMode(hp::g_window, true);
    hp::g_heldRect = place;
}

void MapCursorPoints(PointMap toGame, PointMap toScreen) {
    hp::g_toGame = toGame;
    hp::g_toScreen = toScreen;
}

bool CursorPosition(Point* at) {
    Point point{};
    if (!hp::MousePosition(&point)) return false;
    if (hp::g_toGame) hp::g_toGame(&point);
    *at = point;
    return true;
}

void SetCursorPosition(Point at) {
    HoldCursor(nullptr);
    if (!hp::g_live) return;
    if (hp::g_toScreen) hp::g_toScreen(&at);
    SDL_WarpMouseGlobal(static_cast<float>(at.x), static_cast<float>(at.y));
}

// The harness's motion only.
Point TakeMouseMotion() { return Point{hp::g_motionX.exchange(0), hp::g_motionY.exchange(0)}; }

void CopyText(std::string_view text) { hp::g_clipboard = std::string(text); }

std::string ClipboardText() { return hp::g_clipboard; }

} // namespace platform
