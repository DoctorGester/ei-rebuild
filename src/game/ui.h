#pragma once

// The 2D interface: the interface manager Interface3D, its windows, their clickable regions, off-screen
// surfaces and 3D model previews. Layouts are designed for 800x600; Interface3D holds the real screen
// size and scales between the two; windows keep their regions in layout coordinates and convert the mouse
// position before hit testing.

#include "engine_object.h"
#include "object_tables.h"   // g_keyActions
#include "save_picture.h"
#include "texture.h"   // TextureKind
#include "ui_font.h"
#include "unit.h"   // LockKey

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

inline constexpr float kHudFullTurn = /*007C8140*/ 6.2831855f;
inline constexpr float kHudQuarterTurn = /*007C8144*/ 1.5707964f;
inline constexpr Quaternion kHudRestRotation = /*007C8150*/ {1.0f, 0.0f, 0.0f, 0.0f};

// The interface sounds most windows share (Interface3D::PlayInterfaceSound): a confirm, a cancel, a pick.
inline constexpr const char* kMessageBoxOkSound = "buttons\\messbox\\ok.wav";
inline constexpr const char* kMessageBoxCancelSound = "buttons\\messbox\\cancel.wav";
inline constexpr const char* kSaveSelectSound = "buttons\\save\\select.wav";

namespace console { class ConsoleWindow; }
class Party;

class UiWindow;
class UiContainer;
class BriefingScreen;
class ChatOverlay;
struct UiInputSide;
class UiMesh;
struct Surface;
class Material;
class Texture;
class TextureDevice;
class UiSurface;
class ItemScreen;
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

// The key of the multiplayer message 12's text (Interface3D::MessageText), "reserved_02": EI_Plugin's Pause
// writes its digit, '2' for a pause and '3' ("reserved_03") for a resume (player_server_orders.cpp).
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
    // Layout (800x600) to screen and back, in the window's 4:3 part (screen_layout.h).
    void ScaleRectToScreen(Rect* rect) const;
    void ScalePointToScreen(Point* point) const;
    void ScaleRectFromScreen(Rect* rect) const;
    void ScalePointFromScreen(Point* point) const;

    // The same for sizes and offsets, without the 4:3 part's margins: the text surfaces' draw and text height, edit
    // field, connection list, save / load list, chat line, tooltip, the drags.
    void ScaleRectToScreenNoMargin(Rect* rect) const;
    void ScalePointToScreenNoMargin(Point* point) const;
    void ScalePointFromScreenNoMargin(Point* point) const;

    // Once per frame: pending closes, the queued window, the mouse, the top window and the root.
    void Update();

    // A script string event ("say" / "say_block" text [key], "tutorial" name, "briefing" name, "autosave",
    // "endofgame"); the first word is lowercased. Rebirth's NewStringEvents adds "gameover [title [text]]".
    void OnStringEvent(std::string_view event);
    UiContainer* GetActiveWindow() const;
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
    void PushWindow(UiContainer* window, WindowBackdrop keep);
    // Closes and deletes every window, then opens `window` as PushWindow does.
    void Activate(UiContainer* window);
    // Reads the interface's saved state from `stream`: flags and two values; from save version 0x47 one
    // more; from 0x56 an obsolete list (count, then name and value pairs, discarded) and a string; from
    // 0x5D one more value; from 0x5F a string.
    void LoadState(ByteStream* stream);
    // The text of message `id` (0-12: "lmp_player_connected" .. "reserved_02"; others an empty key) from
    // the text table, formatted with `value` unless 0.
    std::string MessageText(int id, const char* value);
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
    void SetConsoleRect(const Rect* rect);
    void SetConsoleHistory(int lines);
    // The console's "fps": a surface for the frame rate at (730, 10)-(800, 28) unless there is one; off
    // frees it.
    void ShowFps(bool on);
    // The FPS surface's paint.
    void PaintFps(UiSurface& surface) const;
    // A quest variable changed (client, script): the top window hears of it; at 1 (or NaN) a name of
    // three or more '.' parts keeps its third part in savedText (the current quest).
    void OnQuestVariable(std::string_view name, float value);
    // News for the top window (nothing without a window), each reaching its virtual of that name: items
    // picked up, a unit joined / left the party, the game lost, a unit speaks, the players changed, an
    // exchange between players ended; SaveWindowState has the bottom window save its state.
    void OnItemsPicked(const std::vector<Item*>& items, int money, bool crippled);
    void OnUnitJoined(Unit* unit);
    void OnUnitLeft(Unit* unit);
    void OnGameLost();
    void OnUnitSpeech(int id, AckKind kind);
    void OnPlayersChanged(int added);
    void OnExchangeDone(Party* partyB, Party* partyA, Party* partyC);
    void SaveWindowState();
    void OnChatLine(const char* text);
    // The save / load window's status text, drawn in green on a cleared surface over (0, 280)-(800, 320)
    // for one frame, then the surface freed.
    void ShowStatusText(std::string_view text);

    // Mouse messages from the input dispatcher (ui_interface_input.cpp): each goes to the input side that
    // took the mouse, else the top window's, else nowhere. Button downs and double clicks set or clear
    // the button flags around the call.
    void OnLeftDown(Point point);
    void OnLeftUp(Point point);
    void OnMouseMove(Point point);
    void OnRightDown(Point point);
    void OnRightUp(Point point);
    void OnLeftDouble(Point point);
    void OnRightDouble(Point point);
    void OnWheel(float notches, Point point);
    UiInputSide* MouseTarget() const;
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
    UiInputSide* mouseCapture = nullptr;    // input side that took the button press
    console::ConsoleWindow* console = nullptr;
    ScreenFade* fade = nullptr;
    ChatOverlay* root = nullptr;    // the chat overlay; updated after the top window every frame
    UiSurface* fpsSurface = nullptr;
    std::uint32_t fpsTime = 0;
    int fpsFrames = 0;
    std::string fpsText;             // the last measured rate ("%.1f"); empty until the first period
    // The windows draw, redraw their surfaces and update their tooltips only while this is set.
    bool drawing = false;
    std::vector<UiContainer*> stack;    // the open windows (every one a container), the top one last
    UiContainer* queuedWindow = nullptr;    // activated by the next frame update
    bool closeAll = false;
    std::uint32_t pendingValue = 0;
    bool pendingFlag = false;
    Backdrop backdropMode = Backdrop::None;   // the backdrop is drawn in First, TakeSecond and Both (PushWindow sets TakeSecond / TakeFirst)
    UiSurface* backdrop = nullptr;
    UiSurface* secondBackdrop = nullptr;
    std::size_t backdropWindows = 0;   // mode 4: the windows its backdrop shows (the stack's size when mode 3 was set)
    UiSurface* screenCopy = nullptr;   // never shown: the screen under the first window, the escape menu's save picture
    std::uint8_t serverFlag = 0;     // the server flag kept for the first window (PushWindow)
    UiSurface* statusSurface = nullptr;    // only while ShowStatusText runs
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

    // Clears the modifier and button flags and deactivates the top window's input side.
    void ResetInput();
    // The main loop's exit: closes every window, writes the help topics to config\tutorial.ini, saves the
    // key bindings and frees what Create made (the queued window and the fade pointers are not cleared).
    void Shutdown();

    // Writes what LoadState reads, with an empty obsolete list (count 0).
    void SaveState(ByteStream* stream);

    void ResetSavedState();
};

extern Interface3D g_interface;

class UiRegion;
struct UiTooltip;

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
    // Enabled when its rectangle is set.
    explicit UiRegion(Interface3D* ownerUi)
        : ui(ownerUi), rectOnly(false), enabled(false), offsetX(0), offsetY(0), tip(nullptr) {}
    ~UiRegion();
    UiRegion(const UiRegion&) = delete;
    UiRegion& operator=(const UiRegion&) = delete;
    void SetTipText(std::string_view text);
    // Replaces the tooltip by one with the text "tip <id>" (up to a '#'; the key itself when there is no
    // such text) showing command `command`'s key (0: none); id 0 gives an empty tooltip.
    void SetTip(int textId, int command = 0);
    void SetTipString(std::string_view text, int command = 0);

    Interface3D* ui = nullptr;
    std::vector<Point> points;    // polygon vertices
    Rect bounds;
    bool rectOnly = false;
    bool enabled = false;
    int offsetX = 0;                 // subtracted from tested points
    int offsetY = 0;
    UiTooltip* tip = nullptr;
};

// The place of `region` in `list` (a window's regions, a panel's array of buttons): a list's slot; -1 when the
// region is null or not there.
template <typename List>
int RegionIndex(const List& list, const UiRegion* region) {
    int index = 0;
    for (const UiRegion* entry : list) {
        if (region && entry == region) return index;
        ++index;
    }
    return -1;
}

enum class SkillAnimation : int;   // map_object_figures.h

// The pale blue the interface draws its texts, rows and numbers in.
inline constexpr std::uint32_t kUiTextColour = 0xA7D7E4;

// How a text is drawn: one of the interface's three fonts (Interface3D::fonts), its colour, DrawText's format bits
// (render::text::Format) and whether a shadow is drawn first (the interface's texts have one, the tooltips none).
struct TextStyle {
    int font = 0;
    std::uint32_t colour = 0;
    std::uint32_t format = render::text::kLeft;
    bool shadow = true;
};

// A rectangle of the interface a window draws into, drawn into the frame each frame by its Blit (ui_surface.cpp).
// How a window surface's Blit draws it: every pixel, or with its background as the source colour key (the
// pixels of that colour left out).
enum class SurfaceBlend : bool { Opaque, Keyed };

class UiSurface {
public:
    // A surface of the layout rectangle drawn with `blend`, scaled to the screen with `scale`: shown.
    UiSurface(Interface3D* ui, int left, int top, int right, int bottom, SurfaceBlend blend, bool scale);
    UiSurface(Interface3D* ui, const Rect& rect, SurfaceBlend blend, bool scale);
    // Over the layout rectangle `rect` (always scaled).
    UiSurface(Interface3D* ui, const Rect* rect, SurfaceBlend blend);
    // Not virtual (a window's surface list destroys a scrolling surface as a plain one).
    ~UiSurface();
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
    int DrawTextPixels(const UiFont& font, std::string_view text, const Rect& rect, std::uint32_t format, std::uint32_t colour,
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
    void FrameRect(const Rect* rect, std::uint32_t colour);
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

    Interface3D* ui = nullptr;
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
    int DrawTextFrame(const UiFont& font, std::string_view text, const Rect& frame, std::uint32_t format, std::uint32_t colour,
                      bool shadow);
};

// A keyed surface over `area` (layout coordinates, scaled to the screen): what most windows draw on.
inline UiSurface* NewSurface(Interface3D* ui, const Rect& area) { return new UiSurface(ui, area, SurfaceBlend::Keyed, true); }

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

// An input side: a window's own (UiWindow's second base) or a separate handler a container keeps in its
// list (e.g. the battle camera's keys, CameraInput). The interface hands each event to the top window's
// side (arguments: the mouse's position in screen pixels; a key (platform::Key), its scan code and, pressed, its repeat;
// a character's code point and scan code; the wheel's turn in notches and the position). The base does nothing, except
// that a double click is a press.
struct UiInputSide {
    // The handled flag is left as it is (a dispatcher clears it before each event).
    explicit UiInputSide(Interface3D* ownerUi) : ui(ownerUi) {}
    virtual ~UiInputSide() = default;
    virtual void Deactivate() {}
    virtual void LeftDown(Point) {}
    virtual void LeftUp(Point) {}
    virtual void MouseMove(Point) {}
    virtual void RightDown(Point) {}
    virtual void RightUp(Point) {}
    virtual void LeftDouble(Point point);
    virtual void RightDouble(Point point);
    virtual void KeyDown(platform::Key, int, bool) {}
    virtual void Char(int, int) {}
    virtual void KeyUp(platform::Key, int) {}
    // The wheel turned by `notches` (fractions from a smooth wheel), positive away from the player.
    virtual void Wheel(float notches, Point point);
    // Whether the side pans the camera at the screen's edges (CameraInput with the border scroll area above 0).
    virtual bool PansAtEdges() const { return false; }

