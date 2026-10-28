#pragma once

// The 2D interface: the interface manager CInterface3D, its windows, their clickable regions, off-screen
// surfaces and 3D model previews. Layouts are designed for 800x600; CInterface3D holds the real screen
// size and scales between the two; windows keep their regions in layout coordinates and convert the mouse
// position before hit testing.

#include "app.h"   // app::g_interfaceHasScreen
#include "core_transform.h"
#include "options.h"
#include "debug_panel.h"
#include "engine_object.h"
#include "figure_mesh.h"
#include "game_data.h"
#include "game_strings.h"   // game::EmptyText
#include "item_db.h"
#include "object_tables.h"   // g_keyActions
#include "unit.h"   // LockKey

#include "lib/compiled_vtable.h"
#include "lib/containers.h"
#include "lib/mfc.h"
#include "lib/static_object.h"
#include "lib/stlp.h"
#include "lib/str.h"

#include <cstddef>
#include <cstdint>

#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <windows.h>

inline constexpr float kHudFullTurn = /*007C8140*/ 6.2831855f;
inline constexpr float kHudQuarterTurn = /*007C8144*/ 1.5707964f;
inline constexpr Quaternion kHudRestRotation = /*007C8150*/ {1.0f, 0.0f, 0.0f, 0.0f};

namespace console { class ConsoleWindow; }
class CParty;
namespace mfc::impl { class CFont; class CDC; }

class CUiWindow;
class CChatOverlay;
struct UiInputSide;
class CUiMesh;
struct CDXSurface;
class CTexture;
class CTextureDevice;
class CPixelConverter;
class CUiSurface;
class CItemScreen;

// The interface's screen fade and letterbox, drawn over everything by the interface's draw.
struct CScreenFade {
    // The letterbox bars and, while fading, a black rectangle whose alpha moves by rate * elapsed ms; the
    // fade ends when alpha drops below 0.
    int Draw(CSceneRenderer* renderer);
    // Starts a fade over `ms` milliseconds, from alpha 0 (ms > 0) or 255.
    void Start(int ms);
    // Alpha 0, not fading, no letterbox; the rate and the clock stay unwritten.
    CScreenFade* Construct();

    float rate_;                     // alpha per millisecond
    float alpha_;                    // 0..255
    std::uint8_t active_;            // fading
    std::uint8_t unused_09[3];
    DWORD lastTime_;                 // timeGetTime of the last step
    std::uint8_t letterbox_;         // the bars are shown
};
static_assert(offsetof(CScreenFade, lastTime_) == 0x0C && offsetof(CScreenFade, letterbox_) == 0x10);

// The key of the multiplayer message 12's text (CInterface3D::MessageText), "reserved_02": EI_Plugin's Pause
// writes its digit, '2' for a pause and '3' ("reserved_03") for a resume (player_server_orders.cpp).
extern char g_pauseMessageKey[12];

// The interface manager.
class CInterface3D {
public:
    // Layout (800x600) to screen and back; the Copy is identical.
    int ScaleRectToScreen(RECT* rect) const;
    int ScalePointToScreen(POINT* point) const;
    int ScaleRectFromScreen(RECT* rect) const;
    int ScalePointFromScreen(POINT* point) const;
    int ScalePointFromScreenCopy(POINT* point) const;

    // The scaling where Rebirth's WideScreen (widescreen::Active) leaves out the margin: the text surfaces'
    // draw, edit field, connection list, save / load list, chat line, tooltip.
    int CallScaleRectToScreenNoMargin(RECT* rect) const;
    int CallScalePointToScreenNoMargin(POINT* point) const;
    int CallScalePointFromScreenNoMargin(POINT* point) const;

    // The scaling as reached with a null interface (WideScreen's callers): then without the margin.
    int ScaleRectToScreenEntry(RECT* rect) const;
    int ScalePointToScreenEntry(POINT* point) const;
    int ScaleRectFromScreenEntry(RECT* rect) const;
    int ScalePointFromScreenEntry(POINT* point) const;
    int ScalePointFromScreenCopyEntry(POINT* point) const;

    // Once per frame: pending closes, the queued window, the mouse, the top window and the root.
    void Update();

    // A script string event ("say" / "say_block" text [key], "tutorial" name, "briefing" name, "autosave",
    // "endofgame"); the first word is lowercased. Rebirth's NewStringEvents adds "gameover [title [text]]".
    void OnStringEvent(const char* event);
    CUiWindow* GetActiveWindow() const;
    // The top window as the screen the caller runs under (the HUD's panels under the CBattleScreen, the
    // briefing chat under the CBriefingScreen); no check.
    template <typename Screen>
    Screen* ActiveWindowAs() const {
        return static_cast<Screen*>(GetActiveWindow());
    }
    // Consumed by Update (the top window closes).
    void SetPendingValue(std::uint32_t value);
    // Makes screen `id` (1 in-game, 3 base, 4 island map, 2, 5, 7, 8, 9) and activates it; 0 does
    // nothing; 6 and ids above 9 hand over the id itself as the window pointer.
    void CreateScreen(int id);
    // `flags`: WM_KEYDOWN lParam bits (scan code in the low 9, bit 14 repeat). The key under Esc toggles the
    // console; Scroll Lock saves "shot<nnnn>.bmp"; an unhandled ';' shows help.
    void OnKeyDown(int key, int repeat, int flags);
    // Opens `window` on top of the stack (null is fatal). The first window keeps the server flag; `a` set
    // and `b` clear / both set pick backdrop mode 3 / 1.
    void PushWindow(CUiWindow* window, int a, int b);
    // Closes and deletes every window, then opens `window` as PushWindow does.
    void Activate(CUiWindow* window);
    // Reads the interface's saved state from `stream`: flags and two values; from save version 0x47 one
    // more; from 0x56 an obsolete list (count, then name and value pairs, discarded) and a CString; from
    // 0x5D one more value; from 0x5F a CString.
    void LoadState(CStream* stream);
    // The text of message `id` (0-12: "lmp_player_connected" .. "reserved_02"; others an empty key) from
    // the text table, formatted with `value` unless 0, into `out`.
    std::string* MessageText(std::string* out, int id, const char* value);
    // Without `force`, takes the next queued message into the current text or returns when there is
    // none. A non-empty text opens the message window over the others, unless the option is off and it
    // was not forced.
    void ShowMessage(int force);
    // The active window's help topic (none: nothing): the map counts topics seen (an unseen one is
    // entered with 0, giving -1 here); the help window opens when forced (`flag`), or when the option is
    // on and the topic was not seen.
    void OpenHelp(int flag);
    // A 2D sound at priority 1000, volume 100; returns its handle.
    int PlaySound(const char* name, int loop);
    void StopSound(int handle);
    // A multiplayer client's screens call them for Enter / Backspace. While the chat line is closed:
    // Enter opens it, Backspace empties the chat lines.
    void ChatKeyEnter();
    void ChatKeyBackspace();
    // The extent of `text` in interface font `font`, measured on the game window's DC and scaled to the
    // layout (cx * 800 / width, cy * 600 / height). Rebirth-patched; WideScreen behind its flag.
    SIZE* TextExtent(SIZE* out, std::string_view text, int font);
    // Sets up the stack, lists, map and texts; nothing else is written.
    CInterface3D* Construct();
    // When there is a console window.
    void ConsoleAddText(const char* text);
    void SetConsoleRect(const RECT* rect);
    void SetConsoleHistory(int lines);
    void SetDebugInfoRect(const RECT* rect);
    // The console's "fps": a surface for the frame rate at (730, 10)-(800, 28) unless there is one; off
    // frees it.
    void ShowFps(int on);
    // A quest variable changed (client, script): the top window hears of it; at 1 (or NaN) a name of
    // three or more '.' parts keeps its third part in savedText_ (the current quest).
    void OnQuestVariable(const char* name, float value);
    // News for the top window (nothing without a window), each reaching its virtual of that name: items
    // picked up, a unit joined / left the party, the game lost, a unit speaks, the players changed, an
    // exchange between players ended; SaveWindowState has the bottom window save its state.
    void OnItemsPicked(void* items, int money, int crippled);
    void OnUnitJoined(CUnit* unit);
    void OnUnitLeft(CUnit* unit);
    void OnGameLost();
    void OnUnitSpeech(int id, unsigned event);
    void OnPlayersChanged(int added);
    void OnExchangeDone(CParty* partyB, CParty* partyA, CParty* partyC);
    void SaveWindowState();
    void OnChatLine(const char* text, std::uint32_t number);
    // The save / load window's status text, drawn in green on a cleared surface over (0, 280)-(800, 320)
    // for one frame, then the surface freed.
    void ShowStatusText(const char* text);

    // Mouse messages from the input dispatcher (ui_interface_input.cpp): each goes to the input side that
    // took the mouse, else the top window's, else nowhere. Button downs and double clicks set or clear
    // the button flags around the call; OnWheel returns the handler's result (0 without a target).
    void OnLeftDown(int buttons, int x, int y);
    void OnLeftUp(int buttons, int x, int y);
    void OnMouseMove(int buttons, int x, int y);
    void OnRightDown(int buttons, int x, int y);
    void OnRightUp(int buttons, int x, int y);
    void OnLeftDouble(int buttons, int x, int y);
    void OnRightDouble(int buttons, int x, int y);
    int OnWheel(int flags, int delta, int x, int y);
    UiInputSide* MouseTarget() const;
    // A typed character (WM_CHAR): nothing for the key under Esc; the shown console's OnChar, else a
    // multiplayer client's open chat line, else the top window.
    void OnChar(int key, int repeat, int flags);
    // A key released: VK_SHIFT / VK_CONTROL / VK_MENU clear their modifier flags; then the shown
    // console's OnKeyUp, else the top window.
    void OnKeyUp(int key, int repeat, int flags);
    // Closes every window, top first.
    void CloseAll();
    // CloseAll when asked for.
    void CloseAllPending();
    // Every frame from the main loop: a pending close-all, the backdrop (modes 2-4), the top window's
    // draw, then the fade over it all.
    int Draw();
    // Rebirth-patched (WideScreen behind its flag). What is drawn over the scene with GDI and blits after
    // EndScene: the top window's surfaces and tooltips, the status surface, a backdrop asked for (mode 1
    // also keeps a 256 x 192 copy and greys it into mode 2; mode 3 puts the old one aside into mode 4), the
    // client's chat overlay, the frame rate and the console.
    void DrawOverlays();
    // Rebirth-patched (WideScreen behind its flag). Makes the three "Times New Roman" fonts sized to the
    // screen width, activates the game language's keyboard layout (else English, else the one it began
    // with), reads the key bindings and config\tutorial.ini's help topics, and makes the console, debug
    // panel, chat overlay and fade.
    void Create(CTextureDevice* device);

    int width_;                      // screen width
    int height_;                     // screen height
    CTextureDevice* renderer_;       // the device
    void* mouseCapture_;             // input side that took the button press
    std::uint8_t unused_10[0x14 - 0x10];
    console::ConsoleWindow* console_;
    CScreenFade* fade_;
    CChatOverlay* root_;             // the chat overlay; updated after the top window every frame
    CUiSurface* fpsSurface_;
    int fpsSpare_;                   // 0 from Create with the frame rate fields; nothing reads it
    DWORD fpsTime_;
    int fpsFrames_;
    // The windows draw, redraw their surfaces and update their tooltips only while this is set.
    bool drawing_;
    std::uint8_t unused_31[0x34 - 0x31];
    std::vector<CUiWindow*> stack_;   // the open windows, the top one last
    CUiWindow* queuedWindow_;        // activated by the next frame update
    bool closeAll_;
    std::uint8_t unused_45[0x48 - 0x45];
    std::uint32_t pendingValue_;
    bool pendingFlag_;
    std::uint8_t unused_4D[0x50 - 0x4D];
    int backdropMode_;               // the backdrop is drawn in modes 2, 3 and 4 (PushWindow sets 3 / 1)
    CUiSurface* backdrop_;
    CUiSurface* secondBackdrop_;
    void* block5C_;                  // freed by the shutdown with operator delete (writer not identified)
    std::uint8_t serverFlag_;        // the server flag kept for the first window (PushWindow)
    std::uint8_t unused_61[0x64 - 0x61];
    CUiSurface* statusSurface_;      // only while ShowStatusText runs
    int mouseX_;
    int mouseY_;
    // The three interface fonts (Create: "Times New Roman", sized with the screen width).
    mfc::impl::CFont* fonts_[3];
    // Set by the key-down handler from the scan codes, cleared by key-up and when the windows change.
    bool ctrlDown_;
    std::uint8_t shiftDown_;
    bool altDown_;
    // The left press is a double click (a spell or quick item clicked twice is used at once).
    bool doubleClick_;
    bool rightDown_;
    // The top window is asked every frame until it agrees ("autosave" sets it).
    bool pollTop_;
    std::uint8_t unused_82[0x84 - 0x82];
    std::list<std::string> messages_;   // queued message texts (ShowMessage takes the first)
    bool disconnected_;
    // The state saved with a game (LoadState), cleared by ResetSavedState. The HUD panels keep theirs
    // here every frame.
    std::uint8_t hudPanelsIn_[3];    // [0] the unit panel, [1] the top panel, [2] the map
    int hudBodyPart_;                // CHudUnitPanel::bodyPart_
    int hudTopPage_;                 // CHudTopPanel::page_
    int fastGame_;                   // the fast game speed (CHudClock::fast_)
    // Help topics seen: name -> times (or the page the help window was closed on; ui_help_window.cpp);
    // config\tutorial.ini "name count" lines, read by Create, written by Shutdown, counted by OpenHelp.
    std::map<std::string, int, str::MbsLess> helpTopics_;
    std::string messageText_;
    int messageFlag_;                // the message window's page, set when it closes
    std::string savedText_;         // the current quest (OnQuestVariable); saved from version 0x5F

    // Clears the modifier and button flags and deactivates the top window's input side.
    void ResetInput();
    // The main loop's exit: closes every window, writes the help topics to config\tutorial.ini, saves the
    // key bindings and frees what Create made (the queued window and the fade pointers are not cleared).
    void Shutdown();
    // The global interface's destructor, run at exit.
    void DestroyBody();

    // Writes what LoadState reads, with an empty obsolete list (count 0).
    void SaveState(CStream* stream);

