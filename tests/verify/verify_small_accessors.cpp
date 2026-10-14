// Checks for small_accessors.cpp (A-E1). Main thread.
//  - Side by side (pure, or one field written: the field is put back between the two runs): the
//    Vector3 from parts and the scene helper's setters (SceneInfo::Load, any map or model load),
//    the particle manager's constructor tail (the client world's set-up), and the script
//    accessors 004B9F50 / 60 / 90, 004BA010, 004BA030, 004BA060 / 70 / 80, 004BA9C0. The script
//    ones have no caller left in the exe (Execute is rewritten), so no scenario reaches them.
//  - Ours only: the counted pointer's destructor (the prologue is a short jump; the world server's
//    tick calls it), 004B2520 and 004B9FD0 (the same), 004BAA20 (First moves two reference
//    counts and has its own check) and 004BC1F0 (it writes to the stream).
#include "verify.h"

#include "../../src/game/base_manager.h"
#include "../../src/game/combat.h"
#include "../../src/game/core_refcount.h"
#include "../../src/game/core_streams.h"
#include "../../src/game/effects.h"
#include "../../src/game/engine_object.h"
#include "../../src/game/item.h"
#include "../../src/game/map_object.h"
#include "../../src/game/scene_loader.h"
#include "../../src/game/script.h"
#include "../../src/game/unit.h"

#include <cstdint>
#include <cstring>

// small_accessors.cpp (no header declares the shims).
void __fastcall DropObjectReference(CEngineObject** slot);
void __fastcall ParticleManagerClearQuietStart(CParticleManager* manager);
CEngineObject* __fastcall ScriptRefCompanion(const ScriptObjectRef* ref);
int __fastcall ScriptSessionActive(const CRefCounted* session);
int __fastcall ScriptRound(void*, void*, float value);
void __fastcall ScriptItemRepair(CItem* item);
CEngineObject* __fastcall ScriptUnitController(const CUnit* unit);
int __fastcall ScriptUnitCarriedCount(const CUnit* unit);
UnitStats* __fastcall ScriptControllerStats(CUnitServer* controller);
void __fastcall ScriptWriteText(const mfc::CString* text, CStream* stream);