    Interface3D* ui = nullptr;
    bool handled = false;            // set by a handler that used the event
};

// The extra input side whose handlers do nothing: the battle screen's, the briefing's, the main menu's and
// the view screens'.
struct UiInertInput final : UiInputSide {
    explicit UiInertInput(Interface3D* ui) : UiInputSide(ui) {}
};

// The window's part below its input side: the engine's transform object and 0x40 bytes the window's code
// never touches.
class UiWindowBase : public TransformObject {
protected:
};

// Base of the interface windows; its input side is its second base.
class UiWindow : public UiWindowBase, public UiInputSide {
public:
    explicit UiWindow(Interface3D* ui);
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

    UiSurface* AddSurface(UiSurface* surface);
    void ClearSurfaces();

    UiRegion* AddRegion();
    // Deletes `region` and drops it from the list (nothing when it is not listed).
    void RemoveRegion(UiRegion* region);
    void ClearRegions();

    UiModel* AddModel();

    UiInputSide* Input() { return this; }
    // Takes the mouse for the next button-up (what every button-down handler does).
    void CaptureMouse() { ui->mouseCapture = Input(); }
    // Button-up handlers act only when they took the mouse; they release it first.
    bool ReleaseMouse() {
        if (ui->mouseCapture != Input()) return false;
        ui->mouseCapture = nullptr;
        return true;
    }
    // A button's mouse handling (like ImGui's ButtonBehavior) over the window's first `count` regions:
    // HitRegion gives the region under the point, the event marked used, or null (a move, a wheel turn);
    // PressRegion also takes the mouse (a button down); PressedRegion is the release: null without the
    // mouse taken here, which it releases first.
    UiRegion* HitRegion(Point point, int count = INT_MAX);
    UiRegion* PressRegion(Point point, int count = INT_MAX);
    UiRegion* PressedRegion(Point point, int count = INT_MAX);
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
    bool enabled = true;
    Material* blackMaterial = nullptr;   // given to every sprite (AddSprite)
    Material* whiteMaterial = nullptr;
    std::vector<Texture*> objects;             // the referenced textures
    std::vector<UiMesh*> sceneObjects;
    std::vector<UiSurface*> surfaces;
    std::vector<UiRegion*> regions;
    std::vector<UiModel*> models;
};

// The tooltip attached to a region; its surface is made while it is shown.
struct UiTooltip : UiWindow {
    // The showing: the region sets Hovered (mouse inside) or Disappearing (left); OnMouseMove does the rest.
    enum class State { Idle, Hovered, Appearing, Disappearing, Shown };

    UiTooltip(Interface3D* ui, UiRegion* region, std::string_view text, int command);
    ~UiTooltip() override;
    // Empty: nothing. The size is not measured again.
    void SetText(std::string_view text);
    // The surface's paint: the background, the frame, the first line in the title colour, followed by
    // "  ( <key> )" with keyName, then the others.
    void PaintLines(UiSurface& surface) const;
    // Rebirth-patched (WideScreen). One step of the showing, mouse in layout coordinates;
    // Hovered waits for the delay (setting 20; 0: never shown), then makes the surface at the mouse, painted from
    // then on (Appearing), or goes back to Idle when the mouse has left the region; Appearing becomes Shown;
    // Disappearing waits while the timer is positive, then drops the surface (Idle).
    void OnMouseMove(Point point);
    void DrawSprites(SceneRenderer* pass) override;
    void DrawSurfaces() override;
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
};

// The panels of the item screens (camp, multiplayer hero screen, exchange): the description panel, hero
// equipment, quick strip, trade and repair grids, constructor pages and item strips, built on the blank
// panel. Their input sides share the left-button handler; ItemAt gives the item or help topic under the
// mouse.
class UiItemPanel : public UiWindow {
public:
    UiItemPanel(Interface3D* ui, ItemScreen* newOwner) : UiWindow(ui), owner(newOwner) {}
    // The item under the screen point (x, y), or null; a panel may set a help topic instead. The panels
    // without items of their own (description panel, statistics column, trade panel): nothing, `topic` 0.
    virtual Item* ItemAt(Point point, int* topic);

    // The item under (x, y) goes to the owner with mode; without one, a help topic the panel named goes to
    // the owner's help handler. The event is not marked used.
    void LeftDown(Point point) override;

    int mode = 0;                    // the action mode handed to the owner (ItemCell::mode)
    ItemScreen* owner = nullptr;
};

// The in-game key bindings' size (g_keyActions).
constexpr int kKeyActionCount = 0x200;

// --- Battle screen and its HUD panels ---
// The battle screen owns a row of panel windows and forwards mouse events to the panel whose rectangle
// holds the point. Coordinates are the 800x600 layout (Rebirth moves some panels).

class HudQuickItems;
class HudSpellColumn;
class HudActionColumn;
class HudWeaponBar;
class HudPortraits;
class HudUnitPanel;
class BattleScreen;
class UiScrollText;

// The depth buffer set to `z` over `count` screen rectangles (at most 8); all of it without rectangles
// (ui_container_draws.cpp).
void ClearUiDepth(float z, const Rect* rects = nullptr, int count = 0);

// Weapon bar (four 40x80 slots at (80..240, 520..600)).
class HudWeaponBar : public UiWindow {
public:
    void LeftDown(Point point) override;
    void LeftUp(Point point) override;     // selects a weapon
    void MouseMove(Point point) override;
    // Every frame: the highlight follows the unit's weapon.
    void Update() override;
    void DrawSprites(SceneRenderer* pass) override;
    // Rebirth-patched (WideScreen: the bar at the screen's left edge, from 80 - shift with
    // more regions up to x 240).
    HudWeaponBar(Interface3D* ui, BattleScreen* owner);
    // Weapon `slot` (its button or key), unless it is the one shown or not in use: highlighted, the weapon
    // sound, and the unit's weapon set.
    void Choose(int slot);
    // WideScreen: with the left panels' offset.
    void Highlight(int index, bool on);
    // The next slot's sprite: `image` copied into the bar's texture, the model `model` turned to face the
    // screen (a plain rectangle without one); shown not highlighted.
    void AddWeapon(std::string_view model, std::string_view image);
    // A sprite (Rebirth-patched) and a tooltip per weapon, then the unit's current weapon highlighted.
    void SetUnit(Unit* unit);

    BattleScreen* owner = nullptr;
    int count = 0;                   // slots in use
    int selected = 0;
    Unit* unit = nullptr;
    std::vector<std::string> labels;    // the tooltip's labels
};

// Quick items (2 rows of four 50x50 slots at (520..720, 500..600)).
class HudQuickItems : public UiWindow {
public:
    void LeftDown(Point point) override;
    void MouseMove(Point point) override;
    // A unit's quick item (bottom row) is picked or, with a double click, used at once; a player's item
    // (top row) starts mode 6, or cancels when it is the highlighted one.
    void LeftUp(Point point) override;
    void DrawSprites(SceneRenderer* pass) override;
    // Every frame: the tooltips of the slots that show an item.
    void Update() override;
    // The item's name and, for a wand (category 5) or a scroll (8) with a spell, its numbers after labels.
    std::string ItemTip(unsigned index);
    // The unit's quick item `slot` (a click on it or its key): a wand or scroll becomes the pending action
    // (3 on a unit for a spell with flag 0x10000000, else 4 at a point); the highlighted slot again, or an
    // empty one, drops it.
    void Pick(int slot);
    // The unit's wand or scroll in `slot` used at once (a double click, or the key handler's Ctrl / Alt), as
    // HudSpellColumn::ClickAlternate.
    void UseNow(int slot);
    // With `on` also selected = slot.
    void Highlight(int slot, bool on);
    // Every slot off; selected = -1.
    void ClearHighlights();
    // The entry shown in slot `slot` (a region's index; -1: empty, or no region).
    int RegionItem(int slot) const { return slot < 0 ? -1 : regionItem[slot]; }

    // Rebirth-patched (WideScreen: the panel at the screen's right edge).
    HudQuickItems(Interface3D* ui, BattleScreen* owner);
    // The next sprite (at most eight, else null): `image` copied into the panel's texture, the model `model`
    // turned over (a plain rectangle without one).
    UiMesh* AddItem(std::string_view model, std::string_view image);
    // The unit's four quick items in the bottom row (sprites Rebirth-patched); for the local player's unit
    // up to four of the player's ItemKind::Quest items in the top row.
    void SetUnit(Unit* unit);

    struct Entry {
        bool own = false;            // one of the unit's quick items (else the player's)
        int id = 0;                  // the item's id
        Item* item = nullptr;
        UiMesh* sprite = nullptr;
    };

    BattleScreen* owner = nullptr;
    int shownCount = 0;              // sprites added (the shown entries)
    int selected = 0;
    Unit* unit = nullptr;
    std::vector<Entry> entries;
    int regionItem[8] = {};          // per slot (region) its entry, or -1
    std::vector<std::string> labels;    // the tooltip's labels
};

// Spell column on the right (760..800 x 190..510).
class HudSpellColumn : public UiWindow {
public:
    void LeftDown(Point point) override;
    void LeftUp(Point point) override;
    void MouseMove(Point point) override;
    void DrawSprites(SceneRenderer* pass) override;
    // Spell `index` (its button or key) becomes the pending action: 1 on a unit for a spell with flag
    // 0x10000000, else 2 at a point; the highlighted one again drops it.
    void Click(int index);
    // A double click (or the key handler's Ctrl / Alt) casts at once: on the unit itself, or an attack spell
    // on the nearest unit it sees (none: the "nomagic" sound). The pending action is dropped after.
    void ClickAlternate(int index);
    // selected = index either way.
    void Highlight(int index, bool on);
    // selected = -1.
    void ClearHighlights();
    // Rebirth-patched (WideScreen: the column at the screen's right edge).
    HudSpellColumn(Interface3D* ui, BattleScreen* owner);
    // The next button at the bottom, the ones so far moved up a step; selected = its index.
    void AddSpell(std::string_view model, std::string_view image);
    // A sprite (Rebirth-patched) and a tooltip per spell slot that holds one.
    void SetUnit(Unit* unit);

    BattleScreen* owner = nullptr;
    int count = 0;                   // sprites added
    int selected = 0;
    Unit* unit = nullptr;
    std::vector<std::string> labels;    // the tooltip's labels
};

// Action column on the left (six 40x40 buttons at 0..40 x 270..510).
class HudActionColumn : public UiWindow {
public:
    void LeftDown(Point point) override;
    void LeftUp(Point point) override;
    void MouseMove(Point point) override;
    void DrawSprites(SceneRenderer* pass) override;
    // Action button `index` (a click or its key) becomes the pending action: 8 (an order on an object) for
    // action 0 (science), else 7 (follow); the highlighted one again drops it.
    void Select(int index);
    void Highlight(int index, bool on);
    void ClearHighlights();
    // Rebirth-patched (WideScreen: the column at the screen's left edge).
    HudActionColumn(Interface3D* ui, BattleScreen* owner);
    // The next button, above the ones so far; selected = its index.
    void AddAction(std::string_view model, std::string_view image, int action);
    // With `extra` first a "skill0011" button; with a unit the science button.
    void SetUnit(Unit* unit, bool extra);

