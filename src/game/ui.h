#pragma once

// The 2D interface (address range 005F0000-00612000): the interface manager CInterface3D, its
// windows, their clickable regions, off-screen surfaces and 3D model previews. Names provisional.
//
// Interface layouts are designed for 800x600. CInterface3D holds the real screen size and scales
// coordinates between the two; windows keep their regions in layout coordinates and convert the
// mouse position before hit testing.

#include "core_transform.h"
#include "engine_object.h"
#include "item_db.h"
#include "unit.h"   // LockKey

#include "lib/containers.h"
#include "lib/mfc.h"
#include "lib/stlp.h"

#include <cstddef>
#include <cstdint>

#include <windows.h>

namespace mfc { class CString; }
namespace console { class ConsoleWindow; }
namespace ui_gdi { struct GameFont; }

// A CString an item's or a spell's virtual method returns (by hidden pointer); released when it goes.
struct ReturnedString {
    char* data = nullptr;
    mfc::CString* Slot() { return reinterpret_cast<mfc::CString*>(&data); }
    ~ReturnedString() { Slot()->~CString(); }
};
static_assert(sizeof(ReturnedString) == 4);

class CUiWindow;
class CChatOverlay;
struct UiInputSide;
class CUiMesh;
struct CDXSurface;
class CSceneRenderer;   // model.h
class CTextureDevice;   // texture.h
class CStream;          // core_streams.h
class CUiSurface;

// The interface's screen fade and letterbox (CInterface3D +0x18), drawn over everything by the
// interface's draw (005F5220). Only the fields its draw uses.
struct CScreenFade {
    // 005E69C0 (RET 4; ui_interface_draw.cpp). With the letterbox, black bars of the screen height
    // times 0.125-ish (00740150) at the top and bottom; while fading, the alpha moves by
    // rate * elapsed ms towards 255 (rate > 0) or 0 (rate < 0), clamped at 255; below 0 the fade
    // ends (nothing drawn), otherwise a black rectangle of that alpha over the screen. Both draws
    // with the depth test off (render state 7) through the renderer's slot 19. Returns 1.
    int Draw(CSceneRenderer* renderer);
    // 005E6B90 (RET 4; ui_interface_draw.cpp). Starts a fade over `ms` milliseconds: rate_ 255 /
    // ms (stored as float), from alpha 0 (ms > 0, fading out) or 255, the clock now, fading.
    void Start(int ms);
    // 005E6990 (ui_small_members.cpp; CInterface3D::Create builds it). Constructor: alpha 0, not
    // fading, no letterbox; the rate and the clock are not written. Returns this.
    CScreenFade* Construct();

    float rate_;                     // +0x00 alpha per millisecond
    float alpha_;                    // +0x04 0..255
    std::uint8_t active_;            // +0x08 fading
    std::uint8_t unknown_09[3];
    DWORD lastTime_;                 // +0x0C timeGetTime of the last step
    std::uint8_t letterbox_;         // +0x10 the bars are shown
};
static_assert(offsetof(CScreenFade, lastTime_) == 0x0C && offsetof(CScreenFade, letterbox_) == 0x10);

// The interface manager (CInterface3D; created by 005F4990, which reads the screen size from the
// render target). Only the members used by the rewritten code; the object is created by original
// code, so its size is not asserted.
class CInterface3D {
public:
    // 005F7CA0. Scales a layout rectangle (800x600) to the screen. Returns what the original
    // leaves in EAX: the low dword of the last division's magic multiplication (callers ignore it).
    int ScaleRectToScreen(RECT* rect) const;
    // 005F7D20. Same for a point.
    int ScalePointToScreen(POINT* point) const;
    // 005F7DA0. Scales a screen rectangle to layout coordinates; returns the new bottom (EAX).
    int ScaleRectFromScreen(RECT* rect) const;
    // 005F7E00 and 005F7E30 (identical copies). Screen point to layout; returns the new y (EAX).
    int ScalePointFromScreen(POINT* point) const;
    int ScalePointFromScreenCopy(POINT* point) const;

    // Rewritten code calls the scaling functions through their original addresses: Rebirth patches
    // all of them (widescreen), so in that install the hooks are skipped and the patched versions
    // must still run (rewrite-guide rule 9). In the vanilla install these land on the rewrites above.
    int CallScaleRectToScreen(RECT* rect) const;        // -> 005F7CA0
    int CallScalePointToScreen(POINT* point) const;     // -> 005F7D20
    int CallScalePointFromScreen(POINT* point) const;   // -> 005F7E00

    // 005F57C0. Once per frame (main loop): closes the top window while a value is pending,
    // closes all windows on request, activates a queued window, reads the mouse and updates the
    // top window and the root (ui_interface_frame.cpp).
    void Update();

    // 005F8070 (ui_interface_frame.cpp). A script string event ("say" / "say_block" text [key],
    // "tutorial" name, "briefing" name, "autosave", "endofgame"); the first word is lowercased.
    // Rebirth's NewStringEvents adds "gameover [title [text]]".
    void OnStringEvent(const char* event);
    // 005F7590. The window on top of the active-window stack, or null.
    CUiWindow* GetActiveWindow() const;
    // 005F74E0. Stores a value and raises the flag next to it (consumed by the frame update).
    void SetPendingValue(std::uint32_t value);
    // 005F5BA0 (RET 4). Makes screen `id` (1 in-game, 2, 3 base, 4 island map, 5, 7, 8, 9) and hands
    // it to the activation (005F71C0); 0 does nothing, 6 and ids above 9 hand over the id
    // itself as the window pointer (as the original). ui_create_screen.cpp.
    void CreateScreen(int id);
    // 005F78C0 (RET 0xC; the interface's key-down handler, `flags` the WM_KEYDOWN lParam bits:
    // scan code in the low 9 bits, bit 14 repeat). Ctrl, Shift and Alt set +0x7C, +0x7D, +0x7E
    // (not on a repeat); the key under Esc toggles the console; Scroll Lock saves
    // "shot<nnnn>.bmp"; other keys go to the shown console, else (multiplayer client) the window
    // at +0x1C when its +0x14C is set, else the active window, and a ';' key nobody handled
    // shows help (005F86F0). ui_interface_keys.cpp.
    void OnKeyDown(int key, int repeat, int flags);
    // 005F7350 (RET 0xC). Opens `window` on top of the stack. With `a` set (low bytes) and `b`
    // clear: +0x50 = 3 and 00458010(0, 1) first; for the first window: +0x60 keeps the server
    // flag, which is then set (not as a multiplayer client), and with `a` and `b` set +0x50 = 1
    // and 00458010(0, 1). The modifier flags are cleared, the old top deactivated (input slot 1,
    // slot 19 with 0), the window pushed (null is fatal), five render states reset, the window
    // created (slot 25) and the messages pumped. ui_create_screen.cpp.
    void PushWindow(CUiWindow* window, int a, int b);
    // 005F71C0 (RET 4). Replaces every window with `window`: a pending close-all (+0x44) runs
    // first (005F74F0); both backdrops (+0x54, +0x58) are deleted and +0x50 / +0x60 cleared, the
    // server flag cleared; each open window is deactivated (input slot 1), closed (slot 26) and
    // deleted, top first; then 005F8C50, the message pump, and the window opened as PushWindow
    // does (null is fatal). ui_create_screen.cpp.
    void Activate(CUiWindow* window);
    // 005F8420 (RET 4). Reads the interface's saved state from `stream` (slot 1 reads bytes;
    // +0x08 the save version): +0x89..+0x8B, +0x8C, +0x90; from version 0x47 +0x94; from 0x56 an
    // obsolete list (count, then name and value pairs, discarded) and the CString +0xA4; from 0x5D
    // +0xA8; from 0x5F the CString +0xAC. ui_interface_save.cpp.
    void LoadState(void* stream);
    // 005F8C90 (RET 0xC; on 007C7D50; multiplayer messages). The text of message `id` (0-12:
    // "lmp_player_connected" .. "reserved_02"; others an empty key) from the text table,
    // formatted with `value` when it is not 0, constructed into `out`; returns `out`
    // (ui_interface_save.cpp).
    void* MessageText(void* out, int id, int value);
    // 005F8A80 (RET 4). Without `force`, takes the next queued message (the list at +0x84) into
    // +0xA4 (+0xA8 = 0), or returns when there is none. A non-empty +0xA4 then opens the message
    // window (0x194 bytes, vtables 00740E28 / 00740DF4) over the others, unless the option
    // 0079BE78 is off and it was not forced. ui_create_screen.cpp.
    void ShowMessage(int force);
    // 005F86F0 (RET 4; ShowHelp above calls it through its address). The active window's help
    // topic (its slot 41; none: nothing): the map at +0x98 counts topics seen (an unseen one is
    // entered with 0, giving -1 here); the help window (0x194 bytes, vtables 00740E28 / 00740DF4)
    // opens when forced (`flag`), or when the option 0079BE78 is on and the topic was not seen.
    void OpenHelp(int flag);

    // 005F86F0 (not rewritten). Shows the help text of the active window's topic (its slot 41).
    void ShowHelp(int flag);
    // 005F7E60 (RET 8; ui_interface_glue.cpp). Plays an interface sound (`name`, e.g.
    // "buttons\battle\click.wav") without position: priority 1000, volume 100, `loop` passed on
    // (sound::CSoundManager::Play2D). The interface object is not used. Returns Play2D's handle.
    int PlaySound(const char* name, int loop);
    // 005F7E90 (RET 4). Stops sound `handle` unless it is -1 (the interface is not used).
    void StopSound(int handle);
    // 005F7C60 / 005F7C80 (ui_small_members.cpp; a multiplayer client's screens call them for
    // Enter / Backspace). While the chat line is closed: Enter opens it (SetOpen(1)), Backspace
    // empties the chat lines (ClearLines).
    void ChatKeyEnter();
    void ChatKeyBackspace();
    // 005F8340 (RET 0xC; Rebirth-patched, this is the vanilla body; ui_text_draw.cpp; rewritten
    // callers go through InterfaceTextExtentOriginal). The extent of `text` (a CString taken by
    // value) in interface font `font`, measured on the game window's DC (GetTextExtentPoint32)
    // and scaled to the layout: cx * 800 / width, cy * 600 / height. Returns `out`.
    SIZE* TextExtent(SIZE* out, mfc::CString text, int font);
    // 005F46A0 (ui_small_members.cpp). The constructor of the interface object 007C7D50, run by
    // its static initialiser (005F4680) before the hook is installed: the window stack empty, the
    // message list's sentinel and the help topic map's header from the node pool, the two texts
    // empty. Nothing else is written. Returns this.
    CInterface3D* Construct();
    // 005F7EB0 / 005F7ED0 / 005F7EF0 (RET 4). The console window's AddText / SetRect / SetHistory,
    // when there is one.
    void ConsoleAddText(const char* text);
    void SetConsoleRect(const int* rect);
    void SetConsoleHistory(int lines);
    // 005F7F10 (RET 4). The debug information panel's rectangle (00444C10; the interface is not
    // used).
    void SetDebugInfoRect(const RECT* rect);
    // 005F5700 (RET 4; the console's "fps"). With `on` (low byte) a surface for the frame rate at
    // (730, 10)-(800, 28) unless there is one; without, the surface freed.
    void ShowFps(int on);
    // 005F7FC0 (RET 8; the client and the script tell it of a quest variable change). The top
    // window's slot 33 hears of it; at 1 (or NaN) a name of three or more '.' parts keeps its
    // third part in savedText_ (the current quest).
    void OnQuestVariable(const char* name, float value);
    // News for the top window (ui_interface_glue.cpp; nothing without a window): 005F7F30 (RET 0xC)
    // items picked up (slot 28), 005F7F60 / 005F7F80 (RET 4) a unit joined / left the party (slots
    // 29 / 30; the client's UnitChanged 00667BA0 picks one by the unit's +0x24C), 005F7FA0 the game
    // is lost (slot 31 with 0), 005F82A0 (RET 8) a unit speaks (slot 32 with no extra), 005F82D0
    // (RET 4) the players changed (slot 36), 005F8310 (RET 0xC) an exchange between players ended
    // (slot 37). 005F86D0: the bottom window saves its state (slot 40; the save game 0057B3B0).
    void OnItemsPicked(void* items, int money, int crippled);
    void OnUnitJoined(void* unit);
    void OnUnitLeft(void* unit);
    void OnGameLost();
    void OnUnitSpeech(int id, unsigned event);
    void OnPlayersChanged(int added);
    void OnExchangeDone(void* partyB, void* partyA, void* partyC);
    void SaveWindowState();
    // 005F82F0 (RET 8). A chat line (`text` from player `number`) for the root window's chat
    // (005E2A30 on +0x1C).
    void OnChatLine(const char* text, std::uint32_t number);
    // 005F75B0 (RET 4; the save / load window while it saves or loads). `text` drawn in green
    // (005FDE20: rect (0, 0)-(800, 40), format 2) on a cleared surface over (0, 280)-(800, 320)
    // (+0x64), one frame shown (00458010 with 1, 1), the surface freed.
    void ShowStatusText(const char* text);

    // Mouse messages from the input dispatcher (00457970; ui_interface_input.cpp; RET 0xC, the
    // wheel RET 0x10): each goes to the input side that took the mouse (+0x0C), else to the top
    // window's, else nowhere. 005F76A0 left down (clears +0x7F first), 005F76E0 left up (+0x7F
    // cleared after), 005F7740 move, 005F7780 / 005F77C0 right down / up (+0x80 set / cleared
    // first), 005F7800 left double click (+0x7F set first), 005F7840 right double click (+0x80
    // set first), 005F7880 wheel (returns the handler's result, 0 without a target).
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
    // 005F7B40 (RET 0xC; ui_interface_input.cpp). A typed character (WM_CHAR): nothing for the
    // key under Esc (scan 0x29); the shown console's OnChar, else a multiplayer client's open chat
    // line (+0x1C), else the top window (input slot 10).
    void OnChar(int key, int repeat, int flags);
    // 005F7BE0 (RET 0xC). A key released: VK_SHIFT / VK_CONTROL / VK_MENU clear +0x7D / +0x7C /
    // +0x7E; then the shown console's OnKeyUp (005E47E0), else the top window (input slot 11).
    void OnKeyUp(int key, int repeat, int flags);
    // 005F74F0. Closes every window, top first (input slot 1, slot 26, deleting destructor), with
    // the cursor's shape 3 first; clears the five flags +0x7C..+0x80, pumps the messages and
    // clears the screen flag (InterfaceHasScreen).
    void CloseAll();
    // 005F5AB0 (the input dispatcher's first step). CloseAll when +0x44 asks for it (cleared).
    void CloseAllPending();
    // 005F5220 (ui_interface_draw.cpp; every frame from the main loop). A pending close-all
    // (CloseAllPending, inlined by the original); the backdrop (+0x54, its slot 0) in backdrop
    // modes 2-4; the top window's slot 20 with the renderer (00500960); then the fade over it all.
    // Returns 1.
    int Draw();
    // 005F5320 (Rebirth-patched, this is the vanilla body, hooked in the vanilla launch only;
    // ui_interface_draw.cpp; the main loop calls it after the scene's EndScene through its
    // address). What is drawn over the scene with GDI and blits: the debug panel hears the
    // surfaces' kept and allocated bytes; the top window redraws its surfaces (slot 22) and
    // tooltips (slot 23); the status surface blits; a backdrop asked for (mode 1 or 3) is made
    // (800 x 600, or 400 x 300 stretched when the option 00784753 is set) and takes the screen:
    // mode 1 also keeps a 256 x 192 copy (+0x5C) and greys the backdrop (mode 2), mode 3 first
    // puts the old backdrop aside (+0x58, its contents kept in memory) (mode 4); a multiplayer
    // client with one window redraws the chat overlay; the debug panel ticks; the frame rate
    // ("FPS:" and frames * 1000 / ms, every 400 ms) is drawn and blitted while shown; the shown
    // console blits.
    void DrawOverlays();
    // 005F4990 (RET 4; Rebirth-patched, this is the vanilla body, hooked in the vanilla launch
    // only; ui_interface_create.cpp; the application's start 004570D4 calls it with the
    // renderer's device object 007AF080). The screen size is the render target's (not written
    // usefully without a primary surface). The three fonts are "Times New Roman": a 75-point one
    // is made first and its height in pixels / 100 is the scale s; the fonts are of
    // _ftol(width * k / s / 800) tenths of a point, k = 104, 112, 144 (a font that cannot be
    // made is fatal: "CInterface3D::Create.\n Cannot create font.\n", and nothing else is
    // done). The keyboard layout of the language 0079BE98 is activated (each layout is activated
    // in turn and its name read as hex), else the English one (0x409), else the one it began
    // with. Then the key bindings are read, the console window (0x7C bytes), the debug
    // information panel, the chat overlay (0x158) and the fade (0x14) are made, the state is
    // cleared and config\tutorial.ini's "topic count" lines are read into the help topics.
    void Create(CTextureDevice* device);

    int width_;                      // +0x00 screen width
    int height_;                     // +0x04 screen height
    struct UiRenderer* renderer_;    // +0x08 (its +0x54 is the Direct3D 7 device)
    void* mouseCapture_;             // +0x0C input side (window +0x94) that took the button press
    std::uint8_t unknown_10[0x14 - 0x10];
    console::ConsoleWindow* console_;   // +0x14 the console window (shown while its +0x7A is set)
    CScreenFade* fade_;              // +0x18
    CChatOverlay* root_;             // +0x1C the chat overlay; updated after the top window every frame
    CUiSurface* fpsSurface_;         // +0x20 the frame rate's surface while shown (ShowFps)
    int unknown_24;                  // +0x24 cleared by Create
    DWORD fpsTime_;                  // +0x28 when the frame rate was last worked out (DrawOverlays)
    int fpsFrames_;                  // +0x2C frames since then
    // +0x30 the windows draw, redraw their surfaces and update their tooltips only while this is
    // set (the containers' slots 20, 22 and 23).
    bool drawing_;
    std::uint8_t unknown_31[0x34 - 0x31];
    CUiWindow** stackBegin_;         // +0x34 stack of active windows (STLport vector)
    CUiWindow** stackEnd_;           // +0x38
    CUiWindow** stackCapacity_;      // +0x3C
    CUiWindow* queuedWindow_;        // +0x40 activated by the next frame update (005F57C0)
    bool closeAll_;                  // +0x44 every window is closed (005F5AB0)
    std::uint8_t unknown_45[0x48 - 0x45];
    std::uint32_t pendingValue_;     // +0x48
    bool pendingFlag_;               // +0x4C
    std::uint8_t unknown_4D[0x50 - 0x4D];
    int backdropMode_;               // +0x50 the backdrop is drawn in modes 2, 3 and 4 (PushWindow sets 3 / 1)
    CUiSurface* backdrop_;           // +0x54
    CUiSurface* secondBackdrop_;     // +0x58
    void* block5C_;                  // +0x5C freed by the shutdown with operator delete (writer not identified)
    std::uint8_t serverFlag_;        // +0x60 the server flag kept for the first window (PushWindow)
    std::uint8_t unknown_61[0x64 - 0x61];
    CUiSurface* statusSurface_;      // +0x64 only while ShowStatusText runs
    int mouseX_;                     // +0x68 mouse position in screen coordinates
    int mouseY_;                     // +0x6C
    // +0x70..+0x78 the three interface fonts (Create 005F4990: "Times New Roman", sized with the
    // screen width); text drawing selects font n from here.
    ui_gdi::GameFont* fonts_[3];
    // Set by the key-down handler (005F78C0) from the scan codes, cleared by the key-up one
    // (005F7BE0) from the virtual keys and when the windows change.
    bool ctrlDown_;                  // +0x7C
    std::uint8_t shiftDown_;         // +0x7D
    bool altDown_;                   // +0x7E
    // The left press is a double click (005F7800 sets it, the button's down and up clear it); a
    // spell or quick item clicked twice is used at once.
    bool doubleClick_;               // +0x7F
    bool rightDown_;                 // +0x80 the right button is held
    std::uint8_t unknown_81[0x84 - 0x81];
    stlp::list<mfc::CString> messages_;   // +0x84 queued message texts (ShowMessage takes the first)
    std::uint8_t unknown_88;
    // +0x89..+0x97: the state saved with a game (LoadState), cleared by ResetSavedState.
    std::uint8_t unknown_89[3];
    int unknown_8C;
    int unknown_90;
    int fastGame_;                   // +0x94 the fast game speed (the clock's +0x154)
    // +0x98 help topics seen: topic name -> times (or the page the help window was closed on; see ui_help_window.cpp) (config\tutorial.ini, "name count" lines; read
    // by Create 005F4990, written by Shutdown, counted by OpenHelp 005F86F0).
    stlp::map<mfc::CString, int> helpTopics_;
    mfc::CString messageText_;       // +0xA4 the message window's text (ShowMessage)
    int messageFlag_;                // +0xA8 (ShowMessage clears it; the message window's page, set when it closes)
    mfc::CString savedText_;         // +0xAC the current quest (OnQuestVariable); saved from version 0x5F

    // 005F8C50. Clears the modifier, double-click and right-button flags (+0x7C..+0x80) and
    // deactivates the top window's input side (input slot 1), if any (ui_interface_shutdown.cpp).
    void ResetInput();
    // 005F4F90 (the main loop's exit, 00456440). Closes every window (as CloseAll, without its
    // trailing deactivation), ResetInput, pumps the messages, clears the screen flag; writes the
    // help topics to config\tutorial.ini ("%s %d\n" each); then frees the frame rate surface (+0x20), the queued window
    // (+0x40, not cleared), both backdrops, +0x5C, the debug panel's surface and font (00444BD0),
    // the root window, the console window; saves the key bindings (00462DF0); frees the three fonts
    // and the fade (+0x18, not cleared).
    void Shutdown();
    // 005F4790 (the destructor of the global interface 007C7D50, run at exit): the two CStrings,
    // the help topic map, the message list, the window stack's storage.
    void DestroyBody();

    // 005F85C0 (RET 4; ui_interface_save.cpp). Writes what LoadState reads: +0x89..+0x8B, +0x8C,
    // +0x90, +0x94, an empty obsolete list (count 0), the message text with its zero, +0xA8 and
    // the saved text with its zero.
    void SaveState(CStream* stream);

    // 005F51F0 (ui_main_menu.cpp; the main menu's Create). Clears the saved state: +0x89..+0x8B,
    // +0x8C, +0x90 and +0x94.
    void ResetSavedState();
};
static_assert(offsetof(CInterface3D, mouseCapture_) == 0x0C);
static_assert(offsetof(CInterface3D, doubleClick_) == 0x7F);
static_assert(offsetof(CInterface3D, stackBegin_) == 0x34);
static_assert(offsetof(CInterface3D, pendingValue_) == 0x48);
static_assert(offsetof(CInterface3D, mouseX_) == 0x68);
static_assert(offsetof(CInterface3D, messageText_) == 0xA4 && offsetof(CInterface3D, savedText_) == 0xAC);
static_assert(offsetof(CInterface3D, console_) == 0x14);
static_assert(offsetof(CInterface3D, root_) == 0x1C);
static_assert(offsetof(CInterface3D, drawing_) == 0x30);
static_assert(offsetof(CInterface3D, closeAll_) == 0x44);
static_assert(offsetof(CInterface3D, rightDown_) == 0x80);
static_assert(offsetof(CInterface3D, unknown_8C) == 0x8C);
static_assert(offsetof(CInterface3D, fastGame_) == 0x94);
static_assert(offsetof(CInterface3D, fpsSurface_) == 0x20 && offsetof(CInterface3D, queuedWindow_) == 0x40);
static_assert(offsetof(CInterface3D, statusSurface_) == 0x64);
static_assert(offsetof(CInterface3D, block5C_) == 0x5C && offsetof(CInterface3D, fonts_) == 0x70);
static_assert(offsetof(CInterface3D, messages_) == 0x84 && offsetof(CInterface3D, helpTopics_) == 0x98);

