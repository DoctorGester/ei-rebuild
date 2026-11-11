#pragma once

// The core of the 2D interface: the interface manager Interface3D, the window bases UiWindow and UiContainer, their
// clickable regions, off-screen surfaces and 3D model previews. Layouts are designed for 800x600; Interface3D holds
// the real screen size and scales between the two; windows keep their regions in layout coordinates and convert the
// mouse position before hit testing. Each window family has its own header (ui_battle.h, ui_options.h, ui_camp.h ...;
// docs/notes/ui.md lists them).

#include "engine/core/engine_object.h"
#include "game/saves/save_picture.h"
#include "engine/models/texture.h"   // TextureKind
#include "game/ui/ui_font.h"
#include "game/units/unit.h"   // LockKey

#include "lib/containers.h"
#include "platform/platform.h"
#include "lib/str.h"
#include "lib/rect.h"
#include "render/text_format.h"

#include <climits>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

// The interface sounds most windows share (Interface3D::PlayInterfaceSound): a confirm, a cancel, a pick.
inline constexpr const char* kMessageBoxOkSound = "buttons\\messbox\\ok.wav";
inline constexpr const char* kMessageBoxCancelSound = "buttons\\messbox\\cancel.wav";
inline constexpr const char* kSaveSelectSound = "buttons\\save\\select.wav";

namespace console { class ConsoleWindow; }
class Party;

class UiWindow;
class UiContainer;
class BriefingScreen;
class BattleScreen;
class CampPages;
class ExchangeScreen;
class MpHeroScreen;
class ChatOverlay;
class UiMesh;
struct Surface;
class Material;
class Texture;
class TextureDevice;
class UiSurface;
class ItemScreen;

// What an input event is (UiEvent::kind).
enum class UiEventKind : std::uint8_t {
    LeftDown,
    LeftUp,
    MouseMove,
    RightDown,
    RightUp,
    LeftDouble,
    RightDouble,
    KeyDown,
    Char,
    KeyUp,
    Wheel,
};

// An input event. The interface hands each to the window that took the mouse or the top window (OnEvent); a
// container offers it to its extra inputs, then its children, until one uses it. A handler without its own
// double click takes one as a press.
struct UiEvent {
    UiEventKind kind = UiEventKind::MouseMove;
    Point at;                                   // the mouse in screen pixels (mouse events and the wheel)
    platform::Key key = platform::Key::None;    // KeyDown, KeyUp
    int scan = 0;                               // KeyDown, KeyUp, Char: the key's scan code (0x100 extended)
    int character = 0;                          // Char: the code point typed
    float delta = 0.0f;                         // Wheel: the turn in notches (fractions from a smooth wheel),
                                                // positive away from the player
    bool repeat = false;                        // KeyDown: the key was down already
    bool handled = false;                       // set by a handler that used it: a container passes it on no further

    // Whether a container offers it only to the children under the mouse (mouse events and the wheel; keys go to
    // every enabled child).
    bool AtMouse() const {
        return kind != UiEventKind::KeyDown && kind != UiEventKind::Char && kind != UiEventKind::KeyUp;
    }
};

namespace screen_layout {
struct Layout;
}

// The interface's screen fade and letterbox, drawn over everything by the interface's draw.
struct ScreenFade {
    // The letterbox bars and, while fading, a black rectangle whose alpha moves by rate * elapsed ms; the
    // fade ends when alpha drops below 0.
    void Draw(SceneRenderer* renderer);
    // Starts a fade over `ms` milliseconds, from alpha 0 (ms > 0) or 255.
    void Start(int ms);
    // The height of each letterbox bar on a screen `height` pixels high.
    static int LetterboxBar(int height);

    float rate = 0.0f;               // alpha per millisecond
    float alpha = 0.0f;              // 0..255
    bool active = false;             // fading
    std::uint32_t lastTime = 0;              // timeGetTime of the last step
    bool letterbox = false;          // the bars are shown
};

// Whether the game's texts are Russian (the video options group's title holds letters beyond ASCII): the texts the
// rebuild adds to the interface are shown in Russian, else in English (ui_options.cpp).
bool GameTextsInRussian();

// The key of the multiplayer message 12's text (Interface3D::MessageText), "reserved_02": the host's pause handler
// writes its digit, '2' for a pause and '3' ("reserved_03") for a resume (EI_Plugin's Pause; player_server_orders.cpp).
extern char g_pauseMessageKey[12];

// What a window pushed over the screen below keeps of it.
enum class WindowBackdrop {
    None,
    Screen,         // the screen below is kept as the backdrop (backdrop mode 3)
    FirstWindow,    // the same (mode 1) when only one window is open, else nothing
};

