// Checks for the interface code (ui_interface.cpp, ui_window.cpp).
//
// Pure functions and those that only write an output run side by side with the original (the
// original into a scratch copy). Functions that allocate, free or call into other objects run
// only ours; the wrapper snapshots the state first and checks the result against what the
// original's code does.
#include "verify.h"

#include "../../src/game/ui.h"
#include "../../src/game/ui_widescreen.h"

#include <cstring>

namespace {
std::uint32_t Bits(float f) {
    std::uint32_t u;
    std::memcpy(&u, &f, sizeof(u));
    return u;
}

void* s_unused;

// A copy of the pointers of an interface array (enough for every window seen).
struct ArraySnapshot {
    static constexpr int kMax = 512;
    void* items[kMax];
    int size;
    int capacity;
    int growBy;
    bool hasData;
    bool complete;
    void* data;

    template <typename T>
    void Take(const UiPtrArray<T>& a) {
        size = a.size;
        capacity = a.capacity;
        growBy = a.growBy;
        hasData = a.data != nullptr;
        data = a.data;
        complete = size >= 0 && size <= kMax && (size == 0 || a.data);
        if (complete)
            for (int i = 0; i < size; ++i) items[i] = a.data[i];
    }
};

// Capacity after appending one element, as the original computes it.
int ExpectedCapacity(bool hasData, int size, int capacity, int growBy) {
    if (!hasData) return size + 1;
    if (size + 1 <= capacity) return capacity;
    int grow = growBy;
    if (grow == 0) {
        grow = size / 8;
        if (grow < 4) grow = 4;
        else if (grow > 0x400) grow = 0x400;
    }
    int newCapacity = grow + capacity;
    return size + 1 < newCapacity ? newCapacity : size + 1;
}

bool Aligned(const void* p) { return (reinterpret_cast<std::uintptr_t>(p) & 0x1F) == 0; }

// The array after Append(item) from the snapshot `before`.
template <typename T>
bool AppendedCorrectly(const ArraySnapshot& before, const UiPtrArray<T>& a, const void* item) {
    if (!before.complete) return true;
    if (a.size != before.size + 1 || !a.data || !Aligned(a.data)) return false;
    int capacity = ExpectedCapacity(before.hasData, before.size, before.capacity, before.growBy);
    if (a.capacity != capacity || a.growBy != before.growBy) return false;
    for (int i = 0; i < before.size; ++i)
        if (a.data[i] != before.items[i]) return false;
    return a.data[before.size] == item;
}

// The array after removing slot `index` from the snapshot `before`.
template <typename T>
bool RemovedCorrectly(const ArraySnapshot& before, const UiPtrArray<T>& a, int index) {
    if (!before.complete) return true;
    if (a.size != before.size - 1 || a.capacity != before.capacity) return false;
    for (int i = 0; i < a.size; ++i)
        if (a.data[i] != before.items[i < index ? i : i + 1]) return false;
    return true;
}

template <typename T>
bool Empty(const UiPtrArray<T>& a) {
    return !a.data && a.size == 0 && a.capacity == 0;
}
} // namespace

// --- CInterface3D scaling: the original on a scratch copy of the output -----------------------

// With Rebirth's WideScreen addon's code calls them with ECX 0 too (ui.h, the scaling hooks): the
// interface's size is shown when there is one.
static int Width(const CInterface3D* self) { return self ? self->width_ : 0; }
static int Height(const CInterface3D* self) { return self ? self->height_ : 0; }

// 005F7CA0 / 005F7D20: PUSH ESI; MOV ESI,[ESP+8]; MOV EAX,imm32 (10 bytes).
static int(__fastcall* s_scaleRectTo)(const CInterface3D*, void*, RECT*);
static int __fastcall ScaleRectToVerify(const CInterface3D* self, void*, RECT* rect) {
    RECT in = *rect, expected = *rect;
    int expectedResult = s_scaleRectTo(self, nullptr, &expected);
    int result = ScaleRectToScreenEntry(self, nullptr, rect);
    bool same = std::memcmp(&expected, rect, sizeof(RECT)) == 0 && result == expectedResult;
    ei::verify::Check("CInterface3D::ScaleRectToScreen", true, same, "%dx%d (%d,%d,%d,%d)", Width(self),
                      Height(self), in.left, in.top, in.right, in.bottom);
    return expectedResult;
}
EI_VERIFY(0x005F7CA0, ScaleRectToVerify, 10, s_scaleRectTo);