// The interface's growable pointer arrays: engine aligned arrays of pointers (lib::AlignedArray).
// The original has one out-of-line SetSize per array (006003D0, 006004E0, 00600730, 00600840,
// 00600950, 00601D00) and SetAtGrow (006005F0), registered on AlignedArray's members; Add,
// RemoveAt and RemoveAll it inlines.
template <typename T>
using UiPtrArray = lib::AlignedArray<T*>;

// Allocates storage for `bytes` with the interface arrays' 32-byte alignment (00600A80 for 4-byte
// elements, 004F65B0 for 8-byte ones); FreeAligned releases it.
void* AllocAligned(std::size_t bytes);
void FreeAligned(void* data);

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
    // 005FC8F0. Whether (x, y) lies in the region: inside the bounds, and unless the region is a
    // plain rectangle, on the inner side of every polygon edge.
    BOOL HitTest(int x, int y) const;
    // 005FC660. Whether the tooltip is showing.
    BOOL HasActiveTip() const;
    // 005FC9E0. Makes the region the rectangle (left, top, right, bottom) and enables it.
    void SetRect(int left, int top, int right, int bottom);
    // 005FCA40. Drops the shape and disables the region.
    void ClearShape();
    // 005FCA70. Adds a polygon vertex, growing the bounds; three vertices enable the region.
    void AddPoint(int x, int y);
    // 005FC880. Updates the tooltip state for a mouse position and forwards it to the tooltip.
    void OnMouseMove(int x, int y);
    // 005FC600 (ui_region_tips.cpp). The destructor body: deletes the tooltip and frees the
    // vertex storage (the fields are left as they are).
    void ReleaseContents();
    // 005FC690 (RET 4; ui_region_tips.cpp). Sets the tooltip's text (0065E540) when there is a
    // tooltip.
    void SetTipText(const char* text);
    // 005FC730 (RET 0xC; ui_region_tips.cpp). Replaces the tooltip by one with the text
    // "tip <id>" (up to a '#'; the key itself when there is no such text) and (a, b); id 0 gives
    // an empty tooltip with (1, 0).
    void SetTip(int textId, int a, int b);
    // 005FC6B0 (RET 0xC; ui_region_tips.cpp). Replaces the tooltip by one showing `text`.
    void SetTipString(const char* text, int a, int b);
    // Layout code calls SetRect through its original address: Rebirth patches it (rule 9).
    void CallSetRect(int left, int top, int right, int bottom);   // -> 005FC9E0
    void CallAddPoint(int x, int y);                              // -> 005FCA70 (Rebirth-patched too)
    // 0062AC90 (RET 0x10; ui_main_menu.cpp). SetRect (through its original address) from a RECT
    // passed by value; the menus' layouts use it for their menus.reg rectangles.
    void SetRectFrom(RECT rect);

    CInterface3D* interface_;        // +0x00
    lib::AlignedArray<UiPoint> points_;   // +0x04 polygon vertices (aligned storage; 8-byte allocator 004F65B0)
    RECT bounds_;                    // +0x14
    bool rectOnly_;                  // +0x24
    bool enabled_;                   // +0x25
    std::uint8_t unknown_26[2];
    int offsetX_;                    // +0x28 subtracted from tested points
    int offsetY_;                    // +0x2C
    CUiTooltip* tip_;                // +0x30
};
static_assert(offsetof(CUiRegion, bounds_) == 0x14);
static_assert(offsetof(CUiRegion, offsetX_) == 0x28);
static_assert(sizeof(CUiRegion) == 0x34);

// An off-screen DirectDraw surface a window draws into (0x38 bytes, vtable 00740F28, constructed by
// 005FCC00/005FCDD0). Only the placement members are named.
class CUiSurface {
public:
    // 005FD3A0. Sets the screen rectangle from layout coordinates.
    void SetRect(int left, int top, int right, int bottom);
    // 005FE400. Moves the screen rectangle so its top-left is the layout point (x, y).
    void MoveTo(int x, int y);
    // 005FDFA0. Word-wraps `text` to the width of `rect` (layout coordinates; breaks at spaces and
    // '\n') and draws up to `maxLines` lines one rect height apart with 005FDE20 (`font`,
    // `colour`, `format`, `flag` passed on). The DC must be acquired. Returns the line count
    // (ui_text.cpp).
    int DrawWrappedText(const char* text, const RECT* rect, int maxLines, int font, COLORREF colour, int format,
                        int flag);
    // 005FDE20 (RET 0x18; Rebirth-patched, this is the vanilla body, hooked in the vanilla launch
    // only; ui_text_draw.cpp; rewritten callers go through SurfaceDrawText). Draws `text` in
    // interface font `font` and `colour` into `rect` (layout coordinates scaled to the screen,
    // 005F7CA0, then two pixels wider and higher; an empty rect is the whole surface) with the
    // DrawText `format`, transparent; with `shadow` first in 0x080808 one pixel right and down.
    // The DC must be held. Returns DrawText's height scaled back to the layout (005F7E30), or 0
    // without a surface.
    int DrawTextIn(const char* text, const RECT* rect, int font, COLORREF colour, UINT format, int shadow);
    // 005FD120 (ui_surface_create.cpp). Destructor body: releases the DirectDraw surface.
    void ReleaseContents();
    // 005FD760 (RET 8; ui_window.cpp). Sets (`frozen` nonzero) or clears the shown flag (4 of
    // +0x0C): setting it puts kept contents (+0x1C) back into a surface (005FD7A0 with 1); clearing
    // it with `restore` set keeps the contents in memory and releases the surface (005FD7A0 with 0).
    void Freeze(int frozen, int restore);
    // 005FCC00 (ui_surface_create.cpp). Constructor: a surface of the layout rectangle (left, top,
    // right, bottom) with format `format`; returns this.
    CUiSurface* Construct(CInterface3D* ui, int left, int top, int right, int bottom, int format, int flag);
    // 005FD3D0. Copies the screen's rect_ into the surface (nothing when it is the screen).
    // 005FD440 (vtable slot 4, RET 8). A new[] block of width * height 5:6:5 pixels: the surface
    // stretched to that size through a temporary surface. 005FE440. Greys the surface in place
    // ((2 r + 4 g + b) / 16 per channel). ui_surface_pixels.cpp.
    // 005FD7A0 (RET 4). `restore` 0: copies the DirectDraw surface's memory (pitch * height bytes)
    // to +0x1C (size +0x20) and releases the surface; 1: makes a new surface (+0x14 x +0x18) when
    // there is none and copies the kept bytes back into it, freeing them. Both keep the memory
    // totals 007C7E74 (kept bytes) and 007C7E78 (surface bytes).
    void SwapContents(int restore);
    void CaptureScreen();
    std::uint16_t* Rgb565Copy(int width, int height);
    void Greyscale();

    // Slot 0 (005FDA80, ui_surface_blit.cpp): blits the surface onto the render target when it is
    // shown.
    void Blit() {
        using Fn = void(__thiscall*)(CUiSurface*);
        reinterpret_cast<Fn>(static_cast<void**>(vtable_)[0])(this);
    }
    bool Shown() const { return (flags_ & kShown) != 0; }
    static constexpr std::uint32_t kShown = 4;   // set and cleared by Freeze (005FD760)

    // Calls 005FD7A0 through its address (the wrapper runs).
    void CallSwapContents(int restore);
    // 005FDCB0 (RET 8; ui_surface_frame.cpp). With a surface (and its DC held): a one-pixel frame
    // of `colour` (a GDI solid brush) around `rect` (layout coordinates, scaled to the screen through
    // 005F7CA0) or, for an empty rect, around the whole surface.
    void FrameRect(const RECT* rect, COLORREF colour);
    // 005FE370 (RET 4) / 005FE2F0 (ui_surface_frame.cpp). With a surface (its DC held): the whole
    // surface filled with `colour` / with background_ (CDC::FillSolidRect).
    void Fill(COLORREF colour);
    void Clear();

    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x00740F28);
    static constexpr std::uint32_t kFromPicture = 1;   // flags_: made by 005FCF80 from an .mmp picture

    void* vtable_;
    CInterface3D* interface_;        // +0x04
    HDC dc_;                         // +0x08 while the DC is held (005FDC00 .. 005FDC60)
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

// The memory totals the window surfaces keep (005FD7A0, 005FCC00, 005FD120): the bytes of the
// contents kept in memory while swapped out (007C7E74) and the surfaces' height * pitch (007C7E78).
inline std::uint32_t& UiSurfaceKeptBytes() { return *reinterpret_cast<std::uint32_t*>(0x007C7E74); }
inline std::uint32_t& UiSurfaceBytes() { return *reinterpret_cast<std::uint32_t*>(0x007C7E78); }
// The game's device lock (007AEE78), held around DirectDraw calls on the game's surfaces.
inline CRITICAL_SECTION* GameDeviceLock() { return reinterpret_cast<CRITICAL_SECTION*>(0x007AEE78); }

// The draw pass a window's slot 20 is given: slot 7 sets the colour the 3D draws use (+0x18).
struct UiDrawPass {
    void SetColour(std::uint32_t colour) {
        using Fn = void(__thiscall*)(UiDrawPass*, std::uint32_t);
        reinterpret_cast<Fn>(vtable_[7])(this, colour);
    }

    void** vtable_;
    std::uint8_t unknown_04[0x18 - 0x04];
    std::uint32_t colour_;           // +0x18
};
static_assert(offsetof(UiDrawPass, colour_) == 0x18);

class CObject3D;

// A 3D model shown in a window (an inventory item, a portrait): one pointer to a scene object
// (a CObject3D). ui_model.cpp unless noted.
class CUiModel {
public:
    // 005FC450 (ui_interface.cpp). Places the object in front of the camera at a layout position
    // and depth.
    void SetScreenPosition(float x, float y, float depth);
    // 005FC1D0. Releases the object (slot 4) and clears the pointer.
    void ReleaseObject();
    // 005FC2F0 (RET 4). Draws the object in `pass` (a UiDrawPass) in white at the interface's
    // animation clock (timeGetTime / 67), with object flag 0x800 set while it draws.
    void Draw(UiDrawPass* pass);
    // 005FC1F0 (RET 0xC). A new map object (class 0x33, id 0x88CA6C00) of the client world's map
    // object manager, referenced, flag 0x4000000, at (0, 0, 3), with the figure of model `name`
    // (all parts, 00516DA0), its textures (0051C970 on +0x94), animations (00518840 / 00518860)
    // and texture `from` swapped for `to` (0051C250; e.g. "escMenu00" / "escMenu00labels").
    void Load(const char* name, const char* from, const char* to);
    // The same through its address (ui_layouts.cpp), so the wrapper runs.
    void CallLoad(const char* name, const char* from, const char* to);
    // 005FC4F0 (RET 4) shows or hides all its parts (part flag 0x80), 005FC4C0 (RET 8) one part by
    // name.
    void ShowAllParts(int show);
    void ShowPart(const char* name, int show);
    // 005FC3B0 (RET 4). Plays skill animation `animation` + 1 (00518FC0, 005187B0).
    void Play(int animation);
    // 005FC3D0. Plays the figure's rest pose (the object manager's object named like the figure).
    void Stop();
    CObject3D* Figure() const { return reinterpret_cast<CObject3D*>(object_); }

    CTransformObject* object_;
};
static_assert(sizeof(CUiModel) == 4);

// An input side: a window's own at +0x94 ({vtable, interface, handled flag}), or a separate handler
// a container keeps in its list (+0x158; e.g. the battle camera's keys, constructed by 005EACB0).
// The interface hands each event to the top window's side (00457970 -> 005F76A0..005F7BE0) through
// a slot, thiscall: 1 deactivate (no arguments; the window loses the top, 005F7350); 2 / 3 left
// button down / up, 4 mouse move, 5 / 6 right button down / up, 7 / 8 left / right double click
// (button flags, x, y in screen pixels); 9 key down, 10 character (WM_CHAR), 11 key up (key,
// repeat, scan code and flags); 12 wheel (key flags, delta, x, y).
struct UiInputSide {
    enum Slot : int {
        kDeactivate = 1,
        kLeftDown = 2,
        kLeftUp = 3,
        kMouseMove = 4,
        kRightDown = 5,
        kRightUp = 6,
        kLeftDouble = 7,
        kRightDouble = 8,
        kKeyDown = 9,
        kChar = 10,
        kKeyUp = 11,
        kWheel = 12,
    };
    // Slot 1.
    void Deactivate() {
        using Fn = void(__thiscall*)(UiInputSide*);
        reinterpret_cast<Fn>(vtable_[kDeactivate])(this);
    }
    // Slots 2-11: (buttons, x, y) or (key, repeat, flags). Returns the handler's EAX.
    int Event(int slot, int a, int b, int c) {
        using Fn = int(__thiscall*)(UiInputSide*, int, int, int);
        return reinterpret_cast<Fn>(vtable_[slot])(this, a, b, c);
    }
    // 005E2860 / 005E2880 (slots 7 / 8 of the base input side 007401D0 and most screens;
    // ui_input_defaults.cpp; RET 0xC): a double click is handled as a single press, slot 2 / 5 with
    // the same arguments; the handler's EAX is returned.
    int LeftDoubleAsDown(int buttons, int x, int y);
    int RightDoubleAsDown(int buttons, int x, int y);
    // 005E28B0 (slot 12 of the base input side 007401D0 and of every window without a wheel
    // handler; RET 0x10; ui_small_members.cpp): the wheel is not used, 0.
    int WheelIgnored(int flags, int delta, int x, int y);
    // 005EACD0 and 1 more (slot 0, the deleting destructor; ui_window_delete.cpp). The base vtable
    // stored back and the memory freed when bit 0 of `flags` is set; returns this.
    UiInputSide* InputSideDelete(unsigned flags);
    // 005EA850 (slot 0, the deleting destructor; ui_window_delete.cpp). The base vtable stored back
    // and the memory freed when bit 0 of `flags` is set; returns this.
    UiInputSide* InputSideBaseDelete(unsigned flags);
    // Slot 12. The handlers return 1.
    int Wheel(int flags, int delta, int x, int y) {
        using Fn = int(__thiscall*)(UiInputSide*, int, int, int, int);
        return reinterpret_cast<Fn>(vtable_[kWheel])(this, flags, delta, x, y);
    }

    void** vtable_;
    void* owner_;                    // +0x04 the interface for a window's own side
    bool handled_;                   // +0x08 set by a handler that used the event
};
static_assert(offsetof(UiInputSide, handled_) == 0x08);

// Base of the interface windows (constructor 005FE700, vtables 00740F60 and 00740F2C at +0x94).
// It derives from the engine's transform object, which is kept opaque here.
class CUiWindow {
public:
    // 005FFCC0. Index of the first region containing the screen point (x, y), or -1.
    int RegionAt(int x, int y) const;
    // 005FFD30. Whether the screen point (x, y) lies in the window rectangle.
    BOOL ContainsPoint(int x, int y) const;
    // 005FEBF0. Updates every region's tooltip with the current mouse position (argument unused).
    void UpdateRegionTips(int unused);

    // 006002A0. Sets the enabled flag; with `notify` also calls virtual slot 19 with it.
    void SetEnabled(int enabled, int notify);

    // 005FEDD0. Appends an object and takes a reference (null is ignored).
    void AddObject(CEngineObject* object);
    // 005FF110 / 005FF180. Releases an object and removes it from the list.
    void RemoveObject(CEngineObject* object);
    void RemoveObjectAt(int index);
    // 005FF1E0. Every referenced object released (slot 4), the list emptied (its storage freed).
    void ClearObjects();

    // 005FF700 / 005FF770 / 005FF7D0. The second object list (+0x108).
    BOOL RemoveSceneObject(CEngineObject* object);
    // 005FF460 (RET 8). Replaces scene object `object` (released) at its place in the list with
    // object `name` of the model manager (007AF5F4), given the window's texture 0 and taking the
    // old one's position, +0xCC/+0xD0, orientation, texture and scale; the new one or null.
    CEngineObject* ReplaceSceneObject(CEngineObject* object, const char* name);
    void RemoveSceneObjectAt(int index);
    void ClearSceneObjects();

    // 005FFA90 / 005FFBF0 / 005FFC50. Surfaces the window owns.
    CUiSurface* AddSurface(CUiSurface* surface);   // returns `surface`
    void RemoveSurfaceAt(int index);
    void ClearSurfaces();

    // 005FF830 / 005FF9C0 / 005FFA20. Regions (AddRegion returns the new, empty region).
    CUiRegion* AddRegion();
    void RemoveRegionAt(int index);
    void ClearRegions();

    // 005FFD80 / 005FFF40. 3D model previews.
    CUiModel* AddModel();
    void ClearModels();
    // 005FE930. Destructor body: releases the three objects at +0xEC..+0xF4, the scene objects,
    // surfaces, regions, referenced objects and models (in that order), clears +0x08, then the
    // engine object's base destructor (ui_window.cpp).
    void DestroyBase();
    // 005FE910 and 11 more (slot 0, the deleting destructor; ui_window_delete.cpp). DestroyBase,
    // then the memory freed when bit 0 of `flags` is set; returns this.
    CUiWindow* UiWindowDelete(unsigned flags);
    // 005FE700 (RET 4). Constructor: the engine object base, an empty window of `ui` (arrays, the
    // rectangle and +0xA0..+0xD7 zeroed, rotation from 007C7E58), two materials of the manager
    // 007AF740 (+0xF0 white, +0xEC black; one reference each), enabled (+0xE8 = 1, then slot 19
    // with 1). Returns this (ui_window.cpp).
    CUiWindow* Construct(CInterface3D* ui);

    void** vtable_;
    std::uint8_t unknown_004[0x94 - 0x004];   // CTransformObject members and more
    // The input side starts at +0x94: {vtable, interface, handled flag}. Its handlers get `this`
    // = window + 0x94. Slots: 2 button down, 3 button up, 4 mouse move, 7 double click, 9 key
    // down, 12 wheel; each takes (buttons or key, x, y) in screen pixels.
    void* InputSide() { return &inputVtable_; }
    UiInputSide* Input() { return reinterpret_cast<UiInputSide*>(&inputVtable_); }
    // Takes the mouse for the next button-up (what every button-down handler does).
    void CaptureMouse() { interface_->mouseCapture_ = InputSide(); }
    // Button-up handlers act only when they took the mouse; they release it first.
    bool ReleaseMouse() {
        if (interface_->mouseCapture_ != InputSide()) return false;
        interface_->mouseCapture_ = nullptr;
        return true;
    }
    // Slot 33 (+0x84): a quest variable changed (CInterface3D::OnQuestVariable).
    void OnQuestVariable(const char* name, float value) {
        using Fn = void(__thiscall*)(CUiWindow*, const char*, float);
        reinterpret_cast<Fn>(vtable_[33])(this, name, value);
    }
    // The interface's news slots (CInterface3D::OnItemsPicked .. SaveWindowState; the battle
    // screen's implementations are in ui_camp.h): 28 items picked up, 29 / 30 a unit joined / left,
    // 31 the game ended, 32 a unit speaks, 36 the players changed, 37 an exchange ended, 40 the
    // window's part of a save game.
    void VirtualItemsPicked(void* items, int money, int crippled) {
        using Fn = void(__thiscall*)(CUiWindow*, void*, int, int);
        reinterpret_cast<Fn>(vtable_[28])(this, items, money, crippled);
    }
    void VirtualUnitJoined(void* unit) {
        using Fn = void(__thiscall*)(CUiWindow*, void*);
        reinterpret_cast<Fn>(vtable_[29])(this, unit);
    }
    void VirtualUnitLeft(void* unit) {
        using Fn = void(__thiscall*)(CUiWindow*, void*);
        reinterpret_cast<Fn>(vtable_[30])(this, unit);
    }
    void VirtualGameOver(int won) {
        using Fn = void(__thiscall*)(CUiWindow*, int);
        reinterpret_cast<Fn>(vtable_[31])(this, won);
    }
    void VirtualSpeech(int id, unsigned event, char* extra) {
        using Fn = void(__thiscall*)(CUiWindow*, int, unsigned, char*);
        reinterpret_cast<Fn>(vtable_[32])(this, id, event, extra);
    }
    void VirtualPlayersChanged(int added) {
        using Fn = void(__thiscall*)(CUiWindow*, int);
        reinterpret_cast<Fn>(vtable_[36])(this, added);
    }
    void VirtualExchangeDone(void* partyB, void* partyA, void* partyC) {
        using Fn = void(__thiscall*)(CUiWindow*, void*, void*, void*);
        reinterpret_cast<Fn>(vtable_[37])(this, partyB, partyA, partyC);
    }
    void VirtualSaveState() { CallVirtual(40); }
    // Slot 26 (+0x68): the window is closed (taken off the screen, before it is deleted).
    void CloseWindow() {
        using Fn = void(__thiscall*)(CUiWindow*);
        reinterpret_cast<Fn>(vtable_[26])(this);
    }
    // Slot 0: the deleting destructor (with 1: the memory is freed).
    void DeleteWindow() {
        using Fn = void(__thiscall*)(CUiWindow*, int);
        reinterpret_cast<Fn>(vtable_[0])(this, 1);
    }
    // The window's own mouse move handler (input slot 4), through its input vtable.
    void InputMouseMove(int buttons, int x, int y) {
        using Handler = void(__thiscall*)(void*, int, int, int);
        reinterpret_cast<Handler>(inputVtable_[4])(InputSide(), buttons, x, y);
    }
    // 005FEB90 (RET 4; ui_window.cpp). Base drawing (virtual slot 20): the scene objects through
    // the sprite draw (005E6D90), then the models (005FC2F0); returns 1.
    BOOL DrawBase(int pass);
    // 005EC300 (RET 0x14; the camp and the multiplayer hero screen). Draws `model` (a hero) as a
    // preview: clears the depth buffer (all of it, then the preview's screen rectangles), turns it
    // by `turn` radians about y and places it by `depth` (`wide`'s low byte: six rectangles of the
    // camp layout, else one), draws it lit through `pass` and puts its transform back
    // (ui_hero_preview.cpp).
    void DrawHeroPreview(void* pass, std::uint8_t* model, float turn, float depth, int wide);
    // 005FFFB0 (RET 0x28). Writes image "<name>.mmp" (decoded as imageWidth x imageHeight by the
    // device's decoder) into `texture`'s 256-wide 1:5:5:5 surface: its top-left width x height at
    // (x, y), or white there when there is no such image. Returns whether the image exists (0 also
    // when the lock fails and the error report returns 0). ui_item_view.cpp.
    int CopyImage(CEngineObject* texture, const char* name, int imageWidth, int imageHeight, int width, int height,
                  int x, int y, int unused1, int unused2);
    // 005FEF20 (RET 0xC; ui_window.cpp). The texture manager's texture `name` of `kind` (found or
    // made, 00691D00), loaded (006922A0, or from `directory` with 006926F0) and appended to the
    // referenced objects with one more reference; null when either step fails (a texture made but
    // not loaded is kept by the manager, as in the original).
    CEngineObject* LoadTexture(const char* name, int kind, const char* directory);
    // 005FF0B0 (RET 0xC; ui_window.cpp). A new CPU-filled texture (00691D80) appended to the
    // referenced objects with one more reference and flag 0x40000; null when none is made.
    CEngineObject* CreateTexture(int width, int height, int kind);
    // 006002C0 (RET 4; virtual slot 19 of the plain windows, ui_window.cpp). `show` 0: every
    // surface keeps its contents in memory and lets its DirectDraw surface go (005FD7A0 with 0);
    // otherwise the shown surfaces (flag 4) get theirs back (005FD7A0 with `show`).
    void ShowSurfaces(int show);
    // 005FF3C0 (RET 4; ui_window.cpp). Object `name` of the model manager (007AF5F4; null: a new
    // interface sprite, CUiMesh) added to the scene objects (once: one reference taken and the
    // object attached to the window), given the black material and the first referenced object
    // (or null) as its texture; returns it, or null when the manager makes none.
    CUiMesh* AddSprite(const char* name);
    // 005FECC0 (ui_window.cpp). Base slot 22 (redraw): every shown surface blits itself (slot 0).
    void RedrawBase();
    // 005FED00 (ui_window.cpp). Base slot 23: each region's tooltip follows the mouse (as
    // UpdateRegionTips) and redraws itself (its slot 22), region by region.
    void RedrawTips();
    // Slots 22, 23 and 24 (+0x58, +0x5C, +0x60) through the vtable: redraw, tooltips, update.
    // Slot 20 (+0x50): the window's per-frame draw in the renderer's pass (returns 1).
    BOOL VirtualDraw(void* pass) {
        using Fn = BOOL(__thiscall*)(CUiWindow*, void*);
        return reinterpret_cast<Fn>(vtable_[20])(this, pass);
    }
    void VirtualRedraw() { CallVirtual(22); }
    void VirtualRedrawTips() { CallVirtual(23); }
    void VirtualUpdate() { CallVirtual(24); }
    // Slot 25 (+0x64) of the item panels and strips: the item under the screen point (x, y), or
    // null; a panel may set a help topic instead.
    std::uint8_t* VirtualItemAt(int x, int y, int* topic) {
        using Fn = std::uint8_t*(__thiscall*)(CUiWindow*, int, int, int*);
        return reinterpret_cast<Fn>(vtable_[25])(this, x, y, topic);
    }
    // Slot 19 (+0x4C): show or hide the window's surfaces (and a container's children).
    void VirtualShow(int show) {
        using Fn = void(__thiscall*)(CUiWindow*, int);
        reinterpret_cast<Fn>(vtable_[19])(this, show);
    }
    void CallVirtual(int slot) {
        using Fn = void(__thiscall*)(CUiWindow*);
        reinterpret_cast<Fn>(vtable_[slot])(this);
    }
    // 005EE400 (slot 25 of the panels without items of their own: the description panel 007404DC,
    // the blank item panel 00741B58, 00741A1C, the trade panel 007417A8 and the stats panel
    // 00741ABC; RET 0xC; ui_small_members.cpp). Nothing under the point: `topic` 0, null.
    void* NoItemAt(int x, int y, int* topic);
    // Scene object `index` as the interface sprite it is.
    CUiMesh* SceneMesh(int index) const { return reinterpret_cast<CUiMesh*>(sceneObjects_.data[index]); }