// Where the backdrop behind the windows stands (Interface3D::backdropMode); DrawOverlays moves it on.
enum class Backdrop {
    None,
    TakeFirst,      // the screen is captured and greyed into `backdrop` at the next DrawOverlays (a window opened over the screen)
    First,          // the greyed screen is drawn behind the windows
    TakeSecond,     // the old backdrop is put aside and the screen captured again (a window opened over a window)
    Both,           // the second backdrop is drawn; the first one is under it
};

// The interface manager.
class Interface3D {
public:
    // Once per frame: pending closes, the queued window, the mouse, the top window and the root.
    void Update();

    // A script string event ("say" / "say_block" text [key], "tutorial" name, "briefing" name, "autosave",
    // "endofgame"); the first word is lowercased. Rebirth's NewStringEvents adds "gameover [title [text]]".
    void OnStringEvent(std::string_view event);
    UiContainer* GetActiveWindow() const;
    // `screen` (one of the screen pointers below) while it is the top window, else null.
    template <typename Screen>
    Screen* OnTop(Screen* screen) const {
        return screen && GetActiveWindow() == screen ? screen : nullptr;
    }
    // Consumed by Update (the top window closes).
    void SetPendingValue(std::uint32_t value);
    // Makes screen `id` (1 in-game, 3 base, 4 island map, 2, 5, 7, 8, 9) and activates it; 0 does
    // nothing; 6 and ids above 9 hand over the id itself as the window pointer.
    void CreateScreen(int id);
    // A key pressed: the key, its set-1 scan code (0x100 extended: the key tables are indexed by it) and
    // whether it was down already (the keyboard's repeat). The key under Esc toggles the console; Scroll Lock saves
    // "shot<nnnn>.bmp"; an unhandled ';' shows help.
    void OnKeyDown(platform::Key key, int scan, bool repeat);
    // Opens `window` on top of the stack (null is fatal). The first window keeps the server flag.
    void PushWindow(std::unique_ptr<UiContainer> window, WindowBackdrop keep);
    // Closes and deletes every window, then opens `window` as PushWindow does.
    void Activate(std::unique_ptr<UiContainer> window);
    // Reads the interface's saved state from `stream`: flags and two values; from save version 0x47 one
    // more; from 0x56 an obsolete list (count, then name and value pairs, discarded) and a string; from
    // 0x5D one more value; from 0x5F a string.
    void LoadState(ByteStream* stream);
    // The text of message `id` (0-12: "lmp_player_connected" .. "reserved_02"; others an empty key) from
    // the text table, formatted with `value` unless 0.
    std::string MessageText(int id, std::string_view value);
    // Without `forced`, takes the next queued message into the current text or returns when there is
    // none. A non-empty text opens the message window over the others, unless the option is off and it
    // was not forced.
    void ShowMessage(bool forced);
    // The active window's help topic (none: nothing): the map counts topics seen (an unseen one is
    // entered with 0, giving -1 here); the help window opens when `forced`, or when the option is
    // on and the topic was not seen.
    void OpenHelp(bool forced);
    // A 2D sound at priority 1000, volume 100; returns its handle.
    int PlayInterfaceSound(std::string_view name, bool loop = false);
    void StopSound(int handle);
    // A multiplayer client's screens call them for Enter / Backspace. While the chat line is closed:
    // Enter opens it, Backspace empties the chat lines.
    void ChatKeyEnter();
    void ChatKeyBackspace();
    // The extent of `text` in interface font `font` (ui_font.h's TextExtent) scaled to the layout by the window's
    // 4:3 part (screen_layout::TextExtentToLayout).
    Size TextExtent(std::string_view text, int font) const;
    // `text` word-wrapped to `width` layout units in `font` (UiSurface::DrawWrappedText's lines: breaks at
    // spaces and '\n', a line keeps the space after its last word).
    std::vector<std::string> WrapText(std::string_view text, int width, int font) const;
    // When there is a console window.
    void ConsoleAddText(std::string_view text);
    void SetConsoleRect(const Rect& rect);
    void SetConsoleHistory(int lines);
    // The console's "fps": a surface for the frame rate at (730, 10)-(800, 28) unless there is one; off
    // frees it.
    void ShowFps(bool on);
    // The FPS surface's paint.
    void PaintFps(UiSurface& surface) const;
    // A quest variable changed (client, script): the top window hears of it; at 1 (or NaN) a name of
    // three or more '.' parts keeps its third part in savedText (the current quest).
    void OnQuestVariable(std::string_view name, float value);
    // News for the top window (nothing without a window), each reaching its handler of that name: items
    // picked up and a unit's speech (the battle screen's), a unit joined / left the party, the game lost, the players
    // changed (the briefing's), an exchange between players ended (the exchange screen's); SaveWindowState has the
    // bottom window save its state.
    void OnItemsPicked(const std::vector<Item*>& items, int money, bool crippled);
    void OnUnitJoined(Unit* unit);
    void OnUnitLeft(Unit* unit);
    void OnGameLost();
    void OnUnitSpeech(int id, AckKind kind);
    void OnPlayersChanged(int added);
    void OnExchangeDone(Party* partyB, Party* partyA, Party* partyC);
    void SaveWindowState();
    void OnChatLine(std::string_view text);
    // The save / load window's status text, drawn in green on a cleared surface over (0, 280)-(800, 320)
    // for one frame, then the surface freed.
    void ShowStatusText(std::string_view text);

