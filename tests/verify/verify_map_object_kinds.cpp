// Checks for the member-less map object classes (src/game/map_object_kinds.cpp) and the counted
// reference (src/game/counted_ref.cpp), A-U6.
// - Class names / ids: pure (MOV EAX,imm32, 5 bytes).
// - Constructors, destructors: ours only (vtable, `this` returned).
// - AIRef Reset / Assign / Load: ours only (they release and take references): the slot holds what
//   it should; Save: the original and ours write into capture streams, compared.
// Reached by every scenario that builds zone1 (trees, stones, buildings, stuff) and by saving and
// loading (world_load.txt, magic_traps.txt, levers.txt); the references by the AI and the units
// every tick.
#include "verify.h"
#include "unit_capture_stream.h"

#include "../../src/game/map_object_kinds.h"
#include "../../src/game/unit_ai.h"

#include <cstring>

namespace {
using unitverify::CaptureStream;
void* s_unused;
using NameFn = const char*(__fastcall*)(CMapObject*);
using IdFn = int(__fastcall*)(CMapObject*);
NameFn s_treeName, s_buildingName, s_stoneName, s_stuffName;
IdFn s_treeId, s_buildingId, s_stoneId, s_stuffId;
void(__fastcall* s_refSave)(AIRef*, void*, AIStream*);

const char* CheckName(const char* what, NameFn original, NameFn ours, CMapObject* self) {
    const char* expected = original(self);
    ei::verify::Check(what, expected, ours(self), "");
    return expected;
}
int CheckId(const char* what, IdFn original, IdFn ours, CMapObject* self) {
    int expected = original(self);
    ei::verify::Check(what, expected, ours(self), "");
    return expected;
}

template <std::uintptr_t Vtable>
PlainMapObject<Vtable>* __fastcall ConstructVerify(PlainMapObject<Vtable>* self) {
    PlainMapObject<Vtable>* result = self->Construct();
    ei::verify::Check("PlainMapObject::Construct", true,
                      result == self && *reinterpret_cast<std::uintptr_t*>(self) == Vtable, "vtable %08X",
                      static_cast<unsigned>(Vtable));
    return result;
}
template <std::uintptr_t Vtable>
void __fastcall DestructVerify(PlainMapObject<Vtable>* self) {
    self->DestructBody();
    ei::verify::Check("PlainMapObject::DestructBody", true, self->companion == nullptr, "vtable %08X",
                      static_cast<unsigned>(Vtable));
}
template <std::uintptr_t Vtable>
PlainMapObject<Vtable>* __fastcall DeleteVerify(PlainMapObject<Vtable>* self, void*, int flags) {
    PlainMapObject<Vtable>* result = self->DeletingDestruct(flags);
    ei::verify::Check("PlainMapObject::DeletingDestruct", static_cast<void*>(self), static_cast<void*>(result),
                      "flags %d", flags);
    return result;
}
} // namespace

static const char* __fastcall TreeNameVerify(CMapObject* s) { return CheckName("TreeClassName", s_treeName, TreeClassName, s); }
EI_VERIFY(0x00529730, TreeNameVerify, 5, s_treeName);
static int __fastcall TreeIdVerify(CMapObject* s) { return CheckId("TreeClassId", s_treeId, TreeClassId, s); }
EI_VERIFY(0x00529740, TreeIdVerify, 5, s_treeId);
static const char* __fastcall BuildingNameVerify(CMapObject* s) { return CheckName("BuildingClassName", s_buildingName, BuildingClassName, s); }
EI_VERIFY(0x00529810, BuildingNameVerify, 5, s_buildingName);
static int __fastcall BuildingIdVerify(CMapObject* s) { return CheckId("BuildingClassId", s_buildingId, BuildingClassId, s); }
EI_VERIFY(0x00529820, BuildingIdVerify, 5, s_buildingId);
static const char* __fastcall StoneNameVerify(CMapObject* s) { return CheckName("StoneClassName", s_stoneName, StoneClassName, s); }
EI_VERIFY(0x005298F0, StoneNameVerify, 5, s_stoneName);
static int __fastcall StoneIdVerify(CMapObject* s) { return CheckId("StoneClassId", s_stoneId, StoneClassId, s); }
EI_VERIFY(0x00529900, StoneIdVerify, 5, s_stoneId);
static const char* __fastcall StuffNameVerify(CMapObject* s) { return CheckName("StuffClassName", s_stuffName, StuffClassName, s); }
EI_VERIFY(0x005299D0, StuffNameVerify, 5, s_stuffName);
static int __fastcall StuffIdVerify(CMapObject* s) { return CheckId("StuffClassId", s_stuffId, StuffClassId, s); }
EI_VERIFY(0x005299E0, StuffIdVerify, 5, s_stuffId);

