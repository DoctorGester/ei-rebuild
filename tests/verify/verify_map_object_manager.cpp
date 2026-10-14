// Checks for map_object_manager.cpp, map_object_companion_slots.cpp and object3d_small_methods.cpp
// (A-U12).
// Side by side (pure): the class names / ids and the folder (00512600, 00512610, 00512620,
// 00510520, 00510530, 005156E0, 005156F0): managers and companions saved, loaded and logged.
// Ours only (they change objects; the state afterwards is checked):
// - CMapObjectManager: Construct / Destruct / DeletingDestruct (005125E0, 00512650, 00512630) and
//   CreateManager (00512660): the world's managers made and dropped (game start, exit).
//   CreateMapObject (00512A30): every map object of a zone, spawns. FindAnimation (00512B20): the
//   interface's models (CUiModel::Stop, the main menu). GiveDefaults (00513630): an object added
//   without a prototype (the server's AddMapObject). PrototypeEquipment (005143E0): no scenario
//   (its caller, CParty::FillFromPrototype, is rewritten and reads the lists itself).
// - The companions: CompanionDestruct / Delete / Create / Destroy and the empty slots (00510710-
//   00510760, 005104C0, 005106D0-00510700): the base slots the server's companion keeps (hits,
//   effect changes on a hosted game's objects). DeleteClientCompanion / DeleteServer (00510540,
//   00515700): objects dropped. ShowHit (005117F0): a hit on a client object.
//   EffectParticle::Assign / ReleaseSource (00512490, 00511290): effects on client objects.
//   DeltaField::Destruct (00510340): objects and arrows dropped.
// - CObject3D: DeleteObject3D (00515D50): none known (map objects have their own slot 0).
//   SetSpecific (005161A0): none known (its caller 00455BA0 is not reached by a rewritten
//   path). SetPartFlags / ClearPartFlags / SetPartsUnknownD4 / D8 (00517910, 00517950, 00519500,
//   00519530): the interface's model views; the client's new objects. SetAnimationState
//   (005187D0): every object made. PlayAnimation / PlayAnimationSecond / AttachPartMesh /
//   RestartPose (005187B0, 00518840, 00518820, 00518860): interface models, party units, arrows,
//   weapon changes. BoxSetIgnored (005174E0): none (its caller LoadPart is rewritten and sets the
//   bit itself).
#include "verify.h"

#include "../../src/game/effects.h"
#include "../../src/game/map_object.h"
#include "../../src/game/map_object_figures.h"
#include "../../src/game/net_delta.h"
#include "../../src/game/unit.h"
#include "../../src/game/unit_create.h"

#include <cstring>

const char* __fastcall MapObjectManagerFolder(CMapObjectManager*);
const char* __fastcall MapObjectManagerClassName(CMapObjectManager*);
int __fastcall MapObjectManagerClassId(CMapObjectManager*);
void __fastcall CompanionIgnore(CMapObjectCompanion*, void*, int);
int __fastcall CompanionQuery(CMapObjectCompanion*, void*, int, int);
void __fastcall CompanionHit(CMapObjectCompanion*, void*, int, int);
const char* __fastcall ClientCompanionClassName(CMapObjectClientSpecific*);
int __fastcall ClientCompanionClassId(CMapObjectClientSpecific*);
const char* __fastcall ServerCompanionClassName(CMapObjectServer*);
int __fastcall ServerCompanionClassId(CMapObjectServer*);
void __fastcall BoxSetIgnored(std::uint32_t* flags, void*, bool ignored);

namespace {
void* s_unused;
constexpr std::uintptr_t kManagerVtable = std::uintptr_t(0x0073DCD8);
std::uintptr_t VtableOf(const void* object) { return *static_cast<const std::uintptr_t*>(object); }
} // namespace

// --- The manager ----------------------------------------------------------------------------------------

static CMapObjectManager* __fastcall ManagerConstructVerify(CMapObjectManager* self) {
    CMapObjectManager* result = self->Construct();
    ei::verify::Check("CMapObjectManager::Construct", true, result == self && VtableOf(self) == kManagerVtable, "");
    return result;
}
EI_VERIFY(0x005125E0, ManagerConstructVerify, 0, s_unused);

static void __fastcall ManagerDestructVerify(CMapObjectManager* self) {
    self->Destruct();
    ei::verify::Check("CMapObjectManager::Destruct", true, VtableOf(self) != kManagerVtable, "");
}
EI_VERIFY(0x00512650, ManagerDestructVerify, 0, s_unused);