    void ResetSavedState();
};
static_assert(offsetof(CInterface3D, mouseCapture_) == 0x0C);
static_assert(offsetof(CInterface3D, ctrlDown_) == 0x7C && offsetof(CInterface3D, altDown_) == 0x7E);
static_assert(offsetof(CInterface3D, doubleClick_) == 0x7F);
static_assert(offsetof(CInterface3D, stack_) == 0x34);

static_assert(offsetof(CInterface3D, pendingValue_) == 0x48);
static_assert(offsetof(CInterface3D, mouseX_) == 0x68);
static_assert(offsetof(CInterface3D, console_) == 0x14);
static_assert(offsetof(CInterface3D, root_) == 0x1C);
static_assert(offsetof(CInterface3D, drawing_) == 0x30);
static_assert(offsetof(CInterface3D, closeAll_) == 0x44);
static_assert(offsetof(CInterface3D, fpsSurface_) == 0x20 && offsetof(CInterface3D, queuedWindow_) == 0x40);
static_assert(offsetof(CInterface3D, statusSurface_) == 0x64);
static_assert(offsetof(CInterface3D, block5C_) == 0x5C && offsetof(CInterface3D, fonts_) == 0x70);

extern lib::StaticObject<CInterface3D> g_interface;

template <typename T>
using UiPtrArray = lib::AlignedArray<T*>;

class CUiRegion;
struct CUiTooltip;

struct UiPoint {
    int x;
    int y;
};

// A clickable area of a window, created by CUiWindow::AddRegion: a rectangle, optionally narrowed to a
// convex polygon, and an optional tooltip.
class CUiRegion {
public:
    BOOL HitTest(int x, int y) const;
    BOOL HasActiveTip() const;
    // Also enables the region.
    void SetRect(int left, int top, int right, int bottom);
    // Adds a polygon vertex, growing the bounds; three vertices enable the region.
    void AddPoint(int x, int y);
    void OnMouseMove(int x, int y);
    // The destructor body (fields left as they are).
    void ReleaseContents();
    void SetTipText(const char* text);
    // Replaces the tooltip by one with the text "tip <id>" (up to a '#'; the key itself when there is no
    // such text) and (a, b); id 0 gives an empty tooltip with (1, 0).
    void SetTip(int textId, int a, int b);
    void SetTipString(const char* text, int a, int b);
    void SetRectFrom(RECT rect);

    CInterface3D* interface_;
    std::vector<UiPoint> points_;   // polygon vertices
    RECT bounds_;
    bool rectOnly_;
    bool enabled_;
    std::uint8_t unused_26[2];
    int offsetX_;                    // subtracted from tested points
    int offsetY_;
    CUiTooltip* tip_;
};

// An off-screen DirectDraw surface a window draws into; no destructor: the owners destroy it directly.
class CUiSurface {
public:
    // A surface of the layout rectangle with format `format`, scaled to the screen when the low byte of
    // `flag` is set.
    CUiSurface(CInterface3D* ui, int left, int top, int right, int bottom, int format, int flag);
    // Over the layout rectangle `rect` (always scaled) with format `format`.
    CUiSurface(CInterface3D* ui, const RECT* rect, int format);
    // Releases the kept bytes and the DirectDraw surface. Not virtual (a window's surface list destroys a
    // scrolling surface as a plain one).
    ~CUiSurface();
    // The game's heap (null without memory: then no constructor runs).
    static void* operator new(std::size_t size) noexcept;
    static void operator delete(void* block);
    // Onto the render target, when shown.
    virtual void Blit();

    std::uint32_t PixelAt(int x, int y);
    // Layout coordinates.
    void SetRect(int left, int top, int right, int bottom);
    void MoveTo(int x, int y);
    // Word-wraps `text` to the width of `rect` (layout coordinates; breaks at spaces and '\n') and draws up
    // to `maxLines` lines one rect height apart with DrawText. The DC must be acquired. Returns the line
    // count.
    int DrawWrappedText(const char* text, const RECT* rect, int maxLines, int font, COLORREF colour, int format,
                        int flag);
    // Rebirth-patched (WideScreen behind its flag). `rect` in layout coordinates (empty: the whole surface),
    // grown by two pixels once scaled; `shadow` first draws the text in 0x080808 one pixel right and down.
    // The DC must be held. Returns the drawn height in layout units, 0 without a surface.
    int DrawTextIn(std::string_view text, const RECT* rect, int font, COLORREF colour, UINT format, int shadow);
    mfc::impl::CDC* GetDC();
    void ReleaseDC();
    // DrawTextIn's line of text (Rebirth-patched).
    int DrawLine(std::string_view text, RECT* rect, int font, std::uint32_t colour, int format, int shadow);
    // Sets (`frozen` nonzero) or clears the shown flag: setting it puts kept contents back into a surface;
    // clearing it with `restore` keeps the contents in memory and releases the surface.
    void Freeze(int frozen, int restore);
    // SwapContents: `restore` 0 moves the surface's pixels into kept_ and releases the surface; 1 makes the
    // surface again and copies them back. Rgb565Copy returns a new[] block of width * height 5:6:5 pixels
    // (the surface stretched). Greyscale: (2 r + 4 g + b) / 16 per channel.
    void SwapContents(int restore);
    void CaptureScreen();
    std::uint16_t* Rgb565Copy(int width, int height);
    void Greyscale();

    bool Shown() const { return (flags_ & kShown) != 0; }
    static constexpr std::uint32_t kShown = 4;   // set and cleared by Freeze

    void CallSwapContents(int restore);
    // With a surface (and its DC held): a one-pixel frame of `colour` around `rect` (layout coordinates;
    // empty: the whole surface).
    void FrameRect(const RECT* rect, COLORREF colour);
    // The whole surface; Clear fills with background_.
    void Fill(COLORREF colour);
    void Clear();

    static constexpr std::uint32_t kFromPicture = 1;   // flags_: made from an .mmp picture

    CInterface3D* interface_;
    HDC dc_;                         // while the DC is held
    std::uint32_t flags_;            // the constructors store the format here, then set kShown
    CDXSurface* holder_;             // the DirectDraw surface's holder (null while swapped out)
    int width_;
    int height_;
    std::uint8_t* kept_;             // the contents kept in memory while swapped out
    std::uint32_t keptSize_;
    RECT rect_;                      // screen coordinates
    COLORREF background_;            // the colour Clear fills with (0 from the constructors)
};
static_assert(offsetof(CUiSurface, interface_) == 0x04);
static_assert(offsetof(CUiSurface, rect_) == 0x24);
static_assert(offsetof(CUiSurface, flags_) == 0x0C);
static_assert(offsetof(CUiSurface, holder_) == 0x10);
static_assert(offsetof(CUiSurface, width_) == 0x14);
static_assert(offsetof(CUiSurface, height_) == 0x18);
static_assert(offsetof(CUiSurface, kept_) == 0x1C);
static_assert(offsetof(CUiSurface, keptSize_) == 0x20);
static_assert(offsetof(CUiSurface, background_) == 0x34);
static_assert(sizeof(CUiSurface) == 0x38);
// The surface's CDC (a temporary MFC wrapper; null without a DirectDraw surface) and the matching release.
mfc::impl::CDC* UiSurfaceGetDC(CUiSurface* surface);
void UiSurfaceReleaseDC(CUiSurface* surface);
inline mfc::impl::CDC* CUiSurface::GetDC() { return UiSurfaceGetDC(this); }
inline void CUiSurface::ReleaseDC() { UiSurfaceReleaseDC(this); }

// The memory totals the window surfaces keep: bytes kept while swapped out and the surfaces' height * pitch.
inline std::uint32_t g_uiSurfaceKeptBytes = /*007C7E74*/ {};
inline std::uint32_t g_uiSurfaceBytes = /*007C7E78*/ {};
inline std::uint32_t& UiSurfaceKeptBytes() { return g_uiSurfaceKeptBytes; }
inline std::uint32_t& UiSurfaceBytes() { return g_uiSurfaceBytes; }

// A 3D model shown in a window (an inventory item, a portrait).
class CUiModel {
public:
    void SetScreenPosition(float x, float y, float depth);
    void ReleaseObject();
    // Draws the object in `pass` in white at the interface's animation clock (timeGetTime / 67), with
    // object flag 0x800 set while it draws.
    void Draw(CSceneRenderer* pass);
    // A new map object (class 0x33, id 0x88CA6C00) of the client world's map object manager, referenced,
    // flag 0x4000000, at (0, 0, 3), with the figure of model `name` (all parts), its textures and
    // animations, and texture `from` swapped for `to`.
    void Load(const char* name, const char* from, const char* to);
    void ShowAllParts(int show);
    void ShowPart(const char* name, int show);
    // Plays skill animation `animation` + 1.
    void Play(int animation);
    // Plays the figure's rest pose (the object manager's object named like the figure).
    void Stop();
    CObject3D* Figure() const { return object_; }

    CObject3D* object_;
};
static_assert(sizeof(CUiModel) == 4);

// An input side: a window's own (CUiWindow's second base) or a separate handler a container keeps in its
// list (e.g. the battle camera's keys, CCameraInput). The interface hands each event to the top window's
// side (arguments: buttons / x, y in screen pixels; key, repeat, scan code and flags; wheel: key flags,
// delta, x, y; only the wheel's result is read). The base does nothing, except that a double click is a
// press and the wheel is unused.
struct UiInputSide {
    enum Slot : int {
        kEventDeactivate = 1,
        kEventLeftDown = 2,
        kEventLeftUp = 3,
        kEventMouseMove = 4,
        kEventRightDown = 5,
        kEventRightUp = 6,
        kEventLeftDouble = 7,
        kEventRightDouble = 8,
        kEventKeyDown = 9,
        kEventChar = 10,
        kEventKeyUp = 11,
        kEventWheel = 12,
    };
    // The handled flag is left as it is (a dispatcher clears it before each event).
    explicit UiInputSide(CInterface3D* ui) : interface_(ui) {}
    virtual ~UiInputSide();
    virtual void Deactivate() {}
    virtual int LeftDown(int, int, int) { return 0; }
    virtual int LeftUp(int, int, int) { return 0; }
    virtual int MouseMove(int, int, int) { return 0; }
    virtual int RightDown(int, int, int) { return 0; }
    virtual int RightUp(int, int, int) { return 0; }
    virtual int LeftDouble(int buttons, int x, int y);
    virtual int RightDouble(int buttons, int x, int y);
    virtual int KeyDown(int, int, int) { return 0; }
    virtual int Char(int, int, int) { return 0; }
    virtual int KeyUp(int, int, int) { return 0; }
    virtual int Wheel(int flags, int delta, int x, int y);
    // Events 2-11 by number (the containers' dispatch).
    int Event(int slot, int a, int b, int c);

    // The game's heap; `new` gives null without memory and then constructs nothing.
    static void* operator new(std::size_t size) noexcept;
    static void* operator new(std::size_t, void* place) noexcept { return place; }
    static void operator delete(void* block);
    static void operator delete(void*, void*) noexcept {}

    CInterface3D* interface_;
    bool handled_;                   // set by a handler that used the event
};

// The extra input side whose handlers do nothing: the battle screen's, the briefing's, the main menu's and
// the view screens'.
struct UiInertInput final : UiInputSide {
    explicit UiInertInput(CInterface3D* ui) : UiInputSide(ui) {}
    ~UiInertInput() override;
    int KeyDown(int, int, int) override { return 0; }
    int KeyUp(int, int, int) override { return 0; }
};

// The window's part below its input side: the engine's transform object and 0x40 bytes the window's code
// never touches.
class CUiWindowBase : public CTransformObject {
protected:
    std::uint8_t unused_054[0x94 - 0x54];
};

// Base of the interface windows; its input side is its second base.
class CUiWindow : public CUiWindowBase, public UiInputSide {
public:
    explicit CUiWindow(CInterface3D* ui);
    ~CUiWindow() override;
    using CEngineObject::operator new;
    using CEngineObject::operator delete;

    // The class name is "Sorry, no other way." and the id 0x45.
    int Create() override;
    void Destroy() override {}
    const char* GetClassName() override;
    int GetClassId() override;
    virtual void Build() {}
    virtual void Show(int show) { ShowSurfaces(show); }
    virtual BOOL Draw(CSceneRenderer* pass) { return DrawBase(pass); }
    virtual void DrawChild(CSceneRenderer* pass) { UpdateRegionTips(pass); }
    virtual void Redraw() { RedrawBase(); }
    virtual void RedrawTips() { RedrawRegionTips(); }
    virtual void Update() {}


    // The first region containing the screen point, or -1.
    int RegionAt(int x, int y) const;
    BOOL ContainsPoint(int x, int y) const;
    void UpdateRegionTips(CSceneRenderer* unused);

    void SetEnabled(int enabled, int notify);

    // Takes a reference (null is ignored).
    void AddObject(CTexture* texture);
    void RemoveObject(CTexture* texture);
    void RemoveObjectAt(int index);
    void ClearObjects();

    BOOL RemoveSceneObject(CEngineObject* object);
    // Replaces scene object `object` (released) at its place with object `name` of the model manager,
    // given the window's texture 0 and the old one's position, orientation, texture and scale; returns the
    // new one or null.
    CEngineObject* ReplaceSceneObject(CEngineObject* object, const char* name);
    void RemoveSceneObjectAt(int index);
    void ClearSceneObjects();

    CUiSurface* AddSurface(CUiSurface* surface);
    void RemoveSurfaceAt(int index);
    void ClearSurfaces();

    CUiRegion* AddRegion();
    void RemoveRegionAt(int index);
    void ClearRegions();

    CUiModel* AddModel();

