// The platform layer on SDL3 (platform/platform.h; docs/notes/platform.md, "SDL3"): the clocks, the message box, the
// threads, the window and its events, the cursor, the mouse and the clipboard, in platform.h's order. Built into the
// builds without the harness (release, debug, play, x64); the harness's builds take its own
// (tests/verify/platform/harness_platform.cpp). The game keeps its own loops (each takes SDL's events through
// EventWaits / NextEvent / TakeEvent); the renderer takes the window's SDL_Window (sdl_window.h) for its swap chain.
#include "platform/platform.h"
#include "platform/display_sizes.h"
#include "platform/log.h"
#include "platform/sdl/sdl_input.h"
#include "platform/sdl/sdl_window.h"

#define SDL_MAIN_HANDLED   // SDL_main.h only for SDL_RegisterApp: the program keeps its own entry
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>

namespace platform {
namespace {

// The game window (one) and its native handle, which the game holds as its NativeWindow.
SDL_Window* g_window = nullptr;
NativeWindow g_native = nullptr;

SDL_Window* WindowOf(NativeWindow window) { return window && window == g_native ? g_window : nullptr; }

} // namespace

SDL_Window* SdlWindow(NativeWindow window) { return WindowOf(window); }

// ==== Clocks =========================================================================================================

// Both clocks are SDL's millisecond count, from SDL's first read plus a day: the values the original read on a machine up
// for a day, not the near-zero ones of a fresh count (the game keeps 0 as "never" in times it compares with them).

namespace {

constexpr std::uint64_t kClockStart = 24ull * 60 * 60 * 1000;

std::uint32_t Clock() { return static_cast<std::uint32_t>(SDL_GetTicks() + kClockStart); }

} // namespace

std::uint32_t FrameClock() { return Clock(); }

std::uint32_t CoarseClock() { return Clock(); }

void Sleep(std::uint32_t ms) {
    if (ms == 0)
        std::this_thread::yield();
    else
        SDL_Delay(ms);
}

std::int64_t SecondsSince1970() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

// ==== The message box ================================================================================================

MessageAnswer ShowMessageBox(NativeWindow owner, const char* text, const char* caption, MessageButtons buttons,
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

// SDL's window and events as the game's Events (docs/notes/platform.md, "SDL3"): keys as platform::Key and
// the set-1 scan codes (sdl_keys.cpp), SDL's typed text and the control characters a key types, the mouse in pixels,
// the focus only on a change. Every event waits in the queue for the game's loops.

namespace {

// The events taken from SDL and not yet from us.
std::deque<Event> g_waiting;
// PostQuit's request: NextEvent gives it once nothing else waits.
bool g_quitPosted = false;
int g_quitCode = 0;
// The focus the game was told last (a Focus event only on a change).
bool g_told = false;
bool g_toldActive = false;
bool g_toldMinimized = false;
// SDL's keyboard and mouse events as ours (sdl_input.h), and the mouse's motion since TakeMouseMotion took it.
sdl_input::Translator g_input;

void Queue(Event event) { g_waiting.push_back(std::move(event)); }

bool Minimized() { return g_window && (SDL_GetWindowFlags(g_window) & SDL_WINDOW_MINIMIZED) != 0; }

void Focus(bool active, bool minimized) {
    if (g_told && active == g_toldActive && minimized == g_toldMinimized) return;
    g_told = true;
    g_toldActive = active;
    g_toldMinimized = minimized;
    Event event;
    event.type = EventType::Focus;
    event.active = active;
    event.minimized = minimized;
    Queue(event);
}

// ---- The mouse ---------------------------------------------------------------------------------------------------

// Pixels a window point holds: SDL's mouse and window coordinates are points, the game's (its input, its frame, the
// renderer's drawable) pixels. On macOS a point is 2 pixels on Retina; elsewhere 1.
float PixelsPerPoint() {
#if defined(__APPLE__)
    if (const float density = g_window ? SDL_GetWindowPixelDensity(g_window) : 0.0f; density > 0.0f) return density;
#endif
    return 1.0f;
}

int Whole(float coordinate) { return static_cast<int>(std::floor(coordinate)); }

// A coordinate in points as whole pixels.
int Pixels(float points) { return Whole(points * PixelsPerPoint()); }

// ---- SDL's events ------------------------------------------------------------------------------------------------

void Queue(EventType type) {
    Event event;
    event.type = type;
    Queue(event);
}

void Take(const SDL_Event& e) {
    if (sdl_input::IsCloseKey(e)) {
        Queue(EventType::CloseRequested);
        return;
    }
    if (g_input.Take(e, PixelsPerPoint(), &g_waiting)) return;
    switch (e.type) {
    case SDL_EVENT_WINDOW_FOCUS_GAINED: Focus(true, Minimized()); break;
    case SDL_EVENT_WINDOW_FOCUS_LOST: Focus(false, Minimized()); break;
    case SDL_EVENT_WINDOW_MINIMIZED: Focus(false, true); break;
    case SDL_EVENT_WINDOW_RESTORED:
        if (g_window && (SDL_GetWindowFlags(g_window) & SDL_WINDOW_INPUT_FOCUS)) Focus(true, false);
        break;
    // The player closes the window (or the system ends the session): the game decides.
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
    case SDL_EVENT_QUIT: Queue(EventType::CloseRequested); break;
    default: break;
    }
}

// SDL's events taken until one of ours waits (or none is left).
void Fill() {
    SDL_Event e;
    while (g_waiting.empty() && SDL_PollEvent(&e)) Take(e);
}

// The next event taken from us: the first waiting, else PostQuit's request (once), else none.
std::optional<Event> PopEvent() {
    if (!g_waiting.empty()) {
        Event event = std::move(g_waiting.front());
        g_waiting.pop_front();
        return event;
    }
    if (!g_quitPosted) return std::nullopt;
    Event quit;
    quit.type = EventType::Quit;
    quit.quitCode = g_quitCode;
    g_quitPosted = false;
    return quit;
}

} // namespace

// ---- The window --------------------------------------------------------------------------------------------------

namespace {

// A decorated window's client area `width` x `height` (in SDL's window units), made smaller when it and the borders
// would not fit in `display`'s usable bounds, centred there. False (nothing done) when SDL cannot tell the bounds.
bool PlaceInUsableBounds(SDL_Window* window, SDL_DisplayID display, int width, int height) {
    SDL_Rect work{};
    if (!SDL_GetDisplayUsableBounds(display, &work)) return false;
    int top = 0, left = 0, bottom = 0, right = 0;   // the borders and the title bar (none where SDL cannot tell)
    SDL_GetWindowBordersSize(window, &top, &left, &bottom, &right);
    width = std::min(width, work.w - left - right);
    height = std::min(height, work.h - top - bottom);
    SDL_SetWindowSize(window, width, height);
    SDL_SetWindowPosition(window, work.x + left + (work.w - left - right - width) / 2,
                          work.y + top + (work.h - top - bottom - height) / 2);
    return true;
}

} // namespace

void StartWindowSystem(const char* name) {
    SDL_SetHint(SDL_HINT_WINDOWS_CLOSE_ON_ALT_F4, "0");      // Alt+F4 is ours (IsCloseKey), the same on every system
    SDL_SetHint(SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE, "0");    // closing the window is a request (CloseRequested)
    SDL_SetHint(SDL_HINT_MOUSE_AUTO_CAPTURE, "0");           // mouse events only over the window, as Windows gave them
    // The held cursor (HoldCursor): relative mode keeps it visible where the mouse rectangle holds it, its motion scaled
    // as the pointer's.
    SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_CURSOR_VISIBLE, "1");
    SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_MODE_CENTER, "0");
    SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_SYSTEM_SCALE, "1");
    // The window class's name (BringOtherInstanceForward finds a running game by it).
