#pragma once

// The 2D interface: the interface manager CInterface3D, its
// windows, their clickable regions, off-screen surfaces and 3D model previews. Names provisional.
// Interface layouts are designed for 800x600. CInterface3D holds the real screen size and scales
// coordinates between the two; windows keep their regions in layout coordinates and convert the
// mouse position before hit testing.

#include "app.h"   // app::g_interfaceHasScreen
#include "core_transform.h"
#include "options.h"
#include "debug_panel.h"
#include "engine_object.h"
#include "figure_mesh.h"
#include "game_data.h"
#include "item_db.h"
#include "object_tables.h"   // g_keyActions
#include "unit.h"   // LockKey

#include "lib/compiled_vtable.h"
#include "lib/containers.h"
#include "lib/mfc.h"
#include "lib/static_object.h"
#include "lib/stlp.h"
#include "lib/stlp_tree_cstring.h"

#include <cstddef>
#include <cstdint>

#include <windows.h>

// The interface's angles and rest rotation that static initialisers set (pi + pi, pi * 0.5, the identity).
inline constexpr float kHudFullTurn = /*007C8140*/ 6.2831855f;
inline constexpr float kHudQuarterTurn = /*007C8144*/ 1.5707964f;
inline constexpr Quaternion kHudRestRotation = /*007C8150*/ {1.0f, 0.0f, 0.0f, 0.0f};

namespace console { class ConsoleWindow; }
class CFigureManager;   // figure_manager.h
class CItem;
class CParty;
class CUnit;
namespace mfc::impl { class CFont; class CDC; }   // lib/mfc_gdi.h

class CUiWindow;
class CChatOverlay;
struct UiInputSide;
class CUiMesh;
struct CDXSurface;
class CSceneRenderer;   // model.h
class CTextureDevice;   // texture.h
class CPixelConverter;  // texture.h
class CStream;          // core_streams.h
class CUiSurface;
class CItemScreen;   // ui_item_info.h

// The interface's screen fade and letterbox (CInterface3D +0x18), drawn over everything by the
// interface's draw. Only the fields its draw uses.
struct CScreenFade {
    // With the letterbox, black bars of the screen height
    // times 0.125-ish at the top and bottom; while fading, the alpha moves by
    // rate * elapsed ms towards 255 (rate > 0) or 0 (rate < 0), clamped at 255; below 0 the fade
    // ends (nothing drawn), otherwise a black rectangle of that alpha over the screen. Both draws
    // with the depth test off (render state 7) through the renderer's slot 19. Returns 1.
    int Draw(CSceneRenderer* renderer);
    // Starts a fade over `ms` milliseconds: rate_ 255 /
    // ms (stored as float), from alpha 0 (ms > 0, fading out) or 255, the clock now, fading.
    void Start(int ms);
    // Constructor (CInterface3D::Create builds it): alpha 0, not
    // fading, no letterbox; the rate and the clock are not written. Returns this.
    CScreenFade* Construct();

    float rate_;                     // +0x00 alpha per millisecond
    float alpha_;                    // +0x04 0..255
    std::uint8_t active_;            // +0x08 fading
    std::uint8_t unused_09[3];
    DWORD lastTime_;                 // +0x0C timeGetTime of the last step
    std::uint8_t letterbox_;         // +0x10 the bars are shown
};
static_assert(offsetof(CScreenFade, lastTime_) == 0x0C && offsetof(CScreenFade, letterbox_) == 0x10);

// The key of the multiplayer message 12's text (CInterface3D::MessageText), "reserved_02": EI_Plugin's Pause
// writes its digit, '2' for a pause and '3' ("reserved_03") for a resume (player_server_orders.cpp).
extern char g_pauseMessageKey[12];

// The interface manager (CInterface3D; created at start-up, it reads the screen size from the
// render target). Only the members used by the rewritten code; the object is created by the
// game's code, so its size is not asserted.
class CInterface3D {
public:
    // Scales a layout rectangle (800x600) to the screen. Returns what the game
    // leaves in EAX: the low dword of the last division's magic multiplication (callers ignore it).
    int ScaleRectToScreen(RECT* rect) const;
    // Same for a point.
    int ScalePointToScreen(POINT* point) const;
    // Scales a screen rectangle to layout coordinates; returns the new bottom (EAX).
    int ScaleRectFromScreen(RECT* rect) const;
    // Identical copies. Screen point to layout; returns the new y (EAX).
    int ScalePointFromScreen(POINT* point) const;
    int ScalePointFromScreenCopy(POINT* point) const;

    // The same where Rebirth's WideScreen scales without the margin: addon clears ECX before the call
    // (XOR ECX,ECX over MOV ECX,[ESI+0x98] / [EDI+4]: the text surfaces' draw, the edit
    // field, the connection list, the save / load list, the chat line,
    // the tooltip). With the flag on (widescreen::Active) these call the
    // original code with ECX 0, which addon's code and our hooks take as "no margin"; with it
    // off, with this interface, as the original.
    int CallScaleRectToScreenNoMargin(RECT* rect) const;
    int CallScalePointToScreenNoMargin(POINT* point) const;
    int CallScalePointFromScreenNoMargin(POINT* point) const;

    // The hooks of the scaling functions
    // (ui_interface.cpp): the methods above, except that with Rebirth's WideScreen code addon's own
    // callers reach them with ECX 0 (no interface): then the wide scaling without the margin.
    int ScaleRectToScreenEntry(RECT* rect) const;
    int ScalePointToScreenEntry(POINT* point) const;
    int ScaleRectFromScreenEntry(RECT* rect) const;
    int ScalePointFromScreenEntry(POINT* point) const;
    int ScalePointFromScreenCopyEntry(POINT* point) const;

    // Once per frame (main loop): closes the top window while a value is pending,
    // closes all windows on request, activates a queued window, reads the mouse and updates the
    // top window and the root (ui_interface_frame.cpp).
    void Update();

    // A script string event ("say" / "say_block" text [key],
    // "tutorial" name, "briefing" name, "autosave", "endofgame"); the first word is lowercased.
    // Rebirth's NewStringEvents adds "gameover [title [text]]".
    void OnStringEvent(const char* event);
    // The window on top of the active-window stack, or null.
    CUiWindow* GetActiveWindow() const;
    // The top window as the screen the caller runs under (the HUD's panels under the CBattleScreen,
    // the briefing chat under the CBriefingScreen): the original reads its fields without a check.
    template <typename Screen>
    Screen* ActiveWindowAs() const {
        return static_cast<Screen*>(GetActiveWindow());
    }
    // Stores a value and raises the flag next to it (consumed by the frame update).
    void SetPendingValue(std::uint32_t value);
    // Makes screen `id` (1 in-game, 2, 3 base, 4 island map, 5, 7, 8, 9) and hands
    // it to the activation; 0 does nothing, 6 and ids above 9 hand over the id
    // itself as the window pointer (as the original). ui_create_screen.cpp.
    void CreateScreen(int id);
    // The interface's key-down handler, `flags` the WM_KEYDOWN lParam bits:
    // scan code in the low 9 bits, bit 14 repeat). Ctrl, Shift and Alt set +0x7C, +0x7D, +0x7E
    // (not on a repeat); the key under Esc toggles the console; Scroll Lock saves
    // "shot<nnnn>.bmp"; other keys go to the shown console, else (multiplayer client) the window
    // at +0x1C when its +0x14C is set, else the active window, and a ';' key nobody handled
    // shows help. ui_interface_keys.cpp.
    void OnKeyDown(int key, int repeat, int flags);
    // Opens `window` on top of the stack. With `a` set (low bytes) and `b`
    // clear: +0x50 = 3 and a frame shown (0, 1) first; for the first window: +0x60 keeps the server
    // flag, which is then set (not as a multiplayer client), and with `a` and `b` set +0x50 = 1
    // and a frame shown (0, 1). The modifier flags are cleared, the old top deactivated (input
    // virtual 1, virtual 19 with 0), the window pushed (null is fatal), five render states reset, the window
    // created (virtual 25) and the messages pumped. ui_create_screen.cpp.
    void PushWindow(CUiWindow* window, int a, int b);
    // Replaces every window with `window`: a pending close-all (+0x44) runs
    // first; both backdrops (+0x54, +0x58) are deleted and +0x50 / +0x60 cleared, the
    // server flag cleared; each open window is deactivated (input slot 1), closed (slot 26) and
    // deleted, top first; then the message pump, and the window opened as PushWindow
    // does (null is fatal). ui_create_screen.cpp.
    void Activate(CUiWindow* window);
    // Reads the interface's saved state from `stream` (slot 1 reads bytes;
    // +0x08 the save version): +0x89..+0x8B, +0x8C, +0x90; from version 0x47 +0x94; from 0x56 an
    // obsolete list (count, then name and value pairs, discarded) and the CString +0xA4; from 0x5D
    // +0xA8; from 0x5F the CString +0xAC. ui_interface_save.cpp.
    void LoadState(CStream* stream);
    // The text of message `id` (0-12:
    // "lmp_player_connected" .. "reserved_02"; others an empty key) from the text table,
    // formatted with `value` when it is not 0, constructed into `out`; returns `out`
    // (ui_interface_save.cpp).
    mfc::CString* MessageText(void* out, int id, const char* value);
    // Without `force`, takes the next queued message (the list at +0x84) into
    // +0xA4 (+0xA8 = 0), or returns when there is none. A non-empty +0xA4 then opens the message
    // window (0x194 bytes) over the others, unless the option
    // is off and it was not forced. ui_create_screen.cpp.
    void ShowMessage(int force);
    // The active window's help
    // topic (its slot 41; none: nothing): the map at +0x98 counts topics seen (an unseen one is
    // entered with 0, giving -1 here); the help window (0x194 bytes)
    // opens when forced (`flag`), or when the option is on and the topic was not seen.
    void OpenHelp(int flag);
    // Plays an interface sound (`name`, e.g.
    // "buttons\battle\click.wav") without position: priority 1000, volume 100, `loop` passed on
    // (sound::CSoundManager::Play2D). The interface object is not used. Returns Play2D's handle.
    int PlaySound(const char* name, int loop);
    // Stops sound `handle` unless it is -1 (the interface is not used).
    void StopSound(int handle);
    // A multiplayer client's screens call them for
    // Enter / Backspace). While the chat line is closed: Enter opens it (SetOpen(1)), Backspace
    // empties the chat lines (ClearLines).
    void ChatKeyEnter();
    void ChatKeyBackspace();
    // The extent of `text` (a CString taken by value) in interface font `font`, measured on the game window's DC (GetTextExtentPoint32)
    // and scaled to the layout: cx * 800 / width, cy * 600 / height. Returns `out`. Rebirth-patched, WideScreen
    // reproduced behind its flag; ui_text_draw.cpp.
    SIZE* TextExtent(SIZE* out, mfc::CString text, int font);
    // The constructor of the global interface object, run by
    // its static initialiser before the hook is installed: the window stack empty, the
    // message list's sentinel and the help topic map's header from the node pool, the two texts
    // empty. Nothing else is written. Returns this.
    CInterface3D* Construct();
    // The console window's AddText / SetRect / SetHistory,
    // when there is one.
    void ConsoleAddText(const char* text);
    void SetConsoleRect(const RECT* rect);
    void SetConsoleHistory(int lines);
    // The debug information panel's rectangle (the interface is not
    // used).
    void SetDebugInfoRect(const RECT* rect);
    // The console's "fps". With `on` (low byte) a surface for the frame rate at
    // (730, 10)-(800, 28) unless there is one; without, the surface freed.
    void ShowFps(int on);
    // The client and the script tell it of a quest variable change. The top
    // window's slot 33 hears of it; at 1 (or NaN) a name of three or more '.' parts keeps its
    // third part in savedText_ (the current quest).
    void OnQuestVariable(const char* name, float value);
    // News for the top window (ui_interface_glue.cpp; nothing without a window): items picked up,
    // a unit joined / left the party (the client's UnitChanged picks one by the unit's +0x24C), the game
    // is lost, a unit speaks (no extra), the players changed, an exchange between players ended;
    // and the bottom window saves its state (the save game). Each reaches the top window's
    // virtual of that name.
    void OnItemsPicked(void* items, int money, int crippled);
    void OnUnitJoined(CUnit* unit);
    void OnUnitLeft(CUnit* unit);
    void OnGameLost();
    void OnUnitSpeech(int id, unsigned event);
    void OnPlayersChanged(int added);
    void OnExchangeDone(CParty* partyB, CParty* partyA, CParty* partyC);
    void SaveWindowState();
    // A chat line (`text` from player `number`) for the root window's chat
    // (on +0x1C).
    void OnChatLine(const char* text, std::uint32_t number);
    // The save / load window while it saves or loads. `text` drawn in green
    // (rect (0, 0)-(800, 40), format 2) on a cleared surface over (0, 280)-(800, 320)
    // (+0x64), one frame shown (1, 1), the surface freed.
    void ShowStatusText(const char* text);

    // Mouse messages from the input dispatcher (ui_interface_input.cpp): each goes to the input side
    // that took the mouse (+0x0C), else to the top window's, else nowhere. Left down (clears +0x7F first), left up (+0x7F
    // cleared after), move, right down / up (+0x80 set / cleared
    // first), left double click (+0x7F set first), right double click (+0x80
    // set first), wheel (returns the handler's result, 0 without a target).
    void OnLeftDown(int buttons, int x, int y);
    void OnLeftUp(int buttons, int x, int y);
    void OnMouseMove(int buttons, int x, int y);
    void OnRightDown(int buttons, int x, int y);
    void OnRightUp(int buttons, int x, int y);
    void OnLeftDouble(int buttons, int x, int y);
    void OnRightDouble(int buttons, int x, int y);
    int OnWheel(int flags, int delta, int x, int y);
    // The input side mouse events go to: the one that took the mouse, else the top window's.
    UiInputSide* MouseTarget() const;
    // A typed character (WM_CHAR): nothing for the
    // key under Esc (scan 0x29); the shown console's OnChar, else a multiplayer client's open chat
    // line (+0x1C), else the top window (input slot 10).
    void OnChar(int key, int repeat, int flags);
    // A key released: VK_SHIFT / VK_CONTROL / VK_MENU clear +0x7D / +0x7C /
    // +0x7E; then the shown console's OnKeyUp, else the top window (input slot 11).
    void OnKeyUp(int key, int repeat, int flags);
    // Closes every window, top first (input virtual 1, virtual 26, deleting destructor), with
    // the cursor's shape 3 first; clears the five flags +0x7C..+0x80, pumps the messages and
    // clears the screen flag (InterfaceHasScreen).
    void CloseAll();
    // The input dispatcher's first step. CloseAll when +0x44 asks for it (cleared).
    void CloseAllPending();
    // Every frame from the main loop (ui_interface_draw.cpp). A pending close-all
    // (CloseAllPending); the backdrop (+0x54, its blit) in backdrop
    // modes 2-4; the top window's draw with the renderer; then the fade over it all.
    // Returns 1.
    int Draw();
    // Rebirth-patched (ui_interface_draw.cpp, reproducing WideScreen, so hooked in both
    // launches; the main loop calls it after the scene's EndScene). What is drawn over the scene with GDI and blits: the debug panel hears the
    // surfaces' kept and allocated bytes; the top window redraws its surfaces and
    // tooltips; the status surface blits; a backdrop asked for (mode 1 or 3) is made
    // (800 x 600, or 400 x 300 stretched when the stretch option is set; with Rebirth's
    // WideScreen the mode's width x height in pixels, unscaled) and takes the screen:
    // mode 1 also keeps a 256 x 192 copy (+0x5C) and greys the backdrop (mode 2), mode 3 first
    // puts the old backdrop aside (+0x58, its contents kept in memory) (mode 4); a multiplayer
    // client with one window redraws the chat overlay; the debug panel ticks; the frame rate
    // ("FPS:" and frames * 1000 / ms, every 400 ms) is drawn and blitted while shown; the shown
    // console blits.
    void DrawOverlays();
    // Rebirth-patched (ui_interface_create.cpp, reproducing WideScreen, so hooked
    // in both launches; the application's start calls it with the
    // renderer's device object). The screen size is the render target's (not written
    // usefully without a primary surface). The three fonts are "Times New Roman": a 75-point one
    // is made first and its height in pixels / 100 is the scale s (with Rebirth's WideScreen its
    // height times width * 0.0075 / height: widescreen::FontScale); the fonts are of
    // _ftol(width * k / s / 800) tenths of a point, k = 104, 112, 144 (a font that cannot be
    // made is fatal: "CInterface3D::Create.\n Cannot create font.\n", and nothing else is
    // done). The keyboard layout of the game's language is activated (each layout is activated
    // in turn and its name read as hex), else the English one (0x409), else the one it began
    // with. Then the key bindings are read, the console window (0x7C bytes), the debug
    // information panel, the chat overlay (0x158) and the fade (0x14) are made, the state is
    // cleared and config\tutorial.ini's "topic count" lines are read into the help topics.
    void Create(CTextureDevice* device);

    int width_;                      // +0x00 screen width
    int height_;                     // +0x04 screen height
    struct UiRenderer* renderer_;    // +0x08 (its +0x54 is the Direct3D 7 device)
    void* mouseCapture_;             // +0x0C input side (window +0x94) that took the button press
    std::uint8_t unused_10[0x14 - 0x10];
    console::ConsoleWindow* console_;   // +0x14 the console window (shown while its +0x7A is set)
    CScreenFade* fade_;              // +0x18
    CChatOverlay* root_;             // +0x1C the chat overlay; updated after the top window every frame
    CUiSurface* fpsSurface_;         // +0x20 the frame rate's surface while shown (ShowFps)
    int fpsSpare_;                   // +0x24 0 from Create with the frame rate fields; nothing reads it
    DWORD fpsTime_;                  // +0x28 when the frame rate was last worked out (DrawOverlays)
    int fpsFrames_;                  // +0x2C frames since then
    // +0x30 the windows draw, redraw their surfaces and update their tooltips only while this is
    // set (the containers' slots 20, 22 and 23).
    bool drawing_;
    std::uint8_t unused_31[0x34 - 0x31];
    CUiWindow** stackBegin_;         // +0x34 stack of active windows (STLport vector)
    CUiWindow** stackEnd_;           // +0x38
    CUiWindow** stackCapacity_;      // +0x3C
    CUiWindow* queuedWindow_;        // +0x40 activated by the next frame update
    bool closeAll_;                  // +0x44 every window is closed
    std::uint8_t unused_45[0x48 - 0x45];
    std::uint32_t pendingValue_;     // +0x48
    bool pendingFlag_;               // +0x4C
    std::uint8_t unused_4D[0x50 - 0x4D];
    int backdropMode_;               // +0x50 the backdrop is drawn in modes 2, 3 and 4 (PushWindow sets 3 / 1)
    CUiSurface* backdrop_;           // +0x54
    CUiSurface* secondBackdrop_;     // +0x58
    void* block5C_;                  // +0x5C freed by the shutdown with operator delete (writer not identified)
    std::uint8_t serverFlag_;        // +0x60 the server flag kept for the first window (PushWindow)
    std::uint8_t unused_61[0x64 - 0x61];
    CUiSurface* statusSurface_;      // +0x64 only while ShowStatusText runs
    int mouseX_;                     // +0x68 mouse position in screen coordinates
    int mouseY_;                     // +0x6C
    // +0x70..+0x78 the three interface fonts (Create: "Times New Roman", sized with the
    // screen width); text drawing selects font n from here.
    mfc::impl::CFont* fonts_[3];
    // Set by the key-down handler from the scan codes, cleared by the key-up one
    // from the virtual keys and when the windows change.
    bool ctrlDown_;                  // +0x7C
    std::uint8_t shiftDown_;         // +0x7D
    bool altDown_;                   // +0x7E
    // The left press is a double click (set by the double-click handler, the button's down and up clear it); a
    // spell or quick item clicked twice is used at once.
    bool doubleClick_;               // +0x7F
    bool rightDown_;                 // +0x80 the right button is held
    // +0x81 the top window is asked every frame until it agrees ("autosave" sets it).
    bool pollTop_;
    std::uint8_t unused_82[0x84 - 0x82];
    stlp::list<mfc::CString> messages_;   // +0x84 queued message texts (ShowMessage takes the first)
    bool disconnected_;              // +0x88 a network client lost the server (the frame shows a message)
    // +0x89..+0x97: the state saved with a game (LoadState), cleared by ResetSavedState. The HUD
    // panels keep theirs here every frame: in (or coming in) for the unit panel, the top panel and
    // the map, the unit panel's body part and the top panel's page.
    std::uint8_t hudPanelsIn_[3];    // +0x89 [0] the unit panel, [1] the top panel, [2] the map
    int hudBodyPart_;                // +0x8C CHudUnitPanel::bodyPart_
    int hudTopPage_;                 // +0x90 CHudTopPanel::page_
    int fastGame_;                   // +0x94 the fast game speed (the clock's +0x154)
    // +0x98 help topics seen: topic name -> times (or the page the help window was closed on; see ui_help_window.cpp) (config\tutorial.ini, "name count" lines; read
    // by Create, written by Shutdown, counted by OpenHelp).
    stlp::map<mfc::CString, int, stlp::CStringLess> helpTopics_;   // lib_trees::HelpTopics
    mfc::CString messageText_;       // +0xA4 the message window's text (ShowMessage)
    int messageFlag_;                // +0xA8 (ShowMessage clears it; the message window's page, set when it closes)
    mfc::CString savedText_;         // +0xAC the current quest (OnQuestVariable); saved from version 0x5F

    // Clears the modifier, double-click and right-button flags (+0x7C..+0x80) and
    // deactivates the top window's input side (input slot 1), if any (ui_interface_shutdown.cpp).
    void ResetInput();
    // Called at the main loop's exit. Closes every window (as CloseAll, without its
    // trailing deactivation), ResetInput, pumps the messages, clears the screen flag; writes the
    // help topics to config\tutorial.ini ("%s %d\n" each); then frees the frame rate surface (+0x20), the queued window
    // (+0x40, not cleared), both backdrops, +0x5C, the debug panel's surface and font,
    // the root window, the console window; saves the key bindings; frees the three fonts
    // and the fade (+0x18, not cleared).
    void Shutdown();
    // The destructor of the global interface, run at exit: the two CStrings,
    // the help topic map, the message list, the window stack's storage.
    void DestroyBody();

    // Writes what LoadState reads: +0x89..+0x8B, +0x8C,
    // +0x90, +0x94, an empty obsolete list (count 0), the message text with its zero, +0xA8 and
    // the saved text with its zero.
    void SaveState(CStream* stream);

    // Called by the main menu's Create (ui_main_menu.cpp). Clears the saved state: +0x89..+0x8B,
    // +0x8C, +0x90 and +0x94.
    void ResetSavedState();
};
static_assert(offsetof(CInterface3D, mouseCapture_) == 0x0C);
static_assert(offsetof(CInterface3D, doubleClick_) == 0x7F);
static_assert(offsetof(CInterface3D, hudPanelsIn_) == 0x89 && offsetof(CInterface3D, hudTopPage_) == 0x90);
static_assert(offsetof(CInterface3D, fastGame_) == 0x94 && offsetof(CInterface3D, messages_) == 0x84);
static_assert(offsetof(CInterface3D, stackBegin_) == 0x34);

static_assert(offsetof(CInterface3D, pendingValue_) == 0x48);
static_assert(offsetof(CInterface3D, mouseX_) == 0x68);
static_assert(offsetof(CInterface3D, messageText_) == 0xA4 && offsetof(CInterface3D, savedText_) == 0xAC);
static_assert(offsetof(CInterface3D, console_) == 0x14);
static_assert(offsetof(CInterface3D, root_) == 0x1C);
static_assert(offsetof(CInterface3D, drawing_) == 0x30);
static_assert(offsetof(CInterface3D, closeAll_) == 0x44);
static_assert(offsetof(CInterface3D, rightDown_) == 0x80 && offsetof(CInterface3D, pollTop_) == 0x81 &&
              offsetof(CInterface3D, disconnected_) == 0x88);