    // A mouse event from the input dispatcher (ui_interface_input.cpp; its kind, position and the wheel's turn): it
    // goes to the window that took the mouse, else the top window, else nowhere. Button downs and double clicks set
    // or clear the button flags around the call.
    void OnMouse(UiEvent event);
    UiWindow* MouseTarget() const;
    // A typed character (a code point) with the scan code of the key that typed it: nothing for the key under Esc;
    // the shown console's OnChar, else a multiplayer client's open chat line, else the top window.
    void OnChar(int key, int scan);
    // A key released: Shift, Control and Alt clear their modifier flags; then the shown console's OnKeyUp, else the top
    // window.
    void OnKeyUp(platform::Key key, int scan);
    // Closes and deletes the top window: its input deactivated, its CloseScreen run.
    void PopWindow();
    // Closes every window, top first.
    void CloseAll();
    // CloseAll when asked for.
    void CloseAllPending();
    // Every frame from the main loop: a pending close-all, the backdrop (modes 2-4), the top window's
    // draw, then the fade over it all.
    void Draw();
    // Rebirth-patched (WideScreen). What is blitted over the scene after EndScene: the top window's surfaces and tooltips, the status surface, a backdrop asked for (mode 1
    // also keeps a 256 x 192 copy and greys it into mode 2; mode 3 puts the old one aside into mode 4), the
    // client's chat overlay, the frame rate and the console.
    void DrawOverlays();
    // Rebirth-patched (WideScreen). Makes the three "Times New Roman" fonts sized to the
    // screen width, activates the game language's keyboard layout (else English, else the one it began
    // with), reads the key bindings and config\tutorial.ini's help topics, and makes the console, debug
    // panel, chat overlay and fade.
    void Create(TextureDevice* device);
    // The device's screen took another size (app::SetResolution, between frames; `before`: the layout it had): the
    // interface's size and fonts, every surface placed again (UiSurface::RelayoutAll), then each window on the stack,
    // the bottom one first, built again where its layout depends on the window's shape (UiContainer::LayoutChanged),
    // and the backdrops taken again (RetakeBackdrops).
    void Resize(const screen_layout::Layout& before);
    // The screen copies behind the windows (modes 2 and 4) taken again at the screen's size: frame steps without a
    // present with the stack cut back to the windows each copy showed (the bottom one, greyed, with the escape menu's
    // save copy; then those under a help window over it), as PushWindow took them.
    void RetakeBackdrops();
    // Where the game's picture is on the screen: the whole screen, less the letterbox bars while they are shown.
    // Presses outside it are not passed on (app.cpp's input dispatch) and the camera's edge pan works at its edges.
    Rect PictureRect() const;
    // Whether the top window's camera input pans at the picture's edges now (the world view is up, not a menu, the
    // escape menu or a modal window, and the border scroll area is above 0): app::UpdateCursorConfinement's condition.
    bool EdgePanLive() const;