#if defined(_WIN32)
    SDL_RegisterApp(name, 0, nullptr);
#else
    (void)name;
#endif
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
        ei::Log("platform: no SDL video: %s", SDL_GetError());
}

NativeWindow CreateGameWindow(const char* title, int width, int height, bool fullScreen) {
#if defined(__APPLE__)
    // The window in points, so that its drawable holds `width` x `height` pixels (on Retina half as many points).
    if (const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetPrimaryDisplay());
        mode && mode->pixel_density > 0.0f) {
        width = static_cast<int>(std::lround(static_cast<float>(width) / mode->pixel_density));
        height = static_cast<int>(std::lround(static_cast<float>(height) / mode->pixel_density));
    }
#endif
    const SDL_PropertiesID properties = SDL_CreateProperties();
    SDL_SetStringProperty(properties, SDL_PROP_WINDOW_CREATE_TITLE_STRING, title);
    SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_X_NUMBER, 0);
    SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_Y_NUMBER, 0);
    SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, width);
    SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, height);
#if defined(__APPLE__)
    // Full screen is SDL's desktop full screen below (a Space of its own): a titled window, whose title bar the
    // Space hides and which comes back when the player leaves the Space.
    SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_BORDERLESS_BOOLEAN, false);
#else
    SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_BORDERLESS_BOOLEAN, fullScreen);
