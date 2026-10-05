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

#include <cstddef>
#include <cstdint>

#include <windows.h>

class CUiWindow;
class CUiMesh;

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
    // 005F7E60 (not rewritten). Plays an interface sound (`name`, e.g. "buttons\battle\click.wav");
    // the interface object itself is not used. Returns what the sound system returns.
    int PlaySound(const char* name, int flag);

    int width_;                      // +0x00 screen width
    int height_;                     // +0x04 screen height
    struct UiRenderer* renderer_;    // +0x08 (its +0x54 is the Direct3D 7 device)
    void* mouseCapture_;             // +0x0C input side (window +0x94) that took the button press
    std::uint8_t unknown_10[0x34 - 0x10];
    CUiWindow** stackBegin_;         // +0x34 stack of active windows (STLport vector)
    CUiWindow** stackEnd_;           // +0x38
    CUiWindow** stackCapacity_;      // +0x3C
    std::uint8_t unknown_40[0x48 - 0x40];
    std::uint32_t pendingValue_;     // +0x48
    bool pendingFlag_;               // +0x4C
    std::uint8_t unknown_4D[0x68 - 0x4D];
    int mouseX_;                     // +0x68 mouse position in screen coordinates
    int mouseY_;                     // +0x6C
    std::uint8_t unknown_70[0x7C - 0x70];
    bool keyShift_;                  // +0x7C \ key-modifier flags (names provisional; the
    std::uint8_t keyFlag7D_;         // +0x7D | battle screen tests +0x7F to choose between two
    bool keyFlag7E_;                 // +0x7E | actions on a spell or quick-item click)
    bool keyCtrl_;                   // +0x7F /
};
static_assert(offsetof(CInterface3D, mouseCapture_) == 0x0C);
static_assert(offsetof(CInterface3D, keyCtrl_) == 0x7F);
static_assert(offsetof(CInterface3D, stackBegin_) == 0x34);
static_assert(offsetof(CInterface3D, pendingValue_) == 0x48);
static_assert(offsetof(CInterface3D, mouseX_) == 0x68);

// The interface's growable pointer arrays (an MFC CArray-like template the game inlines): the
// storage is aligned to 32 bytes, with the pointer returned by operator new stored just before it.
// Growth: by growBy_ when set, otherwise by size / 8 clamped to [4, 1024].
template <typename T>
struct UiPtrArray {
    T** data_;
    int size_;
    int capacity_;
    int growBy_;

    // 006003D0, 006004E0, 00600730, 00600840, 00600950 (one instantiation per array). Sets the
    // size (new slots are not initialised); `growBy` -1 keeps the current growth step.
    void SetSize(int newSize, int growBy);

    // Inlined by the original at each Add call site: appends `item` (null included).
    void Append(T* item);
    // 006005F0 (RET 8). Stores `item` at `index`, growing the array to index + 1 first when it is
    // that short (SetSize, inlined).
    void SetAtGrow(int index, T* item);
    // Inlined by the original: removes the slot at `index`, shifting the rest down.
    void RemoveSlot(int index);
    // Inlined by the original: frees the storage and empties the array.
    void FreeStorage();
};
static_assert(sizeof(UiPtrArray<void>) == 0x10);

// Allocates storage for `bytes` with the interface arrays' 32-byte alignment (00600A80 for 4-byte
// elements, 004F65B0 for 8-byte ones); FreeAligned releases it.
void* AllocAligned(std::size_t bytes);
void FreeAligned(void* data);

// The tooltip attached to a region (0x184 bytes, constructed by 0065E320).
struct CUiTooltip {
    void** vtable_;                  // slot 0: deleting destructor
    std::uint8_t unknown_004[0x180 - 0x004];
    int state_;                      // +0x180 0, 1: idle/hovered; other values: shown
    // 0065E580. Tracks the mouse (layout coordinates).
    void OnMouseMove(int x, int y);
    // 0065EA50 (window slot 20; the tooltip is a CUiWindow, vtable 00742F64). Draws only while
    // shown; returns 1 otherwise. (ui_menus.cpp)
    BOOL Draw(int pass);
    // 0065EA80 (slot 22). Redraws (005FECC0, the window's base slot 22) only while shown.
    void Redraw();
    bool Shown() const { return state_ != 1 && state_ != 0; }
};
static_assert(offsetof(CUiTooltip, state_) == 0x180);

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
    // 005FC600 (not rewritten). Releases the tooltip and the vertex storage.
    void ReleaseContents();
    // 005FC690 (not rewritten). Sets the tooltip's text (when there is a tooltip).
    void SetTipText(const char* text);
    // 005FC730 (not rewritten). Gives the region the tooltip with text "tip_<id>" (0: none).
    void SetTip(int textId, int a, int b);
    // 005FC6B0 (not rewritten). Gives the region a tooltip showing `text`.
    void SetTipString(const char* text, int a, int b);
    // Layout code calls SetRect through its original address: Rebirth patches it (rule 9).
    void CallSetRect(int left, int top, int right, int bottom);   // -> 005FC9E0

    CInterface3D* interface_;        // +0x00
    UiPoint* points_;                // +0x04 polygon vertices (aligned storage)
    int pointCount_;                 // +0x08
    int pointCapacity_;              // +0x0C
    int pointGrowBy_;                // +0x10
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
    // 005FD120 (ui_surface_create.cpp). Destructor body: releases the DirectDraw surface.
    void ReleaseContents();
    // 005FD760 (not rewritten). Sets (`frozen`) or clears flag 4 of +0x0C; setting it saves the
    // surface's contents when it has any (005FD7A0), clearing it restores them when `restore`.
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

    void* vtable_;
    CInterface3D* interface_;        // +0x04
    std::uint8_t unknown_08[0x24 - 0x08];
    RECT rect_;                      // +0x24 screen coordinates
    std::uint8_t unknown_34[0x38 - 0x34];
};
static_assert(offsetof(CUiSurface, rect_) == 0x24);
static_assert(sizeof(CUiSurface) == 0x38);