    void** inputVtable_;                      // +0x94 secondary vtable (input handlers)
    CInterface3D* interface_;                 // +0x98
    bool inputHandled_;                       // +0x9C set by a handler that used the event
    std::uint8_t unknown_09D[0xD8 - 0x09D];
    RECT rect_;                               // +0xD8 layout coordinates
    int enabled_;                             // +0xE8
    CEngineObject* blackMaterial_;            // +0xEC given to every sprite (AddSprite)
    CEngineObject* whiteMaterial_;            // +0xF0
    CEngineObject* unknown_0F4;               // +0xF4 released by the destructor
    UiPtrArray<CEngineObject> objects_;       // +0xF8 referenced objects
    UiPtrArray<CEngineObject> sceneObjects_;  // +0x108
    UiPtrArray<CUiSurface> surfaces_;         // +0x118
    UiPtrArray<CUiRegion> regions_;           // +0x128
    UiPtrArray<CUiModel> models_;             // +0x138
};
static_assert(offsetof(CUiWindow, inputVtable_) == 0x94);
static_assert(offsetof(CUiWindow, interface_) == 0x98);
static_assert(offsetof(CUiWindow, inputHandled_) == 0x9C);
static_assert(sizeof(CUiWindow) == 0x148);
static_assert(offsetof(CUiWindow, rect_) == 0xD8);
static_assert(offsetof(CUiWindow, enabled_) == 0xE8);
static_assert(offsetof(CUiWindow, blackMaterial_) == 0xEC);
static_assert(offsetof(CUiWindow, objects_) == 0xF8);
static_assert(offsetof(CUiWindow, regions_) == 0x128);
static_assert(offsetof(CUiWindow, models_) == 0x138);

// The tooltip attached to a region (0x184 bytes, vtables 00742F64 / 00742F30, constructor
// 0065E320; ui_tooltip.cpp, A-H20). Its surface is made while it is shown (0065E580).
struct CUiTooltip : CUiWindow {
    // Slot 0: the deleting destructor (with 1: the memory is freed).
    void Delete() {
        using Fn = void(__thiscall*)(CUiTooltip*, int);
        reinterpret_cast<Fn>(vtable_[0])(this, 1);
    }
    // 0065E320 (RET 0x14; ui_tooltip.cpp). Constructor: the window, no lines, idle, of `region`;
    // a non-empty `text` is split at newlines into the lines and measured (Measure). Returns this.
    CUiTooltip* Construct(CInterface3D* ui, CUiRegion* region, const char* text, int flag, int extra);
    // 0065E4F0 (ui_tooltip.cpp). Destructor body: the lines (006FE958), then the window's.
    void DestroyBody();
    // 0065E4D0 (RET 4; ui_control_delete.cpp). Slot 0, the deleting destructor: DestroyBody, then the
    // memory freed when bit 0 of `flags` is set. Returns this.
    CUiTooltip* TooltipDelete(unsigned flags);
    // 0065E540 (RET 4; ui_tooltip.cpp). A non-empty `text` replaces the lines and they are drawn
    // (the size is not measured again); null or empty: nothing.
    void SetText(const char* text);
    // 0065E880 (ui_tooltip.cpp). Draws the lines into surface 0 (when there is one): filled with
    // 0x1C2B, framed in 0x31B3FF, then line by line in font 0 from (6, 4), lineHeight_ apart, the
    // first flag_ lines in 0x31B3FF and the others in 0xA7D7E4; the first line followed by
    // "  ( <key> )" when keyName_ is set.
    void DrawLines();
    // 0065E580 (RET 8; Rebirth-patched: the vanilla body is OnMouseMoveVanilla in ui_tooltip.cpp
    // and callers use this forwarder, rule 9). One step of the showing, with the mouse at (x, y)
    // in layout coordinates; the region has set the state to 1 (mouse inside) or 3 (left) before.
    // State 1: no lines or a zero delay (setting 20) gives 0; the timer runs up to the delay, then
    // the key of command extra_ is looked up and measured, the state becomes 2 and the surface is
    // made at the mouse + the cursor offset, kept on the 800x600 layout. State 2 draws and becomes
    // 4 (the appearing time is 0). State 3 draws while the timer is positive, then drops the
    // sprites and the surface and becomes 0. States 0 and 4: nothing.
    void OnMouseMove(int x, int y);
    void OnMouseMoveVanilla(int x, int y);
    // 0065EA50 (window slot 20; ui_menus.cpp). Draws only while shown; returns 1 otherwise.
    BOOL Draw(int pass);
    // 0065EA80 (slot 22). Redraws (005FECC0, the window's base slot 22) only while shown.
    void Redraw();
    bool Shown() const { return state_ != 1 && state_ != 0; }

    mfc::CStringArray lines_;        // +0x148 the text's lines (none for an empty text)
    int lineHeight_;                 // +0x15C the last line's height: the rows' step
    int keyWidth_;                   // +0x160 the first line with the key name, + 12 (0065E580)
    int flag_;                       // +0x164 the constructor's fourth argument: the title lines (1 almost everywhere)
    int extra_;                      // +0x168 its fifth: a command whose key is shown (0065E580)
    const char* keyName_;            // +0x16C that key's name, or null
    CUiRegion* region_;              // +0x170 the region it belongs to
    int width_;                      // +0x174 widest line + 12 (a text only)
    int height_;                     // +0x178 the lines' heights + 8
    float timer_;                    // +0x17C seconds in the current state (0065E580)
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
// hero equipment, quick strip, trade and repair grids, constructor pages and the blank panel
// (vtables 00741B58 / 00741B24, constructor 0060E210). Their input sides share the left-button
// handler 005EE310; slot 25 (VirtualItemAt) gives the item or help topic under the mouse.
class CUiItemPanel : public CUiWindow {
public:
    // 005EE310 (input slot 2, RET 0xC; ui_item_panel_input.cpp). The item under (x, y) goes to the
    // owner's slot 42 with mode_ (the camp's MoveItem); without one, a help topic the panel named
    // goes to the owner's slot 44 (the camp: the strip tabs for it). The event is not marked used.
    void OnLeftDown(int buttons, int x, int y);
    // 0060E210 (RET 8; ui_item_panel_input.cpp). The blank panel's constructor (the camp's): the
    // window base, vtables 00741B58 / 00741B24, owner_ = `owner` (mode_ left). Returns this.
    CUiItemPanel* ConstructBlank(CInterface3D* ui, CUiWindow* owner);

    // The owner's slots the panels call (the camp 0074091C, the hero screen 0074214C).
    // Slot 42 (+0xA8): take `item` with the panel's action mode.
    void OwnerTakeItem(void* item) {
        using Fn = void(__thiscall*)(CUiWindow*, void*, int);
        reinterpret_cast<Fn>(owner_->vtable_[42])(owner_, item, mode_);
    }
    // Slot 44 (+0xB0): help topic `topic` clicked.
    void OwnerTopicClicked(int topic) {
        using Fn = void(__thiscall*)(CUiWindow*, int, int);
        reinterpret_cast<Fn>(owner_->vtable_[44])(owner_, topic, mode_);
    }

    int mode_;                       // +0x148 the action mode handed to the owner (ItemCell::mode)
    CUiWindow* owner_;               // +0x14C the item screen
};
static_assert(offsetof(CUiItemPanel, mode_) == 0x148);
static_assert(offsetof(CUiItemPanel, owner_) == 0x14C);

// Set when a screen is activated (005F71C0 / 005F7350), cleared when every window is closed; the
// main loop runs its frames only while it is set (00784750; app_frame.cpp's Running).
inline bool& InterfaceHasScreen() { return *reinterpret_cast<bool*>(0x00784750); }

// The client world (CWorldClient, world.h).
inline class CWorldClient* UiClientWorld() { return reinterpret_cast<class CWorldClient*>(0x007AF5B8); }
// The client glue object the interface asks for units (007AFEA5; its methods do not read it).
inline void* UiClientGlue() { return reinterpret_cast<void*>(0x007AFEA5); }
// Option 17 of the options window (0079BE74): the numbers flying up from units hit are shown.
inline int& ShowHitNumbersOption() { return *reinterpret_cast<int*>(0x0079BE74); }
// The debug information panel (string_map.cpp; the console's "debuginfo" rectangle).
inline void* DebugInfoPanel() { return reinterpret_cast<void*>(0x0079B930); }
// The game's data path (CString 007AF460; 00578DB0 returns its address).
inline const mfc::CString& GameDataPath() { return *reinterpret_cast<const mfc::CString*>(0x007AF460); }

// The texture manager (CTextureManager, texture.h) the windows load their textures from.
inline void* UiTextureManager() { return *reinterpret_cast<void* const*>(0x007AF744); }
// The model manager (007AF5F4) the windows take their sprites and item models from.
inline void* UiModelManager() { return *reinterpret_cast<void* const*>(0x007AF5F4); }

// The in-game key bindings (config\keyboard.ini, read by 00462C10): the action of each scan code
// (0x200 entries, the WM_KEYDOWN flags & 0x1FF).
inline const signed char* KeyActions() { return reinterpret_cast<const signed char*>(0x0079BF70); }
// The same table for writing (the options window's OK stores its copy there).
inline signed char* KeyActionTable() { return reinterpret_cast<signed char*>(0x0079BF70); }
constexpr int kKeyActionCount = 0x200;

// 005F2EA0 / 005F2EE0. Bounds-checked records of the spell and spell-modifier databases, as the
// spell book reads them (stride 0x84 and 0x24; see item_db.h).
struct UiRecordTable : RecordTable {
    char* SpellAt(std::uint32_t index) const;
    char* ModifierAt(std::uint32_t index) const;
};
static_assert(sizeof(UiRecordTable) == 8);

// --- Battle screen and its HUD panels (00630000-00660000; ui_hud.cpp) -----------------------
//
// The battle screen (set up by 00635250) owns a row of panel windows, each a CUiWindow with its
// own vtables; the screen forwards mouse events to the panel whose rectangle holds the point.
// Panel handlers receive the panel's input side (window + 0x94) as `this`; the rewrites are
// members of the panel classes, registered through small shims that convert the pointer.
// Coordinates below are the 800x600 layout of the original (Rebirth moves some panels).

class CUnit;
class CItem;
class CHudQuickItems;
class CHudSpellColumn;
class CHudActionColumn;
class CHudWeaponBar;
class CHudPortraits;

// An MFC CDWordArray. Its SetSize is the instantiation's (0059C660, not rewritten; `growBy` -1
// keeps the step), through UiDwordsSetSize (ui_battle_setup.cpp).
using UiDwordArray = mfc::CArray<std::uint32_t>;
// The vtable of the game's CArray<CUnit*> (007402AC; SetSize 005EBDB0, PartyArraySetSizeOriginal):
// the battle screen's party, the order unit lists built on the stack.
constexpr std::uintptr_t kUnitPtrArrayVtable = std::uintptr_t(0x007402AC);
void __fastcall UiDwordsSetSize(UiDwordArray* array, void*, int size, int growBy);

// The interface's renderer (CInterface3D +0x08), only as the HUD uses it.
struct UiD3DDevice {
    void** vtable_;   // IDirect3DDevice7: slot 20 SetRenderState, 21 GetRenderState
};
struct UiRenderer {
    // 004E7840 / 004E7A40 (not rewritten). Flush the two batched vertex buffers.
    void FlushBatchA();
    void FlushBatchB();

    std::uint8_t unknown_00[0x08];
    long result_;                    // +0x08 the last device call's result
    std::uint8_t unknown_0C[0x54 - 0x0C];
    UiD3DDevice* device_;            // +0x54
    std::uint8_t unknown_58[0x90 - 0x58];
    std::uint32_t caps_;             // +0x90 (0x4000 with stencil_: the depth buffer has a stencil)
    std::uint8_t unknown_94[0x9C - 0x94];
    int stencil_;                    // +0x9C
};
static_assert(offsetof(UiRenderer, caps_) == 0x90);
static_assert(offsetof(UiRenderer, stencil_) == 0x9C);
static_assert(offsetof(UiRenderer, device_) == 0x54);

// Direct3D 7 render states the HUD sets around its drawing.
constexpr int kRenderStateAlphaTest = 15;    // D3DRENDERSTATE_ALPHATESTENABLE
constexpr int kRenderStateAlphaRef = 24;     // D3DRENDERSTATE_ALPHAREF
constexpr int kRenderStateSpecular = 29;     // D3DRENDERSTATE_SPECULARENABLE

// The battle screen. Only the members the panels use; created by original code.
class CBattleScreen : public CUiWindow {
public:
    // 00638EF0 (ui_hud.cpp). A click on `unit` (or, without one, at screen point x, y) while
    // party members are selected: by the pending action (+0x17C: 0 select / attack / move, 1 spell
    // on a unit, 2 and 4 spell on a point, 3 quick item, 5 item and 6 spell on an object, 7 follow,
    // 8 order on an object) or the interface's modifier keys; with nobody selected it selects the
    // clicked member. Rebirth's FixSpellSelfCasting #1 drops the Ctrl flag of a spell on a unit.
    void SelectUnit(CUnit* unit, int x, int y);
    // 00639EF0 (RET 0xC; ui_battle_hover.cpp). The mouse is over `unit` (or, without one, at
    // screen point x, y): a live unit with flag 0x400000 goes into the unit panel's hover block
    // (+0x150), otherwise the block is emptied; with party members selected the cursor shows what
    // a click would do (exit 15, the flags +0x18D.. 16+i, attack 1, cast 2 / can't 0x17, use 4,
    // no 0xD, order 0x18), by the pending action +0x17C as SelectUnit reads it.
    void HoverUnit(CUnit* unit, int x, int y);
    // 00636F10 (RET 0x14; ui_hud.cpp). Orders the selected party units to the point (x, y, z):
    // a point on a zone exit whose "z.<zone>" variable is not 1 is remembered (+0x18C, +0x188 the
    // exit); each selected member's portrait nods after a random delay and the local view gets
    // the order (0066A7D0, or 0066A720 when the low byte of `mode` is set) with the low byte of
    // `ctrl`. SelectUnit calls it for a click on the ground (mode 0) or with a modifier (mode 1).
    void OrderAt(float x, float y, float z, int mode, int ctrl);
    // Inlined by the original wherever a HUD button starts or drops an action (the switch at
    // 0063EE29 / 0063DF0C, and 0063FB50's tail): the pending action becomes `mode` on `slot` with
    // `item`, every highlight of the quick item panel (0063CD20 per slot), the spell column and
    // the action column is dropped (each panel's +0x150 -1), and `slot` is highlighted in the
    // panel of that mode (1, 2 the spell column; 3-6 the quick items; 7, 8 the actions; others
    // none). Mode 0 with slot and item -1 cancels (ui_hud_pick.cpp).
    void SetPendingAction(int mode, int slot, int item);

    UiPtrArray<CUiWindow> children_; // +0x148 the container's child windows ([9] the message log)
    std::uint8_t unknown_158[0x16C - 0x158];
    CUnit** party_;                  // +0x16C the player's units shown as portraits
    int partyCount_;                 // +0x170
    std::uint8_t unknown_174[0x17C - 0x174];
    int pendingAction_;              // +0x17C what the next click on the view does (SelectUnit)
    int pendingSlot_;                // +0x180 its spell, quick item or action button, or -1
    int pendingItem_;                // +0x184 the player's item id (mode 6), the action (7, 8), or -1
    // +0x188 / +0x18C: a zone exit OrderAt found under the clicked point (the update asks about
    // leaving while exitPending_ is set). CBattleScreenSetup (ui_camp.h) calls them savedSelection_
    // and savedFlag_.
    int pendingExit_;                // +0x188
    std::uint8_t exitPending_;       // +0x18C
    // +0x18D the order modifier keys held (key actions 0x11..0x16; CBattleScreenSetup::orderKeys_).
    std::uint8_t orderKeys_[6];
    std::uint8_t unknown_193;
    CHudWeaponBar* weaponBar_;       // +0x194
    CHudQuickItems* quickItems_;     // +0x198
    CHudSpellColumn* spellColumn_;   // +0x19C
    CHudActionColumn* actionColumn_; // +0x1A0
    CHudPortraits* portraits_;       // +0x1A4
    std::uint8_t unknown_1A8[0x220 - 0x1A8];
    // +0x220 the key a player's item gives (its slot 21) when it starts mode 6; SelectUnit hands
    // it to the target's AcceptsKey (map object slot 42).
    LockKey itemUseKey_;
    int portraitMode_;               // +0x22C 0: normal; 1, 2: the next portrait click picks a
                                     //        player for a server command (4, 5)

    // Slot 32 (+0x80; 006366F0, CBattleScreenSetup::OnUnitSpeech, RET 0xC): the unit with `id`
    // speaks about `event`.
    void VirtualUnitSpeech(int id, unsigned event, char* extra) {
        using Fn = void(__thiscall*)(CBattleScreen*, int, unsigned, char*);
        reinterpret_cast<Fn>(vtable_[32])(this, id, event, extra);
    }
};
static_assert(offsetof(CBattleScreen, pendingExit_) == 0x188);
static_assert(offsetof(CBattleScreen, exitPending_) == 0x18C);
static_assert(offsetof(CBattleScreen, orderKeys_) == 0x18D);
static_assert(offsetof(CBattleScreen, weaponBar_) == 0x194);
static_assert(offsetof(CBattleScreen, portraits_) == 0x1A4);
static_assert(offsetof(CBattleScreen, itemUseKey_) == 0x220);
static_assert(offsetof(CBattleScreen, party_) == 0x16C);
static_assert(offsetof(CBattleScreen, pendingAction_) == 0x17C);
static_assert(offsetof(CBattleScreen, pendingSlot_) == 0x180);
static_assert(offsetof(CBattleScreen, pendingItem_) == 0x184);
static_assert(offsetof(CBattleScreen, quickItems_) == 0x198);
static_assert(offsetof(CBattleScreen, actionColumn_) == 0x1A0);
static_assert(offsetof(CBattleScreen, portraitMode_) == 0x22C);

// Weapon bar (constructor 0063BAB0, vtables 0074279C / 00742768): four 40x80 slots at
// (80..240, 520..600).
class CHudWeaponBar : public CUiWindow {
public:
    void OnMouseDown(int buttons, int x, int y);   // 0063C6D0
    void OnMouseUp(int buttons, int x, int y);     // 0063C710: selects a weapon
    void OnMouseMove(int buttons, int x, int y);   // 0063C7D0
    // 0063C600 (slot 24, every frame). Follows the unit's weapon (+0x270) with the highlight.
    void Update();
    // 0063C4F0 (slot 20). Draws with alpha test and specular on.
    BOOL Draw(int pass);
    // 0063C050 (Rebirth-patched; rewritten callers go through this forwarder). Highlights slot
    // `index` (or not).
    void Highlight(int index, int on);
    // The vanilla bodies of the bar's Rebirth-patched functions (ui_hud_build.cpp; hooked in the
    // vanilla launch only, the battle screen and SetUnit reach them through their forwarders).
    // 0063BAB0 (RET 8). Constructor: the window base, the labels, the vtables, the rectangle
    // (80, 520, 240, 600), four 40-wide regions with empty tips, no sprites, nothing selected, no
    // unit, and the labels "infoitem_15", "_20", "_10", "_5". Returns this.
    CHudWeaponBar* Construct(CInterface3D* ui, CBattleScreen* owner);
    // 0063C050 (RET 8). Slot `index` (0 .. count_ - 1) highlighted: its sprite at (100 + 40 *
    // index, 540) depth 12, scale 0.65, colour 0xFF282828, its region from y 495; or not: at y 560,
    // scale 1.2 ^ count_ and depth 24 times that, colour 0xFF000000, the region from y 520.
    void HighlightVanilla(int index, int on);
    // 0063BCC0 (RET 8). The next slot's sprite: `image` copied into the bar's texture (42 x 128,
    // at column count_ / 2, row count_ % 2), the model `model` turned to face the screen, or a
    // plain rectangle when there is no such model; its texture offsets, plain white; counted and
    // shown not highlighted (Highlight).
    void AddWeapon(const char* model, const char* image);
    // 0063C180 (RET 4; ui_hud_tips.cpp). Shows `unit`'s weapons: sprites dropped, the four
    // regions' tips cleared; per weapon a sprite (0063BCC0, Rebirth-patched) and the tip of its
    // region (id 0x26 + slot): the name, damage range and type, the prototype's +0x6C and the
    // weapon's +0x58 and, for a weapon with a spell, the spell's name; then the unit's +0x270
    // highlighted.
    void SetUnit(CUnit* unit);