static_assert(offsetof(CInterface3D, hudBodyPart_) == 0x8C);
static_assert(offsetof(CInterface3D, fastGame_) == 0x94);
static_assert(offsetof(CInterface3D, fpsSurface_) == 0x20 && offsetof(CInterface3D, queuedWindow_) == 0x40);
static_assert(offsetof(CInterface3D, statusSurface_) == 0x64);
static_assert(offsetof(CInterface3D, block5C_) == 0x5C && offsetof(CInterface3D, fonts_) == 0x70);
static_assert(offsetof(CInterface3D, messages_) == 0x84 && offsetof(CInterface3D, helpTopics_) == 0x98);
static_assert(sizeof(CInterface3D) == 0xB0);

// The game's interface manager (the global object; ui_small_members.cpp): built by its static initialiser
// in the list's order, destroyed by the exit function it registers.
extern lib::StaticObject<CInterface3D> g_interface;

// The interface's growable pointer arrays: engine aligned arrays of pointers (lib::AlignedArray),
// each with its out-of-line SetSize and SetAtGrow, registered on AlignedArray's members; Add,
// RemoveAt and RemoveAll are inlined.
template <typename T>
using UiPtrArray = lib::AlignedArray<T*>;

// The tooltip attached to a region (CUiWindow, below).
class CUiRegion;
struct CUiTooltip;

struct UiPoint {
    int x;
    int y;
};

// A clickable area of a window (0x34 bytes, created by CUiWindow::AddRegion): a rectangle,
// optionally narrowed to a convex polygon, and an optional tooltip.
class CUiRegion {
public:
    // Whether (x, y) lies in the region: inside the bounds, and unless the region is a
    // plain rectangle, on the inner side of every polygon edge.
    BOOL HitTest(int x, int y) const;
    // Whether the tooltip is showing.
    BOOL HasActiveTip() const;
    // Makes the region the rectangle (left, top, right, bottom) and enables it.
    void SetRect(int left, int top, int right, int bottom);
    // Drops the shape and disables the region.
    void ClearShape();
    // Adds a polygon vertex, growing the bounds; three vertices enable the region.
    void AddPoint(int x, int y);
    // Updates the tooltip state for a mouse position and forwards it to the tooltip.
    void OnMouseMove(int x, int y);
    // The destructor body: deletes the tooltip and frees the
    // vertex storage (the fields are left as they are).
    void ReleaseContents();
    // Sets the tooltip's text when there is a
    // tooltip.
    void SetTipText(const char* text);
    // Replaces the tooltip by one with the text
    // "tip <id>" (up to a '#'; the key itself when there is no such text) and (a, b); id 0 gives
    // an empty tooltip with (1, 0).
    void SetTip(int textId, int a, int b);
    // Replaces the tooltip by one showing `text`.
    void SetTipString(const char* text, int a, int b);
    // SetRect from a RECT
    // passed by value; the menus' layouts use it for their menus.reg rectangles.
    void SetRectFrom(RECT rect);

    CInterface3D* interface_;        // +0x00
    lib::AlignedArray<UiPoint> points_;   // +0x04 polygon vertices (aligned storage)
    RECT bounds_;                    // +0x14
    bool rectOnly_;                  // +0x24
    bool enabled_;                   // +0x25
    std::uint8_t unused_26[2];
    int offsetX_;                    // +0x28 subtracted from tested points
    int offsetY_;                    // +0x2C
    CUiTooltip* tip_;                // +0x30
};
static_assert(offsetof(CUiRegion, bounds_) == 0x14);
static_assert(offsetof(CUiRegion, offsetX_) == 0x28);
static_assert(sizeof(CUiRegion) == 0x34);

// An off-screen DirectDraw surface a window draws into (0x38 bytes; its first virtual is the
// blit; no destructor: the owners destroy it directly).
class CUiSurface {
public:
    // A surface of the layout rectangle (left, top, right,
    // bottom) with format `format`, scaled to the screen when the low byte of `flag` is set.
    CUiSurface(CInterface3D* ui, int left, int top, int right, int bottom, int format, int flag);
    // Over the layout rectangle `rect` (always scaled) with
    // format `format`.
    CUiSurface(CInterface3D* ui, const RECT* rect, int format);
    // Showing the picture "<name>.mmp" of the texture manager
    // at its own size (screen rectangle 0, 0, width, height).
    CUiSurface(CInterface3D* ui, mfc::CString name);
    // The kept bytes and the DirectDraw surface released, the screen
    // rectangle emptied. Not virtual (a window's surface list
    // destroys a scrolling surface as a plain one).
    ~CUiSurface();
    // The game's heap (null without memory: then no constructor runs).
    static void* operator new(std::size_t size) noexcept;
    static void operator delete(void* block);
    // Blits the surface onto the render target when it is shown.
    virtual void Blit();

    // 0x005FD240 (ui_surface_pixels.cpp).
    std::uint32_t PixelAt(int x, int y);
    // Sets the screen rectangle from layout coordinates.
    void SetRect(int left, int top, int right, int bottom);
    // Moves the screen rectangle so its top-left is the layout point (x, y).
    void MoveTo(int x, int y);
    // Word-wraps `text` to the width of `rect` (layout coordinates; breaks at spaces and
    // '\n') and draws up to `maxLines` lines one rect height apart with DrawText (`font`,
    // `colour`, `format`, `flag` passed on). The DC must be acquired. Returns the line count
    // (ui_text.cpp).
    int DrawWrappedText(const char* text, const RECT* rect, int maxLines, int font, COLORREF colour, int format,
                        int flag);
    // Rebirth-patched, WideScreen reproduced behind its flag (ui_text_draw.cpp; callers go through SurfaceDrawText).
    // Draws `text` in
    // interface font `font` and `colour` into `rect` (layout coordinates scaled to the screen,
    // then two pixels wider and higher; an empty rect is the whole surface) with the
    // DrawText `format`, transparent; with `shadow` first in 0x080808 one pixel right and down.
    // The DC must be held. Returns DrawText's height scaled back to the layout, or 0
    // without a surface.
    int DrawTextIn(const char* text, const RECT* rect, int font, COLORREF colour, UINT format, int shadow);
    // The surface's DC held and
    // released (UiSurfaceGetDC / UiSurfaceReleaseDC below).
    mfc::impl::CDC* GetDC();
    void ReleaseDC();
    // Through its address (Rebirth patches it; ui_text_draw.cpp): DrawTextIn's line of text.
    int DrawLine(const char* text, RECT* rect, int font, std::uint32_t colour, int format, int shadow);
    // Sets (`frozen` nonzero) or clears the shown flag (4 of
    // +0x0C): setting it puts kept contents (+0x1C) back into a surface; clearing
    // it with `restore` set keeps the contents in memory and releases the surface.
    void Freeze(int frozen, int restore);
    // Copies the screen's rect_ into the surface (nothing when it is the screen).
    // A new[] block of width * height 5:6:5 pixels: the surface
    // stretched to that size through a temporary surface. Greys the surface in place
    // ((2 r + 4 g + b) / 16 per channel). ui_surface_pixels.cpp.
    // `restore` 0: copies the DirectDraw surface's memory (pitch * height bytes)
    // to +0x1C (size +0x20) and releases the surface; 1: makes a new surface (+0x14 x +0x18) when
    // there is none and copies the kept bytes back into it, freeing them. Both keep the memory
    // totals of kept bytes and surface bytes.
    void SwapContents(int restore);
    void CaptureScreen();
    std::uint16_t* Rgb565Copy(int width, int height);
    void Greyscale();

    bool Shown() const { return (flags_ & kShown) != 0; }
    static constexpr std::uint32_t kShown = 4;   // set and cleared by Freeze

    // Calls the surface's contents function through its address (the wrapper runs).
    void CallSwapContents(int restore);
    // With a surface (and its DC held): a one-pixel frame
    // of `colour` (a GDI solid brush) around `rect` (layout coordinates, scaled to the screen)
    // or, for an empty rect, around the whole surface.
    void FrameRect(const RECT* rect, COLORREF colour);
    // The whole
    // surface filled with `colour` / with background_ (CDC::FillSolidRect) (ui_surface_frame.cpp).
    void Fill(COLORREF colour);
    void Clear();

    static constexpr std::uint32_t kFromPicture = 1;   // flags_: made from an .mmp picture

    CInterface3D* interface_;        // +0x04
    HDC dc_;                         // +0x08 while the DC is held
    std::uint32_t flags_;            // +0x0C (the constructors store the format here, then set kShown)
    CDXSurface* holder_;             // +0x10 the DirectDraw surface's holder (null while swapped out)
    int width_;                      // +0x14
    int height_;                     // +0x18
    std::uint8_t* kept_;             // +0x1C the contents kept in memory while swapped out
    std::uint32_t keptSize_;         // +0x20
    RECT rect_;                      // +0x24 screen coordinates
    COLORREF background_;            // +0x34 the colour Clear fills with (0 from the constructors)
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
// The surface's CDC (CUiSurface::GetDC / ReleaseDC; a temporary
// MFC wrapper; null without a DirectDraw surface) and the matching release.
mfc::impl::CDC* __fastcall UiSurfaceGetDC(CUiSurface* surface);
void __fastcall UiSurfaceReleaseDC(CUiSurface* surface);
inline mfc::impl::CDC* CUiSurface::GetDC() { return UiSurfaceGetDC(this); }
inline void CUiSurface::ReleaseDC() { UiSurfaceReleaseDC(this); }

// The memory totals the window surfaces keep: the bytes of the
// contents kept in memory while swapped out and the surfaces' height * pitch.
inline std::uint32_t g_uiSurfaceKeptBytes = /*007C7E74*/ {};
inline std::uint32_t g_uiSurfaceBytes = /*007C7E78*/ {};
inline std::uint32_t& UiSurfaceKeptBytes() { return g_uiSurfaceKeptBytes; }
inline std::uint32_t& UiSurfaceBytes() { return g_uiSurfaceBytes; }
// The game's device lock, held around DirectDraw calls on the game's surfaces.

class CObject3D;

// A 3D model shown in a window (an inventory item, a portrait): one pointer to a scene object
// (a CObject3D). ui_model.cpp unless noted.
class CUiModel {
public:
    // Places the object in front of the camera at a layout position
    // and depth.
    void SetScreenPosition(float x, float y, float depth);
    // Releases the object and clears the pointer.
    void ReleaseObject();
    // Draws the object in `pass` (the scene renderer a window's draw is given) in white at the
    // interface's
    // animation clock (timeGetTime / 67), with object flag 0x800 set while it draws.
    void Draw(CSceneRenderer* pass);
    // A new map object (class 0x33, id 0x88CA6C00) of the client world's map
    // object manager, referenced, flag 0x4000000, at (0, 0, 3), with the figure of model `name`
    // (all parts), its textures (on +0x94), animations
    // and texture `from` swapped for `to` (e.g. "escMenu00" / "escMenu00labels").
    void Load(const char* name, const char* from, const char* to);
    // Shows or hides all its parts (part flag 0x80), or one part by
    // name.
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

// An input side: a window's own at +0x94 (CUiWindow's second base), or a separate handler a container
// keeps in its list (+0x158; e.g. the battle camera's keys, CCameraInput). The interface hands each
// event to the top window's side: 1 deactivate (the window loses the
// top); 2 / 3 left button down / up, 4 mouse move, 5 / 6 right button down / up, 7 / 8 left /
// right double click (button flags, x, y in screen pixels); 9 key down, 10 character (WM_CHAR), 11 key
// up (key, repeat, scan code and flags); 12 wheel (key flags, delta, x, y). The handlers return EAX,
// which only the wheel's callers read. Its own table is the base input side: 1-6 and
// 9-11 do nothing, 7 / 8 a double click is a press, 12 the wheel is not used.
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
    virtual void Deactivate() {}                                           // 1
    virtual int LeftDown(int, int, int) { return 0; }                      // 2
    virtual int LeftUp(int, int, int) { return 0; }                        // 3
    virtual int MouseMove(int, int, int) { return 0; }                     // 4
    virtual int RightDown(int, int, int) { return 0; }                     // 5
    virtual int RightUp(int, int, int) { return 0; }                       // 6
    // A double click is handled as a single
    // press, handler 2 / 5 with the same arguments; that handler's EAX is returned.
    virtual int LeftDouble(int buttons, int x, int y);                     // 7
    virtual int RightDouble(int buttons, int x, int y);                    // 8
    virtual int KeyDown(int, int, int) { return 0; }                       // 9
    virtual int Char(int, int, int) { return 0; }                          // 10
    virtual int KeyUp(int, int, int) { return 0; }                         // 11
    // The wheel is not used, 0.
    virtual int Wheel(int flags, int delta, int x, int y);                 // 12
    // Slots 2-11 by number (the containers' dispatch): (buttons, x, y) or (key, repeat, flags).
    int Event(int slot, int a, int b, int c);

    // Input sides live on the game's heap (ui_window_delete.cpp: gamecrt::operator_new / delete); `new`
    // gives null without memory and then constructs nothing.
    static void* operator new(std::size_t size) noexcept;
    static void* operator new(std::size_t, void* place) noexcept { return place; }
    static void operator delete(void* block);
    static void operator delete(void*, void*) noexcept {}

    CInterface3D* interface_;        // +0x04
    bool handled_;                   // +0x08 set by a handler that used the event
};

// The extra input side whose handlers do nothing (0x0C bytes; two of them are empty): the battle screen's, the briefing's, the main menu's and the view
// screens'.
struct UiInertInput final : UiInputSide {
    explicit UiInertInput(CInterface3D* ui) : UiInputSide(ui) {}
    ~UiInertInput() override;
    int KeyDown(int, int, int) override { return 0; }   // 9
    int KeyUp(int, int, int) override { return 0; }     // 11
};

// The window's part below its input side (+0x00..+0x94): the engine's transform object and 0x40 bytes the
// window's code never touches (the original's window base, inlined in the window's constructor).
class CUiWindowBase : public CTransformObject {
protected:
    std::uint8_t unused_054[0x94 - 0x54];
};

// Base of the interface windows. Its input side is
// its second base, at +0x94: the handlers get `this` = window + 0x94 (the compiler's adjustor thunks,
// like the game's SUB ECX,0x94; JMP).
class CUiWindow : public CUiWindowBase, public UiInputSide {
public:
    // An empty window of `ui` (arrays, the rectangle and +0xA0..+0xD7
    // zeroed, the identity rotation), two materials of the global manager (+0xF0 white, +0xEC black; one
    // reference each), enabled (+0xE8 = 1, then Show(1)).
    explicit CUiWindow(CInterface3D* ui);
    // Releases the three objects at +0xEC..+0xF4, the scene objects, surfaces,
    // regions, referenced objects and models (in that order), clears +0x08, then the engine object's. The
    // deleting destructor and its copies (ui_window_delete.cpp).
    ~CUiWindow() override;
    using CEngineObject::operator new;
    using CEngineObject::operator delete;

    // The interface objects' slots (world_class_slots.h): 2 answers 1, 3 does nothing, 11 / 12 the name
    // "Sorry, no other way." and the id 0x45.
    int Create() override;
    void Destroy() override {}
    const char* GetClassName() override;
    int GetClassId() override;
    virtual void Build() {}
    virtual void Show(int show) { ShowSurfaces(show); }                // 19
    virtual BOOL Draw(int pass) { return DrawBase(pass); }             // 20
    virtual void DrawChild(int pass) { UpdateRegionTips(pass); }       // 21
    virtual void Redraw() { RedrawBase(); }                            // 22
    virtual void RedrawTips() { RedrawRegionTips(); }                  // 23
    virtual void Update() {}


    // Index of the first region containing the screen point (x, y), or -1.
    int RegionAt(int x, int y) const;
    // Whether the screen point (x, y) lies in the window rectangle.
    BOOL ContainsPoint(int x, int y) const;
    // Updates every region's tooltip with the current mouse position (argument unused).
    void UpdateRegionTips(int unused);

    // Sets the enabled flag; with `notify` also calls Show with it.
    void SetEnabled(int enabled, int notify);

    // Appends an object and takes a reference (null is ignored).
    void AddObject(CEngineObject* object);
    // Releases an object and removes it from the list.
    void RemoveObject(CEngineObject* object);
    void RemoveObjectAt(int index);
    // Every referenced object released, the list emptied (its storage freed).
    void ClearObjects();

    // The second object list (+0x108).
    BOOL RemoveSceneObject(CEngineObject* object);
    // Replaces scene object `object` (released) at its place in the list with
    // object `name` of the model manager, given the window's texture 0 and taking the
    // old one's position, +0xCC/+0xD0, orientation, texture and scale; the new one or null.
    CEngineObject* ReplaceSceneObject(CEngineObject* object, const char* name);
    void RemoveSceneObjectAt(int index);
    void ClearSceneObjects();

    // Surfaces the window owns.
    CUiSurface* AddSurface(CUiSurface* surface);   // returns `surface`
    void RemoveSurfaceAt(int index);
    void ClearSurfaces();

    // Regions (AddRegion returns the new, empty region).
    CUiRegion* AddRegion();
    void RemoveRegionAt(int index);
    void ClearRegions();

    // 3D model previews.
    CUiModel* AddModel();
    void ClearModels();

    // The input side (+0x94).
    UiInputSide* Input() { return this; }
    // Takes the mouse for the next button-up (what every button-down handler does).
    void CaptureMouse() { interface_->mouseCapture_ = Input(); }
    // Button-up handlers act only when they took the mouse; they release it first.
    bool ReleaseMouse() {
        if (interface_->mouseCapture_ != Input()) return false;
        interface_->mouseCapture_ = nullptr;
        return true;
    }
    // Base drawing: the scene objects through the sprite draw
    // then the models; returns 1.
    BOOL DrawBase(int pass);
    // Used by the camp and the multiplayer hero screen. Draws `model` (a hero) as a
    // preview: clears the depth buffer (all of it, then the preview's screen rectangles), turns it
    // by `turn` radians about y and places it by `depth` (`wide`'s low byte: six rectangles of the
    // camp layout, else one), draws it lit through `pass` and puts its transform back
    // (ui_hero_preview.cpp).
    void DrawHeroPreview(CSceneRenderer* pass, CUnit* hero, float turn, float depth, int wide);
    // Writes image "<name>.mmp" (decoded as imageWidth x imageHeight by the
    // device's decoder) into `texture`'s 256-wide 1:5:5:5 surface: its top-left width x height at
    // (x, y), or white there when there is no such image. Returns whether the image exists (0 also
    // when the lock fails and the error report returns 0). ui_item_view.cpp.
    int CopyImage(CEngineObject* texture, const char* name, int imageWidth, int imageHeight, int width, int height,
                  int x, int y, int unused1, int unused2);
    // The texture manager's texture `name` of `kind` (found or
    // made), loaded (or from `directory`) and appended to the
    // referenced objects with one more reference; null when either step fails (a texture made but
    // not loaded is kept by the manager).
    CEngineObject* LoadTexture(const char* name, int kind, const char* directory);
    // A new CPU-filled texture appended to the
    // referenced objects with one more reference and flag 0x40000; null when none is made.
    CEngineObject* CreateTexture(int width, int height, int kind);
    // A virtual of the plain windows (ui_window.cpp). `show` 0: every surface keeps its
    // contents in memory and lets its DirectDraw surface go; otherwise the shown
    // surfaces (flag 4) get theirs back.
    void ShowSurfaces(int show);
    // Object `name` of the model manager (null: a new
    // interface sprite, CUiMesh) added to the scene objects (once: one reference taken and the
    // object attached to the window), given the black material and the first referenced object
    // (or null) as its texture; returns it, or null when the manager makes none.
    CUiMesh* AddSprite(const char* name);
    // Base redraw: every shown surface blits itself.
    void RedrawBase();
    // Base update: each region's tooltip follows the mouse (as
    // UpdateRegionTips) and redraws itself, region by region.
    void RedrawRegionTips();
    // Scene object `index` as the interface sprite it is.
    CUiMesh* SceneMesh(int index) const;   // defined after CUiMesh

    std::uint8_t spareA0_[0xD8 - 0xA0];       // +0xA0 zeroed by the constructor; nothing reads it
    RECT rect_;                               // +0xD8 layout coordinates
    int enabled_;                             // +0xE8
    CEngineObject* blackMaterial_;            // +0xEC given to every sprite (AddSprite)
    CEngineObject* whiteMaterial_;            // +0xF0
    CEngineObject* heldObject_;               // +0xF4 0 from the constructor, released by the destructor
                                              //       with the two materials (writer not identified)
    UiPtrArray<CEngineObject> objects_;       // +0xF8 referenced objects
    UiPtrArray<CEngineObject> sceneObjects_;  // +0x108
    UiPtrArray<CUiSurface> surfaces_;         // +0x118
    UiPtrArray<CUiRegion> regions_;           // +0x128
    UiPtrArray<CUiModel> models_;             // +0x138
};
// The verify build's interface census (tests/verify/ui_census.cpp) is told which window owns each
// surface: AddSurface (`added`), RemoveSurfaceAt, ClearSurfaces and the destructor. Null otherwise.
extern void (*g_uiSurfaceOwnerObserver)(const CUiWindow* window, const CUiSurface* surface, bool added);
// ... and when a whole-surface pixel helper runs on a surface (CaptureScreen, Rgb565Copy, Greyscale,
// PixelAt, SwapContents; ui_surface_pixels.cpp), whether called through its game address or
// directly; `writes` for the ones that change the surface's pixels; `caller` the stack slot of the
// helper's return address (where its caller's chain starts). Null otherwise.
extern void (*g_uiSurfacePixelObserver)(const CUiSurface* surface, const char* what, bool writes, void* const* caller);
// The harness reads game.exe's windows (their regions and surfaces, the input side at +0x94).
static_assert(sizeof(CUiWindowBase) == 0x94);
static_assert(offsetof(CUiWindow, surfaces_) == 0x118 && offsetof(CUiWindow, regions_) == 0x128);
static_assert(sizeof(CUiWindow) == 0x148);

// The tooltip attached to a region (0x184 bytes, constructor
// in ui_tooltip.cpp). Its surface is made while it is shown.
struct CUiTooltip : CUiWindow {
    // The window, no lines, idle, of `region`; a non-empty
    // `text` is split at newlines into the lines and measured.
    CUiTooltip(CInterface3D* ui, CUiRegion* region, const char* text, int flag, int extra);
    // The lines (ui_tooltip.cpp).
    ~CUiTooltip() override;
    // A non-empty `text` replaces the lines and they are drawn
    // (the size is not measured again); null or empty: nothing.
    void SetText(const char* text);
    // Draws the lines into surface 0 (when there is one): filled with
    // 0x1C2B, framed in 0x31B3FF, then line by line in font 0 from (6, 4), lineHeight_ apart, the
    // first flag_ lines in 0x31B3FF and the others in 0xA7D7E4; the first line followed by
    // "  ( <key> )" when keyName_ is set.
    void DrawLines();
    // Rebirth-patched, WideScreen reproduced behind its flag (ui_tooltip.cpp). One step of the showing, with the mouse at (x, y)
    // in layout coordinates; the region has set the state to 1 (mouse inside) or 3 (left) before.
    // State 1: no lines or a zero delay (setting 20) gives 0; the timer runs up to the delay, then
    // the key of command extra_ is looked up and measured, the state becomes 2 and the surface is
    // made at the mouse + the cursor offset, kept on the 800x600 layout. State 2 draws and becomes
    // 4 (the appearing time is 0). State 3 draws while the timer is positive, then drops the
    // sprites and the surface and becomes 0. States 0 and 4: nothing.
    void OnMouseMove(int x, int y);
    // The window's draw (ui_menus.cpp). Draws only while shown; returns 1 otherwise.
    BOOL Draw(int pass) override;
    // Redraws (the window's base redraw) only while shown.
    void Redraw() override;
    bool Shown() const { return state_ != 1 && state_ != 0; }