// A 3D model shown in a window (an inventory item, a portrait): one pointer to a scene object.
class CUiModel {
public:
    // 005FC450. Places the object in front of the camera at a layout position and depth.
    void SetScreenPosition(float x, float y, float depth);
    // 005FC1D0 (not rewritten). Releases the object.
    void ReleaseObject();
    // 005FC2F0 (not rewritten). Draws the object.
    void Draw(int pass);
    // Not rewritten: 005FC1F0 loads the model (`name`, its layout and label parts), 005FC4F0
    // shows or hides all its parts, 005FC4C0 one part by name.
    void Load(const char* name, const char* layout, const char* labels);
    void ShowAllParts(int show);
    void ShowPart(const char* name, int show);

    CTransformObject* object_;
};
static_assert(sizeof(CUiModel) == 4);

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
    // Takes the mouse for the next button-up (what every button-down handler does).
    void CaptureMouse() { interface_->mouseCapture_ = InputSide(); }
    // Button-up handlers act only when they took the mouse; they release it first.
    bool ReleaseMouse() {
        if (interface_->mouseCapture_ != InputSide()) return false;
        interface_->mouseCapture_ = nullptr;
        return true;
    }
    // 005FEB90 (not rewritten). Base drawing (virtual slot 20): draws the scene objects and the
    // models; returns 1.
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
    // Layout helpers (not rewritten): 005FEF20 loads a texture into the referenced objects,
    // 005FF0B0 creates an empty one, 005FF3C0 adds a scene object (`model` 0: a new interface
    // sprite, see CUiMesh) and returns it.
    int LoadTexture(const char* name, int a, int b);
    CEngineObject* CreateTexture(int width, int height, int format);
    CUiMesh* AddSprite(int model);
    // 005FECC0 (not rewritten). Base slot 22 (redraw).
    void RedrawBase();

    void** inputVtable_;                      // +0x94 secondary vtable (input handlers)
    CInterface3D* interface_;                 // +0x98
    bool inputHandled_;                       // +0x9C set by a handler that used the event
    std::uint8_t unknown_09D[0xD8 - 0x09D];
    RECT rect_;                               // +0xD8 layout coordinates
    int enabled_;                             // +0xE8
    std::uint8_t unknown_0EC[0xF8 - 0x0EC];
    UiPtrArray<CEngineObject> objects_;       // +0xF8 referenced objects
    UiPtrArray<CEngineObject> sceneObjects_;  // +0x108
    UiPtrArray<CUiSurface> surfaces_;         // +0x118
    UiPtrArray<CUiRegion> regions_;           // +0x128
    UiPtrArray<CUiModel> models_;             // +0x138
};
static_assert(offsetof(CUiWindow, interface_) == 0x98);
static_assert(offsetof(CUiWindow, inputHandled_) == 0x9C);
static_assert(sizeof(CUiWindow) == 0x148);
static_assert(offsetof(CUiWindow, rect_) == 0xD8);
static_assert(offsetof(CUiWindow, enabled_) == 0xE8);
static_assert(offsetof(CUiWindow, objects_) == 0xF8);
static_assert(offsetof(CUiWindow, regions_) == 0x128);
static_assert(offsetof(CUiWindow, models_) == 0x138);

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

// The interface's renderer (CInterface3D +0x08), only as the HUD uses it.
struct UiD3DDevice {
    void** vtable_;   // IDirect3DDevice7: slot 20 SetRenderState, 21 GetRenderState
};
struct UiRenderer {
    // 004E7840 / 004E7A40 (not rewritten). Flush the two batched vertex buffers.
    void FlushBatchA();
    void FlushBatchB();

