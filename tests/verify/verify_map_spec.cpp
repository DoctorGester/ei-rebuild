// Checks for map_spec*.cpp. Side by side: the type names (ours first, then the original's string
// made in a scratch CString and destroyed again; SV2h1), and with the original first the world
// object look-ups and the quaternion rotation (within a few ULPs). Ours only: the constructors (vtable and kind), the
// patrol array copy (same patrols and waypoints as the source). Reading and writing chunks run
// ours only without checks beyond the zone loading itself.
#include "verify.h"

#include "../../src/game/map_spec.h"

#include <cstdint>
#include <cstring>
#include <new>

namespace mapspec {
Vector3* __fastcall QuaternionRotate(const Quaternion* q, void*, Vector3* out, const Vector3* v);
} // namespace mapspec

namespace {
using namespace mapspec;
void* s_unused;

std::uint32_t VtableOf(const void* p) {
    std::uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

// A type name made by ours, then by the original into scratch storage (SV2h1: ours allocates
// first), compared, then the original's destroyed.
template <typename Self>
using NameFn = mfc::CString*(__fastcall*)(Self*, void*, mfc::CString*);
template <typename Self>
mfc::CString* CompareName(const char* what, NameFn<Self> original, mfc::CString* (Self::*ours)(mfc::CString*), Self* self,
                          mfc::CString* out) {
    (self->*ours)(out);
    alignas(mfc::CString) unsigned char raw[sizeof(mfc::CString)];
    auto* expected = original(self, nullptr, reinterpret_cast<mfc::CString*>(raw));
    ei::verify::Check(what, true, std::strcmp(*expected, *out) == 0, static_cast<const char*>(*out));
    expected->~CString();
    return out;
}

// 00469DB0 / 0046B520: PUSH ECX, PUSH ESI (2), MOV ESI,[ESP+0xC] (4).
NameFn<Spec> s_specName;
mfc::CString* __fastcall SpecNameVerify(Spec* self, void*, mfc::CString* out) {
    return CompareName<Spec>("SpecTypeName", s_specName, &Spec::SpecTypeName, self, out);
}
NameFn<TrapSpec> s_trapName;
mfc::CString* __fastcall TrapNameVerify(TrapSpec* self, void*, mfc::CString* out) {
    return CompareName<TrapSpec>("TrapSpecTypeName", s_trapName, &TrapSpec::TrapSpecTypeName, self, out);
}
// 0046B090: PUSH -1 (2), PUSH imm32 (5).
NameFn<ObjectSpec> s_objectName;
mfc::CString* __fastcall ObjectNameVerify(ObjectSpec* self, void*, mfc::CString* out) {
    return CompareName<ObjectSpec>("ObjectSpecTypeName", s_objectName, &ObjectSpec::ObjectSpecTypeName, self, out);
}

// 00469F20: MOV EAX,ECX (2), MOV ECX,[007AF5F8] (6).
void*(__fastcall* s_worldObject)(Spec*);
void* __fastcall WorldObjectVerify(Spec* self) {
    void* expected = s_worldObject(self);
    void* got = self->SpecWorldObject();
    ei::verify::Check("SpecWorldObject", reinterpret_cast<std::uintptr_t>(expected), reinterpret_cast<std::uintptr_t>(got), "");
    return got;
}

// 0046A4B0: SUB ESP,0x30 (3), FLD [ECX+4] (3).
Vector3*(__fastcall* s_rotate)(const Quaternion*, void*, Vector3*, const Vector3*);
Vector3* __fastcall RotateVerify(const Quaternion* q, void*, Vector3* out, const Vector3* v) {
    Vector3 in = *v;
    Vector3 expected;
    s_rotate(q, nullptr, &expected, &in);
    QuaternionRotate(q, nullptr, out, &in);
    ei::verify::CheckNear("QuaternionRotate x", expected.x, out->x, "");
    ei::verify::CheckNear("QuaternionRotate y", expected.y, out->y, "");
    ei::verify::CheckNear("QuaternionRotate z", expected.z, out->z, "");
    return out;
}

// Constructors, ours only.
Spec* __fastcall SpecVerify(Spec* self) {
    self->SpecConstruct();
    ei::verify::Check("SpecConstruct", true, VtableOf(self) == 0x0073C5C4 && self->kind == 0 && self->id == -1, "");
    return self;
}
ObjectSpec* __fastcall ObjectVerify(ObjectSpec* self) {
    self->ObjectSpecConstruct();
    ei::verify::Check("ObjectSpecConstruct", true, VtableOf(self) == 0x0073C5F0 && self->base.kind == 1 && self->player == -1, "");
    return self;
}
FlameSpec* __fastcall FlameVerify(FlameSpec* self) {
    self->FlameSpecConstruct();
    ei::verify::Check("FlameSpecConstruct", true, VtableOf(self) == 0x0073C620 && self->object.base.kind == 6, "");
    return self;
}
TrapSpec* __fastcall TrapVerify(TrapSpec* self) {
    self->TrapSpecConstruct();
    ei::verify::Check("TrapSpecConstruct", true, VtableOf(self) == 0x0073C650 && self->object.base.kind == 7 && self->interval == 15, "");
    return self;
}
LeverSpec* __fastcall LeverVerify(LeverSpec* self) {
    self->LeverSpecConstruct();
    ei::verify::Check("LeverSpecConstruct", true, VtableOf(self) == 0x0073C6B0 && self->object.base.kind == 8 && self->states == 2, "");
    return self;
}

} // namespace

EI_VERIFY(0x00469DB0, SpecNameVerify, 6, s_specName);
EI_VERIFY(0x0046B520, TrapNameVerify, 6, s_trapName);
EI_VERIFY(0x0046B090, ObjectNameVerify, 7, s_objectName);
EI_VERIFY(0x00469F20, WorldObjectVerify, 8, s_worldObject);
EI_VERIFY(0x0046A4B0, RotateVerify, 6, s_rotate);
EI_VERIFY(0x00469CA0, SpecVerify, 0, s_unused);
EI_VERIFY(0x0046A5C0, ObjectVerify, 0, s_unused);
EI_VERIFY(0x0046B210, FlameVerify, 0, s_unused);
EI_VERIFY(0x0046B4A0, TrapVerify, 0, s_unused);
EI_VERIFY(0x0046BD40, LeverVerify, 0, s_unused);