    int width = 0;                   // screen width
    int height = 0;                  // screen height
    TextureDevice* renderer = nullptr;    // the device
    UiWindow* mouseCapture = nullptr;    // the window that took the button press
    console::ConsoleWindow* console = nullptr;
    ScreenFade* fade = nullptr;
    ChatOverlay* root = nullptr;    // the chat overlay; updated after the top window every frame
    std::unique_ptr<UiSurface> fpsSurface;
    std::uint32_t fpsTime = 0;
    int fpsFrames = 0;
    std::string fpsText;             // the last measured rate ("%.1f"); empty until the first period
    // The windows draw, redraw their surfaces and update their tooltips only while this is set.
    bool drawing = false;
    std::vector<std::unique_ptr<UiContainer>> stack;    // the open windows (every one a container), the top one last
    // The screens with news of their own, while open (each sets itself in its CreateScreen and clears itself in its
    // CloseScreen); the news reaches one only while it is the top window (OnTop).
    BattleScreen* battle = nullptr;
    BriefingScreen* briefing = nullptr;
    ExchangeScreen* exchange = nullptr;
    CampPages* camp = nullptr;
    bool closeAll = false;
    std::uint32_t pendingValue = 0;
    bool pendingFlag = false;
    Backdrop backdropMode = Backdrop::None;   // the backdrop is drawn in First, TakeSecond and Both (PushWindow sets TakeSecond / TakeFirst)
    std::unique_ptr<UiSurface> backdrop;
    std::unique_ptr<UiSurface> secondBackdrop;
    std::size_t backdropWindows = 0;   // mode 4: the windows its backdrop shows (the stack's size when mode 3 was set)
    std::unique_ptr<UiSurface> screenCopy;   // never shown: the screen under the first window, the escape menu's save picture
    std::uint8_t serverFlag = 0;     // the server flag kept for the first window (PushWindow)
    std::unique_ptr<UiSurface> statusSurface;    // only while ShowStatusText runs
    Point mouse;
    // The three interface fonts (Create: "Times New Roman", sized with the screen width).
    UiFont fonts[3];
    // Set by the key-down handler from the scan codes, cleared by key-up and when the windows change.
    bool ctrlDown = false;
    std::uint8_t shiftDown = 0;
    bool altDown = false;
    // The left press is a double click (a spell or quick item clicked twice is used at once).
    bool doubleClick = false;
    bool rightDown = false;
    // The top window is asked every frame until it agrees ("autosave" sets it).
    bool pollTop = false;
    std::deque<std::string> messages;   // queued message texts (ShowMessage takes the first)
    bool disconnected = false;
    // Why the client left the server (PlayerClient::LeaveAfterTick): the disconnect box's text instead of
    // "disconnect_msg"; empty for a lost connection. GameFlow::DropSessions clears it.
    std::string disconnectReason;
    // The state saved with a game (LoadState), cleared by ResetSavedState. The HUD panels keep theirs
    // here every frame.
    std::uint8_t hudPanelsIn[3] = {};    // [0] the unit panel, [1] the top panel, [2] the map
    int hudBodyPart = 0;             // HudUnitPanel::bodyPart
    int hudTopPage = 0;              // HudTopPanel::page
    int fastGame = 0;                // the fast game speed (HudClock::fast)
    // Help topics seen: name -> times (or the page the help window was closed on; ui_help_window.cpp);
    // config\tutorial.ini "name count" lines, read by Create, written by Shutdown, counted by OpenHelp.
    std::map<std::string, int, str::MbsLess> helpTopics;
    std::string messageText;
    int messageFlag = 0;             // the message window's page, set when it closes
    std::string savedText;          // the current quest (OnQuestVariable); saved from version 0x5F

    // Clears the modifier and button flags and deactivates the top window.
    void ResetInput();
    // The game's stop (its exit, a switch of mods): closes every window, writes the help topics to
    // config\tutorial.ini and forgets them, saves the key bindings and frees what Create made (the queued window's
    // pointer is not cleared).
    void Shutdown();

    // Writes what LoadState reads, with an empty obsolete list (count 0).
    void SaveState(ByteStream* stream);

    void ResetSavedState();
};

extern Interface3D g_interface;

enum class SkillAnimation : int;   // objects/object3d.h

// The pale blue the interface draws its texts, rows and numbers in.
inline constexpr std::uint32_t kUiTextColour = 0xA7D7E4;

// How a text is drawn: one of the interface's three fonts (Interface3D::fonts), its colour, DrawText's format bits
// (render::text::Format) and whether a shadow is drawn first (the interface's texts have one, the tooltips none).
struct TextStyle {
    int font = 0;
    std::uint32_t colour = 0;
    render::text::Format format = render::text::Format::Left;
    bool shadow = true;
};

// A rectangle of the interface a window draws into, drawn into the frame each frame by its Blit (ui_surface.cpp).
// How a window surface's Blit draws it: every pixel, or with its background as the source colour key (the
// pixels of that colour left out).
enum class SurfaceBlend : bool { Opaque, Keyed };

class UiSurface {
public:
    // A surface of the layout rectangle drawn with `blend`, scaled to the screen with `scale`: shown.
    UiSurface(int left, int top, int right, int bottom, SurfaceBlend blend, bool scale);
    UiSurface(const Rect& rect, SurfaceBlend blend, bool scale);
    // Over the layout rectangle `rect` (always scaled).
    UiSurface(const Rect* rect, SurfaceBlend blend);
    virtual ~UiSurface();
    UiSurface(const UiSurface&) = delete;
    UiSurface& operator=(const UiSurface&) = delete;
    // Every surface placed again after the screen's size changed from `before` (app::SetResolution): its rectangle
    // scaled from its layout rectangle as the layout scales now; what it draws is laid out again at the new size.
    static void RelayoutAll(const screen_layout::Layout& before);
    // Into the frame, when shown.
    virtual void Blit();