#endif
    SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, !fullScreen);
    SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_HIDDEN_BOOLEAN, true);
#if defined(__APPLE__)
    // The drawable in pixels (SDL_GPU's Metal layer at the backing scale), the window and SDL's coordinates in points.
    SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_HIGH_PIXEL_DENSITY_BOOLEAN, true);
#endif
    g_window = SDL_CreateWindowWithProperties(properties);
    SDL_DestroyProperties(properties);
    if (!g_window) return nullptr;
#if defined(__APPLE__)
    // A borderless window over the display's bounds stays under the menu bar and the Dock: full screen on macOS is
    // SDL's desktop full screen, a Space of its own (as the window's green button makes), entered when the window is
    // shown. macOS hides the menu bar and the Dock there; on a display with a notch the window is the part below it.
    if (fullScreen && !SDL_SetWindowFullscreen(g_window, true))
        ei::Log("platform: the window cannot go full screen: %s", SDL_GetError());
#endif
    if (!fullScreen) PlaceInUsableBounds(g_window, SDL_GetPrimaryDisplay(), width, height);
#if defined(_WIN32)
    g_native = SDL_GetPointerProperty(SDL_GetWindowProperties(g_window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#else
    g_native = g_window;
#endif
    SDL_StartTextInput(g_window);
    return g_native;
}

void ShowGameWindow(NativeWindow window) {
    if (SDL_Window* w = WindowOf(window)) SDL_ShowWindow(w);
}

Point ScreenToClient(NativeWindow window, Point point) {
    int left = 0, top = 0;
    if (SDL_Window* w = WindowOf(window)) SDL_GetWindowPosition(w, &left, &top);
    return Point{point.x - Pixels(static_cast<float>(left)), point.y - Pixels(static_cast<float>(top))};
}

Point ClientToScreen(NativeWindow window, Point point) {
    int left = 0, top = 0;
    if (SDL_Window* w = WindowOf(window)) SDL_GetWindowPosition(w, &left, &top);
    return Point{point.x + Pixels(static_cast<float>(left)), point.y + Pixels(static_cast<float>(top))};
}

std::optional<MonitorCover> CoverMonitor(NativeWindow window) {
#if defined(__APPLE__)
    // SDL's full screen covers the display (CreateGameWindow); a window the player took out of its Space stays so.
    (void)window;
    return std::nullopt;
#else
    SDL_Window* w = WindowOf(window);
    if (!w || Minimized()) return std::nullopt;
    SDL_Rect bounds{};
    if (!SDL_GetDisplayBounds(SDL_GetDisplayForWindow(w), &bounds)) return std::nullopt;
    int left = 0, top = 0, width = 0, height = 0;
    SDL_GetWindowPosition(w, &left, &top);
    SDL_GetWindowSize(w, &width, &height);
    if (left == bounds.x && top == bounds.y && width == bounds.w && height == bounds.h) return std::nullopt;
    SDL_SetWindowPosition(w, bounds.x, bounds.y);
    SDL_SetWindowSize(w, bounds.w, bounds.h);
    return MonitorCover{Rect{left, top, left + width, top + height},
                        Rect{bounds.x, bounds.y, bounds.x + bounds.w, bounds.y + bounds.h}};
#endif
}

void SetFullScreen(NativeWindow window, bool fullScreen) {
    SDL_Window* w = WindowOf(window);
    if (!w) return;
#if defined(__APPLE__)
    // Full screen is SDL's desktop full screen (CreateGameWindow): a Space of its own, the display mode kept.
    if (!SDL_SetWindowFullscreen(w, fullScreen)) ei::Log("platform: the window cannot change full screen: %s", SDL_GetError());
    SDL_SyncWindow(w);
#else
    if (SDL_GetWindowFlags(w) & SDL_WINDOW_MAXIMIZED) SDL_RestoreWindow(w);
    SDL_SetWindowBordered(w, !fullScreen);
    SDL_SetWindowResizable(w, !fullScreen);
#endif
}

std::optional<Size> WindowPixelSize(NativeWindow window) {
    SDL_Window* w = WindowOf(window);
    int width = 0, height = 0;
    if (!w || Minimized() || !SDL_GetWindowSizeInPixels(w, &width, &height) || width <= 0 || height <= 0)
        return std::nullopt;
    return Size{width, height};
}

void SetClientSize(NativeWindow window, int width, int height) {
    SDL_Window* w = WindowOf(window);
    if (!w) return;
    if (SDL_GetWindowFlags(w) & SDL_WINDOW_MAXIMIZED) SDL_RestoreWindow(w);
    // SDL sizes the window in points (on Retina two pixels a point).
    const float density = PixelsPerPoint();
    width = static_cast<int>(std::lround(static_cast<float>(width) / density));
    height = static_cast<int>(std::lround(static_cast<float>(height) / density));
    if (!PlaceInUsableBounds(w, SDL_GetDisplayForWindow(w), width, height)) SDL_SetWindowSize(w, width, height);
    SDL_SyncWindow(w);
}

std::vector<Size> DisplaySizes(NativeWindow window) {
    SDL_Window* w = WindowOf(window);
    const SDL_DisplayID display = w ? SDL_GetDisplayForWindow(w) : SDL_GetPrimaryDisplay();
    const SDL_DisplayMode* desktop = SDL_GetDesktopDisplayMode(display);
    if (!desktop) return {};
    // Modes are in points (two pixels a point on Retina).
    auto pixels = [](const SDL_DisplayMode& mode) {
        return Size{static_cast<std::int32_t>(std::lround(mode.w * mode.pixel_density)),
                    static_cast<std::int32_t>(std::lround(mode.h * mode.pixel_density))};
    };
    std::vector<Size> modes;
    int count = 0;
    if (SDL_DisplayMode** list = SDL_GetFullscreenDisplayModes(display, &count)) {
        for (int i = 0; i < count; ++i) modes.push_back(pixels(*list[i]));
        SDL_free(list);
    }
    return ChooseSizes(std::move(modes), pixels(*desktop));
}


void ConfineCursor(NativeWindow window, bool confined) {
    if (SDL_Window* w = WindowOf(window)) SDL_SetWindowMouseGrab(w, confined);
}

// ---- Events ------------------------------------------------------------------------------------------------------

bool EventWaits() {
    Fill();
    return !g_waiting.empty() || g_quitPosted;
}

Event NextEvent() {
    for (;;) {
        Fill();
        if (std::optional<Event> event = PopEvent()) return *std::move(event);
        SDL_WaitEvent(nullptr);
    }
}

std::optional<Event> TakeEvent() {
    Fill();
    return PopEvent();
}

void PostQuit(int code) {
    g_quitPosted = true;
    g_quitCode = code;
}

// ==== The cursor =====================================================================================================

namespace {

PointMap g_toGame = nullptr;
PointMap g_toScreen = nullptr;
bool g_holding = false;   // HoldCursor's
SDL_Rect g_held{};

} // namespace

// The shape shown over the window; null while hidden.
CursorShape CurrentCursor() { return SDL_CursorVisible() ? SDL_GetCursor() : nullptr; }

CursorShape MakeCursor(const std::uint8_t* rgba, int width, int height, int hotX, int hotY) {
    SDL_Surface* surface =
        SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_RGBA32, const_cast<std::uint8_t*>(rgba), width * 4);
    if (!surface) return nullptr;
    // SDL's cursor dimensions and hotspot are in points. Keep the original pixels as its high-density image.
    const float density = PixelsPerPoint();
    const int pointsWide = std::max(1, static_cast<int>(std::lround(width / density)));
    const int pointsHigh = std::max(1, static_cast<int>(std::lround(height / density)));
    if (pointsWide != width || pointsHigh != height) {
        SDL_Surface* pixels = surface;
        surface = SDL_ScaleSurface(pixels, pointsWide, pointsHigh, SDL_SCALEMODE_NEAREST);
        if (surface) SDL_AddSurfaceAlternateImage(surface, pixels);
        SDL_DestroySurface(pixels);
        if (!surface) return nullptr;
        hotX = std::clamp(static_cast<int>(std::lround(hotX / density)), 0, pointsWide - 1);
        hotY = std::clamp(static_cast<int>(std::lround(hotY / density)), 0, pointsHigh - 1);
    }
    SDL_Cursor* cursor = SDL_CreateColorCursor(surface, hotX, hotY);
    SDL_DestroySurface(surface);
    return cursor;
}

