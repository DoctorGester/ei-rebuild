// Checks for sky_object.cpp. Scenarios: any zone with a sky (newgame on zone1: the figure factory
// makes the CSkyObject, class 0x44; the world view lights it every frame through 004D9DE0),
// render_scene, zone_gz2g_traps (a zone change destroys and makes it again).
#include "verify.h"

#include "../../src/game/sky_object.h"

#include <cstring>
#include <vector>

namespace {
std::uintptr_t VtableOf(const void* object) {
    std::uintptr_t vtable;
    std::memcpy(&vtable, object, sizeof(vtable));
    return vtable;
}
template <typename T>
bool Empty(const lib::AlignedArray<T>& a) {
    return a.data == nullptr && a.size == 0 && a.capacity == 0 && a.growBy == 0;
}
} // namespace

// 004D9360: SUB ESP,0x14; FLD [ESP+0x1C] (7 bytes). When the haze needs no recomputing (as many
// distances as vertices), the original runs first and its colours are kept, then ours writes the
// same arrays: every diffuse and specular colour compared (integers from truncated floats; a
// product on a truncation edge could differ in the last unit). Otherwise ours only: one distance
// per vertex.
static void (__fastcall* s_light)(CSkyObject*, void*, const float*, SkyColour, SkyColour, SkyColour);
static void __fastcall LightVerify(CSkyObject* self, void*, const float* direction, SkyColour ambient, SkyColour light,
                                   SkyColour unused) {
    const int count = self->diffuse_.size;
    if (self->distances_.size != count) {
        self->Light(direction, ambient, light, unused);
        ei::verify::Check("CSkyObject::Light distances", count, self->distances_.size, "%d");
        return;
    }
    s_light(self, nullptr, direction, ambient, light, unused);
    std::vector<std::uint32_t> diffuse(self->diffuse_.data, self->diffuse_.data + count);
    std::vector<std::uint32_t> specular(self->specular_.data, self->specular_.data + count);
    self->Light(direction, ambient, light, unused);
    for (int i = 0; i < count; ++i) {
        ei::verify::Check("CSkyObject::Light diffuse", diffuse[i], self->diffuse_.data[i], "vertex %d", i);
        ei::verify::Check("CSkyObject::Light specular", specular[i], self->specular_.data[i], "vertex %d", i);
    }
}
EI_VERIFY(0x004D9360, LightVerify, 7, s_light);

// 004D9DE0 (ours only): passes the call on when there is an object.
static void (__fastcall* s_skyLight)(CSky*, void*, const float*, SkyColour, SkyColour, SkyColour);
static void __fastcall SkyLightVerify(CSky* self, void*, const float* direction, SkyColour ambient, SkyColour light,
                                      SkyColour unused) {
    self->Light(direction, ambient, light, unused);
    ei::verify::Check("CSky::Light", true, true, "");
}
EI_VERIFY(0x004D9DE0, SkyLightVerify, 0, s_skyLight);

// 004D9110 (ours only): the sky's vtable, its own arrays empty.
static CSkyObject* (__fastcall* s_construct)(CSkyObject*);
static CSkyObject* __fastcall ConstructVerify(CSkyObject* self) {
    CSkyObject* result = self->Construct();
    const bool ok = result == self && VtableOf(self) == CSkyObject::kVtableAddress && Empty(self->corners_) &&
                    Empty(self->texCoords_) && self->figureOwner_ == 0;
    ei::verify::Check("CSkyObject::Construct", true, ok, "%p", static_cast<void*>(self));
    return result;
}
EI_VERIFY(0x004D9110, ConstructVerify, 0, s_construct);

// 004D92B0 / 004D9290 (ours only).
static void (__fastcall* s_destruct)(CSkyObject*);
static void __fastcall DestructVerify(CSkyObject* self) {
    self->Destruct();
    ei::verify::Check("CSkyObject::Destruct", true, true, "");
}
EI_VERIFY(0x004D92B0, DestructVerify, 0, s_destruct);

static CSkyObject* (__fastcall* s_delete)(CSkyObject*, void*, unsigned);
static CSkyObject* __fastcall DeleteVerify(CSkyObject* self, void*, unsigned flags) {
    CSkyObject* result = self->DeletingDestruct(flags);
    ei::verify::Check("CSkyObject::DeletingDestruct", static_cast<void*>(self), static_cast<void*>(result), "%p");
    return result;
}
EI_VERIFY(0x004D9290, DeleteVerify, 0, s_delete);

// 004D9270 / 004D9280: MOV EAX,imm32 (5 bytes), against the original.
static const char* (__fastcall* s_className)(CSkyObject*);
static const char* __fastcall ClassNameVerify(CSkyObject* self) {
    const char* expected = s_className(self);
    const char* actual = self->CSkyObject::GetClassName();
    ei::verify::Check("CSkyObject::GetClassName", expected, actual, "%p");
    return actual;
}
EI_VERIFY(0x004D9270, ClassNameVerify, 5, s_className);