static CMapObjectManager* __fastcall ManagerDeleteVerify(CMapObjectManager* self, void*, unsigned flags) {
    CMapObjectManager* result = self->DeletingDestruct(flags);
    ei::verify::Check("CMapObjectManager::DeletingDestruct", static_cast<void*>(self), static_cast<void*>(result), "");
    return result;
}
EI_VERIFY(0x00512630, ManagerDeleteVerify, 0, s_unused);

// Each starts with MOV EAX,imm32 (5 bytes).
static const char*(__fastcall* s_MapObjectManagerFolder)(CMapObjectManager*);
static const char* __fastcall MapObjectManagerFolderVerify(CMapObjectManager* self) {
    const char* expected = s_MapObjectManagerFolder(self);
    const char* actual = MapObjectManagerFolder(self);
    ei::verify::Check("MapObjectManagerFolder", true, expected == actual && std::strcmp(actual, "map objects") == 0, "%s", expected);
    return expected;
}
EI_VERIFY(0x00512600, MapObjectManagerFolderVerify, 5, s_MapObjectManagerFolder);

static const char*(__fastcall* s_MapObjectManagerClassName)(CMapObjectManager*);
static const char* __fastcall MapObjectManagerClassNameVerify(CMapObjectManager* self) {
    const char* expected = s_MapObjectManagerClassName(self);
    const char* actual = MapObjectManagerClassName(self);
    ei::verify::Check("MapObjectManagerClassName", true, expected == actual && std::strcmp(actual, "CMapObjectManager") == 0, "%s", expected);
    return expected;
}
EI_VERIFY(0x00512610, MapObjectManagerClassNameVerify, 5, s_MapObjectManagerClassName);

static const char*(__fastcall* s_ClientCompanionClassName)(CMapObjectClientSpecific*);
static const char* __fastcall ClientCompanionClassNameVerify(CMapObjectClientSpecific* self) {
    const char* expected = s_ClientCompanionClassName(self);
    const char* actual = ClientCompanionClassName(self);
    ei::verify::Check("ClientCompanionClassName", true, expected == actual && std::strcmp(actual, "CMapObjectClientSpecific") == 0, "%s", expected);
    return expected;
}
EI_VERIFY(0x00510520, ClientCompanionClassNameVerify, 5, s_ClientCompanionClassName);

static const char*(__fastcall* s_ServerCompanionClassName)(CMapObjectServer*);
static const char* __fastcall ServerCompanionClassNameVerify(CMapObjectServer* self) {
    const char* expected = s_ServerCompanionClassName(self);
    const char* actual = ServerCompanionClassName(self);
    ei::verify::Check("ServerCompanionClassName", true, expected == actual && std::strcmp(actual, "CMapObjectServer") == 0, "%s", expected);
    return expected;
}
EI_VERIFY(0x005156E0, ServerCompanionClassNameVerify, 5, s_ServerCompanionClassName);

static int(__fastcall* s_MapObjectManagerClassId)(CMapObjectManager*);
static int __fastcall MapObjectManagerClassIdVerify(CMapObjectManager* self) {
    int expected = s_MapObjectManagerClassId(self);
    ei::verify::Check("MapObjectManagerClassId", expected, MapObjectManagerClassId(self), "");
    return expected;
}
EI_VERIFY(0x00512620, MapObjectManagerClassIdVerify, 5, s_MapObjectManagerClassId);

static int(__fastcall* s_ClientCompanionClassId)(CMapObjectClientSpecific*);
static int __fastcall ClientCompanionClassIdVerify(CMapObjectClientSpecific* self) {
    int expected = s_ClientCompanionClassId(self);
    ei::verify::Check("ClientCompanionClassId", expected, ClientCompanionClassId(self), "");
    return expected;
}
EI_VERIFY(0x00510530, ClientCompanionClassIdVerify, 5, s_ClientCompanionClassId);

static int(__fastcall* s_ServerCompanionClassId)(CMapObjectServer*);
static int __fastcall ServerCompanionClassIdVerify(CMapObjectServer* self) {
    int expected = s_ServerCompanionClassId(self);
    ei::verify::Check("ServerCompanionClassId", expected, ServerCompanionClassId(self), "");
    return expected;
}
EI_VERIFY(0x005156F0, ServerCompanionClassIdVerify, 5, s_ServerCompanionClassId);

