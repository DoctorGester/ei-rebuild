// Checks for class_slots.cpp (A-E1). Main thread.
//  - The class names and ids, CHoover's Create and the unit's forward speed: pure, side by side.
//    All are reached through vtables only; which scenarios call them is not known (no run). The
//    lightning ones need a bolt (a lightning spell or a script's lightning).
//  - The deleting destructors, LightningSegment::Destruct, CLightningManager::Create,
//    FakeUnitCreate: ours only (they free or create); what can be read afterwards is checked.
//  - The unit's TakeHit, ObjectDeleted and SetScriptName slots: ours only (one call on the
//    controller or one assignment; running it twice would hit twice); the result or the text.
#include "verify.h"

#include "../../src/game/lightning.h"
#include "../../src/game/object.h"
#include "../../src/game/scene_loader.h"
#include "../../src/game/spell.h"
#include "../../src/game/unit.h"

#include <cstdint>
#include <cstring>

// class_slots.cpp (no header declares the shims).
CHoover* __fastcall HooverDeletingDestruct(CHoover* self, void*, unsigned flags);
int __fastcall HooverCreate(CHoover*);
const char* __fastcall HooverGetClassName(const CHoover*);
int __fastcall HooverGetClassId(const CHoover*);
CLightning* __fastcall LightningDeletingDestruct(CLightning* self, void*, unsigned flags);
const char* __fastcall LightningGetClassName(const CLightning*);
int __fastcall LightningGetClassId(const CLightning*);
int __fastcall FakeUnitCreate(scene::FakeUnitScene* self);
const char* __fastcall FakeUnitGetClassName(const CUnit*);
int __fastcall UnitTakeHit(CUnit* unit, void*, DamageInfo* hit, CEngineObject* attacker, CEngineObject* source);
void __fastcall UnitObjectDeleted(CUnit* unit, void*, CEngineObject* object);
float __fastcall UnitForwardSpeed(CUnit* unit);
const mfc::CString* __fastcall UnitSetScriptName(CUnit* unit, void*, const char* name);

namespace {
void* s_unused;

// The six-byte getters: MOV EAX,imm32 (5).
int(__fastcall* s_hooverCreate)(CHoover*);
int __fastcall HooverCreateVerify(CHoover* self) {
    int expected = s_hooverCreate(self);
    int actual = HooverCreate(self);
    ei::verify::Check("CHoover::Create", expected, actual, "");
    return actual;
}
const char*(__fastcall* s_hooverName)(const CHoover*);
const char* __fastcall HooverNameVerify(const CHoover* self) {
    const char* expected = s_hooverName(self);
    const char* actual = HooverGetClassName(self);
    ei::verify::Check("CHoover::GetClassName", expected, actual, "%s", actual);
    return actual;
}
int(__fastcall* s_hooverId)(const CHoover*);
int __fastcall HooverIdVerify(const CHoover* self) {
    int expected = s_hooverId(self);
    int actual = HooverGetClassId(self);
    ei::verify::Check("CHoover::GetClassId", expected, actual, "");
    return actual;
}
const char*(__fastcall* s_lightningName)(const CLightning*);
const char* __fastcall LightningNameVerify(const CLightning* self) {
    const char* expected = s_lightningName(self);
    const char* actual = LightningGetClassName(self);
    ei::verify::Check("CLightning::GetClassName", expected, actual, "%s", actual);
    return actual;
}
int(__fastcall* s_lightningId)(const CLightning*);
int __fastcall LightningIdVerify(const CLightning* self) {
    int expected = s_lightningId(self);
    int actual = LightningGetClassId(self);
    ei::verify::Check("CLightning::GetClassId", expected, actual, "");
    return actual;
}
const char*(__fastcall* s_fakeUnitName)(const CUnit*);
const char* __fastcall FakeUnitNameVerify(const CUnit* self) {
    const char* expected = s_fakeUnitName(self);
    const char* actual = FakeUnitGetClassName(self);
    ei::verify::Check("CFakeUnit::GetClassName", expected, actual, "%s", actual);
    return actual;
}

// 004468B0: SUB ESP,8 (3), LEA EAX,[ESP+4] (4).
float(__fastcall* s_forwardSpeed)(CUnit*);
float __fastcall ForwardSpeedVerify(CUnit* self) {
    float expected = s_forwardSpeed(self);
    float actual = UnitForwardSpeed(self);
    ei::verify::CheckNear("CUnit slot 59 (forward speed)", expected, actual, "");
    return actual;
}

CHoover* __fastcall HooverDeletingVerify(CHoover* self, void*, unsigned flags) {
    CHoover* result = HooverDeletingDestruct(self, nullptr, flags);
    ei::verify::Check("CHoover deleting destructor", true, result == self, "flags %u", flags);
    return result;
}

CLightning* __fastcall LightningDeletingVerify(CLightning* self, void*, unsigned flags) {
    CLightning* result = LightningDeletingDestruct(self, nullptr, flags);
    ei::verify::Check("CLightning deleting destructor", true, result == self, "flags %u", flags);
    return result;
}

// The block is freed and the pointer left, as the original leaves it.
void __fastcall SegmentDestructVerify(LightningSegment* self) {
    LightningPoint* before = self->points.data;
    self->Destruct();
    ei::verify::Check("LightningSegment::Destruct", true, self->points.data == before, "");
}

int __fastcall ManagerCreateVerify(CLightningManager* self) {
    int result = self->CLightningManager::Create();
    int loaded = *reinterpret_cast<const int*>(0x007A9694);
    ei::verify::Check("CLightningManager::Create", true, loaded == 1, "result %d", result);
    return result;
}

int __fastcall FakeUnitCreateVerify(scene::FakeUnitScene* self) {
    int result = FakeUnitCreate(self);
    bool good = self->skin.unknown_00 == static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(self));
    ei::verify::Check("CFakeUnit::Create", true, good, "result %d", result);
    return result;
}