    BattleScreen* owner = nullptr;
    int count = 0;                   // buttons added
    int selected = 0;
    Unit* unit = nullptr;
    std::vector<std::uint32_t> actions;            // the buttons' actions (0 science, -1 follow)
    std::vector<std::string> labels;    // the tooltip's labels
};

// A portrait's state, one per portrait: the last hit points seen, the face (rest orientation, current
// orientation and a random wobble), the face picture and model, and the slot's network player.
struct HudPortraitSlot {
    int lastHitPoints = 0;           // lowest hit points seen (a drop flashes the portrait)
    float flash = 0.0f;              // 1 after a hit, fading out
    Quaternion restA{1.0f, 0.0f, 0.0f, 0.0f};      // the face's rest orientation is restA * restB
    Quaternion current{1.0f, 0.0f, 0.0f, 0.0f};    // the face's orientation, eased towards the rest one
    Quaternion restB{1.0f, 0.0f, 0.0f, 0.0f};
    int wobbleKind = 0;              // 0 none, 1 nod (about x), 2 shake (y), 3 turn to target
    float wobbleTime = 0.0f;         // starts negative (a delay)
    std::string faceName;
    int faceState = 0;               // picture shown (0 normal, 2 hit, 3 low mana, ...)
    int faceMode = 0;                // 1 / 2: a timed picture, 0 none
    float faceTimer = 0.0f;
    std::string modelPrefix;        // the face model is prefix + build + suffix,
    std::string modelSuffix;        // build "th", "me" or "fa"
    float build = 0.0f;              // the unit's build the model was chosen for
    Quaternion wobbleTarget{1.0f, 0.0f, 0.0f, 0.0f};
    float wobbleSpeed = 0.0f;
    bool hasPlayer = false;          // a multiplayer player without a unit here (no bars)
    int playerId = 0;                // that player's serial, else -1
    // The portrait's sprites, in their drawing order: the face model (or picture), the health and mana bars,
    // the frame (shown while the unit is selected) and the hit flash.
    UiMesh* face = nullptr;
    UiMesh* healthBar = nullptr;
    UiMesh* manaBar = nullptr;
    UiMesh* frame = nullptr;
    UiMesh* hitFlash = nullptr;
};

// Party portraits (250..550 x 440..600).
class HudPortraits : public UiWindow {
public:
    void LeftDown(Point point) override;
    void LeftUp(Point point) override;     // selects the unit
    void MouseMove(Point point) override;   // shows the unit / sets the cursor
    // Every frame: the bars, the hit flash, the face picture and model, the wobble and the selection marker.
    void Update() override;
    // Face picture `state` (the face name plus "a" 1, "c" 2, "b" 3; the plain name without such an image)
    // into quarter index % 4 of texture objects[index / 4 + 1].
    void ShowFace(int index, int state);
    // One portrait per party unit and, in a multiplayer client, one per player without a live unit.
    void Rebuild(bool wide);
    // 56 wide (multiplayer: 40, or 56 with `wide` for the first). `hasPlayer` marks a player's portrait (half
    // tint, no bars).
    void AddPortrait(const std::string& model, std::string_view picture, bool wide, bool hasPlayer);
    // Portrait `index` starts wobble `kind` (1 nod, 2 shake) after a short random delay.
    void Wobble(int index, int kind);

    HudPortraits(Interface3D* ui, BattleScreen* owner);

    BattleScreen* owner = nullptr;
    int count = 0;
    std::vector<HudPortraitSlot> slots;    // one per portrait; region i is portrait i
    UiMesh* marker = nullptr;              // the camera target's marker, above its portrait
};

// A sprite's depth in front of the interface camera for the dialog pieces (frames, shades, buttons), the layout
// pixels one mesh unit spans (SetRectangle's `scale`) and the side of a whole interface texture in texels.
inline constexpr float kDialogDepth = 3.0f;
inline constexpr float kSpriteScale = 100.0f;
inline constexpr int kUiTextureSize = 0x100;

// The figure factory's class 0x43 ("CI3DFigure"): a plain figure mesh with the sprite's fields.
class UiMesh : public FigureMesh {
public:
    UiMesh();
    ~UiMesh() override;
    // The sprite's start: unit scale and tint, the identity orientation at depth 10, opaque black, visible.
    void Create() override;
    // The material and the texture forgotten (not released), then the figure's own Destroy.
    void Destroy() override;
    const char* ClassName() override;
    ClassId GetClassId() override;

    // The quads get z 0 and w 1, the vertices a zero normal index.
    void SetQuadCount(int count);
    void SetVertexCount(int count);
    void SetIndexCount(int count);
    void SetUvCount(int count);
    // The sprite fills the figure mesh's arrays itself: vertices (a quad is one VertexPacket, its four
    // corners x, y, z, w), corners (MeshCorner per vertex), texCoords, indices.
    // Where Place put the sprite (the layout point and the depth; each stored as float).
    struct Placement {
        float x = 0.0f, y = 0.0f, depth = 0.0f;
    };
    Placement GetPlacement() const;
    // The shapes, Rebirth-patched: with Rebirth's WideScreen every shape's layout x is moved
    // by the panel offset. A rectangle: texels (u0, v0)-(u1, v1), v upwards, both u one less when u0 > u1,
    // half a texel inwards; position the layout centre at `depth`.
    void SetRectangle(float depth, float scale, float left, float top, float right, float bottom, int u0, int v0,
                             int u1, int v1);
    void SetRectangle(float depth, float scale, const Rect& rect, int u0, int v0, int u1, int v1) {
        SetRectangle(depth, scale, static_cast<float>(rect.left), static_cast<float>(rect.top),
                     static_cast<float>(rect.right), static_cast<float>(rect.bottom), u0, v0, u1, v1);
    }
    // A grid of columns x rows cells, each showing the whole picture; position the top left corner.
    void SetGrid(float depth, float scale, float left, float top, float right, float bottom, int u0, int v0, int u1,
                        int v1, int columns, int rows);
    // A frame `border` wide inside the rectangle, each side showing the whole picture.
    void SetFrame(float depth, float scale, float left, float top, float right, float bottom, float border, int u0,
                         int v0, int u1, int v1);
    void SetFrame(float depth, float scale, const Rect& rect, float border, int u0, int v0, int u1, int v1) {
        SetFrame(depth, scale, static_cast<float>(rect.left), static_cast<float>(rect.top),
                 static_cast<float>(rect.right), static_cast<float>(rect.bottom), border, u0, v0, u1, v1);
    }
    // The first layout point is position, the other two relative to it in units of `scale`.
    void SetTriangle(float depth, float scale, float x0, float y0, float x1, float y1, float x2, float y2, float u0,
                            float v0, float u1, float v1, float u2, float v2);
    // The shape builders (ui_mesh.cpp; docs/notes/ui.md, "Sprite shapes"): each fills the sprite from
    // scratch (quad positions in the sprite's own units, layout pixels / scale, the vertex records, the
    // triangle indices, texels / 256 nudged by half a texel, the placement) and hands the arrays to the renderer.
    // A framed rectangle: an outer quad (left, top)-(right, bottom) and an inner one `border` pixels in,
    // 8 vertices and 24 indices forming the frame between them; texels (u0, v0)-(u1, v1) for the outer
    // edge, moved in proportionally for the inner one.
    void Framed(float depth, float scale, float left, float top, float right, float bottom, float border, int u0,
                int v0, int u1, int v1);
    // A disc of `count` points around the layout point (x, y), radius `radius`; texels on the circle of
    // radius `uvRadius` around (u, v); a fan of count - 2 triangles.
    void Disc(float depth, float scale, float x, float y, float radius, int count, int u, int v, int uvRadius);
    // A ring of `count` segments around (x, y) between `radius - width` and `radius`: 4 vertices and 6
    // indices per segment (the last segment closes on vertices 0 and 1), count / 2 quads; texels
    // (u0, v0)-(u1, v1) for every segment.
    void Ring(float depth, float scale, float x, float y, float radius, int count, float width, int u0, int v0,
              int u1, int v1);
    // Only the per-vertex arrays: one block (four 0s, four 0s, four 1s, four 1s) and two with four entries
    // (the builders' common start).
    void ResetColours();
    // Puts the sprite at layout point (x, y) at `depth` in front of the interface camera (`depth` copied as
    // bits). Rebirth-patched: with Rebirth's WideScreen x is moved by the panel offset first.
    void Place(float x, float y, float depth);
    // Copied as bits.
    void SetScale(float scale);
    void SetScaleYBits(std::uint32_t bits);
    // When visible.
    void Draw(SceneRenderer* renderer);
    // Where Draw puts the sprite: its orientation's rotation with its position as column 3.
    Matrix4 PlacementMatrix() const;
    // The depth (z / w) of the sprite's nearest drawn face at screen point `at` as Draw would draw it after `camera`
    // (the pipeline's screen stack's top before the draw), Direct3D 7's pixel centres at whole coordinates; none
    // where no face is (hidden or not visible: none).
    std::optional<float> DepthAt(const Matrix4& camera, Point at) const;

    // FigureMesh's fields as a sprite uses them: FigureMeshFlag::Flat draws in flatColour (ARGB),
    // flatSpecular the HUD buttons' highlight (0xFF404040 / 0xFF000000), textureOffsetU / V_ a frame of the
    // sprite sheet, localRotation the HUD face models' turn.
    bool visible = false;
    float tint[3] = {};              // colour factors (the HUD: 1 selected, 0.5 not)
    // Applied after the placement; the diagonal is SetScale's scale.
    Matrix4 scaleMatrix;
    float& ScaleX() { return scaleMatrix.m[0]; }
    float& ScaleY() { return scaleMatrix.m[5]; }
    float& ScaleZ() { return scaleMatrix.m[10]; }
    Material* material = nullptr;   // the window's black material
    Texture* texture = nullptr;     // one of the window's referenced textures
};

// A side panel's slide (the map's, the unit panel's, the top panel's): the sling button or its key flips `direction` and
// starts it, every frame's Step moves `t` by the frame's time and stops it at either end.
struct PanelSlide {
    // `t` moves by the frame's time * direction / 0.3 and stays within 0..1; `moving` ends at either end.
    void Step();
    // The panel's offset in pixels: `travel` eased (2 t^2 up to half way, 1 - 2 (1 - t)^2 after), truncated.
    int Offset() const;
    // The sling button (or the map's key): the sling sound, then the slide turned round and started.
    void Toggle(Interface3D* ui);

    float t = 0.0f;                  // 0..1: how far the panel has slid
    float direction = 0.0f;          // +1 / -1
    int travel = 0;                  // full slide distance in pixels
    bool moving = false;             // set when the sling button (or its key) starts the slide
};

// Map (615..800 x 0..165).
class HudMap : public UiWindow {
public:
    HudMap(Interface3D* ui, BattleScreen* owner);
    void LeftDown(Point point) override;   // the view buttons
    // Rebirth-patched (WideScreen: the map's centre at 700 + shift). A held view button is
    // let go; otherwise the sling button starts the slide, the round map and the map's square focus the
    // camera on the map point (inside the world), the north button is FaceNorth.
    void LeftUp(Point point) override;
    void MouseMove(Point point) override;
    // Over the sling button the wheel zooms the map by 2.5^(notches / 10).
    void Wheel(float notches, Point point) override;
    // "Camera north" (its button and key): the click sound, the camera turned north, northTime 0.5.
    void FaceNorth();
    // The view buttons' icons lit by `view`.
    void ShowView();
    // Every frame: the markers mesh of the units, marked objects, zone exits and players.
    void BuildMarkers();
    // Every frame, before BuildMarkers: follows the camera and builds the visible-area mesh clipped to the
    // world, and the world-bounds quad.
    void UpdateView();
    // Rebirth-patched (WideScreen). The parts at the current slide offset.
    void Layout();
    // Rebirth-patched (WideScreen). Every frame: the zoom (2.5 times a second while a view
    // button is held), the view and markers, the slide (0.3 s for the whole way, eased) and the "camera
    // north" time.
    void Update() override;
    // Rebirth-patched (WideScreen: the panel at the screen's right edge). The world's extent
    // and picture come from the local player's zone record (32 by 32 and "battle00" without one); `slid`
    // starts the slide at its far end.
    void Setup(float zoom, bool slid);

    BattleScreen* owner = nullptr;
    Vector2 centre;                  // world point at the map's centre (the camera's)
    Vector2 facing;                  // view direction (normalised by the camera follow)
    float worldWidth = 0.0f;         // the zone's extent (clipping rectangle 0..width)
    float worldHeight = 0.0f;
    float textureSize = 0.0f;        // world units per map texture (texture coordinates)
    float scale = 0.0f;              // world units to map units
    float zoom = 0.0f;               // 1 .. zoomMax (the view buttons change it)
    float zoomMax = 0.0f;
    float halfSide = 0.0f;           // 80: half the map's side in layout units (a click's offset over it)
    std::vector<Vector2> viewPolygon;        // the round map's outline in map units (the disc's corners)
    std::vector<Vector2> visibleArea;        // the outline in world units, clipped to the world
    PanelSlide slide;                // the offset is to the right
    int view = 0;                   // 1 or 2 while a view button is held (zoom in, zoom out), else 0
    float northTime = 0.0f;          // 0.5 when "camera north" turns the camera, then counts down