static int(__fastcall* s_scalePointTo)(const CInterface3D*, void*, POINT*);
static int __fastcall ScalePointToVerify(const CInterface3D* self, void*, POINT* point) {
    POINT in = *point, expected = *point;
    int expectedResult = s_scalePointTo(self, nullptr, &expected);
    int result = ScalePointToScreenEntry(self, nullptr, point);
    bool same = std::memcmp(&expected, point, sizeof(POINT)) == 0 && result == expectedResult;
    ei::verify::Check("CInterface3D::ScalePointToScreen", true, same, "%dx%d (%d,%d)", Width(self), Height(self),
                      in.x, in.y);
    return expectedResult;
}
EI_VERIFY(0x005F7D20, ScalePointToVerify, 10, s_scalePointTo);

// 005F7DA0, 005F7E00, 005F7E30 (with Rebirth's WideScreen: addon's code written over them, which
// the trampolines run; the stolen bytes are whole instructions of both, EI_REBIRTH_LIVE_STOLEN).
// 005F7DA0: PUSH ESI; MOV ESI,[ESP+8]; MOV EAX,[ESI] (7 bytes; addon's: PUSH EBP; PUSH EBX; PUSH ESI;
// MOV ESI,[ESP+0x10]). addon's entry 005F7D9E in front of it arrives with no interface (ECX 0).

static int(__fastcall* s_scaleRectFrom)(const CInterface3D*, void*, RECT*);
static int __fastcall ScaleRectFromVerify(const CInterface3D* self, void*, RECT* rect) {
    RECT in = *rect, expected = *rect;
    int expectedResult = s_scaleRectFrom(self, nullptr, &expected);
    int result = ScaleRectFromScreenEntry(self, nullptr, rect);
    bool same = std::memcmp(&expected, rect, sizeof(RECT)) == 0 && result == expectedResult;
    ei::verify::Check("CInterface3D::ScaleRectFromScreen", true, same, "%dx%d (%d,%d,%d,%d)", Width(self),
                      Height(self), in.left, in.top, in.right, in.bottom);
    return expectedResult;
}
EI_VERIFY(0x005F7DA0, ScaleRectFromVerify, 7, s_scaleRectFrom);

// 005F7E00: PUSH ESI; MOV ESI,[ESP+8]; MOV EAX,[ESI]; LEA EAX,[EAX+EAX*4] (10 bytes; addon's: PUSH
// ECX; PUSH ESI; MOV ESI,[ESP+0xC]; MOV EAX,[ESI]; TEST ECX,ECX). addon's entry 005F7DFE in front of
// it (called by its patched 005EB210 and 00630010) arrives with ECX 0.
static int(__fastcall* s_scalePointFrom)(const CInterface3D*, void*, POINT*);
static int __fastcall ScalePointFromVerify(const CInterface3D* self, void*, POINT* point) {
    POINT in = *point, expected = *point;
    int expectedResult = s_scalePointFrom(self, nullptr, &expected);
    int result = ScalePointFromScreenEntry(self, nullptr, point);
    bool same = std::memcmp(&expected, point, sizeof(POINT)) == 0 && result == expectedResult;
    ei::verify::Check("CInterface3D::ScalePointFromScreen", true, same, "%dx%d (%d,%d)", Width(self), Height(self),
                      in.x, in.y);
    return expectedResult;
}
EI_VERIFY(0x005F7E00, ScalePointFromVerify, 10, s_scalePointFrom);

static int(__fastcall* s_scalePointFromCopy)(const CInterface3D*, void*, POINT*);
static int __fastcall ScalePointFromCopyVerify(const CInterface3D* self, void*, POINT* point) {
    POINT in = *point, expected = *point;
    int expectedResult = s_scalePointFromCopy(self, nullptr, &expected);
    int result = ScalePointFromScreenCopyEntry(self, nullptr, point);
    bool same = std::memcmp(&expected, point, sizeof(POINT)) == 0 && result == expectedResult;
    ei::verify::Check("CInterface3D::ScalePointFromScreenCopy", true, same, "%dx%d (%d,%d)", Width(self),
                      Height(self), in.x, in.y);
    return expectedResult;
}
// 005F7E30: PUSH ESI; MOV ESI,[ESP+8] (5 bytes; addon's: JMP 005F7E00, re-targeted in the trampoline).
EI_VERIFY(0x005F7E30, ScalePointFromCopyVerify, 5, s_scalePointFromCopy);

// 005F7590: MOV EAX,[ECX+0x34]; MOV EDX,[ECX+0x38] (6 bytes).
static CUiWindow*(__fastcall* s_getActiveWindow)(const CInterface3D*);
static CUiWindow* __fastcall GetActiveWindowVerify(const CInterface3D* self) {
    CUiWindow* expected = s_getActiveWindow(self);
    ei::verify::Check("CInterface3D::GetActiveWindow", expected, self->CInterface3D::GetActiveWindow(), "stack %p-%p",
                      self->stackBegin_, self->stackEnd_);
    return expected;
}
EI_VERIFY(0x005F7590, GetActiveWindowVerify, 6, s_getActiveWindow);