int __fastcall TakeHitVerify(CUnit* self, void*, DamageInfo* hit, CEngineObject* attacker, CEngineObject* source) {
    CEngineObject* controller = self->controller;
    int result = UnitTakeHit(self, nullptr, hit, attacker, source);
    ei::verify::Check("CUnit slot 31 (TakeHit)", true, controller != nullptr, "result %d", result);
    return result;
}

void __fastcall ObjectDeletedVerify(CUnit* self, void*, CEngineObject* object) {
    CEngineObject* controller = self->controller;
    UnitObjectDeleted(self, nullptr, object);
    ei::verify::Check("CUnit slot 56 (ObjectDeleted)", true, self->controller == controller, "");
}

const mfc::CString* __fastcall SetScriptNameVerify(CUnit* self, void*, const char* name) {
    const mfc::CString* result = UnitSetScriptName(self, nullptr, name);
    bool good = result == reinterpret_cast<const mfc::CString*>(&self->scriptName) &&
                std::strcmp(self->scriptName, name ? name : "") == 0;
    ei::verify::Check("CUnit slot 61 (SetScriptName)", true, good, "\"%.40s\"", name ? name : "(null)");
    return result;
}
} // namespace

EI_VERIFY(0x00461C90, HooverCreateVerify, 5, s_hooverCreate);
EI_VERIFY(0x00461C40, HooverNameVerify, 5, s_hooverName);
EI_VERIFY(0x00461C50, HooverIdVerify, 5, s_hooverId);
EI_VERIFY(0x004687D0, LightningNameVerify, 5, s_lightningName);
EI_VERIFY(0x004687E0, LightningIdVerify, 5, s_lightningId);
EI_VERIFY(0x004468D0, FakeUnitNameVerify, 5, s_fakeUnitName);
EI_VERIFY(0x004468B0, ForwardSpeedVerify, 7, s_forwardSpeed);
EI_VERIFY(0x00461C60, HooverDeletingVerify, 0, s_unused);
EI_VERIFY(0x004687F0, LightningDeletingVerify, 0, s_unused);
EI_VERIFY(0x00468920, SegmentDestructVerify, 0, s_unused);
EI_VERIFY(0x004689D0, ManagerCreateVerify, 0, s_unused);
EI_VERIFY(0x00446AF0, FakeUnitCreateVerify, 0, s_unused);
EI_VERIFY(0x004467F0, TakeHitVerify, 0, s_unused);
EI_VERIFY(0x00446850, ObjectDeletedVerify, 0, s_unused);
EI_VERIFY(0x00446830, SetScriptNameVerify, 0, s_unused);