    UiInputSide* Input() { return this; }
    // Takes the mouse for the next button-up (what every button-down handler does).
    void CaptureMouse() { interface_->mouseCapture_ = Input(); }
    // Button-up handlers act only when they took the mouse; they release it first.
    bool ReleaseMouse() {
        if (interface_->mouseCapture_ != Input()) return false;
        interface_->mouseCapture_ = nullptr;
        return true;
    }
    // The scene objects, then the models; returns 1.
    BOOL DrawBase(CSceneRenderer* pass);
    // The camp's and the multiplayer hero screen's hero preview, turned by `turn` radians about y; `wide`'s
    // low byte clears the depth under six camp layout rectangles, else one. The hero's transform is restored.
    void DrawHeroPreview(CSceneRenderer* pass, CUnit* hero, float turn, float depth, int wide);
    // Copies the top-left width x height of image "<name>.mmp" (decoded as imageWidth x imageHeight) into
    // `texture`'s 256-wide 1:5:5:5 surface at (x, y), or white without such an image. Returns whether the
    // image exists (0 also when the lock fails).
    int CopyImage(CTexture* texture, const char* name, int imageWidth, int imageHeight, int width, int height,
                  int x, int y, int unused1, int unused2);
    // The texture manager's texture `name` of `kind` (found or made), loaded (or from `directory`) and
    // appended to the referenced objects with one more reference; null when either step fails (a texture
    // made but not loaded is kept by the manager).
    CTexture* LoadTexture(const char* name, int kind, const char* directory);
    // A new CPU-filled texture (flag 0x40000) kept with the referenced objects; null when none is made.
    CTexture* CreateTexture(int width, int height, int kind);
    // `show` 0: every surface keeps its contents in memory and lets its DirectDraw surface go; otherwise the
    // shown surfaces get theirs back.
    void ShowSurfaces(int show);
    // Object `name` of the model manager (null: a new CUiMesh) added to the scene objects with the black
    // material and the first referenced object as its texture; null when the manager makes none.
    CUiMesh* AddSprite(const char* name);
    void RedrawBase();
    void RedrawRegionTips();
    CUiMesh* SceneMesh(int index) const;   // defined after CUiMesh

    std::uint8_t spareA0_[0xD8 - 0xA0];       // zeroed by the constructor; nothing reads it
    RECT rect_;                               // layout coordinates
    int enabled_;
    CEngineObject* blackMaterial_;            // given to every sprite (AddSprite)
    CEngineObject* whiteMaterial_;
    CEngineObject* heldObject_;               // released by the destructor (writer not identified)
    UiPtrArray<CTexture> objects_;            // the referenced textures
    UiPtrArray<CEngineObject> sceneObjects_;
    UiPtrArray<CUiSurface> surfaces_;
    UiPtrArray<CUiRegion> regions_;
    UiPtrArray<CUiModel> models_;
};
// The verify build's interface census (tests/verify/ui_census.cpp) is told which window owns each
// surface: AddSurface (`added`), RemoveSurfaceAt, ClearSurfaces and the destructor. Null otherwise.
extern void (*g_uiSurfaceOwnerObserver)(const CUiWindow* window, const CUiSurface* surface, bool added);
// ... and when a whole-surface pixel helper runs on a surface (CaptureScreen, Rgb565Copy, Greyscale,
// PixelAt, SwapContents); `writes` for the ones that change the pixels; `caller` the stack slot of the
// helper's return address. Null otherwise.
extern void (*g_uiSurfacePixelObserver)(const CUiSurface* surface, const char* what, bool writes, void* const* caller);
static_assert(sizeof(CUiWindowBase) == 0x94);
static_assert(offsetof(CUiWindow, surfaces_) == 0x118 && offsetof(CUiWindow, regions_) == 0x128);
static_assert(sizeof(CUiWindow) == 0x148);

// The tooltip attached to a region; its surface is made while it is shown.
struct CUiTooltip : CUiWindow {
    CUiTooltip(CInterface3D* ui, CUiRegion* region, const char* text, int flag, int extra);
    ~CUiTooltip() override;
    // Null or empty: nothing. The size is not measured again.
    void SetText(const char* text);
    // The first flag_ lines in the title colour, the first line followed by "  ( <key> )" with keyName_.
    void DrawLines();
    // Rebirth-patched (WideScreen behind its flag). One step of the showing, mouse in layout coordinates;
    // the region has set state 1 (mouse inside) or 3 (left). State 1 waits for the delay (setting 20; 0:
    // never shown), then makes the surface at the mouse (state 2); 2 draws and becomes 4; 3 draws while the
    // timer is positive, then drops the surface (state 0).
    void OnMouseMove(int x, int y);
    BOOL Draw(CSceneRenderer* pass) override;
    void Redraw() override;
    bool Shown() const { return state_ != 1 && state_ != 0; }

    std::vector<std::string> lines_; // the text's lines (none for an empty text)
    int lineHeight_;                 // the last line's height: the rows' step
    int keyWidth_;                   // the first line with the key name, + 12
    int flag_;                       // the constructor's fourth argument: the title lines (1 almost everywhere)
    int extra_;                      // its fifth: a command whose key is shown
    const char* keyName_;            // that key's name, or null
    CUiRegion* region_;              // the region it belongs to
    int width_;                      // widest line + 12 (a text only)
    int height_;                     // the lines' heights + 8
    float timer_;                    // seconds in the current state
    int state_;                      // 0 idle, 1 hovered, 2 appearing, 3 disappearing, 4 shown
};
static_assert(offsetof(CUiTooltip, lines_) == 0x148);

// The panels of the item screens (camp, multiplayer hero screen, exchange): the description panel, hero
// equipment, quick strip, trade and repair grids, constructor pages and item strips, built on the blank
// panel. Their input sides share the left-button handler; ItemAt gives the item or help topic under the
// mouse.
class CUiItemPanel : public CUiWindow {
public:
    CUiItemPanel(CInterface3D* ui, CItemScreen* owner);
    // The item under the screen point (x, y), or null; a panel may set a help topic instead. The panels
    // without items of their own (description panel, statistics column, trade panel): nothing, `topic` 0.
    virtual CItem* ItemAt(int x, int y, int* topic);
    int LeftDown(int b, int x, int y) override { return OnLeftDown(b, x, y), 0; }

    // The item under (x, y) goes to the owner with mode_; without one, a help topic the panel named goes to
    // the owner's help handler. The event is not marked used.
    void OnLeftDown(int buttons, int x, int y);

    // Hands `item` to the owner with mode_.
    void OwnerTakeItem(CItem* item);
    void OwnerTopicClicked(int topic);

    int mode_;                       // the action mode handed to the owner (ItemCell::mode)
    CItemScreen* owner_;
};
static_assert(offsetof(CUiItemPanel, mode_) == 0x148);
static_assert(offsetof(CUiItemPanel, owner_) == 0x14C);


// Option 17 of the options window: the numbers flying up from units hit are shown.
inline int& ShowHitNumbersOption() { return options::TheOptions()->drawFlyingHp_; }
// The console's "debuginfo" rectangle.
strmap::CDebugPanel* DebugInfoPanel();


// The in-game key bindings.
inline const signed char* KeyActions() { return g_keyActions; }
inline signed char* KeyActionTable() { return g_keyActions; }
constexpr int kKeyActionCount = 0x200;


// --- Battle screen and its HUD panels ---
// The battle screen owns a row of panel windows and forwards mouse events to the panel whose rectangle
// holds the point. Coordinates are the 800x600 layout (Rebirth moves some panels).

class CHudQuickItems;
class CHudSpellColumn;
class CHudActionColumn;
class CHudWeaponBar;
class CHudPortraits;
class CHudUnitPanel;
class CBattleScreen;
class CUiScrollText;

using UiDwordArray = mfc::CArray<std::uint32_t>;

// The interface code's calls on the device's IDirect3DDevice7 (ui_d3d_device.cpp).
namespace uidevice {
long Clear(CTextureDevice* device, DWORD count, void* rects, DWORD flags, DWORD colour, float z, DWORD stencil);
long GetTextureStageState(CTextureDevice* device, DWORD stage, DWORD type, std::uint32_t* value);
long SetTextureStageState(CTextureDevice* device, DWORD stage, DWORD type, DWORD value);
long SetRenderState(CTextureDevice* device, std::uint32_t state, std::uint32_t value);
long GetRenderState(CTextureDevice* device, std::uint32_t state, std::uint32_t* value);
long ApplyStateBlock(CTextureDevice* device, std::uint32_t handle);
// Whether the device's depth buffer has a stencil.
bool HasStencil(const CTextureDevice* device);
} // namespace uidevice

// Direct3D 7 render states the HUD sets around its drawing.
constexpr int kRenderStateAlphaTest = 15;    // D3DRENDERSTATE_ALPHATESTENABLE
constexpr int kRenderStateAlphaRef = 24;     // D3DRENDERSTATE_ALPHAREF
constexpr int kRenderStateSpecular = 29;     // D3DRENDERSTATE_SPECULARENABLE

// Weapon bar (four 40x80 slots at (80..240, 520..600)).
class CHudWeaponBar : public CUiWindow {
public:
    void OnMouseDown(int buttons, int x, int y);
    void OnMouseUp(int buttons, int x, int y);     // selects a weapon
    void OnMouseMove(int buttons, int x, int y);
    int LeftDown(int b, int x, int y) override { return OnMouseDown(b, x, y), 0; }
    int LeftUp(int b, int x, int y) override { return OnMouseUp(b, x, y), 0; }
    int MouseMove(int b, int x, int y) override { return OnMouseMove(b, x, y), 0; }
    // Every frame: the highlight follows the unit's weapon.
    void Update() override;
    BOOL Draw(CSceneRenderer* pass) override;
    // Rebirth-patched (WideScreen behind its flag: the bar at the screen's left edge, from 80 - shift with
    // more regions up to x 240).
    CHudWeaponBar(CInterface3D* ui, CBattleScreen* owner);
    // WideScreen: with the left panels' offset.
    void Highlight(int index, int on);
    // The next slot's sprite: `image` copied into the bar's texture, the model `model` turned to face the
    // screen (a plain rectangle without one); shown not highlighted.
    void AddWeapon(const char* model, const char* image);
    // A sprite (Rebirth-patched) and a tooltip per weapon, then the unit's current weapon highlighted.
    void SetUnit(CUnit* unit);

    ~CHudWeaponBar() override;

    CBattleScreen* owner_;
    int count_;                      // slots in use
    int selected_;
    CUnit* unit_;
    std::vector<std::string> labels_;   // the tooltip's labels
};
static_assert(offsetof(CHudWeaponBar, unit_) == 0x154);

// Quick items (2 rows of four 50x50 slots at (520..720, 500..600)).
class CHudQuickItems : public CUiWindow {
public:
    void OnMouseDown(int buttons, int x, int y);
    void OnDoubleClick(int buttons, int x, int y);
    void OnMouseMove(int buttons, int x, int y);
    // A unit's quick item (bottom row) is picked or, with a double click, used at once; a player's item
    // (top row) starts mode 6, or cancels when it is the highlighted one.
    void OnMouseUp(int buttons, int x, int y);
    int LeftDown(int b, int x, int y) override { return OnMouseDown(b, x, y), 0; }
    int LeftUp(int b, int x, int y) override { return OnMouseUp(b, x, y), 0; }
    int MouseMove(int b, int x, int y) override { return OnMouseMove(b, x, y), 0; }
    int LeftDouble(int b, int x, int y) override { return OnDoubleClick(b, x, y), 0; }
    BOOL Draw(CSceneRenderer* pass) override;
    // Every frame: the tooltips of the slots that show an item.
    void Update() override;
    // The item's name and, for a wand (category 5) or a scroll (8) with a spell, its numbers after labels_.
    void ItemTip(unsigned index, std::string* text);
    // The unit's quick item `slot` (a click on it or its key): a wand or scroll becomes the pending action
    // (3 on a unit for a spell with flag 0x10000000, else 4 at a point); the highlighted slot again, or an
    // empty one, drops it.
    void Pick(int slot);
    // The unit's wand or scroll in `slot` used at once (a double click, or the key handler's Ctrl / Alt), as
    // CHudSpellColumn::ClickAlternate.
    void UseNow(int slot);
    // With `on` also selected_ = slot.
    void Highlight(int slot, int on);
    void HighlightAt(int slot, int on);
    // Item of the region at `index` (-1: empty). Not checked: index -1 reads the dword before the array.
    int RegionItem(int index) const {
        const int* items = regionItem_;   // declared below
        return items[index];
    }

    // Rebirth-patched (WideScreen behind its flag: the panel at the screen's right edge).
    CHudQuickItems(CInterface3D* ui, CBattleScreen* owner);
    // The next sprite (at most eight): `image` copied into the panel's texture, the model `model` turned
    // over (a plain rectangle without one).
    void AddItem(const char* model, const char* image);
    // The unit's four quick items in the bottom row (sprites Rebirth-patched); for the local player's unit
    // up to four of the player's 0x3009 items in the top row.
    void SetUnit(CUnit* unit);

    struct Entry {
        bool own;                    // one of the unit's quick items (else the player's)
        std::uint8_t unused_1[3];
        int id;                      // the item's id
        CItem* item;
    };

    ~CHudQuickItems() override;

    CBattleScreen* owner_;
    int shownCount_;                 // sprites added (the shown entries)
    int selected_;
    CUnit* unit_;
    mfc::CArray<Entry> entries_;
    int regionItem_[8];
    std::vector<std::string> labels_;   // the tooltip's labels
};
static_assert(offsetof(CHudQuickItems, entries_) == 0x158);
static_assert(offsetof(CHudQuickItems, regionItem_) == 0x16C);
static_assert(offsetof(CHudQuickItems, selected_) == 0x150);

// Spell column on the right (760..800 x 190..510).
class CHudSpellColumn : public CUiWindow {
public:
    void OnMouseDown(int buttons, int x, int y);
    void OnDoubleClick(int buttons, int x, int y);
    void OnMouseUp(int buttons, int x, int y);
    void OnMouseMove(int buttons, int x, int y);
    int LeftDown(int b, int x, int y) override { return OnMouseDown(b, x, y), 0; }
    int LeftUp(int b, int x, int y) override { return OnMouseUp(b, x, y), 0; }
    int MouseMove(int b, int x, int y) override { return OnMouseMove(b, x, y), 0; }
    int LeftDouble(int b, int x, int y) override { return OnDoubleClick(b, x, y), 0; }
    BOOL Draw(CSceneRenderer* pass) override;
    // Spell `index` (its button or key) becomes the pending action: 1 on a unit for a spell with flag
    // 0x10000000, else 2 at a point; the highlighted one again drops it.
    void Click(int index);
    // A double click (or the key handler's Ctrl / Alt) casts at once: on the unit itself, or an attack spell
    // on the nearest unit it sees (none: the "nomagic" sound). The pending action is dropped after.
    void ClickAlternate(int index);
    // selected_ = index either way.
    void Highlight(int index, int on);
    void HighlightAt(int index, int on);
    // selected_ = -1.
    void ClearHighlights();
    // Rebirth-patched (WideScreen behind its flag: the column at the screen's right edge).
    CHudSpellColumn(CInterface3D* ui, CBattleScreen* owner);
    // The next button at the bottom, the ones so far moved up a step; selected_ = its index.
    void AddSpell(const char* model, const char* image);
    // A sprite (Rebirth-patched) and a tooltip per spell slot that holds one.
    void SetUnit(CUnit* unit);