    mfc::CStringArray lines_;        // +0x148 the text's lines (none for an empty text)
    int lineHeight_;                 // +0x15C the last line's height: the rows' step
    int keyWidth_;                   // +0x160 the first line with the key name, + 12
    int flag_;                       // +0x164 the constructor's fourth argument: the title lines (1 almost everywhere)
    int extra_;                      // +0x168 its fifth: a command whose key is shown
    const char* keyName_;            // +0x16C that key's name, or null
    CUiRegion* region_;              // +0x170 the region it belongs to
    int width_;                      // +0x174 widest line + 12 (a text only)
    int height_;                     // +0x178 the lines' heights + 8
    float timer_;                    // +0x17C seconds in the current state
    int state_;                      // +0x180 0 idle, 1 hovered, 2 appearing, 3 disappearing, 4 shown
};
static_assert(offsetof(CUiTooltip, lines_) == 0x148);
static_assert(offsetof(CUiTooltip, lineHeight_) == 0x15C);
static_assert(offsetof(CUiTooltip, flag_) == 0x164);
static_assert(offsetof(CUiTooltip, keyName_) == 0x16C);
static_assert(offsetof(CUiTooltip, region_) == 0x170);
static_assert(offsetof(CUiTooltip, state_) == 0x180);
static_assert(sizeof(CUiTooltip) == 0x184);

// The panels of the item screens (camp, multiplayer hero screen, exchange): the description panel,
// hero equipment, quick strip, trade and repair grids, constructor pages and the item strips, built on
// the blank panel. Their input sides
// share the left-button handler; ItemAt gives the item or help topic under the mouse.
class CUiItemPanel : public CUiWindow {
public:
    // The blank panel's constructor: the window base,
    // owner_ = `owner` (mode_ left).
    CUiItemPanel(CInterface3D* ui, CItemScreen* owner);
    // The item under the screen point (x, y), or
    // null; a panel may set a help topic instead. The panels without items of their own (the
    // description panel, the statistics column, the trade panel): nothing under the point, `topic` 0.
    virtual CItem* ItemAt(int x, int y, int* topic);
    int LeftDown(int b, int x, int y) override { return OnLeftDown(b, x, y), 0; }   // input 2

    // The left-button handler (ui_item_panel_input.cpp). The item under (x, y) goes to the
    // owner's MoveItem-like handler with mode_ (the camp's MoveItem); without one, a help topic the panel named
    // goes to the owner's help handler (the camp: the strip tabs for it). The event is not marked used.
    void OnLeftDown(int buttons, int x, int y);

    // The owner's handlers the panels call (the camp, the hero screen; ui_item_info.h).
    // Take `item` with the panel's action mode.
    void OwnerTakeItem(CItem* item);
    // Help topic `topic` clicked.
    void OwnerTopicClicked(int topic);

    int mode_;                       // +0x148 the action mode handed to the owner (ItemCell::mode)
    CItemScreen* owner_;             // +0x14C the item screen
};
static_assert(offsetof(CUiItemPanel, mode_) == 0x148);
static_assert(offsetof(CUiItemPanel, owner_) == 0x14C);


// The client world (CWorldClient, world.h).
// Option 17 of the options window: the numbers flying up from units hit are shown.
inline int& ShowHitNumbersOption() { return options::TheOptions()->drawFlyingHp_; }
// The debug information panel (string_map.cpp; the console's "debuginfo" rectangle).
strmap::CDebugPanel* DebugInfoPanel();

// The texture manager (CTextureManager, texture.h) the windows load their textures from.

// The in-game key bindings (object_tables.h g_keyActions), for reading and for writing.
inline const signed char* KeyActions() { return g_keyActions; }
inline signed char* KeyActionTable() { return g_keyActions; }
constexpr int kKeyActionCount = 0x200;

// The bounds-checked records of the spell and spell-modifier databases, as the
// spell book reads them: itemdb::SpellTable::At / SpellModifierTable::At (item_db.h).

// --- Battle screen and its HUD panels -----------------------
// The battle screen owns a row of panel windows, each a CUiWindow with its
// own vtables; the screen forwards mouse events to the panel whose rectangle holds the point.
// Panel handlers receive the panel's input side (window + 0x94) as `this`; the rewrites are
// members of the panel classes, registered through small shims that convert the pointer.
// Coordinates below are the 800x600 layout (Rebirth moves some panels).

class CItem;
class CHudQuickItems;
class CHudSpellColumn;
class CHudActionColumn;
class CHudWeaponBar;
class CHudPortraits;
class CHudUnitPanel;
class CBattleScreen;   // below CUiContainer
class CUiScrollText;  // below CBattleScreen

// An MFC CDWordArray (`growBy` -1 keeps the step).
using UiDwordArray = mfc::CArray<std::uint32_t>;

// The interface's renderer (CInterface3D +0x08), only as the HUD uses it.
// The IDirect3DDevice7 as the interface code calls it: each method is the interface's of the same
// name through device.h's calls (ui_d3d_device.cpp), so that in the switch-over without DirectDraw
// it has its result without a device.
struct UiD3DDevice {
    long Clear(DWORD count, void* rects, DWORD flags, DWORD colour, float z, DWORD stencil);
    long GetTextureStageState(DWORD stage, DWORD type, std::uint32_t* value);
    long SetTextureStageState(DWORD stage, DWORD type, DWORD value);
    long SetRenderState(std::uint32_t state, std::uint32_t value);
    long GetRenderState(std::uint32_t state, std::uint32_t* value);
    long ApplyStateBlock(std::uint32_t handle);

    void** vtable_;   // IDirect3DDevice7
};
struct UiRenderer {
    std::uint8_t unused_00[0x08];
    long result_;                    // +0x08 the last device call's result
    std::uint8_t unused_0C[0x4C - 0x0C];
    CPixelConverter* decoder_;       // +0x4C the pixel decoder (decodes an .mmp image's pixels)
    std::uint8_t unused_50[0x54 - 0x50];
    UiD3DDevice* device_;            // +0x54
    std::uint8_t unused_58[0x90 - 0x58];
    std::uint32_t caps_;             // +0x90 (0x4000 with stencil_: the depth buffer has a stencil)
    std::uint8_t unused_94[0x9C - 0x94];
    int stencil_;                    // +0x9C
};
static_assert(offsetof(UiRenderer, caps_) == 0x90);
static_assert(offsetof(UiRenderer, stencil_) == 0x9C);
static_assert(offsetof(UiRenderer, device_) == 0x54);

// Direct3D 7 render states the HUD sets around its drawing.
constexpr int kRenderStateAlphaTest = 15;    // D3DRENDERSTATE_ALPHATESTENABLE
constexpr int kRenderStateAlphaRef = 24;     // D3DRENDERSTATE_ALPHAREF
constexpr int kRenderStateSpecular = 29;     // D3DRENDERSTATE_SPECULARENABLE

// Weapon bar (four 40x80 slots at
// (80..240, 520..600).
class CHudWeaponBar : public CUiWindow {
public:
    void OnMouseDown(int buttons, int x, int y);
    void OnMouseUp(int buttons, int x, int y);     // selects a weapon
    void OnMouseMove(int buttons, int x, int y);
    // The input side: the left button and the mouse moves.
    int LeftDown(int b, int x, int y) override { return OnMouseDown(b, x, y), 0; }      // 2
    int LeftUp(int b, int x, int y) override { return OnMouseUp(b, x, y), 0; }          // 3
    int MouseMove(int b, int x, int y) override { return OnMouseMove(b, x, y), 0; }     // 4
    // Every frame. Follows the unit's weapon (+0x270) with the highlight.
    void Update() override;
    // Draws with alpha test and specular on.
    BOOL Draw(int pass) override;
    // The bodies of the bar's Rebirth-patched functions (ui_hud_build.cpp; WideScreen reproduced behind its flag, W3: the bar
    // at the screen's left edge).
    // Constructor: the window, the labels, the rectangle (80, 520, 240, 600),
    // four 40-wide regions with empty tips (WideScreen: everything from 80 - shift, the regions
    // still up to x 240, so more of them), no sprites, nothing selected, no unit, and the labels
    // "infoitem_15", "_20", "_10", "_5".
    CHudWeaponBar(CInterface3D* ui, CBattleScreen* owner);
    // Slot `index` (0 .. count_ - 1) highlighted: its sprite at (100 + 40 *
    // index, 540) depth 12, scale 0.65, colour 0xFF282828, its region from y 495; or not: at y 560,
    // scale 1.2 ^ count_ and depth 24 times that, colour 0xFF000000, the region from y 520
    // (WideScreen: with the left panels' offset).
    void Highlight(int index, int on);
    // The next slot's sprite: `image` copied into the bar's texture (42 x 128,
    // at column count_ / 2, row count_ % 2), the model `model` turned to face the screen, or a
    // plain rectangle when there is no such model; its texture offsets, plain white; counted and
    // shown not highlighted (Highlight).
    void AddWeapon(const char* model, const char* image);
    // Shows `unit`'s weapons: sprites dropped, the four
    // regions' tips cleared; per weapon a sprite (Rebirth-patched) and the tip of its
    // region (id 0x26 + slot): the name, damage range and type, the prototype's +0x6C and the
    // weapon's +0x58 and, for a weapon with a spell, the spell's name; then the unit's +0x270
    // highlighted.
    void SetUnit(CUnit* unit);

    // The labels, then
    // the window's.
    ~CHudWeaponBar() override;

    CBattleScreen* owner_;           // +0x148
    int count_;                      // +0x14C slots in use
    int selected_;                   // +0x150
    CUnit* unit_;                    // +0x154
    mfc::CStringArray labels_;       // +0x158 the tooltip's labels
};
static_assert(offsetof(CHudWeaponBar, unit_) == 0x154);
static_assert(offsetof(CHudWeaponBar, labels_) == 0x158);
static_assert(sizeof(CHudWeaponBar) == 0x16C);   // CBattleScreen's operator new

// Quick items (2 rows of four 50x50 slots
// at (520..720, 500..600).
class CHudQuickItems : public CUiWindow {
public:
    void OnMouseDown(int buttons, int x, int y);
    void OnDoubleClick(int buttons, int x, int y);
    void OnMouseMove(int buttons, int x, int y);
    // Button release: a unit's quick item (bottom row) is picked
    // or, with key +0x7F, used at once; a player's item (top row) starts mode 6, or
    // cancels when it is the highlighted one.
    void OnMouseUp(int buttons, int x, int y);
    // The input side: the left button and the mouse moves.
    int LeftDown(int b, int x, int y) override { return OnMouseDown(b, x, y), 0; }      // 2
    int LeftUp(int b, int x, int y) override { return OnMouseUp(b, x, y), 0; }          // 3
    int MouseMove(int b, int x, int y) override { return OnMouseMove(b, x, y), 0; }     // 4
    int LeftDouble(int b, int x, int y) override { return OnDoubleClick(b, x, y), 0; }  // 7
    BOOL Draw(int pass) override;
    // Every frame. Fills the tooltip of each quick item slot that shows one.
    void Update() override;
    // The tooltip text of quick item `index` into `text` (a
    // CString): the item's name and, for a wand (category 5) or a scroll (8) with a spell, its
    // numbers after labels_.
    void ItemTip(unsigned index, mfc::CString* text);
    // Picks the unit's quick item `slot` (a click on it or its
    // key): without a unit nothing; without an item there the pending action is dropped (no
    // sound); otherwise the click sound, then a click on the highlighted slot drops the pending
    // action, and a wand (category 5) or scroll (8) becomes the pending action on `slot`: 3 when
    // its spell has flag 0x10000000 (on a unit), 4 (at a point), 0 without a spell. Other
    // categories change nothing.
    void Pick(int slot);
    // The unit's quick item `slot` used at once (a release with
    // key flag +0x7F, or the key handler's Ctrl / Alt): as ClickAlternate with pending action 3
    // and quick 1, for a wand (category 5) or scroll (8) with a spell; nothing without a unit or
    // an item there.
    void UseNow(int slot);
    // Highlights the sprite shown in region `slot` when
    // it shows one (bottom row, slots 0-3: scale 0.65 / 0.55; top row 1.0 / 0.85; colour
    // 0xFF404040 / 0xFF000000; with `on` also selected_ = slot).
    void Highlight(int slot, int on);
    // The same through the game's code (so the verify wrapper sees the call).
    void HighlightAt(int slot, int on);
    // Item of the region at `index` (-1: empty). The region lookup is not checked,
    // so index -1 reads the dword before the array (+0x168).
    int RegionItem(int index) const {
        const int* items = regionItem_;   // declared below
        return items[index];
    }

    // The bodies of the panel's Rebirth-patched functions (ui_hud_build.cpp; WideScreen reproduced behind its flag: the
    // panel at the screen's right edge). The constructor: the window, the
    // entries array, the labels, the rectangle (520, 500, 720, 600), four 50 x 50 regions along the
    // bottom row from the right (x 670 .. 520) and four along the top row, with empty tips and no
    // items, nothing selected, no unit, and the labels "infoitem_7" .. "_13", "_21", "_22", "_26"
    // .. "_28".
    CHudQuickItems(CInterface3D* ui, CBattleScreen* owner);
    // The next sprite (at most eight): `image` copied into the panel's texture
    // (64 x 64 at (128 + 64 * (n / 4), 64 * (n % 4))), the model `model` turned over (pi about x)
    // or a plain rectangle without one; its texture offsets, plain white; counted.
    void AddItem(const char* model, const char* image);
    // Shows `unit`'s quick items: the sprites, tooltips and
    // entries are dropped; for each of the unit's four quick items a sprite (Rebirth-patched) in the bottom row, an entry and its tooltip; then, when the unit
    // is the local player's, up to four of the player's 0x3009 items in the top row.
    void SetUnit(CUnit* unit);

    // An item shown (12 bytes, MFC CArray at +0x158).
    struct Entry {
        bool own;                    // +0 one of the unit's quick items (else the player's)
        std::uint8_t unused_1[3];
        int id;                      // +4 the item's +0xC
        CItem* item;                 // +8
    };

    // The labels, the
    // entries array (its vtable written, the storage deleted), then the window's.
    ~CHudQuickItems() override;

    CBattleScreen* owner_;           // +0x148
    int shownCount_;                 // +0x14C sprites added (the shown entries)
    int selected_;                   // +0x150
    CUnit* unit_;                    // +0x154
    mfc::CArray<Entry> entries_;     // +0x158 (QuickEntryArrayClass)
    int regionItem_[8];              // +0x16C
    mfc::CStringArray labels_;       // +0x18C the tooltip's labels
};
static_assert(offsetof(CHudQuickItems, entries_) == 0x158);
static_assert(offsetof(CHudQuickItems, labels_) == 0x18C);
static_assert(sizeof(CHudQuickItems) == 0x1A0);   // CBattleScreen's operator new
static_assert(offsetof(CHudQuickItems, regionItem_) == 0x16C);
static_assert(offsetof(CHudQuickItems, selected_) == 0x150);

// Spell column on the right (760..800 x 190..510).
class CHudSpellColumn : public CUiWindow {
public:
    void OnMouseDown(int buttons, int x, int y);
    void OnDoubleClick(int buttons, int x, int y);
    void OnMouseUp(int buttons, int x, int y);
    void OnMouseMove(int buttons, int x, int y);
    // The input side: the left button and the mouse moves.
    int LeftDown(int b, int x, int y) override { return OnMouseDown(b, x, y), 0; }      // 2
    int LeftUp(int b, int x, int y) override { return OnMouseUp(b, x, y), 0; }          // 3
    int MouseMove(int b, int x, int y) override { return OnMouseMove(b, x, y), 0; }     // 4
    int LeftDouble(int b, int x, int y) override { return OnDoubleClick(b, x, y), 0; }  // 7
    BOOL Draw(int pass) override;
    // A click on spell `index` (its button or key): a click
    // on the highlighted one plays the click sound and drops the pending action; otherwise the
    // previous button loses its highlight, `index` gets it, the click sound, and when the unit's
    // spell there has its data (+0x44) the spell becomes the pending action on `index`: 1 with
    // flag 0x10000000 (on a unit), else 2 (at a point). An index out of range does nothing.
    void Click(int index);
    // Click with key flag +0x7F (or the key handler's Ctrl /
    // Alt): the spell is cast at once. Without a unit or with `index` out of range nothing;
    // otherwise the pending action becomes 1 on `index`, and when the spell there has its data
    // (+0x44) it is used (UseOnUnit / UseAtPoint, quick 0) on the unit itself, or for an attack
    // spell (CSpell::IsOffensive) on the nearest unit the unit sees (none: the "nomagic" sound,
    // the pending action dropped, done). Then the click sound and the pending action dropped.
    void ClickAlternate(int index);
    // Button `index` highlighted (scale 1.15, colour
    // 0xFF404040) or not (1.0, 0xFF000000); selected_ = index either way.
    void Highlight(int index, int on);
    void HighlightAt(int index, int on);
    // Every button unhighlighted, then selected_ = -1.
    void ClearHighlights();
    // The bodies of the column's Rebirth-patched functions (ui_hud_build.cpp; WideScreen reproduced behind its flag: the
    // column at the screen's right edge). The constructor: the window, the labels,
    // the rectangle (760, 190, 800, 510), no sprites or regions, nothing selected, no unit, and
    // the labels "infoitem_7", "_18", "_8" .. "_13", "_21", "_22", "_26" .. "_28".
    CHudSpellColumn(CInterface3D* ui, CBattleScreen* owner);
    // The next button: the regions and sprites so far move up one 40-high step,
    // a region and the sprite of `model` (not checked) are added at the bottom (y 470 .. 510) with
    // `image` copied into the column's texture (64 x 64 at (128 + 64 * (n / 4), 64 * (n % 4)));
    // plain white, scale 1, colour 0xFF000000; counted, selected_ = its index.
    void AddSpell(const char* model, const char* image);
    // Shows `unit`'s spells: sprites and regions dropped;
    // for each of the unit's eight spell slots that holds one, a sprite (Rebirth-patched)
    // and the tooltip of region `index` (tip id 0x1E + index): the spell's name and, when it has
    // its data (+0x44), seven lines of numbers each after a label of labels_ (some numbers
    // replaced by labels when absent: an area for flag 0x10000000, a time under 2 ticks).
    void SetUnit(CUnit* unit);

    // The labels, then
    // the window's.
    ~CHudSpellColumn() override;

    CBattleScreen* owner_;           // +0x148
    int count_;                      // +0x14C sprites added
    int selected_;                   // +0x150
    CUnit* unit_;                    // +0x154
    mfc::CStringArray labels_;       // +0x158 the tooltip's labels
};
static_assert(offsetof(CHudSpellColumn, count_) == 0x14C);
static_assert(offsetof(CHudSpellColumn, selected_) == 0x150);
static_assert(offsetof(CHudSpellColumn, labels_) == 0x158);
static_assert(sizeof(CHudSpellColumn) == 0x16C);   // CBattleScreen's operator new

// Action column on the left (six 40x40
// buttons at 0..40 x 270..510).
class CHudActionColumn : public CUiWindow {
public:
    void OnMouseDown(int buttons, int x, int y);
    void OnMouseUp(int buttons, int x, int y);
    void OnMouseMove(int buttons, int x, int y);
    // The input side: the left button and the mouse moves.
    int LeftDown(int b, int x, int y) override { return OnMouseDown(b, x, y), 0; }      // 2
    int LeftUp(int b, int x, int y) override { return OnMouseUp(b, x, y), 0; }          // 3
    int MouseMove(int b, int x, int y) override { return OnMouseMove(b, x, y), 0; }     // 4
    BOOL Draw(int pass) override;
    // Selects action button `index` (a click on it or its
    // key): the highlighted one again plays the click sound and drops the pending action;
    // another moves the highlight to it, plays the click sound and makes its action the pending
    // one on `index`: 8 (an order on an object) for action 0 (science), else 7 (follow), the
    // action in +0x184. An index out of range does nothing.
    void Select(int index);
    // As CHudSpellColumn::Highlight.
    void Highlight(int index, int on);
    void HighlightAt(int index, int on);
    // As CHudSpellColumn::ClearHighlights.
    void ClearHighlights();
    // The bodies of the column's Rebirth-patched functions (ui_hud_build.cpp; WideScreen reproduced behind its flag: the
    // column at the screen's left edge). The constructor: the window, the actions
    // array, the labels, the rectangle (0, 250, 40, 510), six 40 x 40 regions from the bottom
    // (y 470) up with empty tips, no sprites or actions, nothing selected, no unit, and the label
    // "infoitem_16".
    CHudActionColumn(CInterface3D* ui, CBattleScreen* owner);
    // The next button, above the ones so far: the sprite of `model` (not
    // checked) at (20, 490 - 40 * count_) with `image` copied into the column's texture (64 x 64
    // at (64 * (n / 4), 64 * (n % 4))); plain white, scale 1, colour 0xFF000000; `action` appended;
    // counted, selected_ = its index.
    void AddAction(const char* model, const char* image, int action);
    // Shows `unit`'s actions: sprites and the action array
    // dropped, the six regions' tips cleared; with `extra` (its low byte) first a "skill0011"
    // button (tip 0x290E); with a unit the science button ("skill0009", action 0) whose tip is
    // the "perk Science" text, the unit's science level (+0x408) and the "tip 10511" text.
    void SetUnit(CUnit* unit, int extra);

    // The labels, the
    // actions array (its vtable written, the storage deleted), then the window's.
    ~CHudActionColumn() override;

    CBattleScreen* owner_;           // +0x148
    int count_;                      // +0x14C buttons added
    int selected_;                   // +0x150
    CUnit* unit_;                    // +0x154
    UiDwordArray actions_;           // +0x158 the buttons' actions (0 science, -1 follow)
    mfc::CStringArray labels_;       // +0x16C the tooltip's labels
};
static_assert(offsetof(CHudActionColumn, count_) == 0x14C);
static_assert(offsetof(CHudActionColumn, selected_) == 0x150);
static_assert(offsetof(CHudActionColumn, actions_) == 0x158);
static_assert(offsetof(CHudActionColumn, labels_) == 0x16C);
static_assert(sizeof(CHudActionColumn) == 0x180);   // CBattleScreen's operator new

// A portrait's state (0x78 bytes, array at CHudPortraits +0x158): the last hit points seen,
// the face (rest orientation, current orientation and a random wobble), the face picture and
// model, and the slot's network player.
struct HudPortraitSlot {
    int lastHitPoints_;              // +0x00 lowest hit points seen (a drop flashes the portrait)
    float flash_;                    // +0x04 1 after a hit, fading out
    Quaternion restA_;               // +0x08 the face's rest orientation is restA_ * restB_
    Quaternion current_;             // +0x18 the face's orientation, eased towards the rest one
    Quaternion restB_;               // +0x28
    int wobbleKind_;                 // +0x38 0 none, 1 nod (about x), 2 shake (y), 3 turn to target
    float wobbleTime_;               // +0x3C starts negative (a delay)
    mfc::CString faceName_;          // +0x40
    int faceState_;                  // +0x44 picture shown (0 normal, 2 hit, 3 low mana, ...)
    int faceMode_;                   // +0x48 1 / 2: a timed picture, 0 none
    float faceTimer_;                // +0x4C
    mfc::CString modelPrefix_;       // +0x50 the face model is prefix + build + suffix,
    mfc::CString modelSuffix_;       // +0x54 build "th", "me" or "fa"
    float build_;                    // +0x58 the unit's build the model was chosen for
    Quaternion wobbleTarget_;        // +0x5C
    float wobbleSpeed_;              // +0x6C
    bool hasPlayer_;                 // +0x70 a multiplayer player without a unit here (no bars)
    std::uint8_t unused_71[3];
    int playerId_;                   // +0x74 that player's serial (PlayerMessage +0x30), else -1

