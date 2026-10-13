// Checks for unit_stat_small.cpp, unit_small_methods.cpp, unit_controller_slots.cpp,
// ClientMotionChanged (unit_client_path.cpp) and CountedListLoad (counted_ref.cpp) (A-U12).
// Side by side (pure):
// - UnitStats::StrengthRatio / DexterityRatio (00523CB0, 00523CC0): the item panel's lines for a
//   hero's attributes (the inventory with a statistic's description open).
// - ExcessDamage / MaxDamage (00522AE0, 00522B20): the AI's attack options (any fight).
// - SchoolPrice (00527FF0): the trade panel's lists (a shop).
// - AttackParams::ScaleForDifficulty (0052E0D0): every attack's parameters (any fight).
// - SkillDatabase::SkillAt / LevelAt (00526FF0, 00524FA0): the statistics rebuilt (ResetToBase, a
//   new or loaded hero) and weapon skill bonuses (any fight). UnitModelAt / UnitPrototypeAt
//   (00539280, 0053AD30): footstep sounds, party units filled from prototypes, the unit database's
//   load.
// - The class names / ids (0052E280, 0052E290, 00533E00, 00533E10) and ControllerFiguresLoaded
//   (00533B90): units and controllers saved, loaded or logged; a server unit's figure loaded.
// - CUnit::HasPlayer / DoubleHeight / WornItem (005301A0, 00530560, 00530830): hits, loot,
//   attack steps and armour layers (any fight).
// Ours only (they change objects; the state afterwards is checked):
// - WritePackedVitals / ReadPackedVitals / WritePackedParams / ReadPackedParams (00523700-00523730):
//   none (the net state writers are rewritten and call the field themselves).
// - CUnit::SetMotionSpeeds (0052EA40), CUnitClientSpecific::SetMarkers (00535240): the path marker
//   message (a move order in a game with markers shown).
// - The base controller's Destruct / Delete / Create / Destroy (00533DB0, 00533D90, 00533DD0,
//   00533DE0): controllers made and dropped (Create / Destroy: none known, both classes override
//   them). Its zero slots of five bytes (00533BD0, 00533BE0, 00533BF0: client units refreshed,
//   hit or healed; 00533C80, 00533C90: a server unit's weapon added or removed).
// - CUnitClientSpecific::DropFaceTexture (00538EA0): a client object removed.
//   ClientMotionChanged (00535B20): a client unit's path replaced.
// - CountedListLoad (0052AF20): a saved game with magic traps or AI brains loaded.
// The other controller slots are under five bytes: too short to hook.
#include "verify.h"

#include "../../src/game/ai_motion.h"
#include "../../src/game/combat.h"
#include "../../src/game/effects.h"
#include "../../src/game/net_delta.h"
#include "../../src/game/unit.h"
#include "../../src/game/unit_ai.h"
#include "../../src/game/unit_client_models.h"
#include "../../src/game/unit_create.h"
#include "../../src/game/unit_db.h"

#include <cstring>

void __fastcall WritePackedVitals(const UnitVitalsPacked* vitals, DeltaField* field);
void __fastcall ReadPackedVitals(UnitVitalsPacked* vitals, DeltaField* field);
void __fastcall WritePackedParams(const UnitParams* params, DeltaField* field);
void __fastcall ReadPackedParams(UnitParams* params, DeltaField* field);
std::uint32_t __fastcall ExcessDamage(const std::uint16_t* damage, void*, const std::uint16_t* protection);
std::uint16_t __fastcall MaxDamage(const std::uint16_t* damage);
int __fastcall SchoolPrice(int from, int to);
UnitModelRecord* __fastcall UnitModelAt(UnitModelRecord** models, void*, std::uint32_t index);
UnitPrototypeRecord* __fastcall UnitPrototypeAt(UnitPrototypeTable* table, void*, std::uint32_t index);
const char* __fastcall UnitClassName(CUnit*);
int __fastcall UnitClassId(CUnit*);
void __fastcall ControllerDestruct(CUnitController* self);
CUnitController* __fastcall ControllerDelete(CUnitController* self, void*, unsigned flags);
int __fastcall ControllerCreate(CUnitController* self);
void __fastcall ControllerDestroy(CUnitController* self);
int __fastcall ControllerFiguresLoaded(CUnitController*);
int __fastcall ControllerZeroOne(CUnitController*, void*, int);
int __fastcall ControllerZeroThree(CUnitController*, void*, int, int, int);
bool __fastcall ControllerFalse(CUnitController*, void*, int);
const char* __fastcall ClientControllerClassName(CUnitClientSpecific*);
int __fastcall ClientControllerClassId(CUnitClientSpecific*);
void __fastcall ClientMotionChanged(CUnitClientSpecific* self);
void __fastcall CountedListLoad(mfc::CTypedPtrList<CEngineObject>* list, AIStream* stream, void* context);