    ~CHudSpellColumn() override;

    CBattleScreen* owner_;
    int count_;                      // sprites added
    int selected_;
    CUnit* unit_;
    std::vector<std::string> labels_;   // the tooltip's labels
};
static_assert(offsetof(CHudSpellColumn, count_) == 0x14C);
static_assert(offsetof(CHudSpellColumn, selected_) == 0x150);

// Action column on the left (six 40x40 buttons at 0..40 x 270..510).
class CHudActionColumn : public CUiWindow {
public:
    void OnMouseDown(int buttons, int x, int y);
    void OnMouseUp(int buttons, int x, int y);
    void OnMouseMove(int buttons, int x, int y);
    int LeftDown(int b, int x, int y) override { return OnMouseDown(b, x, y), 0; }
    int LeftUp(int b, int x, int y) override { return OnMouseUp(b, x, y), 0; }
    int MouseMove(int b, int x, int y) override { return OnMouseMove(b, x, y), 0; }
    BOOL Draw(CSceneRenderer* pass) override;
    // Action button `index` (a click or its key) becomes the pending action: 8 (an order on an object) for
    // action 0 (science), else 7 (follow); the highlighted one again drops it.
    void Select(int index);
    void Highlight(int index, int on);
    void HighlightAt(int index, int on);
    void ClearHighlights();
    // Rebirth-patched (WideScreen behind its flag: the column at the screen's left edge).
    CHudActionColumn(CInterface3D* ui, CBattleScreen* owner);
    // The next button, above the ones so far; selected_ = its index.
    void AddAction(const char* model, const char* image, int action);
    // With `extra`'s low byte first a "skill0011" button; with a unit the science button.
    void SetUnit(CUnit* unit, int extra);

    ~CHudActionColumn() override;

    CBattleScreen* owner_;
    int count_;                      // buttons added
    int selected_;
    CUnit* unit_;
    UiDwordArray actions_;           // the buttons' actions (0 science, -1 follow)
    std::vector<std::string> labels_;   // the tooltip's labels
};
static_assert(offsetof(CHudActionColumn, count_) == 0x14C);
static_assert(offsetof(CHudActionColumn, selected_) == 0x150);
static_assert(offsetof(CHudActionColumn, actions_) == 0x158);

// A portrait's state, one per portrait: the last hit points seen, the face (rest orientation, current
// orientation and a random wobble), the face picture and model, and the slot's network player.
struct HudPortraitSlot {
    int lastHitPoints_;              // lowest hit points seen (a drop flashes the portrait)
    float flash_;                    // 1 after a hit, fading out
    Quaternion restA_;               // the face's rest orientation is restA_ * restB_
    Quaternion current_;             // the face's orientation, eased towards the rest one
    Quaternion restB_;
    int wobbleKind_;                 // 0 none, 1 nod (about x), 2 shake (y), 3 turn to target
    float wobbleTime_;               // starts negative (a delay)
    std::string faceName_;
    int faceState_;                  // picture shown (0 normal, 2 hit, 3 low mana, ...)
    int faceMode_;                   // 1 / 2: a timed picture, 0 none
    float faceTimer_;
    std::string modelPrefix_;       // the face model is prefix + build + suffix,
    std::string modelSuffix_;       // build "th", "me" or "fa"
    float build_;                    // the unit's build the model was chosen for
    Quaternion wobbleTarget_;
    float wobbleSpeed_;
    bool hasPlayer_;                 // a multiplayer player without a unit here (no bars)
    std::uint8_t unused_71[3];
    int playerId_;                   // that player's serial, else -1
};
static_assert(offsetof(HudPortraitSlot, wobbleKind_) == 0x38);
static_assert(offsetof(HudPortraitSlot, faceName_) == 0x40);

// Party portraits (250..550 x 440..600).
class CHudPortraits : public CUiWindow {
public:
    void OnMouseDown(int buttons, int x, int y);
    void OnMouseUp(int buttons, int x, int y);     // selects the unit
    void OnMouseMove(int buttons, int x, int y);   // shows the unit / sets the cursor
    int LeftDown(int b, int x, int y) override { return OnMouseDown(b, x, y), 0; }
    int LeftUp(int b, int x, int y) override { return OnMouseUp(b, x, y), 0; }
    int MouseMove(int b, int x, int y) override { return OnMouseMove(b, x, y), 0; }
    // Every frame: the bars, the hit flash, the face picture and model, the wobble and the selection marker.
    void Update() override;
    // Face picture `state` (the face name plus "a" 1, "c" 2, "b" 3; the plain name without such an image)
    // into quarter index % 4 of texture objects_[index / 4 + 1].
    void ShowFace(int index, int state);
    // One portrait per party unit and, in a multiplayer client, one per player without a live unit.
    void Rebuild(int wide);
    // 56 wide (multiplayer: 40, or 56 with the low byte of `wide` for the first). The low byte of
    // `hasPlayer` marks a player's portrait (half tint, no bars).
    void AddPortrait(const char* model, const char* picture, int wide, int hasPlayer);

    CHudPortraits(CInterface3D* ui, CBattleScreen* owner);
    ~CHudPortraits() override;

    CBattleScreen* owner_;
    int count_;
    std::uint8_t unused_150[0x158 - 0x150];
    // One per portrait (appended, ranges erased: with recordvec::HudPortraitSlotElement).
    stlp::vector<HudPortraitSlot> slots_;
};
static_assert(offsetof(CHudPortraits, slots_) == 0x158);
static_assert(sizeof(CHudPortraits) == 0x164);

#undef GetClassName
// The figure factory's class 0x43 ("CI3DFigure"): a plain figure mesh with the sprite's fields.
class CUiMesh : public CPlainFigureMesh {
public:
    CUiMesh();
    ~CUiMesh() override;
    int Create() override;
    void Destroy() override;
    const char* GetClassName() override;
    int GetClassId() override;

    // The quads get z 0 and w 1, the vertices a zero normal index.
    void SetQuadCount(int count);
    void SetVertexCount(int count);
    void SetIndexCount(int count);
    void SetUvCount(int count);
    // The sprite fills the figure mesh's arrays itself: vertices_ (a quad is one VertexPacket, its four
    // corners x, y, z, w), corners_ (MeshCorner per vertex), texCoords_, indices_.
    // Where Place put the sprite (the layout point and the depth; each stored as float).
    void GetPlacement(float* x, float* y, float* depth) const;
    // Puts the sprite at layout point (x, y) at `depth` in front of the interface camera. Rebirth patches
    // it; PlaceBody is the body.
    void Place(float x, float y, float depth);
    // Rebirth-patched: SetRectangleVanilla.
    void SetRectangle(float depth, float scale, float left, float top, float right, float bottom, int u0, int v0,
                      int u1, int v1);
    // The shapes' vanilla bodies, Rebirth-patched; with Rebirth's WideScreen every shape's layout x is moved
    // by the panel offset. A rectangle: texels (u0, v0)-(u1, v1), v upwards, both u one less when u0 > u1,
    // half a texel inwards; position_ the layout centre at `depth`.
    void SetRectangleVanilla(float depth, float scale, float left, float top, float right, float bottom, int u0, int v0,
                             int u1, int v1);
    // A grid of columns x rows cells, each showing the whole picture; position_ the top left corner.
    void SetGridVanilla(float depth, float scale, float left, float top, float right, float bottom, int u0, int v0, int u1,
                        int v1, int columns, int rows);
    // A frame `border` wide inside the rectangle, each side showing the whole picture.
    void SetFrame(float depth, float scale, float left, float top, float right, float bottom, float border, int u0,
                         int v0, int u1, int v1);
    // The first layout point is position_, the other two relative to it in units of `scale`.
    void SetTriangle(float depth, float scale, float x0, float y0, float x1, float y1, float x2, float y2, float u0,
                            float v0, float u1, float v1, float u2, float v2);
    // The shape builders (ui_mesh_build.cpp; docs/notes/ui.md, "Sprite shapes"): each fills the sprite from
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
    // `depth` copied as bits. With Rebirth's WideScreen x is moved by the panel offset first; addon's other
    // code calls it too.
    void PlaceBody(float x, float y, float depth);
    // Copied as bits.
    void SetScale(float scale);
    // The material and the texture forgotten (not released), then the figure's own Destroy.
    void DestroySprite();
    void SetScaleYBits(std::uint32_t bits);
    float GetScaleX() const;
    // When visible; returns 1.
    int Draw(CSceneRenderer* renderer);
    int CreateSprite();

    // CFigureMesh's fields as a sprite uses them: flags_ 0x100 draws in flatColour_ (ARGB), flatSpecular_
    // the HUD buttons' highlight (0xFF404040 / 0xFF000000), textureOffsetU_ / V_ a frame of the sprite
    // sheet, localRotation_ the HUD face models' turn.
    int visible_;
    float tint_[3];                  // colour factors (the HUD: 1 selected, 0.5 not)
    float tintSpare_[3];             // nothing reads it
    // Applied after the placement; the diagonal is SetScale's scale.
    Matrix4 scaleMatrix_;
    float& ScaleX() { return scaleMatrix_.m[0]; }
    float& ScaleY() { return scaleMatrix_.m[5]; }
    float& ScaleZ() { return scaleMatrix_.m[10]; }
    CEngineObject* material_;        // the window's black material
    CTexture* texture_;              // one of the window's referenced textures
};
static_assert(offsetof(CUiMesh, flatSpecular_) == 0xD8 && offsetof(CUiMesh, textureOffsetU_) == 0xCC);

inline CUiMesh* AsUiMesh(CEngineObject* object) { return static_cast<CUiMesh*>(object); }
inline CUiMesh* CUiWindow::SceneMesh(int index) const { return AsUiMesh(sceneObjects_.data[index]); }

struct UiPointF {
    float x;
    float y;
};
using UiPointArray = std::vector<UiPointF>;

// Map (615..800 x 0..165). Regions: 0 the sling button, 1 the round map, 2 the map area, 3 and 4 the two
// view buttons, 5 a button above them, 6 the whole panel.
class CHudMap : public CUiWindow {
public:
    CHudMap(CInterface3D* ui, CBattleScreen* owner);
    void OnMouseDown(int buttons, int x, int y);   // the view buttons
    // Rebirth-patched (WideScreen behind its flag: the map's centre at 700 + shift). A held view button is
    // let go; otherwise region 0 starts the slide, 1 / 2 focus the camera on the map point (inside the
    // world), 5 is "camera north".
    void OnMouseUp(int buttons, int x, int y);
    void OnMouseMove(int buttons, int x, int y);
    // Over region 0 the wheel zooms the map by 2.5^(delta / 1200).
    BOOL OnWheel(int buttons, int delta, int x, int y);
    int LeftDown(int b, int x, int y) override { return OnMouseDown(b, x, y), 0; }
    int LeftUp(int b, int x, int y) override { return OnMouseUp(b, x, y), 0; }
    int MouseMove(int b, int x, int y) override { return OnMouseMove(b, x, y), 0; }
    int Wheel(int f, int d, int x, int y) override { return OnWheel(f, d, x, y); }
    // Every frame: the marker mesh (scene object 2) of the units, marked objects, zone exits and players.
    void BuildMarkers();
    // Every frame, before BuildMarkers: follows the camera and builds the visible-area mesh (scene object 1)
    // clipped to the world, and the world-bounds quad (scene object 0).
    void UpdateView();
    ~CHudMap() override;
    // Rebirth-patched (WideScreen behind its flag). The parts at the current slide offset.
    void Layout();
    void Update() override { UpdateVanilla(); }
    // Rebirth-patched (WideScreen behind its flag). Every frame: the zoom (2.5 times a second while a view
    // button is held), the view and markers, the slide (0.3 s for the whole way, eased) and the "camera
    // north" time.
    void UpdateVanilla();
    // Rebirth-patched (WideScreen behind its flag: the panel at the screen's right edge). The world's extent
    // and picture come from the local player's zone record (32 by 32 and "battle00" without one); `slid`
    // starts the slide at its far end.
    void SetupVanilla(float zoom, std::uint8_t slid);

    CBattleScreen* owner_;
    float centreX_;                  // world point at the map's centre (the camera's)
    float centreY_;
    float facingX_;                  // view direction (normalised by the camera follow)
    float facingY_;
    float worldWidth_;               // the zone's extent (clipping rectangle 0..width)
    float worldHeight_;
    float textureSize_;              // world units per map texture (texture coordinates)
    float scale_;                    // world units to map units
    float zoom_;                     // 1 .. zoomMax_ (the view buttons change it)
    float zoomMax_;
    float halfSide_;                 // 80: half the map's side in layout units (a click's offset over it)
    UiPointArray viewPolygon_;       // the round map's outline in map units (the disc's corners)
    UiPointArray visibleArea_;       // the outline in world units, clipped to the world
    int sliding_;                    // set when the sling button (or its key) starts the slide
    int offset_;                     // current slide offset in pixels (to the right)
    int travel_;                     // full slide distance
    float slide_;                    // 0..1: how far the panel has slid (offset_ = travel_ eased)
    float slideDirection_;           // +1 / -1
    int view_;                       // 1 or 2 while a view button is held (scene objects 6 and 7), else 0
    float northTime_;                // 0.5 when "camera north" turns the camera, then counts down
};
static_assert(offsetof(CHudMap, centreX_) == 0x14C);
static_assert(offsetof(CHudMap, scale_) == 0x168);
static_assert(offsetof(CHudMap, zoomMax_) == 0x170);