    // Layout coordinates.
    void MoveTo(Point at);
    // Word-wraps `text` to the width of `rect` (layout coordinates; breaks at spaces and '\n') and draws up
    // to `maxLines` lines one rect height apart with DrawTextIn. Returns the line count.
    int DrawWrappedText(std::string_view text, const Rect& rect, int maxLines, const TextStyle& style);
    // Rebirth-patched (WideScreen). `rect` in layout coordinates (empty: the whole surface),
    // grown by two pixels once scaled; a style with `shadow` first draws the text in 0x080808 one pixel right and down.
    // Returns the drawn height in layout units, 0 without a surface.
    int DrawTextIn(std::string_view text, const Rect& rect, const TextStyle& style);
    // DrawText of `text` (UTF-8) in `font` in `rect` (surface pixels) by DrawText's `format` (render_text.h's
    // Layout), in `colour`, first in 0x080808 one pixel right and down when `shadow`. Returns DrawText's value
    // (the height laid out, pixels); 0 without a surface or font.
    int DrawTextPixels(const UiFont& font, std::string_view text, const Rect& rect, render::text::Format format, std::uint32_t colour,
                       bool shadow);
    // Show: shown. Hide: not shown (its frame picture is kept).
    void Show();
    void Hide();
    // CaptureScreen: the screen under the surface copied into it (a frame picture). ReadPicture: that picture's
    // pixels (empty without one). Greyscale: the picture drawn as (2 r + 4 g + b) / 16 per channel.
    void CaptureScreen();
    save_picture::Picture ReadPicture() const;
    void Greyscale();

    bool Shown() const { return shown; }

    // A one-pixel frame of `colour` around `rect` (layout coordinates; empty: the whole
    // surface).
    void FrameRect(const Rect& rect, std::uint32_t colour);
    // The whole surface; Clear fills with background.
    void Fill(std::uint32_t colour);
    void Clear();
    // In surface pixels: `rect` filled (CDC::FillSolidRect); a solid pen's line of `width`
    // pixels from `from` to `to` (MoveTo, LineTo: the game's 1 and 2 pixel pens, render_ui.h's Line); one
    // pixel (SetPixel).
    void FillRect(const Rect& rect, std::uint32_t colour);
    void Line(Point from, Point to, int width, std::uint32_t colour);
    void Dot(Point at, std::uint32_t colour);
    // RelayoutAll's step for this surface.
    void Relayout(const screen_layout::Layout& before);

    SurfaceBlend blend = {};
    bool shown = true;               // set by Show, cleared by Hide
    int width = 0;
    int height = 0;
    bool greyed = false;             // its frame picture drawn grey
    Rect rect;                       // screen coordinates
    Rect layoutRect;                 // what rect is scaled from (layout coordinates); screen pixels when not `scaled`
    bool scaled = true;              // made over a layout rectangle (not the screen's pixels)
    std::uint32_t background = 0;         // the colour Clear fills with

    // Its window paints it each frame: Blit calls `paint` after the frame picture, and what paint draws goes
    // straight into the frame. A draw outside the paint draws nothing (a text still returns its value).
    void PaintWith(std::function<void(UiSurface&)> paint);

private:
    std::function<void(UiSurface&)> paint;
    bool painting = false;           // inside paint
    Rect paintClip;                  // Blit's clip while painting
    bool Transparent(std::uint32_t colour) const;
    int DrawTextFrame(const UiFont& font, std::string_view text, const Rect& frame, render::text::Format format, std::uint32_t colour,
                      bool shadow);
};

// A keyed surface over `area` (layout coordinates, scaled to the screen): what most windows draw on.
inline std::unique_ptr<UiSurface> NewSurface(const Rect& area) {
    return std::make_unique<UiSurface>(area, SurfaceBlend::Keyed, true);
}

class UiRegion;

// The tooltip of a region (UiRegion::tip); its surface is made while it is shown.
struct UiTooltip {
    // The showing: the region sets Hovered (mouse inside) or Disappearing (left); OnMouseMove does the rest.
    enum class State { Idle, Hovered, Appearing, Disappearing, Shown };