namespace {
void* s_unused;
} // namespace

// --- Statistics ------------------------------------------------------------------------------------------

// FLD [ECX+10h] (3 bytes), FDIV [abs] (6).
static double(__fastcall* s_StrengthRatio)(UnitStats*);
static double __fastcall StrengthRatioVerify(UnitStats* self) {
    double expected = s_StrengthRatio(self);
    ei::verify::CheckNear("UnitStats::StrengthRatio", expected, self->StrengthRatio(), "");
    return expected;
}
EI_VERIFY(0x00523CB0, StrengthRatioVerify, 9, s_StrengthRatio);

static double(__fastcall* s_DexterityRatio)(UnitStats*);
static double __fastcall DexterityRatioVerify(UnitStats* self) {
    double expected = s_DexterityRatio(self);
    ei::verify::CheckNear("UnitStats::DexterityRatio", expected, self->DexterityRatio(), "");
    return expected;
}
EI_VERIFY(0x00523CC0, DexterityRatioVerify, 9, s_DexterityRatio);

static void __fastcall WritePackedVitalsVerify(const UnitVitalsPacked* vitals, DeltaField* field) {
    WritePackedVitals(vitals, field);
    ei::verify::Check("WritePackedVitals", true, true, "");
}
EI_VERIFY(0x00523700, WritePackedVitalsVerify, 0, s_unused);

static void __fastcall ReadPackedVitalsVerify(UnitVitalsPacked* vitals, DeltaField* field) {
    ReadPackedVitals(vitals, field);
    ei::verify::Check("ReadPackedVitals", true, true, "");
}
EI_VERIFY(0x00523710, ReadPackedVitalsVerify, 0, s_unused);

static void __fastcall WritePackedParamsVerify(const UnitParams* params, DeltaField* field) {
    WritePackedParams(params, field);
    ei::verify::Check("WritePackedParams", true, true, "");
}
EI_VERIFY(0x00523720, WritePackedParamsVerify, 0, s_unused);

static void __fastcall ReadPackedParamsVerify(UnitParams* params, DeltaField* field) {
    ReadPackedParams(params, field);
    ei::verify::Check("ReadPackedParams", true, true, "");
}
EI_VERIFY(0x00523730, ReadPackedParamsVerify, 0, s_unused);

// PUSH EBX, PUSH ESI, MOV ESI,[ESP+0Ch] (6 bytes). Only AX is a result.
static std::uint32_t(__fastcall* s_ExcessDamage)(const std::uint16_t*, void*, const std::uint16_t*);
static std::uint32_t __fastcall ExcessDamageVerify(const std::uint16_t* damage, void*, const std::uint16_t* protection) {
    std::uint32_t expected = s_ExcessDamage(damage, nullptr, protection);
    ei::verify::Check("ExcessDamage", expected & 0xFFFFu, ExcessDamage(damage, nullptr, protection) & 0xFFFFu, "");
    return expected;
}
EI_VERIFY(0x00522AE0, ExcessDamageVerify, 6, s_ExcessDamage);

// PUSH ESI, XOR EAX,EAX, LEA EDX,[ECX+2] (6 bytes).
static std::uint16_t(__fastcall* s_MaxDamage)(const std::uint16_t*);
static std::uint16_t __fastcall MaxDamageVerify(const std::uint16_t* damage) {
    std::uint16_t expected = s_MaxDamage(damage);
    ei::verify::Check("MaxDamage", expected, MaxDamage(damage), "");
    return expected;
}
EI_VERIFY(0x00522B20, MaxDamageVerify, 6, s_MaxDamage);

