// Checks for object3d_lifetime.cpp (A-U). All of them change references, free or allocate, so only
// ours runs and the state afterwards is checked:
// - Create3D (00515F70): result 1, position and scale 0, the identity orientation and default scale
//   from the globals, no side half, animation database or figure, flags 0, bounds and part boxes
//   cleared.
// - Destroy3D (00516080): no figure, effect sources, animation state, parts, pending parts or side
//   half; flags 0.
// - AddEffectSource (00518A00) / RemoveEffectSource (00518AE0): the source is in the list exactly
//   once afterwards / not at all, the count one higher / lower when it changed.
// - CountedRef<T>::Assign (00519580 the figure; copies 0051E570 the wounds material, 0051E4F0 its
//   preset): the slot holds the value, which gained a reference unless it was the old one.
// Reached by: Create / Destroy of every map object (CMapObject 0050E770 / 0050E840: every
// scenario's zone load, spawns and removals: unit_create, units); the effect sources by particle
// sources attached to units and objects (004849F0: spells_cast, effects, particles, combat's
// blood); Assign by the animated figures (00516DA0) and Destroy3D.
#include "verify.h"

#include "../../src/game/map_object_figures.h"
#include "../../src/game/link_figure.h"
#include "../../src/game/map_object_shadow.h"
#include "../../src/game/model.h"

#include <cstring>

namespace {
void* s_unusedCreate;
void* s_unusedDestroy;
void* s_unusedAdd;
void* s_unusedRemove;
void* s_unusedAssign;

int Occurrences(const CObject3D* object, const CEngineObject* source) {
    int n = 0;
    for (auto* node = object->effectSources.head; node; node = node->next)
        if (node->data == source) ++n;
    return n;
}
} // namespace

static int __fastcall Create3DVerify(CObject3D* self) {
    int result = self->Create3D();
    const char* problem = nullptr;
    if (result != 1) problem = "result";
    else if (self->position.x != 0.0f || self->position.y != 0.0f || self->position.z != 0.0f) problem = "position";
    else if (std::memcmp(&self->orientation, reinterpret_cast<const void*>(0x007AF1C0), 16)) problem = "orientation";
    else if (self->attached || self->animations || self->figure || self->flags_) problem = "side half / database / figure";
    else if (std::memcmp(&self->defaultScale, reinterpret_cast<const void*>(0x007AF1D0), 12)) problem = "default scale";
    else if (self->boundsFlags || self->radius != 0.0f || self->partBoxes || self->partBoxCount) problem = "bounds";
    ei::verify::Check("CObject3D::Create3D", true, problem == nullptr, "object %d: %s", self->id_,
                      problem ? problem : "");
    return result;
}
EI_VERIFY(0x00515F70, Create3DVerify, 0, s_unusedCreate);

static void __fastcall Destroy3DVerify(CObject3D* self) {
    self->Destroy3D();
    const char* problem = nullptr;
    if (self->figure || self->animation || self->attached) problem = "figure / animation / side half";
    else if (self->effectSources.head || self->effectSources.count) problem = "effect sources";
    else if (self->parts.size || self->parts.data) problem = "parts";
    else if (self->pendingParts.size || self->pendingParts.data) problem = "pending parts";
    else if (self->flags_ || self->animations) problem = "flags / database";
    ei::verify::Check("CObject3D::Destroy3D", true, problem == nullptr, "object %d: %s", self->id_,
                      problem ? problem : "");
}
EI_VERIFY(0x00516080, Destroy3DVerify, 0, s_unusedDestroy);

static void __fastcall AddEffectSourceVerify(CObject3D* self, void*, CEngineObject* source) {
    bool there = Occurrences(self, source) > 0;
    int count = self->effectSources.count;
    self->AddEffectSource(source);
    bool ok = Occurrences(self, source) == 1 && self->effectSources.count == count + (there ? 0 : 1);
    ei::verify::Check("CObject3D::AddEffectSource", true, ok, "object %d: %d sources (was %d, there %d)", self->id_,
                      self->effectSources.count, count, there ? 1 : 0);
}
EI_VERIFY(0x00518A00, AddEffectSourceVerify, 0, s_unusedAdd);

static void __fastcall RemoveEffectSourceVerify(CObject3D* self, void*, CEngineObject* source) {
    bool there = Occurrences(self, source) > 0;
    int count = self->effectSources.count;
    self->RemoveEffectSource(source);
    bool ok = self->effectSources.count == count - (there ? 1 : 0) && (self->effectSources.count > 0 ||
                                                                       !self->effectSources.head);
    ei::verify::Check("CObject3D::RemoveEffectSource", true, ok, "object %d: %d sources (was %d, there %d)",
                      self->id_, self->effectSources.count, count, there ? 1 : 0);
}
EI_VERIFY(0x00518AE0, RemoveEffectSourceVerify, 0, s_unusedRemove);

namespace {
int CountOf(const CLinkMesh* figure) { return figure->refCount_; }
int CountOf(const WoundsMaterial* material) { return material->refCount; }
int CountOf(const CRenderPreset* preset) { return preset->refs_; }
} // namespace

template <typename T>
static void __fastcall CountedRefAssignVerify(CountedRef<T>* self, void*, T* value) {
    bool same = self->object == value;
    int references = value ? CountOf(value) : 0;
    self->Assign(value);
    bool ok = self->object == value && (!value || same || CountOf(value) == references + 1);
    ei::verify::Check("CountedRef::Assign", true, ok, "value %p", static_cast<void*>(value));
}
EI_VERIFY(0x00519580, CountedRefAssignVerify<CLinkMesh>, 0, s_unusedAssign);
EI_VERIFY(0x0051E570, CountedRefAssignVerify<WoundsMaterial>, 0, s_unusedAssign);   // byte-identical copies (A-U11)
EI_VERIFY(0x0051E4F0, CountedRefAssignVerify<CRenderPreset>, 0, s_unusedAssign);