// 005F74E0 writes +0x48 and +0x4C only: the original on a copy of the first 0x50 bytes.
// MOV EAX,[ESP+4]; MOV byte [ECX+0x4C],1 (8 bytes).
static void(__fastcall* s_setPending)(void*, void*, std::uint32_t);
static void __fastcall SetPendingVerify(CInterface3D* self, void*, std::uint32_t value) {
    std::uint8_t scratch[0x50];
    std::memcpy(scratch, self, sizeof(scratch));
    s_setPending(scratch, nullptr, value);
    self->CInterface3D::SetPendingValue(value);
    ei::verify::Check("CInterface3D::SetPendingValue", true, std::memcmp(scratch, self, sizeof(scratch)) == 0,
                      "value %08X", value);
}
EI_VERIFY(0x005F74E0, SetPendingVerify, 8, s_setPending);

// --- Record lookups ---------------------------------------------------------------------------

// 005F2EA0 / 005F2EE0: PUSH ESI; MOV ESI,[ECX]; MOV ECX,[ECX+4] (6 bytes).
static char*(__fastcall* s_spellAt)(const UiRecordTable*, void*, std::uint32_t);
static char* __fastcall SpellAtVerify(const UiRecordTable* self, void*, std::uint32_t index) {
    char* expected = s_spellAt(self, nullptr, index);
    ei::verify::Check("UiRecordTable::SpellAt", expected, self->UiRecordTable::SpellAt(index), "index %u", index);
    return expected;
}
EI_VERIFY(0x005F2EA0, SpellAtVerify, 6, s_spellAt);

static char*(__fastcall* s_modifierAt)(const UiRecordTable*, void*, std::uint32_t);
static char* __fastcall ModifierAtVerify(const UiRecordTable* self, void*, std::uint32_t index) {
    char* expected = s_modifierAt(self, nullptr, index);
    ei::verify::Check("UiRecordTable::ModifierAt", expected, self->UiRecordTable::ModifierAt(index), "index %u",
                      index);
    return expected;
}
EI_VERIFY(0x005F2EE0, ModifierAtVerify, 6, s_modifierAt);

// --- CUiSurface / CUiModel: the original on scratch copies ------------------------------------

// 005FD3A0: LEA EAX,[ECX+0x24]; PUSH ESI; MOV ESI,[ESP+8] (8 bytes).
static void(__fastcall* s_surfaceSetRect)(CUiSurface*, void*, int, int, int, int);
static void __fastcall SurfaceSetRectVerify(CUiSurface* self, void*, int left, int top, int right, int bottom) {
    CUiSurface scratch;
    std::memcpy(&scratch, self, sizeof(scratch));
    s_surfaceSetRect(&scratch, nullptr, left, top, right, bottom);
    self->CUiSurface::SetRect(left, top, right, bottom);
    ei::verify::Check("CUiSurface::SetRect", true, std::memcmp(&scratch, self, sizeof(scratch)) == 0,
                      "(%d,%d,%d,%d)", left, top, right, bottom);
}
EI_VERIFY(0x005FD3A0, SurfaceSetRectVerify, 8, s_surfaceSetRect);

// 005FE400: PUSH ESI; MOV ESI,ECX; LEA EAX,[ESP+8] (7 bytes).
static void(__fastcall* s_surfaceMoveTo)(CUiSurface*, void*, int, int);
static void __fastcall SurfaceMoveToVerify(CUiSurface* self, void*, int x, int y) {
    CUiSurface scratch;
    std::memcpy(&scratch, self, sizeof(scratch));
    s_surfaceMoveTo(&scratch, nullptr, x, y);
    self->CUiSurface::MoveTo(x, y);
    ei::verify::Check("CUiSurface::MoveTo", true, std::memcmp(&scratch, self, sizeof(scratch)) == 0, "(%d,%d)", x, y);
}
EI_VERIFY(0x005FE400, SurfaceMoveToVerify, 7, s_surfaceMoveTo);