    // 0063BC70 (ui_hud_destroy.cpp; slot 0 0063BC50 calls it). Destructor body: the labels, then the
    // window base's (005FE930).
    void DestroyBody();
    // 0063BC50 (slot 0, the deleting destructor; ui_window_delete.cpp). DestroyBody, then the
    // memory freed when bit 0 of `flags` is set; returns this.
    CHudWeaponBar* HudWeaponBarDelete(unsigned flags);

    CBattleScreen* owner_;           // +0x148
    int count_;                      // +0x14C slots in use
    int selected_;                   // +0x150
    CUnit* unit_;                    // +0x154
    void* labelsVtable_;             // +0x158 CStringArray: the tooltip's labels
    char** labels_;                  // +0x15C
};
static_assert(offsetof(CHudWeaponBar, unit_) == 0x154);
static_assert(offsetof(CHudWeaponBar, labels_) == 0x15C);

// Quick items (constructor 0063C800, vtables 0074283C / 00742808): 2 rows of four 50x50 slots
// at (520..720, 500..600).
class CHudQuickItems : public CUiWindow {
public:
    void OnMouseDown(int buttons, int x, int y);   // 0063DAA0
    void OnDoubleClick(int buttons, int x, int y); // 0063DAE0
    void OnMouseMove(int buttons, int x, int y);   // 0063E270
    // 0063DB20 (ui_hud.cpp). Button release: a unit's quick item (bottom row) is picked (0063DD70)
    // or, with key +0x7F, used at once (0063E020); a player's item (top row) starts mode 6, or
    // cancels when it is the highlighted one.
    void OnMouseUp(int buttons, int x, int y);
    BOOL Draw(int pass);                           // 0063CDC0
    // 0063D9D0 (slot 24, every frame). Fills the tooltip of each quick item slot that shows one.
    void Update();
    // 0063CEE0 (RET 8; ui_hud_tips.cpp). The tooltip text of quick item `index` into `text` (a
    // CString): the item's name and, for a wand (category 5) or a scroll (8) with a spell, its
    // numbers after labels_.
    void ItemTip(unsigned index, void* text);
    // The same through the original's address (so the verify wrapper sees the call).
    void ItemTipAt(unsigned index, void* text);
    // 0063DD70 (RET 4; ui_hud_pick.cpp). Picks the unit's quick item `slot` (a click on it or its
    // key): without a unit nothing; without an item there the pending action is dropped (no
    // sound); otherwise the click sound, then a click on the highlighted slot drops the pending
    // action, and a wand (category 5) or scroll (8) becomes the pending action on `slot`: 3 when
    // its spell has flag 0x10000000 (on a unit), 4 (at a point), 0 without a spell. Other
    // categories change nothing.
    void Pick(int slot);
    // 0063E020 (RET 4; ui_hud_pick.cpp). The unit's quick item `slot` used at once (a release with
    // key flag +0x7F, or the key handler's Ctrl / Alt): as ClickAlternate with pending action 3
    // and quick 1, for a wand (category 5) or scroll (8) with a spell; nothing without a unit or
    // an item there.
    void UseNow(int slot);
    void UseNowAt(int slot);   // through the original's address
    // 0063CD20 (RET 8; ui_hud_highlight.cpp). Highlights the sprite shown in region `slot` when
    // it shows one (bottom row, slots 0-3: scale 0.65 / 0.55; top row 1.0 / 0.85; colour
    // 0xFF404040 / 0xFF000000; with `on` also selected_ = slot).
    void Highlight(int slot, int on);
    // The same through the original's address (so the verify wrapper sees the call).
    void HighlightAt(int slot, int on);
    // Item of the region at `index` (-1: empty). The original indexes this with the result of
    // RegionAt unchecked, so index -1 reads the dword before the array (+0x168).
    int RegionItem(int index) const {
        return reinterpret_cast<const int*>(reinterpret_cast<const std::uint8_t*>(this) + 0x16C)[index];
    }

    // The vanilla bodies of the panel's Rebirth-patched functions (ui_hud_build.cpp; hooked in the
    // vanilla launch only). 0063C800 (RET 8): the constructor: the window base, the entries array,
    // the labels, the vtables, the rectangle (520, 500, 720, 600), four 50 x 50 regions along the
    // bottom row from the right (x 670 .. 520) and four along the top row, with empty tips and no
    // items, nothing selected, no unit, and the labels "infoitem_7" .. "_13", "_21", "_22", "_26"
    // .. "_28". Returns this.
    CHudQuickItems* Construct(CInterface3D* ui, CBattleScreen* owner);
    // 0063CAE0 (RET 8). The next sprite (at most eight): `image` copied into the panel's texture
    // (64 x 64 at (128 + 64 * (n / 4), 64 * (n % 4))), the model `model` turned over (pi about x)
    // or a plain rectangle without one; its texture offsets, plain white; counted.
    void AddItem(const char* model, const char* image);
    // 0063D2B0 (RET 4; ui_battle_update.cpp). Shows `unit`'s quick items: the sprites, tooltips and
    // entries are dropped; for each of the unit's four quick items (00531920) a sprite (0063CAE0,
    // Rebirth-patched) in the bottom row, an entry and its tooltip (0063CEE0); then, when the unit
    // is the local player's (00665440), up to four of the player's 0x3009 items in the top row.
    void SetUnit(CUnit* unit);

    // An item shown (12 bytes, MFC CArray at +0x158).
    struct Entry {
        bool own;                    // +0 one of the unit's quick items (else the player's)
        std::uint8_t unknown_1[3];
        int id;                      // +4 the item's +0xC
        CEngineObject* item;         // +8
    };

    // 0063CA70 (ui_hud_destroy.cpp; slot 0 0063CA50 calls it). Destructor body: the labels, the entries
    // array (its vtable 007428A0 written, the storage deleted), then the
    // window base's (005FE930).
    void DestroyBody();
    // 0063CA50 (slot 0, the deleting destructor; ui_window_delete.cpp). DestroyBody, then the
    // memory freed when bit 0 of `flags` is set; returns this.
    CHudQuickItems* HudQuickItemsDelete(unsigned flags);

    CBattleScreen* owner_;           // +0x148
    int unknown_14C;                 // +0x14C sprites added (the shown entries)
    int selected_;                   // +0x150
    CUnit* unit_;                    // +0x154
    void* entriesVtable_;            // +0x158 CArray<Entry>
    Entry* entries_;                 // +0x15C
    int entryCount_;                 // +0x160
    int entryCapacity_;              // +0x164
    int entryGrowBy_;                // +0x168
    int regionItem_[8];              // +0x16C
    void* labelsVtable_;             // +0x18C CStringArray: the tooltip's labels
    char** labels_;                  // +0x190
};
static_assert(offsetof(CHudQuickItems, entries_) == 0x15C);
static_assert(offsetof(CHudQuickItems, labels_) == 0x190);
static_assert(offsetof(CHudQuickItems, regionItem_) == 0x16C);
static_assert(offsetof(CHudQuickItems, selected_) == 0x150);

// Spell column on the right (constructor 0063E2A0, vtables 007428E4 / 007428B0;
// 760..800 x 190..510).
class CHudSpellColumn : public CUiWindow {
public:
    void OnMouseDown(int buttons, int x, int y);   // 0063F1E0
    void OnDoubleClick(int buttons, int x, int y); // 0063F220
    void OnMouseUp(int buttons, int x, int y);     // 0063F260
    void OnMouseMove(int buttons, int x, int y);   // 0063F2D0
    BOOL Draw(int pass);                           // 0063F190
    // 0063EC90 (RET 4; ui_hud_pick.cpp). A click on spell `index` (its button or key): a click
    // on the highlighted one plays the click sound and drops the pending action; otherwise the
    // previous button loses its highlight, `index` gets it, the click sound, and when the unit's
    // spell there has its data (+0x44) the spell becomes the pending action on `index`: 1 with
    // flag 0x10000000 (on a unit), else 2 (at a point). An index out of range does nothing.
    void Click(int index);
    // The same through the original's address (OnMouseUp; so the verify wrapper sees the call).
    void ClickAt(int index);
    // 0063EF60 (RET 4; ui_hud_pick.cpp). Click with key flag +0x7F (or the key handler's Ctrl /
    // Alt): the spell is cast at once. Without a unit or with `index` out of range nothing;
    // otherwise the pending action becomes 1 on `index`, and when the spell there has its data
    // (+0x44) it is used (UseOnUnit / UseAtPoint, quick 0) on the unit itself, or for an attack
    // spell (CSpell::IsOffensive) on the nearest unit the unit sees (none: the "nomagic" sound,
    // the pending action dropped, done). Then the click sound and the pending action dropped.
    void ClickAlternate(int index);
    void ClickAlternateAt(int index);   // through the original's address
    // 0063E750 (RET 8; ui_hud_highlight.cpp). Button `index` highlighted (scale 1.15, colour
    // 0xFF404040) or not (1.0, 0xFF000000); selected_ = index either way.
    void Highlight(int index, int on);
    void HighlightAt(int index, int on);   // through the original's address
    // 0063E7C0 (ui_hud_highlight.cpp). Every button unhighlighted, then selected_ = -1.
    void ClearHighlights();
    // The vanilla bodies of the column's Rebirth-patched functions (ui_hud_build.cpp; hooked in
    // the vanilla launch only). 0063E2A0 (RET 8): the constructor: the window base, the labels,
    // the vtables, the rectangle (760, 190, 800, 510), no sprites or regions, nothing selected, no
    // unit, and the labels "infoitem_7", "_18", "_8" .. "_13", "_21", "_22", "_26" .. "_28".
    // Returns this.
    CHudSpellColumn* Construct(CInterface3D* ui, CBattleScreen* owner);
    // 0063E4B0 (RET 8). The next button: the regions and sprites so far move up one 40-high step,
    // a region and the sprite of `model` (not checked) are added at the bottom (y 470 .. 510) with
    // `image` copied into the column's texture (64 x 64 at (128 + 64 * (n / 4), 64 * (n % 4)));
    // plain white, scale 1, colour 0xFF000000; counted, selected_ = its index.
    void AddSpell(const char* model, const char* image);
    // 0063E820 (RET 4; ui_spell_column.cpp). Shows `unit`'s spells: sprites and regions dropped;
    // for each of the unit's eight spell slots that holds one, a sprite (0063E4B0, Rebirth-patched)
    // and the tooltip of region `index` (tip id 0x1E + index): the spell's name and, when it has
    // its data (+0x44), seven lines of numbers each after a label of labels_ (some numbers
    // replaced by labels when absent: an area for flag 0x10000000, a time under 2 ticks).
    void SetUnit(CUnit* unit);

    // 0063E460 (ui_hud_destroy.cpp; slot 0 0063E440 calls it). Destructor body: the labels, then the
    // window base's (005FE930).
    void DestroyBody();
    // 0063E440 (slot 0, the deleting destructor; ui_window_delete.cpp). DestroyBody, then the
    // memory freed when bit 0 of `flags` is set; returns this.
    CHudSpellColumn* HudSpellColumnDelete(unsigned flags);

    CBattleScreen* owner_;           // +0x148
    int count_;                      // +0x14C sprites added
    int selected_;                   // +0x150
    CUnit* unit_;                    // +0x154
    void* labelsVtable_;             // +0x158 CStringArray: the tooltip's labels
    char** labels_;                  // +0x15C
};
static_assert(offsetof(CHudSpellColumn, count_) == 0x14C);
static_assert(offsetof(CHudSpellColumn, selected_) == 0x150);
static_assert(offsetof(CHudSpellColumn, labels_) == 0x15C);

// Action column on the left (constructor 0063F300, vtables 00742980 / 0074294C; six 40x40
// buttons at 0..40 x 270..510).
class CHudActionColumn : public CUiWindow {
public:
    void OnMouseDown(int buttons, int x, int y);   // 0063FDA0
    void OnMouseUp(int buttons, int x, int y);     // 0063FDE0
    void OnMouseMove(int buttons, int x, int y);   // 0063FE40
    BOOL Draw(int pass);                           // 0063FD50
    // 0063FB50 (RET 4; ui_hud_pick.cpp). Selects action button `index` (a click on it or its
    // key): the highlighted one again plays the click sound and drops the pending action;
    // another moves the highlight to it, plays the click sound and makes its action the pending
    // one on `index`: 8 (an order on an object) for action 0 (science), else 7 (follow), the
    // action in +0x184. An index out of range does nothing.
    void Select(int index);
    // The same through the original's address (OnMouseUp; so the verify wrapper sees the call).
    void SelectAt(int index);
    // 0063F730 (RET 8; ui_hud_highlight.cpp). As CHudSpellColumn::Highlight.
    void Highlight(int index, int on);
    void HighlightAt(int index, int on);   // through the original's address
    // 0063F7A0 (ui_hud_highlight.cpp). As CHudSpellColumn::ClearHighlights.
    void ClearHighlights();
    // The vanilla bodies of the column's Rebirth-patched functions (ui_hud_build.cpp; hooked in
    // the vanilla launch only). 0063F300 (RET 8): the constructor: the window base, the actions
    // array, the labels, the vtables, the rectangle (0, 250, 40, 510), six 40 x 40 regions from
    // the bottom (y 470) up with empty tips, no sprites or actions, nothing selected, no unit, and
    // the label "infoitem_16". Returns this.
    CHudActionColumn* Construct(CInterface3D* ui, CBattleScreen* owner);
    // 0063F500 (RET 0xC). The next button, above the ones so far: the sprite of `model` (not
    // checked) at (20, 490 - 40 * count_) with `image` copied into the column's texture (64 x 64
    // at (64 * (n / 4), 64 * (n % 4))); plain white, scale 1, colour 0xFF000000; `action` appended;
    // counted, selected_ = its index.
    void AddAction(const char* model, const char* image, int action);
    // 0063F800 (RET 8; ui_hud_tips.cpp). Shows `unit`'s actions: sprites and the action array
    // dropped, the six regions' tips cleared; with `extra` (its low byte) first a "skill0011"
    // button (tip 0x290E); with a unit the science button ("skill0009", action 0) whose tip is
    // the "perk Science" text, the unit's science level (+0x408) and the "tip 10511" text.
    void SetUnit(CUnit* unit, int extra);

    // 0063F490 (ui_hud_destroy.cpp; slot 0 0063F470 calls it). Destructor body: the labels, the actions
    // array (its vtable 0073C3D0 written, the storage deleted), then the
    // window base's (005FE930).
    void DestroyBody();
    // 0063F470 (slot 0, the deleting destructor; ui_window_delete.cpp). DestroyBody, then the
    // memory freed when bit 0 of `flags` is set; returns this.
    CHudActionColumn* HudActionColumnDelete(unsigned flags);

    CBattleScreen* owner_;           // +0x148
    int count_;                      // +0x14C buttons added
    int selected_;                   // +0x150
    CUnit* unit_;                    // +0x154
    UiDwordArray actions_;           // +0x158 the buttons' actions (0063F500 adds; 0 science, -1 follow)
    void* labelsVtable_;             // +0x16C CStringArray: the tooltip's labels
    char** labels_;                  // +0x170
};
static_assert(offsetof(CHudActionColumn, count_) == 0x14C);
static_assert(offsetof(CHudActionColumn, selected_) == 0x150);
static_assert(offsetof(CHudActionColumn, actions_) == 0x158);
static_assert(offsetof(CHudActionColumn, labels_) == 0x170);

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
    char* faceName_;                 // +0x40 CString
    int faceState_;                  // +0x44 picture shown (0 normal, 2 hit, 3 low mana, ...)
    int faceMode_;                   // +0x48 1 / 2: a timed picture, 0 none
    float faceTimer_;                // +0x4C
    char* modelPrefix_;              // +0x50 CString: the face model is prefix + build + suffix,
    char* modelSuffix_;              // +0x54 CString  build "th", "me" or "fa"
    float build_;                    // +0x58 the unit's build the model was chosen for
    Quaternion wobbleTarget_;        // +0x5C
    float wobbleSpeed_;              // +0x6C
    bool hasPlayer_;                 // +0x70 a multiplayer player without a unit here (no bars)
    std::uint8_t unknown_71[3];
    int playerId_;                   // +0x74 that player's serial (PlayerMessage +0x30), else -1

    // The three CString members.
    mfc::CString& FaceName() { return *reinterpret_cast<mfc::CString*>(&faceName_); }
    mfc::CString& ModelPrefix() { return *reinterpret_cast<mfc::CString*>(&modelPrefix_); }
    mfc::CString& ModelSuffix() { return *reinterpret_cast<mfc::CString*>(&modelSuffix_); }
};
static_assert(sizeof(HudPortraitSlot) == 0x78);
static_assert(offsetof(HudPortraitSlot, wobbleKind_) == 0x38);
static_assert(offsetof(HudPortraitSlot, wobbleTarget_) == 0x5C);

// A figure prototype (the records of the map at 007AF3A4); only the model name the faces use.
struct FigurePrototypeRecord {
    std::uint8_t unknown_000[0xE8];
    const char* model;               // +0xE8 e.g. "unhuma", "unhufe"
};
static_assert(offsetof(FigurePrototypeRecord, model) == 0xE8);

// Party portraits (constructor 0063FE70, vtables 00742A18 / 007429E4; 250..550 x 440..600).
class CHudPortraits : public CUiWindow {
public:
    void OnMouseDown(int buttons, int x, int y);   // 00641CE0
    void OnMouseUp(int buttons, int x, int y);     // 00641E20: selects the unit
    void OnMouseMove(int buttons, int x, int y);   // 00641EA0: shows the unit / sets the cursor
    // 00641080 (slot 24, every frame): health and mana bars, the hit flash, the face picture and
    // model, the face's wobble and the selection marker (ui_hud_portraits.cpp).
    void Update();
    // 00640670 (RET 8; ui_hud_portrait_setup.cpp). Copies face picture `state` of portrait `index`
    // (the slot's face name plus "a" 1, "c" 2, "b" 3; the plain name when there is no such image)
    // into quarter index % 4 of texture objects_[index / 4 + 1].
    void ShowFace(int index, int state);
    void ShowFaceAt(int index, int state);   // through the original's address
    // 006407E0 (RET 4; ui_hud_portrait_setup.cpp). Rebuilds the portraits: sprites, regions and
    // textures but the first dropped, the selection marker, then one portrait per party unit (face
    // names from its prototype and build, tip the unit's name) and, in a multiplayer client, one per
    // player without a live unit. `wide` goes to AddPortrait.
    void Rebuild(int wide);
    // 00640060 (RET 0x10; ui_hud_portrait_setup.cpp). Appends portrait count_: a slot, the face
    // picture `picture` into a quarter of the current face texture (a new 256x256 one every fourth
    // portrait), the face model `model` (a plain picture sprite without one), the two bars, the
    // frame, the flash and the region; 56 wide (multiplayer: 40, or 56 with the low byte of `wide`
    // for the first). The low byte of `hasPlayer` marks a player's portrait (half tint, no bars).
    void AddPortrait(const char* model, const char* picture, int wide, int hasPlayer);
    void AddPortraitAt(const char* model, const char* picture, int wide, int hasPlayer);
    // 005FF460 (CUiWindow::ReplaceSceneObject, called through its address). Replaces scene
    // object `object` with a model loaded by `name` (keeping its placement); returns the new
    // object or 0.
    CEngineObject* ReplaceSceneObject(CEngineObject* object, const char* name);

    // 0063FE70 (RET 8; ui_screen_constructors.cpp). Constructor: the window over 250..550 x
    // 440..600 (vtables 00742A18 / 007429E4), no portraits, and the selection marker sprite (a
    // 20x20 rectangle at depth 10, scale 80, texels 0x22, 0x96 - 0x36, 0xAA). Returns this.
    CHudPortraits* Construct(CInterface3D* ui, CBattleScreen* owner);
    // 0063FF70 (ui_screen_constructors.cpp; slot 0 0063FF50 calls it). Destructor body: each
    // slot's three strings (suffix, prefix, face name), the slots' storage back to the node
    // allocator, then the window base's (005FE930).
    void DestroyBody();
    // 0063FF50 (slot 0, the deleting destructor; ui_window_delete.cpp). DestroyBody, then the
    // memory freed when bit 0 of `flags` is set; returns this.
    CHudPortraits* HudPortraitsDelete(unsigned flags);

    CBattleScreen* owner_;           // +0x148
    int count_;                      // +0x14C
    std::uint8_t unknown_150[0x158 - 0x150];
    HudPortraitSlot* slots_;         // +0x158 STLport vector
    HudPortraitSlot* slotsEnd_;      // +0x15C
    HudPortraitSlot* slotsCapacity_; // +0x160
};
static_assert(offsetof(CHudPortraits, slots_) == 0x158);