void FreeCursor(CursorShape shape) { SDL_DestroyCursor(static_cast<SDL_Cursor*>(shape)); }

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

std::optional<Point> CursorPosition() {
    float x = 0.0f, y = 0.0f;
    SDL_GetGlobalMouseState(&x, &y);
    const Point point{Pixels(x), Pixels(y)};
    return g_toGame ? g_toGame(point) : point;
}

// Held: SDL's relative mode (the motion from the device, not the pointer) with the pointer kept in a one-pixel mouse
// rectangle at the point. Let go, SDL puts the pointer where its motion would have taken it.
void HoldCursor(Point at) {
    if (!g_window) return;
    const Point screen = g_toScreen ? g_toScreen(at) : at;
    int left = 0, top = 0;
    SDL_GetWindowPosition(g_window, &left, &top);
    const SDL_Rect place{Whole(static_cast<float>(screen.x) / PixelsPerPoint()) - left,
                         Whole(static_cast<float>(screen.y) / PixelsPerPoint()) - top, 1, 1};
    if (g_holding && place.x == g_held.x && place.y == g_held.y) return;
    SDL_SetWindowMouseRect(g_window, &place);
    SDL_SetWindowRelativeMouseMode(g_window, true);
    g_held = place;
    g_holding = true;
}

void ReleaseCursor() {
    if (!g_window || !g_holding) return;
    SDL_SetWindowMouseRect(g_window, nullptr);
    SDL_SetWindowRelativeMouseMode(g_window, false);
    g_holding = false;
}

void SetCursorPosition(Point at) {
    ReleaseCursor();
    if (g_toScreen) at = g_toScreen(at);
    SDL_WarpMouseGlobal(static_cast<float>(at.x) / PixelsPerPoint(), static_cast<float>(at.y) / PixelsPerPoint());
}

// ==== The mouse ======================================================================================================

// The motion SDL's events carried since the last call (the event loop adds it up).
Point TakeMouseMotion() { return g_input.TakeMotion(); }

// ==== The clipboard ==================================================================================================

void CopyText(std::string_view text) { SDL_SetClipboardText(std::string(text).c_str()); }

std::string ClipboardText() {
    char* held = SDL_GetClipboardText();
    std::string text = held ? held : "";
    SDL_free(held);
    return text;
}

} // namespace platform