// Unit panel (0..185 x 0..225). Regions: 0 the sling button, 1-4 the body parts, 5 the whole panel.
class CHudUnitPanel : public CUiWindow {
public:
    CHudUnitPanel(CInterface3D* ui, CBattleScreen* owner);
    void DrawUnitInfo(CUnit* unitIn);
    // The mana bar only for a party unit.
    void ShowHealth(CUnit* unit);
    // The name of each present part and, for state 3, its durability bar.
    void ShowParts(CUnit* unit);
    void OnMouseDown(int buttons, int x, int y);
    void OnMouseUp(int buttons, int x, int y);
    void OnMouseMove(int buttons, int x, int y);
    BOOL OnWheel(int buttons, int delta, int x, int y);
    int LeftDown(int b, int x, int y) override { return OnMouseDown(b, x, y), 0; }
    int LeftUp(int b, int x, int y) override { return OnMouseUp(b, x, y), 0; }
    int MouseMove(int b, int x, int y) override { return OnMouseMove(b, x, y), 0; }
    int Wheel(int f, int d, int x, int y) override { return OnWheel(f, d, x, y); }
    // The battle screen's keys call it after setting bodyPart_.
    void ShowBodyPart();
    // The first six effects on `unit` (text, time left and picture); scene object 9 shows when there are more.
    void ShowEffects(CUnit* unit);
    ~CHudUnitPanel() override;
    // Rebirth-patched (WideScreen behind its flag). The parts at the current slide offset.
    void Layout();
    void Update() override { UpdateVanilla(); }
    // Rebirth-patched (WideScreen behind its flag). Every frame: the slide (as the map's); a dead hover unit
    // is forgotten.
    void UpdateVanilla();
    // Rebirth-patched (WideScreen behind its flag: the panel at the screen's left edge, rectangle (-133,
    // 0)-(52, 225) and sliding_ 0, addon's constants). `slid` starts the panel at the far end of its slide.
    void SetupVanilla(int bodyPart, std::uint8_t slid);
    // Rebirth-patched (WideScreen behind its flag). The hover unit, else the selected one, while the panel
    // is not slid away: its model (not on info page 2) over a depth buffer cleared to 0 around the panel,
    // its name, bars and texts. Returns 1.
    BOOL DrawVanilla(CSceneRenderer* pass);
    BOOL Draw(CSceneRenderer* pass) override { return DrawVanilla(pass); }

    CBattleScreen* owner_;
    CUnit* shownUnit_;               // the first selected unit (RefreshSelection), or null
    CUnit* hoverUnit_;               // the unit under the mouse (HoverUnit), or null
    // The model's view for the selected unit [0] and the hover unit [1], from the units' prototypes (the
    // battle screen copies them as dwords).
    float viewHeight_[2];            // how far up the panel the model stands, in units of its size
    float viewSize_[2];              // the model's size: it is drawn 1 / size (times its animation's ratio)
    std::string strings_[2];        // the selected and the hover unit's names
    int sliding_;                    // set when the sling button starts the slide
    int offset_;                     // current slide offset in pixels (to the left)
    int travel_;                     // full slide distance
    float slide_;                    // 0..1: how far the panel has slid (offset_ = travel_ eased)
    float slideDirection_;           // +1 / -1
    int bodyPart_;                   // selected body part 0-3 (scene objects 3-6)
    int effectIcons_[6];             // the spell pictures in the icon texture's six places
    std::vector<std::string> texts_; // "infounit_0".."infounit_17": the info lines' labels
    std::vector<std::string> partLabels_;   // four: the shown names of "skull", "torso", "leg", "arm"
};
static_assert(offsetof(CHudUnitPanel, owner_) == 0x148 && offsetof(CHudUnitPanel, shownUnit_) == 0x14C);
static_assert(offsetof(CHudUnitPanel, hoverUnit_) == 0x150 && offsetof(CHudUnitPanel, strings_) == 0x164);

// Game clock (720..800 x 520..600).
class CHudClock : public CUiWindow {
public:
    CHudClock(CInterface3D* ui, CBattleScreen* owner) : CUiWindow(ui), owner_(owner) {}
    void OnMouseDown(int buttons, int x, int y);
    // Rebirth's MPSpeedControl behind its flag. Region 0 toggles the pause; 1 and 2 set the normal / fast
    // speed (not on a network client: update period 55 / 27 ms); 3 opens the objectives.
    void OnMouseUp(int buttons, int x, int y);
    void OnMouseMove(int buttons, int x, int y);
    int LeftDown(int b, int x, int y) override { return OnMouseDown(b, x, y), 0; }
    int LeftUp(int b, int x, int y) override { return OnMouseUp(b, x, y), 0; }
    int MouseMove(int b, int x, int y) override { return OnMouseMove(b, x, y), 0; }
    // Every frame: the face turns with the time of day, the hand and dial; blinks while paused.
    void Update() override;
    // The objectives window of the current zone (one button, no exits, no zone list).
    void OpenObjectives();
    // Rebirth-patched (WideScreen behind its flag: the clock at the screen's right edge). Regions 0-2 the
    // pause, normal and fast sectors, 3 the objectives quarter disc.
    void SetupVanilla(int fast);

    CBattleScreen* owner_;           // the clock does not read it
    float blink_;                    // 0..1, the pause blink phase
    float dial_;                     // speed dial angle (wraps at pi / 6)
    int fast_;                       // fast game speed (copied to CInterface3D::fastGame_)
    bool highlight_;                 // the pause button pulses (scene object 6)
};
static_assert(offsetof(CHudClock, blink_) == 0x14C);
static_assert(offsetof(CHudClock, highlight_) == 0x158);
static_assert(sizeof(CHudClock) == 0x15C);   // CBattleScreen's operator new

// Movement panel (0..80 x 520..600). Region 0 toggles the units' flag, regions 1-4 set their movement mode 0-3.
class CHudMovePanel : public CUiWindow {
public:
    CHudMovePanel(CInterface3D* ui, CBattleScreen* owner) : CUiWindow(ui), owner_(owner) {}
    void OnMouseDown(int buttons, int x, int y);
    void OnMouseUp(int buttons, int x, int y);
    void OnMouseMove(int buttons, int x, int y);
    int LeftDown(int b, int x, int y) override { return OnMouseDown(b, x, y), 0; }
    int LeftUp(int b, int x, int y) override { return OnMouseUp(b, x, y), 0; }
    int MouseMove(int b, int x, int y) override { return OnMouseMove(b, x, y), 0; }
    // Every frame: the selected units' common mode and flag.
    void Update() override;
    // Region 0 (and its key): the selected party units' flag becomes 0 when all have 1, else 1.
    void ToggleFlag();
    // WideScreen behind its flag (the left panels' offset). A negative mode hides the marker.
    void ShowModeVanilla(int mode);
    // Rebirth-patched (WideScreen behind its flag: the panel at the screen's left edge).
    void SetupVanilla();
    void Build() override { SetupVanilla(); }

    CBattleScreen* owner_;
    int mode_;                       // the shown movement mode (-1, -2: none, mixed)
    int flag_;                       // the selected units' common flag (-1, -2: none, mixed)
};
static_assert(offsetof(CHudMovePanel, mode_) == 0x14C && offsetof(CHudMovePanel, flag_) == 0x150);
static_assert(sizeof(CHudMovePanel) == 0x154);   // CBattleScreen's operator new

// Top panel (180..620 x 0..100). Regions: 0 the sling button, 1 and 2 the two page buttons, 3 the whole panel.
class CHudTopPanel : public CUiWindow {
public:
    CHudTopPanel(CInterface3D* ui, CBattleScreen* owner) : CUiWindow(ui), owner_(owner) {}
    void OnMouseDown(int buttons, int x, int y);
    void OnMouseUp(int buttons, int x, int y);
    void OnMouseMove(int buttons, int x, int y);
    BOOL OnWheel(int buttons, int delta, int x, int y);
    int LeftDown(int b, int x, int y) override { return OnMouseDown(b, x, y), 0; }
    int LeftUp(int b, int x, int y) override { return OnMouseUp(b, x, y), 0; }
    int MouseMove(int b, int x, int y) override { return OnMouseMove(b, x, y), 0; }
    int Wheel(int f, int d, int x, int y) override { return OnWheel(f, d, x, y); }
    // Page `page_` into the message log (page 0 scrolled to the end).
    void ShowPage();
    // Every frame: the slide.
    void Update() override;
    // The parts, the message log and the regions at the current slide offset.
    void Layout();
    // Page 1's text: the party's money and the local player's items merged by kind, one per line.
    void ShowItems();
    ~CHudTopPanel() override;
    // Slid in when the low byte of `shown` is set.
    void Build(int page, int shown);
    // Appends "<combat_picked_up or, with the low byte of `crippled`, combat_crippled> <item name>" and a
    // newline to page 0's text, and to the message log when page 0 is up.
    void AddPickedUp(CItem* item, int crippled);
    // The same with the money ("format_money"), for money > 0.
    void AddMoney(int money);

    CBattleScreen* owner_;
    int sliding_;
    int offset_;                     // current slide offset in pixels
    int travel_;                     // full slide distance
    float slide_;                    // 0 (out) .. 1 (in)
    float slideDirection_;
    int page_;
    std::string pageTexts_[2];      // the message log's text of pages 0 and 1
};
static_assert(offsetof(CHudTopPanel, owner_) == 0x148);
static_assert(offsetof(CHudTopPanel, slideDirection_) == 0x15C);
static_assert(offsetof(CHudTopPanel, page_) == 0x160 && offsetof(CHudTopPanel, pageTexts_) == 0x164);

// --- Screens with child windows ---

// Base of the screens that hold child windows and extra input sides (the Esc menu, the save/load and
// options windows, the battle screen). An event goes to the extra input sides in order, then to the enabled
// children (mouse events only to those holding the point), until one sets its handled flag; see
// docs/notes/ui.md, "Container input dispatch".
class CUiContainer : public CUiWindow {
public:
    explicit CUiContainer(CInterface3D* ui);
    ~CUiContainer() override;

    void Show(int show) override { ShowChildren(show); }
    // Only while the interface draws: the window's own draw, then every enabled child's.
    BOOL Draw(CSceneRenderer* pass) override;
    // Alpha reference 1 around the region tips and every enabled child's draw, while the interface draws.
    void DrawChild(CSceneRenderer* pass) override;
    void Redraw() override { RedrawContainer(); }
    void RedrawTips() override { RedrawChildTips(); }
    void Update() override { UpdateBase(); }
    // The screens' virtuals the interface calls (CInterface3D); the defaults do nothing.
    virtual void CreateScreen();
    virtual void CloseScreen();
    virtual void MessageResult(int) {}                                 // the value a window above left pending when it closed
    virtual void ItemsPicked(void*, int, int) {}                       // items picked up (items, money, crippled)
    virtual void UnitJoined(CUnit*) {}
    virtual void UnitLeft(CUnit*) {}
    virtual void GameOver(int) {}
    virtual void Speech(int, unsigned, char*) {}                       // a unit speaks
    virtual void QuestVariable(const char*, float) {}                  // a quest variable changed
    virtual void ShowBriefing(const char*) {}                          // a "briefing <name>" string event
    virtual bool ReadyToStopPolling() { return false; }                // whether the interface may stop asking (pollTop_)
    virtual void PlayersChanged(int) {}
    virtual void ExchangeDone(CParty*, CParty*, CParty*) {}            // an exchange ended
    virtual void PlayerList(int) {}                                    // the player list for kicking (1) or banning (2)
    virtual void DebugFill() {}                                        // the console's "give <player> assortment"
    virtual void SaveState() {}                                        // the window's part of a save game
    virtual const char* HelpTopic() { return nullptr; }

    void Deactivate() override;                                        // every side and child, no flags
    int LeftDown(int buttons, int x, int y) override;
    int LeftUp(int buttons, int x, int y) override;
    int MouseMove(int buttons, int x, int y) override;
    int RightDown(int buttons, int x, int y) override;
    int RightUp(int buttons, int x, int y) override;
    int LeftDouble(int buttons, int x, int y) override;
    int RightDouble(int buttons, int x, int y) override;
    int KeyDown(int key, int repeat, int flags) override;
    int Char(int key, int repeat, int flags) override;
    int KeyUp(int key, int repeat, int flags) override;
    int Wheel(int flags, int delta, int x, int y) override;

    // Seconds since the last frame (milliseconds * 0.001f, left unrounded).
    static double FrameTime();
    void UpdateBase();
    // Only while the interface draws: the window's own, then every enabled child's.
    void RedrawContainer();
    void RedrawChildTips();
    // Null is ignored.
    void AddChild(CUiWindow* child);
    void AddInput(UiInputSide* input);
    // The window's own surfaces, then every enabled child's.
    void ShowChildren(int show);

    UiPtrArray<CUiWindow> children_;
    UiInputSide** inputsBegin_;               // extra input sides
    UiInputSide** inputsEnd_;
    UiInputSide** inputsCapacity_;

private:
    // `atPoint` limits the children to those holding (b, c).
    void Dispatch(int slot, int a, int b, int c, bool atPoint);
};
static_assert(sizeof(CUiContainer) == 0x164);
// A window on the interface's stack as the screen it is (every window pushed there is a container).
inline CUiContainer* ScreenOf(CUiWindow* window) { return static_cast<CUiContainer*>(window); }

// --- Battle screen ---
// The battle screen's "pers" texts by text key id.
using PersonTextMap = std::map<int, std::string>;

// The in-game screen (CreateScreen id 1); its Create builds the HUD panels as children in order: weapon
// bar, quick items, spell column, action column, portraits, unit panel, map, clock, movement panel, message
// log, top panel. See docs/notes/ui.md, section 6.
class CBattleScreen : public CUiContainer {
public:
    // A click on `unit` (or, without one, at screen point x, y): with party members selected, by the pending
    // action (0 select / attack / move, 1 spell on a unit, 2 and 4 spell on a point, 3 quick item, 5 item
    // and 6 spell on an object, 7 follow, 8 order on an object) or the modifier keys; with nobody selected
    // it selects the clicked member. Rebirth's FixSpellSelfCasting #1 drops the Ctrl flag of a spell on a
    // unit.
    void SelectUnit(CUnit* unit, int x, int y);
    // The unit panel's hover unit, and with party members selected the cursor for what a click would do.
    void HoverUnit(CUnit* unit, int x, int y);
    // Orders the selected party units to the point; a point on a zone exit whose "z.<zone>" variable is not
    // 1 is remembered (exitPending_). `mode` 0 a click on the ground, 1 with a modifier.
    void OrderAt(float x, float y, float z, int mode, int ctrl);
    // The pending action becomes `mode` on `slot` with `item`, the panels' highlights move to `slot` in the
    // panel of that mode. Mode 0 with slot and item -1 cancels.
    void SetPendingAction(int mode, int slot, int item);