    std::uint8_t unknown_00[0x54];
    UiD3DDevice* device_;            // +0x54
};
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

    std::uint8_t unknown_148[0x16C - 0x148];
    CUnit** party_;                  // +0x16C the player's units shown as portraits
    int partyCount_;                 // +0x170
    std::uint8_t unknown_174[0x22C - 0x174];
    int portraitMode_;               // +0x22C 0: normal; 1, 2: the next portrait click picks a
                                     //        player for a server command (4, 5)
};
static_assert(offsetof(CBattleScreen, party_) == 0x16C);
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
    // 0063C050 (Rebirth-patched, not rewritten). Highlights slot `index` (or not).
    void Highlight(int index, int on);
    // 0063C180 (RET 4; ui_hud_tips.cpp). Shows `unit`'s weapons: sprites dropped, the four
    // regions' tips cleared; per weapon a sprite (0063BCC0, Rebirth-patched) and the tip of its
    // region (id 0x26 + slot): the name, damage range and type, the prototype's +0x6C and the
    // weapon's +0x58 and, for a weapon with a spell, the spell's name; then the unit's +0x270
    // highlighted.
    void SetUnit(CUnit* unit);

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
    BOOL Draw(int pass);                           // 0063CDC0
    // 0063D9D0 (slot 24, every frame). Fills the tooltip of each quick item slot that shows one.
    void Update();
    // 0063CEE0 (RET 8; ui_hud_tips.cpp). The tooltip text of quick item `index` into `text` (a
    // CString): the item's name and, for a wand (category 5) or a scroll (8) with a spell, its
    // numbers after labels_.
    void ItemTip(unsigned index, void* text);
    // The same through the original's address (so the verify wrapper sees the call).
    void ItemTipAt(unsigned index, void* text);
    // Item of the region at `index` (-1: empty). The original indexes this with the result of
    // RegionAt unchecked, so index -1 reads the dword before the array (+0x168).
    int RegionItem(int index) const {
        return reinterpret_cast<const int*>(reinterpret_cast<const std::uint8_t*>(this) + 0x16C)[index];
    }

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

// Spell column on the right (constructor 0063E2A0, vtables 007428E4 / 007428B0;
// 760..800 x 190..510).
class CHudSpellColumn : public CUiWindow {
public:
    void OnMouseDown(int buttons, int x, int y);   // 0063F1E0
    void OnDoubleClick(int buttons, int x, int y); // 0063F220
    void OnMouseUp(int buttons, int x, int y);     // 0063F260
    void OnMouseMove(int buttons, int x, int y);   // 0063F2D0
    BOOL Draw(int pass);                           // 0063F190
    // 0063EC90 / 0063EF60 (not rewritten). Click on spell `index`, without / with key flag +0x7F.
    void Click(int index);
    void ClickAlternate(int index);
    // 0063E820 (RET 4; ui_spell_column.cpp). Shows `unit`'s spells: sprites and regions dropped;
    // for each of the unit's eight spell slots that holds one, a sprite (0063E4B0, Rebirth-patched)
    // and the tooltip of region `index` (tip id 0x1E + index): the spell's name and, when it has
    // its data (+0x44), seven lines of numbers each after a label of labels_ (some numbers
    // replaced by labels when absent: an area for flag 0x10000000, a time under 2 ticks).
    void SetUnit(CUnit* unit);

    CBattleScreen* owner_;           // +0x148
    int count_;                      // +0x14C sprites added
    int selected_;                   // +0x150
    CUnit* unit_;                    // +0x154
    void* labelsVtable_;             // +0x158 CStringArray: the tooltip's labels
    char** labels_;                  // +0x15C
};
static_assert(offsetof(CHudSpellColumn, count_) == 0x14C);
static_assert(offsetof(CHudSpellColumn, labels_) == 0x15C);

// Action column on the left (constructor 0063F300, vtables 00742980 / 0074294C; six 40x40
// buttons at 0..40 x 270..510).
class CHudActionColumn : public CUiWindow {
public:
    void OnMouseDown(int buttons, int x, int y);   // 0063FDA0
    void OnMouseUp(int buttons, int x, int y);     // 0063FDE0
    void OnMouseMove(int buttons, int x, int y);   // 0063FE40
    BOOL Draw(int pass);                           // 0063FD50
    // 0063FB50 (not rewritten). Selects action `index`.
    void Select(int index);
    // 0063F800 (RET 8; ui_hud_tips.cpp). Shows `unit`'s actions: sprites and the action array
    // dropped, the six regions' tips cleared; with `extra` (its low byte) first a "skill0011"
    // button (tip 0x290E); with a unit the science button ("skill0009", action 0) whose tip is
    // the "perk Science" text, the unit's science level (+0x408) and the "tip 10511" text.
    void SetUnit(CUnit* unit, int extra);