    // The regions, in their hit-test order (the whole panel's last, unnamed).
    UiRegion* slingButton = nullptr;
    UiRegion* roundMap = nullptr;          // the round map (zoomed)
    UiRegion* mapSquare = nullptr;         // the square around it
    UiRegion* viewButtons[2] = {};         // zoom in, zoom out
    UiRegion* northButton = nullptr;
    // The sprites, in their drawing order.
    UiMesh* bounds = nullptr;              // the world's bounds, in the map's picture
    UiMesh* visibleDisc = nullptr;         // the visible area's disc, in the picture too
    UiMesh* markers = nullptr;             // four texels of one colour each
    UiMesh* mapBack = nullptr;             // the map's and the buttons' plain backgrounds
    UiMesh* buttonsBack = nullptr;
    UiMesh* slingArrow = nullptr;
    UiMesh* viewIcons[2] = {};
    UiMesh* ring = nullptr;                // round the map
    UiMesh* frame = nullptr;
    UiMesh* needle = nullptr;              // the compass needle
    UiMesh* northIcon = nullptr;
};

// Unit panel (0..185 x 0..225).
class HudUnitPanel : public UiWindow {
public:
    HudUnitPanel(Interface3D* ui, BattleScreen* owner);
    // The unit the panel shows: the hover unit, else the selected one, while the panel is not slid away and the
    // unit's model is attached; else null.
    Unit* PanelUnit() const;
    // The panel surface's paint: PanelUnit's name and the texts of its page (bodyPart): the hit points and mana,
    // the parts' names, the info lines or the effects.
    void PaintTexts(UiSurface& surface);
    // The info page's lines for `unit`: hit points, mana, speed, vision, attack, defence, damage, rate,
    // protection and, for a unit outside the party, its level against the leader's.
    void PaintUnitInfo(UiSurface& surface, Unit* unit);
    // The hit points' and, for a party unit, the mana's bar; PaintHealth their numbers.
    void ShowHealth(Unit* unit);
    void PaintHealth(UiSurface& surface, Unit* unit);
    // The durability bar of each present part in state 3; PaintParts the parts' names.
    void ShowParts(Unit* unit);
    void PaintParts(UiSurface& surface, Unit* unit);
    void LeftDown(Point point) override;
    void LeftUp(Point point) override;
    void MouseMove(Point point) override;
    void Wheel(float notches, Point point) override;
    // The body part buttons lit by bodyPart.
    void ShowBodyPart();
    // The pictures of the first six effects on `unit`; the "more" mark shows when there are more. PaintEffects
    // their names and times left.
    void ShowEffects(Unit* unit);
    void PaintEffects(UiSurface& surface, Unit* unit);
    // Rebirth-patched (WideScreen). The parts at the current slide offset.
    void Layout();
    // Rebirth-patched (WideScreen). Every frame: the slide (as the map's); a dead hover unit
    // is forgotten.
    void Update() override;
    // Rebirth-patched (WideScreen: the panel at the screen's left edge, its rectangle moved with it; addon's own
    // rectangle and "no slide" are not kept, ui_hud_slide.cpp). `slid` starts the panel at the far end of its slide.
    void Setup(int bodyPart, bool slid);
    // Rebirth-patched (WideScreen). The hover unit, else the selected one, while the panel
    // is not slid away: its model (not on info page 2) over a depth buffer cleared to 0 around the panel and
    // its bars (the texts are PaintTexts').
    void DrawSprites(SceneRenderer* pass) override;

    BattleScreen* owner = nullptr;
    Unit* shownUnit = nullptr;      // the first selected unit (RefreshSelection), or null
    Unit* hoverUnit = nullptr;      // the unit under the mouse (HoverUnit), or null
    // The model's view for the selected unit [0] and the hover unit [1], from the units' prototypes (the
    // battle screen copies them as dwords).
    float viewHeight[2] = {};        // how far up the panel the model stands, in units of its size
    float viewSize[2] = {};          // the model's size: it is drawn 1 / size (times its animation's ratio)
    std::string strings[2];         // the selected and the hover unit's names
    PanelSlide slide;                // the offset is to the left
    int bodyPart = 0;                // the selected body part button 0-3: the page shown
    int effectIcons[6] = {};         // the spell pictures in the icon texture's six places
    std::vector<std::string> texts; // "infounit_0".."infounit_17": the info lines' labels
    std::vector<std::string> partLabels;    // four: the shown names of "skull", "torso", "leg", "arm"

    // The regions, in their hit-test order (the whole panel's last, unnamed).
    UiRegion* slingButton = nullptr;
    UiRegion* partButtons[4] = {};
    // The sprites, in their drawing order.
    UiMesh* background = nullptr;          // drawn alone, before the model
    UiMesh* edge = nullptr;
    UiMesh* slingArrow = nullptr;
    UiMesh* partIcons[4] = {};
    UiMesh* frame = nullptr;
    UiMesh* heightBar = nullptr;           // a strip of twenty cells down the edge
    UiMesh* moreMark = nullptr;            // more effects than places
    UiMesh* healthBar = nullptr;           // each bar over its frame
    UiMesh* healthFrame = nullptr;
    UiMesh* manaBar = nullptr;
    UiMesh* manaFrame = nullptr;
    UiMesh* bodyBars[6] = {};              // the six body parts' bars, each over its frame
    UiMesh* bodyFrames[6] = {};
    UiMesh* effectSprites[6] = {};         // the effects' pictures, from the panel's icon texture
};

// Game clock (720..800 x 520..600).
class HudClock : public UiWindow {
public:
    explicit HudClock(Interface3D* ui) : UiWindow(ui) {}
    void LeftDown(Point point) override;
    // Rebirth's MPSpeedControl behind its flag. The pause button toggles the pause; the normal and fast
    // buttons set the speed (not on a network client: update period 55 / 27 ms); the objectives button opens
    // the objectives.
    void LeftUp(Point point) override;
    void MouseMove(Point point) override;
    // Every frame: the blink and the dial step on (the dial only while the game runs), then Show.
    void Update() override;
    // The face turned to the time of day, the hand and the dial's marks placed; the face and the hand blink while
    // paused.
    void Show();
    // The objectives window of the current zone (one button, no exits, no zone list).
    void OpenObjectives();
    // The pause button and its key: the clock sound, then the server flag 10 flipped (pause / resume).
    void TogglePause();
    // The normal / fast speed (button and keys): the update period 55 / 27 ms and the clock sound.
    void SetSpeed(bool fast);
    // Rebirth-patched (WideScreen: the clock at the screen's right edge).
    void Setup(bool fast);

    float blink = 0.0f;              // 0..1, the pause blink phase
    float dial = 0.0f;               // speed dial angle (wraps at pi / 6)
    bool fast = false;               // fast game speed (copied to Interface3D::fastGame)
    bool highlight = false;          // the objectives button pulses

    // The regions: the pause, normal and fast sectors and the objectives' quarter disc.
    UiRegion* pauseButton = nullptr;
    UiRegion* normalButton = nullptr;
    UiRegion* fastButton = nullptr;
    UiRegion* objectivesButton = nullptr;
    // The sprites, in their drawing order.
    UiMesh* face = nullptr;               // turned to the time of day
    UiMesh* hand = nullptr;
    UiMesh* dialMarks[4] = {};
    UiMesh* objectivesIcon = nullptr;
    UiMesh* rim = nullptr;
};

// Movement panel (0..80 x 520..600): the flag button toggles the units' flag, the four mode buttons set their
// movement mode 0-3.
class HudMovePanel : public UiWindow {
public:
    HudMovePanel(Interface3D* ui, BattleScreen* newOwner) : UiWindow(ui), owner(newOwner) {}
    void LeftDown(Point point) override;
    void LeftUp(Point point) override;
    void MouseMove(Point point) override;
    // Every frame: the selected units' common mode and flag.
    void Update() override;
    // The flag button (and its key): the selected party units' flag becomes 0 when all have 1, else 1.
    void ToggleFlag();
    // WideScreen (the left panels' offset). A negative mode hides the marker.
    void ShowMode(int mode);
    // A mode button and its keys: the click sound and the mode 0-3 for the selected party units (nothing
    // without the local player's view), then the marker moved there.
    void SetMode(int mode);
    // Rebirth-patched (WideScreen: the panel at the screen's left edge).
    void Build() override;

    BattleScreen* owner = nullptr;
    int mode = 0;                    // the shown movement mode (-1, -2: none, mixed)
    int flag = 0;                    // the selected units' common flag (-1, -2: none, mixed)

    // The regions: the flag's quarter disc in the corner, then the four modes' sectors.
    UiRegion* flagButton = nullptr;
    UiRegion* modeButtons[4] = {};
    // The sprites, in their drawing order.
    UiMesh* marker = nullptr;              // the hand pointing at the mode's button
    UiMesh* modeIcons[4] = {};
    UiMesh* flagIcon = nullptr;            // shows the flag through its texture offset
};

// Top panel (180..620 x 0..100).
class HudTopPanel : public UiWindow {
public:
    HudTopPanel(Interface3D* ui, BattleScreen* newOwner) : UiWindow(ui), owner(newOwner) {}
    void LeftDown(Point point) override;
    void LeftUp(Point point) override;
    void MouseMove(Point point) override;
    void Wheel(float notches, Point point) override;
    // Page `page` into the message log (page 0 scrolled to the end).
    void ShowPage();
    // Every frame: the slide.
    void Update() override;
    // The parts, the message log and the regions at the current slide offset.
    void Layout();
    // Page 1's text: the party's money and the local player's items merged by kind, one per line.
    void ShowItems();
    // Slid in when `shown`.
    void Setup(int page, bool shown);
    // Appends "<combat_picked_up or, when `crippled`, combat_crippled> <item name>" and a newline to page 0's
    // text, and to the message log when page 0 is up.
    void AddPickedUp(Item* item, bool crippled);
    // The same with the money ("format_money"), for money > 0.
    void AddMoney(int money);

    BattleScreen* owner = nullptr;
    PanelSlide slide;                // t 0 (out) .. 1 (in)
    int page = 0;
    std::string pageTexts[2];       // the message log's text of pages 0 and 1

    // The regions, in their hit-test order (the whole panel's last, unnamed).
    UiRegion* slingButton = nullptr;
    UiRegion* pageButtons[2] = {};
    // The sprites, in their drawing order.
    UiMesh* back = nullptr;
    UiMesh* bar = nullptr;                 // the strip along the top
    UiMesh* slingArrow = nullptr;
    UiMesh* pageIcons[2] = {};
    UiMesh* frame = nullptr;
};

// --- Screens with child windows ---

// Base of the screens that hold child windows and extra input sides (the Esc menu, the save/load and
// options windows, the battle screen). An event goes to the extra input sides in order, then to the enabled
// children (mouse events only to those holding the point), until one sets its handled flag; see
// docs/notes/ui.md, "Container input dispatch".
class UiContainer : public UiWindow {
public:
    explicit UiContainer(Interface3D* ui) : UiWindow(ui) {}
    ~UiContainer() override;

    // Only while the interface draws: the window's own draw, then every enabled child's.
    void DrawSprites(SceneRenderer* pass) override;
    // Only while the interface draws: the window's own, then every enabled child's.
    void DrawSurfaces() override;
    void DrawTips() override;
    void Update() override;
    // The screen as the briefing, null for any other.
    virtual BriefingScreen* AsBriefing() { return nullptr; }
    // The screens' virtuals the interface calls (Interface3D); every window put on the stack has its own, the others' defaults
    // do nothing.
    virtual void CreateScreen() = 0;
    virtual void CloseScreen() = 0;
    virtual void MessageResult(std::uint32_t) {}                                 // the value a window above left pending when it closed
    virtual void ItemsPicked(const std::vector<Item*>&, int, bool) {}   // items picked up (items, money, crippled)
    virtual void UnitJoined(Unit*) {}
    virtual void UnitLeft(Unit*) {}
    virtual void GameOver(bool) {}
    virtual void Speech(int, AckKind, char*) {}                        // a unit speaks
    virtual void QuestVariable(std::string_view, float) {}                  // a quest variable changed
    virtual void ShowBriefing(std::string_view) {}                          // a "briefing <name>" string event
    virtual bool ReadyToStopPolling() { return false; }                // whether the interface may stop asking (pollTop)
    virtual void PlayersChanged(int) {}
    virtual void ExchangeDone(Party*, Party*, Party*) {}            // an exchange ended
    virtual void PlayerList(int) {}                                    // the player list for kicking (1) or banning (2)
    virtual void DebugFill() {}                                        // the console's "give <player> assortment"
    virtual void SaveState() {}                                        // the window's part of a save game
    virtual const char* HelpTopic() { return nullptr; }
    // The screen took another size (Interface3D::Resize), after every surface was placed again: what of the
    // window was built for the window's old shape is built again (the battle screen's panels at the edges, the
    // camp's curtains over the margins). Most windows have nothing of that.
    virtual void LayoutChanged() {}