    UiTooltip(UiRegion* region, std::string_view text, int command);
    // Empty: nothing. The size is not measured again.
    void SetText(std::string_view text);
    // The surface's paint: the background, the frame, the first line in the title colour, followed by
    // "  ( <key> )" with keyName, then the others.
    void PaintLines(UiSurface& out) const;
    // Rebirth-patched (WideScreen). One step of the showing, mouse in layout coordinates;
    // Hovered waits for the delay (setting 20; 0: never shown), then makes the surface at the mouse, painted from
    // then on (Appearing), or goes back to Idle when the mouse has left the region; Appearing becomes Shown;
    // Disappearing waits while the timer is positive, then drops the surface (Idle).
    void OnMouseMove(Point point);
    // While shown: the surface into the frame.
    void Draw();
    bool Shown() const { return state != State::Idle && state != State::Hovered; }

    std::vector<std::string> lines; // the text's lines (none for an empty text)
    int lineHeight = 0;              // the last line's height: the rows' step
    int keyWidth = 0;                // the first line with the key name, + 12
    int extra = 0;                   // a command whose key is shown
    const char* keyName = nullptr;   // that key's name, or null
    UiRegion* region = nullptr;     // the region it belongs to
    int width = 0;                   // widest line + 12 (a text only)
    int height = 0;                  // the lines' heights + 8
    float timer = 0.0f;              // seconds in the current state
    State state = State::Idle;
    std::unique_ptr<UiSurface> surface;   // made when it appears, dropped when it has disappeared
};

// A clickable area of a window, created by UiWindow::AddRegion: a rectangle, optionally narrowed to a
// convex polygon, and an optional tooltip.
class UiRegion {
public:
    bool HitTest(Point point) const;
    bool HasActiveTip() const;
    // Also enables the region.
    void SetRect(int left, int top, int right, int bottom);
    void SetRect(const Rect& area);
    // Adds a polygon vertex, growing the bounds; three vertices enable the region.
    void AddPoint(int x, int y);
    void OnMouseMove(Point point);
    void SetTipText(std::string_view text);
    // Replaces the tooltip by one with the text "tip <id>" (up to a '#'; the key itself when there is no
    // such text) showing command `command`'s key (0: none); id 0 gives an empty tooltip.
    void SetTip(int textId, int command = 0);
    void SetTipString(std::string_view text, int command = 0);

    std::vector<Point> points;    // polygon vertices
    Rect bounds;
    bool rectOnly = false;
    bool enabled = false;
    int offsetX = 0;                 // subtracted from tested points
    int offsetY = 0;
    std::unique_ptr<UiTooltip> tip;
};

// The place of `region` in `list` (a window's regions, a panel's array of buttons): a list's slot; -1 when the
// region is null or not there.
template <typename List>
int RegionIndex(const List& list, const UiRegion* region) {
    int index = 0;
    for (const auto& entry : list) {
        if (region && std::to_address(entry) == region) return index;
        ++index;
    }
    return -1;
}

// A text a window shows on a painted surface, kept as the window's state where it drew it before: DrawTextIn's
// arguments (layout units, one of the interface's fonts, with the shadow) or, with maxLines, DrawWrappedText's
// (wrapped again at the current fonts when painted).
struct UiLabel {
    std::string text;
    Rect area;
    TextStyle style;
    int maxLines = 0;                // > 0: word-wrapped, at most this many lines
};
// A window's paint step: `labels` drawn in order.
void PaintLabels(UiSurface& surface, const std::vector<UiLabel>& labels);

// A 3D model shown in a window (an inventory item, a portrait).
class UiModel {
public:
    void SetScreenPosition(float x, float y, float depth);
    UiModel() = default;
    // The object released.
    ~UiModel();
    UiModel(const UiModel&) = delete;
    UiModel& operator=(const UiModel&) = delete;
    // Draws the object in `pass` in white at the interface's animation clock (timeGetTime / 67), with
    // object flag 0x800 set while it draws.
    void Draw(SceneRenderer* pass);
    // A new map object (MapObjectType::Creature, id 0x88CA6C00) of the client world's map object manager, referenced,
    // MapObjectFlag::Preview, at (0, 0, 3), with the figure of model `name` (all parts), its textures and
    // animations, and texture `from` swapped for `to`.
    void Load(const char* name, const char* from, const char* to);
    void ShowAllParts(int show);
    void ShowPart(const char* name, int show);
    // Plays the animation `animation`.
    void Play(SkillAnimation animation);
    // Plays the figure's rest pose (the object manager's object named like the figure).
    void Stop();

    Object3D* object = nullptr;
};

// The wheel's units a notch: the original took the turn in Windows' units, and its wheel constants are per unit.
inline constexpr float kWheelUnits = 120.0f;