    CBattleScreen* owner_;           // +0x148
    int count_;                      // +0x14C buttons added
    int selected_;                   // +0x150
    CUnit* unit_;                    // +0x154
    std::uint8_t actions_[0x14];     // +0x158 CDWordArray: the buttons' actions (0063F500 adds)
    void* labelsVtable_;             // +0x16C CStringArray: the tooltip's labels
    char** labels_;                  // +0x170
};
static_assert(offsetof(CHudActionColumn, count_) == 0x14C);
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
    bool hasPlayer_;                 // +0x70
    std::uint8_t unknown_71[3];
    int playerId_;                   // +0x74
};
static_assert(sizeof(HudPortraitSlot) == 0x78);
static_assert(offsetof(HudPortraitSlot, wobbleKind_) == 0x38);
static_assert(offsetof(HudPortraitSlot, wobbleTarget_) == 0x5C);

// Party portraits (constructor 0063FE70, vtables 00742A18 / 007429E4; 250..550 x 440..600).
class CHudPortraits : public CUiWindow {
public:
    void OnMouseDown(int buttons, int x, int y);   // 00641CE0
    void OnMouseUp(int buttons, int x, int y);     // 00641E20: selects the unit
    void OnMouseMove(int buttons, int x, int y);   // 00641EA0: shows the unit / sets the cursor
    // 00641080 (slot 24, every frame): health and mana bars, the hit flash, the face picture and
    // model, the face's wobble and the selection marker (ui_hud_portraits.cpp).
    void Update();
    // 00640670 (not rewritten). Draws face picture `state` for portrait `index`.
    void ShowFace(int index, int state);
    // 005FF460 (CUiWindow::ReplaceSceneObject, called through its address). Replaces scene
    // object `object` with a model loaded by `name` (keeping its placement); returns the new
    // object or 0.
    CEngineObject* ReplaceSceneObject(CEngineObject* object, const char* name);

    CBattleScreen* owner_;           // +0x148
    int count_;                      // +0x14C
    std::uint8_t unknown_150[0x158 - 0x150];
    HudPortraitSlot* slots_;         // +0x158
};
static_assert(offsetof(CHudPortraits, slots_) == 0x158);

// A 2D mesh of the interface (the sprite objects 005E70E0 and its siblings set up). Positions
// are kept per quad as four x then four y floats; each vertex has a 6-byte record (the position
// it uses and a frame of the texture); indices are 16 bits. The arrays grow like UiPtrArray.
struct UiQuadBlock {
    float x[4];
    float y[4];
    std::uint8_t unknown_20[0x20];
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
    // Not rewritten: array sizes (005EA210 quads, 005EA600 vertices, 005EA4C0 indices) and
    // Commit (005EA150), which hands the arrays to the renderer fields (+0x9C..+0xC8) and
    // returns the index count.
    void SetQuadCount(int count);
    void SetVertexCount(int count);
    void SetIndexCount(int count);
    void SetUvCount(int count);      // 005EA380 texture coordinates (8 bytes each)
    int Commit();
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

    std::uint8_t unknown_000[0x1C0];
    UiQuadBlock* quads_;             // +0x1C0
    int quadCount_;                  // +0x1C4
    std::uint8_t unknown_1C8[0x1E0 - 0x1C8];
    UiMeshVertex* vertices_;         // +0x1E0
    int vertexCount_;                // +0x1E4
    std::uint8_t unknown_1E8[0x200 - 0x1E8];
    std::uint16_t* indices_;         // +0x200
    int indexCount_;                 // +0x204
    std::uint8_t unknown_208[0x210 - 0x208];
    float* uvs_;                     // +0x210 u, v per vertex
    int uvCount_;                    // +0x214
    std::uint8_t unknown_218[0x220 - 0x218];
    int visible_;                    // +0x220
};
static_assert(offsetof(CUiMesh, quads_) == 0x1C0);
static_assert(offsetof(CUiMesh, vertices_) == 0x1E0);
static_assert(offsetof(CUiMesh, indices_) == 0x200);
static_assert(offsetof(CUiMesh, uvs_) == 0x210);
static_assert(offsetof(CUiMesh, visible_) == 0x220);

// A growable array of 2D points with the interface arrays' 32-byte alignment.
struct UiPointF {
    float x;
    float y;
};
struct UiPointArray {
    // 00453100 (not rewritten). Sets the size; `growBy` -1 keeps the growth step.
    void SetSize(int size, int growBy);

    UiPointF* data_;
    int size_;
    int capacity_;
    int growBy_;
};
static_assert(sizeof(UiPointArray) == 0x10);