    void Deactivate() override;                                        // every side and child, no flags
    void LeftDown(Point point) override;
    void LeftUp(Point point) override;
    void MouseMove(Point point) override;
    void RightDown(Point point) override;
    void RightUp(Point point) override;
    void LeftDouble(Point point) override;
    void RightDouble(Point point) override;
    void KeyDown(platform::Key key, int scan, bool repeat) override;
    void Char(int key, int scan) override;
    void KeyUp(platform::Key key, int scan) override;
    void Wheel(float notches, Point point) override;

    // Seconds since the last frame (milliseconds * 0.001f).
    static float FrameTime();
    // The last frame's whole milliseconds (the ticks the held buttons' repeats count).
    static int FrameTicks();
    // Null is ignored.
    void AddChild(UiWindow* child);
    void AddInput(UiInputSide* input);

    std::vector<UiWindow*> children;
    std::vector<UiInputSide*> inputs;         // extra input sides
    // A child that takes every input event while it is set, wherever the mouse is (an open drop-down list).
    UiWindow* popup = nullptr;
};

// --- Battle screen ---

// What the next click on the view does (BattleScreen::pendingAction, SelectUnit).
enum PendingAction : int {
    kPendingNone = 0,           // select, attack or move
    kPendingSpellOnUnit = 1,    // the pending spell
    kPendingSpellAtPoint = 2,
    kPendingQuickOnUnit = 3,    // the pending quick item
    kPendingQuickAtPoint = 4,
    kPendingItemOnObject = 5,   // an item (pendingItem) used on an object
    kPendingKeyOnObject = 6,    // one of the player's items (its key in itemUseKey) used on an object
    kPendingFollow = 7,         // the action buttons
    kPendingOrder = 8,          // an order on an object (the science button)
};

// The in-game screen (CreateScreen id 1); its Create builds the HUD panels as children in order: weapon
// bar, quick items, spell column, action column, portraits, unit panel, map, clock, movement panel, message
// log, top panel. See docs/notes/ui.md, section 6.
class BattleScreen : public UiContainer {
public:
    // A click on `unit` (or, without one, at screen point x, y): with party members selected, by the pending
    // action (0 select / attack / move, 1 spell on a unit, 2 and 4 spell on a point, 3 quick item, 5 item
    // and 6 spell on an object, 7 follow, 8 order on an object) or the modifier keys; with nobody selected
    // it selects the clicked member. Rebirth's FixSpellSelfCasting #1 drops the Ctrl flag of a spell on a
    // unit.
    void SelectUnit(Unit* unit, Point at);
    // The unit panel's hover unit, and with party members selected the cursor for what a click would do.
    void HoverUnit(Unit* unit, Point at);
    // Orders the selected party units to the point (`goTo`: with a modifier, else a walk); a point on a zone
    // exit whose "z.<zone>" variable is not 1 is remembered (exitPending).
    void OrderAt(Vector3 point, bool goTo, bool run);
    // The pending action becomes `mode` on `slot` with `item`, the panels' highlights move to `slot` in the
    // panel of that mode. kPendingNone with slot and item -1 cancels.
    void SetPendingAction(PendingAction mode, int slot, int item);

    // Reads the screen's saved state when a game is being loaded, else remembers the local player's first
    // hero.
    void CreateScreen() override;
    // The panels built again for the window's new shape (they sit at its edges), showing what they showed: the
    // party's portraits, the selection, the pending action, the map's zoom and the message log.
    void LayoutChanged() override;
    // The HUD panels (the children, in their order) for the window's shape, the map at `mapZoom`.
    void BuildPanels(float mapZoom);
    // Rebirth's AddNewConsoleCommands applies "nogui" first.
    void DrawSprites(SceneRenderer* pass) override;
    // Every frame: a dead selected unit empties the panels, a multiplayer client rebuilds the party, the
    // "leave_zone" box for a pending exit, the selection box's marks, the quest sound.
    void Update() override;
    // A click on the unit under the point, or a drag selecting the party units inside the box. Details:
    // docs/notes/ui.md, "Behaviour moved out of header comments".
    void LeftUp(Point point) override;
    // Also every frame from Draw. Details: docs/notes/ui.md, "Behaviour moved out of header comments".
    void MouseMove(Point point) override;
    // While a portrait click picks a player the portraits get it; else, when no panel took it, a press on
    // the view starts.
    void LeftDown(Point point) override;
    void LeftDouble(Point point) override;
    void Deactivate() override;
    // Rebirth's MPSpeedControl behind its flag. While a portrait click picks a player, Esc ends that; on a
    // network client Enter / Backspace go to the chat line. Otherwise Esc drops the pending action, else
    // opens the escape menu; other keys run their action (docs/notes/ui.md, "Behaviour moved out of header
    // comments").
    void KeyDown(platform::Key key, int scan, bool repeat) override;
    // Releases the order key of the key's action.
    void KeyUp(platform::Key key, int scan) override;
    // Offered to the map, the unit panel, the message log and the top panel in that order; when none takes
    // it, the container passes it on with those four disabled.
    void Wheel(float notches, Point point) override;
    // Its GetName when not empty; for a unit whose id is a text key, its "pers" text; otherwise the first
    // line of the text "unit " + the prototype's name with spaces as '_'.
    std::string UnitName(Unit* unit);
    // A "q." player variable changed: a quest line to the top panel's text and the message log; the clock
    // pulses. Details: docs/notes/ui.md, "Behaviour moved out of header comments".
    void QuestVariable(std::string_view name, float value) override;
    // With exactly one living party unit selected the weapon bar, quick items, spell and action columns show
    // it, otherwise they are emptied; the pending action and highlights are cleared. Details:
    // docs/notes/ui.md, "Behaviour moved out of header comments".
    void RefreshSelection();
    // Nothing on a network client or past the party. With the modifier key the member flips, else it alone
    // is chosen. Details: docs/notes/ui.md, "Behaviour moved out of header comments".
    void ToggleMember(int index);
    // The party indexes of the selected members (flag 0x10), in order.
    std::vector<int> SelectedIndexes() const;
    // The order kind is the first order key held (6 for none); a member more than five levels below the
    // target speaks (speech event 10).
    void AttackUnit(Unit* target, bool ctrl);
    // The same for a dead `target` (no kind, no levels).
    void OrderDeadUnit(Unit* target, bool ctrl);
    // The pending spell (or with `quick` the quick item) used on `target` by each selected member; the
    // pending action is dropped after.
    void UseOnUnit(Unit* target, bool quick, bool ctrl);
    // The same at the point. For the member that is `unit` itself the point first moves 0.1 behind it, for
    // it and the members after it.
    void UseAtPoint(Vector3 point, Unit* unit, bool quick);
    // The unit's speech line goes to the top panel's text and the message log; the kind Kill shows its timed
    // face, the kinds NoWayToAttack..Injured shake its portrait.
    void Speech(int id, AckKind kind, char* extra) override;

    void CloseScreen() override;
    // Polled every frame by the interface until it answers true. After a new game (hasHero): the camera
    // follows the first party unit and the autosave "savea" when the option is on.
    bool ReadyToStopPolling() override;
    void ItemsPicked(const std::vector<Item*>& items, int money, bool crippled) override;
    // A network client instead empties the top panel's page-0 text.
    void UnitLeft(Unit* unit) override;
    // The count and ids of the selected party units, the map's zoom, the server flag (a byte), the local
    // player's first value, the top panel's page-0 text with its zero, exitPending (a byte), pendingExit.
    void SaveState() override;
    // With a record of the local player for it, the unit goes after the party units whose records come
    // first and the record's selection byte decides; otherwise it is appended, not selected. A unit among
    // heroes (the save's selection) is taken out and selected. A network client instead empties the top
    // panel's page-0 text.
    void UnitJoined(Unit* unit) override;
    // Also turns the face's rest orientation about -x by kHudQuarterTurn / 7.
    void MarkPortrait(int index);
    // 0x10001 (the "leave_zone" question) leaves through pendingExit; 0x20001 (game over: load) opens the
    // load window; 0x20002, 0x90001 and 0x100001 leave the game.
    void MessageResult(std::uint32_t result) override;
    // Lost (`won` 0): the box "game_over" (result 0x20000); won: "end_of_game" (0xFFFF0000).
    void GameOver(bool won) override;
    // A portrait click picks a player while it is not 0.
    void PlayerList(int mode) override { portraitMode = mode; }
    explicit BattleScreen(Interface3D* ui);

    struct PartyIds {
        // Both id vectors hold the same ids in the same order (the camera's id is not compared).
        bool Equals(const PartyIds& other) const;

        std::vector<int> withUnit;
        int camera = 0;
        std::vector<int> without;
    };

    std::vector<Unit*> party;       // shown as portraits
    // The pending action: what the next click on the view does (SelectUnit), its spell, quick item
    // or action button, and the player's item id (mode 6) or the action (7, 8).
    PendingAction pendingAction = {};
    int pendingSlot = 0;
    int pendingItem = 0;
    // A zone exit OrderAt found under the clicked point; the update asks about leaving while exitPending is
    // set. Saved with the game (version 0x62).
    int pendingExit = 0;
    bool exitPending = false;
    // Key actions 0x11..0x16 held; AttackUnit's order kind is the first one set.
    std::uint8_t orderKeys[6] = {};
    HudWeaponBar* weaponBar = nullptr;
    HudQuickItems* quickItems = nullptr;
    HudSpellColumn* spellColumn = nullptr;
    HudActionColumn* actionColumn = nullptr;
    HudPortraits* portraits = nullptr;
    HudUnitPanel* unitPanel = nullptr;
    HudMap* map = nullptr;
    HudClock* clock = nullptr;
    HudMovePanel* movePanel = nullptr;
    UiScrollText* messageLog = nullptr;
    HudTopPanel* topPanel = nullptr;
    bool viewPressed = false;        // the left button went down on the view
    bool boxSelecting = false;       // a selection box is being dragged
    Point press;                     // where the button went down on the view (screen)
    Rect selectionBox;               // the box
    std::vector<std::uint32_t> heroes;             // ids of the heroes shown (saved with the game)
    std::map<int, std::string> personTexts;       // the "pers" texts by person
    int questSound = 0;              // the quest sound to play (1 quest, 2 part done, 3 done)
    bool hasHero = false;            // a hero id was added (new game)
    PartyIds clientParty;            // the client's party ids last built
    // The key a player's item gives when it starts mode 6; SelectUnit and the hover hand it to the target's
    // AcceptsKey.
    LockKey itemUseKey = {1, 0, 0};
    int portraitMode = 0;            // 1, 2: the next portrait click picks a player for a server command (4, 5)
};

// A scroll bar (the battle screen's message log derives from it).
class UiScrollbar : public UiWindow {
public:
    int Position() const { return position; }
    // Whether the position moved since the last call (the flag cleared).
    bool TakeMoved();
    // The position, the moved flag cleared unread.
    int TakePosition();
    // `speed` in lines per second; position 0.
    UiScrollbar(Interface3D* ui, Rect rect, float depth, int maximum, float speed);
    ~UiScrollbar() override;
    // Scrolls while an arrow is held: whole steps of the accumulated amount move the position.
    void Update() override;
    void LeftDown(Point point) override;   // thumb drag or an arrow
    void LeftUp(Point point) override;
    // Drags the thumb (state 1). The event is used either way.
    void MouseMove(Point point) override;
    // Over the track: delta (low 16 bits) * speed / 120 into the accumulated amount.
    void Wheel(float notches, Point point) override;
    // Clamped to 0..maximum, moved set.
    virtual void SetPosition(int position);
    // The track is the right 10 pixels less 26 at either end. The position is set again (one below first,
    // so it counts as a change).
    virtual void SetRect(const Rect* rect);
    // Negative: 0. The sprites show only for a maximum above 0; the position is set again as SetRect does.
    void SetMaximum(int maximum);
    int maxPosition = 0;
    float speed = 0.0f;              // lines per second while an arrow is held (/16)
    int position = 0;
    // Layout coordinates (the options slider: left + 26 .. right - 26 over the bottom 10 pixels).
    Rect track;
    Rect thumb;                      // layout coordinates
    int grabX = 0;                   // mouse offset from the thumb centre while dragging
    int grabY = 0;
    float depth = 0.0f;              // the sprites' depth
    bool moved = false;
    int state = 0;                   // 0 idle, 1 dragging the thumb, 2 up, 3 down
    float accumulated = 0.0f;        // scrolled amount not yet applied