    // Reads the screen's saved state when a game is being loaded, else remembers the local player's first
    // hero.
    void CreateScreen() override;
    // Rebirth's AddNewConsoleCommands applies "nogui" first. Returns 1.
    BOOL Draw(CSceneRenderer* pass) override;
    // Every frame: a dead selected unit empties the panels, a multiplayer client rebuilds the party, the
    // "leave_zone" box for a pending exit, the selection box's marks, the quest sound.
    void Update() override;
    // A click on the unit under the point, or a drag selecting the party units inside the box. Details:
    // docs/notes/ui.md, "Behaviour moved out of header comments".
    void OnLButtonUp(int buttons, int x, int y);
    // Also every frame from Draw. Details: docs/notes/ui.md, "Behaviour moved out of ui.h comments".
    void OnMouseMove(int buttons, int x, int y);
    // While a portrait click picks a player the portraits get it; else, when no panel took it, a press on
    // the view starts.
    void OnLButtonDown(int buttons, int x, int y);
    void OnLeftDouble(int buttons, int x, int y);
    void OnDeactivate();
    // Rebirth's MPSpeedControl behind its flag. While a portrait click picks a player, Esc ends that; on a
    // network client Enter / Backspace go to the chat line. Otherwise Esc drops the pending action, else
    // opens the escape menu; other keys run their action (docs/notes/ui.md, "Behaviour moved out of ui.h
    // comments").
    void OnKeyDown(int key, int repeat, int flags);
    // Releases the order key of the key's action.
    void OnKeyUp(int key, int repeat, int flags);
    // Offered to the map, the unit panel, the message log and the top panel in that order; when none takes
    // it, the container passes it on with those four disabled. Returns 1.
    BOOL OnWheel(int flags, int delta, int x, int y);
    // Its GetName when not empty; for a unit whose id is a text key, its "pers" text; otherwise the first
    // line of the text "unit " + the prototype's name with spaces as '_'.
    void UnitName(CUnit* unit, std::string* out);
    // A "q." player variable changed: a quest line to the top panel's text and the message log; the clock
    // pulses. Details: docs/notes/ui.md, "Behaviour moved out of ui.h comments".
    void OnQuestVariable(const char* name, float value);
    // With exactly one living party unit selected the weapon bar, quick items, spell and action columns show
    // it, otherwise they are emptied; the pending action and highlights are cleared. Details:
    // docs/notes/ui.md, "Behaviour moved out of header comments".
    void RefreshSelection();
    // Nothing on a network client or past the party. With the modifier key the member flips, else it alone
    // is chosen. Details: docs/notes/ui.md, "Behaviour moved out of header comments".
    void ToggleMember(int index);
    // The order kind is the first order key held (6 for none); a member more than five levels below the
    // target speaks (speech event 10).
    void AttackUnit(CUnit* target, int ctrl);
    // The same for a dead `target` (no kind, no levels).
    void OrderDeadUnit(CUnit* target, int ctrl);
    // The pending spell (`quick` 0) or quick item used on `target` by each selected member; the pending
    // action is dropped after.
    void UseOnUnit(CUnit* target, char quick, int ctrl);
    // The same at the point. For the member that is `unit` itself the point first moves 0.1 behind it, for
    // it and the members after it.
    void UseAtPoint(float x, float y, float z, CUnit* unit, char quick);
    // The unit's speech line goes to the top panel's text and the message log; event 0x30 shows its timed
    // face, events 0xB..0x15 shake its portrait.
    void OnUnitSpeech(int id, unsigned event, char* extra);

    void Close();
    // Polled every frame by the interface until it answers true. After a new game (hasHero_): the camera
    // follows the first party unit and the autosave "savea" when the option is on.
    bool PollShown();
    void OnItemsPicked(mfc::CTypedPtrList<CItem>* items, int money, int crippled);
    // A network client instead empties the top panel's page-0 text.
    void OnUnitLeft(CUnit* unit);
    // The count and ids of the selected party units, the map's zoom, the server flag (a byte), the local
    // player's first value, the top panel's page-0 text with its zero, exitPending_ (a byte), pendingExit_.
    void SaveState() override;
    // With a record of the local player for it, the unit goes after the party units whose records come
    // first and the record's selection byte decides; otherwise it is appended, not selected. A unit among
    // heroes_ (the save's selection) is taken out and selected. A network client instead empties the top
    // panel's page-0 text.
    void OnUnitJoined(CUnit* unit);
    // Also turns the face's rest orientation about -x by kHudQuarterTurn / 7.
    void MarkPortrait(int index);
    // 0x10001 (the "leave_zone" question) leaves through pendingExit_; 0x20001 (game over: load) opens the
    // load window; 0x20002, 0x90001 and 0x100001 leave the game.
    void OnMessageResult(int result);
    // Lost (`won` 0): the box "game_over" (result 0x20000); won: "end_of_game" (0xFFFF0000).
    void OnGameOver(char won);
    // A portrait click picks a player while it is not 0.
    void SetPortraitMode(int mode);
    explicit CBattleScreen(CInterface3D* ui);
    ~CBattleScreen() override;
    void CloseScreen() override { Close(); }
    void MessageResult(int result) override { OnMessageResult(result); }
    void ItemsPicked(void* items, int money, int crippled) override {
        OnItemsPicked(static_cast<mfc::CTypedPtrList<CItem>*>(items), money, crippled);
    }
    void UnitJoined(CUnit* unit) override { OnUnitJoined(unit); }
    void UnitLeft(CUnit* unit) override { OnUnitLeft(unit); }
    void GameOver(int won) override { OnGameOver(static_cast<char>(won)); }
    void Speech(int id, unsigned event, char* extra) override { OnUnitSpeech(id, event, extra); }
    void QuestVariable(const char* name, float value) override { OnQuestVariable(name, value); }
    bool ReadyToStopPolling() override { return PollShown(); }
    void PlayerList(int mode) override { SetPortraitMode(mode); }
    void Deactivate() override { OnDeactivate(); }
    int LeftDown(int b, int x, int y) override { return OnLButtonDown(b, x, y), 0; }
    int LeftUp(int b, int x, int y) override { return OnLButtonUp(b, x, y), 0; }
    int MouseMove(int b, int x, int y) override { return OnMouseMove(b, x, y), 0; }
    int LeftDouble(int b, int x, int y) override { return OnLeftDouble(b, x, y), 0; }
    int KeyDown(int k, int r, int f) override { return OnKeyDown(k, r, f), 0; }
    int KeyUp(int k, int r, int f) override { return OnKeyUp(k, r, f), 0; }
    int Wheel(int f, int d, int x, int y) override { return OnWheel(f, d, x, y); }

    // The client's party ids: players with a unit, the camera's player, the others.
    using IdVector = std::vector<int>;
    struct PartyIds {
        // Both id vectors hold the same ids in the same order (the camera's id is not compared).
        bool Equals(const PartyIds& other) const;

        IdVector withUnit;
        int camera;
        IdVector without;
    };

    int spare_;                      // nothing reads it
    mfc::CArray<CUnit*> party_;      // shown as portraits
    // The pending action: what the next click on the view does (SelectUnit), its spell, quick item
    // or action button, and the player's item id (mode 6) or the action (7, 8).
    int pendingAction_;
    int pendingSlot_;
    int pendingItem_;
    // A zone exit OrderAt found under the clicked point; the update asks about leaving while exitPending_ is
    // set. Saved with the game (version 0x62).
    int pendingExit_;
    std::uint8_t exitPending_;
    // Key actions 0x11..0x16 held; AttackUnit's order kind is the first one set.
    std::uint8_t orderKeys_[6];
    std::uint8_t unused_193;
    CHudWeaponBar* weaponBar_;
    CHudQuickItems* quickItems_;
    CHudSpellColumn* spellColumn_;
    CHudActionColumn* actionColumn_;
    CHudPortraits* portraits_;
    CHudUnitPanel* unitPanel_;
    CHudMap* map_;
    CHudClock* clock_;
    CHudMovePanel* movePanel_;
    CUiScrollText* messageLog_;
    CHudTopPanel* topPanel_;
    CUiScrollText* MessageLog() const;
    bool viewPressed_;               // the left button went down on the view
    bool boxSelecting_;              // a selection box is being dragged
    std::uint8_t unused_1C2[0x1C4 - 0x1C2];
    int pressX_;                     // where the button went down on the view (screen)
    int pressY_;
    RECT selectionBox_;              // the box
    UiDwordArray heroes_;            // ids of the heroes shown (saved with the game)
    PersonTextMap personTexts_;      // the "pers" texts by person
    int questSound_;                 // the quest sound to play (1 quest, 2 part done, 3 done)
    bool hasHero_;                   // a hero id was added (new game)
    std::uint8_t unused_201[3];
    PartyIds clientParty_;           // the client's party ids last built
    // The key a player's item gives when it starts mode 6; SelectUnit and the hover hand it to the target's
    // AcceptsKey.
    LockKey itemUseKey_;
    int portraitMode_;               // 1, 2: the next portrait click picks a player for a server command (4, 5)
};
static_assert(offsetof(CBattleScreen, spare_) == 0x164 && offsetof(CBattleScreen, party_) == 0x168);
static_assert(offsetof(CBattleScreen, pendingAction_) == 0x17C && offsetof(CBattleScreen, pendingSlot_) == 0x180);
static_assert(offsetof(CBattleScreen, pendingItem_) == 0x184 && offsetof(CBattleScreen, pendingExit_) == 0x188);
static_assert(offsetof(CBattleScreen, exitPending_) == 0x18C && offsetof(CBattleScreen, orderKeys_) == 0x18D);
static_assert(offsetof(CBattleScreen, weaponBar_) == 0x194 && offsetof(CBattleScreen, quickItems_) == 0x198);
static_assert(offsetof(CBattleScreen, actionColumn_) == 0x1A0 && offsetof(CBattleScreen, portraits_) == 0x1A4);
static_assert(offsetof(CBattleScreen, topPanel_) == 0x1BC && offsetof(CBattleScreen, pressX_) == 0x1C4);
static_assert(offsetof(CBattleScreen, selectionBox_) == 0x1CC && offsetof(CBattleScreen, heroes_) == 0x1DC);

// A scroll bar (the battle screen's message log derives from it). Regions: 0 the up arrow, 1 the down arrow,
// 2 the track.
class CUiScrollbar : public CUiWindow {
public:
    // Returns 1 (and clears the flag) when it moved since the last call.
    BOOL TakePosition(int* position);
    // `speed` in lines per second; position 0.
    CUiScrollbar(CInterface3D* ui, RECT rect, float depth, int maximum, float speed);
    ~CUiScrollbar() override;
    // Scrolls while an arrow is held: whole steps of the accumulated amount move the position.
    void Update() override;
    void OnMouseDown(int buttons, int x, int y);   // thumb drag or an arrow
    void OnMouseUp(int buttons, int x, int y);
    // Drags the thumb (state 1). The event is used either way.
    void OnMouseMove(int buttons, int x, int y);
    // Over the track (region 2): delta (low 16 bits) * speed_ / 120 into the accumulated amount.
    int OnWheel(int flags, int delta, int x, int y);
    // Clamped to 0..maximum, moved_ set.
    virtual void SetPosition(int position);
    // The track is the right 10 pixels less 26 at either end. The position is set again (one below first,
    // so it counts as a change).
    virtual void SetRect(const RECT* rect);
    int LeftDown(int b, int x, int y) override { return OnMouseDown(b, x, y), 0; }
    int LeftUp(int b, int x, int y) override { return OnMouseUp(b, x, y), 0; }
    int MouseMove(int b, int x, int y) override { return OnMouseMove(b, x, y), 0; }
    int Wheel(int f, int d, int x, int y) override { return OnWheel(f, d, x, y); }
    // Negative: 0. The sprites show only for a maximum above 0; the position is set again as SetRect does.
    void SetMaximum(int maximum);
    int Maximum() const;
    int maxPosition_;
    float speed_;                    // lines per second while an arrow is held (/16)
    int position_;
    // Layout coordinates (the options slider: left + 26 .. right - 26 over the bottom 10 pixels).
    RECT track_;
    RECT thumb_;                     // layout coordinates
    int grabX_;                      // mouse offset from the thumb centre while dragging
    int grabY_;
    float depth_;                    // the sprites' depth
    bool moved_;
    std::uint8_t unused_181[3];
    int state_;                      // 0 idle, 1 dragging the thumb, 2 up, 3 down
    float accumulated_;              // scrolled amount not yet applied
};
static_assert(offsetof(CUiScrollbar, position_) == 0x150);
static_assert(offsetof(CUiScrollbar, track_) == 0x154);
static_assert(offsetof(CUiScrollbar, thumb_) == 0x164);
static_assert(offsetof(CUiScrollbar, depth_) == 0x17C);
static_assert(sizeof(CUiScrollbar) == 0x18C);

// The options window's horizontal slider, laid out along the bottom of its rectangle. Regions 0 the left
// arrow, 1 the right arrow, 2 the track.
class CUiSlider : public CUiScrollbar {
public:
    CUiSlider(CInterface3D* ui, RECT rect, float depth, int maximum, float speed)
        : CUiScrollbar(ui, rect, depth, maximum, speed) {}
    // Along the track's width; the thumb 28 wide and 10 high.
    void SetPosition(int position) override;
    // The scroll bar's drag along x.
    void OnMouseMove(int buttons, int x, int y);
    int MouseMove(int b, int x, int y) override { return OnMouseMove(b, x, y), 0; }
    void SetRect(const RECT* rect) override;
};
static_assert(sizeof(CUiSlider) == 0x18C);

static_assert(offsetof(CUiScrollbar, state_) == 0x184);
static_assert(offsetof(CUiScrollbar, accumulated_) == 0x188);