// Map (set up by 00641F20, vtables 00742660 / 0074262C; 615..800 x 0..165). Regions: 0 the
// sling button, 1 the round map, 2 the map area, 3 and 4 the two view buttons, 5 a button
// above them, 6 the whole panel.
class CHudMap : public CUiWindow {
public:
    void OnMouseDown(int buttons, int x, int y);   // 00645060: the view buttons
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
    std::uint8_t unknown_174[4];
    UiPointF* viewPolygon_;          // +0x178 the round map's outline in map units
    int viewPolygonCount_;           // +0x17C
    std::uint8_t unknown_180[0x188 - 0x180];
    UiPointArray visibleArea_;       // +0x188 the outline in world units, clipped to the world
    std::uint8_t unknown_198[0x1AC - 0x198];
    int view_;                       // +0x1AC 1 or 2 (scene objects 6 and 7 are the buttons)
};
static_assert(offsetof(CHudMap, centreX_) == 0x14C);
static_assert(offsetof(CHudMap, scale_) == 0x168);
static_assert(offsetof(CHudMap, zoomMax_) == 0x170);
static_assert(offsetof(CHudMap, visibleArea_) == 0x188);
static_assert(offsetof(CHudMap, view_) == 0x1AC);

// Unit panel (set up by 006456E0, vtables 007426F8 / 007426C4; 0..185 x 0..225). Regions: 0
// the sling button, 1-4 the body parts, 5 the whole panel.
class CHudUnitPanel : public CUiWindow {
public:
    void OnMouseDown(int buttons, int x, int y);   // 00648330
    void OnMouseUp(int buttons, int x, int y);     // 00648370
    void OnMouseMove(int buttons, int x, int y);   // 006484C0
    BOOL OnWheel(int buttons, int delta, int x, int y);   // 006484F0

    CBattleScreen* owner_;           // +0x148
    std::uint8_t unknown_14C[0x16C - 0x14C];
    int sliding_;                    // +0x16C set when the sling button starts the slide
    std::uint8_t unknown_170[0x17C - 0x170];
    float slideDirection_;           // +0x17C +1 / -1
    int bodyPart_;                   // +0x180 selected body part 0-3 (scene objects 3-6)
};
static_assert(offsetof(CHudUnitPanel, slideDirection_) == 0x17C);
static_assert(offsetof(CHudUnitPanel, bodyPart_) == 0x180);

// Game clock (set up by 00648520, vtables 007425C8 / 00742594; 720..800 x 520..600).
class CHudClock : public CUiWindow {
public:
    void OnMouseDown(int buttons, int x, int y);   // 00648D30
    void OnMouseMove(int buttons, int x, int y);   // 00649160
    // 006488E0 (slot 24, every frame): turns the face with the time of day, the hand and the
    // speed dial (scene objects 1-5), blinks while the game is paused (ui_hud_frame.cpp).
    void Update();

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
    // 00649810 (not rewritten). Region 0: toggles +0x438 of the selected units.
    void ToggleFlag();
    // 00649910 (Rebirth-patched, not rewritten). Highlights mode `mode` (-1/-2: none).
    void ShowMode(int mode);

    CBattleScreen* owner_;           // +0x148
    std::uint8_t unknown_14C[4];
    int flag_;                       // +0x150 common +0x438 of the selected units (-1, -2: none, mixed)
};
static_assert(offsetof(CHudMovePanel, flag_) == 0x150);

// Top panel (set up by 00649AF0, vtables 00742498 / 00742464; 180..620 x 0..100). Regions: 0
// the sling button, 1 and 2 the two page buttons, 3 the whole panel.
class CHudTopPanel : public CUiWindow {
public:
    void OnMouseDown(int buttons, int x, int y);   // 0064A1C0
    void OnMouseUp(int buttons, int x, int y);     // 0064A200
    void OnMouseMove(int buttons, int x, int y);   // 0064A2C0
    BOOL OnWheel(int buttons, int delta, int x, int y);   // 0064A2F0
    // 0064A320 (not rewritten). Shows page `page_`.
    void ShowPage();
    // 00649FF0 (slot 24, every frame): slides the panel in or out (ui_hud_frame.cpp).
    void Update();
    // 00649E20 (not rewritten). Places the panel's parts at the current slide offset.
    void Layout();

    CBattleScreen* owner_;           // +0x148
    int sliding_;                    // +0x14C
    int offset_;                     // +0x150 current slide offset in pixels
    int travel_;                     // +0x154 full slide distance
    float slide_;                    // +0x158 0 (out) .. 1 (in)
    float slideDirection_;           // +0x15C
    int page_;                       // +0x160
};
static_assert(offsetof(CHudTopPanel, slideDirection_) == 0x15C);
static_assert(offsetof(CHudTopPanel, page_) == 0x160);