// A container's extra input: a handler that is not a window (the battle camera's, CameraInput; the view screen's unit
// picking, UnitPickInput). The container offers each event to its extra inputs before its children.
class UiInput {
public:
    virtual ~UiInput() = default;
    virtual void OnEvent(UiEvent& event) = 0;
    // The input loses the screen (the window under it deactivated): held buttons and modes let go.
    virtual void Deactivate() {}
    // Whether it pans the camera at the screen's edges (CameraInput with the border scroll area above 0).
    virtual bool PansAtEdges() const { return false; }

};

// Base of the interface windows.
class UiWindow : public TransformObject {
public:
    UiWindow();
    // The counted objects released; the mouse let go when the window had taken it.
    ~UiWindow() override;

    // The class name is "Sorry, no other way." and the id 0x45.
    void Create() override {}
    void Destroy() override {}
    const char* ClassName() override;
    ClassId GetClassId() override;
    virtual void Build() {}
    // Into the frame, in the scene: the window's sprites and models; a container's children after them.
    virtual void DrawSprites(SceneRenderer* pass) { DrawBase(pass); }
    // After the scene: the window's surfaces blit themselves; a container's children after them.
    virtual void DrawSurfaces();
    // After the surfaces: the tooltips follow the mouse and redraw.
    virtual void DrawTips();
    virtual void Update() {}
    // An input event (UiEvent); the base uses none.
    virtual void OnEvent(UiEvent&) {}
    // The window loses the input (popped, the application deactivated, a container's whole tree): held buttons and
    // modes let go. A container's goes to its extra inputs and every child.
    virtual void Deactivate() {}

    // The first region containing the screen point, or null.
    UiRegion* RegionUnder(Point point) const;
    bool ContainsPoint(Point point) const;

    void SetEnabled(bool enabled);

    // Takes a reference (null is ignored).
    void AddObject(Texture* texture);
    void RemoveObject(Texture* texture);
    void RemoveObjectAt(int index);
    void ClearObjects();

    bool RemoveSceneObject(EngineObject* object);
    // Replaces scene object `object` (released) at its place with object `name` of the model manager,
    // given the window's texture 0 and the old one's position, orientation, texture and scale; returns the
    // new one or null.
    UiMesh* ReplaceSceneObject(UiMesh* object, std::string_view name);
    void ClearSceneObjects();

    // Kept in the surfaces; returns it.
    template <typename T>
    T* AddSurface(std::unique_ptr<T> surface) {
        T* added = surface.get();
        surfaces.push_back(std::move(surface));
        return added;
    }
    void ClearSurfaces();

    UiRegion* AddRegion();
    // Deletes `region` and drops it from the list (nothing when it is not listed).
    void RemoveRegion(UiRegion* region);
    void ClearRegions();

    UiModel* AddModel();

    // Takes the mouse for the next button-up (what every button-down handler does).
    void CaptureMouse() { g_interface.mouseCapture = this; }
    // Button-up handlers act only when they took the mouse; they release it first.
    bool ReleaseMouse() {
        if (g_interface.mouseCapture != this) return false;
        g_interface.mouseCapture = nullptr;
        return true;
    }
    // A button's mouse handling (like ImGui's ButtonBehavior) over the window's first `count` regions:
    // HitRegion gives the region under the event's point, the event marked used, or null (a move, a wheel turn);
    // PressRegion also takes the mouse (a button down); PressedRegion is the release: null without the
    // mouse taken here, which it releases first.
    UiRegion* HitRegion(UiEvent& event, int count = INT_MAX);
    UiRegion* PressRegion(UiEvent& event, int count = INT_MAX);
    UiRegion* PressedRegion(UiEvent& event, int count = INT_MAX);
    // The scene objects, then the models.
    void DrawBase(SceneRenderer* pass);
    // The camp's and the multiplayer hero screen's hero preview, turned by `turn` radians about y; `wide`
    // clears the depth under six camp layout rectangles, else one. The hero's transform is restored.
    void DrawHeroPreview(SceneRenderer* pass, Unit* hero, float turn, float depth, bool wide);
    // Copies the top-left width x height of image "<name>.mmp" (decoded as imageWidth x imageHeight) into
    // `texture`'s 256-wide 1:5:5:5 surface at (x, y), or white without such an image. Returns whether the
    // image exists (false also when the lock fails).
    bool CopyImage(Texture* texture, std::string_view name, int imageWidth, int imageHeight, int width, int height,
                  int x, int y);
    // The texture manager's texture `name` of `kind` (found or made), loaded and appended to the
    // referenced objects with one more reference; null when either step fails (a texture made but not
    // loaded is kept by the manager).
    Texture* LoadTexture(std::string_view name, TextureKind kind);
    // A new CPU-filled texture (flag 0x40000) kept with the referenced objects; null when none is made.
    Texture* CreateTexture(int width, int height, TextureKind kind);
    // Object `name` of the model manager added to the scene objects with the black material and the first
    // referenced object as its texture; null when the manager makes none. AddBlankSprite: a new UiMesh.
    UiMesh* AddSprite(std::string_view name);
    UiMesh* AddBlankSprite();
    UiMesh* AddSceneObject(UiMesh* object);   // AddSprite's shared end: listed, black material, texture 0