namespace {
void* s_unused;

bool SameVector(const Vector3& a, const Vector3& b) { return std::memcmp(&a, &b, sizeof a) == 0; }

// ---- Ours only ----

void __fastcall DropReferenceVerify(CEngineObject** slot) {
    CEngineObject* object = *slot;
    int before = object ? object->refCount_ : 0;
    DropObjectReference(slot);
    bool good = *slot == nullptr && (!object || object->refCount_ == before - 1);
    ei::verify::Check("counted pointer destructor (00455C80)", true, good, "count before %d", before);
}

CEngineObject* __fastcall RefCompanionVerify(const ScriptObjectRef* ref) {
    CEngineObject* result = ScriptRefCompanion(ref);
    ei::verify::Check("script accessor 004B2520", true, ref->object != nullptr || result == nullptr, "");
    return result;
}

int __fastcall SessionActiveVerify(const CRefCounted* session) {
    int result = ScriptSessionActive(session);
    ei::verify::Check("script accessor 004B9FD0", true, result == 0 || (result == 1 && session != nullptr), "");
    return result;
}

CEngineObject* __fastcall IteratorFirstVerify(ObjectListIterator* iterator, void*, CBaseManager* list) {
    CEngineObject* result = iterator->First(list);
    ei::verify::Check("script accessor 004BAA20", true, iterator->manager == list, "");
    return result;
}

void __fastcall WriteTextVerify(const mfc::CString* text, CStream* stream) {
    // No check: the callers left in the exe pass objects that are not CStreams (only their slot 2
    // is the same call), so the position cannot be asked. It differed on all 114,088 calls of a
    // run on 2026-10-13 while the port matches the original's eleven instructions.
    ScriptWriteText(text, stream);
}

// ---- Side by side ----

// 00471A90: MOV EDX,[ESP+8] (4), MOV EAX,ECX (2).
Vector3*(__fastcall* s_fromParts)(Vector3*, void*, float, float, float);
Vector3* __fastcall FromPartsVerify(Vector3* self, void*, float x, float y, float z) {
    Vector3 expected;
    Vector3* expectedResult = s_fromParts(&expected, nullptr, x, y, z);
    Vector3* result = self->ConstructScene(x, y, z);
    bool same = expectedResult == &expected && result == self && SameVector(expected, *self);
    ei::verify::Check("Vector3 from parts (00471A90)", true, same, "%g %g %g", x, y, z);
    return result;
}

// 00471AB0 / AD0 / AF0: MOV EAX,[ESP+4] (4), MOV EDX,[EAX] (2). The original on the field, which is
// then put back, then ours.
using HelperSetter = void(__fastcall*)(CMaterial*, void*, const Vector3*);
void CheckHelperSetter(const char* name, HelperSetter original, HelperSetter ours, CMaterial* helper,
                       const Vector3* value, Vector4 MaterialColors::*field) {
    // The colour's red, green and blue: its first three floats.
    Vector3& rgb = *reinterpret_cast<Vector3*>(&(helper->colors.*field));
    Vector3 saved = rgb;
    Vector3 input = *value;
    original(helper, nullptr, &input);
    Vector3 expected = rgb;
    rgb = saved;
    ours(helper, nullptr, value);
    ei::verify::Check(name, true, SameVector(expected, rgb), "");
}
void __fastcall DiffuseOurs(CMaterial* helper, void*, const Vector3* value) { helper->SetDiffuse(value); }
void __fastcall AmbientOurs(CMaterial* helper, void*, const Vector3* value) { helper->SetAmbient(value); }
void __fastcall SpecularOurs(CMaterial* helper, void*, const Vector3* value) { helper->SetSpecular(value); }
HelperSetter s_setVectorB;
void __fastcall SetVectorBVerify(CMaterial* helper, void*, const Vector3* value) {
    CheckHelperSetter("scene helper setter 00471AB0", s_setVectorB, DiffuseOurs, helper, value,
                      &MaterialColors::diffuse);
}
HelperSetter s_setVectorA;
void __fastcall SetVectorAVerify(CMaterial* helper, void*, const Vector3* value) {
    CheckHelperSetter("scene helper setter 00471AD0", s_setVectorA, AmbientOurs, helper, value,
                      &MaterialColors::ambient);
}
HelperSetter s_setVectorC;
void __fastcall SetVectorCVerify(CMaterial* helper, void*, const Vector3* value) {
    CheckHelperSetter("scene helper setter 00471AF0", s_setVectorC, SpecularOurs, helper, value,
                      &MaterialColors::specular);
}

// 004981A0: MOV DWORD PTR [ECX+0xE4],0 (10).
void(__fastcall* s_clearQuietStart)(CParticleManager*);
void __fastcall ClearQuietStartVerify(CParticleManager* manager) {
    int saved = manager->quietStart;
    s_clearQuietStart(manager);
    int expected = manager->quietStart;
    manager->quietStart = saved;
    ParticleManagerClearQuietStart(manager);
    ei::verify::Check("particle manager constructor tail (004981A0)", expected, manager->quietStart, "");
}

// 004B9F50 / 004B9F60: MOV EAX,[ESP+4] (4), MOV EDX,[ECX+8] (3).
using FlagSetter = void(__fastcall*)(CEngineObject*, void*, std::uint32_t);
void CheckFlagSetter(const char* name, FlagSetter original, FlagSetter ours, CEngineObject* object,
                     std::uint32_t bits) {
    std::uint32_t saved = object->flags_;
    original(object, nullptr, bits);
    std::uint32_t expected = object->flags_;
    object->flags_ = saved;
    ours(object, nullptr, bits);
    ei::verify::Check(name, expected, object->flags_, "flags %08X bits %08X", saved, bits);
}
FlagSetter s_setFlags;
void __fastcall SetFlagBitsOurs(CEngineObject* object, void*, std::uint32_t bits) { object->SetFlagBits(bits); }
void __fastcall ClearFlagBitsOurs(CEngineObject* object, void*, std::uint32_t bits) { object->ClearFlagBits(bits); }
void __fastcall SetFlagsVerify(CEngineObject* object, void*, std::uint32_t bits) {
    CheckFlagSetter("script accessor 004B9F50", s_setFlags, SetFlagBitsOurs, object, bits);
}
FlagSetter s_clearFlags;
void __fastcall ClearFlagsVerify(CEngineObject* object, void*, std::uint32_t bits) {
    CheckFlagSetter("script accessor 004B9F60", s_clearFlags, ClearFlagBitsOurs, object, bits);
}

// 004B9F90: MOV EAX,[ESP+4] (4), ADD ECX,0x1C (3).
void(__fastcall* s_setPosition)(CMapObject*, void*, const Vector3*);
void __fastcall SetPositionVerify(CMapObject* object, void*, const Vector3* point) {
    Vector3 saved = object->position;
    Vector3 input = *point;
    s_setPosition(object, nullptr, &input);
    Vector3 expected = object->position;
    object->position = saved;
    object->SetScriptPosition(point);
    ei::verify::Check("script accessor 004B9F90", true, SameVector(expected, object->position), "");
}

// 004BA010: PUSH EBP (1), MOV EBP,ESP (2), PUSH ECX (1), FLD DWORD PTR [EBP+8] (3).
int(__fastcall* s_round)(void*, void*, float);
int __fastcall RoundVerify(void* self, void*, float value) {
    int expected = s_round(self, nullptr, value);
    int actual = ScriptRound(self, nullptr, value);
    ei::verify::Check("script accessor 004BA010", expected, actual, "%g", value);
    return actual;
}

// 004BA030: MOV EAX,[ECX+0x24] (3), MOV [ECX+0x20],EAX (3).
void(__fastcall* s_repair)(CItem*);
void __fastcall RepairVerify(CItem* item) {
    std::uint32_t saved = item->condition_;
    s_repair(item);
    std::uint32_t expected = item->condition_;
    item->condition_ = saved;
    ScriptItemRepair(item);
    ei::verify::Check("script accessor 004BA030", expected, item->condition_, "");
}

// 004BA060 / 004BA070 / 004BA080 / 004BA9C0: one six-byte MOV or LEA.
CEngineObject*(__fastcall* s_controller)(const CUnit*);
CEngineObject* __fastcall ControllerVerify(const CUnit* unit) {
    CEngineObject* expected = s_controller(unit);
    CEngineObject* actual = ScriptUnitController(unit);
    ei::verify::Check("script accessor 004BA060", true, expected == actual, "");
    return actual;
}
int(__fastcall* s_carriedCount)(const CUnit*);
int __fastcall CarriedCountVerify(const CUnit* unit) {
    int expected = s_carriedCount(unit);
    int actual = ScriptUnitCarriedCount(unit);
    ei::verify::Check("script accessor 004BA070", expected, actual, "");
    return actual;
}
CEngineObject*(__fastcall* s_carriedItem)(const CUnit*, void*, int);
CEngineObject* __fastcall CarriedItemVerify(const CUnit* unit, void*, int index) {
    CEngineObject* expected = s_carriedItem(unit, nullptr, index);
    CEngineObject* actual = unit->CarriedItem(index);
    ei::verify::Check("script accessor 004BA080", true, expected == actual, "index %d", index);
    return actual;
}
UnitStats*(__fastcall* s_stats)(CUnitServer*);
UnitStats* __fastcall StatsVerify(CUnitServer* controller) {
    UnitStats* expected = s_stats(controller);
    UnitStats* actual = ScriptControllerStats(controller);
    ei::verify::Check("script accessor 004BA9C0", true, expected == actual, "");
    return actual;
}
} // namespace

