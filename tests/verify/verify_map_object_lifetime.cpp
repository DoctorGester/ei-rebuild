// Checks for the constructors and destructors (map_object_lifetime.cpp).
//
// The constructors allocate nothing and link nothing (their sub-constructors only set fields), so
// both versions run: the original into a scratch copy of the raw memory, ours into the object;
// every byte and the returned pointers are compared. The destructors free memory and release
// references: ours only, with the base state they must leave.
#include "verify.h"

#include "../../src/game/combat.h"
#include "../../src/game/map_object.h"
#include "../../src/game/map_object_figures.h"
#include "../../src/game/unit.h"

#include <cstring>

namespace {
void* s_unused;

constexpr std::uintptr_t kEngineObjectVtable = 0x0073B8E0;

// Runs the original constructor on a scratch copy of `self`'s first N bytes and ours on `self`;
// compares the bytes and that each returned its own object.
template <std::size_t N, typename R>
R* SameConstruction(const char* name, R* self, R*(__fastcall* original)(void*), R* (R::*ours)()) {
    alignas(16) unsigned char scratch[N];
    std::memcpy(scratch, self, N);
    R* expected = original(scratch);
    R* actual = (self->*ours)();
    int diff = -1;
    for (std::size_t i = 0; i < N && diff < 0; ++i)
        if (scratch[i] != reinterpret_cast<unsigned char*>(self)[i]) diff = static_cast<int>(i);
    bool ok = diff < 0 && expected == reinterpret_cast<R*>(scratch) && actual == self;
    ei::verify::Check(name, true, ok, "first difference at +0x%X", diff);
    return actual;
}

// After a destructor body chain: the engine object's base vtable and no owner.
bool BaseState(const CEngineObject* object) {
    return *reinterpret_cast<const std::uintptr_t*>(object) == kEngineObjectVtable && object->owner_ == nullptr;
}
} // namespace

// 00515BD0: PUSH -1; PUSH 0x71D526 (7 bytes).
static CObject3D*(__fastcall* s_constructObject3D)(void*);
static CObject3D* __fastcall ConstructObject3DVerify(CObject3D* self) {
    return SameConstruction<sizeof(CObject3D)>("CObject3D::Construct", self, s_constructObject3D, &CObject3D::Construct);
}
EI_VERIFY(0x00515BD0, ConstructObject3DVerify, 7, s_constructObject3D);

// 0050E2D0: PUSH -1; PUSH 0x71CFB0 (7 bytes).
static CMapObject*(__fastcall* s_constructMapObject)(void*);
static CMapObject* __fastcall ConstructMapObjectVerify(CMapObject* self) {
    return SameConstruction<sizeof(CMapObject)>("CMapObject::Construct", self, s_constructMapObject,
                                                &CMapObject::Construct);
}
EI_VERIFY(0x0050E2D0, ConstructMapObjectVerify, 7, s_constructMapObject);

// 0052E110: PUSH -1; PUSH 0x71E7D0 (7 bytes).
static CUnit*(__fastcall* s_constructUnit)(void*);
static CUnit* __fastcall ConstructUnitVerify(CUnit* self) {
    return SameConstruction<sizeof(CUnit)>("CUnit::Construct", self, s_constructUnit, &CUnit::Construct);
}
EI_VERIFY(0x0052E110, ConstructUnitVerify, 7, s_constructUnit);

// 00515610: PUSH -1; PUSH 0x71D44C (7 bytes). The constructor writes up to +0x94.
static CMapObjectServer*(__fastcall* s_constructServer)(void*);
static CMapObjectServer* __fastcall ConstructServerVerify(CMapObjectServer* self) {
    return SameConstruction<0x94>("CMapObjectServer::Construct", self, s_constructServer, &CMapObjectServer::Construct);
}
EI_VERIFY(0x00515610, ConstructServerVerify, 7, s_constructServer);

// 00510450: MOV EAX,ECX; XOR ECX,ECX; MOV [EAX+4],ECX (7 bytes). Writes up to +0x54.
static CMapObjectClientSpecific*(__fastcall* s_constructClient)(void*);
static CMapObjectClientSpecific* __fastcall ConstructClientVerify(CMapObjectClientSpecific* self) {
    return SameConstruction<0x54>("CMapObjectClientSpecific::Construct", self, s_constructClient,
                                  &CMapObjectClientSpecific::Construct);
}
EI_VERIFY(0x00510450, ConstructClientVerify, 7, s_constructClient);

// 0052E5B0. Ours only (releases the controller, frees the carried array): the blank state.
static void __fastcall ResetUnitVerify(CUnit* self) {
    self->Reset();
    bool ok = !self->controller && !self->player && !self->carried && self->carriedCount == 0 &&
              self->prototype23C == -1 && self->unknown_438 == 1 && self->activeWeapon == 0xFFFFFFFFu &&
              !self->weapons[0] && !self->armor[6] && !self->spells[7];
    ei::verify::Check("CUnit::Reset", true, ok, "unit %d", self->id_);
}
EI_VERIFY(0x0052E5B0, ResetUnitVerify, 0, s_unused);

// The destructor bodies. Ours only: the object ends in the engine object's base state.
static void __fastcall DestructObject3DVerify(CObject3D* self) {
    self->DestructBody();
    ei::verify::Check("CObject3D::DestructBody", true, BaseState(self), "");
}
EI_VERIFY(0x00515D70, DestructObject3DVerify, 0, s_unused);

static void __fastcall DestructMapObjectVerify(CMapObject* self) {
    self->DestructBody();
    ei::verify::Check("CMapObject::DestructBody", true, BaseState(self), "");
}
EI_VERIFY(0x0050E660, DestructMapObjectVerify, 0, s_unused);

static void __fastcall DestructUnitVerify(CUnit* self) {
    self->DestructBody();
    ei::verify::Check("CUnit::DestructBody", true, BaseState(self), "");
}
EI_VERIFY(0x0052E310, DestructUnitVerify, 0, s_unused);

// 0052E2A0. Ours only; the memory may be gone afterwards: counted.
static CUnit* __fastcall DeletingDestructorVerify(CUnit* self, void*, std::uint32_t flags) {
    CUnit* result = self->DeletingDestructor(flags);
    ei::verify::Check("CUnit::DeletingDestructor", true, result == self, "");
    return result;
}
EI_VERIFY(0x0052E2A0, DeletingDestructorVerify, 0, s_unused);

static void __fastcall DestructServerVerify(CMapObjectServer* self) {
    self->DestructBody();
    ei::verify::Check("CMapObjectServer::DestructBody", true, BaseState(self), "");
}
EI_VERIFY(0x00515720, DestructServerVerify, 0, s_unused);

static void __fastcall DestructClientVerify(CMapObjectClientSpecific* self) {
    self->DestructBody();
    bool ok = BaseState(self) && *reinterpret_cast<void**>(reinterpret_cast<char*>(self) + 0x1C) == nullptr;
    ei::verify::Check("CMapObjectClientSpecific::DestructBody", true, ok, "");
}
EI_VERIFY(0x00510560, DestructClientVerify, 0, s_unused);

static void __fastcall DestructUnitServerVerify(CUnitServer* self) {
    self->DestructBody();
    bool ok = BaseState(self) && self->unit == nullptr;
    ei::verify::Check("CUnitServer::DestructBody", true, ok, "");
}
EI_VERIFY(0x00546230, DestructUnitServerVerify, 0, s_unused);
