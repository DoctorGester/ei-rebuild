// Checks for light.cpp (CLight and the light manager CLightFactory). Every scenario: the renderer's
// default light (0x2E) at startup, unit and spell lights (0x31) in zones; the sun is a directional
// light (SetDirectional); the manager is built with the client world.
// - CLight::Construct: the original on a scratch copy (bytes neither writes stay equal), ours on the
//   real object; every byte compared but the range's square and inverse (CheckNear: they depend on
//   the FPU precision).
// - SetPoint / SetDirectional: the original on a scratch copy, ours on the light; the whole object
//   compared (TiltTowards is the same rewritten code in both runs) and the results.
// - Everything that allocates, releases or registers runs ours only (stolen bytes 0); the state left
//   is checked.
// - The class name / id getters and slots 2 / 3 run against the original.
#include "verify.h"

#include "../../src/game/spell.h"
#include "../../src/lib/crt.h"

#include <cstring>

namespace {

void* s_unused;

std::uintptr_t VtableOf(const void* object) {
    std::uintptr_t v;
    std::memcpy(&v, object, 4);
    return v;
}

int FirstDifference(const void* a, const void* b, std::size_t n, std::size_t skipFrom = 0, std::size_t skipTo = 0) {
    auto* x = static_cast<const unsigned char*>(a);
    auto* y = static_cast<const unsigned char*>(b);
    for (std::size_t i = 0; i < n; ++i)
        if ((i < skipFrom || i >= skipTo) && x[i] != y[i]) return static_cast<int>(i);
    return -1;
}

// 0068CED0: PUSH -1; PUSH imm32 (7).
CLight*(__fastcall* s_lightCtor)(CLight*);
CLight* __fastcall LightConstructVerify(CLight* self) {
    alignas(16) unsigned char scratch[sizeof(CLight)];
    std::memcpy(scratch, self, sizeof(CLight));
    auto* original = reinterpret_cast<CLight*>(scratch);
    CLight* expected = s_lightCtor(original);
    CLight* actual = self->Construct();
    int diff = FirstDifference(scratch, self, sizeof(CLight), offsetof(CLight, rangeSquared_),
                               offsetof(CLight, rangeSquared_) + 8);
    ei::verify::Check("CLight::Construct", true, diff < 0 && expected == original && actual == self,
                      "light %p: first difference at +0x%X", self, diff);
    ei::verify::CheckNear("CLight::Construct rangeSquared", original->rangeSquared_, self->rangeSquared_, "light %p", self);
    ei::verify::CheckNear("CLight::Construct inverseRangeSquared", original->inverseRangeSquared_,
                          self->inverseRangeSquared_, "light %p", self);
    return actual;
}

// 0068D010 / 0068D0A0: SUB ESP,0xC; MOV EAX,[ESP+0x10] (7).
BOOL(__fastcall* s_setPoint)(CLight*, void*, const Vector3*, const Vector3*);
BOOL(__fastcall* s_setDirectional)(CLight*, void*, const Vector3*, const Vector3*);

template <typename Ours>
BOOL SetBoth(const char* name, BOOL(__fastcall* original)(CLight*, void*, const Vector3*, const Vector3*), Ours ours,
             CLight* self, const Vector3* a, const Vector3* b) {
    // The arguments may point into the light: read them before either run changes it.
    Vector3 first = *a, second = *b;
    alignas(16) unsigned char scratch[sizeof(CLight)];
    std::memcpy(scratch, self, sizeof(CLight));
    BOOL expected = original(reinterpret_cast<CLight*>(scratch), nullptr, &first, &second);
    BOOL actual = ours(self, &first, &second);
    int diff = FirstDifference(scratch, self, sizeof(CLight));
    ei::verify::Check(name, true, diff < 0 && expected == actual, "light %p: first difference at +0x%X", self, diff);
    return actual;
}
BOOL __fastcall SetPointVerify(CLight* self, void*, const Vector3* color, const Vector3* position) {
    return SetBoth("CLight::SetPoint", s_setPoint,
                   [](CLight* l, const Vector3* c, const Vector3* p) { return l->SetPoint(c, p); }, self, color, position);
}
BOOL __fastcall SetDirectionalVerify(CLight* self, void*, const Vector3* color, const Vector3* direction) {
    return SetBoth("CLight::SetDirectional", s_setDirectional,
                   [](CLight* l, const Vector3* c, const Vector3* d) { return l->SetDirectional(c, d); }, self, color,
                   direction);
}

void __fastcall LightDestructVerify(CLight* self) {
    self->Destruct();
    ei::verify::Check("CLight::Destruct", true, VtableOf(self) == CEngineObject::kVtableAddress && !self->owner_,
                      "light %p", self);
}
CLight* __fastcall LightDeleteVerify(CLight* self, void*, unsigned flags) {
    CLight* result = self->DeletingDestruct(flags & ~1u);
    ei::verify::Check("CLight::DeletingDestruct", true, result == self && VtableOf(self) == CEngineObject::kVtableAddress,
                      "light %p", self);
    if (flags & 1) gamecrt::operator_delete(self);
    return result;
}

// 0068CFF0: MOV EAX,1 (5); 0068D000 is 9 bytes with no 5-byte instruction boundary that fits: the
// flags are compared after ours runs on a copy.
int(__fastcall* s_lightCreate)(CLight*);
int __fastcall LightCreateVerify(CLight* self) {
    int expected = s_lightCreate(self);
    ei::verify::Check("CLight::Create", expected, LightCreate(self), "%p", self);
    return expected;
}
void __fastcall LightDestroyVerify(CLight* self) {
    std::uint32_t before = self->flags_;
    LightDestroy(self);
    ei::verify::Check("CLight::Destroy", before & ~0x20u, self->flags_, "%p", self);
}

const char*(__fastcall* s_lightName)(CLight*);
const char* __fastcall LightNameVerify(CLight* self) {
    const char* expected = s_lightName(self);
    ei::verify::Check("CLight::GetClassName", expected, LightClassName(self), "%p", self);
    return expected;
}
int(__fastcall* s_lightId)(CLight*);
int __fastcall LightIdVerify(CLight* self) {
    int expected = s_lightId(self);
    ei::verify::Check("CLight::GetClassId", expected, LightClassId(self), "%p", self);
    return expected;
}

// --- The manager ---------------------------------------------------------------------------------

CLightFactory* __fastcall ManagerConstructVerify(CLightFactory* self) {
    CLightFactory* result = self->Construct();
    bool ok = result == self && VtableOf(self) == CLightFactory::kVtableAddress && self->litIds_.Empty() &&
              self->held_ == nullptr && self->unused_CC == 0 && self->unused_D0 == 1 && self->unused_B0[0] == 0 &&
              self->unused_B0[3] == 0;
    ei::verify::Check("CLightFactory::Construct", true, ok, "manager %p", self);
    return result;
}
void __fastcall ManagerDestructVerify(CLightFactory* self) {
    self->Destruct();
    ei::verify::Check("CLightFactory::Destruct", true, VtableOf(self) == CEngineObject::kVtableAddress && self->unused_CC == 0,
                      "manager %p", self);
}
CLightFactory* __fastcall ManagerDeleteVerify(CLightFactory* self, void*, unsigned flags) {
    CLightFactory* result = self->DeletingDestruct(flags & ~1u);
    ei::verify::Check("CLightFactory::DeletingDestruct", true, result == self && self->unused_CC == 0, "manager %p", self);
    if (flags & 1) gamecrt::operator_delete(self);
    return result;
}
CEngineObject* __fastcall ManagerNewObjectVerify(CLightFactory* self, void*, int type) {
    CEngineObject* object = self->CLightFactory::NewObjectImpl(type);
    bool known = type == 0x2E || type == 0x31;
    bool ok = object ? known && VtableOf(object) == CLight::kVtableAddress && object->type_ == type : !known;
    ei::verify::Check("CLightFactory::NewObject", true, ok, "manager %p type 0x%X -> %p", self, type, object);
    return object;
}
CEngineObject* __fastcall ManagerCreateObjectVerify(CLightFactory* self, void*, int type, int id) {
    CEngineObject* object = self->CLightFactory::CreateObjectImpl(type, id);
    bool ok = (object->flags_ & 0x80) == 0 && object->owner_ == self && (id == -1 || object->id_ == id);
    ei::verify::Check("CLightFactory::CreateObject", true, ok, "manager %p type 0x%X id %d -> %p", self, type, id, object);
    return object;
}

const char*(__fastcall* s_managerName)(CLightFactory*);
const char* __fastcall ManagerNameVerify(CLightFactory* self) {
    const char* expected = s_managerName(self);
    ei::verify::Check("CLightFactory::GetClassName", expected, LightManagerClassName(self), "%p", self);
    return expected;
}
int(__fastcall* s_managerId)(CLightFactory*);
int __fastcall ManagerIdVerify(CLightFactory* self) {
    int expected = s_managerId(self);
    ei::verify::Check("CLightFactory::GetClassId", expected, LightManagerClassId(self), "%p", self);
    return expected;
}

} // namespace

