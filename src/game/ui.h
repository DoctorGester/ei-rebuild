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

    // 005F7590. The window on top of the active-window stack, or null.
    CUiWindow* GetActiveWindow() const;
    // 005F74E0. Stores a value and raises the flag next to it (consumed by the frame update).
    void SetPendingValue(std::uint32_t value);

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
    // 005FD120 (not rewritten). Destructor body: releases the DirectDraw surface.
    void ReleaseContents();

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
    void RemoveSceneObjectAt(int index);
    void ClearSceneObjects();

    // 005FFA90 / 005FFBF0 / 005FFC50. Surfaces the window owns.
    void AddSurface(CUiSurface* surface);
    void RemoveSurfaceAt(int index);
    void ClearSurfaces();

    // 005FF830 / 005FF9C0 / 005FFA20. Regions (AddRegion returns the new, empty region).
    CUiRegion* AddRegion();
    void RemoveRegionAt(int index);
    void ClearRegions();

    // 005FFD80 / 005FFF40. 3D model previews.
    CUiModel* AddModel();
    void ClearModels();

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
    // 00638EF0 (Rebirth-patched, not rewritten). Selects a party member (portrait click).
    void SelectUnit(CUnit* unit, int a, int b);
    // 00639EF0 (not rewritten). Shows the unit under the mouse in the unit panel.
    void HoverUnit(CUnit* unit, int a, int b);

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

    CBattleScreen* owner_;           // +0x148
    int count_;                      // +0x14C slots in use
    int selected_;                   // +0x150
    CUnit* unit_;                    // +0x154
};
static_assert(offsetof(CHudWeaponBar, unit_) == 0x154);

// Quick items (constructor 0063C800, vtables 0074283C / 00742808): 2 rows of four 50x50 slots
// at (520..720, 500..600).
class CHudQuickItems : public CUiWindow {
public:
    void OnMouseDown(int buttons, int x, int y);   // 0063DAA0
    void OnDoubleClick(int buttons, int x, int y); // 0063DAE0
    void OnMouseMove(int buttons, int x, int y);   // 0063E270
    BOOL Draw(int pass);                           // 0063CDC0
    // Item of the region at `index` (-1: empty). The original indexes this with the result of
    // RegionAt unchecked, so index -1 reads the dword before the array (+0x168).
    int RegionItem(int index) const {
        return reinterpret_cast<const int*>(reinterpret_cast<const std::uint8_t*>(this) + 0x16C)[index];
    }

    CBattleScreen* owner_;           // +0x148
    std::uint8_t unknown_14C[0x16C - 0x14C];
    int regionItem_[8];              // +0x16C
};
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

    CBattleScreen* owner_;           // +0x148
    int count_;                      // +0x14C
};
static_assert(offsetof(CHudSpellColumn, count_) == 0x14C);

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

    CBattleScreen* owner_;           // +0x148
    int count_;                      // +0x14C
};
static_assert(offsetof(CHudActionColumn, count_) == 0x14C);

// A portrait slot's network player (0x78 bytes, array at CHudPortraits +0x158).
struct HudPortraitSlot {
    std::uint8_t unknown_00[0x70];
    bool hasPlayer_;                 // +0x70
    std::uint8_t unknown_71[3];
    int playerId_;                   // +0x74
};
static_assert(sizeof(HudPortraitSlot) == 0x78);

// Party portraits (constructor 0063FE70, vtables 00742A18 / 007429E4; 250..550 x 440..600).
class CHudPortraits : public CUiWindow {
public:
    void OnMouseDown(int buttons, int x, int y);   // 00641CE0
    void OnMouseUp(int buttons, int x, int y);     // 00641E20: selects the unit
    void OnMouseMove(int buttons, int x, int y);   // 00641EA0: shows the unit / sets the cursor

    CBattleScreen* owner_;           // +0x148
    int count_;                      // +0x14C
    std::uint8_t unknown_150[0x158 - 0x150];
    HudPortraitSlot* slots_;         // +0x158
};
static_assert(offsetof(CHudPortraits, slots_) == 0x158);

// Map (set up by 00641F20, vtables 00742660 / 0074262C; 615..800 x 0..165). Regions: 0 the
// sling button, 1 the round map, 2 the map area, 3 and 4 the two view buttons, 5 a button
// above them, 6 the whole panel.
class CHudMap : public CUiWindow {
public:
    void OnMouseDown(int buttons, int x, int y);   // 00645060: the view buttons
    void OnMouseMove(int buttons, int x, int y);   // 00645610

    CBattleScreen* owner_;           // +0x148
    std::uint8_t unknown_14C[0x1AC - 0x14C];
    int view_;                       // +0x1AC 1 or 2 (scene objects 6 and 7 are the buttons)
};
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
};

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

    CBattleScreen* owner_;           // +0x148
    int sliding_;                    // +0x14C
    std::uint8_t unknown_150[0x15C - 0x150];
    float slideDirection_;           // +0x15C
    int page_;                       // +0x160
};
static_assert(offsetof(CHudTopPanel, slideDirection_) == 0x15C);
static_assert(offsetof(CHudTopPanel, page_) == 0x160);