// A 2D mesh of the interface (the sprite objects 005E70E0 and its siblings set up). Positions
// are kept per quad as four x then four y floats; each vertex has a 6-byte record (the position
// it uses and a frame of the texture); indices are 16 bits. The arrays grow like UiPtrArray.
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
    std::int16_t unknown_2;
    std::int16_t frame;              // +4 texture frame (the minimap's marker kinds)
};
static_assert(sizeof(UiMeshVertex) == 6);
class CUiMesh {
public:
    // The array sizes (RET 4; ui_mesh_draw.cpp): each is the aligned array's SetSize(count); the
    // quads (005EA210) then get z 0 and w 1 in every block, the vertices (005EA600) +2 zero in
    // every entry; the indices (005EA4C0) and texture coordinates (005EA380, 8 bytes each) nothing.
    void SetQuadCount(int count);
    void SetVertexCount(int count);
    void SetIndexCount(int count);
    void SetUvCount(int count);
    // The four arrays as the engine's aligned arrays (the fields below are their data and size).
    lib::AlignedArray<UiQuadBlock>& Quads() { return *reinterpret_cast<lib::AlignedArray<UiQuadBlock>*>(&quads_); }
    lib::AlignedArray<UiMeshVertex>& Vertices() { return *reinterpret_cast<lib::AlignedArray<UiMeshVertex>*>(&vertices_); }
    lib::AlignedArray<std::uint16_t>& Indices() { return *reinterpret_cast<lib::AlignedArray<std::uint16_t>*>(&indices_); }
    lib::AlignedArray<UiTexel>& Texels() { return *reinterpret_cast<lib::AlignedArray<UiTexel>*>(&uvs_); }
    // Not rewritten: Commit (005EA150), which hands the arrays to the renderer fields
    // (+0x9C..+0xC8) and returns the index count.
    int Commit();
    // 005E7010 (RET 0xC; ui_mesh_draw.cpp). Where Place put the sprite: the layout point
    // ((x / z / 0.4816 + 1) * 400, y / z / 0.4816 * 400 + 300; each stored as float) and the depth
    // (z, copied).
    void GetPlacement(float* x, float* y, float* depth) const;
    // 005E6FA0 (Rebirth-patched, not rewritten). Puts the sprite at layout point (x, y) at
    // `depth` in front of the interface camera (sets the local position +0x1C).
    void Place(float x, float y, float depth);
    // 005E70E0 / 005E7D60 (Rebirth-patched, not rewritten). Make the sprite a textured
    // rectangle (layout left, top, right, bottom at `depth`; texels u0, v0 .. u1, v1) or a
    // framed one (`border` wide).
    void SetRectangle(float depth, float scale, float left, float top, float right, float bottom, int u0, int v0,
                      int u1, int v1);
    void SetFrame(float depth, float scale, float left, float top, float right, float bottom, float border,
                  int u0, int v0, int u1, int v1);
    // 005E99D0 (Rebirth-patched: through its address; the vanilla body is SetTriangleVanilla).
    void SetTriangle(float depth, float scale, float x0, float y0, float x1, float y1, float x2, float y2, float u0,
                     float v0, float u1, float v1, float u2, float v2);
    // 005E70E0 (RET 0x28; Rebirth-patched, this is the vanilla body, hooked in the vanilla launch
    // only; ui_mesh_shapes.cpp; rewritten callers go through SetRectangle). A rectangle of two
    // triangles: one packet of normals (0, 0, 1), four corners, the texels (u0, v0)-(u1, v1) as
    // texture coordinates (v upwards; both u one less when u0 > u1; half a texel inwards), the
    // quad (+-half width, +-half height) / scale about the origin, position_ the layout centre at
    // `depth` (as Place), the three scales 0.48157 * depth * scale / 400; then Commit.
    void SetRectangleVanilla(float depth, float scale, float left, float top, float right, float bottom, int u0, int v0,
                             int u1, int v1);
    // The other shapes' vanilla bodies (Rebirth-patched like the rectangle; ui_mesh_shapes.cpp).
    // 005E7680 (RET 0x30). A grid of columns x rows cells over the rectangle, each cell a quad
    // showing the whole picture (u0, v0)-(u1, v1): (columns + 1) * (rows + 1) points a cell apart
    // (in units of `scale`), four corners and six indices per cell; position_ the layout's top
    // left corner at `depth`.
    void SetGridVanilla(float depth, float scale, float left, float top, float right, float bottom, int u0, int v0, int u1,
                        int v1, int columns, int rows);
    // 005E7D60 (RET 0x2C). A frame `border` wide inside the rectangle: the outer and the inner
    // rectangle's corners (two packets) joined by four quads, each showing the whole picture;
    // position_ the layout centre at `depth`.
    void SetFrameVanilla(float depth, float scale, float left, float top, float right, float bottom, float border, int u0,
                         int v0, int u1, int v1);
    // 005E99D0 (RET 0x38). A triangle: the first layout point is position_ (at `depth`), the
    // other two are points relative to it in units of `scale`; the texels are floats here.
    void SetTriangleVanilla(float depth, float scale, float x0, float y0, float x1, float y1, float x2, float y2, float u0,
                            float v0, float u1, float v1, float u2, float v2);
    // 005E6FA0 (RET 0xC; Rebirth-patched, its widescreen version replaces it; this is the vanilla
    // body, hooked in the vanilla launch only; ui_mesh_draw.cpp). Place: position_ =
    // ((x * 0.0025 - 1) * 0.4816 * depth, (y * 0.0025 - 0.75) * 0.4816 * depth, depth), the
    // constants read from 0074015C, 0073B84C, 0073C524 and 00740158; `depth` copied as bits.
    void PlaceVanilla(float x, float y, float depth);
    // 005E7060 (RET 4; ui_hud_highlight.cpp). The three scale fields = `scale` (copied as bits;
    // EAX is left holding them, callers do not read it).
    void SetScale(float scale);
    // 005E6D70 (vtable slot 3; ui_small_members.cpp). The material and the texture forgotten (not
    // released), then the figure's own slot 3 (005A6C80), whose EAX is returned.
    int DestroySprite();
    // 005E7090 (RET 4). The y scale alone (scaleMatrix_[5]) = `bits` (copied as bits); the quick
    // items' bar (006462A0) calls it.
    void SetScaleYBits(std::uint32_t bits);
    // 005E70B0. The x scale (scaleMatrix_[0], in ST0).
    float GetScaleX() const;
    // 005E6D90 (RET 4; ui_mesh_draw.cpp). Draws the sprite in `renderer`'s pass when visible;
    // returns 1.
    int Draw(CSceneRenderer* renderer);
    // 005E6C60 (vtable slot 2, Create; ui_mesh_draw.cpp). The figure's Create (005A6C70), then no
    // material or texture, the scale matrix the identity, tint 1, +0x230 zero, the identity
    // orientation (007C7CD8), position (0, 0, 10), both colours 0xFF000000, visible. Returns 1.
    int CreateSprite();

    std::uint8_t unknown_000[0x08];
    std::uint32_t flags_;            // +0x08 engine object flags (0x100: drawn in the plain colour +0xD4)
    std::uint8_t unknown_00C[0x1C - 0x0C];
    Vector3 position_;               // +0x1C (Place)
    std::uint8_t unknown_028[0x34 - 0x28];
    Quaternion orientation_;         // +0x34 (the HUD's face models turn with it)
    std::uint8_t unknown_044[0x54 - 0x44];
    float placement_[16];            // +0x54 row-major 4x4: orientation and position (Draw writes it)
    std::uint8_t unknown_094[0xCC - 0x94];
    float uOffset_;                  // +0xCC added to the texture coordinates (a frame of the
    float vOffset_;                  // +0xD0 sprite sheet: the HUD buttons' states, the bars' colours)
    std::uint32_t plainColour_;      // +0xD4 ARGB (the global map's layers and its pick render)
    std::uint32_t colour_;           // +0xD8 ARGB (the HUD buttons: 0xFF404040 highlighted, 0xFF000000 not)
    std::uint8_t unknown_0DC[0x1C0 - 0xDC];
    UiQuadBlock* quads_;             // +0x1C0
    int quadCount_;                  // +0x1C4
    int quadCapacity_;               // +0x1C8
    int quadGrowBy_;                 // +0x1CC
    std::uint8_t unknown_1D0[0x1E0 - 0x1D0];
    UiMeshVertex* vertices_;         // +0x1E0
    int vertexCount_;                // +0x1E4
    int vertexCapacity_;             // +0x1E8
    int vertexGrowBy_;               // +0x1EC
    std::uint8_t unknown_1F0[0x200 - 0x1F0];
    std::uint16_t* indices_;         // +0x200
    int indexCount_;                 // +0x204
    int indexCapacity_;              // +0x208
    int indexGrowBy_;                // +0x20C
    float* uvs_;                     // +0x210 u, v per vertex
    int uvCount_;                    // +0x214
    int uvCapacity_;                 // +0x218
    int uvGrowBy_;                   // +0x21C
    int visible_;                    // +0x220
    float tint_[3];                  // +0x224 colour factors (the HUD: 1 selected, 0.5 not)
    float unknown_230[3];            // +0x230 zero (Create)
    // A row-major 4x4 matrix applied after the placement (Draw); its diagonal elements 0, 5 and 10
    // are the scale 005E7060 sets (the same value in all three).
    float scaleMatrix_[16];          // +0x23C
    float& ScaleX() { return scaleMatrix_[0]; }
    float& ScaleY() { return scaleMatrix_[5]; }
    float& ScaleZ() { return scaleMatrix_[10]; }
    CEngineObject* material_;        // +0x27C the window's black material (CUiWindow +0xEC)
    CEngineObject* texture_;         // +0x280 one of the window's referenced objects (+0xF8)
};
static_assert(offsetof(CUiMesh, colour_) == 0xD8);
static_assert(offsetof(CUiMesh, scaleMatrix_) == 0x23C && offsetof(CUiMesh, material_) == 0x27C);
static_assert(offsetof(CUiMesh, position_) == 0x1C && offsetof(CUiMesh, placement_) == 0x54);
static_assert(offsetof(CUiMesh, quads_) == 0x1C0);
static_assert(offsetof(CUiMesh, vertices_) == 0x1E0);
static_assert(offsetof(CUiMesh, indices_) == 0x200);
static_assert(offsetof(CUiMesh, uvs_) == 0x210 && offsetof(CUiMesh, uvGrowBy_) == 0x21C);
static_assert(offsetof(CUiMesh, quadGrowBy_) == 0x1CC && offsetof(CUiMesh, vertexGrowBy_) == 0x1EC &&
              offsetof(CUiMesh, indexGrowBy_) == 0x20C);
static_assert(offsetof(CUiMesh, visible_) == 0x220);
static_assert(offsetof(CUiMesh, uOffset_) == 0xCC);
static_assert(offsetof(CUiMesh, tint_) == 0x224);
static_assert(offsetof(CUiMesh, flags_) == 0x08);
static_assert(offsetof(CUiMesh, orientation_) == 0x34);
static_assert(offsetof(CUiMesh, plainColour_) == 0xD4);
static_assert(offsetof(CUiMesh, texture_) == 0x280);
static_assert(offsetof(CUiMesh, material_) == 0x27C);

// A growable array of 2D points with the interface arrays' 32-byte alignment.
struct UiPointF {
    float x;
    float y;
};
using UiPointArray = lib::AlignedArray<UiPointF>;
// 00453100 (the 8-byte aligned arrays' SetSize; aligned_arrays.cpp), through its address
// (ui_hud_map.cpp). `growBy` -1 keeps the growth step.
void __fastcall UiPointsSetSize(UiPointArray* points, void*, int size, int growBy);

// Map (set up by 00641F20, vtables 00742660 / 0074262C; 615..800 x 0..165). Regions: 0 the
// sling button, 1 the round map, 2 the map area, 3 and 4 the two view buttons, 5 a button
// above them, 6 the whole panel.
class CHudMap : public CUiWindow {
public:
    void OnMouseDown(int buttons, int x, int y);   // 00645060: the view buttons
    // 00645210 (RET 0xC; ui_hud.cpp, A-H26; Rebirth-patched, hooked in the vanilla launch only).
    // The button released over the map that took it. A held view button is let go (view_ 0, both
    // buttons dim; nothing else). Otherwise by region: 0 the sling (direction turned, the slide
    // starts), 1 / 2 a point of the map (the layout point's offset from (700, 80) over halfSide_,
    // on the round map also over the zoom, over scale_ about the centre; inside the world the
    // camera stops following and focuses there), 5 "camera north" (heading 0, unknown_1B0 0.5);
    // any region marks the input handled.
    void OnMouseUp(int buttons, int x, int y);
    void OnMouseMove(int buttons, int x, int y);   // 00645610
    // 00645640: over region 0 the wheel zooms the map by 2.5^(delta / 1200) (ui_hud_map.cpp).
    BOOL OnWheel(int buttons, int delta, int x, int y);
    // 00643DC0 (every frame, from 00642B40). Rebuilds the marker mesh (scene object 2): the
    // party and other units, the marked objects, the zone exits and the other players
    // (ui_hud_map.cpp).
    void BuildMarkers();
    // 00642E80 (every frame, before BuildMarkers). Follows the camera (centre and facing, the
    // compass needle scene object 10), clips the view polygon to the world rectangle and builds
    // the visible-area mesh (scene object 1) and the world-bounds quad (scene object 0).
    void UpdateView();
    // 00635CA0 (ui_hud_destroy.cpp; slot 0 00635C80 calls it). Destructor body: the visible
    // area's and the view polygon's aligned storage freed, then the window base's (005FE930).
    void DestroyBody();
    // 00635C80 (slot 0, the deleting destructor; ui_window_delete.cpp). DestroyBody, then the
    // memory freed when bit 0 of `flags` is set; returns this.
    CHudMap* HudMapDelete(unsigned flags);
    // 00642930 (plain RET; ui_hud_slide.cpp, A-H26; Rebirth-patched, this is the vanilla body,
    // hooked in the vanilla launch only; our callers go through CallLayout). Places the panel's
    // parts (scene objects 0-4 and 6-11), its text surface and the regions after the first at the
    // current slide offset.
    void LayoutVanilla();
    void CallLayout();                             // through 00642930
    // 00642B40 (slot 24, every frame; ui_hud_slide.cpp, A-H26; Rebirth-patched, the vanilla body).
    // The zoom grows (view 1, up to zoomMax_) or shrinks (view 2, down to 1) by 2.5 times a
    // second; UpdateView and BuildMarkers; a running slide advances (0.3 s for the whole way,
    // eased), the panel is laid out and the sling button (scene object 5) turned and placed for
    // the direction; the interface's "map shown" flag (+0x8B) follows the direction; the "camera
    // north" time (unknown_1B0) counts down, scene object 11 tinted 1 while it runs and 0.5 after.
    void UpdateVanilla();
    // 00641F20 (RET 8; ui_hud_slide.cpp, A-H26; Rebirth-patched, the vanilla body, hooked in the
    // vanilla launch only; the battle screen's build calls it through the address). Sets the new
    // panel up: the world's extent and the map's picture from the local player's zone record (32
    // by 32 and "battle00" without one), the zoom (`zoom` up to zoomMax_), the slide at its far
    // end when `slid`, the seven regions, the twelve sprites, the one-pixel surface at the centre
    // and the view polygon (the disc's sixteen corners); then UpdateView, BuildMarkers and Layout.
    void SetupVanilla(float zoom, std::uint8_t slid);

    CBattleScreen* owner_;           // +0x148
    float centreX_;                  // +0x14C world point at the map's centre (the camera's)
    float centreY_;                  // +0x150
    float facingX_;                  // +0x154 view direction (normalised by 00642E80)
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
    float unknown_1B0;               // +0x1B0 0.5 when the "camera north" key turns the camera; the
                                     //        frame update (00642B40) counts it down
};
static_assert(offsetof(CHudMap, centreX_) == 0x14C);
static_assert(offsetof(CHudMap, scale_) == 0x168);
static_assert(offsetof(CHudMap, zoomMax_) == 0x170);
static_assert(offsetof(CHudMap, viewPolygon_) == 0x178 && offsetof(CHudMap, visibleArea_) == 0x188);
static_assert(offsetof(CHudMap, view_) == 0x1AC);
static_assert(offsetof(CHudMap, sliding_) == 0x198 && offsetof(CHudMap, slide_) == 0x1A4);
static_assert(offsetof(CHudMap, slideDirection_) == 0x1A8);
static_assert(offsetof(CHudMap, unknown_1B0) == 0x1B0);

// Unit panel (set up by 006456E0, vtables 007426F8 / 007426C4; 0..185 x 0..225). Regions: 0
// the sling button, 1-4 the body parts, 5 the whole panel.
class CHudUnitPanel : public CUiWindow {
public:
    // 006477E0 (RET 4; ui_hud_panels.cpp, called by the panel's draw 006462A0). The health bar
    // and number of `unit` (scene objects 10 / 11), and its mana bar and number (12 / 13) when it
    // is one of the party.
    void ShowHealth(CUnit* unit);
    // 00647B70 (RET 4; ui_hud_panels.cpp). The six body parts of `unit`: the name of each present
    // part and, for state 3, its durability bar (scene objects 14 + 2i / 15 + 2i).
    void ShowParts(CUnit* unit);
    void OnMouseDown(int buttons, int x, int y);   // 00648330
    void OnMouseUp(int buttons, int x, int y);     // 00648370
    void OnMouseMove(int buttons, int x, int y);   // 006484C0
    BOOL OnWheel(int buttons, int delta, int x, int y);   // 006484F0
    // 006481D0 (ui_hud_panels.cpp, A-H25; the battle screen's keys 0063A560 call it after setting
    // bodyPart_). The body part buttons (scene objects 3-6): tint 1 for the selected one, 0.5 for
    // the others.
    void ShowBodyPart();
    // 00646B90 (RET 4; ui_hud_panels.cpp, A-H25; called by the panel's draw 006462A0). The first
    // six effects on `unit`: each one's spell text and time left as two lines of the panel's
    // text, and its spell's picture (scene objects 26 + i, cut from the panel's icon texture,
    // where up to six pictures are kept: effectIcons_). Scene object 9 shows when there are more.
    void ShowEffects(CUnit* unit);
    // 00635D00 (ui_hud_destroy.cpp; slot 0 00635CE0 calls it). Destructor body: the part labels,
    // textsVtable_'s array, the two strings (+0x168 first), then the window base's (005FE930).
    void DestroyBody();
    // 00635CE0 (slot 0, the deleting destructor; ui_window_delete.cpp). DestroyBody, then the
    // memory freed when bit 0 of `flags` is set; returns this.
    CHudUnitPanel* HudUnitPanelDelete(unsigned flags);
    // 00645F80 (plain RET; ui_hud_slide.cpp, A-H26; Rebirth-patched, this is the vanilla body,
    // hooked in the vanilla launch only; our callers go through CallLayout). Places the panel's
    // parts (scene objects 0, 1, 3-7, 9-13, the six body parts' pairs 14-25 at the places of the
    // tables 00742434 / 0074244C, the six effect pictures 26-31), its text surface and the
    // regions after the first at the current slide offset.
    void LayoutVanilla();
    void CallLayout();                             // through 00645F80
    // 00647FB0 (slot 24, every frame; ui_hud_slide.cpp, A-H26; Rebirth-patched, the vanilla body).
    // A running slide advances (as the map's), the panel is laid out and the sling button (scene
    // object 2) turned and placed for the direction; a dead hover unit is forgotten; the
    // interface's "unit panel shown" flag (+0x89) follows the direction and its +0x8C is the
    // selected body part.
    void UpdateVanilla();
    // 006456E0 (RET 8; ui_hud_slide.cpp, A-H26; Rebirth-patched: the vanilla body, hooked in the
    // vanilla launch only; the battle screen calls it through its address). `slid` (the
    // interface's +0x89) starts the panel at the far end of its slide: offset 180, slide 1,
    // direction +1 (else 0, 0, -1). Rectangle (0, 0)-(185, 225); regions: 0 the sling button, 1-4 the body part buttons
    // (tips 0x2774..0x2778), 5 the whole panel; scene objects 0 / 1 plain backgrounds, 2 the sling
    // button, 3-6 the part buttons (the selected one tinted 1, the others 0.5), 7 the frame, 8 a
    // strip of cells and 9 the "more effects" mark (both hidden), 10-13 the health and mana bars,
    // 14-25 the body parts' bars (tables 00742434 / 0074244C), 26-31 the effects' pictures (the
    // window's second texture); the text surface (20, 0)-(180, 220); Layout; no hover unit; the
    // texts "infounit_0".."infounit_17" and the labels "hl_skull", "hl_torso", "hl_leg", "hl_arm"
    // appended to the two string arrays; no effect pictures (-1).
    void SetupVanilla(int bodyPart, std::uint8_t slid);
    // 006462A0 (slot 20, RET 4; ui_hud_unit_draw.cpp, A-E2; Rebirth replaces the whole function
    // with its wide-layout version: this is the vanilla body, hooked in the vanilla launch only).
    // The panel's draw: the background (scene object 0), then for the hover unit, else the selected
    // one, while the panel is not slid away: its model in the panel (not on the info page 2), the
    // device's depth buffer set to 0 around the panel's area for the draw and back to 1 after it,
    // its name, the page's bars and texts (ShowHealth / ShowParts / DrawUnitInfo / ShowEffects),
    // then the window's base draw. Returns 1.
    BOOL DrawVanilla(int pass);

    CBattleScreen* owner_;           // +0x148
    CUnit* shownUnit_;               // +0x14C the first selected unit (RefreshSelection), or null
    CUnit* hoverUnit_;               // +0x150 the unit under the mouse (HoverUnit), or null
    // The model's view for the selected unit [0] and the hover unit [1], from the units'
    // prototypes (UnitPrototypeRecord +0x24 / +0x20; the battle screen copies them as dwords).
    float viewHeight_[2];            // +0x154 how far up the panel the model stands, in units of its size
    float viewSize_[2];              // +0x15C the model's size: it is drawn 1 / size (times its animation's ratio)
    char* strings_[2];               // +0x164 CStrings: the selected and the hover unit's names (00637940)
    int sliding_;                    // +0x16C set when the sling button starts the slide
    int offset_;                     // +0x170 current slide offset in pixels (to the left)
    int travel_;                     // +0x174 full slide distance
    float slide_;                    // +0x178 0..1: how far the panel has slid (offset_ = travel_ eased)
    float slideDirection_;           // +0x17C +1 / -1
    int bodyPart_;                   // +0x180 selected body part 0-3 (scene objects 3-6)
    int effectIcons_[6];             // +0x184 the spell pictures in the icon texture's six places
    void* textsVtable_;              // +0x19C CStringArray
    std::uint8_t unknown_1A0[0x1B0 - 0x1A0];
    void* partLabelsVtable_;         // +0x1B0 CStringArray (its data:)
    mfc::CString* partLabels_;       // +0x1B4 four: the shown names of "skull", "torso", "leg", "arm"
};
static_assert(offsetof(CHudUnitPanel, owner_) == 0x148 && offsetof(CHudUnitPanel, shownUnit_) == 0x14C);
static_assert(offsetof(CHudUnitPanel, hoverUnit_) == 0x150 && offsetof(CHudUnitPanel, strings_) == 0x164);
static_assert(offsetof(CHudUnitPanel, slideDirection_) == 0x17C && offsetof(CHudUnitPanel, offset_) == 0x170);
static_assert(offsetof(CHudUnitPanel, bodyPart_) == 0x180);
static_assert(offsetof(CHudUnitPanel, textsVtable_) == 0x19C);
static_assert(offsetof(CHudUnitPanel, partLabels_) == 0x1B4);

// Game clock (set up by 00648520, vtables 007425C8 / 00742594; 720..800 x 520..600).
class CHudClock : public CUiWindow {
public:
    void OnMouseDown(int buttons, int x, int y);   // 00648D30
    // 00648D70 (RET 0xC; ui_hud.cpp, A-H26; Rebirth-patched, hooked in the vanilla launch only).
    // The button released over the clock that took it: region 0 toggles the pause (with the
    // clock's sound); regions 1 and 2 set the normal / fast speed (not on a network client: the
    // pause lifted, fast_ 0 / 1, the world's update period 55 / 27 ms, the interface's fastGame_,
    // the sound); region 3 opens the objectives (OpenObjectives).
    void OnMouseUp(int buttons, int x, int y);
    void OnMouseMove(int buttons, int x, int y);   // 00649160
    // 006488E0 (slot 24, every frame): turns the face with the time of day, the hand and the
    // speed dial (scene objects 1-5), blinks while the game is paused (ui_hud_frame.cpp).
    void Update();
    // 00648F10 (ui_hud.cpp, A-H25; from the clock's click 00648D70). The pause button stops
    // pulsing, the click sound, and a new objectives window of the current zone (one button, no
    // exits, no zone list) opened on top.
    void OpenObjectives();
    void CallOpenObjectives();                     // through 00648F10 (its verify wrapper)
    // 00648520 (RET 4; ui_hud_setup.cpp, A-H26; Rebirth-patched: the vanilla body, hooked in the
    // vanilla launch only; the battle screen calls it through its address). The clock's rectangle
    // (720, 520)-(800, 600); scene objects: 0 the face (a disc of radius 65 about the layout's
    // corner (800, 600)), 1 the hand (a triangle), 2-5 four "in25arrow" sprites (placed by
    // Update), 6 and 7 two more discs (radius 32 and 80); regions 0-2: the three sectors of pi / 6
    // between radius 35 and 80 about the corner (pause, normal, fast; the two speed tips not on a
    // network client), region 3 the quarter disc of radius 34 (the objectives); then `fast`, the
    // world's update period (27 ms fast, 55 normal), the interface's copy, no blink, no pulse.
    void SetupVanilla(int fast);