static int __fastcall CreateManagerVerify(CMapObjectManager* self) {
    int result = self->CreateManager();
    ei::verify::Check("CMapObjectManager::CreateManager", true, result == 0 || result == 1, "%d", result);
    return result;
}
EI_VERIFY(0x00512660, CreateManagerVerify, 0, s_unused);

static CEngineObject* __fastcall CreateMapObjectVerify(CMapObjectManager* self, void*, int classId, int id) {
    CEngineObject* object = self->CreateMapObject(classId, id);
    bool ok = !object || (static_cast<CMapObject*>(object)->attached && static_cast<CMapObject*>(object)->companion);
    ei::verify::Check("CMapObjectManager::CreateMapObject", true, ok, "class %X id %d", classId, id);
    return object;
}
EI_VERIFY(0x00512A30, CreateMapObjectVerify, 0, s_unused);

static FigureAnimation* __fastcall FindAnimationVerify(CMapObjectManager* self, void*, const mfc::CString* figure,
                                                       std::uint32_t key) {
    FigureAnimation* result = self->FindAnimation(figure, key);
    bool ok = !result || ((result->key ^ key) & 0xFFFF8000u) == 0;
    ei::verify::Check("CMapObjectManager::FindAnimation", true, ok, "%s key %X", static_cast<const char*>(*figure), key);
    return result;
}
EI_VERIFY(0x00512B20, FindAnimationVerify, 0, s_unused);

static int __fastcall GiveDefaultsVerify(CMapObjectManager* self, void*, CEngineObject* object) {
    int result = self->GiveDefaults(object);
    ei::verify::Check("CMapObjectManager::GiveDefaults", true, result == 1 && object->unknown_18 == 0, "");
    return result;
}
EI_VERIFY(0x00513630, GiveDefaultsVerify, 0, s_unused);

static int __fastcall PrototypeEquipmentVerify(CMapObjectManager* self, void*, const char* name,
                                               mfc::CStringArray* armour, mfc::CString* weapon,
                                               mfc::CString* secondWeapon, mfc::CStringArray* spells) {
    int result = self->PrototypeEquipment(name, armour, weapon, secondWeapon, spells);
    ei::verify::Check("CMapObjectManager::PrototypeEquipment", true, true, "");
    return result;
}
EI_VERIFY(0x005143E0, PrototypeEquipmentVerify, 0, s_unused);

// --- The companions -------------------------------------------------------------------------------------

static void __fastcall CompanionDestructVerify(CMapObjectCompanion* self) {
    self->DestructCompanion();
    ei::verify::Check("CMapObjectCompanion::Destruct", true,
                      VtableOf(self) == CEngineObject::kVtableAddress && !self->owner, "");
}
EI_VERIFY(0x00510730, CompanionDestructVerify, 0, s_unused);

static CMapObjectCompanion* __fastcall CompanionDeleteVerify(CMapObjectCompanion* self, void*, unsigned flags) {
    CMapObjectCompanion* result = self->DeleteCompanion(flags);
    ei::verify::Check("CMapObjectCompanion::Delete", static_cast<void*>(self), static_cast<void*>(result), "");
    return result;
}
EI_VERIFY(0x00510710, CompanionDeleteVerify, 0, s_unused);

static int __fastcall CompanionCreateVerify(CMapObjectCompanion* self) {
    int result = self->CreateCompanion();
    ei::verify::Check("CMapObjectCompanion::Create", true, result == 1 && !self->owner, "");
    return result;
}
EI_VERIFY(0x00510750, CompanionCreateVerify, 0, s_unused);

static void __fastcall CompanionDestroyVerify(CMapObjectCompanion* self) {
    self->DestroyCompanion();
    ei::verify::Check("CMapObjectCompanion::Destroy", true, !self->owner, "");
}
EI_VERIFY(0x00510760, CompanionDestroyVerify, 0, s_unused);

static int __fastcall CompanionUpdateVerify(CMapObjectCompanion* self) {
    int result = self->UpdateCompanion();
    ei::verify::Check("CMapObjectCompanion::Update", 0, result, "");
    return result;
}
EI_VERIFY(0x005106E0, CompanionUpdateVerify, 0, s_unused);