// --- Screens with child windows: the save/load and options windows (ui_menus.cpp) -----------

// Base of the screens that hold child windows and extra input handlers (vtable 00741008,
// constructor 00600B20): the Esc menu, the save/load and options windows, the battle screen.
// Its input side's default handlers (00601110 button down, 006011F0 up, 006012D0 move,
// 00601570 double click, 00601820 key) pass the event on to the handlers and children.
class CUiContainer : public CUiWindow {
public:
    // 00600270 (not rewritten). Time since the last frame in seconds (timer ticks * 0.001f),
    // left unrounded in ST0.
    double FrameTime();
    // 006010E0 (not rewritten). Base update (slot 24): updates the children.
    void UpdateBase();
    // 00600EF0 (rewritten as a free function, ui_container_draws.cpp; this forwards to its address).
    // Base drawing (slot 20); returns 1.
    BOOL DrawContainer(int pass);
    // 00601040 (not rewritten). Base slot 22 (redraws the window and its children).
    void RedrawContainer();
    // 00600D30. Adds a child window (null is ignored; ui_window.cpp).
    void AddChild(CUiWindow* child);

    UiPtrArray<CUiWindow> children_;          // +0x148
    void** inputsBegin_;                      // +0x158 extra input sides (STLport vector)
    void** inputsEnd_;                        // +0x15C
    void** inputsCapacity_;                   // +0x160
};
static_assert(sizeof(CUiContainer) == 0x164);

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
    // Virtual slot 25 (0065B0C0 for the plain scroll bar): moves to `position`, clamped.
    void CallSetPosition(int position);
    // 0065AEA0 (not rewritten). Constructor: a bar over the layout rectangle `rect` (passed by
    // value), scrolling `speed` lines per second; returns this.
    CUiScrollbar* Construct(CInterface3D* ui, RECT rect, float speed, int a, float b);

    int maxPosition_;                // +0x148
    float speed_;                    // +0x14C lines per second while an arrow is held (/16)
    int position_;                   // +0x150
    std::uint8_t unknown_154[0x164 - 0x154];
    RECT thumb_;                     // +0x164 layout coordinates
    int grabX_;                      // +0x174 mouse offset from the thumb centre while dragging
    int grabY_;                      // +0x178
    float unknown_17C;               // +0x17C
    bool moved_;                     // +0x180
    std::uint8_t unknown_181[3];
    int state_;                      // +0x184 0 idle, 1 dragging the thumb, 2 up, 3 down
    float accumulated_;              // +0x188 scrolled amount not yet applied
};
static_assert(offsetof(CUiScrollbar, position_) == 0x150);
static_assert(offsetof(CUiScrollbar, thumb_) == 0x164);
static_assert(offsetof(CUiScrollbar, state_) == 0x184);
static_assert(offsetof(CUiScrollbar, accumulated_) == 0x188);

namespace mfc { class CString; }

// The pieces of a scrolling text (CUiScrollText below): a format change (0x14 bytes, vtable
// 00742F28, type 1) or a word (0x10 bytes, vtable 00742F20 over the base 00742F24, type 2).
struct UiTextToken {
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
    char* text_;                     // +0x08 CString
    int width_;                      // +0x0C its width in the token's font (005F8340)
};
static_assert(sizeof(UiWordToken) == 0x10);
// MFC CArray of token pointers (SetSize 0065E010).
struct UiTokenArray {
    void* vtable_;
    UiTextToken** data_;             // +0x04
    int size_;                       // +0x08
    int maxSize_;                    // +0x0C
    int growBy_;                     // +0x10
};

// A scrolling text: a scroll bar subclass (constructor 0065CC00) showing tokenised text; the
// battle screen's message log and the objectives window's quest log.
class CUiScrollText : public CUiScrollbar {
public:
    // 0065D4B0 (RET 4; ui_scroll_text.cpp). Appends the tokens of `text`: words (with their
    // widths), and before each run of newline, BEL C<rgb> and BEL F<digit> tokens one format
    // token.
    void Tokenize(const mfc::CString* text);
    // 0065D970 (RET 0xC; not rewritten). The token of `text` at `pos` (spaces and tabs skipped)
    // into `out`; returns the position after it, or -1 at the end.
    int NextToken(const mfc::CString* text, int pos, mfc::CString* out);

    std::uint8_t unknown_18C[0x19C - 0x18C];
    UiTokenArray tokens_;            // +0x19C
};
static_assert(offsetof(CUiScrollText, tokens_) == 0x19C);

// A spin control of the options window (its +0x0C value object).
struct UiSpinValue {
    // 0065BEB0 / 0065BEE0 (not rewritten). Value access.
    bool GetValue(int* value);
    void SetValue(int value);
};
struct UiSpin {
    std::uint8_t unknown_00[0x08];
    CUiScrollbar* bar_;              // +0x08 or null (the options window's Left / Right move it)
    UiSpinValue* value_;             // +0x0C
};