    mfc::CString& FaceName() { return faceName_; }
    mfc::CString& ModelPrefix() { return modelPrefix_; }
    mfc::CString& ModelSuffix() { return modelSuffix_; }
};
static_assert(sizeof(HudPortraitSlot) == 0x78);
static_assert(offsetof(HudPortraitSlot, wobbleKind_) == 0x38);
static_assert(offsetof(HudPortraitSlot, faceName_) == 0x40 && offsetof(HudPortraitSlot, modelPrefix_) == 0x50 &&
              offsetof(HudPortraitSlot, modelSuffix_) == 0x54);
static_assert(offsetof(HudPortraitSlot, wobbleTarget_) == 0x5C);

// A figure prototype (the records of the map); only the model name the faces use.
struct FigurePrototypeRecord {
    std::uint8_t unused_000[0xE8];
    mfc::CString model;              // +0xE8 e.g. "unhuma", "unhufe"
};
static_assert(offsetof(FigurePrototypeRecord, model) == 0xE8);

// Party portraits (250..550 x 440..600).
class CHudPortraits : public CUiWindow {
public:
    void OnMouseDown(int buttons, int x, int y);
    void OnMouseUp(int buttons, int x, int y);     // selects the unit
    void OnMouseMove(int buttons, int x, int y);   // shows the unit / sets the cursor
    // The input side: the left button and the mouse moves.
    int LeftDown(int b, int x, int y) override { return OnMouseDown(b, x, y), 0; }      // 2
    int LeftUp(int b, int x, int y) override { return OnMouseUp(b, x, y), 0; }          // 3
    int MouseMove(int b, int x, int y) override { return OnMouseMove(b, x, y), 0; }     // 4
    // Every frame: health and mana bars, the hit flash, the face picture and
    // model, the face's wobble and the selection marker (ui_hud_portraits.cpp).
    void Update() override;
    // Copies face picture `state` of portrait `index`
    // (the slot's face name plus "a" 1, "c" 2, "b" 3; the plain name when there is no such image)
    // into quarter index % 4 of texture objects_[index / 4 + 1].
    void ShowFace(int index, int state);
    // Rebuilds the portraits: sprites, regions and
    // textures but the first dropped, the selection marker, then one portrait per party unit (face
    // names from its prototype and build, tip the unit's name) and, in a multiplayer client, one per
    // player without a live unit. `wide` goes to AddPortrait.
    void Rebuild(int wide);
    // Appends portrait count_: a slot, the face
    // picture `picture` into a quarter of the current face texture (a new 256x256 one every fourth
    // portrait), the face model `model` (a plain picture sprite without one), the two bars, the
    // frame, the flash and the region; 56 wide (multiplayer: 40, or 56 with the low byte of `wide`
    // for the first). The low byte of `hasPlayer` marks a player's portrait (half tint, no bars).
    void AddPortrait(const char* model, const char* picture, int wide, int hasPlayer);

    // Constructor: the window over 250..550 x
    // 440..600, no portraits, and the selection marker
    // sprite (a 20x20 rectangle at depth 10, scale 80, texels 0x22, 0x96 - 0x36, 0xAA).
    CHudPortraits(CInterface3D* ui, CBattleScreen* owner);
    // Each
    // The destructor: each
    // allocator, then the window's.
    ~CHudPortraits() override;

    CBattleScreen* owner_;           // +0x148
    int count_;                      // +0x14C
    std::uint8_t unused_150[0x158 - 0x150];
    // +0x158 one per portrait (slots are appended and ranges erased: with
    // recordvec::HudPortraitSlotElement).
    stlp::vector<HudPortraitSlot> slots_;
};
static_assert(offsetof(CHudPortraits, slots_) == 0x158);
static_assert(sizeof(CHudPortraits) == 0x164);

// A 2D mesh of the interface (the sprite objects and its siblings set up): a figure
// mesh (CFigureMesh, figure_mesh.h) whose arrays it fills with these views of their elements.
// Positions are kept per quad as four x then four y floats (VertexPacket); each vertex has a
// 6-byte record (MeshCorner: the position it uses and a frame of the texture); indices are 16 bits.
struct UiQuadBlock {
    float x[4];
    float y[4];
    float z[4];                      // 0 when the array grows (SetQuadCount)
    float w[4];                      // 1
};
// A texture coordinate pair of a sprite's vertex.
struct UiTexel {
    float u, v;
};
static_assert(sizeof(UiQuadBlock) == 0x40);
struct UiMeshVertex {
    std::int16_t position;           // +0 vertex number
    std::int16_t normal;             // +2 MeshCorner's second index (figure_mesh.h); the builders write 0
    std::int16_t frame;              // +4 texture frame (the minimap's marker kinds)
};
static_assert(sizeof(UiMeshVertex) == 6);
#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)
// The figure factory's class 0x43 ("CI3DFigure", 0x284 bytes, retired as a table):
// a class 0x42 mesh (CPlainFigureMesh) with the sprite's fields. A real virtual class.
class CUiMesh : public CPlainFigureMesh {
public:
    // The factory's class 0x43: CPlainFigureMesh's constructor; the sprite's
    // fields are set by Create (CreateSprite).
    CUiMesh();
    // CPlainFigureMesh's destructor (nothing of the sprite's to free).
    ~CUiMesh() override;
    // The virtuals: CPlainFigureMesh's but for Create, Destroy, GetClassName and GetClassId.
    int Create() override;
    void Destroy() override;
    const char* GetClassName() override;
    int GetClassId() override;

    // The array sizes: each is the aligned array's SetSize(count); the
    // quads then get z 0 and w 1 in every block, the vertices +2 zero in
    // every entry; the indices and texture coordinates (8 bytes each) nothing.
    void SetQuadCount(int count);
    void SetVertexCount(int count);
    void SetIndexCount(int count);
    void SetUvCount(int count);
    // The figure mesh's arrays with the elements as the interface writes them: the positions
    // (vertices_), the vertex records (corners_) and the texture coordinates (texCoords_); the
    // indices are indices_ itself.
    lib::AlignedArray<UiQuadBlock>& Quads() { return *reinterpret_cast<lib::AlignedArray<UiQuadBlock>*>(&vertices_); }
    lib::AlignedArray<UiMeshVertex>& Vertices() { return *reinterpret_cast<lib::AlignedArray<UiMeshVertex>*>(&corners_); }
    lib::AlignedArray<UiTexel>& Texels() { return *reinterpret_cast<lib::AlignedArray<UiTexel>*>(&texCoords_); }
    const lib::AlignedArray<UiQuadBlock>& Quads() const { return const_cast<CUiMesh*>(this)->Quads(); }
    const lib::AlignedArray<UiMeshVertex>& Vertices() const { return const_cast<CUiMesh*>(this)->Vertices(); }
    const lib::AlignedArray<UiTexel>& Texels() const { return const_cast<CUiMesh*>(this)->Texels(); }
    // (The arrays go to the renderer fields, +0x9C..+0xC8, by CPlainFigureMesh::RefreshBuffers.)
    // Where Place put the sprite: the layout point
    // ((x / z / 0.4816 + 1) * 400, y / z / 0.4816 * 400 + 300; each stored as float) and the depth
    // (z, copied).
    void GetPlacement(float* x, float* y, float* depth) const;
    // Through its address (PlaceBody is the rewrite; Rebirth patches it). Puts the sprite at layout point (x, y) at
    // `depth` in front of the interface camera (sets the local position +0x1C).
    void Place(float x, float y, float depth);
    // Rebirth-patched. Makes the sprite a textured rectangle (layout left, top, right,
    // bottom at `depth`; texels u0, v0 .. u1, v1): SetRectangleVanilla.
    void SetRectangle(float depth, float scale, float left, float top, float right, float bottom, int u0, int v0,
                      int u1, int v1);
    // Rebirth-patched (ui_mesh_shapes.cpp, hooked in the vanilla launch and,
    // reproducing WideScreen, in a Rebirth launch; rewritten callers go through SetRectangle). A rectangle of two
    // triangles: one packet of normals (0, 0, 1), four corners, the texels (u0, v0)-(u1, v1) as
    // texture coordinates (v upwards; both u one less when u0 > u1; half a texel inwards), the
    // quad (+-half width, +-half height) / scale about the origin, position_ the layout centre at
    // `depth` (as Place), the three scales 0.48157 * depth * scale / 400; then Commit. With
    // Rebirth's WideScreen every shape's layout x is moved by the panel offset (ui_widescreen.h).
    void SetRectangleVanilla(float depth, float scale, float left, float top, float right, float bottom, int u0, int v0,
                             int u1, int v1);
    // The other shapes' vanilla bodies (Rebirth-patched like the rectangle; ui_mesh_shapes.cpp).
    // A grid of columns x rows cells over the rectangle, each cell a quad
    // showing the whole picture (u0, v0)-(u1, v1): (columns + 1) * (rows + 1) points a cell apart
    // (in units of `scale`), four corners and six indices per cell; position_ the layout's top
    // left corner at `depth`.
    void SetGridVanilla(float depth, float scale, float left, float top, float right, float bottom, int u0, int v0, int u1,
                        int v1, int columns, int rows);
    // A frame `border` wide inside the rectangle: the outer and the inner
    // rectangle's corners (two packets) joined by four quads, each showing the whole picture;
    // position_ the layout centre at `depth`.
    void SetFrame(float depth, float scale, float left, float top, float right, float bottom, float border, int u0,
                         int v0, int u1, int v1);
    // A triangle: the first layout point is position_ (at `depth`), the
    // other two are points relative to it in units of `scale`; the texels are floats here.
    void SetTriangle(float depth, float scale, float x0, float y0, float x1, float y1, float x2, float y2, float u0,
                            float v0, float u1, float v1, float u2, float v2);
    // Place: position_ =
    // ((x * 0.0025 - 1) * 0.4816 * depth, (y * 0.0025 - 0.75) * 0.4816 * depth, depth), the
    // constants read from the game's data; `depth` copied as bits. With
    // Rebirth's WideScreen x is moved by the panel offset first (ui_widescreen.h); callers still
    // call it through its address (SpritePlaceOriginal) while addon's other code calls it too.
    void PlaceBody(float x, float y, float depth);
    // The three scale fields = `scale` (copied as bits;
    // EAX is left holding them, callers do not read it).
    void SetScale(float scale);
    // The material and the texture forgotten (not
    // released), then the figure's own Destroy, whose EAX is returned.
    void DestroySprite();
    // The y scale alone (scaleMatrix_[5]) = `bits` (copied as bits); the quick
    // items' bar calls it.
    void SetScaleYBits(std::uint32_t bits);
    // The x scale (scaleMatrix_[0], in ST0).
    float GetScaleX() const;
    // Draws the sprite in `renderer`'s pass when visible;
    // returns 1.
    int Draw(CSceneRenderer* renderer);
    // The figure's Create (ui_mesh_draw.cpp), then no
    // material or texture, the scale matrix the identity, tint 1, +0x230 zero, the identity
    // orientation (the identity), position (0, 0, 10), both colours 0xFF000000, visible. Returns 1.
    int CreateSprite();

    // CFigureMesh's fields as a sprite uses them: flags_ (0x100: drawn in the plain colour
    // flatColour_), localPosition_ (Place), localRotation_ (the HUD's face models turn with it),
    // worldMatrix_ (row-major: orientation and position, Draw writes it), textureOffsetU_ /
    // textureOffsetV_ (a frame of the sprite sheet: the HUD buttons' states, the bars' colours),
    // flatColour_ (ARGB: the global map's layers and its pick render), flatSpecular_ (ARGB: the HUD
    // buttons, 0xFF404040 highlighted, 0xFF000000 not), colorsA_ / colorsB_ (the shape builders
    // size them to four entries), normals_ (the shape builders keep one packet: (0, 0, 1) for the
    // four corners with w 1) and the arrays above.
    int visible_;                    // +0x220
    float tint_[3];                  // +0x224 colour factors (the HUD: 1 selected, 0.5 not)
    float tintSpare_[3];             // +0x230 0 from CreateSprite; Draw reads only tint_ (nothing reads it)
    // A row-major 4x4 matrix applied after the placement (Draw); its diagonal elements 0, 5 and 10
    // are the scale set by SetScale (the same value in all three).
    Matrix4 scaleMatrix_;            // +0x23C
    float& ScaleX() { return scaleMatrix_.m[0]; }
    float& ScaleY() { return scaleMatrix_.m[5]; }
    float& ScaleZ() { return scaleMatrix_.m[10]; }
    CEngineObject* material_;        // +0x27C the window's black material (CUiWindow +0xEC)
    CEngineObject* texture_;         // +0x280 one of the window's referenced objects (+0xF8)
};
static_assert(offsetof(CUiMesh, visible_) == 0x220 && offsetof(CUiMesh, tint_) == 0x224);
static_assert(offsetof(CUiMesh, scaleMatrix_) == 0x23C && offsetof(CUiMesh, material_) == 0x27C);
static_assert(offsetof(CUiMesh, texture_) == 0x280 && sizeof(CUiMesh) == 0x284);
static_assert(offsetof(CUiMesh, flatSpecular_) == 0xD8 && offsetof(CUiMesh, textureOffsetU_) == 0xCC);
static_assert(offsetof(CUiMesh, vertices_) == 0x1C0 && offsetof(CUiMesh, corners_) == 0x1E0);
static_assert(offsetof(CUiMesh, indices_) == 0x200 && offsetof(CUiMesh, texCoords_) == 0x210);

// A scene object as the interface sprite it is (a window's sprites are class 0x43 meshes; the
// engine object is the first base, so the pointer is the same).
inline CUiMesh* AsUiMesh(CEngineObject* object) { return static_cast<CUiMesh*>(object); }
inline CUiMesh* CUiWindow::SceneMesh(int index) const { return AsUiMesh(sceneObjects_.data[index]); }

// A growable array of 2D points with the interface arrays' 32-byte alignment.
struct UiPointF {
    float x;
    float y;
};
using UiPointArray = lib::AlignedArray<UiPointF>;
// The 8-byte aligned arrays' SetSize (aligned_arrays.cpp), through its address
// (ui_hud_map.cpp). `growBy` -1 keeps the growth step.
void __fastcall UiPointsSetSize(UiPointArray* points, void*, int size, int growBy);

// Map (615..800 x 0..165). Regions: 0 the
// sling button, 1 the round map, 2 the map area, 3 and 4 the two view buttons, 5 a button
// above them, 6 the whole panel.
class CHudMap : public CUiWindow {
public:
    // Built inline by the battle screen's Create: the window, the
    // owner, both point arrays empty.
    CHudMap(CInterface3D* ui, CBattleScreen* owner);
    void OnMouseDown(int buttons, int x, int y);   // the view buttons
    // Rebirth-patched, WideScreen reproduced behind its flag: the map's centre at
    // 700 + shift.
    // The button released over the map that took it. A held view button is let go (view_ 0, both
    // buttons dim; nothing else). Otherwise by region: 0 the sling (direction turned, the slide
    // starts), 1 / 2 a point of the map (the layout point's offset from (700, 80) over halfSide_,
    // on the round map also over the zoom, over scale_ about the centre; inside the world the
    // camera stops following and focuses there), 5 "camera north" (heading 0, northTime_ 0.5);
    // any region marks the input handled.
    void OnMouseUp(int buttons, int x, int y);
    void OnMouseMove(int buttons, int x, int y);
    // Over region 0 the wheel zooms the map by 2.5^(delta / 1200) (ui_hud_map.cpp).
    BOOL OnWheel(int buttons, int delta, int x, int y);
    // The input side: the left button and the mouse moves.
    int LeftDown(int b, int x, int y) override { return OnMouseDown(b, x, y), 0; }      // 2
    int LeftUp(int b, int x, int y) override { return OnMouseUp(b, x, y), 0; }          // 3
    int MouseMove(int b, int x, int y) override { return OnMouseMove(b, x, y), 0; }     // 4
    int Wheel(int f, int d, int x, int y) override { return OnWheel(f, d, x, y); }     // 12
    // Every frame, from the slide update. Rebuilds the marker mesh (scene object 2): the
    // party and other units, the marked objects, the zone exits and the other players
    // (ui_hud_map.cpp).
    void BuildMarkers();
    // Every frame, before BuildMarkers. Follows the camera (centre and facing, the
    // compass needle scene object 10), clips the view polygon to the world rectangle and builds
    // the visible-area mesh (scene object 1) and the world-bounds quad (scene object 0).
    void UpdateView();
    // The visible
    // area's and the view polygon's aligned storage freed, then the window's.
    ~CHudMap() override;
    // Rebirth-patched, WideScreen reproduced behind its flag.
    // Places the panel's
    // parts (scene objects 0-4 and 6-11), its text surface and the regions after the first at the
    // current slide offset.
    void Layout();
    void Update() override { UpdateVanilla(); }
    // Every frame; Rebirth-patched, WideScreen reproduced behind its flag.
    // The zoom grows (view 1, up to zoomMax_) or shrinks (view 2, down to 1) by 2.5 times a
    // second; UpdateView and BuildMarkers; a running slide advances (0.3 s for the whole way,
    // eased), the panel is laid out and the sling button (scene object 5) turned and placed for
    // the direction; the interface's "map shown" flag (+0x8B) follows the direction; the "camera
    // north" time (northTime_) counts down, scene object 11 tinted 1 while it runs and 0.5 after.
    void UpdateVanilla();
    // Rebirth-patched, WideScreen reproduced behind its flag: the panel at the
    // screen's right edge; the battle screen's build calls it through its address. Sets the new
    // panel up: the world's extent and the map's picture from the local player's zone record (32
    // by 32 and "battle00" without one), the zoom (`zoom` up to zoomMax_), the slide at its far
    // end when `slid`, the seven regions, the twelve sprites, the one-pixel surface at the centre
    // and the view polygon (the disc's sixteen corners); then UpdateView, BuildMarkers and Layout.
    void SetupVanilla(float zoom, std::uint8_t slid);

    CBattleScreen* owner_;           // +0x148
    float centreX_;                  // +0x14C world point at the map's centre (the camera's)
    float centreY_;                  // +0x150
    float facingX_;                  // +0x154 view direction (normalised by the camera follow)
    float facingY_;                  // +0x158
    float worldWidth_;               // +0x15C the zone's extent (clipping rectangle 0..width)
    float worldHeight_;              // +0x160
    float textureSize_;              // +0x164 world units per map texture (texture coordinates)
    float scale_;                    // +0x168 world units to map units
    float zoom_;                     // +0x16C 1 .. zoomMax_ (the view buttons change it)
    float zoomMax_;                  // +0x170
    float halfSide_;                 // +0x174 80: half the map's side in layout units (a click's offset over it)
    UiPointArray viewPolygon_;       // +0x178 the round map's outline in map units (the disc's corners)
    UiPointArray visibleArea_;       // +0x188 the outline in world units, clipped to the world
    int sliding_;                    // +0x198 set when the sling button (or its key) starts the slide
    int offset_;                     // +0x19C current slide offset in pixels (to the right)
    int travel_;                     // +0x1A0 full slide distance
    float slide_;                    // +0x1A4 0..1: how far the panel has slid (offset_ = travel_ eased)
    float slideDirection_;           // +0x1A8 +1 / -1
    int view_;                       // +0x1AC 1 or 2 while a view button is held (scene objects 6 and 7), else 0
    float northTime_;                // +0x1B0 0.5 when the "camera north" key turns the camera; the
                                     //        frame update counts it down
};
static_assert(offsetof(CHudMap, centreX_) == 0x14C);
static_assert(offsetof(CHudMap, scale_) == 0x168);
static_assert(offsetof(CHudMap, zoomMax_) == 0x170);
static_assert(offsetof(CHudMap, viewPolygon_) == 0x178 && offsetof(CHudMap, visibleArea_) == 0x188);
static_assert(offsetof(CHudMap, view_) == 0x1AC);
static_assert(offsetof(CHudMap, sliding_) == 0x198 && offsetof(CHudMap, slide_) == 0x1A4);
static_assert(offsetof(CHudMap, slideDirection_) == 0x1A8);
static_assert(offsetof(CHudMap, northTime_) == 0x1B0);
static_assert(sizeof(CHudMap) == 0x1B4);   // CBattleScreen's operator new

// Unit panel (0..185 x 0..225). Regions: 0
// the sling button, 1-4 the body parts, 5 the whole panel.
class CHudUnitPanel : public CUiWindow {
public:
    // Built inline by the battle screen's Create: the window, the
    // owner, the two strings and the two string arrays.
    CHudUnitPanel(CInterface3D* ui, CBattleScreen* owner);
    // 0x00647040 (ui_hud_unit_info.cpp).
    void DrawUnitInfo(CUnit* unitIn);
    // The health bar
    // and number of `unit` (scene objects 10 / 11), and its mana bar and number (12 / 13) when it
    // is one of the party.
    void ShowHealth(CUnit* unit);
    // The six body parts of `unit`: the name of each present
    // part and, for state 3, its durability bar (scene objects 14 + 2i / 15 + 2i).
    void ShowParts(CUnit* unit);
    void OnMouseDown(int buttons, int x, int y);
    void OnMouseUp(int buttons, int x, int y);
    void OnMouseMove(int buttons, int x, int y);
    BOOL OnWheel(int buttons, int delta, int x, int y);
    // The input side: the left button and the mouse moves.
    int LeftDown(int b, int x, int y) override { return OnMouseDown(b, x, y), 0; }      // 2
    int LeftUp(int b, int x, int y) override { return OnMouseUp(b, x, y), 0; }          // 3
    int MouseMove(int b, int x, int y) override { return OnMouseMove(b, x, y), 0; }     // 4
    int Wheel(int f, int d, int x, int y) override { return OnWheel(f, d, x, y); }     // 12
    // The battle screen's keys call it after setting
    // bodyPart_). The body part buttons (scene objects 3-6): tint 1 for the selected one, 0.5 for
    // the others.
    void ShowBodyPart();
    // The first
    // six effects on `unit`: each one's spell text and time left as two lines of the panel's
    // text, and its spell's picture (scene objects 26 + i, cut from the panel's icon texture,
    // where up to six pictures are kept: effectIcons_). Scene object 9 shows when there are more.
    void ShowEffects(CUnit* unit);
    // The part labels,
    // texts_, the two strings (+0x168 first), then the window's.
    ~CHudUnitPanel() override;
    // Rebirth-patched, WideScreen reproduced behind its flag.
    // Places the panel's
    // parts (scene objects 0, 1, 3-7, 9-13, the six body parts' pairs 14-25 at the places of the
    // tables, the six effect pictures 26-31), its text surface and the
    // regions after the first at the current slide offset.
    void Layout();
    void Update() override { UpdateVanilla(); }
    // Every frame; Rebirth-patched, WideScreen reproduced behind its flag.
    // A running slide advances (as the map's), the panel is laid out and the sling button (scene
    // object 2) turned and placed for the direction; a dead hover unit is forgotten; the
    // interface's "unit panel shown" flag (+0x89) follows the direction and its +0x8C is the
    // selected body part.
    void UpdateVanilla();
    // Rebirth-patched: WideScreen reproduced behind its flag, the panel at the
    // screen's left edge; the battle screen calls it through its address. `slid` (the
    // interface's +0x89) starts the panel at the far end of its slide: offset 180, slide 1,
    // direction +1 (else 0, 0, -1). Rectangle (0, 0)-(185, 225) and sliding_ 1 (WideScreen:
    // (-133, 0)-(52, 225) and 0, addon's constants); regions: 0 the sling button, 1-4 the body part buttons
    // (tips 0x2774..0x2778), 5 the whole panel; scene objects 0 / 1 plain backgrounds, 2 the sling
    // button, 3-6 the part buttons (the selected one tinted 1, the others 0.5), 7 the frame, 8 a
    // strip of cells and 9 the "more effects" mark (both hidden), 10-13 the health and mana bars,
    // 14-25 the body parts' bars (tables), 26-31 the effects' pictures (the
    // window's second texture); the text surface (20, 0)-(180, 220); Layout; no hover unit; the
    // texts "infounit_0".."infounit_17" and the labels "hl_skull", "hl_torso", "hl_leg", "hl_arm"
    // appended to the two string arrays; no effect pictures (-1).
    void SetupVanilla(int bodyPart, std::uint8_t slid);
    // Rebirth-patched, WideScreen reproduced behind its flag: the
    // panel offset until the base draw, the model's x and the depth rectangles moved.
    // The panel's draw: the background (scene object 0), then for the hover unit, else the selected
    // one, while the panel is not slid away: its model in the panel (not on the info page 2), the
    // device's depth buffer set to 0 around the panel's area for the draw and back to 1 after it,
    // its name, the page's bars and texts (ShowHealth / ShowParts / DrawUnitInfo / ShowEffects),
    // then the window's base draw. Returns 1.
    BOOL DrawVanilla(int pass);
    BOOL Draw(int pass) override { return DrawVanilla(pass); }