    std::uint8_t unknown_148[4];
    float blink_;                    // +0x14C 0..1, the pause blink phase
    float dial_;                     // +0x150 speed dial angle (wraps at pi / 6)
    int fast_;                       // +0x154 fast game speed (copied to CInterface3D +0x94)
    bool highlight_;                 // +0x158 the pause button pulses (scene object 6)
};
static_assert(offsetof(CHudClock, blink_) == 0x14C);
static_assert(offsetof(CHudClock, highlight_) == 0x158);

// Movement panel (set up by 00649190, vtables 00742530 / 007424FC; 0..80 x 520..600). Region 0
// toggles the units' +0x438 flag, regions 1-4 set their movement mode 0-3 (+0x268).
class CHudMovePanel : public CUiWindow {
public:
    void OnMouseDown(int buttons, int x, int y);   // 00649630
    void OnMouseUp(int buttons, int x, int y);     // 00649670
    void OnMouseMove(int buttons, int x, int y);   // 00649750
    // 00649550 (slot 24, every frame). Shows the selected units' common mode and flag.
    void Update();
    // 00649810 (ui_hud_panels.cpp). Region 0 (and its key): plays the click, sets +0x438 of the
    // selected party units to 0 when all of them have 1, else to 1 (through the local view), and
    // shows the button's state (scene object 5).
    void ToggleFlag();
    void CallToggleFlag();                         // through 00649810 (its verify wrapper)
    // 00649910 (Rebirth-patched: through its address). Highlights mode `mode` (-1/-2: none).
    void ShowMode(int mode);
    // 00649910 (RET 4; ui_hud.cpp, A-H26; the vanilla body, hooked in the vanilla launch only).
    // mode_ = `mode`; the marker (scene object 0) shows for a mode of 0 or more; the four mode
    // buttons (scene objects 1-4) are tinted 1 for the mode and 0.7 for the others, and the marker
    // is turned about -z by (2 mode + 1) pi / 16 and placed on the circle of radius 73 about
    // (0, 600) at depth 8.
    void ShowModeVanilla(int mode);
    // 00649190 (slot 18, the panel's Create; ui_hud_setup.cpp, A-H26; Rebirth-patched: the vanilla
    // body, hooked in the vanilla launch only). The rectangle (0, 520)-(80, 600); region 0 the
    // quarter disc of radius 34 about the layout's corner (0, 600), regions 1-4 the four sectors
    // of pi / 8 between radius 35 and 80 (tips 0x2904..0x2908); scene objects: 0 the marker (a
    // triangle), 1-4 the four sectors as triangles from the corner (tint 0.7), 5 the middle disc
    // (radius 32); no mode shown (ShowMode(-1)), no flag (-1), the disc's texture frame 0.48.
    void SetupVanilla();

    CBattleScreen* owner_;           // +0x148
    int mode_;                       // +0x14C the shown movement mode (-1, -2: none, mixed)
    int flag_;                       // +0x150 common +0x438 of the selected units (-1, -2: none, mixed)
};
static_assert(offsetof(CHudMovePanel, mode_) == 0x14C && offsetof(CHudMovePanel, flag_) == 0x150);

// Top panel (set up by 00649AF0, vtables 00742498 / 00742464; 180..620 x 0..100). Regions: 0
// the sling button, 1 and 2 the two page buttons, 3 the whole panel.
class CHudTopPanel : public CUiWindow {
public:
    void OnMouseDown(int buttons, int x, int y);   // 0064A1C0
    void OnMouseUp(int buttons, int x, int y);     // 0064A200
    void OnMouseMove(int buttons, int x, int y);   // 0064A2C0
    BOOL OnWheel(int buttons, int delta, int x, int y);   // 0064A2F0
    // 0064A320 (ui_hud_panels.cpp). Shows page `page_`: highlights its button (scene objects 3 /
    // 4) and puts its text into the battle screen's message log (page 0 scrolled to the end).
    void ShowPage();
    void CallShowPage();                           // through 0064A320 (its verify wrapper)
    // 00649FF0 (slot 24, every frame): slides the panel in or out (ui_hud_frame.cpp).
    void Update();
    // 00649E20 (ui_hud_panels.cpp). Places the panel's parts, the message log and the regions at
    // the current slide offset.
    void Layout();
    void CallLayout();                             // through 00649E20 (its verify wrapper)
    // 0064A7A0 (ui_hud_panels.cpp). Builds page 1's text: the money ("format_money" with the
    // local party's +0x50, when above 0), then the local player's item list (+0xE0) merged by
    // kind (slot 14; counts added) into the world's copies (0055E940), one item name per line;
    // shown in the message log when page 1 is up.
    void ShowItems();
    // 00635E00 (ui_hud_destroy.cpp; slot 0 00635DE0 calls it). Destructor body: the two page
    // texts (page 1 first), then the window base's (005FE930).
    void DestroyBody();
    // 00635DE0 (slot 0, the deleting destructor; ui_window_delete.cpp). DestroyBody, then the
    // memory freed when bit 0 of `flags` is set; returns this.
    CHudTopPanel* HudTopPanelDelete(unsigned flags);
    // 00649AF0 (RET 8; ui_hud_top_panel.cpp). Sets the panel up over 180..620 x 0..100, slid in
    // when the low byte of `shown` is set: the two page buttons' and the slide button's regions
    // with their tips (0x27D9 / 0x27DA with 0x32 / 0x33, 0x27D8) and the whole panel's, two plain
    // backgrounds, the "in25arrow" slide arrow, the two page buttons and the frame; then page
    // `page` is shown and the panel laid out (both through their addresses).
    void Build(int page, int shown);
    // 0064A4E0 (RET 8; ui_hud_top_panel.cpp). Appends "<combat_picked_up or, with the low byte of
    // `crippled`, combat_crippled> <item name>" and a newline to page 0's text, and to the message log when
    // page 0 is up.
    void AddPickedUp(CItem* item, int crippled);
    // 0064A630 (RET 4; ui_hud_top_panel.cpp). The same with the money ("format_money"), for money > 0.
    void AddMoney(int money);
    mfc::CString& PageText(int page) { return *reinterpret_cast<mfc::CString*>(&pageTexts_[page]); }

    CBattleScreen* owner_;           // +0x148
    int sliding_;                    // +0x14C
    int offset_;                     // +0x150 current slide offset in pixels
    int travel_;                     // +0x154 full slide distance
    float slide_;                    // +0x158 0 (out) .. 1 (in)
    float slideDirection_;           // +0x15C
    int page_;                       // +0x160
    char* pageTexts_[2];             // +0x164 CStrings: the message log's text of pages 0 and 1
};
static_assert(offsetof(CHudTopPanel, owner_) == 0x148);
static_assert(offsetof(CHudTopPanel, slideDirection_) == 0x15C);
static_assert(offsetof(CHudTopPanel, page_) == 0x160);

// --- Screens with child windows: the save/load and options windows (ui_menus.cpp) -----------

// Base of the screens that hold child windows and extra input handlers (vtable 00741008,
// constructor 00600B20): the Esc menu, the save/load and options windows, the battle screen.
// Its input side's default handlers (00601110 button down, 006011F0 up, 006012D0 move,
// 00601570 double click, 00601820 key) pass the event on to the handlers and children.
class CUiContainer : public CUiWindow {
public:
    // 00600270 (ui_container.cpp; it does not use the window). Time since the last frame in
    // seconds: the display object's (00500960) slot 6 milliseconds * 0.001f, left unrounded in ST0.
    double FrameTime();
    // 006010E0 (ui_container.cpp). Base update (slot 24): every child's slot 24.
    void UpdateBase();
    // 00600EF0 (rewritten as a free function, ui_container_draws.cpp; this forwards to its address).
    // Base drawing (slot 20); returns 1.
    BOOL DrawContainer(int pass);
    // 00601040 (ui_container.cpp). Base slot 22, only while the interface draws (+0x30): the
    // window's own (005FECC0), then every enabled child's slot 22.
    void RedrawContainer();
    // 00601090 (ui_container.cpp). Base slot 23 likewise: 005FED00, then the enabled children's.
    void RedrawChildTips();
    // 00600D30. Adds a child window (null is ignored; ui_window.cpp).
    void AddChild(CUiWindow* child);
    // 00600B20 (RET 4; ui_container.cpp). Constructor: the window base (005FE700), no children or
    // extra input sides, vtables 00741008 / 00740FD4. Returns this.
    CUiContainer* Construct(CInterface3D* ui);
    // 00600BA0 (ui_container.cpp). Destructor body: the extra input sides and the children deleted
    // (their slot 0 with 1), both lists freed, then the window base's (005FE930).
    void DestroyContainer();
    // 00600B80 and 3 more (slot 0, the deleting destructor; ui_window_delete.cpp).
    // DestroyContainer, then the memory freed when bit 0 of `flags` is set; returns this.
    CUiContainer* UiContainerDelete(unsigned flags);
    // 00600CC0 (RET 4; ui_container.cpp). Appends an extra input side (null is ignored).
    void AddInput(UiInputSide* input);
    // 00601AC0 (RET 4; the containers' slot 19, ui_container.cpp). The window's own surfaces
    // (006002C0), then every enabled child's slot 19 with `show`.
    void ShowChildren(int show);

    UiPtrArray<CUiWindow> children_;          // +0x148
    UiInputSide** inputsBegin_;               // +0x158 extra input sides (STLport vector)
    UiInputSide** inputsEnd_;                 // +0x15C
    UiInputSide** inputsCapacity_;            // +0x160
};
static_assert(offsetof(CUiContainer, children_) == 0x148);
static_assert(offsetof(CUiContainer, inputsBegin_) == 0x158);
static_assert(sizeof(CUiContainer) == 0x164);

// A container's own input side (window + 0x94, input vtable 00740FD4; most screens keep some of
// these handlers in their own input vtables). An event goes to the extra input sides (+0x158) in
// order, then to the enabled children (for the mouse events only those whose rectangle holds the
// point, CUiWindow::ContainsPoint), each with its handled flag cleared first, until one sets it;
// that sets the container's own flag. ui_container.cpp.
struct UiContainerInput : UiInputSide {
    CUiContainer* Container() {
        return reinterpret_cast<CUiContainer*>(reinterpret_cast<char*>(this) - offsetof(CUiWindow, inputVtable_));
    }
    void OnDeactivate();                                // 00601A60: every side and child, no flags
    void OnLeftDown(int buttons, int x, int y);         // 00601110
    void OnLeftUp(int buttons, int x, int y);           // 006011F0
    void OnMouseMove(int buttons, int x, int y);        // 006012D0
    void OnRightDown(int buttons, int x, int y);        // 006013B0
    void OnRightUp(int buttons, int x, int y);          // 00601490
    void OnLeftDouble(int buttons, int x, int y);       // 00601570
    void OnRightDouble(int buttons, int x, int y);      // 00601650
    void OnKeyDown(int key, int repeat, int flags);     // 00601820
    void OnChar(int key, int repeat, int flags);        // 006018E0
    void OnKeyUp(int key, int repeat, int flags);       // 006019A0
    int OnWheel(int flags, int delta, int x, int y);    // 00601730 (RET 0x10; returns 1)

    // The shared loop: `slot` of every side and child; `atPoint` limits the children to those
    // holding (b, c).
    void Dispatch(int slot, int a, int b, int c, bool atPoint);
};

// The default handlers of a container's input side (`input` = window + 0x94).
void __fastcall UiContainerMouseDown(void* input, void* unused, int buttons, int x, int y);    // 00601110
void __fastcall UiContainerDoubleClick(void* input, void* unused, int buttons, int x, int y);  // 00601570

// A scroll bar (vtables 00742C18 / 00742BE4, constructor 0065AEA0; the battle screen's message
// log 0065CC00 derives from it). Regions: 0 the up arrow, 1 the down arrow, 2 the track.
class CUiScrollbar : public CUiWindow {
public:
    // 0065B090. The position; returns 1 (and clears the flag) when it moved since the last call.
    BOOL TakePosition(int* position);
    // 0065B360 (slot 24, every frame). Scrolls while an arrow is held: whole steps of the
    // accumulated amount move the position (virtual slot 25, SetPosition).
    void Update();
    void OnMouseDown(int buttons, int x, int y);   // 0065B240: thumb drag or an arrow
    void OnMouseUp(int buttons, int x, int y);     // 0065B340
    // 0065B1A0 (input slot 4, RET 0xC; ui_scrollbar.cpp, A-H20). While the thumb is dragged
    // (state 1): the position under the mouse (layout coordinates less the grab offset, over the
    // track's height times the maximum, truncated by _ftol) through slot 25. The event is used
    // either way.
    void OnMouseMove(int buttons, int x, int y);
    // 0065B430 (input slot 12, RET 0x10; ui_scrollbar.cpp). Over the track (region 2) the wheel
    // scrolls: the accumulated amount less delta (its low 16 bits) times speed_ over 120 (Update
    // applies it), the event used. Returns 1.
    int OnWheel(int flags, int delta, int x, int y);
    // Virtual slot 25 (0065B0C0 for the plain scroll bar): moves to `position`, clamped.
    void CallSetPosition(int position);
    // Virtual slot 26 (0065B490; 0065D0A0 for the scrolling text): places the bar over the layout
    // rectangle `rect` (rect_, arrows, thumb and regions; the text's surface and line width).
    void CallSetRect(const RECT* rect);
    // 0065AEA0 (RET 0x20; ui_scrollbar.cpp, A-H19). Constructor: a window of `ui` with the
    // "Scrollbar" texture (kind 3), four sprites (track, thumb, up, down) and three regions, laid
    // out over `rect` (SetRect, called directly) at `depth`, `maximum` positions, scrolling
    // `speed` lines per second; then SetPosition(0) from -1. Returns this.
    CUiScrollbar* Construct(CInterface3D* ui, RECT rect, float depth, int maximum, float speed);
    // 0065B0C0 (slot 25 of the plain bar, RET 4). `position` clamped to 0..maximum, moved_ set;
    // a new position moves the thumb sprite (1) and thumb_ along the track: the offset is the
    // track's height times the position over the maximum, truncated.
    void SetPosition(int position);
    // 0065B490 (slot 26 of the plain bar, RET 4). rect_, the track (the right 10 pixels less
    // 26 at either end), the four sprites and the three regions (up arrow, down arrow, the rest);
    // then the position set again through slot 25 (one below first, so it counts as a change).
    void SetRect(const RECT* rect);
    // 0065AFE0 (RET 4; ui_scroll_text.cpp, A-H20). The maximum (negative: 0), moved_ set, the four
    // sprites shown only for a maximum above 0; then the position set again through slot 25 (one
    // below first, so it counts as a change).
    void SetMaximum(int maximum);
    // 0065B720 (ui_scroll_text.cpp). The maximum position.
    int Maximum() const;
    // 0065AFC0 (and the slider's 0065BCB0; ui_control_delete.cpp, A-H20). Destructor body: the
    // scroll bar's vtables, then the window's (005FE930).
    void DestroyBody();
    // 0065AFA0 (the slider's 0065BC90; RET 4; ui_control_delete.cpp). Slot 0, the deleting destructor:
    // DestroyBody, then the memory freed when bit 0 of `flags` is set. Returns this.
    CUiScrollbar* ScrollbarDelete(unsigned flags);

    int maxPosition_;                // +0x148
    float speed_;                    // +0x14C lines per second while an arrow is held (/16)
    int position_;                   // +0x150
    // +0x154 the thumb's track (layout coordinates; the options slider 0065B8B0: left + 26 ..
    // right - 26 over the bottom 10 pixels). DR-H16: named from the slider's SetRect only.
    RECT track_;
    RECT thumb_;                     // +0x164 layout coordinates
    int grabX_;                      // +0x174 mouse offset from the thumb centre while dragging
    int grabY_;                      // +0x178
    // +0x17C the sprites' depth: the constructor's first float (0065AEA0 stores its float
    // arguments at +0x17C and +0x14C, the int at +0x148); the slider's SetRect places its four
    // sprites at it.
    float depth_;
    bool moved_;                     // +0x180
    std::uint8_t unknown_181[3];
    int state_;                      // +0x184 0 idle, 1 dragging the thumb, 2 up, 3 down
    float accumulated_;              // +0x188 scrolled amount not yet applied
};
static_assert(offsetof(CUiScrollbar, position_) == 0x150);
static_assert(offsetof(CUiScrollbar, track_) == 0x154);
static_assert(offsetof(CUiScrollbar, thumb_) == 0x164);
static_assert(offsetof(CUiScrollbar, depth_) == 0x17C);
static_assert(sizeof(CUiScrollbar) == 0x18C);

// The options window's horizontal slider (0x18C bytes, vtables 00742CBC / 00742C88): a scroll bar
// whose slot 26 lays it out along the bottom of its rectangle. Sprites (scene objects): 0 the
// track, 1 the thumb, 2 the right arrow, 3 the left arrow, all turned a quarter turn about z;
// regions 0 the left arrow, 1 the right arrow, 2 the track. ui_options.cpp.
class CUiSlider : public CUiScrollbar {
public:
    // 0065BC30 (RET 0x20; ui_scrollbar.cpp): CUiScrollbar's constructor (0065AEA0) with the
    // slider's vtables. Returns this.
    // (`depth` goes to depth_ +0x17C, `maximum` to +0x148, `speed` to speed_ +0x14C.)
    CUiSlider* Construct(CInterface3D* ui, RECT rect, float depth, int maximum, float speed);
    // 0065B730 (slot 25, RET 4; ui_scrollbar.cpp, A-H20). The scroll bar's SetPosition along the
    // track's width: the thumb sprite at (track left + offset, track top + 5), the thumb 28 wide
    // and 10 high.
    void SetPosition(int position);
    // 0065B810 (input slot 4, RET 0xC; ui_scrollbar.cpp, A-H20). The scroll bar's drag along x.
    void OnMouseMove(int buttons, int x, int y);
    // 0065B8B0 (slot 26, RET 4). Lays the slider out over `rect` (layout coordinates) and sets
    // the position again (through slot 25, so it counts as moved).
    void SetRect(const RECT* rect);
};
static_assert(sizeof(CUiSlider) == 0x18C);
static_assert(offsetof(CUiScrollbar, state_) == 0x184);
static_assert(offsetof(CUiScrollbar, accumulated_) == 0x188);

namespace mfc { class CString; }

// The pieces of a scrolling text (CUiScrollText below): a format change (0x14 bytes, vtable
// 00742F28, type 1) or a word (0x10 bytes, vtable 00742F20 over the base 00742F24, type 2).
struct UiTextToken {
    // Slot 0: the deleting destructor (with 1: the memory is freed; 0065D8C0 format, 0065D8F0 word).
    void Delete() {
        using Fn = void(__thiscall*)(UiTextToken*, int);
        reinterpret_cast<Fn>(static_cast<void**>(vtable_)[0])(this, 1);
    }
    // 0065D8E0 / 0065D960 (ui_scroll_text.cpp): the destructor body of a format token and of the
    // base: the base vtable 00742F24 back.
    void DestroyBase();
    // 0065D8A0 / 0065D8C0 (RET 4; ui_control_delete.cpp). Slot 0, the deleting destructors of the base
    // token (its body inlined: the base vtable written) and of a format token (DestroyBase), then the
    // memory freed when bit 0 of `flags` is set. Return this.
    UiTextToken* TextTokenDelete(unsigned flags);
    UiTextToken* FormatTokenDelete(unsigned flags);

    void* vtable_;
    int type_;                       // +0x04 1 format, 2 word
};
struct UiFormatToken : UiTextToken {
    bool newline_;                   // +0x08 a line break (a newline token, or a font change after
                                     //       the first token)
    std::uint8_t unknown_09[3];
    int font_;                       // +0x0C interface font
    COLORREF colour_;                // +0x10
};
static_assert(sizeof(UiFormatToken) == 0x14);
struct UiWordToken : UiTextToken {
    // 0065D910 (ui_scroll_text.cpp): the destructor body: the text released, the base vtable back.
    void DestroyWord();
    // 0065D8F0 (RET 4; ui_control_delete.cpp). Slot 0, the deleting destructor: DestroyWord, then the
    // memory freed when bit 0 of `flags` is set. Returns this.
    UiWordToken* WordTokenDelete(unsigned flags);

    char* text_;                     // +0x08 CString
    int width_;                      // +0x0C its width in the token's font (005F8340)
};
static_assert(sizeof(UiWordToken) == 0x10);
// MFC CArray of token pointers (SetSize 0065E010, ui_scroll_text.cpp).
using UiTokenArray = mfc::CArray<UiTextToken*>;
// A laid out line of a scrolling text (CUiScrollText +0x1B0 holds pointers to them; DR-H16: only
// the two fields DrawLines reads, the record's size is not known).
struct UiTextLine {
    int height_;                     // +0x00 pixels
    int firstToken_;                 // +0x04 index in the token array
};