    // The regions, in their creation order: the arrows (on the slider: left, right) and the track.
    UiRegion* upButton = nullptr;
    UiRegion* downButton = nullptr;
    UiRegion* trackArea = nullptr;
    // The sprites, in their drawing order (the slider turns them a quarter: its up arrow on the right).
    UiMesh* trackSprite = nullptr;
    UiMesh* thumbSprite = nullptr;
    UiMesh* upArrow = nullptr;
    UiMesh* downArrow = nullptr;
};

// The options window's horizontal slider, laid out along the bottom of its rectangle.
class UiSlider : public UiScrollbar {
public:
    UiSlider(Interface3D* ui, Rect rect, float depth, int maximum, float speed)
        : UiScrollbar(ui, rect, depth, maximum, speed) {}
    // Along the track's width; the thumb 28 wide and 10 high.
    void SetPosition(int position) override;
    // The scroll bar's drag along x.
    void MouseMove(Point point) override;
    void SetRect(const Rect* rect) override;
};

// The pieces of a scrolling text: a format change (type 1) or a word (type 2).
struct UiFormatToken;
struct UiWordToken;
struct UiTextToken {
    explicit UiTextToken(int newType) : type(newType) {}
    virtual ~UiTextToken();
    // The token as the class its type makes, null for the other one.
    virtual UiFormatToken* AsFormat() { return nullptr; }
    virtual const UiFormatToken* AsFormat() const { return nullptr; }
    virtual UiWordToken* AsWord() { return nullptr; }

    int type = 0;                    // 1 format, 2 word
};
struct UiFormatToken : UiTextToken {
    explicit UiFormatToken(int font);
    ~UiFormatToken() override;
    UiFormatToken* AsFormat() override { return this; }
    const UiFormatToken* AsFormat() const override { return this; }

    bool newline = false;            // a newline token, or a font change after the first token
    int font = 0;                    // interface font
    std::uint32_t colour = 0;
};
struct UiWordToken : UiTextToken {
    UiWordToken() : UiTextToken(2), width(0) {}
    ~UiWordToken() override;
    UiWordToken* AsWord() override { return this; }

    std::string text;
    int width = 0;                   // its width in the token's font
};
// A laid out line of a scrolling text: the fields PlaceWords reads.
struct UiTextLine {
    int height = 0;                  // pixels
    int firstToken = 0;              // index in the token array
};

// A scroll bar showing tokenised text: the battle screen's message log and the objectives window's quest
// log.
class UiScrollText : public UiScrollbar {
public:
    // 20 lines per second, justified (align 2).
    UiScrollText(Interface3D* ui, const char* text, Rect rect, float depth);
    ~UiScrollText() override;
    // Scrolled to the end first, then kept that far from the end.
    void AddText(std::string_view text);
    // Scrolls to the top.
    void SetText(std::string_view text);
    void SetRect(const Rect* rect) override;
    // Places the words again when the position moved.
    void DrawSprites(SceneRenderer* pass) override;
    // The lines stay until the next layout.
    void Clear();
    // Words apart by a third of the line's height (the font in effect at the line's start); a newline format
    // token ends the line and sets the next lines' font. Then the scroll bar's maximum.
    void Layout();
    // One format token before each run of newline, BEL C<rgb> and BEL F<digit> tokens.
    void Tokenize(const std::string& text);
    // The token at `pos` (spaces and tabs skipped): a BEL F / BEL C format (3 / 5 characters), a newline, or
    // up to the next space, tab, BEL or newline. Returns the position after it, or -1 at the end (`out`
    // untouched). `this` is not read.
    int NextToken(const std::string& text, int pos, std::string* out);
    // The words of the lines shown from `scroll` pixels down into shownWords. A line is justified when align is
    // not 1, it is not the last, not ended by a newline, and of two or more tokens.
    void PlaceWords(int scroll);
    // The surface's paint: shownWords.
    void PaintWords(UiSurface& surface) const;

    Rect textRect;                   // the text's rectangle (surface coordinates)
    std::vector<UiTextToken*> tokens;
    std::vector<UiTextLine*> lines; // the laid out lines
    int align = 0;                   // 1: left aligned (else justified)
    // A word as PlaceWords placed it: its rectangle (surface layout units), font and colour.
    struct ShownWord {
        std::string text;
        Rect rect;
        int font = 0;
        std::uint32_t colour = 0;
    };
    std::vector<ShownWord> shownWords;

private:
    // The tail of SetRect (the text 14 pixels narrower for the bar).
    void PlaceText(const Rect& rect);
    void DeleteTokens();
};


// A spin control of the options window: a window stepping through its texts (the on / off settings).
class UiSpin : public UiWindow {
public:
    UiSpin(Interface3D* ui, Rect rect, const char* text);
    ~UiSpin() override;
    void AddText(std::string_view text);
    int Value() const { return value; }
    // Whether it changed since the last call (the flag cleared).
    bool TakeChanged();
    // The value, the changed flag cleared unread.
    int TakeValue();
    // 0 when not below the text count (a negative one is kept).
    void SetValue(int value);
    // The value `by` steps on, wrapping both ways.
    void Step(int by);
    // The next value, wrapping.
    void LeftDown(Point point) override;
    void LeftDouble(Point) override {}

    std::vector<std::string> texts;
    int value = 0;
    bool changed = false;
    Rect layout;                     // the constructor's rectangle
};

// A drop-down list (ours; the options' resolution row): the row shows the chosen text; a click (or Open) opens the
// list of every text below the row, or above it when only that side has room, with the chosen one marked, the active
// one (under the mouse, or moved to by the keys) highlighted and the game's scroll bar when they don't all fit. A
// click on an entry or Enter chooses the active one; Escape, a click outside or another click on the row closes it
// without a change. While open it is its owner's popup (every input event goes to it) and the owner's children under
// it are not drawn. Drawn in the game's list look: the frame and the shade of the save list, the rows' highlight and
// font. See docs/notes/ui-native.md, "The drop-down list".
class UiDropList : public UiContainer {
public:
    // `row` in layout coordinates.
    UiDropList(Interface3D* ui, UiContainer* owner, Rect row);
    void AddText(std::string_view text);
    // Whether it changed since the last call (the flag cleared).
    bool TakeChanged();
    // 0 when not below the text count.
    void SetValue(int value);
    // The value `by` steps on, wrapping both ways.
    void Step(int by);
    void Open();
    void Close();
    // The first entry shown, and entry `index`'s rectangle (layout; empty when not shown).
    int FirstShown() const;
    Rect EntryRect(int index) const;
    // The entries from `index` on shown (scrolled as little as needed).
    void ScrollTo(int index);

    // A control inside a window, never put on the stack: nothing to create or close.
    void CreateScreen() override {}
    void CloseScreen() override {}
    void Update() override;
    void Deactivate() override;
    void LeftDown(Point point) override;
    void LeftDouble(Point point) override { LeftDown(point); }
    void MouseMove(Point point) override;
    void Wheel(float notches, Point point) override;
    void KeyDown(platform::Key key, int scan, bool repeat) override;

    std::vector<std::string> texts;
    int value = 0;                   // the chosen entry
    bool changed = false;
    int active = 0;                  // the highlighted entry while open
    UiContainer* owner = nullptr;
    Rect row;                        // layout coordinates
    Rect frame;                      // the open list's frame (layout); entries inside its 5-pixel border
    bool open = false;
    int shown = 0;                   // entries shown at once
    UiScrollbar* scroll = nullptr;
    std::vector<UiWindow*> covered;  // the owner's children not drawn while open (under the list)
    UiMesh* rowArrow = nullptr;      // the row's arrow; the open list's shade, marks and frame
    UiMesh* listShade = nullptr;
    UiMesh* chosenMark = nullptr;
    UiMesh* activeMark = nullptr;
    UiMesh* listFrame = nullptr;

private:
    void PaintRow(UiSurface& surface) const;
    void PaintList(UiSurface& surface) const;
    // The entry at screen point `point`, or -1.
    int EntryAt(Point point) const;
    void Choose(int index);
    void SetActive(int index);
    // The marks placed for the scroll position and the active and chosen entries.
    void PlaceMarks();
};

// A saved game in the save/load list; the fields after the folder come from the save's info.sav.
struct SaveEntry {
    // "save_quick", "save_auto_enter" / "save_auto_exit" for "saveq", "savea" / "saveb", else the zone's name.
    void MakeTitle();
    // Text "zone " + zone (its first line), else "save_camp"; text "allod " + map, else "Unknown".
    std::string ZoneTitle() const;
    std::string AllodTitle() const;

    std::string title;
    std::string folder;             // under saves\ ("saveq" quick, "savea" / "saveb" automatic, "save<n>")
    std::int32_t savedAt = 0;        // time() when it was saved (info.sav version 2 and up)
    float gameTime = 0.0f;           // the "gtime" variable
    std::string map;                // the map of the zone
    std::string zone;
    bool broken = false;             // no info.sav: titled "Error Save", no picture
};

// The quick and automatic saves (`slot` the save folder's name). Nothing for a multiplayer client or when
// the disc lacks the space a save needs (0 free space: not checked). Writes info.sav (the "gtime" variable,
// the zone's map, the zone, the time, an empty title) and shot.sav (the screen's picture, save_picture.h).
void QuickSave(const char* slot, bool stepFrame);
// Not for a multiplayer client; only when saves\<slot>\ exists.
void QuickLoad(const char* slot);

// The save/load window (opened from the Esc menu).
class SaveLoadWindow : public UiContainer {
public:
    SaveLoadWindow(Interface3D* ui, bool newIsSave, bool newSliding)
        : UiContainer(ui), isSave(newIsSave), sliding(newSliding) {}
    void DrawSprites(SceneRenderer* pass) override;                      // slides in
    void Update() override;
    // The new save's picture released.
    void CloseScreen() override;
    void LeftDown(Point point) override;
    void LeftDouble(Point point) override;
    void MessageResult(std::uint32_t result) override;

    // A save window saves into the new save (row 0) or asks "overwrite" (message box 0x20000); a load
    // window loads.
    void Confirm();
    // Not for a save window's new save; asks "delete" (message box 0x10000).
    void AskDelete();
    // Deletes the selected save's files and folder and refills the list.
    void DeleteSave();
    // Not for a broken save.
    void LoadSelected();

    // The caret kept within the name and the selection's marker on its row; PaintList (surface 0's paint) draws the
    // rows, the caret and the selected save's details (Rebirth-patched: WideScreen).
    void RefreshListVanilla();
    void PaintList(UiSurface& surface) const;

    // Saves into the selected entry's folder, then info.sav and shot.sav (newPicture); a message
    // box when the disc is full.
    void SaveGame();
    // The selected save's shot.sav (as `picture`, no larger than shown), or dialog for a save window's new save;
    // hidden for none or a broken save.
    void ShowPicture();
    // Reads saves\save*\info.sav and sorts the list; a save window's new save goes first (newPicture made from the
    // escape menu's copy of the screen, shown as dialog).
    void FillEntries();

    void CreateScreen() override;

    int EntryCount() const { return static_cast<int>(entries.size()); }
    void TakeSelectedTitle();

    // In a save window the edit keys edit the save's name (the first edit of an ordinary save takes over its
    // title, Backspace / Delete starting from an empty name).
    void KeyDown(platform::Key key, int scan, bool repeat) override;
    // In a save window: the first character for an ordinary save starts an empty name; a name stays under 60
    // characters and 178 pixels (font 1).
    void Char(int key, int scan) override;
    // Clamps the selection and scrolls it into view (16 rows).
    void ShowSelection();
    // "saveq", "savea" or "saveb"; no bound check.
    bool IsReservedSave(int index);