// The pieces of a scrolling text: a format change (type 1) or a word (type 2).
struct UiFormatToken;
struct UiWordToken;
struct UiTextToken {
    explicit UiTextToken(int type) : type_(type) {}
    virtual ~UiTextToken();
    // The game's heap (null without memory: then no constructor runs).
    static void* operator new(std::size_t size) noexcept;
    static void operator delete(void* block);
    UiFormatToken* AsFormat();
    const UiFormatToken* AsFormat() const;
    UiWordToken* AsWord();

    int type_;                       // 1 format, 2 word
};
struct UiFormatToken : UiTextToken {
    explicit UiFormatToken(int font);
    ~UiFormatToken() override;

    bool newline_;                   // a newline token, or a font change after the first token
    std::uint8_t unused_09[3];
    int font_;                       // interface font
    COLORREF colour_;
};
static_assert(sizeof(UiFormatToken) == 0x14);
struct UiWordToken : UiTextToken {
    UiWordToken() : UiTextToken(2), width_(0) {}
    ~UiWordToken() override;

    std::string text_;
    int width_;                      // its width in the token's font
};
static_assert(offsetof(UiWordToken, text_) == 0x08);
inline UiFormatToken* UiTextToken::AsFormat() { return static_cast<UiFormatToken*>(this); }
inline const UiFormatToken* UiTextToken::AsFormat() const { return static_cast<const UiFormatToken*>(this); }
inline UiWordToken* UiTextToken::AsWord() { return static_cast<UiWordToken*>(this); }
using UiTokenArray = mfc::CArray<UiTextToken*>;
// A laid out line of a scrolling text: the fields DrawLines reads (the record's size is not known).
struct UiTextLine {
    int height_;                     // pixels
    int firstToken_;                 // index in the token array
};

// A scroll bar showing tokenised text: the battle screen's message log and the objectives window's quest
// log.
class CUiScrollText : public CUiScrollbar {
public:
    // 20 lines per second, justified (align_ 2).
    CUiScrollText(CInterface3D* ui, const char* text, RECT rect, float depth);
    ~CUiScrollText() override;
    // Scrolled to the end first, then kept that far from the end.
    void AddText(const char* text);
    // Scrolls to the top.
    void SetText(const char* text);
    void SetRect(const RECT* rect) override;
    // Draws the lines again when the position moved; returns 1.
    BOOL Draw(CSceneRenderer* pass) override;
    // The lines stay until the next layout.
    void Clear();
    // Words apart by a third of the line's height (the font in effect at the line's start); a newline format
    // token ends the line and sets the next lines' font. Then the scroll bar's maximum.
    void Layout();
    // One format token before each run of newline, BEL C<rgb> and BEL F<digit> tokens.
    void Tokenize(const std::string* text);
    // The token at `pos` (spaces and tabs skipped): a BEL F / BEL C format (3 / 5 characters), a newline, or
    // up to the next space, tab, BEL or newline. Returns the position after it, or -1 at the end (`out`
    // untouched). `this` is not read.
    int NextToken(const std::string* text, int pos, std::string* out);
    // From `scroll` pixels down. A line is justified when align_ is not 1, it is not the last, not ended by
    // a newline, and of two or more tokens.
    void DrawLines(int scroll);

    RECT textRect_;                  // the text's rectangle (surface coordinates)
    UiTokenArray tokens_;
    mfc::CArray<UiTextLine*> lines_; // the laid out lines
    int align_;                      // 1: left aligned (else justified)

private:
    // The tail of SetRect (the text 14 pixels narrower for the bar).
    void PlaceText(const RECT& rect);
    void DeleteTokens();
};
static_assert(offsetof(CUiScrollText, textRect_) == 0x18C);

// The tenth child window (messageLog_).
inline CUiScrollText* CBattleScreen::MessageLog() const { return static_cast<CUiScrollText*>(children_.data[9]); }
static_assert(offsetof(CUiScrollText, tokens_) == 0x19C);
static_assert(offsetof(CUiScrollText, lines_) == 0x1B0);
static_assert(offsetof(CUiScrollText, align_) == 0x1C4);
static_assert(sizeof(CUiScrollText) == 0x1C8);

// A spin control of the options window: a window stepping through its texts (the on / off settings).
class CUiSpin : public CUiWindow {
public:
    CUiSpin(CInterface3D* ui, RECT rect, const char* text);
    ~CUiSpin() override;
    void AddText(const char* text);
    // Returns whether it changed since the last call. SetValue: 0 when not below the text count (a negative
    // one is kept).
    bool GetValue(int* value);
    void SetValue(int value);
    // The next value, wrapping.
    void OnLButtonDown(int flags, int x, int y);
    int LeftDown(int b, int x, int y) override { return OnLButtonDown(b, x, y), 0; }
    int LeftDouble(int, int, int) override { return 0; }

    std::vector<std::string> texts_;
    int value_;
    bool changed_;
    RECT layout_;                    // the constructor's rectangle
};

// A saved game in the save/load list; the fields after the folder come from the save's info.sav.
struct SaveEntry {
    // "save_quick", "save_auto_enter" / "save_auto_exit" for "saveq", "savea" / "saveb", else the zone's name.
    void MakeTitle(int unused);
    // Text "zone " + zone_ (its first line), else "save_camp"; text "allod " + map_, else "Unknown".
    std::string ZoneTitle() const;
    std::string AllodTitle() const;
    SaveEntry* Assign(const SaveEntry* from);
    SaveEntry* CopyConstruct(const SaveEntry* from);
    void Destruct();

    std::string title_;
    std::string folder_;            // under saves\ ("saveq" quick, "savea" / "saveb" automatic, "save<n>")
    std::int32_t savedAt_;           // time() when it was saved (info.sav version 2 and up)
    float gameTime_;                 // the "gtime" variable
    std::string map_;               // the map of the zone
    std::string zone_;
    std::vector<std::string> stringArrays_[2];  // copied with the entry
    bool broken_;                    // no info.sav: titled "Error Save", no picture
};

// The quick and automatic saves (`slot` the save folder's name). Nothing for a multiplayer client or when
// the disc lacks the space a save needs (0 free space: not checked). Writes info.sav (the "gtime" variable,
// the zone's map, the zone, the time, an empty title) and shot.sav (the screen as 256x192 5:6:5 pixels).
void QuickSave(const char* slot, char stepFrame);
// Not for a multiplayer client; only when saves\<slot>\ exists.
void QuickLoad(const char* slot);

// The save/load window (opened from the Esc menu). Regions: 0 OK, 1 cancel, 2 delete, 3.. the visible rows.
class CSaveLoadWindow : public CUiContainer {
public:
    CSaveLoadWindow(CInterface3D* ui, bool isSave, bool sliding)
        : CUiContainer(ui), isSave_(isSave), sliding_(sliding) {}
    ~CSaveLoadWindow() override;
    BOOL Draw(CSceneRenderer* pass) override;                      // slides in
    void Update() override;
    void CloseScreen() override { CloseDialog(); }
    void MessageResult(int result) override { OnDialogResult(result); }
    int LeftDown(int b, int x, int y) override { return OnMouseDown(b, x, y), 0; }
    int LeftDouble(int b, int x, int y) override { return OnDoubleClick(b, x, y), 0; }
    int KeyDown(int k, int r, int f) override { return OnKeyDown(k, r, f), 0; }
    int Char(int k, int r, int f) override { return OnChar(k, r, f), 0; }
    void OnMouseDown(int buttons, int x, int y);
    void OnDoubleClick(int buttons, int x, int y);
    void CloseDialog();
    void OnDialogResult(int result);

    // A save window saves into the new save (row 0) or asks "overwrite" (message box 0x20000); a load
    // window loads.
    void Confirm();
    // Not for a save window's new save; asks "delete" (message box 0x10000).
    void AskDelete();
    // Deletes the selected save's files and folder and refills the list.
    void DeleteSave();
    // Not for a broken save.
    void LoadSelected();

    void RefreshListVanilla();

    // Saves into the selected entry's folder, then info.sav and shot.sav (from dialog_'s pixels); a message
    // box when the disc is full.
    void SaveGame();
    // The selected save's shot.sav (read into objects_[1]), or dialog_ for a save window's new save; hidden
    // for none or a broken save.
    void ShowPicture();
    // Reads saves\save*\info.sav and sorts the list; a save window's new save goes first (dialog_ made from
    // the escape menu's 256x192 copy of the screen).
    void FillEntries();

    void CreateScreen() override;

    int EntryCount() const { return static_cast<int>(entries_.end() - entries_.begin()); }
    void TakeSelectedTitle();

    // In a save window the edit keys edit the save's name (the first edit of an ordinary save takes over its
    // title, Backspace / Delete starting from an empty name).
    void OnKeyDown(int key, int repeat, int flags);
    // In a save window: the first character for an ordinary save starts an empty name; a name stays under 60
    // characters and 178 pixels (font 1).
    void OnChar(int key, int repeat, int flags);
    // Clamps the selection and scrolls it into view (16 rows).
    void ShowSelection();
    // "saveq", "savea" or "saveb"; no bound check.
    bool IsReservedSave(int index);

    stlp::vector<SaveEntry> entries_;
    int selected_;
    int firstVisible_;               // list scroll position
    bool isSave_;                    // save (not load) window
    bool sliding_;                   // sliding in; input is ignored meanwhile
    std::uint8_t unused_17A[2];
    float slide_;                    // 0..1
    CUiScrollbar* scrollbar_;
    CTexture* dialog_;               // the save window's picture of the game for the new save
    std::string name_;              // the save name being edited
    bool nameTyped_;
    std::uint8_t unused_18D[3];
    int cursor_;                     // caret position in name_ (bytes, at a character's start: U4)
};
static_assert(offsetof(CSaveLoadWindow, selected_) == 0x170);
static_assert(offsetof(CSaveLoadWindow, slide_) == 0x17C);
static_assert(offsetof(CSaveLoadWindow, name_) == 0x188);

struct OptionDefinition {
    int id_;
    const char* name_;               // "option_" + name is its text key (else the name is shown)
    int kind_;                       // 0 a slider, 1 an on / off spin control
    int maximum_;                    // the slider's range
    int group_;                      // the option group (0-10) it is shown in
    int row_;                        // its row there
};
static_assert(sizeof(OptionDefinition) == 0x18);
constexpr int kOptionDefinitionCount = 26;

struct ActionDefinition {
    int id_;                         // the action (the key code in the key table)
    const char* name_;               // "action_" + name is its text key
    int group_;
    int row_;
};
static_assert(sizeof(ActionDefinition) == 0x10);
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
    const OptionDefinition* definition_;
    int value_;
    CUiScrollbar* bar_;              // a CUiSlider, or null
    CUiSpin* spin_;                  // or null
    std::string text_;              // the shown name
};

// An exit shown on the objectives window's zone picture.
struct ObjectiveExit {
    bool flag_;                      // a known distance (global map), or the zone variable is not 1
    std::uint8_t unused_01[3];
    int exit_;                       // index in the zone's exit list
    float distance_;
    float x_;                        // centre of the exit rectangle (world units)
    float y_;
    float mapX_;                     // on the picture (layout)
    float mapY_;
};
static_assert(sizeof(ObjectiveExit) == 0x1C);

// The objectives window (TAB in a zone opens it for the current zone). Left: the quests of the zone (rows),
// the log; right: the zone picture with its exits; the arrows switch between zones when it lists several.
class CObjectivesWindow : public CUiContainer {
public:
    explicit CObjectivesWindow(CInterface3D* ui);
    // Not the exit distances.
    ~CObjectivesWindow() override;
    void CloseScreen() override { RestoreAlphaRef(); }
    int LeftDown(int b, int x, int y) override { return OnLButtonDown(b, x, y), 0; }
    int KeyDown(int k, int r, int f) override { return OnKeyDown(k, r, f), 0; }
    long RestoreAlphaRef();
    void CreateScreen() override;
    // Picture, exits, quests and description.
    void ShowZone(const char* zone);
    // When the selected quest changed: its description and a marker and region per place of the quest.
    void ShowQuest();
    // Up to five rows from firstRow_.
    void DrawQuestList();
    // Region 0 OK (with two buttons the time to the nearest exit passes and that zone is requested), 1
    // cancel, 2 / 3 the previous / next listed zone, 4-8 a quest row, from 9 an exit.
    void OnLButtonDown(int buttons, int x, int y);
    // Enter as the OK region, Esc closes, PgUp / PgDn / Up / Down move the selected quest by 5 / 1. A key
    // left unused that is bound to the objectives action closes a window with one button.
    void OnKeyDown(int key, int repeat, int flags);
    // A multiplayer client takes them from the quest database's record, otherwise from map.txt's "#quest
    // <name>" entry (the second line after it, its words as x y pairs).
    void LoadQuestPlaces(const char* name);
    // Exit `index` becomes the nearest (its marker 1.2 times markerScale_); the blink restarts.
    void SelectExit(int index);
    // Keeps the selected quest within the five shown rows.
    void RefreshQuests();
    // Every frame: the markers' blink, alphas (cos(2.5 t) * 0.3 + 0.5) * 255 and (cos(5 t) * 0.4 + 0.6) *
    // 255.
    void Update() override;
    // `exitDistances` may be null. The help topic is "quest_global_map" with two buttons, else
    // "quest_mission".
    void Setup(const char* zone, void* exitDistances, int twoButtons, int listZones);
    const char* HelpTopic() override;

    std::vector<std::string> zones_;   // the zones listed
    std::string zoneName_;          // the zone it was opened for
    int twoButtons_;                 // OK on the left and a second button (else OK centred)
    std::map<int, float>* exitDistances_;   // exit -> distance, or null
    int listZones_;                  // list the zones of the zone's allod with quests
    int zoneIndex_;
    std::string shownZone_;
    mfc::CArray<ObjectiveExit> exits_;
    int nearestExit_;
    int selectedQuest_;              // row of the quest named by the current-quest global
    int shownQuest_;                 // the row ShowQuest last showed (-1: none)
    std::vector<std::string> quests_;
    CUiScrollbar* bar_;
    int firstRow_;                   // the first quest row shown (the bar's position)
    float markerScale_;
    float blinkTime_;                // seconds since the exit was selected (the markers' blink)
    std::vector<UiPointF> questPlaces_;   // the selected quest's places
    std::vector<std::string> objectiveTexts_;   // obj_0..obj_2
    std::uint32_t savedAlphaRef_;
};
static_assert(offsetof(CObjectivesWindow, zones_) == 0x164 && offsetof(CObjectivesWindow, zoneName_) == 0x170);

