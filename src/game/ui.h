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

    int width_;                      // +0x00 screen width
    int height_;                     // +0x04 screen height
    std::uint8_t unknown_08[0x34 - 0x08];
    CUiWindow** stackBegin_;         // +0x34 stack of active windows (STLport vector)
    CUiWindow** stackEnd_;           // +0x38
    CUiWindow** stackCapacity_;      // +0x3C
    std::uint8_t unknown_40[0x48 - 0x40];
    std::uint32_t pendingValue_;     // +0x48
    bool pendingFlag_;               // +0x4C
    std::uint8_t unknown_4D[0x68 - 0x4D];
    int mouseX_;                     // +0x68 mouse position in screen coordinates
    int mouseY_;                     // +0x6C
};
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
    void** inputVtable_;                      // +0x94 secondary vtable (input handlers)
    CInterface3D* interface_;                 // +0x98
    std::uint8_t unknown_09C[0xD8 - 0x09C];
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