// SUB ESP,8 (3 bytes), FLD QWORD [abs] (6). The difference of two powers, truncated: one apart on a
// rounding edge is expected (rule 7).
static int(__fastcall* s_SchoolPrice)(int, int);
static int __fastcall SchoolPriceVerify(int from, int to) {
    int expected = s_SchoolPrice(from, to);
    int actual = SchoolPrice(from, to);
    ei::verify::Check("SchoolPrice", true, actual - expected <= 1 && expected - actual <= 1, "%d -> %d: %d / %d", from,
                      to, expected, actual);
    return expected;
}
EI_VERIFY(0x00527FF0, SchoolPriceVerify, 9, s_SchoolPrice);

// MOV EAX,[ESP+4] (4 bytes), PUSH ESI (1).
static void(__fastcall* s_ScaleForDifficulty)(AttackParams*, void*, int);
static void __fastcall ScaleForDifficultyVerify(AttackParams* self, void*, int keyed) {
    AttackParams expected = *self;
    s_ScaleForDifficulty(&expected, nullptr, keyed);
    self->ScaleForDifficulty(keyed);
    ei::verify::Check("AttackParams::ScaleForDifficulty", true,
                      expected.attack == self->attack && expected.defence == self->defence, "%d %d / %d %d",
                      expected.attack, expected.defence, self->attack, self->defence);
}
EI_VERIFY(0x0052E0D0, ScaleForDifficultyVerify, 5, s_ScaleForDifficulty);

// --- Database records --------------------------------------------------------------------------------------

// PUSH ESI, MOV ESI,[ECX], MOV ECX,[ECX+4] (6 bytes).
static SkillRecord*(__fastcall* s_SkillAt)(SkillDatabase*, void*, std::uint32_t);
static SkillRecord* __fastcall SkillAtVerify(SkillDatabase* self, void*, std::uint32_t index) {
    SkillRecord* expected = s_SkillAt(self, nullptr, index);
    ei::verify::Check("SkillDatabase::SkillAt", static_cast<void*>(expected), static_cast<void*>(self->SkillAt(index)),
                      "%u", index);
    return expected;
}
EI_VERIFY(0x00526FF0, SkillAtVerify, 6, s_SkillAt);

// MOV EDX,[ECX+10h], MOV EAX,[ECX+0Ch] (6 bytes).
static SkillLevelRecord*(__fastcall* s_LevelAt)(SkillDatabase*, void*, std::uint32_t);
static SkillLevelRecord* __fastcall LevelAtVerify(SkillDatabase* self, void*, std::uint32_t index) {
    SkillLevelRecord* expected = s_LevelAt(self, nullptr, index);
    ei::verify::Check("SkillDatabase::LevelAt", static_cast<void*>(expected), static_cast<void*>(self->LevelAt(index)),
                      "%u", index);
    return expected;
}
EI_VERIFY(0x00524FA0, LevelAtVerify, 6, s_LevelAt);

// PUSH ESI, MOV ESI,[ECX], MOV ECX,[ECX+4] (6 bytes).
static UnitModelRecord*(__fastcall* s_UnitModelAt)(UnitModelRecord**, void*, std::uint32_t);
static UnitModelRecord* __fastcall UnitModelAtVerify(UnitModelRecord** models, void*, std::uint32_t index) {
    UnitModelRecord* expected = s_UnitModelAt(models, nullptr, index);
    ei::verify::Check("UnitModelAt", static_cast<void*>(expected),
                      static_cast<void*>(UnitModelAt(models, nullptr, index)), "%u", index);
    return expected;
}
EI_VERIFY(0x00539280, UnitModelAtVerify, 6, s_UnitModelAt);

static UnitPrototypeRecord*(__fastcall* s_UnitPrototypeAt)(UnitPrototypeTable*, void*, std::uint32_t);
static UnitPrototypeRecord* __fastcall UnitPrototypeAtVerify(UnitPrototypeTable* table, void*, std::uint32_t index) {
    UnitPrototypeRecord* expected = s_UnitPrototypeAt(table, nullptr, index);
    ei::verify::Check("UnitPrototypeAt", static_cast<void*>(expected),
                      static_cast<void*>(UnitPrototypeAt(table, nullptr, index)), "%u", index);
    return expected;
}
EI_VERIFY(0x0053AD30, UnitPrototypeAtVerify, 6, s_UnitPrototypeAt);

// --- The unit ------------------------------------------------------------------------------------------------