// 005FC450 writes the object's +0x1C..+0x27: the original through a scratch model pointing at a
// copy of the object's first 0x28 bytes. SUB ESP,0xC; FLD [ESP+0x10] (7 bytes).
static void(__fastcall* s_setScreenPosition)(CUiModel*, void*, float, float, float);
static void __fastcall SetScreenPositionVerify(CUiModel* self, void*, float x, float y, float depth) {
    std::uint8_t object[0x28];
    std::memcpy(object, self->object_, sizeof(object));
    CUiModel scratch{reinterpret_cast<CTransformObject*>(object)};
    s_setScreenPosition(&scratch, nullptr, x, y, depth);
    self->CUiModel::SetScreenPosition(x, y, depth);
    ei::verify::Check("CUiModel::SetScreenPosition", true,
                      std::memcmp(object + 0x1C, &self->object_->localPosition_, 12) == 0, "%08X %08X %08X",
                      Bits(x), Bits(y), Bits(depth));
}
EI_VERIFY(0x005FC450, SetScreenPositionVerify, 7, s_setScreenPosition);

// --- CUiRegion ---------------------------------------------------------------------------------

// 005FC8F0: SUB ESP,0xC; MOV EDX,[ESP+0x14] (7 bytes).
static BOOL(__fastcall* s_hitTest)(const CUiRegion*, void*, int, int);
static BOOL __fastcall HitTestVerify(const CUiRegion* self, void*, int x, int y) {
    BOOL expected = s_hitTest(self, nullptr, x, y);
    ei::verify::Check("CUiRegion::HitTest", expected, self->CUiRegion::HitTest(x, y), "(%d,%d) points %d rect %d",
                      x, y, self->points_.size, self->rectOnly_);
    return expected;
}
EI_VERIFY(0x005FC8F0, HitTestVerify, 7, s_hitTest);

// 005FC660: MOV EAX,[ECX+0x30]; TEST EAX,EAX (5 bytes).
static BOOL(__fastcall* s_hasActiveTip)(const CUiRegion*);
static BOOL __fastcall HasActiveTipVerify(const CUiRegion* self) {
    BOOL expected = s_hasActiveTip(self);
    ei::verify::Check("CUiRegion::HasActiveTip", expected, self->CUiRegion::HasActiveTip(), "tip %p", self->tip_);
    return expected;
}
EI_VERIFY(0x005FC660, HasActiveTipVerify, 5, s_hasActiveTip);

// The rest free or allocate the vertex storage: ours only, then the fields are checked.
// SetRect and AddPoint with Rebirth's WideScreen (ours replaces addon's in a Rebirth launch): x
// moved by the panel offset, which a Rebirth launch shares with addon's panel builders.
static int PanelOffset() { return widescreen::Active() ? widescreen::PanelOffset() : 0; }

static void __fastcall RegionSetRectVerify(CUiRegion* self, void*, int left, int top, int right, int bottom) {
    const int offset = PanelOffset();
    self->CUiRegion::SetRect(left, top, right, bottom);
    bool ok = !self->points_.data && self->points_.size == 0 && self->points_.capacity == 0 &&
              self->bounds_.left == left + offset && self->bounds_.top == top && self->bounds_.right == right + offset &&
              self->bounds_.bottom == bottom && self->enabled_ && self->rectOnly_;
    ei::verify::Check("CUiRegion::SetRect", true, ok, "(%d,%d,%d,%d) offset %d", left, top, right, bottom, offset);
}
EI_VERIFY(0x005FC9E0, RegionSetRectVerify, 0, s_unused);

static void __fastcall ClearShapeVerify(CUiRegion* self) {
    self->CUiRegion::ClearShape();
    bool ok = !self->points_.data && self->points_.size == 0 && self->points_.capacity == 0 && !self->enabled_ &&
              !self->rectOnly_;
    ei::verify::Check("CUiRegion::ClearShape", true, ok, "");
}
EI_VERIFY(0x005FCA40, ClearShapeVerify, 0, s_unused);

static void __fastcall AddPointVerify(CUiRegion* self, void*, int x, int y) {
    constexpr int kMax = 256;
    UiPoint before[kMax];
    int count = self->points_.size, capacity = self->points_.capacity;
    bool hasData = self->points_.data != nullptr;
    RECT bounds = self->bounds_;
    bool enabled = self->enabled_;
    bool complete = count >= 0 && count <= kMax && (count == 0 || self->points_.data);
    if (complete && count) std::memcpy(before, self->points_.data, count * sizeof(UiPoint));

    const int offset = PanelOffset();
    self->CUiRegion::AddPoint(x, y);

    if (!complete) return;
    x += offset;
    RECT expected = bounds;
    if (count == 0) {
        expected = RECT{x, y, x, y};
    } else {
        if (x < expected.left) expected.left = x;
        if (y < expected.top) expected.top = y;
        if (x > expected.right) expected.right = x;
        if (y > expected.bottom) expected.bottom = y;
    }
    int expectedCapacity = ExpectedCapacity(hasData, count, capacity, self->points_.growBy);
    bool ok = self->points_.size == count + 1 && self->points_.capacity == expectedCapacity && Aligned(self->points_.data) &&
              std::memcmp(&self->bounds_, &expected, sizeof(RECT)) == 0 &&
              (count == 0 || std::memcmp(self->points_.data, before, count * sizeof(UiPoint)) == 0) &&
              self->points_.data[count].x == x && self->points_.data[count].y == y &&
              self->enabled_ == (enabled || count + 1 > 2);
    ei::verify::Check("CUiRegion::AddPoint", true, ok, "(%d,%d) count %d capacity %d offset %d", x, y, count, capacity,
                      offset);
}
EI_VERIFY(0x005FCA70, AddPointVerify, 0, s_unused);

