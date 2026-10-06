// Checks for object3d_slots.cpp (A-U2).
// - CombineRotation3D (00516370): the original runs, the world orientation is kept and put back,
//   ours runs; the four floats compared (CheckNear).
// - CopyFigure (005161E0): ours only; the parts array holds the other's parts and the transform
//   bytes (+0x1C..+0x54) equal the other's.
// - AttachSpecific (00516D50): ours only; a side half is attached both ways.
// - RemoveAllEffectSources (00515E90): ours only; the list is empty.
// - DrawFigure / DrawCollectDirty / DrawFigureShadow (00516460 / 00516420 / 00516490): ours only;
//   1 without a side half.
// Reached by: slot 14 through UpdateWorld for every placed object each frame (all scenarios);
// slot 19 when an object's figure half is made (every spawn: units, unit_create); slot 5 when an
// object is released (unit_create's remove, combat_more's kills); slots 22-24 by the client world
// drawing objects (every frame); slot 20 by copies of figures (the interface's unit models, ui_*).
#include "verify.h"

#include "../../src/game/core_transform.h"
#include "../../src/game/map_object_figures.h"

#include <cstdint>
#include <cstring>

namespace {
void* s_unusedCopy;
void* s_unusedAttach;
void* s_unusedRemoveAll;
void* s_unusedDraw;
void* s_unusedDrawDirty;
void* s_unusedDrawShadow;
} // namespace

// 00516370: SUB ESP,0x10; MOV EAX,[ESP+0x14] (7 bytes).
static void(__fastcall* s_combineRotation)(CObject3D*, void*, const Quaternion*);
static void __fastcall CombineRotation3DVerify(CObject3D* self, void*, const Quaternion* parent) {
    Quaternion before = self->worldOrientation;
    s_combineRotation(self, nullptr, parent);
    Quaternion expected = self->worldOrientation;
    self->worldOrientation = before;
    self->CombineRotation3D(parent);
    const Quaternion& actual = self->worldOrientation;
    bool same = ei::verify::NearlyEqual(expected.w, actual.w, 4, 1e-6f) &&
                ei::verify::NearlyEqual(expected.x, actual.x, 4, 1e-6f) &&
                ei::verify::NearlyEqual(expected.y, actual.y, 4, 1e-6f) &&
                ei::verify::NearlyEqual(expected.z, actual.z, 4, 1e-6f);
    ei::verify::Check("CObject3D::CombineRotation3D", true, same, "(%g %g %g %g) vs (%g %g %g %g)", expected.w,
                      expected.x, expected.y, expected.z, actual.w, actual.x, actual.y, actual.z);
}
EI_VERIFY(0x00516370, CombineRotation3DVerify, 7, s_combineRotation);

static void __fastcall CopyFigureVerify(CObject3D* self, void*, const CObject3D* other) {
    self->CopyFigure(other);
    bool ok = self->parts.size == other->parts.size &&
              (other->parts.size == 0 ||
               std::memcmp(self->parts.data, other->parts.data, other->parts.size * sizeof(void*)) == 0) &&
              std::memcmp(reinterpret_cast<const std::uint8_t*>(self) + 0x1C,
                          reinterpret_cast<const std::uint8_t*>(other) + 0x1C, 0x54 - 0x1C) == 0;
    ei::verify::Check("CObject3D::CopyFigure", true, ok, "%d parts", other->parts.size);
}
EI_VERIFY(0x005161E0, CopyFigureVerify, 0, s_unusedCopy);

static void __fastcall AttachSpecificVerify(CObject3D* self, void*, CEngineObject* half) {
    bool takes = half && half->type_ == 0x46;
    CEngineObject* before = self->attached;
    self->AttachSpecific(half);
    bool ok = takes ? self->attached == half && self->Specific()->object == self : self->attached == before;
    ei::verify::Check("CObject3D::AttachSpecific", true, ok, "type %d", half ? half->type_ : -1);
}
EI_VERIFY(0x00516D50, AttachSpecificVerify, 0, s_unusedAttach);

static void __fastcall RemoveAllEffectSourcesVerify(CObject3D* self) {
    int count = self->effectSources.count;
    self->RemoveAllEffectSources();
    bool ok = self->effectSources.count == 0 && !self->effectSources.head && !self->effectSources.tail;
    ei::verify::Check("CObject3D::RemoveAllEffectSources", true, ok, "%d sources", count);
}
EI_VERIFY(0x00515E90, RemoveAllEffectSourcesVerify, 0, s_unusedRemoveAll);

static int __fastcall DrawFigureVerify(CObject3D* self, void*, void* context) {
    bool half = self->attached != nullptr;
    int result = self->DrawFigure(context);
    ei::verify::Check("CObject3D::DrawFigure", true, half || result == 1, "result %d", result);
    return result;
}
EI_VERIFY(0x00516460, DrawFigureVerify, 0, s_unusedDraw);

static int __fastcall DrawCollectDirtyVerify(CObject3D* self, void*, void* context) {
    bool half = self->attached != nullptr;
    int result = self->DrawCollectDirty(context);
    bool ok = half || (result == 1 && self->dirtyRect[0] == 0 && self->dirtyRect[1] == 0 && self->dirtyRect[2] == 0 &&
                       self->dirtyRect[3] == 0);
    ei::verify::Check("CObject3D::DrawCollectDirty", true, ok, "result %d", result);
    return result;
}
EI_VERIFY(0x00516420, DrawCollectDirtyVerify, 0, s_unusedDrawDirty);

static int __fastcall DrawFigureShadowVerify(CObject3D* self, void*, void* context) {
    bool half = self->attached != nullptr;
    int result = self->DrawFigureShadow(context);
    ei::verify::Check("CObject3D::DrawFigureShadow", true, half || result == 1, "result %d", result);
    return result;
}
EI_VERIFY(0x00516490, DrawFigureShadowVerify, 0, s_unusedDrawShadow);