// Each starts with MOV EAX,imm32 (5 bytes).
static const char*(__fastcall* s_UnitClassName)(CUnit*);
static const char* __fastcall UnitClassNameVerify(CUnit* self) {
    const char* expected = s_UnitClassName(self);
    const char* actual = UnitClassName(self);
    ei::verify::Check("UnitClassName", true, expected == actual && std::strcmp(actual, "CUnit") == 0, "%s", expected);
    return expected;
}
EI_VERIFY(0x0052E280, UnitClassNameVerify, 5, s_UnitClassName);

static int(__fastcall* s_UnitClassId)(CUnit*);
static int __fastcall UnitClassIdVerify(CUnit* self) {
    int expected = s_UnitClassId(self);
    ei::verify::Check("UnitClassId", expected, UnitClassId(self), "");
    return expected;
}
EI_VERIFY(0x0052E290, UnitClassIdVerify, 5, s_UnitClassId);

static void __fastcall SetMotionSpeedsVerify(CUnit* self, void*, float speed, float turnRate) {
    self->SetMotionSpeeds(speed, turnRate);
    const UnitMotion* motion = self->Motion();
    ei::verify::Check("CUnit::SetMotionSpeeds", true,
                      std::memcmp(&motion->speedScale, &speed, 4) == 0 && std::memcmp(&motion->baseTurnRate, &turnRate, 4) == 0,
                      "");
}
EI_VERIFY(0x0052EA40, SetMotionSpeedsVerify, 0, s_unused);

// MOV EDX,[ECX+24Ch] (6 bytes).
static int(__fastcall* s_HasPlayer)(const CUnit*);
static int __fastcall HasPlayerVerify(const CUnit* self) {
    int expected = s_HasPlayer(self);
    ei::verify::Check("CUnit::HasPlayer", expected, self->HasPlayer(), "");
    return expected;
}
EI_VERIFY(0x005301A0, HasPlayerVerify, 6, s_HasPlayer);

// FLD [ECX+14Ch] (6 bytes).
static double(__fastcall* s_DoubleHeight)(const CUnit*);
static double __fastcall DoubleHeightVerify(const CUnit* self) {
    double expected = s_DoubleHeight(self);
    ei::verify::CheckNear("CUnit::DoubleHeight", expected, self->DoubleHeight(), "");
    return expected;
}
EI_VERIFY(0x00530560, DoubleHeightVerify, 6, s_DoubleHeight);

// MOV EAX,[ESP+4] (4 bytes), MOV EAX,[ECX+EAX*4+480h] (7).
static CEngineObject*(__fastcall* s_WornItem)(const CUnit*, void*, int);
static CEngineObject* __fastcall WornItemVerify(const CUnit* self, void*, int slot) {
    CEngineObject* expected = s_WornItem(self, nullptr, slot);
    ei::verify::Check("CUnit::WornItem", static_cast<void*>(expected), static_cast<void*>(self->WornItem(slot)), "%d",
                      slot);
    return expected;
}
EI_VERIFY(0x00530830, WornItemVerify, 11, s_WornItem);

// --- The controllers -------------------------------------------------------------------------------------------

static void __fastcall ControllerDestructVerify(CUnitController* self) {
    ControllerDestruct(self);
    ei::verify::Check("CUnitController::Destruct", true, !self->unit_, "");
}
EI_VERIFY(0x00533DB0, ControllerDestructVerify, 0, s_unused);

static CUnitController* __fastcall ControllerDeleteVerify(CUnitController* self, void*, unsigned flags) {
    CUnitController* result = ControllerDelete(self, nullptr, flags);
    ei::verify::Check("CUnitController::Delete", static_cast<void*>(self), static_cast<void*>(result), "");
    return result;
}
EI_VERIFY(0x00533D90, ControllerDeleteVerify, 0, s_unused);

static int __fastcall ControllerCreateVerify(CUnitController* self) {
    int result = ControllerCreate(self);
    ei::verify::Check("CUnitController::Create", true, result == 1 && !self->unit_, "");
    return result;
}
EI_VERIFY(0x00533DD0, ControllerCreateVerify, 0, s_unused);

static void __fastcall ControllerDestroyVerify(CUnitController* self) {
    ControllerDestroy(self);
    ei::verify::Check("CUnitController::Destroy", true, !self->unit_, "");
}
EI_VERIFY(0x00533DE0, ControllerDestroyVerify, 0, s_unused);