// 005FC880 and 005FEBF0 call the tooltip's handler (0065E580), which runs the tooltip's own state
// machine: 0 and 4 stay, 1 may become 0 or 2, 2 may become 4, 3 may become 0. Ours runs alone; the
// state we hand over (from our HitTest, verified on its own) must explain the final state.
namespace {
int TipStateAfterTransition(int state, bool inside) {
    if (inside) return state == 0 || state == 1 ? 1 : state;
    return state != 0 && state != 1 ? 3 : state;
}
bool TipHandlerCanReach(int from, int to) {
    if (from == to) return true;
    switch (from) {
    case 1: return to == 0 || to == 2;
    case 2: return to == 4;
    case 3: return to == 0;
    default: return false;
    }
}
} // namespace

static void __fastcall RegionMouseMoveVerify(CUiRegion* self, void*, int x, int y) {
    CUiTooltip* tip = self->tip_;
    int before = tip ? tip->state_ : 0;
    bool inside = tip && self->CUiRegion::HitTest(x, y) != 0;
    self->CUiRegion::OnMouseMove(x, y);
    if (!tip) return;
    int handed = TipStateAfterTransition(before, inside);
    ei::verify::Check("CUiRegion::OnMouseMove", true, TipHandlerCanReach(handed, tip->state_),
                      "(%d,%d) inside %d state %d -> %d", x, y, inside, before, tip->state_);
}
EI_VERIFY(0x005FC880, RegionMouseMoveVerify, 0, s_unused);

// --- CUiWindow ---------------------------------------------------------------------------------

// 005FFCC0 / 005FFD30: SUB ESP,8; MOV EAX,[ESP+0xC] (7 bytes).
static int(__fastcall* s_regionAt)(const CUiWindow*, void*, int, int);
static int __fastcall RegionAtVerify(const CUiWindow* self, void*, int x, int y) {
    int expected = s_regionAt(self, nullptr, x, y);
    ei::verify::Check("CUiWindow::RegionAt", expected, self->CUiWindow::RegionAt(x, y), "(%d,%d) regions %d", x, y,
                      self->regions_.size);
    return expected;
}
EI_VERIFY(0x005FFCC0, RegionAtVerify, 7, s_regionAt);

static BOOL(__fastcall* s_containsPoint)(const CUiWindow*, void*, int, int);
static BOOL __fastcall ContainsPointVerify(const CUiWindow* self, void*, int x, int y) {
    BOOL expected = s_containsPoint(self, nullptr, x, y);
    ei::verify::Check("CUiWindow::ContainsPoint", expected, self->CUiWindow::ContainsPoint(x, y), "(%d,%d)", x, y);
    return expected;
}
EI_VERIFY(0x005FFD30, ContainsPointVerify, 7, s_containsPoint);

// 005FEBF0: the same check for every region with a tooltip, at the scaled mouse position (005FED00
// does the same and redraws each tooltip).
template <typename Run>
static void RegionTipsCheck(const char* name, CUiWindow* self, Run run) {
    constexpr int kMax = 128;
    CUiTooltip* tips[kMax];
    int states[kMax];
    bool inside[kMax];
    int count = self->regions_.size;
    bool complete = count >= 0 && count <= kMax;
    if (complete) {
        POINT mouse{self->interface_->mouseX_, self->interface_->mouseY_};
        self->interface_->CallScalePointFromScreen(&mouse);
        for (int i = 0; i < count; ++i) {
            CUiRegion* region = self->regions_.data[i];
            tips[i] = region->tip_;
            states[i] = tips[i] ? tips[i]->state_ : 0;
            inside[i] = tips[i] && region->CUiRegion::HitTest(mouse.x, mouse.y) != 0;
        }
    }
    run();
    if (!complete) return;
    bool ok = true;
    for (int i = 0; i < count; ++i)
        if (tips[i]) ok &= TipHandlerCanReach(TipStateAfterTransition(states[i], inside[i]), tips[i]->state_);
    ei::verify::Check(name, true, ok, "regions %d", count);
}