    CBattleScreen* owner_;           // +0x148
    CUnit* shownUnit_;               // +0x14C the first selected unit (RefreshSelection), or null
    CUnit* hoverUnit_;               // +0x150 the unit under the mouse (HoverUnit), or null
    // The model's view for the selected unit [0] and the hover unit [1], from the units'
    // prototypes (UnitPrototypeRecord +0x24 / +0x20; the battle screen copies them as dwords).
    float viewHeight_[2];            // +0x154 how far up the panel the model stands, in units of its size
    float viewSize_[2];              // +0x15C the model's size: it is drawn 1 / size (times its animation's ratio)
    mfc::CString strings_[2];        // +0x164 the selected and the hover unit's names
    int sliding_;                    // +0x16C set when the sling button starts the slide
    int offset_;                     // +0x170 current slide offset in pixels (to the left)
    int travel_;                     // +0x174 full slide distance
    float slide_;                    // +0x178 0..1: how far the panel has slid (offset_ = travel_ eased)
    float slideDirection_;           // +0x17C +1 / -1
    int bodyPart_;                   // +0x180 selected body part 0-3 (scene objects 3-6)
    int effectIcons_[6];             // +0x184 the spell pictures in the icon texture's six places
    mfc::CStringArray texts_;        // +0x19C "infounit_0".."infounit_17": the info lines' labels
    mfc::CStringArray partLabels_;   // +0x1B0 four: the shown names of "skull", "torso", "leg", "arm"
};
static_assert(offsetof(CHudUnitPanel, texts_) == 0x19C && offsetof(CHudUnitPanel, partLabels_) == 0x1B0);
static_assert(offsetof(CHudUnitPanel, owner_) == 0x148 && offsetof(CHudUnitPanel, shownUnit_) == 0x14C);
static_assert(offsetof(CHudUnitPanel, hoverUnit_) == 0x150 && offsetof(CHudUnitPanel, strings_) == 0x164);
static_assert(offsetof(CHudUnitPanel, slideDirection_) == 0x17C && offsetof(CHudUnitPanel, offset_) == 0x170);
static_assert(offsetof(CHudUnitPanel, bodyPart_) == 0x180);
static_assert(sizeof(CHudUnitPanel) == 0x1C4);   // CBattleScreen's operator new

// Game clock (720..800 x 520..600).
class CHudClock : public CUiWindow {
public:
    // Built inline by the battle screen's Create: the window and the owner.
    CHudClock(CInterface3D* ui, CBattleScreen* owner) : CUiWindow(ui), owner_(owner) {}
    void OnMouseDown(int buttons, int x, int y);
    // Rebirth's MPSpeedControl reproduced behind its flag.
    // The button released over the clock that took it: region 0 toggles the pause (with the
    // clock's sound); regions 1 and 2 set the normal / fast speed (not on a network client: the
    // pause lifted, fast_ 0 / 1, the world's update period 55 / 27 ms, the interface's fastGame_,
    // the sound); region 3 opens the objectives (OpenObjectives).
    void OnMouseUp(int buttons, int x, int y);
    void OnMouseMove(int buttons, int x, int y);
    // The input side: the left button and the mouse moves.
    int LeftDown(int b, int x, int y) override { return OnMouseDown(b, x, y), 0; }      // 2
    int LeftUp(int b, int x, int y) override { return OnMouseUp(b, x, y), 0; }          // 3
    int MouseMove(int b, int x, int y) override { return OnMouseMove(b, x, y), 0; }     // 4
    // Every frame: turns the face with the time of day, the hand and the
    // speed dial (scene objects 1-5), blinks while the game is paused (ui_hud_frame.cpp).
    void Update() override;
    // From the clock's click. The pause button stops
    // pulsing, the click sound, and a new objectives window of the current zone (one button, no
    // exits, no zone list) opened on top.
    void OpenObjectives();
    // Rebirth-patched: WideScreen reproduced behind its flag, the clock at the
    // screen's right edge; the battle screen calls it through its address. The clock's rectangle
    // (720, 520)-(800, 600); scene objects: 0 the face (a disc of radius 65 about the layout's
    // corner (800, 600)), 1 the hand (a triangle), 2-5 four "in25arrow" sprites (placed by
    // Update), 6 and 7 two more discs (radius 32 and 80); regions 0-2: the three sectors of pi / 6
    // between radius 35 and 80 about the corner (pause, normal, fast; the two speed tips not on a
    // network client), region 3 the quarter disc of radius 34 (the objectives); then `fast`, the
    // world's update period (27 ms fast, 55 normal), the interface's copy, no blink, no pulse.
    void SetupVanilla(int fast);

    CBattleScreen* owner_;           // +0x148 (Create sets it; the clock does not read it)
    float blink_;                    // +0x14C 0..1, the pause blink phase
    float dial_;                     // +0x150 speed dial angle (wraps at pi / 6)
    int fast_;                       // +0x154 fast game speed (copied to CInterface3D +0x94)
    bool highlight_;                 // +0x158 the pause button pulses (scene object 6)
};
static_assert(offsetof(CHudClock, blink_) == 0x14C);
static_assert(offsetof(CHudClock, highlight_) == 0x158);
static_assert(sizeof(CHudClock) == 0x15C);   // CBattleScreen's operator new

// Movement panel (0..80 x 520..600). Region 0
// toggles the units' +0x438 flag, regions 1-4 set their movement mode 0-3 (+0x268).
class CHudMovePanel : public CUiWindow {
public:
    // Built inline by the battle screen's Create: the window and the owner.
    CHudMovePanel(CInterface3D* ui, CBattleScreen* owner) : CUiWindow(ui), owner_(owner) {}
    void OnMouseDown(int buttons, int x, int y);
    void OnMouseUp(int buttons, int x, int y);
    void OnMouseMove(int buttons, int x, int y);
    // The input side: the left button and the mouse moves.
    int LeftDown(int b, int x, int y) override { return OnMouseDown(b, x, y), 0; }      // 2
    int LeftUp(int b, int x, int y) override { return OnMouseUp(b, x, y), 0; }          // 3
    int MouseMove(int b, int x, int y) override { return OnMouseMove(b, x, y), 0; }     // 4
    // Every frame. Shows the selected units' common mode and flag.
    void Update() override;
    // Region 0 (and its key): plays the click, sets +0x438 of the
    // selected party units to 0 when all of them have 1, else to 1 (through the local view), and
    // shows the button's state (scene object 5).
    void ToggleFlag();
    // WideScreen reproduced behind its flag: with the left panels' offset.
    // mode_ = `mode`; the marker (scene object 0) shows for a mode of 0 or more; the four mode
    // buttons (scene objects 1-4) are tinted 1 for the mode and 0.7 for the others, and the marker
    // is turned about -z by (2 mode + 1) pi / 16 and placed on the circle of radius 73 about
    // (0, 600) at depth 8.
    void ShowModeVanilla(int mode);
    // The panel's Create; Rebirth-patched: WideScreen reproduced behind its flag,
    // the panel at the screen's left edge. The rectangle (0, 520)-(80, 600); region 0 the
    // quarter disc of radius 34 about the layout's corner (0, 600), regions 1-4 the four sectors
    // of pi / 8 between radius 35 and 80 (tips 0x2904..0x2908); scene objects: 0 the marker (a
    // triangle), 1-4 the four sectors as triangles from the corner (tint 0.7), 5 the middle disc
    // (radius 32); no mode shown (ShowMode(-1)), no flag (-1), the disc's texture frame 0.48.
    void SetupVanilla();
    void Build() override { SetupVanilla(); }

    CBattleScreen* owner_;           // +0x148
    int mode_;                       // +0x14C the shown movement mode (-1, -2: none, mixed)
    int flag_;                       // +0x150 common +0x438 of the selected units (-1, -2: none, mixed)
};
static_assert(offsetof(CHudMovePanel, mode_) == 0x14C && offsetof(CHudMovePanel, flag_) == 0x150);
static_assert(sizeof(CHudMovePanel) == 0x154);   // CBattleScreen's operator new

// Top panel (180..620 x 0..100). Regions: 0 the sling
// button, 1 and 2 the two page buttons, 3 the whole panel.
class CHudTopPanel : public CUiWindow {
public:
    // Built inline by the battle screen's Create: the window, the owner, the two page texts.
    CHudTopPanel(CInterface3D* ui, CBattleScreen* owner) : CUiWindow(ui), owner_(owner) {}
    void OnMouseDown(int buttons, int x, int y);
    void OnMouseUp(int buttons, int x, int y);
    void OnMouseMove(int buttons, int x, int y);
    BOOL OnWheel(int buttons, int delta, int x, int y);
    // The input side: the left button and the mouse moves.
    int LeftDown(int b, int x, int y) override { return OnMouseDown(b, x, y), 0; }      // 2
    int LeftUp(int b, int x, int y) override { return OnMouseUp(b, x, y), 0; }          // 3
    int MouseMove(int b, int x, int y) override { return OnMouseMove(b, x, y), 0; }     // 4
    int Wheel(int f, int d, int x, int y) override { return OnWheel(f, d, x, y); }     // 12
    // Shows page `page_`: highlights its button (scene objects 3 /
    // 4) and puts its text into the battle screen's message log (page 0 scrolled to the end).
    void ShowPage();
    // Every frame: slides the panel in or out (ui_hud_frame.cpp).
    void Update() override;
    // Places the panel's parts, the message log and the regions at
    // the current slide offset.
    void Layout();
    // Builds page 1's text: the money ("format_money" with the
    // local party's +0x50, when above 0), then the local player's item list (+0xE0) merged by
    // kind (slot 14; counts added) into the world's copies, one item name per line;
    // shown in the message log when page 1 is up.
    void ShowItems();
    // The two page
    // texts (page 1 first), then the window's.
    ~CHudTopPanel() override;
    // Sets the panel up over 180..620 x 0..100, slid in
    // when the low byte of `shown` is set: the two page buttons' and the slide button's regions
    // with their tips (0x27D9 / 0x27DA with 0x32 / 0x33, 0x27D8) and the whole panel's, two plain
    // backgrounds, the "in25arrow" slide arrow, the two page buttons and the frame; then page
    // `page` is shown and the panel laid out (both through their addresses).
    void Build(int page, int shown);
    // Appends "<combat_picked_up or, with the low byte of
    // `crippled`, combat_crippled> <item name>" and a newline to page 0's text, and to the message log when
    // page 0 is up.
    void AddPickedUp(CItem* item, int crippled);
    // The same with the money ("format_money"), for money > 0.
    void AddMoney(int money);
    mfc::CString& PageText(int page) { return pageTexts_[page]; }

    CBattleScreen* owner_;           // +0x148
    int sliding_;                    // +0x14C
    int offset_;                     // +0x150 current slide offset in pixels
    int travel_;                     // +0x154 full slide distance
    float slide_;                    // +0x158 0 (out) .. 1 (in)
    float slideDirection_;           // +0x15C
    int page_;                       // +0x160
    mfc::CString pageTexts_[2];      // +0x164 the message log's text of pages 0 and 1
};
static_assert(offsetof(CHudTopPanel, owner_) == 0x148);
static_assert(offsetof(CHudTopPanel, slideDirection_) == 0x15C);
static_assert(offsetof(CHudTopPanel, page_) == 0x160 && offsetof(CHudTopPanel, pageTexts_) == 0x164);
static_assert(sizeof(CHudTopPanel) == 0x16C);   // CBattleScreen's operator new

// --- Screens with child windows: the save/load and options windows (ui_menus.cpp) -----------

// Base of the screens that hold child windows and extra input handlers:
// the Esc menu, the save/load and options windows, the battle screen. Its input
// handlers pass an event on to the extra input sides (+0x158) in order, then to the enabled children (for the
// mouse events only those whose rectangle holds the point, CUiWindow::ContainsPoint), each with its handled flag
// cleared first, until one sets it; that sets the container's own flag. Most screens keep some of these handlers
// and call them after their own (ui_container.cpp; docs/notes/ui.md, "Container input dispatch").
class CUiContainer : public CUiWindow {
public:
    // The window base, no children or extra input sides.
    explicit CUiContainer(CInterface3D* ui);
    // The extra input sides and the children deleted, both lists freed, then the
    // window base's. The deleting destructor and its copies (ui_window_delete.cpp).
    ~CUiContainer() override;

    void Show(int show) override { ShowChildren(show); }              // 19
    // (ui_container_draws.cpp). Nothing when the interface's +0x30 is clear; else the pass state
    // block, the window's own draw, then every enabled child's draw. Returns 1.
    BOOL Draw(int pass) override;
    // (ui_alpha_draws.cpp). Alpha reference 1 around the region tips and every enabled child's
    // draw, while the interface draws.
    void DrawChild(int pass) override;
    void Redraw() override { RedrawContainer(); }                     // 22
    void RedrawTips() override { RedrawChildTips(); }                 // 23
    void Update() override { UpdateBase(); }                          // 24
    // The screens' virtuals the interface calls (CInterface3D): the screen's Create and one taken off the screen
    // (pure here: _purecall), then game.exe's shared empty stubs.
    virtual void CreateScreen();                                       // 25
    virtual void CloseScreen();                                        // 26
    virtual void MessageResult(int) {}                                 // 27: the value a window above left pending when it closed
    virtual void ItemsPicked(void*, int, int) {}                       // 28: items picked up (items, money, crippled)
    virtual void UnitJoined(CUnit*) {}                                 // 29
    virtual void UnitLeft(CUnit*) {}                                   // 30
    virtual void GameOver(int) {}                                      // 31
    virtual void Speech(int, unsigned, char*) {}                       // 32: a unit speaks
    virtual void QuestVariable(const char*, float) {}                  // 33: a quest variable changed
    virtual void ShowBriefing(const char*) {}                          // 34: a "briefing <name>" string event
    virtual bool ReadyToStopPolling() { return false; }                // 35: whether the interface may stop asking (pollTop_)
    virtual void PlayersChanged(int) {}                                // 36
    virtual void ExchangeDone(CParty*, CParty*, CParty*) {}            // 37: an exchange ended
    virtual void PlayerList(int) {}                                    // 38: the player list for kicking (1) or banning (2)
    virtual void DebugFill() {}                                        // 39: the console's "give <player> assortment"
    virtual void SaveState() {}                                        // 40: the window's part of a save game
    virtual const char* HelpTopic() { return nullptr; }                // 41

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

    // (ui_container.cpp; it does not use the window). Time since the last frame in
    // seconds: the display object's millisecond reading * 0.001f, left unrounded in ST0.
    static double FrameTime();
    // Base update: every child's update.
    void UpdateBase();
    // Base redraw, only while the interface draws (+0x30): the
    // window's own, then every enabled child's.
    void RedrawContainer();
    // Base update of the tooltips likewise: the window's, then the enabled children's.
    void RedrawChildTips();
    // Adds a child window (null is ignored; ui_window.cpp).
    void AddChild(CUiWindow* child);
    // Appends an extra input side (null is ignored).
    void AddInput(UiInputSide* input);
    // The containers' show / hide (ui_container.cpp). The window's own surfaces
    // then every enabled child's with `show`.
    void ShowChildren(int show);

    UiPtrArray<CUiWindow> children_;          // +0x148
    UiInputSide** inputsBegin_;               // +0x158 extra input sides (STLport vector)
    UiInputSide** inputsEnd_;                 // +0x15C
    UiInputSide** inputsCapacity_;            // +0x160

private:
    // The input handlers' shared loop: `slot` of every side and child; `atPoint` limits the children to those
    // holding (b, c).
    void Dispatch(int slot, int a, int b, int c, bool atPoint);
};
static_assert(sizeof(CUiContainer) == 0x164);
// A window on the interface's stack as the screen it is (every window pushed there is a container).
inline CUiContainer* ScreenOf(CUiWindow* window) { return static_cast<CUiContainer*>(window); }

// --- Battle screen --------------------------------------------------------------------------
// The battle screen's "pers" texts (an STLport map<int, CString> at +0x1F0): a person's text by
// its text key id. find(key) is what the game inlines (ui_battle_mouse.cpp).
using PersonTextMap = stlp::map<int, mfc::CString>;
using PersonTextNode = PersonTextMap::Node;
static_assert(offsetof(PersonTextNode, second) == 0x14);

// The in-game screen (CreateScreen id 1, 0x230 bytes; created by
// the interface). Its Create builds the HUD panels as children
// (ui_battle_setup.cpp). Children, in order: weapon bar, quick items, spell column, action column,
// portraits, unit panel, map, clock, movement panel, message log, top panel. See
// docs/notes/ui.md, section 6.
class CBattleScreen : public CUiContainer {
public:
    // A click on `unit` (or, without one, at screen point x, y) while
    // party members are selected: by the pending action (+0x17C: 0 select / attack / move, 1 spell
    // on a unit, 2 and 4 spell on a point, 3 quick item, 5 item and 6 spell on an object, 7 follow,
    // 8 order on an object) or the interface's modifier keys; with nobody selected it selects the
    // clicked member. Rebirth's FixSpellSelfCasting #1 drops the Ctrl flag of a spell on a unit.
    void SelectUnit(CUnit* unit, int x, int y);
    // The mouse is over `unit` (or, without one, at
    // screen point x, y): a live unit with flag 0x400000 goes into the unit panel's hover block
    // (+0x150), otherwise the block is emptied; with party members selected the cursor shows what
    // a click would do (exit 15, the flags +0x18D.. 16+i, attack 1, cast 2 / can't 0x17, use 4,
    // no 0xD, order 0x18), by the pending action +0x17C as SelectUnit reads it.
    void HoverUnit(CUnit* unit, int x, int y);
    // Orders the selected party units to the point (x, y, z):
    // a point on a zone exit whose "z.<zone>" variable is not 1 is remembered (exitPending_,
    // pendingExit_ the exit); each selected member's portrait nods after a random delay and the
    // local view gets the order (a different one when the low byte of `mode` is set) with the
    // low byte of `ctrl`. SelectUnit calls it for a click on the ground (mode 0) or with a modifier
    // (mode 1).
    void OrderAt(float x, float y, float z, int mode, int ctrl);
    // A HUD button starts or drops an action (several buttons do this inline):
    // the pending action becomes `mode` on `slot` with
    // `item`, every highlight of the quick item panel (per slot), the spell column and
    // the action column is dropped (each panel's +0x150 -1), and `slot` is highlighted in the
    // panel of that mode (1, 2 the spell column; 3-6 the quick items; 7, 8 the actions; others
    // none). Mode 0 with slot and item -1 cancels (ui_hud_pick.cpp).
    void SetPendingAction(int mode, int slot, int item);