    Rect rect = {};                           // layout coordinates
    // The layout x the window's own coordinates start at: its code gives its regions and sprites x + originX. The
    // battle HUD's edge panels sit at -EdgeShift() (the left edge) or +EdgeShift() (the right edge), every other
    // window at 0 (screen_layout.h).
    int originX = 0;
    bool enabled = true;
    Material* blackMaterial = nullptr;   // given to every sprite (AddSprite)
    Material* whiteMaterial = nullptr;
    std::vector<Texture*> objects;             // the referenced textures
    std::vector<UiMesh*> sceneObjects;
    std::vector<std::unique_ptr<UiSurface>> surfaces;
    std::vector<std::unique_ptr<UiRegion>> regions;
    std::vector<std::unique_ptr<UiModel>> models;
};

// The depth buffer set to `z` over `count` screen rectangles (at most 8); all of it without rectangles
// (ui_interface.cpp).
void ClearUiDepth(float z, const Rect* rects = nullptr, int count = 0);
// A screen's draw (the briefing screen's, the main menu's): the client world, then a mouse move at the cursor
// when `mouseMove`, the depth buffer cleared to 1 and the container draw (ui_container.cpp).
void DrawScreen(UiContainer* self, SceneRenderer* pass, bool mouseMove);

// --- Screens with child windows ---

// Base of the screens that hold child windows and extra inputs (the Esc menu, the save/load and
// options windows, the battle screen). An event goes to the extra inputs in order, then to the enabled
// children (mouse events only to those holding the point), until one uses it; see
// docs/notes/ui.md, "Container input dispatch".
class UiContainer : public UiWindow {
public:
    // Only while the interface draws: the window's own draw, then every enabled child's.
    void DrawSprites(SceneRenderer* pass) override;
    // Only while the interface draws: the window's own, then every enabled child's.
    void DrawSurfaces() override;
    void DrawTips() override;
    void Update() override;
    // The screens' virtuals the interface calls (Interface3D); every window put on the stack has its own, the others' defaults
    // do nothing. The news only one screen hears goes to it through the interface's pointer to it (Interface3D::battle).
    virtual void CreateScreen() = 0;
    virtual void CloseScreen() = 0;
    virtual void MessageResult(std::uint32_t) {}                                 // the value a window above left pending when it closed
    virtual void UnitJoined(Unit*) {}
    virtual void UnitLeft(Unit*) {}
    virtual void GameOver(bool) {}
    virtual bool ReadyToStopPolling() { return false; }                // whether the interface may stop asking (pollTop)
    virtual void PlayerList(int) {}                                    // the player list for kicking (1) or banning (2)
    virtual void SaveState() {}                                        // the window's part of a save game
    virtual const char* HelpTopic() { return nullptr; }
    // The screen took another size (Interface3D::Resize), after every surface was placed again: what of the
    // window was built for the window's old shape is built again (the battle screen's panels at the edges, the
    // camp's curtains over the margins). Most windows have nothing of that.
    virtual void LayoutChanged() {}

    void Deactivate() override;   // every extra input and child
    // The event, unused so far, to the popup alone (then used); else to each extra input, then each enabled child
    // (under the mouse for mouse events and the wheel), until one uses it.
    void OnEvent(UiEvent& event) override;

    // Seconds since the last frame (milliseconds * 0.001f).
    static float FrameTime();
    // The last frame's whole milliseconds (the ticks the held buttons' repeats count).
    static int FrameTicks();
    // Kept in the children; returns it.
    template <typename T>
    T* AddChild(std::unique_ptr<T> child) {
        T* added = child.get();
        children.push_back(std::move(child));
        return added;
    }
    void AddInput(std::unique_ptr<UiInput> input) { inputs.push_back(std::move(input)); }

    std::vector<std::unique_ptr<UiWindow>> children;
    std::vector<std::unique_ptr<UiInput>> inputs;   // extra inputs
    // A child that takes every input event while it is set, wherever the mouse is (an open drop-down list).
    UiWindow* popup = nullptr;
};