static void __fastcall UpdateRegionTipsVerify(CUiWindow* self, void*, int unused) {
    RegionTipsCheck("CUiWindow::UpdateRegionTips", self, [&] { self->CUiWindow::UpdateRegionTips(unused); });
}
EI_VERIFY(0x005FEBF0, UpdateRegionTipsVerify, 0, s_unused);

static void __fastcall RedrawTipsVerify(CUiWindow* self) {
    RegionTipsCheck("CUiWindow::RedrawTips", self, [&] { self->CUiWindow::RedrawTips(); });
}
EI_VERIFY(0x005FED00, RedrawTipsVerify, 0, s_unused);

// 006002A0: slot 19 (006002C0 / 00601AC0) shows or hides surfaces and children; the flag is ours.
static void __fastcall SetEnabledVerify(CUiWindow* self, void*, int enabled, int notify) {
    self->CUiWindow::SetEnabled(enabled, notify);
    ei::verify::Check("CUiWindow::SetEnabled", enabled, self->enabled_, "notify %d", notify);
}
EI_VERIFY(0x006002A0, SetEnabledVerify, 0, s_unused);

static void __fastcall AddObjectVerify(CUiWindow* self, void*, CEngineObject* object) {
    ArraySnapshot before;
    before.Take(self->objects_);
    int refs = object ? object->refCount_ : 0;
    self->CUiWindow::AddObject(object);
    bool ok = object ? AppendedCorrectly(before, self->objects_, object) && object->refCount_ == refs + 1
                     : self->objects_.size == before.size;
    ei::verify::Check("CUiWindow::AddObject", true, ok, "object %p size %d", object, before.size);
}
EI_VERIFY(0x005FEDD0, AddObjectVerify, 0, s_unused);

static void __fastcall RemoveObjectVerify(CUiWindow* self, void*, CEngineObject* object) {
    ArraySnapshot before;
    before.Take(self->objects_);
    int index = -1;
    if (object && before.complete)
        for (int i = 0; i < before.size && index < 0; ++i)
            if (before.items[i] == object) index = i;
    self->CUiWindow::RemoveObject(object);
    bool ok = index >= 0 ? RemovedCorrectly(before, self->objects_, index) : self->objects_.size == before.size;
    ei::verify::Check("CUiWindow::RemoveObject", true, ok, "object %p index %d", object, index);
}
EI_VERIFY(0x005FF110, RemoveObjectVerify, 0, s_unused);

static void __fastcall RemoveObjectAtVerify(CUiWindow* self, void*, int index) {
    ArraySnapshot before;
    before.Take(self->objects_);
    self->CUiWindow::RemoveObjectAt(index);
    ei::verify::Check("CUiWindow::RemoveObjectAt", true, RemovedCorrectly(before, self->objects_, index), "index %d",
                      index);
}
EI_VERIFY(0x005FF180, RemoveObjectAtVerify, 0, s_unused);

static BOOL __fastcall RemoveSceneObjectVerify(CUiWindow* self, void*, CEngineObject* object) {
    ArraySnapshot before;
    before.Take(self->sceneObjects_);
    int index = -1;
    if (before.complete)
        for (int i = 0; i < before.size && index < 0; ++i)
            if (before.items[i] == object) index = i;
    BOOL result = self->CUiWindow::RemoveSceneObject(object);
    bool ok = !before.complete ||
              (index >= 0 ? result == 1 && RemovedCorrectly(before, self->sceneObjects_, index)
                          : result == 0 && self->sceneObjects_.size == before.size);
    ei::verify::Check("CUiWindow::RemoveSceneObject", true, ok, "object %p index %d", object, index);
    return result;
}
EI_VERIFY(0x005FF700, RemoveSceneObjectVerify, 0, s_unused);

static void __fastcall RemoveSceneObjectAtVerify(CUiWindow* self, void*, int index) {
    ArraySnapshot before;
    before.Take(self->sceneObjects_);
    self->CUiWindow::RemoveSceneObjectAt(index);
    ei::verify::Check("CUiWindow::RemoveSceneObjectAt", true, RemovedCorrectly(before, self->sceneObjects_, index),
                      "index %d", index);
}
EI_VERIFY(0x005FF770, RemoveSceneObjectAtVerify, 0, s_unused);