    static constexpr int kVisibleRows = 16;
    UiRegion* okButton = nullptr;
    UiRegion* cancelButton = nullptr;
    UiRegion* deleteButton = nullptr;
    UiRegion* rows[kVisibleRows] = {};   // the visible rows, from firstVisible
    UiMesh* pictureSprite = nullptr;     // the selected save's picture
    UiMesh* marker = nullptr;            // the selected row's marker
    std::vector<SaveEntry> entries;
    int selected = 0;
    int firstVisible = 0;            // list scroll position
    bool isSave = false;             // save (not load) window
    bool sliding = false;            // sliding in; input is ignored meanwhile
    float slide = 0.0f;              // 0..1
    UiScrollbar* scrollbar = nullptr;
    // The selected save's picture in layout coordinates (CreateScreen's sprite).
    static constexpr Rect kPictureRect{410, 100, 700, 320};
    Texture* picture = nullptr;     // the selected save's picture (ShowPicture)
    Texture* dialog = nullptr;      // the save window's picture of the game for the new save
    std::vector<std::uint8_t> newPicture;    // the new save's shot.sav bytes (save_picture.h), dialog's picture
    std::string name;               // the save name being edited
    bool nameTyped = false;
    int cursor = 0;                  // caret position in name (bytes, at a character's start: U4)
};

// The options window's settings, in the order of OptionsWindow::settings and kOptionDefinitions.
enum OptionId : int {
    kOptionSfxVolume,
    kOptionStreamVolume,
    kOptionVoiceVolume,
    kOptionKeyboardPower,
    kOptionMousePower,
    kOptionScrollBorder,
    kOptionRubberSelect,
    kOptionShadows,
    kOptionSelectType,
    kOptionShowPath,
    kOptionBrightness,
    kOptionGamma,
    kOptionShowFlyingHp,
    kOptionShowTutorial,
    kOptionAutosave,
    kOptionTooltipTime,
    kOptionSwitchFilters,
    kOptionCameraReverseX,
    kOptionCameraReverseY,
    kOptionReverseStereo,
    kOptionDifficulty,
    kOptionResolution,
    kOptionDisplayMode,
    kOptionVsync,
    kOptionMsaa,
    kOptionAnisotropy,
    kOptionPerfOverlay,
    kOptionCount,
};

struct OptionDefinition {
    const char* name = nullptr;      // "option_" + name is its text key (else the name is shown)
    int kind = 0;                    // 0 a slider, 1 an on / off spin control, 2 the resolutions' drop-down list, 3 a
                                     // spin control over a choice list (ui_options.cpp's)
    int maximum = 0;                 // the slider's range
    int group = 0;                   // the option group (0-10) it is shown in
    int row = 0;                     // its row there
};
constexpr int kOptionDefinitionCount = kOptionCount;

struct ActionDefinition {
    int id = 0;                      // the action (the key code in the key table)
    const char* name = nullptr;      // "action" + name is its text key
    int group = 0;
    int row = 0;
};
constexpr int kActionDefinitionCount = 59;
// The key actions (a table of 60; the first is a blank id 0): the key bindings file's names and the options
// window's rows (group, row).
inline constexpr ActionDefinition kActionDefinitions[60] = /*00787070*/ {
    {0, "", 0, 0},
    {1, "pause", 3, 4},
    {2, "decel", 3, 3},
    {3, "accel", 3, 2},
    {4, "obj", 3, 6},
    {5, "camera_up", 6, 1},
    {6, "camera_down", 6, 2},
    {7, "camera_left", 6, 3},
    {8, "camera_right", 6, 4},
    {9, "camera_zoom_in", 6, 5},
    {10, "camera_zoom_out", 6, 6},
    {11, "camera_track", 6, 0},
    {12, "camera_norm", 6, 8},
    {13, "camera1", 6, 9},
    {14, "camera2", 6, 10},
    {15, "camera3", 6, 11},
    {16, "camera4", 6, 12},
    {17, "cs_head", 9, 0},
    {18, "cs_body", 9, 1},
    {19, "cs_rleg", 9, 2},
    {20, "cs_lleg", 9, 3},
    {21, "cs_rhand", 9, 4},
    {22, "cs_lhand", 9, 5},
    {23, "crawl", 5, 7},
    {24, "sneak", 5, 6},
    {25, "walk", 5, 5},
    {26, "run", 5, 4},
    {27, "swarm", 5, 2},
    {28, "follow", 5, 0},
    {29, "use_science", 5, 1},
    {30, "spell1", 8, 0},
    {31, "spell2", 8, 1},
    {32, "spell3", 8, 2},
    {33, "spell4", 8, 3},
    {34, "spell5", 8, 4},
    {35, "spell6", 8, 5},
    {36, "spell7", 8, 6},
    {37, "spell8", 8, 7},
    {38, "weapon1", 7, 0},
    {39, "weapon2", 7, 1},
    {40, "weapon3", 7, 2},
    {41, "weapon4", 7, 3},
    {42, "item1", 7, 5},
    {43, "item2", 7, 6},
    {44, "item3", 7, 7},
    {45, "item4", 7, 8},
    {46, "w_info1", 10, 3},
    {47, "w_info2", 10, 4},
    {48, "w_info3", 10, 5},
    {49, "w_info4", 10, 6},
    {50, "w_text1", 10, 0},
    {51, "w_text2", 10, 1},
    {52, "w_minimap", 10, 8},
    {53, "quicksave", 3, 8},
    {54, "quickload", 3, 9},
    {55, "select1", 4, 0},
    {56, "select2", 4, 1},
    {57, "select3", 4, 2},
    {58, "select_all", 4, 4},
    {59, "tutorial_script", 10, 10},
};

// The rows of the shown group point at them.
struct OptionSetting {
    int value = 0;
    UiScrollbar* bar = nullptr;     // a UiSlider, or null
    UiSpin* spin = nullptr;         // or null
    UiDropList* list = nullptr;     // the resolutions' drop-down list, or null
    std::string text;               // the shown name
};

// An exit shown on the objectives window's zone picture.
struct ObjectiveExit {
    bool flag = false;               // a known distance (global map), or the zone variable is not 1
    int exit = 0;                    // index in the zone's exit list
    float distance = 0.0f;
    float x = 0.0f;                  // centre of the exit rectangle (world units)
    float y = 0.0f;
    float mapX = 0.0f;               // on the picture (layout)
    float mapY = 0.0f;
};

// The objectives window (TAB in a zone opens it for the current zone). Left: the quests of the zone (rows),
// the log; right: the zone picture with its exits; the arrows switch between zones when it lists several.
class ObjectivesWindow : public UiContainer {
public:
    using UiContainer::UiContainer;
    // The alpha test's reference Create replaced put back.
    void CloseScreen() override;
    void CreateScreen() override;
    // Picture, exits, quests and description.
    void ShowZone(std::string_view zone);
    // When the selected quest changed: its description and a marker and region per place of the quest.
    void ShowQuest();
    // Up to five rows from firstRow: their marks and the selection's marker, their titles into questTitles.
    void DrawQuestList();
    // Surface 0's and surface 1's paints: questTitles; the zone's title and description.
    void PaintQuestList(UiSurface& surface) const;
    void PaintZone(UiSurface& surface) const;
    // OK (with two buttons the time to the nearest exit passes and that zone is requested), cancel, the previous /
    // next listed zone, a quest row or an exit.
    void LeftDown(Point point) override;
    // Enter as OK, Esc closes, PgUp / PgDn / Up / Down move the selected quest by 5 / 1. A key
    // left unused that is bound to the objectives action closes a window with one button.
    void KeyDown(platform::Key key, int scan, bool repeat) override;
    // A multiplayer client takes them from the quest database's record, otherwise from map.txt's "#quest
    // <name>" entry (the second line after it, its words as x y pairs).
    void LoadQuestPlaces(std::string_view name);
    // Exit `index` becomes the nearest (its marker 1.2 times markerScale); the blink restarts.
    void SelectExit(int index);
    // Keeps the selected quest within the five shown rows.
    void RefreshQuests();
    // Every frame: the markers' blink, alphas (cos(2.5 t) * 0.3 + 0.5) * 255 and (cos(5 t) * 0.4 + 0.6) *
    // 255.
    void Update() override;
    // `exitDistances` may be null. The help topic is "quest_global_map" with two buttons, else
    // "quest_mission".
    void Setup(std::string_view zone, std::map<int, float>* exitDistances, bool twoButtons, bool listZones);
    const char* HelpTopic() override;

    std::vector<std::string> zones;    // the zones listed
    std::string zoneName;           // the zone it was opened for
    bool twoButtons = false;         // OK on the left and a second button (else OK centred)
    std::map<int, float>* exitDistances = nullptr;    // exit -> distance, or null
    bool listZones = false;          // list the zones of the zone's allod with quests
    int zoneIndex = 0;
    std::string shownZone;
    std::vector<ObjectiveExit> exits;
    int nearestExit = 0;
    int selectedQuest = 0;           // row of the quest named by the current-quest global
    int shownQuest = 0;              // the row ShowQuest last showed (-1: none)
    std::vector<std::string> quests;
    UiScrollText* questText = nullptr;   // the selected quest's text column (a child)
    UiScrollbar* bar = nullptr;
    int firstRow = 0;                // the first quest row shown (the bar's position)
    float markerScale = 0.0f;
    float blinkTime = 0.0f;          // seconds since the exit was selected (the markers' blink)
    std::vector<Vector2> questPlaces;    // the selected quest's places
    std::vector<std::string> objectiveTexts;    // obj_0..obj_2
    std::vector<std::string> questTitles;    // the shown rows' titles (DrawQuestList)
    std::string zoneTitle;          // the last zone with a text: its title and description (ShowZone)
    std::string zoneText;
    std::uint8_t savedAlphaRef = 0;

    static constexpr int kVisibleRows = 5;
    UiRegion* okButton = nullptr;
    UiRegion* cancelButton = nullptr;        // the second button (no tip with one button)
    UiRegion* previousZoneButton = nullptr;
    UiRegion* nextZoneButton = nullptr;
    UiRegion* rows[kVisibleRows] = {};       // the quest rows, from firstRow
    UiMesh* okIcon = nullptr;
    UiMesh* selectionMarker = nullptr;       // on the selected quest's row
    UiMesh* zonePicture = nullptr;           // ShowZone's picture
    UiMesh* bottomShade = nullptr;           // the strip under the buttons; selecting an exit hides it
    UiMesh* bottomFrame = nullptr;
    UiMesh* previousZoneArrow = nullptr;
    UiMesh* nextZoneArrow = nullptr;
    UiMesh* questMarks[kVisibleRows] = {};   // a shown row's done / failed mark
    // ShowZone's exits and ShowQuest's places: a marker and a region each, made after the window's own.
    std::vector<UiMesh*> exitMarkers;
    std::vector<UiRegion*> exitAreas;
    std::vector<UiMesh*> placeMarkers;
    std::vector<UiRegion*> placeAreas;
};

// A key binding row of the options window.
struct OptionBinding {
    const ActionDefinition* action = nullptr;
    const UiKeyName* key = nullptr;     // the key bound to the action, or null
    std::string name;               // the action's shown name
    std::string keysText;           // the keys' names
};

// A row of the options window: a key binding or a setting.
struct OptionRow {
    OptionBinding* binding = nullptr;
    OptionSetting* setting = nullptr;
};

// The options window: OK, cancel, the option groups' buttons (100..250 x 156..420) and the rows of the shown group
// right of them.
class OptionsWindow : public UiContainer {
public:
    static constexpr int kGroups = 11;
    static constexpr int kRows = 14;

    OptionsWindow(Interface3D* ui, bool newSliding) : UiContainer(ui), sliding(newSliding) {}
    void DrawSprites(SceneRenderer* pass) override;                      // slides in
    void DrawSurfaces() override;                            // not while sliding
    void CloseScreen() override {}
    const char* HelpTopic() override;
    void LeftDown(Point point) override;
    void LeftDouble(Point point) override;
    void MessageResult(std::uint32_t result) override;
    // Applies the settings whose bar or spin control changed (volumes, the display's gamma at once).
    void Update() override;

    // The group buttons' tints and the highlight on the selected row; PaintRows (surface 0's paint) draws the group
    // titles and the shown group's rows (a binding waiting for its key in red).
    void RefreshRows();
    void PaintRows(UiSurface& surface) const;
    // Clamped to 0-10; the first filled row selected (or row 0), the key wait cleared.
    void ShowGroup(int group);
    // OK: the key table and every setting into the game's option globals (the difficulty only outside a
    // multiplayer client); a shadow switch (settings 7-9) turned off drops those shadows; another display mode or
    // resolution is chosen (app::ChooseFullScreen, app::ChooseResolution), the renderer's switches set; the
    // settings file saved.
    void Apply();
    // A slider (kind 0), an on / off spin control (kind 1), the resolutions' drop-down list (kind 2) or a choice spin
    // control (kind 3) per setting.
    void BuildSettings();
    void LoadSettings();
    // The keys' text is the first key name of a scan code bound to the action.
    void BuildBindings();