// A saved game in the save/load list (0x44 bytes, STLport vector at +0x164). Copy construction
// 00656640 (and 00657C60), assignment 006538E0, destructor 00656720 (not rewritten). The fields
// after the folder come from the save's info.sav (ui_saveload.cpp).
struct SaveEntry {
    char* title_;                    // +0x00 CString shown in the list
    char* folder_;                   // +0x04 CString folder name under saves\ ("saveq" quick save,
                                     //       "savea"/"saveb" automatic saves, "save<n>")
    std::int32_t savedAt_;           // +0x08 time() when it was saved (info.sav version 2 and up)
    float gameTime_;                 // +0x0C the "gtime" variable
    char* map_;                      // +0x10 CString: the map of the zone
    char* zone_;                     // +0x14 CString: the zone
    std::uint8_t stringArrays_[0x28];   // +0x18 two CStringArrays (+0x18, +0x2C), copied with the entry
    bool broken_;                    // +0x40 no info.sav: titled "Error Save", no picture
    std::uint8_t unknown_41[3];
};
static_assert(sizeof(SaveEntry) == 0x44);
static_assert(offsetof(SaveEntry, zone_) == 0x14);
static_assert(offsetof(SaveEntry, broken_) == 0x40);

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

    // Not rewritten.
    void Confirm();          // 00655290 OK: asks before overwriting, or loads
    void AskDelete();        // 00655380
    void DeleteSave();       // 00655B20 deletes the selected save's folder
    void RefreshList();      // 006567A0 (Rebirth-patched)

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
    // The selected save's title becomes the name being edited.
    void TakeSelectedTitle();

    // ui_menu_keys.cpp.
    // 006548A0 (input side, RET 0xC). Keys: Enter confirms, Esc cancels, Up / Down / PgUp / PgDn
    // move the selection; in a save window Backspace, Delete, Home, End, Left and Right edit the
    // save's name (the first edit of a save that is not a quick or automatic one takes over its
    // title, Backspace / Delete starting from an empty name).
    void OnKeyDown(int key, int repeat, int flags);
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

// A setting of the options window (0x14 bytes, 26 of them at +0x378).
struct OptionSetting {
    const int* definition_;          // +0x00 its first int is the setting's id
    int value_;                      // +0x04
    CUiScrollbar* bar_;              // +0x08 or null
    UiSpinValue* spin_;              // +0x0C or null
    std::uint8_t unknown_10[4];
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
    // 0064BE80 (slot 25, the window's Create; ui_objectives.cpp).
    void Build();
    // 0064CC10: shows zone `zone`: picture, exits, quests and description.
    void ShowZone(const char* zone);
    // 0064DC60 (__fastcall): when the selected quest changed, its description in the log and a
    // marker and region per place of the quest on the zone picture.
    void ShowQuest();
    // Not rewritten.
    void SelectExit(int index);      // 0064E990
    void RefreshQuests();            // 0064E8B0

    char** zonesBegin_;              // +0x164 STLport vector<CString> of the zones listed
    char** zonesEnd_;                // +0x168
    char** zonesCapacity_;           // +0x16C
    char* zoneName_;                 // +0x170 CString: the zone it was opened for
    int twoButtons_;                 // +0x174 OK on the left and a second button (else OK centred)
    void* exitDistances_;            // +0x178 STLport map<int, float>* (exit -> distance) or null
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
    int unknown_1C0;                 // +0x1C0
    float markerScale_;              // +0x1C4
    int unknown_1C8;                 // +0x1C8
    // +0x1CC STLport vector of the selected quest's places (8-byte points); RefreshQuests
    // (0064DC60) gives each a sprite and a region after the exits'.
    UiPointF* questPlacesBegin_;     // +0x1CC
    UiPointF* questPlacesEnd_;       // +0x1D0
    std::uint8_t unknown_1D4[0x1D8 - 0x1D4];
    std::uint8_t objectiveTexts_[0x14];   // +0x1D8 CStringArray of obj_0..obj_2
    std::uint32_t savedAlphaRef_;    // +0x1EC
};
static_assert(offsetof(CObjectivesWindow, zonesBegin_) == 0x164);
static_assert(offsetof(CObjectivesWindow, exits_) == 0x18C);
static_assert(offsetof(CObjectivesWindow, bar_) == 0x1BC);
static_assert(sizeof(CObjectivesWindow) == 0x1F0);