static void __fastcall ClearSceneObjectsVerify(CUiWindow* self) {
    self->CUiWindow::ClearSceneObjects();
    ei::verify::Check("CUiWindow::ClearSceneObjects", true, Empty(self->sceneObjects_), "");
}
EI_VERIFY(0x005FF7D0, ClearSceneObjectsVerify, 0, s_unused);

// 005FF1E0 (A-I17): the referenced objects' list empty afterwards. Only the base screen's chat
// window calls it (006091C0; multiplayer chat in a base).
static void __fastcall ClearObjectsVerify(CUiWindow* self) {
    self->CUiWindow::ClearObjects();
    ei::verify::Check("CUiWindow::ClearObjects", true, Empty(self->objects_), "");
}
EI_VERIFY(0x005FF1E0, ClearObjectsVerify, 0, s_unused);

static CUiSurface* __fastcall AddSurfaceVerify(CUiWindow* self, void*, CUiSurface* surface) {
    ArraySnapshot before;
    before.Take(self->surfaces_);
    CUiSurface* result = self->CUiWindow::AddSurface(surface);
    bool ok = (surface ? AppendedCorrectly(before, self->surfaces_, surface) : self->surfaces_.size == before.size) &&
              result == surface;
    ei::verify::Check("CUiWindow::AddSurface", true, ok, "surface %p size %d", surface, before.size);
    return result;
}
EI_VERIFY(0x005FFA90, AddSurfaceVerify, 0, s_unused);

static void __fastcall RemoveSurfaceAtVerify(CUiWindow* self, void*, int index) {
    ArraySnapshot before;
    before.Take(self->surfaces_);
    self->CUiWindow::RemoveSurfaceAt(index);
    ei::verify::Check("CUiWindow::RemoveSurfaceAt", true, RemovedCorrectly(before, self->surfaces_, index),
                      "index %d", index);
}
EI_VERIFY(0x005FFBF0, RemoveSurfaceAtVerify, 0, s_unused);

static void __fastcall ClearSurfacesVerify(CUiWindow* self) {
    self->CUiWindow::ClearSurfaces();
    ei::verify::Check("CUiWindow::ClearSurfaces", true, Empty(self->surfaces_), "");
}
EI_VERIFY(0x005FFC50, ClearSurfacesVerify, 0, s_unused);

static CUiRegion* __fastcall AddRegionVerify(CUiWindow* self) {
    ArraySnapshot before;
    before.Take(self->regions_);
    CUiRegion* region = self->CUiWindow::AddRegion();
    bool ok = AppendedCorrectly(before, self->regions_, region);
    if (region)
        ok &= region->interface_ == self->interface_ && !region->points_.data && region->points_.size == 0 &&
              region->points_.capacity == 0 && region->points_.growBy == 0 && !region->tip_ && !region->enabled_ &&
              !region->rectOnly_ && region->offsetX_ == 0 && region->offsetY_ == 0;
    ei::verify::Check("CUiWindow::AddRegion", true, ok, "size %d", before.size);
    return region;
}
EI_VERIFY(0x005FF830, AddRegionVerify, 0, s_unused);

static void __fastcall RemoveRegionAtVerify(CUiWindow* self, void*, int index) {
    ArraySnapshot before;
    before.Take(self->regions_);
    self->CUiWindow::RemoveRegionAt(index);
    ei::verify::Check("CUiWindow::RemoveRegionAt", true, RemovedCorrectly(before, self->regions_, index), "index %d",
                      index);
}
EI_VERIFY(0x005FF9C0, RemoveRegionAtVerify, 0, s_unused);

static void __fastcall ClearRegionsVerify(CUiWindow* self) {
    self->CUiWindow::ClearRegions();
    ei::verify::Check("CUiWindow::ClearRegions", true, Empty(self->regions_), "");
}
EI_VERIFY(0x005FFA20, ClearRegionsVerify, 0, s_unused);

static CUiModel* __fastcall AddModelVerify(CUiWindow* self) {
    ArraySnapshot before;
    before.Take(self->models_);
    CUiModel* model = self->CUiWindow::AddModel();
    bool ok = model ? AppendedCorrectly(before, self->models_, model) && !model->object_
                    : self->models_.size == before.size;
    ei::verify::Check("CUiWindow::AddModel", true, ok, "size %d", before.size);
    return model;
}
EI_VERIFY(0x005FFD80, AddModelVerify, 0, s_unused);

static void __fastcall ClearModelsVerify(CUiWindow* self) {
    self->CUiWindow::ClearModels();
    ei::verify::Check("CUiWindow::ClearModels", true, Empty(self->models_), "");
}
EI_VERIFY(0x005FFF40, ClearModelsVerify, 0, s_unused);

// --- UiPtrArray::SetSize (five identical copies in the original) -------------------------------