// A scrolling text: a scroll bar subclass (constructor 0065CC00) showing tokenised text; the
// battle screen's message log and the objectives window's quest log.
class CUiScrollText : public CUiScrollbar {
public:
    // 0065CC00 (RET 0x1C; ui_scroll_text.cpp, A-H20). Constructor: a scroll bar over `rect` (by
    // value) at `depth` (maximum 0, 20 lines per second), the two arrays, a surface over the
    // rectangle (format 5; null when the allocation fails), laid out as SetRect without the
    // virtual call, then `text` tokenised and laid out, position 0 (slot 25, then the plain bar's
    // SetPosition), justified (align_ 2). Returns this.
    CUiScrollText* Construct(CInterface3D* ui, const char* text, RECT rect, float depth);
    // 0065CE20 (ui_scroll_text.cpp). Destructor body: the tokens deleted (slot 0), the lines
    // freed, both arrays' storage freed, then the scroll bar's (its vtables and the window's).
    void DestroyBody();
    // 0065CE00 (RET 4; ui_control_delete.cpp). Slot 0, the deleting destructor: DestroyBody, then the
    // memory freed when bit 0 of `flags` is set. Returns this.
    CUiScrollText* ScrollTextDelete(unsigned flags);
    // 0065CF30 (RET 4; ui_scroll_text.cpp). Appends `text`: scrolled to the end first, then kept
    // that far from the end (the position plus the growth of the maximum).
    void AddText(const char* text);
    // 0065CFE0 (RET 4; ui_scroll_text.cpp). Replaces the text (Clear, the new tokens laid out) and
    // scrolls to the top.
    void SetText(const char* text);
    // 0065D0A0 (slot 26, RET 4; ui_scroll_text.cpp). The scroll bar's SetRect (0065B490), then the
    // surface moved to the rectangle's corner and the text laid out over it (PlaceText).
    void SetRect(const RECT* rect);
    // 0065D120 (slot 20, RET 4; ui_scroll_text.cpp). Draws the lines again when the position
    // moved, then the window's base drawing; returns its result (1).
    BOOL Draw(int pass);
    // 0065D450 (ui_scroll_text.cpp). Every token deleted and the token array emptied (the lines
    // stay until the next layout).
    void Clear();
    // 0065DAA0 (ui_scroll_text.cpp). Breaks the tokens into lines (UiTextLine, 8 bytes each) no
    // wider than the text: a word adds its width and a third of the line's height (the height of
    // the font in effect at the line's start); a word reaching the width starts the next line
    // (unless it is the line's first), a newline format token ends the line and sets the next
    // lines' font. Then the scroll bar's maximum: the lines' heights less the text's height.
    void Layout();
    // 0065D4B0 (RET 4; ui_scroll_text.cpp). Appends the tokens of `text`: words (with their
    // widths), and before each run of newline, BEL C<rgb> and BEL F<digit> tokens one format
    // token.
    void Tokenize(const mfc::CString* text);
    // 0065D970 (RET 0xC; ui_scroll_text.cpp, A-H19; ECX not read). The token of `text` at `pos`
    // (spaces and tabs skipped): a BEL F / BEL C format (3 / 5 characters), a newline, or up to the
    // next space, tab, BEL or newline
    // into `out`; returns the position after it, or -1 at the end (`out` untouched).
    int NextToken(const mfc::CString* text, int pos, mfc::CString* out);
    // 0065D150 (RET 4; called by 0065D120; ui_scroll_text.cpp, DR-H16). Draws the lines
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
static_assert(offsetof(CUiScrollText, tokens_) == 0x19C);
static_assert(offsetof(CUiScrollText, lines_) == 0x1B0);
static_assert(offsetof(CUiScrollText, align_) == 0x1C4);

// A spin control of the options window (vtables 00742D5C / 00742D28, 0x174 bytes): a window
// stepping through its texts (the on / off settings). DR-H16: was UiSpinValue, with the rows'
// OptionSetting read as a separate UiSpin.
class CUiSpin : public CUiWindow {
public:
    // 0065BCD0 (RET 0x18; ui_spin.cpp, A-H19). Constructor: the window over the layout rectangle
    // `rect` (by value; also kept at +0x164), a surface of it (format 5; null when the allocation
    // fails), the first text `text` (null: the exe's empty text), not changed, value 0 (SetValue).
    // Returns this.
    CUiSpin* Construct(CInterface3D* ui, RECT rect, const char* text);
    // 0065BE70 (RET 4; ui_spin.cpp). Appends a text (null: the empty text).
    void AddText(const char* text);
    // 0065BEB0 / 0065BEE0 (RET 4; ui_spin.cpp). The value, and whether it changed since the last
    // call (the flag is cleared). SetValue: `value` (0 when not below the text count; a negative
    // one is kept), its text drawn on the cleared surface (format 1, colour 0xA7D7E4), changed.
    bool GetValue(int* value);
    void SetValue(int value);
    // 0065BE10 (ui_control_delete.cpp, A-H20). Destructor body: the spin's vtables, the texts
    // (006FE958), then the window's.
    void DestroyBody();
    // 0065BDF0 (RET 4; ui_control_delete.cpp). Slot 0, the deleting destructor: DestroyBody, then the
    // memory freed when bit 0 of `flags` is set. Returns this.
    CUiSpin* SpinDelete(unsigned flags);
    // 0065BFD0 (input side slot 2, RET 0xC; ui_spin.cpp, A-H25). A click: the message box's OK
    // sound and the next value (SetValue: back to the first past the last text).
    void OnLButtonDown(int flags, int x, int y);

    mfc::CStringArray texts_;        // +0x148
    int value_;                      // +0x15C
    bool changed_;                   // +0x160
    std::uint8_t unknown_161[3];
    RECT layout_;                    // +0x164 the constructor's rectangle
};
static_assert(offsetof(CUiSpin, value_) == 0x15C);
static_assert(offsetof(CUiSpin, layout_) == 0x164);
static_assert(sizeof(CUiSpin) == 0x174);

// A saved game in the save/load list (0x44 bytes, STLport vector at +0x164). Copy construction
// 00656640 (and 00657C60), assignment 006538E0, destructor 00656720 (not rewritten). The fields
// after the folder come from the save's info.sav (ui_saveload.cpp).
struct SaveEntry {
    // 00653A50 (RET 4, the argument unused; ui_saveload.cpp). The title: the texts "save_quick",
    // "save_auto_enter" / "save_auto_exit" for the folders "saveq", "savea" / "saveb", otherwise
    // the zone's name (00653BD0).
    void MakeTitle(int unused);
    // A-H14 (ui_saveload.cpp). 00653BD0 (RET 4): the zone's name, text "zone " + zone_ (its first
    // line), else the text "save_camp". 00653CD0 (RET 4, from RefreshList 006567A0): the allod's
    // name, text "allod " + map_, else "Unknown".
    mfc::CString ZoneTitle() const;
    mfc::CString AllodTitle() const;

    char* title_;                    // +0x00 CString shown in the list
    char* folder_;                   // +0x04 CString folder name under saves\ ("saveq" quick save,
                                     //       "savea"/"saveb" automatic saves, "save<n>")
    std::int32_t savedAt_;           // +0x08 time() when it was saved (info.sav version 2 and up)
    float gameTime_;                 // +0x0C the "gtime" variable
    char* map_;                      // +0x10 CString: the map of the zone
    char* zone_;                     // +0x14 CString: the zone
    mfc::CStringArray stringArrays_[2];  // +0x18, +0x2C copied with the entry
    bool broken_;                    // +0x40 no info.sav: titled "Error Save", no picture
    std::uint8_t unknown_41[3];
};
static_assert(sizeof(SaveEntry) == 0x44);
static_assert(offsetof(SaveEntry, zone_) == 0x14);
static_assert(offsetof(SaveEntry, broken_) == 0x40);

// ui_saveload.cpp. 006570E0 (__fastcall: the save folder's name, `stepFrame` in DL; plain RET):
// the quick and automatic saves. Nothing for a multiplayer client or when the disc lacks the
// space a save needs (0 free space: not checked). With `stepFrame` a frame step (00458010(0, 1));
// the screen copied into an 800x600 surface, the busy cursor, the server flag kept in the
// interface, "notify_saving" shown, the game saved (00581610), then info.sav (the "gtime"
// variable, the zone's map, the zone, the time, an empty title) and shot.sav (the surface as
// 256x192 5:6:5 pixels); the surface freed and the cursor back.
void __fastcall QuickSave(const char* slot, char stepFrame);
// 006575B0 (__fastcall, plain RET). Unless a multiplayer client: when saves\<slot>\ exists,
// "notify_loading" is shown, 00578920(1) and the game flow is asked to load it (flag 0).
void __fastcall QuickLoad(const char* slot);

// The save/load window (vtables 00741388 / 00741354, 0x194 bytes, opened from the Esc menu).
// Regions: 0 OK, 1 cancel, 2 delete, 3.. the visible list rows.
class CSaveLoadWindow : public CUiContainer {
public:
    void OnMouseDown(int buttons, int x, int y);       // 00654660
    void OnDoubleClick(int buttons, int x, int y);     // 006547B0
    BOOL Draw(int pass);                               // 006544A0 (slot 20): slides in
    void Update();                                     // 006545E0 (slot 24)
    void CloseDialog();                                // 00654430 (slot 26)
    void OnDialogResult(int result);                   // 00654450 (slot 27)
    // 006070C0 (ui_window_delete.cpp). The destructor body: the name, each entry's strings and
    // string arrays, the entries' storage, then the container's.
    void DestroyBody();
    // 006070A0 (slot 0, the deleting destructor; ui_window_delete.cpp). DestroyBody, then the
    // memory freed when bit 0 of `flags` is set; returns this.
    CSaveLoadWindow* SaveLoadDelete(unsigned flags);

    // A-H14 (ui_saveload.cpp).
    // 00655290. OK: nothing without a selection; a save window plays "ok" and saves into the new
    // save (row 0) or asks "overwrite" (message box 0x20000); a load window loads (LoadSelected).
    void Confirm();
    // 00655380. Delete: with a selection that is not a save window's new save, plays "delete" and
    // asks "delete" (message box 0x10000).
    void AskDelete();
    // 00655B20. Deletes every file of the selected save's folder and the folder, refills the list,
    // keeps the selection in it, takes its title, redraws the list and the picture.
    void DeleteSave();
    // 00655A10. A selected save that is not broken: "notify_loading" shown, 00578920(1), "ok"
    // played, the game flow asked to load its folder (flag 0).
    void LoadSelected();
    // Through the original addresses (the verify build's wrappers run there).
    void CallConfirm();
    void CallAskDelete();
    void CallDeleteSave();
    void CallLoadSelected();

    // 006567A0 (Rebirth-patched: callers use this forwarder, rule 9; the vanilla body is
    // RefreshListVanilla in ui_saveload_list.cpp, A-H26). Draws the list into surface 0: the caret
    // clamped to the name; up to sixteen rows from firstVisible_, each the save's title (the typed
    // name for the selected row of a save window) and, but for a save window's new save, its date
    // ("%d/%m, %H:%M"); the selected row also gets the caret (a save window, not the quick or an
    // automatic save) and the marker (scene object 4, hidden otherwise); then for a selected save
    // that is not broken its allod, zone, the game's day and time ("save_day" d, h:mm from
    // gameTime_ hours) and its folder.
    void RefreshList();
    void RefreshListVanilla();

    // ui_saveload.cpp. The window's two picture textures (256x256, 5:6:5): objects_[1] shows the
    // selected save's shot.sav, dialog_ the game for a new save. The Call* forms go through the
    // original addresses (the verify build's wrappers run there).
    // 00655550. Saves into the selected entry's folder (the game flow's save, then info.sav from
    // the new save's entry and shot.sav from dialog_'s pixels); a message box when the disc is full.
    void SaveGame();
    // 00656E10. The picture sprite shows the selected save's shot.sav (read into objects_[1]), or
    // dialog_ for the new save of a save window; hidden for none or a broken save.
    void ShowPicture();
    // 00655D40 (00656464 is its tail, not a function). Reads saves\save*\info.sav into the entry
    // list, sorts it, puts the new save's entry first in a save window (with dialog_ made from the
    // escape menu's 256x192 copy of the screen) and sets the scroll bar's range.
    void FillEntries();
    void CallSaveGame();
    void CallShowPicture();
    void CallFillEntries();
    // "cpuwindows": game-owned memory for a picture texture of the window (beside its DirectDraw
    // texture; the new renderer reads it). Nothing without the option or a texture.
    static void GivePictureMemory(CEngineObject* texture);
    // 00653DD0 (slot 25, the window's Create): builds the window (ui_layouts.cpp).
    void Build();

    int EntryCount() const { return static_cast<int>(entriesEnd_ - entriesBegin_); }
    // 006577B0 (ui_menus.cpp). The selected save's title becomes the name being edited.
    void TakeSelectedTitle();

    // ui_menu_keys.cpp.
    // 006548A0 (input side, RET 0xC). Keys: Enter confirms, Esc cancels, Up / Down / PgUp / PgDn
    // move the selection; in a save window Backspace, Delete, Home, End, Left and Right edit the
    // save's name (the first edit of a save that is not a quick or automatic one takes over its
    // title, Backspace / Delete starting from an empty name).
    void OnKeyDown(int key, int repeat, int flags);
    // 00655050 (input side, RET 0xC). A typed character in a save window: the first one for an
    // ordinary save starts an empty name; without Ctrl, a name under 60 characters takes the
    // character at the caret when the text stays under 178 pixels (font 1); the list is redrawn.
    // Backspace, Tab, Enter and Esc only go to the base handler, which every character reaches.
    void OnChar(int key, int repeat, int flags);
    // 00655430. Clamps the selection to the list, scrolls it into view (16 rows), takes its
    // title as the name, redraws the list and shows its picture.
    void ShowSelection();
    void CallShowSelection();        // through 00655430
    // 00657820 (RET 4). Entry `index` is the quick save or an automatic save ("saveq", "savea",
    // "saveb"; no bound check).
    bool IsReservedSave(int index);

    SaveEntry* entriesBegin_;        // +0x164
    SaveEntry* entriesEnd_;          // +0x168
    SaveEntry* entriesCapacity_;     // +0x16C
    int selected_;                   // +0x170
    int firstVisible_;               // +0x174 list scroll position
    bool isSave_;                    // +0x178 save (not load) window
    bool sliding_;                   // +0x179 sliding in; input is ignored meanwhile
    std::uint8_t unknown_17A[2];
    float slide_;                    // +0x17C 0..1
    CUiScrollbar* scrollbar_;        // +0x180
    CEngineObject* dialog_;          // +0x184 the save window's picture of the game for the new save
                                     //        (a texture made by FillEntries; released by slot 26.
                                     //        The name is from an earlier reading.)
    char* name_;                     // +0x188 CString: the save name being edited
    bool nameTyped_;                 // +0x18C
    std::uint8_t unknown_18D[3];
    int cursor_;                     // +0x190 caret position in name_
};
static_assert(offsetof(CSaveLoadWindow, selected_) == 0x170);
static_assert(offsetof(CSaveLoadWindow, slide_) == 0x17C);
static_assert(offsetof(CSaveLoadWindow, cursor_) == 0x190);
static_assert(sizeof(CSaveLoadWindow) == 0x194);

// A setting's definition (0x18 bytes; the exe's table of 26 at 00787430).
struct OptionDefinition {
    int id_;                         // +0x00
    const char* name_;               // +0x04 "option_" + name is its text key (else the name is shown)
    int kind_;                       // +0x08 0 a slider, 1 an on / off spin control
    int maximum_;                    // +0x0C the slider's range
    int group_;                      // +0x10 the option group (0-10) it is shown in
    int row_;                        // +0x14 its row there
};
static_assert(sizeof(OptionDefinition) == 0x18);
inline const OptionDefinition* OptionDefinitions() { return reinterpret_cast<const OptionDefinition*>(0x00787430); }
constexpr int kOptionDefinitionCount = 26;

// A key action's definition (0x10 bytes; the exe's table of 59 at 00787080).
struct ActionDefinition {
    int id_;                         // +0x00 the action (the key code in the key table 0079BF70)
    const char* name_;               // +0x04 "action_" + name is its text key
    int group_;                      // +0x08
    int row_;                        // +0x0C
};
static_assert(sizeof(ActionDefinition) == 0x10);
inline const ActionDefinition* ActionDefinitions() { return reinterpret_cast<const ActionDefinition*>(0x00787080); }
constexpr int kActionDefinitionCount = 59;

// A setting of the options window (0x14 bytes, 26 of them at +0x378; copy constructor 00653100,
// destructor 00651CE0: the text). The rows of the shown group point at them.
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
    std::uint8_t unknown_01[3];
    int exit_;                       // +0x04 index in the zone's exit list
    float distance_;                 // +0x08
    float x_;                        // +0x0C centre of the exit rectangle (world units)
    float y_;                        // +0x10
    float mapX_;                     // +0x14 on the picture (layout)
    float mapY_;                     // +0x18
};
static_assert(sizeof(ObjectiveExit) == 0x1C);

// The objectives window (vtables 007411BC / 00741188, 0x1F0 bytes, constructor 0063B5D0; TAB in a
// zone opens it for the current zone). Left: the quests of the zone (rows), the log; right: the
// zone picture with its exits; the arrows switch between zones when it lists several.
class CObjectivesWindow : public CUiContainer {
public:
    // 0063B5D0 (RET 4; ui_screen_constructors.cpp). Constructor: the container, the empty zone
    // list, strings, exit array (vtable 00741264), quest arrays and places; vtables 007411BC /
    // 00741188 (the rest is set by Build). Returns this.
    CObjectivesWindow* Construct(CInterface3D* ui);
    // 00603B90 (ui_window_delete.cpp). The destructor body: the objective texts, the quest places'
    // storage, the quests, the exits (vtable 00741264, storage freed), the two zone names, the
    // zone list, then the container's. The exit distances (+0x178) are not freed here.
    void DestroyBody();
    // 00603B70 (slot 0, the deleting destructor; ui_window_delete.cpp). DestroyBody, then the
    // memory freed when bit 0 of `flags` is set; returns this.
    CObjectivesWindow* ObjectivesDelete(unsigned flags);
    // 0064BE80 (slot 25, the window's Create; ui_objectives.cpp).
    void Build();
    // 0064CC10: shows zone `zone`: picture, exits, quests and description.
    void ShowZone(const char* zone);
    // 0064DC60 (__fastcall): when the selected quest changed, its description in the log and a
    // marker and region per place of the quest on the zone picture.
    void ShowQuest();
    // 0064D9B0 (__fastcall). Draws the quest list: up to five rows from `firstRow_`, each with
    // its title ("quest " text, else the quest's name) and its state mark (scene objects 16-20:
    // state 2 one frame, 3 the other, else hidden); the selected row gets the cursor (object 2).
    void DrawQuestList();
    // ui_objectives_input.cpp (A-H17).
    // 0064E330 (input slot 2, RET 0xC). The base input's press, then by region: 0 OK ("tutorial
    // ok" sound; with two buttons the time to the nearest exit passes (00578700) and the zone is
    // requested at that exit (RequestZone); else the pending value 0), 1 cancel (with two
    // buttons: the cancel sound and the pending value 0), 2 / 3 the previous / next listed zone
    // (wrapping; only when zones are listed and there are two or more), 4-8 a quest row
    // (+0x1C0 the first shown; selected, drawn, shown), from 9 an exit (with two buttons and its
    // flag: the "enter" sound and SelectExit, else the cancel sound).
    void OnLButtonDown(int buttons, int x, int y);
    // 0064E5A0 (input slot 9, RET 0xC; ui_objectives_input.cpp, A-H21). The base input's key
    // handler, then: Enter as the OK region (without two buttons the pending value 0, else the
    // nearest exit's travel and zone request), Esc closes (the cancel sound with two buttons, else
    // the OK sound), PgUp / PgDn / Up / Down move the selected quest by 5 / 1 (RefreshQuests); those
    // mark the key used. A key not used that is bound to the objectives action (key table 4) closes a
    // window with one button.
    void OnKeyDown(int key, int repeat, int flags);
    // 0064EA80 (RET 4; ShowQuest calls it through its address). The places of quest `name` into
    // +0x1CC (emptied first): a multiplayer client takes them from the quest database's record
    // (its float pairs), otherwise from map.txt's "#quest <name>" entry (the second line after
    // it, its words as x y pairs).
    void LoadQuestPlaces(const char* name);
    // The quest places (+0x1CC) as the STLport vector they are.
    stlp::vector<UiPointF>& QuestPlaces() { return reinterpret_cast<stlp::vector<UiPointF>&>(questPlacesBegin_); }
    // The other containers as what they are (the fields below are their parts).
    stlp::vector<mfc::CString>& Zones() { return reinterpret_cast<stlp::vector<mfc::CString>&>(zonesBegin_); }
    mfc::CArray<ObjectiveExit>& Exits() { return reinterpret_cast<mfc::CArray<ObjectiveExit>&>(exitsVtable_); }
    mfc::CStringArray& Quests() { return reinterpret_cast<mfc::CStringArray&>(quests_); }
    mfc::CStringArray& ObjectiveTexts() { return reinterpret_cast<mfc::CStringArray&>(objectiveTexts_); }
    // ui_objectives_input.cpp (A-H24).
    // 0064E990 (RET 4). Exit `index` becomes the nearest (+0x19C): its marker (sprite 21 + index)
    // 1.2 times markerScale_ and the first frame, the blink restarted; the others markerScale_ and
    // the first frame when their flag is set, else u offset -0.125. Sprites 4 and 5 hidden.
    void SelectExit(int index);
    // 0064E8B0. With quests: the selected one clamped to the list, the shown rows moved to keep it
    // within five, the bar set there (slot 25, then TakePosition), the list drawn and the quest
    // shown.
    void RefreshQuests();
    // 0064E760 (slot 24, every frame). The container's update; a moved bar shows its rows; the
    // blink time advances by the frame time and gives two alphas, (cos(2.5 t) x 0.3 + 0.5) x 255
    // and (cos(5 t) x 0.4 + 0.6) x 255 (_ftol): the quest place markers (sprites after the exits')
    // are red with the second, the nearest exit's marker white with the second, the other exits'
    // grey (C8C8C8) with the first; all drawn in their plain colour (flag 0x100).
    void Update();
    // Through their addresses (ui_objectives.cpp), so the wrappers run.
    void CallSelectExit(int index);
    void CallRefreshQuests();
    // A-H25 (ui_objectives.cpp). 0064BE40 (RET 0x10): what the window is opened for, before it
    // opens: the zone, the exits' distances (or null), the two buttons and the zone list.
    // 0064D990 (slot 41): the help topic, "quest_global_map" with two buttons, else
    // "quest_mission".
    void Setup(const char* zone, void* exitDistances, int twoButtons, int listZones);
    const char* HelpTopic() const;

    char** zonesBegin_;             // +0x164 STLport vector<CString> of the zones listed
    char** zonesEnd_;                // +0x168
    char** zonesCapacity_;           // +0x16C
    char* zoneName_;                 // +0x170 CString: the zone it was opened for
    int twoButtons_;                 // +0x174 OK on the left and a second button (else OK centred)
    stlp::map<int, float>* exitDistances_;   // +0x178 exit -> distance, or null
    int listZones_;                  // +0x17C list the zones of the zone's allod with quests
    int zoneIndex_;                  // +0x180 the zone shown
    char* shownZone_;                // +0x184 CString
    void* exitsVtable_;              // +0x188 CArray<ObjectiveExit>
    ObjectiveExit* exits_;           // +0x18C
    int exitCount_;                  // +0x190
    int exitCapacity_;               // +0x194
    int exitGrowBy_;                 // +0x198
    int nearestExit_;                // +0x19C
    int selectedQuest_;              // +0x1A0 row of the quest named by the global at 007C8178
    int shownQuest_;                 // +0x1A4 the row ShowQuest last showed (-1: none)
    std::uint8_t quests_[0x14];      // +0x1A8 CStringArray (data +0x1AC, size +0x1B0)
    CUiScrollbar* bar_;              // +0x1BC
    int firstRow_;                   // +0x1C0 the first quest row shown (the bar's position)
    float markerScale_;              // +0x1C4
    float blinkTime_;                // +0x1C8 seconds since the exit was selected (the markers' blink)
    // +0x1CC STLport vector of the selected quest's places (8-byte points); RefreshQuests
    // (0064DC60) gives each a sprite and a region after the exits'.
    UiPointF* questPlacesBegin_;     // +0x1CC
    UiPointF* questPlacesEnd_;       // +0x1D0
    UiPointF* questPlacesCapacity_;  // +0x1D4
    std::uint8_t objectiveTexts_[0x14];   // +0x1D8 CStringArray of obj_0..obj_2
    std::uint32_t savedAlphaRef_;    // +0x1EC
};
static_assert(offsetof(CObjectivesWindow, zonesBegin_) == 0x164);
static_assert(offsetof(CObjectivesWindow, exits_) == 0x18C);
static_assert(offsetof(CObjectivesWindow, bar_) == 0x1BC);
static_assert(sizeof(CObjectivesWindow) == 0x1F0);

// The game's table of key names (0x48 bytes each, 88 of them from 007857B0 to 00787070).
struct UiKeyName {
    int scan_;                       // +0x00 scan code
    std::uint8_t unknown_04[4];
    char name_[0x40];                // +0x08
};
static_assert(sizeof(UiKeyName) == 0x48);
inline const UiKeyName* UiKeyNames() { return reinterpret_cast<const UiKeyName*>(0x007857B0); }
constexpr int kUiKeyNameCount = 88;