static void __fastcall CompanionIgnoreVerify(CMapObjectCompanion* self, void*, int value) {
    CompanionIgnore(self, nullptr, value);
    ei::verify::Check("CMapObjectCompanion empty slot", true, true, "");
}
EI_VERIFY(0x005106D0, CompanionIgnoreVerify, 0, s_unused);
EI_VERIFY(0x00510700, CompanionIgnoreVerify, 0, s_unused);

static int __fastcall CompanionQueryVerify(CMapObjectCompanion* self, void*, int first, int second) {
    int result = CompanionQuery(self, nullptr, first, second);
    ei::verify::Check("CMapObjectCompanion::Query", 0, result, "");
    return result;
}
EI_VERIFY(0x005104C0, CompanionQueryVerify, 0, s_unused);

static void __fastcall CompanionHitVerify(CMapObjectCompanion* self, void*, int damage, int kind) {
    CompanionHit(self, nullptr, damage, kind);
    ei::verify::Check("CMapObjectCompanion::Hit", true, true, "");
}
EI_VERIFY(0x005106F0, CompanionHitVerify, 0, s_unused);

static CMapObjectClientSpecific* __fastcall ClientDeleteVerify(CMapObjectClientSpecific* self, void*, unsigned flags) {
    CMapObjectClientSpecific* result = self->DeleteClientCompanion(flags);
    ei::verify::Check("CMapObjectClientSpecific::DeleteClientCompanion", static_cast<void*>(self),
                      static_cast<void*>(result), "");
    return result;
}
EI_VERIFY(0x00510540, ClientDeleteVerify, 0, s_unused);

static void __fastcall ShowHitVerify(CMapObjectClientSpecific* self, void*, int damage, int kind) {
    self->ShowHit(damage, kind);
    ei::verify::Check("CMapObjectClientSpecific::ShowHit", true, true, "");
}
EI_VERIFY(0x005117F0, ShowHitVerify, 0, s_unused);

static CMapObjectServer* __fastcall ServerDeleteVerify(CMapObjectServer* self, void*, unsigned flags) {
    CMapObjectServer* result = self->DeleteServer(flags);
    ei::verify::Check("CMapObjectServer::DeleteServer", static_cast<void*>(self), static_cast<void*>(result), "");
    return result;
}
EI_VERIFY(0x00515700, ServerDeleteVerify, 0, s_unused);

static EffectParticle* __fastcall ParticleAssignVerify(EffectParticle* self, void*, const EffectParticle* other) {
    EffectParticle* result = self->Assign(other);
    bool ok = result == self && self->type == other->type && self->power == other->power &&
              self->source == other->source && self->present == other->present;
    ei::verify::Check("EffectParticle::Assign", true, ok, "");
    return result;
}
EI_VERIFY(0x00512490, ParticleAssignVerify, 0, s_unused);

static void __fastcall ParticleReleaseVerify(EffectParticle* self) {
    CParticleSource* source = self->source;
    int before = source ? reinterpret_cast<CEngineObject*>(source)->refCount_ : 0;
    self->ReleaseSource();
    // The source is looked at again only when another holder keeps it alive.
    ei::verify::Check("EffectParticle::ReleaseSource", true, before < 2 || reinterpret_cast<CEngineObject*>(source)->refCount_ == before - 1, "");
}
EI_VERIFY(0x00511290, ParticleReleaseVerify, 0, s_unused);

static void __fastcall FieldDestructVerify(DeltaField* self) {
    self->Destruct();
    ei::verify::Check("DeltaField::Destruct", true, true, "");
}
EI_VERIFY(0x00510340, FieldDestructVerify, 0, s_unused);

// --- The 3D object ----------------------------------------------------------------------------------------

static CObject3D* __fastcall Object3DDeleteVerify(CObject3D* self, void*, unsigned flags) {
    CObject3D* result = self->DeleteObject3D(flags);
    ei::verify::Check("CObject3D::DeleteObject3D", static_cast<void*>(self), static_cast<void*>(result), "");
    return result;
}
EI_VERIFY(0x00515D50, Object3DDeleteVerify, 0, s_unused);

static void __fastcall SetSpecificVerify(CObject3D* self, void*, CObject3DSpecific* half) {
    self->SetSpecific(half);
    ei::verify::Check("CObject3D::SetSpecific", true, self->attached == half && (!half || half->object == self), "");
}
EI_VERIFY(0x005161A0, SetSpecificVerify, 0, s_unused);