template <typename T>
static void SetSizeCheck(const char* name, UiPtrArray<T>* self, int newSize, int growBy) {
    ArraySnapshot before;
    before.Take(*self);
    self->SetSize(newSize, growBy);
    if (!before.complete) return;
    int step = growBy != -1 ? growBy : before.growBy;
    bool ok = self->growBy == step && self->size == newSize;
    if (newSize == 0) {
        ok &= !self->data && self->capacity == 0;
    } else if (!before.hasData) {
        ok &= self->data && Aligned(self->data) && self->capacity == newSize;
    } else if (newSize <= before.capacity) {
        ok &= self->data == before.data && self->capacity == before.capacity;
    } else {
        int grow = step;
        if (grow == 0) {
            grow = before.size / 8;
            if (grow < 4) grow = 4;
            else if (grow > 0x400) grow = 0x400;
        }
        int capacity = grow + before.capacity;
        if (newSize >= capacity) capacity = newSize;
        ok &= self->data && Aligned(self->data) && self->capacity == capacity;
        for (int i = 0; i < before.size; ++i) ok &= self->data[i] == before.items[i];
    }
    ei::verify::Check(name, true, ok, "size %d -> %d grow %d", before.size, newSize, growBy);
}

static void __fastcall SetSizeObjectsVerify(UiPtrArray<CEngineObject>* self, void*, int newSize, int growBy) {
    SetSizeCheck("UiPtrArray<CEngineObject>::SetSize (006003D0)", self, newSize, growBy);
}
EI_VERIFY(0x006003D0, SetSizeObjectsVerify, 0, s_unused);

static void __fastcall SetSizeSceneObjectsVerify(UiPtrArray<CEngineObject>* self, void*, int newSize, int growBy) {
    SetSizeCheck("UiPtrArray<CEngineObject>::SetSize (006004E0)", self, newSize, growBy);
}
EI_VERIFY(0x006004E0, SetSizeSceneObjectsVerify, 0, s_unused);

static void __fastcall SetSizeSurfacesVerify(UiPtrArray<CUiSurface>* self, void*, int newSize, int growBy) {
    SetSizeCheck("UiPtrArray<CUiSurface>::SetSize", self, newSize, growBy);
}
EI_VERIFY(0x00600730, SetSizeSurfacesVerify, 0, s_unused);

static void __fastcall SetSizeRegionsVerify(UiPtrArray<CUiRegion>* self, void*, int newSize, int growBy) {
    SetSizeCheck("UiPtrArray<CUiRegion>::SetSize", self, newSize, growBy);
}
EI_VERIFY(0x00600840, SetSizeRegionsVerify, 0, s_unused);

static void __fastcall SetSizeModelsVerify(UiPtrArray<CUiModel>* self, void*, int newSize, int growBy) {
    SetSizeCheck("UiPtrArray<CUiModel>::SetSize", self, newSize, growBy);
}
EI_VERIFY(0x00600950, SetSizeModelsVerify, 0, s_unused);

static void __fastcall SetSizeChildrenVerify(UiPtrArray<CUiWindow>* self, void*, int newSize, int growBy) {
    SetSizeCheck("UiPtrArray<CUiWindow>::SetSize", self, newSize, growBy);
}
EI_VERIFY(0x00601D00, SetSizeChildrenVerify, 0, s_unused);

// 005FF3C0 (ours only: it makes an object). A new object is appended to the scene objects with the
// array's growth rule (one already listed is not added again); either way it gets the window's
// black material and its first referenced object as the texture.
static CUiMesh* __fastcall AddSpriteVerify(CUiWindow* self, void*, const char* name) {
    ArraySnapshot before;
    before.Take(self->sceneObjects_);
    CUiMesh* sprite = self->CUiWindow::AddSprite(name);
    auto* object = reinterpret_cast<CEngineObject*>(sprite);
    bool ok;
    if (!sprite) {
        ok = self->sceneObjects_.size == before.size;
    } else {
        bool listed = false;
        for (int i = 0; before.complete && i < before.size; ++i) listed |= before.items[i] == object;
        ok = listed ? self->sceneObjects_.size == before.size : AppendedCorrectly(before, self->sceneObjects_, object);
        CEngineObject* texture = self->objects_.size > 0 ? self->objects_.data[0] : nullptr;
        ok &= sprite->material_ == self->blackMaterial_ && sprite->texture_ == texture;
    }
    ei::verify::Check("CUiWindow::AddSprite", true, ok, "name %p sprite %p size %d", name, sprite, before.size);
    return sprite;
}
EI_VERIFY(0x005FF3C0, AddSpriteVerify, 0, s_unused);
