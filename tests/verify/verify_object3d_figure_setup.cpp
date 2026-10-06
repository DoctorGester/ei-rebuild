// Checks for object3d_figure_setup.cpp (A-U4).
// - SetUpAnimatedFigure (005179E0): ours only (it loads the model, plays and releases the animation
//   state's parts: a second run would find no parts). Checked: one box and one part name per part
//   the state animated before, every box flagged 4, the state's part list empty, both bounds flags
//   set, and the bounds recomputed here from the boxes (heroes: x / y -0.5..0.5) agree (CheckNear).
// Reached by: the zone loader (005927C0) for every animated object and unit of a zone, so every
// scenario that starts a game (units, unit_create, world_load); levers (Lever::SetState) too.
// - AlignedPtrArray::Add (00519A10): ours only; the result is the old size, the array one longer
//   with `value` last. Reached through LoadPart (below) for each new part.
// - LoadPart (00517240): ours only; null, or a part listed in the parts with box flag 4 and its
//   pivot at its position. Reached by the client unit models (WearArmourModel, ChangeWeaponModel:
//   units with equipment, combat, unit_create) and by LoadParts.
// - LoadParts (00517110): ours only; returns 1 and the object's model name is `model`. Reached by
//   00516DA0 (an object's figure built from part names: client objects of every zone load).
#include "verify.h"

#include "../../src/game/ai_map.h"
#include "../../src/game/map_object_figures.h"

#include <cfloat>
#include <cmath>
#include <cstring>

namespace {
void* s_unusedSetUp;
void* s_unusedAdd;
void* s_unusedLoadPart;
void* s_unusedLoadParts;

bool Near(float a, float b) { return ei::verify::NearlyEqual(a, b, 8, 1e-5f); }
} // namespace

static void __fastcall SetUpAnimatedFigureVerify(CObject3D* self, void*, const char* model) {
    int parts = self->animation ? self->animation->parts.count : 0;
    bool hadAnimation = self->animation != nullptr;
    self->SetUpAnimatedFigure(model);

    bool ok = (self->boundsFlags & 3) == 3;
    if (hadAnimation) {
        ok = ok && self->partBoxCount == parts && self->partNameCount == parts && self->animation->parts.count == 0 &&
             !self->animation->parts.head;
    }
    float high[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
    float low[3] = {FLT_MAX, FLT_MAX, FLT_MAX};
    if (hadAnimation && ok) {
        for (int i = 0; i < parts; ++i) {
            const FootprintPart& box = self->partBoxes[i];
            ok = ok && (box.flags & 4) != 0;
            const float pivot[3] = {box.pivot.x, box.pivot.y, box.pivot.z};
            const float mn[3] = {box.boxMin.x, box.boxMin.y, box.boxMin.z};
            const float mx[3] = {box.boxMax.x, box.boxMax.y, box.boxMax.z};
            const float off[3] = {box.offset.x, box.offset.y, box.offset.z};
            for (int k = 0; k < 3; ++k) {
                float h = pivot[k] + mx[k] + off[k];
                float l = pivot[k] + mn[k] + off[k];
                if (!(high[k] >= h)) high[k] = h;
                if (!(l >= low[k])) low[k] = l;
            }
        }
    }
    if (self->type_ == 0x32) {
        high[0] = high[1] = 0.5f;
        low[0] = low[1] = -0.5f;
    }
    for (int k = 0; ok && k < 3; ++k) {
        float c = (low[k] + high[k]) * 0.5f;
        ok = Near(c, self->center[k]) && Near(low[k] - c, self->below[k]) && Near(high[k] - c, self->above[k]);
    }
    ei::verify::Check("CObject3D::SetUpAnimatedFigure", true, ok, "%s: %d parts, centre %g %g %g", model ? model : "-",
                      parts, self->center[0], self->center[1], self->center[2]);
}
EI_VERIFY(0x005179E0, SetUpAnimatedFigureVerify, 0, s_unusedSetUp);

static int __fastcall AddVerify(AlignedPtrArray* self, void*, void* value) {
    int size = self->size;
    int index = self->Add(value);
    bool ok = index == size && self->size == size + 1 && self->data[index] == value;
    ei::verify::Check("AlignedPtrArray::Add", true, ok, "size %d -> %d, index %d", size, self->size, index);
    return index;
}
EI_VERIFY(0x00519A10, AddVerify, 0, s_unusedAdd);

static FigurePart* __fastcall LoadPartVerify(CObject3D* self, void*, const char* partName, void* model,
                                             std::uint32_t arg) {
    FigurePart* part = self->LoadPart(partName, model, arg);
    bool ok = true;
    if (part) {
        bool listed = false;
        for (int i = 0; i < self->parts.size; ++i) listed = listed || self->parts.data[i] == part;
        ok = listed && (part->boxFlags & 4) != 0 && part->boxPivot[0] == part->position[0] &&
             part->boxPivot[1] == part->position[1] && part->boxPivot[2] == part->position[2];
    }
    ei::verify::Check("CObject3D::LoadPart", true, ok, "%s: %p", partName ? partName : "-", static_cast<void*>(part));
    return part;
}
EI_VERIFY(0x00517240, LoadPartVerify, 0, s_unusedLoadPart);

static int __fastcall LoadPartsVerify(CObject3D* self, void*, const char* model, const void* partList,
                                      std::uint32_t arg) {
    int result = self->LoadParts(model, partList, arg);
    bool ok = result == 1 && self->name && model && std::strcmp(self->name, model) == 0;
    ei::verify::Check("CObject3D::LoadParts", true, ok, "%s: result %d", model ? model : "-", result);
    return result;
}
EI_VERIFY(0x00517110, LoadPartsVerify, 0, s_unusedLoadParts);

// - MakeFigure (00516DA0): ours only; returns 1, the model name is `model` and flags_ 0x80 is set
//   unless both `arg` and the figure are 0. Reached by the client player's OnNewObject (00668330:
//   every object a zone load sends to the client), the interface's unit models (005FC1F0, ui_*).
namespace {
void* s_unusedMakeFigure;
} // namespace

static int __fastcall MakeFigureVerify(CObject3D* self, void*, const char* model, void* partList, std::uint32_t arg) {
    int result = self->MakeFigure(model, partList, arg);
    bool loaded = (self->flags_ & 0x80) != 0;
    bool ok = result == 1 && self->name && model && std::strcmp(self->name, model) == 0 &&
              loaded == (arg != 0 || self->figure != nullptr);
    ei::verify::Check("CObject3D::MakeFigure", true, ok, "%s: result %d, flags %08X", model ? model : "-", result,
                      self->flags_);
    return result;
}
EI_VERIFY(0x00516DA0, MakeFigureVerify, 0, s_unusedMakeFigure);