// A key binding row of the options window (0x18 bytes, vector at +0x36C; rows_ point at them).
struct OptionBinding {
    const int* key_;                 // +0x00 its first int is the bound key code
    std::uint8_t unknown_04[0x0C];
    std::uint32_t name_;             // +0x10 passed to "key_already_mapped_msg" (a string)
    std::uint8_t unknown_14[4];
};
static_assert(sizeof(OptionBinding) == 0x18);

// The game's table of key names (0x48 bytes each, 88 of them from 007857B0 to 00787070).
struct UiKeyName {
    int scan_;                       // +0x00 scan code
    std::uint8_t unknown_04[4];
    char name_[0x40];                // +0x08
};
static_assert(sizeof(UiKeyName) == 0x48);

// A row of the options window: a key binding or a spin control.
struct OptionRow {
    void* binding_;
    UiSpin* spin_;
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

    // Not rewritten.
    void RefreshRows();      // 00651350
    void ShowGroup(int group);   // 00651D70
    void CommitBinding();    // 00652000
    void Apply();            // 006524A0 OK: applies and stores the settings
    // Cancel: puts back the volumes and the display settings the window started with.
    static void RestoreSettings();
    // 00651940 / 00652200 / 00651600 (not rewritten): the settings table (+0x378), their current
    // values, the key binding rows.
    void BuildSettings();
    void LoadSettings();
    void BuildBindings();
    // 006503C0 (slot 25, the window's Create): builds the window (ui_layouts.cpp).
    void Build();

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
    struct OptionBinding* bindingsBegin_;   // +0x36C STLport vector of the key binding rows
    struct OptionBinding* bindingsEnd_;     // +0x370
    std::uint8_t unknown_374[0x378 - 0x374];
    struct OptionSetting* settings_; // +0x378 26 settings
    std::uint8_t unknown_37C[0x384 - 0x37C];
    OptionRow rows_[14];             // +0x384
    bool waitingForKey_;             // +0x3F4 a key binding row waits for the new key
    bool sliding_;                   // +0x3F5
    std::uint8_t unknown_3F6[2];
    float slide_;                    // +0x3F8
    std::uint8_t groupNames_[0x14];  // +0x3FC CStringArray: the option groups' titles
    bool displayModeChanged_;        // +0x410 a display mode setting (7-9) changed
    std::uint8_t unknown_411[3];
    int unknown_414;                 // +0x414 cleared by Build; OnKeyDown keeps the UiKeyName* of
                                     //        the key pressed for a binding there (null: none)
};
static_assert(offsetof(COptionsWindow, settings_) == 0x378);
static_assert(offsetof(COptionsWindow, displayModeChanged_) == 0x410);
static_assert(offsetof(COptionsWindow, selectedRow_) == 0x364);
static_assert(offsetof(COptionsWindow, bindingsEnd_) == 0x370);
static_assert(offsetof(COptionsWindow, rows_) == 0x384);
static_assert(offsetof(COptionsWindow, waitingForKey_) == 0x3F4);
static_assert(offsetof(COptionsWindow, slide_) == 0x3F8);
static_assert(sizeof(COptionsWindow) == 0x418);

// The message box (constructor 00634650: interface, title and text keys, result, flags, format;
// vtables 00742348 / 00742314, 0x180 bytes; the new game's difficulty question 0074225C is built
// on it with its own Create 006337B0). Flags: 1 an OK button, 2 with 1 also a cancel button (neither: one
// cancel button), 4 an edit field instead of the text (its initial text is the text key itself).
class CMessageBox : public CUiContainer {
public:
    // 00634790 (slot 25, ui_layouts.cpp). Builds the box: the "saveload" textures, a surface over
    // 200..600 x 50..500 with the title and the text (or the edit field) centred vertically as a
    // block, the dark shade and frame behind it and the buttons below (regions with tips
    // 0xC3B4 OK / 0xC3B5 cancel).
    void Build();

    const char* title_;              // +0x164 CString (text of the title key)
    const char* text_;               // +0x168 CString (text of the text key)
    std::uint32_t result_;           // +0x16C 0xFFFF0000: the text in font 1, else 2
    std::uint32_t flags_;            // +0x170
    std::uint32_t format_;           // +0x174 added to the text's DrawText format
    CUiWindow* edit_;                // +0x178 the edit field (flag 4)
    const char* editText_;           // +0x17C
};
static_assert(offsetof(CMessageBox, title_) == 0x164);
static_assert(offsetof(CMessageBox, edit_) == 0x178);
static_assert(sizeof(CMessageBox) == 0x180);

// The new game's difficulty question (vtables 0074225C / 00742228, 0x184 bytes; the main menu's
// region 0): a message box ("option_difficulty", OK and cancel) with two check boxes. Its button
// handler 00633DD0 (not rewritten) sets easy_ from the boxes (regions 2 / 3) and copies it to
// 0079BEC0 on OK.
class CDifficultyBox : public CMessageBox {
public:
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