EI_VERIFY(0x0068CED0, LightConstructVerify, 7, s_lightCtor);
EI_VERIFY(0x0068D010, SetPointVerify, 7, s_setPoint);
EI_VERIFY(0x0068D0A0, SetDirectionalVerify, 7, s_setDirectional);
EI_VERIFY(0x0068CFE0, LightDestructVerify, 0, s_unused);
EI_VERIFY(0x0068CFC0, LightDeleteVerify, 0, s_unused);
EI_VERIFY(0x0068CFF0, LightCreateVerify, 5, s_lightCreate);
EI_VERIFY(0x0068D000, LightDestroyVerify, 0, s_unused);
EI_VERIFY(0x0068CFA0, LightNameVerify, 5, s_lightName);
EI_VERIFY(0x0068CFB0, LightIdVerify, 5, s_lightId);
EI_VERIFY(0x0068D850, ManagerConstructVerify, 0, s_unused);
EI_VERIFY(0x0068D990, ManagerDestructVerify, 0, s_unused);
EI_VERIFY(0x0068D970, ManagerDeleteVerify, 0, s_unused);
EI_VERIFY(0x0068DA40, ManagerNewObjectVerify, 0, s_unused);
EI_VERIFY(0x0068DAD0, ManagerCreateObjectVerify, 0, s_unused);
EI_VERIFY(0x0068D950, ManagerNameVerify, 5, s_managerName);
EI_VERIFY(0x0068D960, ManagerIdVerify, 5, s_managerId);