static int (__fastcall* s_classId)(CSkyObject*);
static int __fastcall ClassIdVerify(CSkyObject* self) {
    const int expected = s_classId(self);
    const int actual = self->CSkyObject::GetClassId();
    ei::verify::Check("CSkyObject::GetClassId", expected, actual, "%X");
    return actual;
}
EI_VERIFY(0x004D9280, ClassIdVerify, 5, s_classId);

// 004D9E80 / 004D9E90 / 004D9EA0 (ours only): an empty array, this returned. Reached from the sky's
// and the figure factory's (class 0x42) constructors.
using AlignedWords = lib::AlignedArray<std::uint32_t>;
static AlignedWords* (__fastcall* s_construct80)(AlignedWords*);
static AlignedWords* (__fastcall* s_construct90)(AlignedWords*);
static AlignedWords* (__fastcall* s_constructA0)(AlignedWords*);
static AlignedWords* __fastcall ArrayConstructVerify(AlignedWords* self) {
    AlignedWords* result = self->Construct();
    ei::verify::Check("AlignedArray::Construct", true, result == self && Empty(*self), "%p", static_cast<void*>(self));
    return result;
}
EI_VERIFY(0x004D9E80, ArrayConstructVerify, 0, s_construct80);
EI_VERIFY(0x004D9E90, ArrayConstructVerify, 0, s_construct90);
EI_VERIFY(0x004D9EA0, ArrayConstructVerify, 0, s_constructA0);
// S13: the other copies (ours only): the scene loader's point block 00475F10 (any figure load), the
// unit skins' 0056FCC0, the figure factory's 0056FD00 and the terrain's (zone loads).
static AlignedWords* s_moreConstructs[8];
EI_VERIFY(0x00475F10, ArrayConstructVerify, 0, s_moreConstructs[0]);
EI_VERIFY(0x0056FCC0, ArrayConstructVerify, 0, s_moreConstructs[1]);
EI_VERIFY(0x0056FD00, ArrayConstructVerify, 0, s_moreConstructs[2]);
EI_VERIFY(0x0056FD30, ArrayConstructVerify, 0, s_moreConstructs[3]);
EI_VERIFY(0x00570DE0, ArrayConstructVerify, 0, s_moreConstructs[4]);
EI_VERIFY(0x00570E10, ArrayConstructVerify, 0, s_moreConstructs[5]);
EI_VERIFY(0x00570E40, ArrayConstructVerify, 0, s_moreConstructs[6]);
EI_VERIFY(0x00570E70, ArrayConstructVerify, 0, s_moreConstructs[7]);

// 005636A0 (S13, ours only: it releases): the client world's teardown (a zone change, the exit).
// The objects it holds must still be counted when it runs.
static void* s_skyDestruct;
static void __fastcall SkyDestructVerify(CSky* self) {
    auto counted = [](const void* object) {
        return !object || static_cast<const CEngineObject*>(object)->refCount_ > 0;
    };
    bool ok = counted(self->object) && counted(self->material) && counted(self->texture);
    self->Destruct();
    ei::verify::Check("CSky::Destruct", true, ok, "object %p material %p texture %p", static_cast<void*>(self->object),
                      static_cast<void*>(self->material), self->texture);
}
EI_VERIFY(0x005636A0, SkyDestructVerify, 0, s_skyDestruct);

// 004D9680 (A-S8, ours only): no object, turning, a texture holder loaded.
static CSky* (__fastcall* s_skyConstruct)(CSky*);
static CSky* __fastcall SkyConstructVerify(CSky* self) {
    CSky* result = self->Construct();
    ei::verify::Check("CSky::Construct", true,
                      result == self && !self->object && !self->material && self->turns == 1 && self->texture,
                      "%p", self->texture);
    return result;
}
EI_VERIFY(0x004D9680, SkyConstructVerify, 0, s_skyConstruct);

// 004D96E0 (A-S8, ours only: it draws): the object at the eye lowered by 16 with that translation in
// its matrix, and the pipeline's stacks 1 and 2 as deep as before. Every frame with a sky (newgame,
// render_scene).
static void (__fastcall* s_skyDraw)(CSky*, void*, CSceneRenderer*, const Vector3*);
static void __fastcall SkyDrawVerify(CSky* self, void*, CSceneRenderer* renderer, const Vector3* eye) {
    const int depth1 = renderer->pipeline_->stacks_[1].count;
    const int depth2 = renderer->pipeline_->stacks_[2].count;
    self->Draw(renderer, eye);
    if (!self->object) return;
    const CSkyObject* o = self->object;
    volatile float rounded = eye->z - 16.0f;   // stored as a float, as the original does
    const float z = rounded;
    const bool ok = o->localPosition_.x == eye->x && o->localPosition_.y == eye->y && o->localPosition_.z == z &&
                    o->matrix_.m[3] == eye->x && o->matrix_.m[7] == eye->y && o->matrix_.m[11] == z &&
                    o->matrix_.m[15] == 1.0f && renderer->pipeline_->stacks_[1].count == depth1 &&
                    renderer->pipeline_->stacks_[2].count == depth2;
    ei::verify::Check("CSky::Draw", true, ok, "depths %d %d", depth1, depth2);
}
EI_VERIFY(0x004D96E0, SkyDrawVerify, 0, s_skyDraw);