    // Reads the screen's saved state when a game is being loaded, else
    // remembers the local player's first hero; creates the panels and the "pers" texts table.
    void CreateScreen() override;
    // `pass` the scene renderer. Draws the screen: the menu object's frame call,
    // the viewport (with +0x1C1), a mouse move at the current position, clears the depth (and
    // stencil) buffer, then the panels. Rebirth's AddNewConsoleCommands applies "nogui"
    // first. Returns 1.
    BOOL Draw(int pass) override;
    // Every frame (ui_battle_update.cpp). When the one selected party unit is
    // dead (IsDead), empties the weapon, quick item, spell and action panels and shows the
    // unit in the unit panel; a multiplayer client rebuilds the party from the session's players
    // (sorted ids; the camera's player first); the children's update, the next queued message;
    // the "leave_zone" box when a saved zone exit is pending (+0x18C); with a selection box
    // (+0x1C1) the party units inside +0x1CC are marked (flag 0x100) and the others unmarked; the
    // quest sound asked for in +0x1FC.
    void Update() override;
    // Input side `this` is the window (ui_battle_update.cpp). The left
    // button released: nothing while a portrait click picks a player (+0x22C); without a press on
    // the view (+0x1C0) the container's handler; a press without a drag (+0x1C1), or on
    // a network client, is a click on the unit under the point (units with flag
    // 0x400000 only) through SelectUnit; a drag selects the party units inside the box
    // (+0x1CC): the world's selection cleared, the box marks and portraits'
    // markers reset, each unit inside controlled with its portrait marked and its face
    // turned (about -x by kHudQuarterTurn / 7), then the speech event with the first one's id; then RefreshSelection.
    void OnLButtonUp(int buttons, int x, int y);
    // Input side `this` is the window (ui_battle_mouse.cpp). The mouse
    // moved (also every frame from Draw, at the current position): while a portrait click picks a
    // player (+0x22C) the cursor shape 0xD and the portraits' own mouse move; otherwise shape 0,
    // then with a press on the view (+0x1C0), a positive BoxSelectThreshold and not a network
    // client: a move farther than the threshold from the press point (+0x1C4) in x or y starts
    // the selection box (+0x1C1), which empties the unit panel's hover unit and spans the press
    // point and (x, y) (+0x1CC); before that the unit under the point is hovered at once.
    // Without a press the container's handler first, and when no panel took the move
    // the unit under the point (any flags) goes to HoverUnit.
    void OnMouseMove(int buttons, int x, int y);
    // Input side `this` is the window (ui_battle_mouse.cpp). The left
    // button went down: while a portrait click picks a player (+0x22C) the portraits' own handler
    // (slot 2) gets it; otherwise, unless a press on the view is already on (+0x1C0), the
    // container's handler first, and when no panel took it a press on the view starts:
    // +0x1C0 set, the point kept (+0x1C4), no drag (+0x1C1).
    void OnLButtonDown(int buttons, int x, int y);
    // Input side `this` is the window (ui_battle_input.cpp). A double
    // click: as the button-down with the container's double-click handler.
    void OnLeftDouble(int buttons, int x, int y);
    // The screen loses the top (ui_battle_input.cpp): the order keys and
    // the press / box flags (+0x1C0, +0x1C1) cleared, then the container's handler.
    void OnDeactivate();
    // A key pressed (ui_battle_keys.cpp; Rebirth's MPSpeedControl reproduced behind its flag;
    // input side `this` is the window). While a portrait click picks a
    // player, Esc ends that and nothing else happens. On a network client Enter / Backspace go to
    // the chat line and the key is used. Otherwise the container's handler
    // first; a key nobody used and not repeating: Esc drops the pending action with
    // every panel's highlight, or without one opens the escape menu (built inline: 0x168 bytes);
    // any other key by its action (KeyActions, by `flags & 0x1FF`):
    // 1 pause (the clock's sound), 2 / 3 normal / fast
    // speed (not while paused or on a network client: the clock's +0x154, the world's update
    // period 55 / 27 ms, the interface's +0x94), 4 the objectives window of the player's zone,
    // 0xB the camera follows the first selected member (or nobody), 0xC the camera faces north
    // (the map's +0x1B0 0.5), 0x11..0x16 the order keys (orderKeys_), 0x17..0x1A the selected
    // members' movement mode 0..3 and 0x1B their flag (the movement panel), 0x1C / 0x1D the
    // action buttons "follow" / "science", 0x1E..0x25 spells 0..7 and 0x2A..0x2D quick items 0..3
    // (cast or used at once with Ctrl or Alt), 0x26..0x29 weapons 0..3, 0x2E..0x31 the unit
    // panel's body parts and 0x32 / 0x33 the top panel's pages (a panel slid away comes back, the
    // choice already shown slides it away), 0x34 the map slides, 0x35 / 0x36 quick save / load
    // ("saveq"), 0x37..0x39 party members 0..2 (ToggleMember; with Ctrl or Alt the camera follows
    // the member), 0x3A every member selected (not on a network client), 0x3B the message window
    // opens). Pause, the objectives, the movement keys, the message window and the
    // escape menu mark the key used.
    void OnKeyDown(int key, int repeat, int flags);
    // A key released (ui_battle_input.cpp; input side `this` is the window). Unless a
    // portrait click picks a player, the container's key-up handler, then the order
    // key of the key's action (0x11..0x16 of the key table, by `flags & 0x1FF`) is released.
    void OnKeyUp(int key, int repeat, int flags);
    // The wheel (ui_battle_input.cpp; input side `this` is the window). Unless a portrait
    // click picks a player, offered to the map, the unit panel, the message log and the top panel
    // in that order (each one's handled flag cleared first, the first that sets it ends it); when
    // none takes it, those four are disabled while the container's wheel
    // handler passes it on, then enabled again. Returns 1.
    BOOL OnWheel(int flags, int delta, int x, int y);
    // The name of `unit` into `out`: its GetName
    // when not empty; for a unit whose id is a text key, its "pers" text (+0x1F0) or empty;
    // otherwise the first line of the text "unit " + the prototype's name with spaces as '_' (or
    // empty without such a text). The unit panel, the hover, the selection and speech lines use it.
    void UnitName(CUnit* unit, mfc::CString* out);
    // A "q." player variable changed while the
    // screen is on top (the quest log passes it on). `name` split at '.': three parts
    // (q.<zone>.<quest>) a quest line, "combat_get_quest" / "_complete_" (value 2) / "_failed_"
    // (3), then the quest's title (text "quest <quest>"); four parts (a sub-objective) the same
    // with "_subobj" and the "#subobj <n>" line, skipped on a single player whose variable
    // "<a>.<b>.<c>" is 0. The line (any part count) goes to the top panel's text (+0x164) and,
    // unless +0x160, the message log; the clock pulses, +0x1FC becomes at least the value (1 for
    // a failure, 3 for a completed quest).
    void OnQuestVariable(const char* name, float value);
    // Called when a unit joins or leaves the
    // party, after items are picked up, by the member toggle, a box drag and a
    // network client's party change). The selection changed: with exactly one living party unit
    // selected the weapon bar, quick items, spell and action columns show it, otherwise they are
    // emptied (the action column keeps its extra button while anyone is selected); the unit panel
    // (+0x14C, +0x15C / +0x154 from the prototype's +0x20 / +0x24) shows the first
    // selected unit (null with nobody selected) when it changed; the pending action (+0x17C 0,
    // +0x180 / +0x184 -1) and every panel highlight are cleared.
    void RefreshSelection();
    // Party member `index` clicked (its portrait or
    // body, SelectUnit; the member keys): nothing on a network client or
    // for an index past the party. The selection is the members' flag 0x10 (as 0x10 or 0); with
    // the interface's +0x7D the member flips (to 1 / 0), else it alone is chosen (1). The world's
    // selection is cleared; each portrait's marker (+0x220) gets the chosen
    // value and its face's rest orientation the turn about -x by kHudQuarterTurn / 7 (or the identity);
    // each chosen member is controlled and the clicked one gets a speech event (id, 0, 0). In
    // single player the local player's unit records (+0x778) follow flag 0x10; then
    void ToggleMember(int index);
    // The selected members attack `target`: the order kind is the first of the
    // six modifier flags +0x18D.. that is set (6 for none); each selected member's portrait nods
    // after a random delay and, when the target is more than five levels above it, the member
    // speaks (speech event 10); the local view gets the order with the low byte
    // of `ctrl` (the interface's +0x7F).
    void AttackUnit(CUnit* target, int ctrl);
    // The same for a dead `target` (no kind, no levels).
    void OrderDeadUnit(CUnit* target, int ctrl);
    // The pending spell (+0x17C 1) or quick item (3) in
    // slot +0x180 used on `target` by each selected member: `quick` 0 a spell (the local view's
    // order (slot, member, target, low byte of `ctrl`), then CUnit::CanCastSpell), else a quick
    // item (the quick item panel shows the member, CUnit::CanUseQuickItem);
    // when the check answers 0 the member's portrait nods after a random delay. Then the pending
    // action is dropped (+0x17C 0, +0x180 / +0x184 -1) and the quick item, spell and action
    // panels' highlights cleared.
    void UseOnUnit(CUnit* target, char quick, int ctrl);
    // The same at the point (x, y, z).
    // For the member that is `unit` itself the point first moves
    // 0.1 behind it (against its rotated Y axis), for it and the members after it.
    void UseAtPoint(float x, float y, float z, CUnit* unit, char quick);
    // The unit with `id` speaks: a
    // phrase picked by the speech lookup (or, for events 0x2A / 0x2B, another with `extra`); when its
    // "say <name>" text exists, the unit's name line + ":\n" and the text (to a '#',
    // CR / LF trimmed, + "\n") are appended to the top panel's text and, unless its +0x160, the
    // message log. Then event 0x30 shows that member's timed face (+0x48 1, +0x4C 0), events
    // 0xB..0x15 make its portrait shake (kind 2) after a random delay.
    void OnUnitSpeech(int id, unsigned event, char* extra);

    // Virtuals of the screen.
    // The screen closes: the camera back to free placement (kFree), the world
    // update period 55 ms, the cursor no longer held, the client glue's stop calls, and
    // the party emptied (SetSize(0, -1)).
    void Close();
    // Polled every frame by the interface until it answers true. After a new
    // game (hasHero_): the camera follows the first party unit, the autosave "savea" when the
    // autosave option is on, hasHero_ cleared, then the help topic. Returns 1.
    bool PollShown();
    // Items were picked up: each item of `items` gives the top panel's
    // "picked up" line (CHudTopPanel's picked-up line with `crippled`), the money its line, the top panel shows
    // its page, then RefreshSelection.
    void OnItemsPicked(mfc::CTypedPtrList<CItem>* items, int money, int crippled);
    // `unit` left the party: removed from the party array (the rest
    // moved down), the portraits rebuilt and RefreshSelection; nothing when it is
    // not in the party. A network client instead empties the top panel's page-0 text and, while the
    // panel shows page 0, the message log's (set to "").
    void OnUnitLeft(CUnit* unit);
    // Writes the screen's part of a save game into the save stream:
    // the count and ids of the selected party units (flag 0x10), the map's zoom, the interface's
    // server flag (+0x60, a byte), the local player's +0x50, the top panel's page-0 text with its
    // zero, exitPending_ (a byte) and pendingExit_.
    void SaveState() override;
    // `unit` joined the party (a network client instead empties the top
    // panel's page-0 text as OnUnitLeft does). Its place: when the local player has a record for it
    // (LocalPlayerData +0x30 / +0x34, 0x784 bytes, unit id +0x1C) it goes after the party units
    // that come before that record in record order (the array grown by one, the tail moved up), and
    // the record's selection byte (+0x778) decides; otherwise it is appended, not selected (the
    // place used below is then the count of records matched). The portraits are rebuilt; a selected unit is controlled and its portrait marked (MarkPortrait). When
    // with 0); a selected unit is controlled and its portrait marked (MarkPortrait). When
    // its id is among heroes_ (the save's selection) it is taken out of heroes_, controlled and
    // marked. Then RefreshSelection.
    void OnUnitJoined(CUnit* unit);
    // The portrait at `index` shows the selection marker (sprite index * 5 + 4 visible) and its face's
    // rest orientation (restB_) turns about -x by kHudQuarterTurn / 7.
    void MarkPortrait(int index);
    // A message box's answer: 0x10001 (the "leave_zone" question) leaves
    // through the saved exit pendingExit_; 0x20001 (game over: load) opens the load
    // window (built inline: 0x194 bytes; flags written even when the
    // allocation failed); 0x20002, 0x90001 and 0x100001 close every window (+0x44), drop the sessions
    // and leave the game.
    void OnMessageResult(int result);
    // The game ends: the client glue's stop calls, then lost (`won` 0): the
    // game-over sound and the box "game_over" / "game_over_msg" (result 0x20000, OK and cancel);
    // won: "end_of_game" / "end_of_game_msg" (0xFFFF0000); opened on top.
    void OnGameOver(char won);
    // portraitMode_ = `mode` (a portrait click
    // picks a player while it is not 0).
    void SetPortraitMode(int mode);
    // Built inline by CInterface3D::CreateScreen (screen 1; ui_create_screen.cpp): the
    // container, the party and hero arrays, the "pers" texts, the client party's ids, the item key.
    explicit CBattleScreen(CInterface3D* ui);
    // (ui_window_delete.cpp): the two party
    // id vectors' storage, the "pers" texts (every node erased, then the header), the hero
    // ids and the party array (their vtables, storage freed), then the container's.
    ~CBattleScreen() override;
    // The virtuals of its own.
    void CloseScreen() override { Close(); }                                                         // 26
    void MessageResult(int result) override { OnMessageResult(result); }                             // 27
    void ItemsPicked(void* items, int money, int crippled) override {                                // 28
        OnItemsPicked(static_cast<mfc::CTypedPtrList<CItem>*>(items), money, crippled);
    }
    void UnitJoined(CUnit* unit) override { OnUnitJoined(unit); }                                    // 29
    void UnitLeft(CUnit* unit) override { OnUnitLeft(unit); }                                        // 30
    void GameOver(int won) override { OnGameOver(static_cast<char>(won)); }                          // 31
    void Speech(int id, unsigned event, char* extra) override { OnUnitSpeech(id, event, extra); }    // 32
    void QuestVariable(const char* name, float value) override { OnQuestVariable(name, value); }     // 33
    bool ReadyToStopPolling() override { return PollShown(); }                                       // 35
    void PlayerList(int mode) override { SetPortraitMode(mode); }                                    // 38
    void Deactivate() override { OnDeactivate(); }                                                   // input 1
    int LeftDown(int b, int x, int y) override { return OnLButtonDown(b, x, y), 0; }                 // input 2
    int LeftUp(int b, int x, int y) override { return OnLButtonUp(b, x, y), 0; }                     // input 3
    int MouseMove(int b, int x, int y) override { return OnMouseMove(b, x, y), 0; }                  // input 4
    int LeftDouble(int b, int x, int y) override { return OnLeftDouble(b, x, y), 0; }                // input 7
    int KeyDown(int k, int r, int f) override { return OnKeyDown(k, r, f), 0; }                      // input 9
    int KeyUp(int k, int r, int f) override { return OnKeyUp(k, r, f), 0; }                          // input 11
    int Wheel(int f, int d, int x, int y) override { return OnWheel(f, d, x, y); }                   // input 12

    // The client's party ids (STLport vector<int>s): the ids of players with a unit, the camera's
    // player's id, the ids of the others. Compared and copied as a whole.
    using IdVector = stlp::vector<int>;
    struct PartyIds {
        // Constructor: both id vectors empty (begin, end and
        // capacity null); the camera id is not written. Returns this.
        PartyIds* Construct();
        // Both id vectors hold the same ids in the same
        // order (the camera's id is not compared). AL.
        bool Equals(const PartyIds& other) const;

        IdVector withUnit;
        int camera;
        IdVector without;
    };

    int spare_;                      // +0x164 0 from Create; nothing in the screen's slots reads it
    // +0x168 the player's units shown as portraits (CArray<CUnit*>, UnitPtrArrayClass; SetSize
    // the SetSize of UnitPtrArrayClass)
    mfc::CArray<CUnit*> party_;
    // The pending action: what the next click on the view does (SelectUnit), its spell, quick item
    // or action button, and the player's item id (mode 6) or the action (7, 8).
    int pendingAction_;              // +0x17C 0
    int pendingSlot_;                // +0x180 -1
    int pendingItem_;                // +0x184 -1
    // +0x188 / +0x18C: a zone exit OrderAt found under the clicked point (the update asks about
    // leaving while exitPending_ is set; the box's answer leaves through it). Saved with the game
    // (version 0x62); Create sets -1 / 0 otherwise.
    int pendingExit_;                // +0x188
    std::uint8_t exitPending_;       // +0x18C
    // +0x18D the order modifier keys held: key actions 0x11..0x16 (the key table) set by the
    // key-down handler and cleared by the key-up one; AttackUnit's order kind
    // is the first one set. Create clears them (a dword and a word).
    std::uint8_t orderKeys_[6];
    std::uint8_t unused_193;
    CHudWeaponBar* weaponBar_;       // +0x194
    CHudQuickItems* quickItems_;     // +0x198
    CHudSpellColumn* spellColumn_;   // +0x19C
    CHudActionColumn* actionColumn_; // +0x1A0
    CHudPortraits* portraits_;       // +0x1A4
    CHudUnitPanel* unitPanel_;       // +0x1A8
    CHudMap* map_;                   // +0x1AC
    CHudClock* clock_;               // +0x1B0
    CHudMovePanel* movePanel_;       // +0x1B4
    CUiScrollText* messageLog_;      // +0x1B8
    CHudTopPanel* topPanel_;         // +0x1BC
    // The message log (child window 9; defined after CUiScrollText).
    CUiScrollText* MessageLog() const;
    bool viewPressed_;               // +0x1C0 the left button went down on the view
    bool boxSelecting_;              // +0x1C1 a selection box is being dragged
    std::uint8_t unused_1C2[0x1C4 - 0x1C2];
    int pressX_;                     // +0x1C4 where the button went down on the view (screen)
    int pressY_;                     // +0x1C8
    RECT selectionBox_;              // +0x1CC the box
    UiDwordArray heroes_;            // +0x1DC ids of the heroes shown (saved with the game)
    PersonTextMap personTexts_;      // +0x1F0 the "pers" texts by person
    int questSound_;                 // +0x1FC 0; the quest sound to play (1 quest, 2 part done, 3 done)
    bool hasHero_;                   // +0x200 a hero id was added (new game)
    std::uint8_t unused_201[3];
    PartyIds clientParty_;           // +0x204 the client's party ids last built (camera -1 from Create)
    // +0x220 the key a player's item gives (the item's key virtual) when it starts mode 6 ({1, 0, 0} from the
    // constructor, LockKey::ConstructItemUseKey); SelectUnit and the hover hand it to the target's
    // AcceptsKey (map object slot 42).
    LockKey itemUseKey_;
    int portraitMode_;               // +0x22C 0: normal; 1, 2: the next portrait click picks a
                                     //        player for a server command (4, 5)
};
static_assert(offsetof(CBattleScreen, spare_) == 0x164 && offsetof(CBattleScreen, party_) == 0x168);
static_assert(offsetof(CBattleScreen, pendingAction_) == 0x17C && offsetof(CBattleScreen, pendingSlot_) == 0x180);
static_assert(offsetof(CBattleScreen, pendingItem_) == 0x184 && offsetof(CBattleScreen, pendingExit_) == 0x188);
static_assert(offsetof(CBattleScreen, exitPending_) == 0x18C && offsetof(CBattleScreen, orderKeys_) == 0x18D);
static_assert(offsetof(CBattleScreen, weaponBar_) == 0x194 && offsetof(CBattleScreen, quickItems_) == 0x198);
static_assert(offsetof(CBattleScreen, actionColumn_) == 0x1A0 && offsetof(CBattleScreen, portraits_) == 0x1A4);
static_assert(offsetof(CBattleScreen, topPanel_) == 0x1BC && offsetof(CBattleScreen, pressX_) == 0x1C4);
static_assert(offsetof(CBattleScreen, selectionBox_) == 0x1CC && offsetof(CBattleScreen, heroes_) == 0x1DC);
static_assert(offsetof(CBattleScreen, personTexts_) == 0x1F0 && offsetof(CBattleScreen, hasHero_) == 0x200);
static_assert(offsetof(CBattleScreen, clientParty_) == 0x204 && sizeof(CBattleScreen::PartyIds) == 0x1C);
static_assert(offsetof(CBattleScreen, itemUseKey_) == 0x220 && offsetof(CBattleScreen, portraitMode_) == 0x22C);
static_assert(sizeof(CBattleScreen) == 0x230);

// A scroll bar (the battle screen's message
// log derives from it). Regions: 0 the up arrow, 1 the down arrow, 2 the track.
class CUiScrollbar : public CUiWindow {
public:
    // The position; returns 1 (and clears the flag) when it moved since the last call.
    BOOL TakePosition(int* position);
    // A window of `ui` with the "Scrollbar" texture
    // (kind 3), four sprites (track, thumb, up, down) and three regions, laid out over `rect`
    // (SetRect, called directly) at `depth`, `maximum` positions, scrolling `speed` lines per
    // second; then SetPosition(0) from -1.
    CUiScrollbar(CInterface3D* ui, RECT rect, float depth, int maximum, float speed);
    // The window's destructor only (and the slider's; ui_control_delete.cpp): only the window's.
    ~CUiScrollbar() override;
    // Every frame. Scrolls while an arrow is held: whole steps of the
    // accumulated amount move the position (SetPosition).
    void Update() override;
    void OnMouseDown(int buttons, int x, int y);   // thumb drag or an arrow
    void OnMouseUp(int buttons, int x, int y);
    // Input side (ui_scrollbar.cpp). While the thumb is dragged
    // (state 1): the position under the mouse (layout coordinates less the grab offset, over the
    // track's height times the maximum, truncated by _ftol) through SetPosition. The event is used
    // either way.
    void OnMouseMove(int buttons, int x, int y);
    // Input side (ui_scrollbar.cpp). Over the track (region 2) the wheel
    // scrolls: the accumulated amount less delta (its low 16 bits) times speed_ over 120 (Update
    // applies it), the event used. Returns 1.
    int OnWheel(int flags, int delta, int x, int y);
    // SetPosition of the plain bar. `position` clamped to 0..maximum, moved_ set;
    // a new position moves the thumb sprite (1) and thumb_ along the track: the offset is the
    // track's height times the position over the maximum, truncated.
    virtual void SetPosition(int position);
    // SetRect of the plain bar (and the scrolling text's). rect_, the track
    // (the right 10 pixels less 26 at either end), the four sprites and the three regions (up arrow,
    // down arrow, the rest); then the position set again through SetPosition (one below first, so it
    // counts as a change).
    virtual void SetRect(const RECT* rect);
    // The input side: the thumb and arrows by the left button, the drag, the wheel.
    int LeftDown(int b, int x, int y) override { return OnMouseDown(b, x, y), 0; }       // 2
    int LeftUp(int b, int x, int y) override { return OnMouseUp(b, x, y), 0; }           // 3
    int MouseMove(int b, int x, int y) override { return OnMouseMove(b, x, y), 0; }      // 4
    int Wheel(int f, int d, int x, int y) override { return OnWheel(f, d, x, y); }       // 12
    // The maximum (negative: 0), moved_ set, the four
    // sprites shown only for a maximum above 0; then the position set again through SetPosition (one
    // below first, so it counts as a change).
    void SetMaximum(int maximum);
    // The maximum position.
    int Maximum() const;
    int maxPosition_;                // +0x148
    float speed_;                    // +0x14C lines per second while an arrow is held (/16)
    int position_;                   // +0x150
    // +0x154 the thumb's track (layout coordinates; the options slider: left + 26 ..
    // right - 26 over the bottom 10 pixels). Named from the slider's SetRect only.
    RECT track_;
    RECT thumb_;                     // +0x164 layout coordinates
    int grabX_;                      // +0x174 mouse offset from the thumb centre while dragging
    int grabY_;                      // +0x178
    // +0x17C the sprites' depth: the constructor's first float (the constructor stores its float
    // arguments at +0x17C and +0x14C, the int at +0x148); the slider's SetRect places its four
    // sprites at it.
    float depth_;
    bool moved_;                     // +0x180
    std::uint8_t unused_181[3];
    int state_;                      // +0x184 0 idle, 1 dragging the thumb, 2 up, 3 down
    float accumulated_;              // +0x188 scrolled amount not yet applied
};
static_assert(offsetof(CUiScrollbar, position_) == 0x150);
static_assert(offsetof(CUiScrollbar, track_) == 0x154);
static_assert(offsetof(CUiScrollbar, thumb_) == 0x164);
static_assert(offsetof(CUiScrollbar, depth_) == 0x17C);
static_assert(sizeof(CUiScrollbar) == 0x18C);

// The options window's horizontal slider (0x18C bytes): a scroll bar
// whose SetRect lays it out along the bottom of its rectangle. Sprites (scene objects): 0 the
// track, 1 the thumb, 2 the right arrow, 3 the left arrow, all turned a quarter turn about z;
// regions 0 the left arrow, 1 the right arrow, 2 the track. ui_options.cpp.
class CUiSlider : public CUiScrollbar {
public:
    // The scroll bar's, then the slider's tables.
    // (`depth` goes to depth_ +0x17C, `maximum` to +0x148, `speed` to speed_ +0x14C.)
    CUiSlider(CInterface3D* ui, RECT rect, float depth, int maximum, float speed)
        : CUiScrollbar(ui, rect, depth, maximum, speed) {}
    // The scroll bar's SetPosition along the
    // track's width: the thumb sprite at (track left + offset, track top + 5), the thumb 28 wide
    // and 10 high.
    void SetPosition(int position) override;
    // Input side (ui_scrollbar.cpp). The scroll bar's drag along x.
    void OnMouseMove(int buttons, int x, int y);
    int MouseMove(int b, int x, int y) override { return OnMouseMove(b, x, y), 0; }
    // Lays the slider out over `rect` (layout coordinates) and sets
    // the position again (through SetPosition, so it counts as moved).
    void SetRect(const RECT* rect) override;
};
static_assert(sizeof(CUiSlider) == 0x18C);

static_assert(offsetof(CUiScrollbar, state_) == 0x184);
static_assert(offsetof(CUiScrollbar, accumulated_) == 0x188);

namespace mfc { class CString; }

// The pieces of a scrolling text (CUiScrollText below): a format change (0x14 bytes, type 1) or a word (0x10
// bytes, type 2) over the base token (each with one virtual: the deleting destructor).
// Made by CUiScrollText::Tokenize (ui_scroll_text.cpp).
struct UiFormatToken;
struct UiWordToken;
struct UiTextToken {
    explicit UiTextToken(int type) : type_(type) {}
    // Nothing of its own (ui_scroll_text.cpp).
    virtual ~UiTextToken();
    // The game's heap (null without memory: then no constructor runs).
    static void* operator new(std::size_t size) noexcept;
    static void operator delete(void* block);
    // The token as the kind type_ says it is (1 format, 2 word).
    UiFormatToken* AsFormat();
    const UiFormatToken* AsFormat() const;
    UiWordToken* AsWord();

    int type_;                       // +0x04 1 format, 2 word
};
struct UiFormatToken : UiTextToken {
    // The format token in `font` (inlined by Tokenize): no line break, the constructor's colour.
    explicit UiFormatToken(int font);
    // Nothing of its own.
    ~UiFormatToken() override;

    bool newline_;                   // +0x08 a line break (a newline token, or a font change after
                                     //       the first token)
    std::uint8_t unused_09[3];
    int font_;                       // +0x0C interface font
    COLORREF colour_;                // +0x10
};
static_assert(sizeof(UiFormatToken) == 0x14);
struct UiWordToken : UiTextToken {
    // An empty word of width 0 (inlined by Tokenize).
    UiWordToken() : UiTextToken(2), width_(0) {}
    // The text released (a member), then the base's.
    ~UiWordToken() override;