inline const UiKeyName* UiKeyNames() { return g_keyNames; }

// A key binding row of the options window.
struct OptionBinding {
    void Destroy() {
        std::destroy_at(&keysText_);
        std::destroy_at(&name_);
        std::destroy_at(&keys_);
    }

    const ActionDefinition* action_;
    // The keys bound to the action (BuildBindings keeps at most one).
    std::vector<const UiKeyName*> keys_;
    std::string name_;              // the action's shown name
    std::string keysText_;          // the keys' names
};

// A row of the options window: a key binding or a setting.
struct OptionRow {
    OptionBinding* binding_;
    OptionSetting* setting_;
};

// The options window. Regions: 0 OK, 1 cancel, 2-12 the option groups (100..250 x 156..420), 13.. the rows of
// the shown group.
class COptionsWindow : public CUiContainer {
public:
    COptionsWindow(CInterface3D* ui, bool sliding) : CUiContainer(ui), sliding_(sliding) {}
    ~COptionsWindow() override;
    BOOL Draw(CSceneRenderer* pass) override;                      // slides in
    void Redraw() override;                            // not while sliding
    void CloseScreen() override {}
    void MessageResult(int result) override { OnDialogResult(result); }
    const char* HelpTopic() override;
    int LeftDown(int b, int x, int y) override { return OnMouseDown(b, x, y), 0; }
    int LeftDouble(int b, int x, int y) override { return OnDoubleClick(b, x, y), 0; }
    int KeyDown(int k, int r, int f) override { return OnKeyDown(k, r, f), 0; }
    void OnMouseDown(int buttons, int x, int y);
    void OnDoubleClick(int buttons, int x, int y);
    void OnDialogResult(int result);
    // Applies the settings whose bar or spin control changed (volumes, the display's gamma at once).
    void Update() override;

    // The group titles, the shown group's rows (a binding waiting for its key in red) and the highlight.
    void RefreshRows();
    // Clamped to 0-10; the first filled row selected (or row 0), the key wait cleared.
    void ShowGroup(int group);
    // OK: the key table and every setting into the game's option globals (the difficulty only outside a
    // multiplayer client); a shadow switch (settings 7-9) turned off drops those shadows. The options have
    // no resolution.
    void Apply();
    // A slider (kind 0) or an on / off spin control (kind 1) per setting.
    void BuildSettings();
    void LoadSettings();
    // The keys' text is the first key name of a scan code bound to the action.
    void BuildBindings();

    // Binds pressedKey_ to the selected row's action, taking it from the action it was bound to. Nothing
    // without a pressed key or a binding row.
    void CommitBinding();
    // Cancel: puts back the volumes and the display settings the window started with.
    static void RestoreSettings();
    void CreateScreen() override;

    // Esc cancels, Enter starts a key binding or steps a spin row, Up / Down select rows, PgUp / PgDn switch
    // groups, Left / Right move a bar. While a binding waits, the next key is bound (Esc gives up); a key
    // bound to another action asks first.
    void OnKeyDown(int key, int repeat, int flags);

    OptionRow& RegionRow(int region) { return rows_[region - 13]; }

    signed char keyOfScan_[0x200];   // the key code bound to each scan code
    int selectedRow_;
    int group_;                      // the option group shown
    stlp::vector<OptionBinding> bindings_;
    std::vector<OptionSetting> settings_;
    OptionRow rows_[14];
    bool waitingForKey_;             // a key binding row waits for the new key
    bool sliding_;
    std::uint8_t unused_3F6[2];
    float slide_;
    std::vector<std::string> groupNames_;   // the option groups' titles
    bool shadowsChanged_;            // a shadow switch (settings 7-9: units, buildings, flora) changed
    std::uint8_t unused_411[3];
    const UiKeyName* pressedKey_;    // the key pressed for a binding row
};
static_assert(offsetof(COptionsWindow, settings_) == 0x378);
static_assert(offsetof(COptionsWindow, selectedRow_) == 0x364);
static_assert(offsetof(COptionsWindow, bindings_) == 0x36C);
static_assert(offsetof(COptionsWindow, groupNames_) == 0x3FC);
static_assert(offsetof(COptionsWindow, rows_) == 0x384);
static_assert(offsetof(COptionsWindow, waitingForKey_) == 0x3F4);
static_assert(offsetof(COptionsWindow, slide_) == 0x3F8);

// The message box's edit field.
class CUiEditField : public CUiWindow {
public:
    // While focused. The first edit key starts editing (Backspace / Delete from an empty text, the others
    // with the caret at the end); the caret is not clamped here.
    void OnKeyDown(int key, int repeat, int flags);
    void DrawContentsVanilla();
    // Every character class, font 1, at most 50 characters; focused. caret_, editing_ and changed_ are left
    // as they are.
    CUiEditField(CInterface3D* ui, RECT rect);
    ~CUiEditField() override;
    int KeyDown(int k, int r, int f) override { return OnKeyDown(k, r, f), 0; }
    int Char(int k, int r, int f) override { return OnChar(k, r, f), 0; }
    // While focused and without Ctrl, a character of an allowed class: the first one starts editing from an
    // empty text; inserted while shorter than maxLength_ and narrower than area_.
    void OnChar(int key, int repeat, int flags);
    // SetText: not editing, changed, the caret at its end.
    void SetLook(std::uint32_t charClasses, char mode, int font, int maxLength, COLORREF colour, COLORREF caretColour);
    void SetText(const char* text);
    void SetFocused(bool focused);

    RECT area_;                      // the surface's layout rectangle
    std::uint32_t charClasses_;      // classes OnChar takes (1 digits, 2 letters / space, 4 others, 8 '.' ':')
    std::uint8_t centred_;           // SetLook's mode
    std::uint8_t unused_15D[3];
    int font_;                       // interface font
    int maxLength_;
    COLORREF colour_;                // the text's colour
    COLORREF caretColour_;           // SetLook's last argument (0xFF from the constructor)
    std::string text_;
    int caret_;                      // caret position in text_ (bytes, at a character's start: U4)
    bool editing_;                   // an edit key was pressed (the first one sets it)
    bool focused_;                   // keys are taken only while set
    bool changed_;
    std::uint8_t unused_17B;
};
static_assert(offsetof(CUiEditField, text_) == 0x170);

// The message box. Flags: 1 an OK button, 2 with 1 also a cancel button (neither: one cancel button), 4 an
// edit field instead of the text (its initial text is the text key itself).
class CMessageBox : public CUiContainer {
public:
    // A missing text gives its key.
    CMessageBox(CInterface3D* ui, const char* titleKey, const char* textKey, std::uint32_t result, std::uint32_t flags,
                std::uint32_t format);
    ~CMessageBox() override;
    void CloseScreen() override { OnClose(); }
    int LeftDown(int b, int x, int y) override { return OnLButtonDown(b, x, y), 0; }
    int KeyDown(int k, int r, int f) override { return OnKeyDown(k, r, f), 0; }
    void CreateScreen() override;
    // Region 0 OK: for the exit question (result_ 0xFFFF0000) the exit movie and the game quits; otherwise
    // the edit field's text into editText_. Any region n then leaves result_ | (n + 1) pending.
    void OnLButtonDown(int buttons, int x, int y);
    // Unless a child used the key, Enter answers as region 0 and Esc as region 1.
    void OnKeyDown(int key, int repeat, int flags);
    // Into the caller's buffer.
    void CopyEditText();
    // Also the master request box's and the difficulty question's.
    void OnClose();

    std::string title_;
    std::string text_;
    std::uint32_t result_;           // 0xFFFF0000: the text in font 1, else 2
    std::uint32_t flags_;
    std::uint32_t format_;           // added to the text's DrawText format
    CUiEditField* edit_;             // the edit field (flag 4)
    char* editText_;                 // the edit field's first text; OK copies the typed one here
};
static_assert(offsetof(CMessageBox, title_) == 0x164);

// The help / message window (CInterface3D::ShowMessage and OpenHelp). Its pages come from the texts:
// "tutorial <text_>" holds the title key and a "#screen" line per page, and "tutor <key>" each key's text.
class CHelpWindow : public CUiContainer {
public:
    CHelpWindow(CInterface3D* ui, bool message, const std::string& text, int page)
        : CUiContainer(ui), message_(message), text_(text), page_(page) {}
    ~CHelpWindow() override;
    BOOL Draw(CSceneRenderer* pass) override;
    void CloseScreen() override { Close(); }
    int LeftDown(int b, int x, int y) override { return OnLeftDown(b, x, y), 0; }
    int KeyDown(int k, int r, int f) override { return OnKeyDown(k, r, f), 0; }
    // Without the text or without pages the interface's pending value is set to 0 and nothing else happens.
    void CreateScreen() override;
    void Close();
    // The pulse: v = (cos(5 t) + 1) / 2 gives alpha 255 * v and the colour 80 * (1 - v).
    void Update() override;
    // Region 0 the previous page, 1 OK (Finish), 2 the next page.
    void OnLeftDown(int buttons, int x, int y);
    // Enter the next page (Finish on the last), Esc or the help key Finish.
    void OnKeyDown(int key, int repeat, int flags);
    // Rebirth-patched (WideScreen behind its flag). Builds the page from its "#screen" line: the rectangle
    // (200, 50)-(600, 560) or "#position", one pulsing frame per "#rectangle" line, one entry per "#text"
    // line. Details: docs/notes/ui.md, "Behaviour moved out of header comments".
    void ShowPage();

    bool message_;                       // a message window (ShowMessage), not help
    std::uint8_t unused_165[3];
    std::string text_;                  // the message, or the help topic
    std::string title_;
    std::vector<std::string> lines_;     // the pages' texts
    int page_;                           // the page shown (the topic's or message's saved page)
    std::list<CUiMesh*> pulsing_;       // sprites whose plain colour pulses (Update)
    std::list<CUiMesh*> pictures_;      // the entries' picture sprites
    float pulseTime_;

private:
    // The page is kept for the topic (or the message).
    void Finish();
};
static_assert(offsetof(CHelpWindow, text_) == 0x168);

// A line of the in-game chat overlay: the text, the speaker, a value and the line's own surface.
struct ChatLine {
    void Destroy();

    std::string text;          // AddLine leaves it empty
    std::string name;          // the line's text without its colour codes
    std::uint32_t time;         // timeGetTime when it was added
    CUiSurface* surface;
};

// The in-game chat overlay: the interface's root window, updated after the top window every frame; in a
// multiplayer client it holds the chat line, which takes the keys while it is open.
class CChatOverlay : public CUiWindow {
public:
    // Rebirth-patched.
    explicit CChatOverlay(CInterface3D* ui);
    ~CChatOverlay() override;
    // `number` is unused. Lines narrower than 780 pixels, "^<digit>" switching the colour (bits 0 / 1 / 2
    // blue / green / red; a missing digit is 7); at most 12 lines remain.
    void AddLine(const char* text, std::uint32_t number);
    // Opening empties the typed line.
    void SetOpen(bool open);
    void ClearLines();
    // Rebirth-patched (WideScreen behind its flag). Hidden while the chat is closed; clamps the caret.
    void DrawTyped();
    // Nothing while the chat is closed. Every key is marked used; Enter sends a non-empty line (a network
    // client: SayMessage kept 60 s) and, like Esc, closes the chat. OnChar inserts unless Ctrl is down, the
    // line is 200 characters or more, or it would be 780 pixels wide; the character is not marked used.
    void OnKeyDown(int key, int repeat, int flags);
    void OnChar(int key, int repeat, int flags);
    int KeyDown(int k, int r, int f) override { return OnKeyDown(k, r, f), 0; }
    int Char(int k, int r, int f) override { return OnChar(k, r, f), 0; }
    // Lines older than 20 s are erased.
    void Update() override;
    void Redraw() override;

    stlp::list<ChatLine> lines_;
    bool chatOpen_;                  // a multiplayer client's chat line is open (it takes the keys)
    std::uint8_t unused_14D[3];
    std::string typed_;
    int caret_;                      // in the typed line (a byte offset at a character's start: U4)
};
static_assert(offsetof(CChatOverlay, lines_) == 0x148);
static_assert(offsetof(CChatOverlay, typed_) == 0x150);

// The new game's difficulty question (the main menu's region 0): a message box ("option_difficulty", OK and
// cancel) with two check boxes. Its button handler sets easy_ from the boxes (regions 2 / 3) and copies it to
// GameDifficulty() on OK.
class CDifficultyBox : public CMessageBox {
public:
    explicit CDifficultyBox(CInterface3D* ui) : CMessageBox(ui, "option_difficulty", game::EmptyText(), 0x20000, 3, 1) {}
    int LeftDown(int b, int x, int y) override { return OnLButtonDown(b, x, y), 0; }
    int KeyDown(int k, int r, int f) override { return OnKeyDown(k, r, f), 0; }
    // No container handler. Region 0 OK (easy_ to GameDifficulty()), 1 cancel, 2 / 3 check easy / hard.
    void OnLButtonDown(int buttons, int x, int y);
    // Unless a child used the key, Enter answers as OK and Esc as cancel.
    void OnKeyDown(int key, int repeat, int flags);
    // CreateScreen adds them first.
    CUiMesh* EasyBox() const { return SceneMesh(0); }
    CUiMesh* HardBox() const { return SceneMesh(1); }
    // The texture shift of a checked box (0xBDB80000).
    static constexpr float kCheckedOffset = -0.08984375f;

    // Hard checked first.
    void CreateScreen() override;

    int easy_;                       // 1 easy, 0 hard (the default)
};

// The cursor's position in screen pixels and the motion a right-button drag accumulates.
long& MouseScreenX();
long& MouseScreenY();
long& MouseDragX();
long& MouseDragY();

// The game's difficulty (1 easy, 0 hard; combat reads it).
inline int& GameDifficulty() { return options::TheOptions()->difficulty_; }

// config\keyboard.ini.
namespace keybind {
void LoadKeyBindings();
void SaveKeyBindings();
}