// MOV EAX,1 (5 bytes).
static int(__fastcall* s_ControllerFiguresLoaded)(CUnitController*);
static int __fastcall ControllerFiguresLoadedVerify(CUnitController* self) {
    int expected = s_ControllerFiguresLoaded(self);
    ei::verify::Check("CUnitController::FiguresLoaded", expected, ControllerFiguresLoaded(self), "");
    return expected;
}
EI_VERIFY(0x00533B90, ControllerFiguresLoadedVerify, 5, s_ControllerFiguresLoaded);

static int __fastcall ControllerZeroOneVerify(CUnitController* self, void*, int value) {
    int result = ControllerZeroOne(self, nullptr, value);
    ei::verify::Check("CUnitController zero slot", 0, result, "");
    return result;
}
EI_VERIFY(0x00533BD0, ControllerZeroOneVerify, 0, s_unused);
EI_VERIFY(0x00533BF0, ControllerZeroOneVerify, 0, s_unused);

static int __fastcall ControllerZeroThreeVerify(CUnitController* self, void*, int first, int second, int third) {
    int result = ControllerZeroThree(self, nullptr, first, second, third);
    ei::verify::Check("CUnitController::TakeHit", 0, result, "");
    return result;
}
EI_VERIFY(0x00533BE0, ControllerZeroThreeVerify, 0, s_unused);

static bool __fastcall ControllerFalseVerify(CUnitController* self, void*, int value) {
    bool result = ControllerFalse(self, nullptr, value);
    ei::verify::Check("CUnitController false slot", false, result, "");
    return result;
}
EI_VERIFY(0x00533C80, ControllerFalseVerify, 0, s_unused);
EI_VERIFY(0x00533C90, ControllerFalseVerify, 0, s_unused);

static const char*(__fastcall* s_ClientControllerClassName)(CUnitClientSpecific*);
static const char* __fastcall ClientControllerClassNameVerify(CUnitClientSpecific* self) {
    const char* expected = s_ClientControllerClassName(self);
    const char* actual = ClientControllerClassName(self);
    ei::verify::Check("ClientControllerClassName", true, expected == actual && std::strcmp(actual, "CUnitClientSpecific") == 0, "%s", expected);
    return expected;
}
EI_VERIFY(0x00533E00, ClientControllerClassNameVerify, 5, s_ClientControllerClassName);

static int(__fastcall* s_ClientControllerClassId)(CUnitClientSpecific*);
static int __fastcall ClientControllerClassIdVerify(CUnitClientSpecific* self) {
    int expected = s_ClientControllerClassId(self);
    ei::verify::Check("ClientControllerClassId", expected, ClientControllerClassId(self), "");
    return expected;
}
EI_VERIFY(0x00533E10, ClientControllerClassIdVerify, 5, s_ClientControllerClassId);

static void __fastcall SetMarkersVerify(CUnitClientSpecific* self, void*, CParticleSource* path, CParticleSource* marker) {
    self->SetMarkers(path, marker);
    ei::verify::Check("CUnitClientSpecific::SetMarkers", true, self->sources[1] == path && self->sources[2] == marker, "");
}
EI_VERIFY(0x00535240, SetMarkersVerify, 0, s_unused);

static void __fastcall DropFaceTextureVerify(CUnitClientSpecific* self) {
    self->DropFaceTexture();
    ei::verify::Check("CUnitClientSpecific::DropFaceTexture", true, !self->faceTexture && self->blinks == 0, "");
}
EI_VERIFY(0x00538EA0, DropFaceTextureVerify, 0, s_unused);

static void __fastcall ClientMotionChangedVerify(CUnitClientSpecific* self) {
    ClientMotionChanged(self);
    ei::verify::Check("ClientMotionChanged", true, true, "");
}
EI_VERIFY(0x00535B20, ClientMotionChangedVerify, 0, s_unused);

static void __fastcall CountedListLoadVerify(mfc::CTypedPtrList<CEngineObject>* list, AIStream* stream, void* context) {
    const int before = list->count;
    CountedListLoad(list, stream, context);
    ei::verify::Check("CountedListLoad", true, list->count >= before && (list->count == 0 || list->tail), "%d -> %d",
                      before, list->count);
}
EI_VERIFY(0x0052AF20, CountedListLoadVerify, 0, s_unused);