    mfc::CString text_;              // +0x08
    int width_;                      // +0x0C its width in the token's font
};
static_assert(sizeof(UiWordToken) == 0x10 && offsetof(UiWordToken, text_) == 0x08);
inline UiFormatToken* UiTextToken::AsFormat() { return static_cast<UiFormatToken*>(this); }
inline const UiFormatToken* UiTextToken::AsFormat() const { return static_cast<const UiFormatToken*>(this); }
inline UiWordToken* UiTextToken::AsWord() { return static_cast<UiWordToken*>(this); }
// MFC CArray of token pointers (ui_scroll_text.cpp).
using UiTokenArray = mfc::CArray<UiTextToken*>;
// A laid out line of a scrolling text (CUiScrollText +0x1B0 holds pointers to them; only
// the two fields DrawLines reads, the record's size is not known).
struct UiTextLine {
    int height_;                     // +0x00 pixels
    int firstToken_;                 // +0x04 index in the token array
};

// A scrolling text: a scroll bar subclass showing tokenised text; the
// battle screen's message log and the objectives window's quest log.
class CUiScrollText : public CUiScrollbar {
public:
    // A scroll bar over `rect` (by value) at
    // `depth` (maximum 0, 20 lines per second), the two arrays, a surface over the rectangle
    // (format 5; null when the allocation fails), laid out as SetRect without the virtual call,
    // then `text` tokenised and laid out, position 0 (SetPosition, then the plain bar's SetPosition),
    // justified (align_ 2).
    CUiScrollText(CInterface3D* ui, const char* text, RECT rect, float depth);
    // (ui_scroll_text.cpp). The tokens
    // deleted, the lines freed, both arrays' storage freed, then the scroll bar's.
    ~CUiScrollText() override;
    // Appends `text`: scrolled to the end first, then kept
    // that far from the end (the position plus the growth of the maximum).
    void AddText(const char* text);
    // Replaces the text (Clear, the new tokens laid out) and
    // scrolls to the top.
    void SetText(const char* text);
    // The scroll bar's SetRect, then the
    // surface moved to the rectangle's corner and the text laid out over it (PlaceText).
    void SetRect(const RECT* rect) override;
    // Draws the lines again when the position
    // moved, then the window's base drawing; returns its result (1).
    BOOL Draw(int pass) override;
    // Every token deleted and the token array emptied (the lines
    // stay until the next layout).
    void Clear();
    // Breaks the tokens into lines (UiTextLine, 8 bytes each) no
    // wider than the text: a word adds its width and a third of the line's height (the height of
    // the font in effect at the line's start); a word reaching the width starts the next line
    // (unless it is the line's first), a newline format token ends the line and sets the next
    // lines' font. Then the scroll bar's maximum: the lines' heights less the text's height.
    void Layout();
    // Appends the tokens of `text`: words (with their
    // widths), and before each run of newline, BEL C<rgb> and BEL F<digit> tokens one format
    // token.
    void Tokenize(const mfc::CString* text);
    // (ui_scroll_text.cpp; `this` not read). The token of `text` at `pos`
    // (spaces and tabs skipped): a BEL F / BEL C format (3 / 5 characters), a newline, or up to the
    // next space, tab, BEL or newline
    // into `out`; returns the position after it, or -1 at the end (`out` untouched).
    int NextToken(const mfc::CString* text, int pos, mfc::CString* out);
    // Called by the scrolling text's redraw (ui_scroll_text.cpp). Draws the lines
    // into surface 0 from `scroll` pixels down: the first line reaching `scroll` and the ones
    // after it while they fit, word by word in their tokens' fonts and colours (initially font 1,
    // 0xA7D7E4, or the last format token before the line), words apart by a third of the font's
    // height, or justified over the text's width (align_ not 1, the line not the last, not ended
    // by a newline, and of two or more tokens).
    void DrawLines(int scroll);

    RECT textRect_;                  // +0x18C the text's rectangle (surface coordinates)
    UiTokenArray tokens_;            // +0x19C
    mfc::CArray<UiTextLine*> lines_; // +0x1B0 the laid out lines
    int align_;                      // +0x1C4 1: left aligned (else justified)

private:
    // The surface moved to the rectangle's corner, the text's rectangle (14 pixels narrower for
    // the bar) and the layout: the tail of SetRect, inlined in the constructor.
    void PlaceText(const RECT& rect);
    void DeleteTokens();
};
static_assert(offsetof(CUiScrollText, textRect_) == 0x18C);

// The battle screen's message log as its code reaches it: the tenth child window (messageLog_).
inline CUiScrollText* CBattleScreen::MessageLog() const { return static_cast<CUiScrollText*>(children_.data[9]); }
static_assert(offsetof(CUiScrollText, tokens_) == 0x19C);
static_assert(offsetof(CUiScrollText, lines_) == 0x1B0);
static_assert(offsetof(CUiScrollText, align_) == 0x1C4);
static_assert(sizeof(CUiScrollText) == 0x1C8);

// A spin control of the options window (0x174 bytes): a window
// stepping through its texts (the on / off settings). Was UiSpinValue, with the rows'
// OptionSetting read as a separate UiSpin.
class CUiSpin : public CUiWindow {
public:
    // The window over the layout rectangle `rect` (by
    // value; also kept at +0x164), a surface of it (format 5; null when the allocation fails), the
    // first text `text` (null: the exe's empty text), not changed, value 0 (SetValue).
    CUiSpin(CInterface3D* ui, RECT rect, const char* text);
    // (ui_control_delete.cpp): the
    // texts.
    ~CUiSpin() override;
    // Appends a text (null: the empty text).
    void AddText(const char* text);
    // The value, and whether it changed since the last
    // call (the flag is cleared). SetValue: `value` (0 when not below the text count; a negative
    // one is kept), its text drawn on the cleared surface (format 1, colour 0xA7D7E4), changed.
    bool GetValue(int* value);
    void SetValue(int value);
    // Input side (ui_spin.cpp). A click: the message box's OK
    // sound and the next value (SetValue: back to the first past the last text).
    void OnLButtonDown(int flags, int x, int y);
    int LeftDown(int b, int x, int y) override { return OnLButtonDown(b, x, y), 0; }   // 2
    int LeftDouble(int, int, int) override { return 0; }

    mfc::CStringArray texts_;        // +0x148
    int value_;                      // +0x15C
    bool changed_;                   // +0x160
    std::uint8_t unused_161[3];
    RECT layout_;                    // +0x164 the constructor's rectangle
};
static_assert(offsetof(CUiSpin, value_) == 0x15C);
static_assert(offsetof(CUiSpin, layout_) == 0x164);
static_assert(sizeof(CUiSpin) == 0x174);

// A saved game in the save/load list (0x44 bytes, STLport vector at +0x164). Copy construction,
// assignment and destructor are not defined here. The fields
// after the folder come from the save's info.sav (ui_saveload.cpp).
struct SaveEntry {
    // (ui_saveload.cpp). The title: the texts "save_quick",
    // "save_auto_enter" / "save_auto_exit" for the folders "saveq", "savea" / "saveb", otherwise
    // the zone's name.
    void MakeTitle(int unused);
    // (ui_saveload.cpp). The zone's name, text "zone " + zone_ (its first
    // line), else the text "save_camp"; and (from RefreshList) the allod's
    // name, text "allod " + map_, else "Unknown".
    mfc::CString ZoneTitle() const;
    mfc::CString AllodTitle() const;
    // (ui_saveload.cpp). The compiler's: operator= copies the strings +0x00, +0x04,
    // the dwords +0x08, +0x0C, the strings +0x10, +0x14, the arrays (CStringArray::Copy), the byte
    // +0x40; returns this. The copy constructor: the strings empty, the arrays
    // constructed, then the same; returns this. The destructor: the arrays (last first),
    // then the strings from the last.
    SaveEntry* Assign(const SaveEntry* from);
    SaveEntry* CopyConstruct(const SaveEntry* from);
    void Destruct();

    mfc::CString title_;             // +0x00 shown in the list
    mfc::CString folder_;            // +0x04 folder name under saves\ ("saveq" quick save,
                                     //       "savea"/"saveb" automatic saves, "save<n>")
    std::int32_t savedAt_;           // +0x08 time() when it was saved (info.sav version 2 and up)
    float gameTime_;                 // +0x0C the "gtime" variable
    mfc::CString map_;               // +0x10 the map of the zone
    mfc::CString zone_;              // +0x14 the zone
    mfc::CStringArray stringArrays_[2];  // +0x18, +0x2C copied with the entry
    bool broken_;                    // +0x40 no info.sav: titled "Error Save", no picture
    std::uint8_t unused_41[3];
};
static_assert(sizeof(SaveEntry) == 0x44);
static_assert(offsetof(SaveEntry, folder_) == 0x04 && offsetof(SaveEntry, map_) == 0x10 && offsetof(SaveEntry, zone_) == 0x14);
static_assert(offsetof(SaveEntry, broken_) == 0x40);

// ui_saveload.cpp. The quick and automatic saves (the save folder's name, `stepFrame` in DL):
// the quick and automatic saves. Nothing for a multiplayer client or when the disc lacks the
// space a save needs (0 free space: not checked). With `stepFrame` a frame step;
// the screen copied into an 800x600 surface, the busy cursor, the server flag kept in the
// interface, "notify_saving" shown, the game saved, then info.sav (the "gtime"
// variable, the zone's map, the zone, the time, an empty title) and shot.sav (the surface as
// 256x192 5:6:5 pixels); the surface freed and the cursor back.
void __fastcall QuickSave(const char* slot, char stepFrame);
// Unless a multiplayer client: when saves\<slot>\ exists,
// "notify_loading" is shown, the client glue's stop call is made and the game flow is asked to load it (flag 0).
void __fastcall QuickLoad(const char* slot);

// The save/load window (0x194 bytes, opened from the Esc menu).
// Regions: 0 OK, 1 cancel, 2 delete, 3.. the visible list rows.
class CSaveLoadWindow : public CUiContainer {
public:
    // Built inline by the battle and briefing screens' game-over load and the menus: the container,
    // no entries, an empty name, a save (`isSave`) or load window,
    // sliding in or not.
    CSaveLoadWindow(CInterface3D* ui, bool isSave, bool sliding)
        : CUiContainer(ui), isSave_(isSave), sliding_(sliding) {}
    // (ui_window_delete.cpp): the name,
    // each entry's strings and string arrays, the entries' storage, then the container's.
    ~CSaveLoadWindow() override;
    // The virtuals of its own.
    BOOL Draw(int pass) override;                      // slides in
    void Update() override;
    void CloseScreen() override { CloseDialog(); }                                          // 26
    void MessageResult(int result) override { OnDialogResult(result); }                    // 27
    int LeftDown(int b, int x, int y) override { return OnMouseDown(b, x, y), 0; }         // 2
    int LeftDouble(int b, int x, int y) override { return OnDoubleClick(b, x, y), 0; }     // 7
    int KeyDown(int k, int r, int f) override { return OnKeyDown(k, r, f), 0; }            // 9
    int Char(int k, int r, int f) override { return OnChar(k, r, f), 0; }                  // 10
    void OnMouseDown(int buttons, int x, int y);
    void OnDoubleClick(int buttons, int x, int y);
    void CloseDialog();
    void OnDialogResult(int result);

    // (ui_saveload.cpp).
    // OK: nothing without a selection; a save window plays "ok" and saves into the new
    // save (row 0) or asks "overwrite" (message box 0x20000); a load window loads (LoadSelected).
    void Confirm();
    // Delete: with a selection that is not a save window's new save, plays "delete" and
    // asks "delete" (message box 0x10000).
    void AskDelete();
    // Deletes every file of the selected save's folder and the folder, refills the list,
    // keeps the selection in it, takes its title, redraws the list and the picture.
    void DeleteSave();
    // A selected save that is not broken: "notify_loading" shown, the client glue's stop call, "ok"
    // played, the game flow asked to load its folder (flag 0).
    void LoadSelected();

    void RefreshListVanilla();

    // ui_saveload.cpp. The window's two picture textures (256x256, 5:6:5): objects_[1] shows the
    // selected save's shot.sav, dialog_ the game for a new save. The Call* forms go through the
    // original addresses (the verify build's wrappers run there).
    // Saves into the selected entry's folder (the game flow's save, then info.sav from
    // the new save's entry and shot.sav from dialog_'s pixels); a message box when the disc is full.
    void SaveGame();
    // The picture sprite shows the selected save's shot.sav (read into objects_[1]), or
    // dialog_ for the new save of a save window; hidden for none or a broken save.
    void ShowPicture();
    // Reads saves\save*\info.sav into the entry
    // list, sorts it, puts the new save's entry first in a save window (with dialog_ made from the
    // escape menu's 256x192 copy of the screen) and sets the scroll bar's range.
    void FillEntries();

    // The window's Create: builds the window (ui_layouts.cpp).
    void CreateScreen() override;

    int EntryCount() const { return static_cast<int>(entries_.end() - entries_.begin()); }
    // The selected save's title becomes the name being edited.
    void TakeSelectedTitle();

    // (ui_menu_keys.cpp).
    // Input side. Keys: Enter confirms, Esc cancels, Up / Down / PgUp / PgDn
    // move the selection; in a save window Backspace, Delete, Home, End, Left and Right edit the
    // save's name (the first edit of a save that is not a quick or automatic one takes over its
    // title, Backspace / Delete starting from an empty name).
    void OnKeyDown(int key, int repeat, int flags);
    // Input side. A typed character in a save window: the first one for an
    // ordinary save starts an empty name; without Ctrl, a name under 60 characters takes the
    // character at the caret when the text stays under 178 pixels (font 1); the list is redrawn.
    // Backspace, Tab, Enter and Esc only go to the base handler, which every character reaches.
    void OnChar(int key, int repeat, int flags);
    // Clamps the selection to the list, scrolls it into view (16 rows), takes its
    // title as the name, redraws the list and shows its picture.
    void ShowSelection();
    // Entry `index` is the quick save or an automatic save ("saveq", "savea",
    // "saveb"; no bound check).
    bool IsReservedSave(int index);

    // +0x164 the saves (recordvec::SaveEntryElement handles the
    // reallocating insert and the erase).
    stlp::vector<SaveEntry> entries_;
    int selected_;                   // +0x170
    int firstVisible_;               // +0x174 list scroll position
    bool isSave_;                    // +0x178 save (not load) window
    bool sliding_;                   // +0x179 sliding in; input is ignored meanwhile
    std::uint8_t unused_17A[2];
    float slide_;                    // +0x17C 0..1
    CUiScrollbar* scrollbar_;        // +0x180
    CEngineObject* dialog_;          // +0x184 the save window's picture of the game for the new save
                                     //        (a texture made by FillEntries; released by the screen's close.
                                     //        The name is from an earlier reading.)
    mfc::CString name_;              // +0x188 the save name being edited
    bool nameTyped_;                 // +0x18C
    std::uint8_t unused_18D[3];
    int cursor_;                     // +0x190 caret position in name_ (bytes, at a character's start: U4)
};
static_assert(offsetof(CSaveLoadWindow, selected_) == 0x170);
static_assert(offsetof(CSaveLoadWindow, slide_) == 0x17C);
static_assert(offsetof(CSaveLoadWindow, name_) == 0x188 && offsetof(CSaveLoadWindow, cursor_) == 0x190);
static_assert(sizeof(CSaveLoadWindow) == 0x194);

// A setting's definition (0x18 bytes; a table of 26).
struct OptionDefinition {
    int id_;                         // +0x00
    const char* name_;               // +0x04 "option_" + name is its text key (else the name is shown)
    int kind_;                       // +0x08 0 a slider, 1 an on / off spin control
    int maximum_;                    // +0x0C the slider's range
    int group_;                      // +0x10 the option group (0-10) it is shown in
    int row_;                        // +0x14 its row there
};
static_assert(sizeof(OptionDefinition) == 0x18);
constexpr int kOptionDefinitionCount = 26;

// A key action's definition (0x10 bytes; a table of 59).
struct ActionDefinition {
    int id_;                         // +0x00 the action (the key code in the key table)
    const char* name_;               // +0x04 "action_" + name is its text key
    int group_;                      // +0x08
    int row_;                        // +0x0C
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

// A setting of the options window (0x14 bytes, 26 of them at +0x378; the destructor frees
// the text). The rows of the shown group point at them.
struct OptionSetting {
    const OptionDefinition* definition_;   // +0x00
    int value_;                      // +0x04
    CUiScrollbar* bar_;              // +0x08 a CUiSlider, or null
    CUiSpin* spin_;                  // +0x0C or null
    mfc::CString text_;              // +0x10 the shown name
};
static_assert(sizeof(OptionSetting) == 0x14);

// An exit shown on the objectives window's zone picture (0x1C bytes, CArray at +0x188).
struct ObjectiveExit {
    bool flag_;                      // +0x00 a known distance (global map), or the zone variable is not 1
    std::uint8_t unused_01[3];
    int exit_;                       // +0x04 index in the zone's exit list
    float distance_;                 // +0x08
    float x_;                        // +0x0C centre of the exit rectangle (world units)
    float y_;                        // +0x10
    float mapX_;                     // +0x14 on the picture (layout)
    float mapY_;                     // +0x18
};
static_assert(sizeof(ObjectiveExit) == 0x1C);

// The objectives window (0x1F0 bytes; TAB in a
// zone opens it for the current zone). Left: the quests of the zone (rows), the log; right: the
// zone picture with its exits; the arrows switch between zones when it lists several.
class CObjectivesWindow : public CUiContainer {
public:
    // The container, the empty zone list, strings, exit
    // array, quest arrays and places (the rest is set by Build).
    explicit CObjectivesWindow(CInterface3D* ui);
    // (ui_window_delete.cpp): the objective
    // texts, the quest places' storage, the quests, the exits (storage freed), the
    // two zone names, the zone list, then the container's. The exit distances (+0x178) are not
    // freed here.
    ~CObjectivesWindow() override;
    // The virtuals of its own.
    void CloseScreen() override { RestoreAlphaRef(); }                                   // 26
    int LeftDown(int b, int x, int y) override { return OnLButtonDown(b, x, y), 0; }   // 2
    int KeyDown(int k, int r, int f) override { return OnKeyDown(k, r, f), 0; }        // 9
    // The device's alpha reference back to the saved one
    // (savedAlphaRef_); SetRenderState's result (not stored in the device object).
    long RestoreAlphaRef();
    // The window's Create (ui_objectives.cpp).
    void CreateScreen() override;
    // Shows zone `zone`: picture, exits, quests and description.
    void ShowZone(const char* zone);
    // When the selected quest changed, its description in the log and a
    // marker and region per place of the quest on the zone picture.
    void ShowQuest();
    // Draws the quest list: up to five rows from `firstRow_`, each with
    // its title ("quest " text, else the quest's name) and its state mark (scene objects 16-20:
    // state 2 one frame, 3 the other, else hidden); the selected row gets the cursor (object 2).
    void DrawQuestList();
    // Input side (ui_objectives_input.cpp). The base input's press, then by region: 0 OK ("tutorial
    // ok" sound; with two buttons the time to the nearest exit passes and the zone is
    // requested at that exit (RequestZone); else the pending value 0), 1 cancel (with two
    // buttons: the cancel sound and the pending value 0), 2 / 3 the previous / next listed zone
    // (wrapping; only when zones are listed and there are two or more), 4-8 a quest row
    // (+0x1C0 the first shown; selected, drawn, shown), from 9 an exit (with two buttons and its
    // flag: the "enter" sound and SelectExit, else the cancel sound).
    void OnLButtonDown(int buttons, int x, int y);
    // Input side (ui_objectives_input.cpp). The base input's key
    // handler, then: Enter as the OK region (without two buttons the pending value 0, else the
    // nearest exit's travel and zone request), Esc closes (the cancel sound with two buttons, else
    // the OK sound), PgUp / PgDn / Up / Down move the selected quest by 5 / 1 (RefreshQuests); those
    // mark the key used. A key not used that is bound to the objectives action (key table 4) closes a
    // window with one button.
    void OnKeyDown(int key, int repeat, int flags);
    // Called by ShowQuest. The places of quest `name` into
    // +0x1CC (emptied first): a multiplayer client takes them from the quest database's record
    // (its float pairs), otherwise from map.txt's "#quest <name>" entry (the second line after
    // it, its words as x y pairs).
    void LoadQuestPlaces(const char* name);
    // ui_objectives_input.cpp.
    // Exit `index` becomes the nearest (+0x19C): its marker (sprite 21 + index)
    // 1.2 times markerScale_ and the first frame, the blink restarted; the others markerScale_ and
    // the first frame when their flag is set, else u offset -0.125. Sprites 4 and 5 hidden.
    void SelectExit(int index);
    // With quests: the selected one clamped to the list, the shown rows moved to keep it
    // within five, the bar set there (slot 25, then TakePosition), the list drawn and the quest
    // shown.
    void RefreshQuests();
    // Every frame. The container's update; a moved bar shows its rows; the
    // blink time advances by the frame time and gives two alphas, (cos(2.5 t) x 0.3 + 0.5) x 255
    // and (cos(5 t) x 0.4 + 0.6) x 255 (_ftol): the quest place markers (sprites after the exits')
    // are red with the second, the nearest exit's marker white with the second, the other exits'
    // grey (C8C8C8) with the first; all drawn in their plain colour (flag 0x100).
    void Update() override;
    // (ui_objectives.cpp). What the window is opened for, before it
    // opens: the zone, the exits' distances (or null), the two buttons and the zone list.
    // The help topic, "quest_global_map" with two buttons, else
    // "quest_mission".
    void Setup(const char* zone, void* exitDistances, int twoButtons, int listZones);
    const char* HelpTopic() override;

    stlp::vector<mfc::CString> zones_;   // +0x164 the zones listed
    mfc::CString zoneName_;          // +0x170 the zone it was opened for
    int twoButtons_;                 // +0x174 OK on the left and a second button (else OK centred)
    stlp::map<int, float>* exitDistances_;   // +0x178 exit -> distance, or null
    int listZones_;                  // +0x17C list the zones of the zone's allod with quests
    int zoneIndex_;                  // +0x180 the zone shown
    mfc::CString shownZone_;         // +0x184
    mfc::CArray<ObjectiveExit> exits_;   // +0x188
    int nearestExit_;                // +0x19C
    int selectedQuest_;              // +0x1A0 row of the quest named by the current-quest global
    int shownQuest_;                 // +0x1A4 the row ShowQuest last showed (-1: none)
    mfc::CStringArray quests_;       // +0x1A8
    CUiScrollbar* bar_;              // +0x1BC
    int firstRow_;                   // +0x1C0 the first quest row shown (the bar's position)
    float markerScale_;              // +0x1C4
    float blinkTime_;                // +0x1C8 seconds since the exit was selected (the markers' blink)
    // +0x1CC the selected quest's places; ShowQuest gives each a sprite and a region
    // after the exits'.
    stlp::vector<UiPointF> questPlaces_;
    mfc::CStringArray objectiveTexts_;   // +0x1D8 obj_0..obj_2
    std::uint32_t savedAlphaRef_;    // +0x1EC
};
static_assert(offsetof(CObjectivesWindow, zones_) == 0x164 && offsetof(CObjectivesWindow, zoneName_) == 0x170);
static_assert(offsetof(CObjectivesWindow, shownZone_) == 0x184);
static_assert(offsetof(CObjectivesWindow, exits_) == 0x188 && offsetof(CObjectivesWindow, nearestExit_) == 0x19C);
static_assert(offsetof(CObjectivesWindow, quests_) == 0x1A8 && offsetof(CObjectivesWindow, questPlaces_) == 0x1CC);
static_assert(offsetof(CObjectivesWindow, objectiveTexts_) == 0x1D8 && offsetof(CObjectivesWindow, savedAlphaRef_) == 0x1EC);
static_assert(offsetof(CObjectivesWindow, bar_) == 0x1BC);
static_assert(sizeof(CObjectivesWindow) == 0x1F0);

// The game's table of key names (object_tables.h's UiKeyName, g_keyNames).
inline const UiKeyName* UiKeyNames() { return g_keyNames; }

// A key binding row of the options window (0x18 bytes, vector at +0x36C; rows_ point at them;
// the destructor is trivial).
struct OptionBinding {
    // Inlined in the options window's destructor body: the two strings, then the
    // vector's storage (its elements' destructor is empty).
    void Destroy() {
        keysText_.~CString();
        name_.~CString();
        keys_.FreeStorage();
    }