EI_VERIFY(0x00455C80, DropReferenceVerify, 0, s_unused);
EI_VERIFY(0x00455610, DropReferenceVerify, 0, s_unused);
EI_VERIFY(0x004BB050, DropReferenceVerify, 0, s_unused);
EI_VERIFY(0x004B2520, RefCompanionVerify, 0, s_unused);
EI_VERIFY(0x004B9FD0, SessionActiveVerify, 0, s_unused);
EI_VERIFY(0x004BAA20, IteratorFirstVerify, 0, s_unused);
EI_VERIFY(0x004BC1F0, WriteTextVerify, 0, s_unused);
EI_VERIFY(0x00471A90, FromPartsVerify, 6, s_fromParts);
EI_VERIFY(0x00471AB0, SetVectorBVerify, 6, s_setVectorB);
EI_VERIFY(0x00471AD0, SetVectorAVerify, 6, s_setVectorA);
EI_VERIFY(0x00471AF0, SetVectorCVerify, 6, s_setVectorC);
EI_VERIFY(0x004981A0, ClearQuietStartVerify, 10, s_clearQuietStart);
EI_VERIFY(0x004B9F50, SetFlagsVerify, 7, s_setFlags);
EI_VERIFY(0x004B9F60, ClearFlagsVerify, 7, s_clearFlags);
EI_VERIFY(0x004B9F90, SetPositionVerify, 7, s_setPosition);
EI_VERIFY(0x004BA010, RoundVerify, 7, s_round);
EI_VERIFY(0x004BA030, RepairVerify, 6, s_repair);
EI_VERIFY(0x004BA060, ControllerVerify, 6, s_controller);
EI_VERIFY(0x004BA070, CarriedCountVerify, 6, s_carriedCount);
EI_VERIFY(0x004BA080, CarriedItemVerify, 6, s_carriedItem);
EI_VERIFY(0x004BA9C0, StatsVerify, 6, s_stats);