// A key binding row of the options window (0x18 bytes, vector at +0x36C; rows_ point at them;
// copy constructor 00652FF0, destructor 006537E0 with 0).
struct OptionBinding {
    // Inlined in the options window's destructor body (0062B180): the two strings, then the
    // vector's storage (its elements' destructor 0062B740 is empty).
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

// The options window (vtables 00741FAC / 00741F78, 0x418 bytes). Regions: 0 OK, 1 cancel, 2-12
// the option groups (100..250 x 156..420), 13.. the rows of the shown group.
class COptionsWindow : public CUiContainer {
public:
    void OnMouseDown(int buttons, int x, int y);       // 00650C20
    void OnDoubleClick(int buttons, int x, int y);     // 00650D70
    BOOL Draw(int pass);                               // 00650920 (slot 20): slides in
    void Redraw();                                     // 00650A20 (slot 22): not while sliding
    void OnDialogResult(int result);                   // 006508E0 (slot 27)
    const char* HelpTopic();                           // 00650900 (slot 41)
    // 00650A30 (slot 24): updates the children, the slide-in, and applies the settings whose
    // scroll bar or spin control changed (volumes, the display's gamma at once).
    void Update();

    // ui_options.cpp (DR-H16).
    // 00651350. Draws surface 0: the group titles (the shown one white-tinted, its button sprite
    // at full tint, the others half), the shown group's title, and each row's name and value (a
    // binding waiting for its key in red); the highlight sprite (scene object 2) on the selected
    // row.
    void RefreshRows();
    // 00651D70 (RET 4). Shows option group `group` (clamped to 0-10): the rows filled from the
    // settings' and actions' definitions of the group (each row's region gets the "tip option_" /
    // "tip action_" text), the shown rows' controls enabled and the others disabled, the first
    // filled row selected (or row 0), the key wait cleared; RefreshRows.
    void ShowGroup(int group);
    // 006524A0. OK: the key table and every setting stored into the game's option globals (the
    // difficulty only outside a multiplayer client; a changed display mode applied, 0056D7A0).
    void Apply();
    // 00651940. The 26 settings made again from their definitions: the shown name ("option_" +
    // name), and a slider (kind 0) or an on / off spin control (kind 1; the difficulty's texts
    // "difficulty_hard" / "difficulty_easy") as a child at the definition's row.
    void BuildSettings();
    // 00652200. The key table and the settings' values from the game's option globals; each
    // setting's slider (slot 25) or spin control takes its value.
    void LoadSettings();
    // 00651600. The 59 binding rows made again from the action definitions: the shown name
    // ("action_" + name), and the first key name (table 007857B0) of a scan code bound to the
    // action, as the keys' text.
    void BuildBindings();
    // The same through the original addresses (the verify build's wrappers run there).
    void CallRefreshRows();
    void CallShowGroup(int group);
    void CallApply();
    void CallBuildSettings();
    void CallLoadSettings();
    void CallBuildBindings();

    // 00652000 (ui_options.cpp, A-H21). Binds pressedKey_ to the selected row's action: the row's
    // keys unbound and replaced by the pressed one (keys text rebuilt), the key taken from the first
    // binding row of the action it was bound to (that row's text rebuilt), then the key table entry
    // set. Nothing without a pressed key or a binding row.
    void CommitBinding();
    void CallCommitBinding();
    // Cancel: puts back the volumes and the display settings the window started with.
    static void RestoreSettings();
    // 006503C0 (slot 25, the window's Create): builds the window (ui_layouts.cpp).
    void Build();
    // 0062B180 (the destructor body; slot 0 0062B160 deletes; ui_menus.cpp): the group titles, the
    // settings' and the bindings' strings and storage, then the container's (00600BA0).
    void DestroyOptions();
    // 0062B160 (slot 0, the deleting destructor; ui_window_delete.cpp). DestroyOptions, then the
    // memory freed when bit 0 of `flags` is set; returns this.
    COptionsWindow* OptionsWindowDelete(unsigned flags);

    // 00650E40 (input side, RET 0xC; ui_menu_keys.cpp). Keys: Esc cancels, Enter starts a key
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
    std::uint8_t unknown_3F6[2];
    float slide_;                    // +0x3F8
    mfc::CStringArray groupNames_;   // +0x3FC the option groups' titles
    bool displayModeChanged_;        // +0x410 a display mode setting (7-9) changed
    std::uint8_t unknown_411[3];
    const UiKeyName* pressedKey_;    // +0x414 the key pressed for a binding row (OnKeyDown; Build
                                     //        clears it)
};
static_assert(offsetof(COptionsWindow, settings_) == 0x378);
static_assert(offsetof(COptionsWindow, displayModeChanged_) == 0x410);
static_assert(offsetof(COptionsWindow, selectedRow_) == 0x364);
static_assert(offsetof(COptionsWindow, bindings_) == 0x36C);
static_assert(offsetof(COptionsWindow, groupNames_) == 0x3FC);
static_assert(offsetof(COptionsWindow, rows_) == 0x384);
static_assert(offsetof(COptionsWindow, waitingForKey_) == 0x3F4);
static_assert(offsetof(COptionsWindow, slide_) == 0x3F8);
static_assert(sizeof(COptionsWindow) == 0x418);

// The message box's edit field (constructor 0065C010: interface, RECT; vtables 00742DF4 / 00742DC0,
// 0x17C bytes; text set by 0065C290).
class CUiEditField : public CUiWindow {
public:
    // 0065C610 (input side slot 9, RET 0xC; ui_edit_field.cpp, DR-H16). While focused:
    // Backspace and Delete remove a character at the caret, Home / End / Left / Right move it
    // (no clamping here); the first edit key starts editing (Backspace / Delete from an empty
    // text, the others with the caret at the end). Then the text is drawn again (0065C2E0) and
    // the key is marked used. Other keys: nothing.
    void OnKeyDown(int key, int repeat, int flags);
    // 0065C2E0 (Rebirth-patched: callers use this forwarder, rule 9; the vanilla body is
    // DrawContentsVanilla in ui_edit_field.cpp, A-H26). The caret is clamped to the text; with the
    // surface's DC (nothing without one) the surface is cleared, the text drawn over the whole
    // field in font_ and colour_ (centred when centred_), and the caret drawn as a line of
    // caretColour_ over the field's height after the first caret_ characters, unless it falls
    // outside the field. (Not named DrawText: windows.h makes that a macro.)
    void DrawContents();
    void DrawContentsVanilla();
    mfc::CString& Text() { return *reinterpret_cast<mfc::CString*>(&text_); }
    // 0065C010 (RET 0x14; ui_edit_field.cpp, A-H19). Constructor: the window, an empty text,
    // vtables 00742DF4 / 00742DC0, every character class, font 1, at most 50 characters, colours
    // 0xFFFFFF / 0xFF, no surfaces, then a surface (format 5) over `rect` (kept in area_; null when
    // the allocation fails), the text drawn, focused. caret_, editing_ and changed_ are left as
    // they are. Returns this.
    CUiEditField* Construct(CInterface3D* ui, RECT rect);
    // 0065C140 (ui_control_delete.cpp, A-H20). Destructor body: the field's vtables, the text
    // released, then the window's.
    void DestroyBody();
    // 0065C120 (RET 4; ui_control_delete.cpp). Slot 0, the deleting destructor: DestroyBody, then the
    // memory freed when bit 0 of `flags` is set. Returns this.
    CUiEditField* EditFieldDelete(unsigned flags);
    // 0065C930 (input side slot 10, RET 0xC; ui_edit_field.cpp, A-H19). While focused and
    // without Ctrl held, a character other than Backspace, Tab, Enter and Esc whose class (digit 1,
    // letter or space 2, '.' or ':' 8, other 4) is allowed: the first one starts editing from an
    // empty text; it is inserted at the caret when the text is shorter than maxLength_ and the new
    // text is narrower than area_ in font_ (005F8340), then the text is drawn and the key used.
    void OnChar(int key, int repeat, int flags);
    // A-H25 (ui_edit_field.cpp). 0065C1A0 (RET 0x18): the character classes, the mode, the font,
    // the length limit and the two colours, then the text drawn. 0065C290 (RET 4): the text (not
    // editing, changed, the caret at its end), drawn. 0065C2D0 (RET 4): whether keys are taken.
    void SetLook(std::uint32_t charClasses, char mode, int font, int maxLength, COLORREF colour, COLORREF caretColour);
    void SetText(const char* text);
    void SetFocused(bool focused);

    RECT area_;                      // +0x148 the surface's layout rectangle
    std::uint32_t charClasses_;      // +0x158 classes OnChar takes (1 digits, 2 letters / space, 4 others, 8 '.' ':')
    std::uint8_t centred_;           // +0x15C 0 from the constructor; SetLook's mode: the text is drawn centred
    std::uint8_t unknown_15D[3];
    int font_;                       // +0x160 interface font
    int maxLength_;                  // +0x164
    COLORREF colour_;                // +0x168 the text's colour
    COLORREF caretColour_;           // +0x16C SetLook's last argument (0xFF from the constructor)
    const char* text_;               // +0x170 CString: the text typed
    int caret_;                      // +0x174 caret position in text_
    bool editing_;                   // +0x178 an edit key was pressed (the first one sets it)
    bool focused_;                   // +0x179 keys are taken only while set
    bool changed_;                   // +0x17A the text changed
    std::uint8_t unknown_17B;
};
static_assert(offsetof(CUiEditField, caret_) == 0x174);
static_assert(offsetof(CUiEditField, changed_) == 0x17A);
static_assert(offsetof(CUiEditField, text_) == 0x170);
static_assert(sizeof(CUiEditField) == 0x17C);

// The message box (constructor 00634650: interface, title and text keys, result, flags, format;
// vtables 00742348 / 00742314, 0x180 bytes; the new game's difficulty question 0074225C is built
// on it with its own Create 006337B0). Flags: 1 an OK button, 2 with 1 also a cancel button (neither: one
// cancel button), 4 an edit field instead of the text (its initial text is the text key itself).
class CMessageBox : public CUiContainer {
public:
    // 00634650 (RET 0x18; ui_screen_constructors.cpp). Constructor: the container, the title
    // (the text of `titleKey`) and, without flag 4, the text of `textKey` (else editText_ =
    // `textKey`, the edit field's first text); `result`, `flags`, `format` kept; vtables 00742348 /
    // 00742314. A missing text gives its key (GetString). Returns this.
    CMessageBox* Construct(CInterface3D* ui, const char* titleKey, const char* textKey, std::uint32_t result,
                           std::uint32_t flags, std::uint32_t format);
    // 00634790 (slot 25, ui_layouts.cpp). Builds the box: the "saveload" textures, a surface over
    // 200..600 x 50..500 with the title and the text (or the edit field) centred vertically as a
    // block, the dark shade and frame behind it and the buttons below (regions with tips
    // 0xC3B4 OK / 0xC3B5 cancel).
    void Build();
    // 00634D20 (input side slot 2, RET 0xC; ui_message_box.cpp). The container's handler first;
    // then region 0 (OK): for the exit question (result_ 0xFFFF0000) the exit movie and the game
    // quits; otherwise the OK sound and, with the edit field, its text copied into editText_;
    // region 1 the cancel sound; any region n >= 0 then leaves result_ | (n + 1) pending.
    void OnLButtonDown(int buttons, int x, int y);
    // 00634E60 (input side slot 9, RET 0xC). The container's handler first; unless a child used
    // the key, Enter answers as region 0 (the edit text copied before the sound) and Esc as
    // region 1, and the key is marked used.
    void OnKeyDown(int key, int repeat, int flags);
    // The OK answer's edit text into editText_ (the caller's buffer).
    void CopyEditText();
    // 00634D00 (slot 26 of the message box, the master request box 00741DD8 and the difficulty
    // question 0074225C; ui_small_members.cpp). The box closes: its scene objects and surfaces
    // released (005FF7D0, 005FFC50).
    void OnClose();
    // 0061E3E0 (and the difficulty question's 00633750, the same code; ui_window_delete.cpp). The
    // destructor body: the text and title strings, then the container's.
    void DestroyBody();
    // 00633730 and 1 more (slot 0, the deleting destructor; ui_window_delete.cpp). DestroyBody,
    // then the memory freed when bit 0 of `flags` is set; returns this.
    CMessageBox* MessageBoxDelete(unsigned flags);

    const char* title_;              // +0x164 CString (text of the title key)
    const char* text_;               // +0x168 CString (text of the text key)
    std::uint32_t result_;           // +0x16C 0xFFFF0000: the text in font 1, else 2
    std::uint32_t flags_;            // +0x170
    std::uint32_t format_;           // +0x174 added to the text's DrawText format
    CUiEditField* edit_;             // +0x178 the edit field (flag 4)
    char* editText_;                 // +0x17C the edit field's first text; OK copies the typed one here
};
static_assert(offsetof(CMessageBox, title_) == 0x164);
static_assert(offsetof(CMessageBox, edit_) == 0x178);
static_assert(sizeof(CMessageBox) == 0x180);

// CMasterRequestBox, the message box waiting on a master server request: ui_screens.h.

// The help / message window (0x194 bytes, vtables 00740E28 / 00740DF4; built inline by
// CInterface3D::ShowMessage 005F8A80 and OpenHelp 005F86F0, ui_create_screen.cpp). Its pages
// come from the texts: "tutorial <text_>" holds the title key and a "#screen" line per page, and
// "tutor <key>" each key's text. Names provisional.
class CHelpWindow : public CUiContainer {
public:
    // 005F8970 (ui_window_delete.cpp). The destructor body: the two lists (nodes, then the
    // sentinel), the text array, the two strings, then the container's.
    void DestroyBody();
    // 005F8950 (slot 0, the deleting destructor; ui_window_delete.cpp). DestroyBody, then the
    // memory freed when bit 0 of `flags` is set; returns this.
    CHelpWindow* HelpWindowDelete(unsigned flags);
    // 00658E10 (slot 25; ui_help_window.cpp). Loads "saveload", makes a 256x256 texture, reads the
    // title and the pages (above; the keys replaced by their "tutor" texts, trailing CR / LF
    // trimmed); without the text or without pages the interface's pending value is set to 0 and
    // nothing else happens. Otherwise the page is clamped to the pages, the pulse restarted, the
    // device's alpha reference set to 1 and the page shown (00659420).
    void Build();
    // 006590F0 (slot 26). Empties the two lists, then the surfaces, regions and scene objects.
    void Close();
    // 0065A5E0 (slot 24). The children's update, then the pulse: its time advanced by the frame
    // time and the pulsing sprites' plain colour set from it (alpha 255 * v, red and green / blue
    // from 80 * (1 - v), v = (cos(5 t) + 1) / 2).
    void Update();
    // 00659190 (input slot 2, RET 0xC). The container's button handler, then region 0 the
    // previous page, 1 OK (Finish), 2 the next page.
    void OnLeftDown(int buttons, int x, int y);
    // 006592C0 (input slot 9, RET 0xC). The container's key handler, then Enter the next page
    // (Finish on the last), Esc or the help key (action ';') Finish; each sets the handled flag.
    void OnKeyDown(int key, int repeat, int flags);
    // 00659420 (Rebirth-patched; through its address). Shows page page_.
    void ShowPage();
    // 00659420's vanilla body (plain RET; ui_help_page.cpp, A-H26; hooked in the vanilla launch
    // only). Empties the two lists, the surfaces, regions and scene objects, opens "tutorial
    // <text_>" (without it the pending value is set to 0) and goes to the page's "#screen" line.
    // The page's rectangle is (200, 50)-(600, 560), or "#position left top right [rows]" (at least
    // 300 wide, rows x 16 + 110 high, at most 600). Scene objects 0 the plain background, 1 the
    // frame, 2 / 3 / 4 the previous, OK and next buttons; then one pulsing frame per "#rectangle
    // left top right bottom" line and one entry per "#text [picture]" line (the next line's first
    // word is the text's key). Surfaces: 0 the page's title and "title (page/pages)", 1 the
    // entries' wrapped "tutor <key>" texts (laid out twice when no rows are given: the first pass
    // makes the pictures, "tutorial_<picture>" copied into referenced object 1's 64-pixel cells,
    // and gives the height), 2 the buttons' texts with the help key's name. Regions 0-2 the
    // buttons.
    void ShowPageVanilla();

    bool message_;                       // +0x164 a message window (ShowMessage), not help
    std::uint8_t unknown_165[3];
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
    // 005E2670 (ui_window_delete.cpp). Destructor: the surface released and freed (cleared),
    // then the two strings.
    void Destroy();

    mfc::CString text;          // +0x00 (AddLine leaves it empty)
    mfc::CString name;          // +0x04 the line's text without its colour codes
    std::uint32_t time;         // +0x08 timeGetTime when it was added
    CUiSurface* surface;        // +0x0C
};
static_assert(sizeof(ChatLine) == 0x10);

// The in-game chat overlay (vtables 007400DC / 007400A8; made by CInterface3D::Create 005F4990,
// constructor 005E26F0): the interface's root window (CInterface3D +0x1C), updated after the top
// window every frame; in a multiplayer client it holds the chat line, which takes the keys while
// it is open.
class CChatOverlay : public CUiWindow {
public:
    // 005E28E0 (ui_window_delete.cpp). The destructor body: the typed line, every chat line
    // (destroyed, node freed), the list's sentinel, then the window's.
    void DestroyBody();
    // 005E28C0 (slot 0, the deleting destructor; ui_window_delete.cpp). DestroyBody, then the
    // memory freed when bit 0 of `flags` is set; returns this.
    CChatOverlay* ChatOverlayDelete(unsigned flags);
    // 005E26F0 (RET 4; ui_chat_overlay.cpp). Constructor: the window base, an empty line list
    // (its sentinel from the node pool), an empty typed line, the vtables 007400DC / 007400A8, the
    // typed line's surface over (10, 573)-(790, 590) (format 4), closed, the caret at 0, then the
    // typed line drawn (005E3700, Rebirth-patched). Returns this.
    CChatOverlay* Construct(CInterface3D* ui);
    // 005E2A30 (RET 8; ui_chat_overlay.cpp; CInterface3D::OnChatLine). Adds a chat line from
    // player `number` (unused): the text is broken into lines narrower than 780 pixels (font 1;
    // at spaces, a word wider than a line on its own), "^<digit>" switching the colour (a missing
    // digit is taken as 7); the oldest lines are erased so that at most 12 remain; each new line
    // gets the time and a surface (its width + 3 by 17, format 5) with its pieces drawn in their
    // colours (the digit's bits 0 / 1 / 2 give blue / green / red, white to start).
    void AddLine(const char* text, std::uint32_t number);
    // 005E36C0 (RET 4). Opens (`open`, a byte: the typed line emptied, the caret at 0) or closes the
    // chat line, then draws it (005E3700).
    void SetOpen(bool open);
    // 005E3930. Every line destroyed and its node returned to the pool; the list left empty.
    void ClearLines();
    // 005E3700 (Rebirth-patched, this is the vanilla body, hooked in the vanilla launch only;
    // rewritten callers go through ChatOverlayDrawTypedOriginal). The typed line's surface hidden
    // while the chat is closed; else shown, the caret clamped to the line, the surface cleared,
    // the line drawn (font 1, white, DT_NOPREFIX, shadowed) over (0, 0)-(783, 17) and, while the
    // text before the caret is narrower than 780, the caret as a white line at its width from y
    // 0 to 16 (layout points scaled to the screen).
    void DrawTyped();
    // Input side (ui_chat_overlay.cpp; nothing while the chat is closed). 005E3230 (slot 9, RET
    // 0xC): every key is marked used; Backspace / Delete remove the character before / at the
    // caret, Home / End / Left / Right move it (unclamped; the draw clamps it), Enter sends a
    // non-empty line as a chat message (a network client: SayMessage kept 60 s, 0041D1A0) and,
    // like Esc, closes the chat; then the line is drawn. 005E3550 (slot 10, RET 0xC): a character
    // other than 8, 9, 13 and 27 is inserted at the caret unless Ctrl is down, the line is 200
    // characters or more, or the new line would be 780 pixels wide or more (font 1, 005F8340);
    // then the line is drawn. The character is not marked used.
    void OnKeyDown(int key, int repeat, int flags);
    void OnChar(int key, int repeat, int flags);
    // 005E2990 (slot 24). Lines older than 20 s (timeGetTime) are erased.
    void Update();
    // 005E29E0 (slot 22). Each line's surface placed at (10, 10 + 16 * row) and blitted, then the
    // window's own redraw (005FECC0).
    void Redraw();

    stlp::list<ChatLine> lines_;     // +0x148
    bool chatOpen_;                  // +0x14C a multiplayer client's chat line is open (it takes the keys)
    std::uint8_t unknown_14D[3];
    mfc::CString typed_;             // +0x150
    int caret_;                      // +0x154 in the typed line (characters)
};
static_assert(offsetof(CChatOverlay, lines_) == 0x148);
static_assert(offsetof(CChatOverlay, caret_) == 0x154);
static_assert(offsetof(CChatOverlay, typed_) == 0x150);

// The new game's difficulty question (vtables 0074225C / 00742228, 0x184 bytes; the main menu's
// region 0): a message box ("option_difficulty", OK and cancel) with two check boxes. Its button
// handler sets easy_ from the boxes (regions 2 / 3) and copies it to GameDifficulty() on OK.
class CDifficultyBox : public CMessageBox {
public:
    // 00633DD0 (input side slot 2, RET 0xC; ui_message_box.cpp; no container handler). Region 0
    // OK: easy_ to GameDifficulty(), the OK sound, result_ | 1 pending; 1 cancel: the cancel
    // sound, result_ | 2; 2 / 3 check easy / hard (the checked box's sprite shows the texture
    // shifted by kCheckedOffset, the other unshifted).
    void OnLButtonDown(int buttons, int x, int y);
    // 00633EC0 (input side slot 9, RET 0xC). The container's handler first; unless a child used
    // the key, Enter answers as OK and Esc as cancel, and the key is marked used.
    void OnKeyDown(int key, int repeat, int flags);
    // The check boxes: the first two sprites (Build adds them first).
    CUiMesh* EasyBox() const { return reinterpret_cast<CUiMesh*>(sceneObjects_.data[0]); }
    CUiMesh* HardBox() const { return reinterpret_cast<CUiMesh*>(sceneObjects_.data[1]); }
    // The texture shift of a checked box (0xBDB80000).
    static constexpr float kCheckedOffset = -0.08984375f;

    // 006337B0 (slot 25, ui_layouts.cpp). Builds the box: the "saveload" textures, a surface over
    // 200..600 x 100..500 with the title, the "tip option_difficulty" text and the
    // "difficulty_easy" / "difficulty_hard" labels; the check boxes beside them (hard checked),
    // the shade, frame and OK / cancel buttons as the message box's; regions OK (tip 0xC3B4),
    // cancel (0xC3B5), easy, hard.
    void Build();

    int easy_;                       // +0x180 1 easy, 0 hard (the default)
};
static_assert(offsetof(CDifficultyBox, easy_) == 0x180);
static_assert(sizeof(CDifficultyBox) == 0x184);

// The mouse (007C7C50: the position in screen pixels; 007C7C60: the motion a right-button drag
// accumulates, read and cleared by the drag's mouse-move handlers 00630010 / 005EB210).
inline int& MouseScreenX() { return *reinterpret_cast<int*>(0x007C7C50); }
inline int& MouseScreenY() { return *reinterpret_cast<int*>(0x007C7C54); }
inline int& MouseDragX() { return *reinterpret_cast<int*>(0x007C7C60); }
inline int& MouseDragY() { return *reinterpret_cast<int*>(0x007C7C64); }

// 0079BEC0: the game's difficulty (1 easy, 0 hard; combat reads it).
inline int& GameDifficulty() { return *reinterpret_cast<int*>(0x0079BEC0); }