static void __fastcall BoxSetIgnoredVerify(std::uint32_t* flags, void*, bool ignored) {
    std::uint32_t before = *flags;
    BoxSetIgnored(flags, nullptr, ignored);
    ei::verify::Check("BoxSetIgnored", ignored ? before | 0x800u : before & ~0x800u, *flags, "");
}
EI_VERIFY(0x005174E0, BoxSetIgnoredVerify, 0, s_unused);

namespace {
bool EveryPart(const CObject3D* object, bool (*test)(const FigurePart*, std::uint32_t), std::uint32_t value) {
    for (int i = 0; i < object->parts.size; ++i)
        if (!test(static_cast<const FigurePart*>(object->parts[i]), value)) return false;
    return true;
}
} // namespace

static void __fastcall SetPartFlagsVerify(CObject3D* self, void*, std::uint32_t mask) {
    self->SetPartFlags(mask);
    bool ok = EveryPart(self, [](const FigurePart* part, std::uint32_t m) { return (part->flags_ & m) == m; }, mask);
    ei::verify::Check("CObject3D::SetPartFlags", true, ok, "%X", mask);
}
EI_VERIFY(0x00517910, SetPartFlagsVerify, 0, s_unused);

static void __fastcall ClearPartFlagsVerify(CObject3D* self, void*, std::uint32_t mask) {
    self->ClearPartFlags(mask);
    bool ok = EveryPart(self, [](const FigurePart* part, std::uint32_t m) { return (part->flags_ & m) == 0; }, mask);
    ei::verify::Check("CObject3D::ClearPartFlags", true, ok, "%X", mask);
}
EI_VERIFY(0x00517950, ClearPartFlagsVerify, 0, s_unused);

static void __fastcall SetPartsD4Verify(CObject3D* self, void*, std::uint32_t value) {
    self->SetPartsUnknownD4(value);
    bool ok = EveryPart(self, [](const FigurePart* part, std::uint32_t v) { return part->unknown_0D4 == v; }, value);
    ei::verify::Check("CObject3D::SetPartsUnknownD4", true, ok, "%X", value);
}
EI_VERIFY(0x00519500, SetPartsD4Verify, 0, s_unused);

static void __fastcall SetPartsD8Verify(CObject3D* self, void*, std::uint32_t value) {
    self->SetPartsUnknownD8(value);
    bool ok = EveryPart(self, [](const FigurePart* part, std::uint32_t v) { return part->unknown_0D8 == v; }, value);
    ei::verify::Check("CObject3D::SetPartsUnknownD8", true, ok, "%X", value);
}
EI_VERIFY(0x00519530, SetPartsD8Verify, 0, s_unused);

static void __fastcall SetAnimationStateVerify(CObject3D* self, void*, CFigureAnimationState* state) {
    self->SetAnimationState(state);
    ei::verify::Check("CObject3D::SetAnimationState", true, self->animation == state && (!state || state->owner == self),
                      "");
}
EI_VERIFY(0x005187D0, SetAnimationStateVerify, 0, s_unused);

static void __fastcall PlayAnimationVerify(CObject3D* self, void*, FigureAnimation* animation, int unused) {
    self->PlayAnimation(animation, unused);
    ei::verify::Check("CObject3D::PlayAnimation", true, !self->animation || self->animation->current == animation, "");
}
EI_VERIFY(0x005187B0, PlayAnimationVerify, 0, s_unused);

static int __fastcall PlayAnimationSecondVerify(CObject3D* self, void*, int unused, FigureAnimation* animation) {
    int result = self->PlayAnimationSecond(unused, animation);
    ei::verify::Check("CObject3D::PlayAnimationSecond", true,
                      result == 1 && (!self->animation || self->animation->current == animation), "");
    return result;
}
EI_VERIFY(0x00518840, PlayAnimationSecondVerify, 0, s_unused);

static int __fastcall AttachPartMeshVerify(CObject3D* self, void*, CEngineObject* part) {
    int result = self->AttachPartMesh(part);
    ei::verify::Check("CObject3D::AttachPartMesh", true, self->animation || result == 0, "%d", result);
    return result;
}
EI_VERIFY(0x00518820, AttachPartMeshVerify, 0, s_unused);

static void __fastcall RestartPoseVerify(CObject3D* self) {
    self->RestartPose();
    ei::verify::Check("CObject3D::RestartPose", true, !self->animation || self->animation->time == 0.0f, "");
}
EI_VERIFY(0x00518860, RestartPoseVerify, 0, s_unused);