    const ActionDefinition* action_; // +0x00
    // +0x04 the keys bound to the action (BuildBindings keeps at most one).
    stlp::vector<const UiKeyName*> keys_;
    mfc::CString name_;              // +0x10 the action's shown name (passed to "key_already_mapped_msg")
    mfc::CString keysText_;          // +0x14 the keys' names, separated
};
static_assert(sizeof(OptionBinding) == 0x18);

// A row of the options window: a key binding or a setting (one of the window's own).
struct OptionRow {
    OptionBinding* binding_;
    OptionSetting* setting_;
};

// The options window (0x418 bytes). Regions: 0 OK, 1 cancel, 2-12
// the option groups (100..250 x 156..420), 13.. the rows of the shown group.
class COptionsWindow : public CUiContainer {
public:
    // Built inline by the Esc menu and the main menu: the container, no binding rows or settings,
    // the group titles' array, sliding in or not.
    COptionsWindow(CInterface3D* ui, bool sliding) : CUiContainer(ui), sliding_(sliding) {
        groupNames_.Construct();
    }
    // (ui_menus.cpp): the group titles, the
    // settings' and the bindings' strings and storage, then the container's.
    ~COptionsWindow() override;
    // The virtuals of its own.
    BOOL Draw(int pass) override;                      // slides in
    void Redraw() override;                            // not while sliding
    void CloseScreen() override {}
    void MessageResult(int result) override { OnDialogResult(result); }                    // 27
    const char* HelpTopic() override;
    int LeftDown(int b, int x, int y) override { return OnMouseDown(b, x, y), 0; }         // 2
    int LeftDouble(int b, int x, int y) override { return OnDoubleClick(b, x, y), 0; }     // 7
    int KeyDown(int k, int r, int f) override { return OnKeyDown(k, r, f), 0; }            // 9
    void OnMouseDown(int buttons, int x, int y);
    void OnDoubleClick(int buttons, int x, int y);
    void OnDialogResult(int result);
    // Updates the children, the slide-in, and applies the settings whose
    // scroll bar or spin control changed (volumes, the display's gamma at once).
    void Update() override;

    // ui_options.cpp
    // Draws surface 0: the group titles (the shown one white-tinted, its button sprite
    // at full tint, the others half), the shown group's title, and each row's name and value (a
    // binding waiting for its key in red); the highlight sprite (scene object 2) on the selected
    // row.
    void RefreshRows();
    // Shows option group `group` (clamped to 0-10): the rows filled from the
    // settings' and actions' definitions of the group (each row's region gets the "tip option_" /
    // "tip action_" text), the shown rows' controls enabled and the others disabled, the first
    // filled row selected (or row 0), the key wait cleared; RefreshRows.
    void ShowGroup(int group);
    // OK: the key table and every setting stored into the game's option globals (the
    // difficulty only outside a multiplayer client); with a shadow switch (settings 7-9) changed,
    // the shadows the switches turned off are dropped. The options have no resolution.
    void Apply();
    // The 26 settings made again from their definitions: the shown name ("option_" +
    // name), and a slider (kind 0) or an on / off spin control (kind 1; the difficulty's texts
    // "difficulty_hard" / "difficulty_easy") as a child at the definition's row.
    void BuildSettings();
    // The key table and the settings' values from the game's option globals; each
    // setting's slider (SetPosition) or spin control takes its value.
    void LoadSettings();
    // The 59 binding rows made again from the action definitions: the shown name
    // ("action_" + name), and the first key name of a scan code bound to the
    // action, as the keys' text.
    void BuildBindings();

    // Binds pressedKey_ to the selected row's action: the row's
    // keys unbound and replaced by the pressed one (keys text rebuilt), the key taken from the first
    // binding row of the action it was bound to (that row's text rebuilt), then the key table entry
    // set. Nothing without a pressed key or a binding row.
    void CommitBinding();
    // Cancel: puts back the volumes and the display settings the window started with.
    static void RestoreSettings();
    // The window's Create: builds the window (ui_layouts.cpp).
    void CreateScreen() override;

    // Input side (ui_menu_keys.cpp). Keys: Esc cancels, Enter starts a key
    // binding or steps a spin row, Up / Down select rows, PgUp / PgDn switch groups, Left /
    // Right move a row's bar. While a binding waits for its key the next key (scan code) is
    // bound, Esc's scan code giving up; a key bound to another action asks first.
    void OnKeyDown(int key, int repeat, int flags);

    // Row of region `region` (13 and up).
    OptionRow& RegionRow(int region) { return rows_[region - 13]; }

    signed char keyOfScan_[0x200];   // +0x164 the key code bound to each scan code
    int selectedRow_;                // +0x364
    int group_;                      // +0x368 the option group shown
    stlp::vector<OptionBinding> bindings_;  // +0x36C the key binding rows
    stlp::vector<OptionSetting> settings_;  // +0x378 26 settings
    OptionRow rows_[14];             // +0x384
    bool waitingForKey_;             // +0x3F4 a key binding row waits for the new key
    bool sliding_;                   // +0x3F5
    std::uint8_t unused_3F6[2];
    float slide_;                    // +0x3F8
    mfc::CStringArray groupNames_;   // +0x3FC the option groups' titles
    bool shadowsChanged_;            // +0x410 a shadow switch (settings 7-9: units, buildings, flora) changed
    std::uint8_t unused_411[3];
    const UiKeyName* pressedKey_;    // +0x414 the key pressed for a binding row (OnKeyDown; Build
                                     //        clears it)
};
static_assert(offsetof(COptionsWindow, settings_) == 0x378);
static_assert(offsetof(COptionsWindow, shadowsChanged_) == 0x410);
static_assert(offsetof(COptionsWindow, selectedRow_) == 0x364);
static_assert(offsetof(COptionsWindow, bindings_) == 0x36C);
static_assert(offsetof(COptionsWindow, groupNames_) == 0x3FC);
static_assert(offsetof(COptionsWindow, rows_) == 0x384);
static_assert(offsetof(COptionsWindow, waitingForKey_) == 0x3F4);
static_assert(offsetof(COptionsWindow, slide_) == 0x3F8);
static_assert(sizeof(COptionsWindow) == 0x418);

// The message box's edit field (constructor: interface, RECT, 0x17C bytes; the text is set by SetText).
class CUiEditField : public CUiWindow {
public:
    // Input side (ui_edit_field.cpp). While focused:
    // Backspace and Delete remove a character at the caret, Home / End / Left / Right move it
    // (no clamping here); the first edit key starts editing (Backspace / Delete from an empty
    // text, the others with the caret at the end). Then the text is drawn again and
    // the key is marked used. Other keys: nothing.
    void OnKeyDown(int key, int repeat, int flags);
    void DrawContentsVanilla();
    mfc::CString& Text() { return text_; }
    // The window, an empty text, every character class, font 1, at most 50 characters, colours 0xFFFFFF /
    // 0xFF, no surfaces, then a surface (format 5) over `rect` (kept in area_; null when the
    // allocation fails), the text drawn, focused. caret_, editing_ and changed_ are left as they are.
    CUiEditField(CInterface3D* ui, RECT rect);
    // (ui_control_delete.cpp): the
    // text released (the member), then the window's.
    ~CUiEditField() override;
    int KeyDown(int k, int r, int f) override { return OnKeyDown(k, r, f), 0; }   // 9
    int Char(int k, int r, int f) override { return OnChar(k, r, f), 0; }         // 10
    // Input side (ui_edit_field.cpp). While focused and
    // without Ctrl held, a character other than Backspace, Tab, Enter and Esc whose class (digit 1,
    // letter or space 2, '.' or ':' 8, other 4) is allowed: the first one starts editing from an
    // empty text; it is inserted at the caret when the text is shorter than maxLength_ and the new
    // text is narrower than area_ in font_, then the text is drawn and the key used.
    void OnChar(int key, int repeat, int flags);
    // (ui_edit_field.cpp). The character classes, the mode, the font,
    // the length limit and the two colours, then the text drawn. SetText: the text (not
    // editing, changed, the caret at its end), drawn. Whether keys are taken:
    void SetLook(std::uint32_t charClasses, char mode, int font, int maxLength, COLORREF colour, COLORREF caretColour);
    void SetText(const char* text);
    void SetFocused(bool focused);

    RECT area_;                      // +0x148 the surface's layout rectangle
    std::uint32_t charClasses_;      // +0x158 classes OnChar takes (1 digits, 2 letters / space, 4 others, 8 '.' ':')
    std::uint8_t centred_;           // +0x15C 0 from the constructor; SetLook's mode: the text is drawn centred
    std::uint8_t unused_15D[3];
    int font_;                       // +0x160 interface font
    int maxLength_;                  // +0x164
    COLORREF colour_;                // +0x168 the text's colour
    COLORREF caretColour_;           // +0x16C SetLook's last argument (0xFF from the constructor)
    mfc::CString text_;              // +0x170 the text typed
    int caret_;                      // +0x174 caret position in text_ (bytes, at a character's start: U4)
    bool editing_;                   // +0x178 an edit key was pressed (the first one sets it)
    bool focused_;                   // +0x179 keys are taken only while set
    bool changed_;                   // +0x17A the text changed
    std::uint8_t unused_17B;
};
static_assert(offsetof(CUiEditField, caret_) == 0x174);
static_assert(offsetof(CUiEditField, changed_) == 0x17A);
static_assert(offsetof(CUiEditField, text_) == 0x170);
static_assert(sizeof(CUiEditField) == 0x17C);

// The message box (constructor: interface, title and text keys, result, flags, format;
// 0x180 bytes; the new game's difficulty question is built
// on it with its own Create). Flags: 1 an OK button, 2 with 1 also a cancel button (neither: one
// cancel button), 4 an edit field instead of the text (its initial text is the text key itself).
class CMessageBox : public CUiContainer {
public:
    // The container, the title (the text of `titleKey`) and,
    // without flag 4, the text of `textKey` (else editText_ = `textKey`, the edit field's first
    // text); `result`, `flags`, `format` kept. A missing text gives its key (GetString).
    CMessageBox(CInterface3D* ui, const char* titleKey, const char* textKey, std::uint32_t result, std::uint32_t flags,
                std::uint32_t format);
    // (also the difficulty question's, the same code; ui_window_delete.cpp): the text and title
    // strings, then the container's.
    // strings, then the container's.
    ~CMessageBox() override;
    // The virtuals of its own (also the master request box's).
    void CloseScreen() override { OnClose(); }                                           // 26
    int LeftDown(int b, int x, int y) override { return OnLButtonDown(b, x, y), 0; }   // 2
    int KeyDown(int k, int r, int f) override { return OnKeyDown(k, r, f), 0; }        // 9
    // Builds the box: the "saveload" textures, a surface over
    // 200..600 x 50..500 with the title and the text (or the edit field) centred vertically as a
    // block, the dark shade and frame behind it and the buttons below (regions with tips
    // 0xC3B4 OK / 0xC3B5 cancel).
    void CreateScreen() override;
    // Input side (ui_message_box.cpp). The container's handler first;
    // then region 0 (OK): for the exit question (result_ 0xFFFF0000) the exit movie and the game
    // quits; otherwise the OK sound and, with the edit field, its text copied into editText_;
    // region 1 the cancel sound; any region n >= 0 then leaves result_ | (n + 1) pending.
    void OnLButtonDown(int buttons, int x, int y);
    // Input side. The container's handler first; unless a child used
    // the key, Enter answers as region 0 (the edit text copied before the sound) and Esc as
    // region 1, and the key is marked used.
    void OnKeyDown(int key, int repeat, int flags);
    // The OK answer's edit text into editText_ (the caller's buffer).
    void CopyEditText();
    // Close (also of the master request box and the difficulty
    // question; ui_small_members.cpp). The box closes: its scene objects and surfaces
    // released.
    void OnClose();

    mfc::CString title_;             // +0x164 the text of the title key
    mfc::CString text_;              // +0x168 the text of the text key
    std::uint32_t result_;           // +0x16C 0xFFFF0000: the text in font 1, else 2
    std::uint32_t flags_;            // +0x170
    std::uint32_t format_;           // +0x174 added to the text's DrawText format
    CUiEditField* edit_;             // +0x178 the edit field (flag 4)
    char* editText_;                 // +0x17C the edit field's first text; OK copies the typed one here
};
static_assert(offsetof(CMessageBox, title_) == 0x164 && offsetof(CMessageBox, text_) == 0x168);
static_assert(offsetof(CMessageBox, edit_) == 0x178);
static_assert(sizeof(CMessageBox) == 0x180);

// CMasterRequestBox, the message box waiting on a master server request: ui_screens.h.

// The help / message window (0x194 bytes; built inline by
// CInterface3D::ShowMessage and OpenHelp, ui_create_screen.cpp). Its pages
// come from the texts: "tutorial <text_>" holds the title key and a "#screen" line per page, and
// "tutor <key>" each key's text. Names provisional.
class CHelpWindow : public CUiContainer {
public:
    // Built inline by CInterface3D::ShowMessage (`message` set, the message's saved
    // page) and OpenHelp (the topic's): the container, the text, an empty title, the
    // pages' array, the page, the two lists with their sentinels.
    CHelpWindow(CInterface3D* ui, bool message, const mfc::CString& text, int page)
        : CUiContainer(ui), message_(message), text_(text), page_(page) {
        lines_.Construct();
        pulsing_.Construct();    // the allocator proxy, then a sentinel
        pictures_.Construct();
    }
    // (ui_window_delete.cpp): the two lists
    // (nodes, then the sentinel), the text array, the two strings, then the container's.
    ~CHelpWindow() override;
    // The virtuals of its own.
    BOOL Draw(int pass) override;
    void CloseScreen() override { Close(); }                                         // 26
    int LeftDown(int b, int x, int y) override { return OnLeftDown(b, x, y), 0; }    // 2
    int KeyDown(int k, int r, int f) override { return OnKeyDown(k, r, f), 0; }      // 9
    // Loads "saveload", makes a 256x256 texture, reads the
    // title and the pages (above; the keys replaced by their "tutor" texts, trailing CR / LF
    // trimmed); without the text or without pages the interface's pending value is set to 0 and
    // nothing else happens. Otherwise the page is clamped to the pages, the pulse restarted, the
    // device's alpha reference set to 1 and the page shown.
    void CreateScreen() override;
    // Empties the two lists, then the surfaces, regions and scene objects.
    void Close();
    // The children's update, then the pulse: its time advanced by the frame
    // time and the pulsing sprites' plain colour set from it (alpha 255 * v, red and green / blue
    // from 80 * (1 - v), v = (cos(5 t) + 1) / 2).
    void Update() override;
    // Input side. The container's button handler, then region 0 the
    // previous page, 1 OK (Finish), 2 the next page.
    void OnLeftDown(int buttons, int x, int y);
    // Input side. The container's key handler, then Enter the next page
    // (Finish on the last), Esc or the help key (action ';') Finish; each sets the handled flag.
    void OnKeyDown(int key, int repeat, int flags);
    // Body of the Rebirth-patched page setup (ui_help_page.cpp; reproducing Rebirth's WideScreen, so
    // hooked in both launches). Empties the two lists, the surfaces, regions and scene objects,
    // opens "tutorial <text_>" (without it the pending value is set to 0) and goes to the page's
    // "#screen" line. The page's rectangle is (200, 50)-(600, 560), or "#position left top right
    // [rows]" (at least 300 wide, rows x 16 + 110 high, at most 600). Scene objects 0 the plain
    // background, 1 the frame, 2 / 3 / 4 the previous, OK and next buttons; then one pulsing frame
    // per "#rectangle left top right bottom" line (with Rebirth's WideScreen some zone tutorial
    // pages' frames move by the layout's extra units, as the battle screen's panels) and one entry
    // per "#text [picture]" line (the next line's first word is the text's key). Surfaces: 0 the page's title and "title (page/pages)", 1 the
    // entries' wrapped "tutor <key>" texts (laid out twice when no rows are given: the first pass
    // makes the pictures, "tutorial_<picture>" copied into referenced object 1's 64-pixel cells,
    // and gives the height), 2 the buttons' texts with the help key's name. Regions 0-2 the
    // buttons.
    void ShowPage();

    bool message_;                       // +0x164 a message window (ShowMessage), not help
    std::uint8_t unused_165[3];
    mfc::CString text_;                  // +0x168 the message, or the help topic
    mfc::CString title_;                 // +0x16C
    mfc::CStringArray lines_;            // +0x170 the pages' texts
    int page_;                           // +0x184 the page shown (the topic's or message's saved page)
    stlp::list<CUiMesh*> pulsing_;       // +0x188 sprites whose plain colour pulses (Update)
    stlp::list<CUiMesh*> pictures_;      // +0x18C the entries' picture sprites
    float pulseTime_;                    // +0x190

private:
    // The OK button's end: the "ok" sound, the page kept for the topic (or the message), the
    // interface's pending value set to 0.
    void Finish();
};
static_assert(offsetof(CHelpWindow, text_) == 0x168);
static_assert(offsetof(CHelpWindow, lines_) == 0x170);
static_assert(offsetof(CHelpWindow, pulsing_) == 0x188);
static_assert(offsetof(CHelpWindow, page_) == 0x184);
static_assert(sizeof(CHelpWindow) == 0x194);

// A line of the in-game chat overlay: the text, the speaker, a value and the line's own surface.
struct ChatLine {
    // Destructor: the surface released and freed (cleared),
    // then the two strings.
    void Destroy();

    mfc::CString text;          // +0x00 (AddLine leaves it empty)
    mfc::CString name;          // +0x04 the line's text without its colour codes
    std::uint32_t time;         // +0x08 timeGetTime when it was added
    CUiSurface* surface;        // +0x0C
};
static_assert(sizeof(ChatLine) == 0x10);

// The in-game chat overlay (made by CInterface3D::Create): the interface's root window (CInterface3D +0x1C), updated after the top
// window every frame; in a multiplayer client it holds the chat line, which takes the keys while
// it is open.
class CChatOverlay : public CUiWindow {
public:
    // The window base, an empty line list (its sentinel from
    // the node pool), an empty typed line, the typed line's
    // surface over (10, 573)-(790, 590) (format 4), closed, the caret at 0, then the typed line drawn
    // (Rebirth-patched).
    explicit CChatOverlay(CInterface3D* ui);
    // (ui_window_delete.cpp): every chat line
    // (destroyed, node freed), the list's sentinel; the typed line (the member).
    ~CChatOverlay() override;
    // (ui_chat_overlay.cpp; CInterface3D::OnChatLine). Adds a chat line from
    // player `number` (unused): the text is broken into lines narrower than 780 pixels (font 1;
    // at spaces, a word wider than a line on its own), "^<digit>" switching the colour (a missing
    // digit is taken as 7); the oldest lines are erased so that at most 12 remain; each new line
    // gets the time and a surface (its width + 3 by 17, format 5) with its pieces drawn in their
    // colours (the digit's bits 0 / 1 / 2 give blue / green / red, white to start).
    void AddLine(const char* text, std::uint32_t number);
    // Opens (`open`, a byte: the typed line emptied, the caret at 0) or closes the
    // chat line, then draws it.
    void SetOpen(bool open);
    // Every line destroyed and its node returned to the pool; the list left empty.
    void ClearLines();
    // Rebirth-patched, WideScreen reproduced behind its flag;
    // rewritten callers go through ChatOverlayDrawTypedOriginal. The typed line's surface hidden
    // while the chat is closed; else shown, the caret clamped to the line, the surface cleared,
    // the line drawn (font 1, white, DT_NOPREFIX, shadowed) over (0, 0)-(783, 17) and, while the
    // text before the caret is narrower than 780, the caret as a white line at its width from y
    // 0 to 16 (layout points scaled to the screen).
    void DrawTyped();
    // Input side (ui_chat_overlay.cpp; nothing while the chat is closed). Keys: every key is marked used; Backspace / Delete remove the character before / at the
    // caret, Home / End / Left / Right move it (unclamped; the draw clamps it), Enter sends a
    // non-empty line as a chat message (a network client: SayMessage kept 60 s) and,
    // like Esc, closes the chat; then the line is drawn. Characters: a character
    // other than 8, 9, 13 and 27 is inserted at the caret unless Ctrl is down, the line is 200
    // characters or more, or the new line would be 780 pixels wide or more (font 1);
    // then the line is drawn. The character is not marked used.
    void OnKeyDown(int key, int repeat, int flags);
    void OnChar(int key, int repeat, int flags);
    int KeyDown(int k, int r, int f) override { return OnKeyDown(k, r, f), 0; }   // 9
    int Char(int k, int r, int f) override { return OnChar(k, r, f), 0; }         // 10
    // Lines older than 20 s (timeGetTime) are erased.
    void Update() override;
    // Each line's surface placed at (10, 10 + 16 * row) and blitted, then the
    // window's own redraw.
    void Redraw() override;

    stlp::list<ChatLine> lines_;     // +0x148
    bool chatOpen_;                  // +0x14C a multiplayer client's chat line is open (it takes the keys)
    std::uint8_t unused_14D[3];
    mfc::CString typed_;             // +0x150
    int caret_;                      // +0x154 in the typed line (a byte offset at a character's start: U4)
};
static_assert(offsetof(CChatOverlay, lines_) == 0x148);
static_assert(offsetof(CChatOverlay, caret_) == 0x154);
static_assert(offsetof(CChatOverlay, typed_) == 0x150);

// The new game's difficulty question (0x184 bytes; the main menu's
// region 0): a message box ("option_difficulty", OK and cancel) with two check boxes. Its button
// handler sets easy_ from the boxes (regions 2 / 3) and copies it to GameDifficulty() on OK.
class CDifficultyBox : public CMessageBox {
public:
    // Built inline by the main menu's click: the message box ("option_difficulty", no
    // text, result 0x20000, OK and cancel, format 1), easy_ left.
    explicit CDifficultyBox(CInterface3D* ui) : CMessageBox(ui, "option_difficulty", game::EmptyText(), 0x20000, 3, 1) {}
    // The virtuals of its own (the message box's first one).
    int LeftDown(int b, int x, int y) override { return OnLButtonDown(b, x, y), 0; }   // 2
    int KeyDown(int k, int r, int f) override { return OnKeyDown(k, r, f), 0; }        // 9
    // Input side (ui_message_box.cpp; no container handler). Region 0
    // OK: easy_ to GameDifficulty(), the OK sound, result_ | 1 pending; 1 cancel: the cancel
    // sound, result_ | 2; 2 / 3 check easy / hard (the checked box's sprite shows the texture
    // shifted by kCheckedOffset, the other unshifted).
    void OnLButtonDown(int buttons, int x, int y);
    // Input side. The container's handler first; unless a child used
    // the key, Enter answers as OK and Esc as cancel, and the key is marked used.
    void OnKeyDown(int key, int repeat, int flags);
    // The check boxes: the first two sprites (Build adds them first).
    CUiMesh* EasyBox() const { return SceneMesh(0); }
    CUiMesh* HardBox() const { return SceneMesh(1); }
    // The texture shift of a checked box (0xBDB80000).
    static constexpr float kCheckedOffset = -0.08984375f;

    // Builds the box: the "saveload" textures, a surface over
    // 200..600 x 100..500 with the title, the "tip option_difficulty" text and the
    // "difficulty_easy" / "difficulty_hard" labels; the check boxes beside them (hard checked),
    // the shade, frame and OK / cancel buttons as the message box's; regions OK (tip 0xC3B4),
    // cancel (0xC3B5), easy, hard.
    void CreateScreen() override;

    int easy_;                       // +0x180 1 easy, 0 hard (the default)
};
static_assert(offsetof(CDifficultyBox, easy_) == 0x180);
static_assert(sizeof(CDifficultyBox) == 0x184);

// The mouse, members of the cursor (ui_cursor_setup.cpp): the position in screen pixels (CCursor::position_) and the
// motion a right-button drag accumulates, read and cleared by the drag's mouse-move handlers
// (CCursor::motion_).
long& MouseScreenX();
long& MouseScreenY();
long& MouseDragX();
long& MouseDragY();

// The game's difficulty (1 easy, 0 hard; combat reads it).
inline int& GameDifficulty() { return options::TheOptions()->difficulty_; }

// key_bindings.cpp: reads and writes config\keyboard.ini (the interface's create and shutdown).
namespace keybind {
void __cdecl LoadKeyBindings();
void __cdecl SaveKeyBindings();
}