EI_VERIFY(0x005296E0, ConstructVerify<mapkind::kTreeVtable>, 0, s_unused);
EI_VERIFY(0x00529770, DestructVerify<mapkind::kTreeVtable>, 0, s_unused);
EI_VERIFY(0x00529750, DeleteVerify<mapkind::kTreeVtable>, 0, s_unused);
EI_VERIFY(0x005297C0, ConstructVerify<mapkind::kBuildingVtable>, 0, s_unused);
EI_VERIFY(0x00529850, DestructVerify<mapkind::kBuildingVtable>, 0, s_unused);
EI_VERIFY(0x00529830, DeleteVerify<mapkind::kBuildingVtable>, 0, s_unused);
EI_VERIFY(0x005298A0, ConstructVerify<mapkind::kStoneVtable>, 0, s_unused);
EI_VERIFY(0x00529930, DestructVerify<mapkind::kStoneVtable>, 0, s_unused);
EI_VERIFY(0x00529910, DeleteVerify<mapkind::kStoneVtable>, 0, s_unused);
EI_VERIFY(0x00529980, ConstructVerify<mapkind::kStuffVtable>, 0, s_unused);
EI_VERIFY(0x00529A10, DestructVerify<mapkind::kStuffVtable>, 0, s_unused);
EI_VERIFY(0x005299F0, DeleteVerify<mapkind::kStuffVtable>, 0, s_unused);

// 0052B030 / 0052B050 / 0052A8F0 / 0050E220: ours only.
static void __fastcall RefResetVerify(AIRef* self) {
    self->Reset();
    ei::verify::Check("AIRef::Reset", true, self->object == nullptr, "");
}
EI_VERIFY(0x0052B030, RefResetVerify, 0, s_unused);
EI_VERIFY(0x0052B050, RefResetVerify, 0, s_unused);

static void __fastcall RefAssignVerify(AIRef* self, void*, CEngineObject* object) {
    self->Assign(object);
    ei::verify::Check("AIRef::Assign", true, self->object == object, "");
}
EI_VERIFY(0x0052A8F0, RefAssignVerify, 0, s_unused);

static void __fastcall RefLoadVerify(AIRef* self, void*, AIStream* stream, void* world) {
    self->Load(stream, world);
    ei::verify::Check("AIRef::Load", true, !self->object || self->object->refCount_ > 0, "");
}
EI_VERIFY(0x0050E220, RefLoadVerify, 0, s_unused);

// 0050E290: PUSH ECX; MOV EAX,[ECX]; TEST EAX,EAX (5 bytes).
static void __fastcall RefSaveVerify(AIRef* self, void*, AIStream* stream) {
    auto* real = reinterpret_cast<UnitStream*>(stream);
    CaptureStream expected(real), actual(real);
    s_refSave(self, nullptr, reinterpret_cast<AIStream*>(&expected));
    self->Save(reinterpret_cast<AIStream*>(&actual));
    ei::verify::Check("AIRef::Save", true, expected.Same(actual), "");
    expected.Forward(real);
}
EI_VERIFY(0x0050E290, RefSaveVerify, 5, s_refSave);