// --- 0068D310 / 0068D5A0 (A-H25): a colour lit by the light ------------------------------------------
// Pure but for `out` (the terrain's vertex lighting: every scenario with a zone): the original into
// the caller's colour, ours into a copy of what it held; r, g, b compared (the original leaves the
// alpha unset when the colour is lit).
namespace {
void CheckLit(const char* name, const Vector4& expected, const Vector4& actual) {
    ei::verify::CheckNear(name, expected.x, actual.x, "red %f / %f", expected.x, actual.x);
    ei::verify::CheckNear(name, expected.y, actual.y, "green %f / %f", expected.y, actual.y);
    ei::verify::CheckNear(name, expected.z, actual.z, "blue %f / %f", expected.z, actual.z);
}

// 0068D310: PUSH EBP; MOV EBP,ESP; SUB ESP,0x14 (6 bytes).
void(__fastcall* s_addDirectional)(const CLight*, void*, Vector4*, float, const Vector4*, const Vector3*);
void __fastcall AddDirectionalVerify(const CLight* self, void*, Vector4* out, float scale, const Vector4* base,
                                     const Vector3* normal) {
    Vector4 ours = *out;
    const Vector4 baseCopy = *base;
    const Vector3 normalCopy = *normal;
    s_addDirectional(self, nullptr, out, scale, base, normal);
    self->AddDirectional(&ours, scale, &baseCopy, &normalCopy);
    CheckLit("CLight::AddDirectional", *out, ours);
}

// 0068D5A0: PUSH EBP; MOV EBP,ESP; SUB ESP,0x1C (6 bytes).
void(__fastcall* s_addPoint)(const CLight*, void*, Vector4*, float, const Vector4*, const Vector3*, const Vector3*, float);
void __fastcall AddPointVerify(const CLight* self, void*, Vector4* out, float scale, const Vector4* base,
                               const Vector3* normal, const Vector3* toLight, float distance) {
    Vector4 ours = *out;
    const Vector4 baseCopy = *base;
    const Vector3 normalCopy = *normal;
    const Vector3 toLightCopy = *toLight;
    s_addPoint(self, nullptr, out, scale, base, normal, toLight, distance);
    self->AddPoint(&ours, scale, &baseCopy, &normalCopy, &toLightCopy, distance);
    CheckLit("CLight::AddPoint", *out, ours);
}
} // namespace

EI_VERIFY(0x0068D310, AddDirectionalVerify, 6, s_addDirectional);
EI_VERIFY(0x0068D5A0, AddPointVerify, 6, s_addPoint);