    // Binds pressedKey to the selected row's action, taking it from the action it was bound to. Nothing
    // without a pressed key or a binding row.
    void CommitBinding();
    // Cancel: puts back the volumes and the display settings the window started with.
    static void RestoreSettings();
    void CreateScreen() override;

    // Esc cancels, Enter starts a key binding, steps a spin row or opens a list row, Up / Down select rows, PgUp /
    // PgDn switch groups, Left / Right move a bar or step a spin or list row back or on. While a binding waits, the
    // next key is bound (Esc gives up); a key bound to another action asks first. An open list takes every key.
    void KeyDown(platform::Key key, int scan, bool repeat) override;

    signed char keyOfScan[0x200] = {};  // the key code bound to each scan code
    int selectedRow = 0;
    int group = 0;                   // the option group shown
    std::vector<OptionBinding> bindings;
    std::vector<OptionSetting> settings;
    OptionRow rows[kRows];
    bool waitingForKey = false;      // a key binding row waits for the new key
    bool sliding = false;
    float slide = 0.0f;
    std::vector<std::string> groupNames;    // the option groups' titles
    bool shadowsChanged = false;     // the shadows switch (setting 7) changed
    const UiKeyName* pressedKey = nullptr;  // the key pressed for a binding row
    std::vector<Size> resolutions;   // the resolution setting's sizes, the screen's among them

    // The regions, in their creation order.
    UiRegion* okButton = nullptr;
    UiRegion* cancelButton = nullptr;
    UiRegion* groupButtons[kGroups] = {};
    UiRegion* rowAreas[kRows] = {};
    UiMesh* highlight = nullptr;           // on the selected row
    UiMesh* groupIcons[kGroups] = {};      // the groups' buttons, lit for the group shown
};

// The message box's edit field.
class UiEditField : public UiWindow {
public:
    // While focused. The first edit key starts editing (Backspace / Delete from an empty text, the others
    // with the caret at the end); the caret is not clamped here.
    void KeyDown(platform::Key key, int scan, bool repeat) override;
    // The caret kept within the text (after every change, as the original's redraw did).
    void ClampCaret();
    // The surface's paint: the text (centred with mode 1), then a 1-pixel caret line in caretColour after the
    // caret's characters while it falls inside the field.
    void PaintContents(UiSurface& surface) const;
    // Every character class, font 1, at most 50 characters; focused. caret, editing and changed are left
    // as they are.
    UiEditField(Interface3D* ui, Rect rect);
    ~UiEditField() override;
    // While focused and without Ctrl, a character of an allowed class: the first one starts editing from an
    // empty text; inserted while shorter than maxLength and narrower than area.
    void Char(int key, int scan) override;
    // SetText: not editing, changed, the caret at its end.
    void SetLook(std::uint32_t charClasses, char mode, int font, int maxLength, std::uint32_t colour, std::uint32_t caretColour);
    void SetText(std::string_view text);
    void SetFocused(bool focused);

    Rect area;                       // the surface's layout rectangle
    std::uint32_t charClasses = 0;   // classes Char takes (1 digits, 2 letters / space, 4 others, 8 '.' ':')
    std::uint8_t centred = 0;        // SetLook's mode
    int font = 0;                    // interface font
    int maxLength = 0;
    std::uint32_t colour = 0;             // the text's colour
    std::uint32_t caretColour = 0;        // SetLook's last argument (0xFF from the constructor)
    std::string text;
    int caret = 0;                   // caret position in text (bytes, at a character's start: U4)
    bool editing = false;            // an edit key was pressed (the first one sets it)
    bool focused = false;            // keys are taken only while set
    bool changed = false;
};

// The buttons of a message box.
enum class MessageButtons : std::uint32_t {
    Ok = 1,
    Cancel = 2,         // the cancel button alone (region 1; region 0 stays empty)
    OkCancel = 3,
    OkCancelEdit = 7,   // OK and cancel under an edit field instead of the text (its initial text is the text key itself)
};

// Which question a message box asks: the high half of what it leaves pending, so that the window below tells its
// boxes apart (each window numbers its own, from First).
enum class MessageQuestion : std::uint32_t {
    First = 0x10000,
    Second = 0x20000,
    Third = 0x30000,
    Fourth = 0x40000,
    Fifth = 0x50000,
    Disconnect = 0x990000,   // the multiplayer client lost its server
    Exit = 0xFFFF0000,       // the end of the game: OK plays the exit movie and the game quits
};

// What was pressed: the region's number plus one.
enum class MessageAnswer : std::uint32_t { Ok = 1, Cancel = 2 };

// What a box of `question` leaves pending (Interface3D::SetPendingValue) for the window below: its MessageResult.
constexpr std::uint32_t BoxResult(MessageQuestion question, MessageAnswer answer) {
    return static_cast<std::uint32_t>(question) | static_cast<std::uint32_t>(answer);
}

// The answers the battle and briefing screens take from their boxes.
inline constexpr std::uint32_t kLeaveZoneYes = BoxResult(MessageQuestion::First, MessageAnswer::Ok);     // "leave_zone"
inline constexpr std::uint32_t kGameOverLoad = BoxResult(MessageQuestion::Second, MessageAnswer::Ok);    // "game_over"
inline constexpr std::uint32_t kGameOverQuit = BoxResult(MessageQuestion::Second, MessageAnswer::Cancel);

// The message box.
class MessageWindow : public UiContainer {
public:
    // A missing text gives its key. The text is centred and word-wrapped.
    MessageWindow(Interface3D* ui, std::string_view titleKey, const char* textKey, MessageQuestion question, MessageButtons buttons);
    void CreateScreen() override;
    // OK: for the exit question the exit movie and the game quits; otherwise the edit field's text into editText.
    // OK or cancel then leaves BoxResult(question, Ok / Cancel) pending.
    void LeftDown(Point point) override;
    // Unless a child used the key, Enter answers as OK and Esc as cancel.
    void KeyDown(platform::Key key, int scan, bool repeat) override;
    // Into the caller's buffer.
    void CopyEditText();
    // Also the master request box's and the difficulty question's.
    void CloseScreen() override;

    std::string title;
    std::string text;
    MessageQuestion question = {};   // Exit: the text in font 1, else 2
    MessageButtons buttons = {};
    std::vector<UiLabel> labels;     // the surface's paint: the title and the text (CreateScreen measures and places them)
    UiEditField* edit = nullptr;    // the edit field (OkCancelEdit)
    char* editText = nullptr;        // the edit field's first text; OK copies the typed one here
    UiRegion* okButton = nullptr;    // null in a cancel-only box
    UiRegion* cancelButton = nullptr;   // null in an OK-only box
};

// The help / message window (Interface3D::ShowMessage and OpenHelp). Its pages come from the texts:
// "tutorial <text>" holds the title key and a "#screen" line per page, and "tutor <key>" each key's text.
class HelpWindow : public UiContainer {
public:
    HelpWindow(Interface3D* ui, bool newMessage, const std::string& newText, int newPage)
        : UiContainer(ui), message(newMessage), text(newText), page(newPage) {}
    void DrawSprites(SceneRenderer* pass) override;
    // The pulsing frames, pictures, surfaces, regions and sprites dropped.
    void CloseScreen() override;
    // Without the text or without pages the interface's pending value is set to 0 and nothing else happens.
    void CreateScreen() override;
    // The pulse: v = (cos(5 t) + 1) / 2 gives alpha 255 * v and the colour 80 * (1 - v).
    void Update() override;
    // The previous page, OK (Finish) or the next page.
    void LeftDown(Point point) override;
    // Enter the next page (Finish on the last), Esc or the help key Finish.
    void KeyDown(platform::Key key, int scan, bool repeat) override;
    // Rebirth-patched (WideScreen). Builds the page from its "#screen" line: the rectangle
    // (200, 50)-(600, 560) or "#position", one pulsing frame per "#rectangle" line, one entry per "#text"
    // line. Details: docs/notes/ui.md, "Behaviour moved out of header comments".
    void ShowPage();
    // The page built again: its pulsing frames follow the battle screen's panels to the window's new edges.
    void LayoutChanged() override;
    // The surfaces' paints: the page's and the topic's titles; the texts; the neighbouring pages' titles and the
    // help key's name.
    void PaintHeader(UiSurface& surface) const;
    void PaintBody(UiSurface& surface) const;
    void PaintFooter(UiSurface& surface) const;

    bool message = false;                // a message window (ShowMessage), not help
    std::string text;                   // the message, or the help topic
    std::string title;
    std::vector<std::string> lines;      // the pages' texts
    int page = 0;                        // the page shown (the topic's or message's saved page)
    std::vector<UiMesh*> pulsing;      // sprites whose plain colour pulses (Update)
    std::vector<UiMesh*> pictures;     // the entries' picture sprites
    UiRegion* previousButton = nullptr;   // the page's buttons (ShowPage; null while none is built)
    UiRegion* okButton = nullptr;
    UiRegion* nextButton = nullptr;
    float pulseTime = 0.0f;
    // What ShowPage keeps for the paints: the page's width (layout), the texts with their first rows, and the help
    // key's hint ("<show_tutorial_key> <key>"; empty without a key).
    struct HelpText {
        std::string text;
        Rect row;
    };
    int pageWidth = 0;
    std::vector<HelpText> bodyTexts;
    std::string keyHint;

private:
    // The page is kept for the topic (or the message).
    void Finish();
};

// A line of the in-game chat overlay: the text, the speaker, a value and the line's own surface.
struct ChatLine {
    std::string text;          // AddLine leaves it empty
    std::string name;          // the line's text without its colour codes
    std::uint32_t time = 0;     // timeGetTime when it was added
    std::unique_ptr<UiSurface> surface;
};
static_assert(std::is_nothrow_move_constructible_v<ChatLine>);

// The in-game chat overlay: the interface's root window, updated after the top window every frame; in a
// multiplayer client it holds the chat line, which takes the keys while it is open.
class ChatOverlay : public UiWindow {
public:
    // Rebirth-patched.
    explicit ChatOverlay(Interface3D* ui);
    // Lines narrower than 780 pixels, "^<digit>" switching the colour (bits 0 / 1 / 2
    // blue / green / red; a missing digit is 7); at most 12 lines remain.
    void AddLine(const char* text);
    // Opening empties the typed line.
    void SetOpen(bool open);
    void ClearLines();
    // Hidden while the chat is closed; clamps the caret.
    void ShowTyped();
    // Rebirth-patched (WideScreen). The typed line's paint: the background, the text and a 1-pixel caret line.
    void PaintTyped(UiSurface& surface) const;
    // Nothing while the chat is closed. Every key is marked used; Enter sends a non-empty line (a network
    // client: SayMessage kept 60 s) and, like Esc, closes the chat. Char inserts unless Ctrl is down, the
    // line is 200 characters or more, or it would be 780 pixels wide; the character is not marked used.
    void KeyDown(platform::Key key, int scan, bool repeat) override;
    void Char(int key, int scan) override;
    // Lines older than 20 s are erased.
    void Update() override;
    void DrawSurfaces() override;

    std::vector<ChatLine> lines;
    bool chatOpen = false;           // a multiplayer client's chat line is open (it takes the keys)
    std::string typed;
    int caret = 0;                   // in the typed line (a byte offset at a character's start: U4)
};

// The new game's difficulty question (the main menu's new game button): a message box ("option_difficulty", OK and
// cancel) with two check boxes. Its button handler sets easy from the boxes and copies it to
// options::g_options.difficulty on OK.
class DifficultyBox : public MessageWindow {
public:
    explicit DifficultyBox(Interface3D* ui) : MessageWindow(ui, "option_difficulty", "", MessageQuestion::Second, MessageButtons::OkCancel) {}
    // No container handler. OK (easy to options::g_options.difficulty), cancel, or a check box checked.
    void LeftDown(Point point) override;
    // Unless a child used the key, Enter answers as OK and Esc as cancel.
    void KeyDown(platform::Key key, int scan, bool repeat) override;
    // The texture shift of a checked box (0xBDB80000).
    static constexpr float kCheckedOffset = -0.08984375f;

    // Hard checked first.
    void CreateScreen() override;

    int easy = 0;                    // 1 easy, 0 hard (the default)
    UiMesh* easyBox = nullptr;       // the check boxes' sprites and regions
    UiMesh* hardBox = nullptr;
    UiRegion* easyArea = nullptr;
    UiRegion* hardArea = nullptr;
};

// config\keyboard.ini.
namespace keybind {
void LoadKeyBindings();
void SaveKeyBindings();
}
