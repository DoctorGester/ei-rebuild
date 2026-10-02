// Differential checks for the unit methods (unit.cpp).
//
// Pure functions run both versions and compare bit for bit. Functions that change the unit run
// ours on the real unit, snapshot the whole CUnit (0x4DC bytes; and the item's reference count
// where one is taken), restore it, run the original and compare. Functions that release items
// run ours only and check the result.
#include "verify.h"

#include "../../src/game/spell.h"
#include "../../src/game/unit.h"

#include <cstring>

namespace {
std::uint32_t Bits(float f) {
    std::uint32_t u;
    std::memcpy(&u, &f, sizeof(u));
    return u;
}

// Runs `ours`, snapshots the first `size` bytes of the object (and the item's reference count),
// restores both, runs `original`; reports whether both left the same bytes and returned the same
// value. The buffers are on the stack: the original may call another verified function
// (MarkChanged) that does the same.
template <typename R, typename Ours, typename Original>
R SameWrites(const char* name, CUnit* self, int size, CEngineObject* item, Ours ours, Original original) {
    unsigned char saved[sizeof(CUnit)];
    unsigned char after[sizeof(CUnit)];
    std::memcpy(saved, self, size);
    int savedRefs = item ? item->refCount_ : 0;
    R actual = ours();
    std::memcpy(after, self, size);
    int afterRefs = item ? item->refCount_ : 0;
    std::memcpy(self, saved, size);
    if (item) item->refCount_ = savedRefs;
    R expected = original();
    int firstDiff = -1;
    for (int i = 0; i < size; ++i) {
        if (after[i] != reinterpret_cast<unsigned char*>(self)[i]) {
            firstDiff = i;
            break;
        }
    }
    bool same = expected == actual && firstDiff < 0 && (!item || afterRefs == item->refCount_);
    ei::verify::Check(name, true, same, "unit %d: result %X vs %X, first difference at +0x%X, refs %d vs %d",
                      self->id_, static_cast<unsigned>(expected), static_cast<unsigned>(actual), firstDiff,
                      item ? item->refCount_ : 0, afterRefs);
    return expected;
}

// The unit setters are only called on units (CUnit and CFakeUnit, at least 0x4DC bytes).
template <typename R, typename Ours, typename Original>
R SameUnitWrites(const char* name, CUnit* self, CEngineObject* item, Ours ours, Original original) {
    return SameWrites<R>(name, self, static_cast<int>(sizeof(CUnit)), item, ours, original);
}

// MarkChanged belongs to the base of all map objects, some smaller than a unit; it writes nothing
// beyond +0x1DE.
constexpr int kMarkChangedExtent = 0x1E0;
} // namespace

// --- Change tracking -------------------------------------------------------------------------

// 0050E9F0: PUSH ESI; MOV ESI,ECX; MOV ECX,[ESP+8] (7 bytes).
static std::uintptr_t(__fastcall* s_markChanged)(CUnit*, void*, int);
static std::uintptr_t __fastcall MarkChangedVerify(CUnit* self, void*, int part) {
    return SameWrites<std::uintptr_t>("CUnit::MarkChanged", self, kMarkChangedExtent, nullptr,
                                      [&] { return self->MarkChanged(part); },
                                      [&] { return s_markChanged(self, nullptr, part); });
}
EI_VERIFY(0x0050E9F0, MarkChangedVerify, 7, s_markChanged);

// 0050F630: SUB ESP,0x14; XOR EDX,EDX (5 bytes).
static BOOL(__fastcall* s_hasEffect)(CUnit*, void*, std::uint32_t);
static BOOL __fastcall HasEffectVerify(CUnit* self, void*, std::uint32_t type) {
    BOOL expected = s_hasEffect(self, nullptr, type);
    ei::verify::Check("CUnit::HasEffect", expected, self->HasEffect(type), "type %X count %d", type, self->effectCount);
    return expected;
}
EI_VERIFY(0x0050F630, HasEffectVerify, 5, s_hasEffect);

// --- Sides and setters (all stateful) ---------------------------------------------------------

// 0052E940: MOV EDX,[ECX+0x244] (6 bytes).
static std::uintptr_t(__fastcall* s_addHostile)(CUnit*, void*, std::uint32_t);
static std::uintptr_t __fastcall AddHostileVerify(CUnit* self, void*, std::uint32_t mask) {
    return SameUnitWrites<std::uintptr_t>("CUnit::AddHostileMask", self, nullptr,
                                          [&] { return self->AddHostileMask(mask); },
                                          [&] { return s_addHostile(self, nullptr, mask); });
}
EI_VERIFY(0x0052E940, AddHostileVerify, 6, s_addHostile);

// 00530180: MOV EAX,[ESP+4]; PUSH 0xB (6 bytes).
static std::uintptr_t(__fastcall* s_setRelationsB)(CUnit*, void*, std::uint32_t);
static std::uintptr_t __fastcall SetRelationsBVerify(CUnit* self, void*, std::uint32_t mask) {
    return SameUnitWrites<std::uintptr_t>("CUnit::SetRelationsB", self, nullptr, [&] { return self->SetRelationsB(mask); },
                                          [&] { return s_setRelationsB(self, nullptr, mask); });
}
EI_VERIFY(0x00530180, SetRelationsBVerify, 6, s_setRelationsB);

// 00530120: MOV EAX,[ESP+4]; MOV EDX,[ECX+8] (7 bytes).
static std::uintptr_t(__fastcall* s_setSide)(CUnit*, void*, int);
static std::uintptr_t __fastcall SetSideVerify(CUnit* self, void*, int side) {
    return SameUnitWrites<std::uintptr_t>("CUnit::SetSide", self, nullptr, [&] { return self->SetSide(side); },
                                          [&] { return s_setSide(self, nullptr, side); });
}
EI_VERIFY(0x00530120, SetSideVerify, 7, s_setSide);

// 00530150: MOV EDX,ECX; MOV EAX,1 (7 bytes).
static std::uint32_t(__fastcall* s_isHostile)(CUnit*, void*, const CUnit*);
static std::uint32_t __fastcall IsHostileVerify(CUnit* self, void*, const CUnit* other) {
    std::uint32_t expected = s_isHostile(self, nullptr, other);
    ei::verify::Check("CUnit::IsHostileTo", expected, self->IsHostileTo(other), "masks %X/%X side %d", self->relationsA,
                      self->relationsB, other->side);
    return expected;
}
EI_VERIFY(0x00530150, IsHostileVerify, 7, s_isHostile);

// 0052E9B0: MOV EAX,[ESP+4]; PUSH 0xD (6 bytes).
static std::uintptr_t(__fastcall* s_set438)(CUnit*, void*, int);
static std::uintptr_t __fastcall Set438Verify(CUnit* self, void*, int value) {
    return SameUnitWrites<std::uintptr_t>("CUnit::SetUnknown438", self, nullptr, [&] { return self->SetUnknown438(value); },
                                          [&] { return s_set438(self, nullptr, value); });
}
EI_VERIFY(0x0052E9B0, Set438Verify, 6, s_set438);

// 0052EA90: MOV AL,[ESP+4]; MOV DL,[ESP+8] (8 bytes).
static std::uintptr_t(__fastcall* s_set254)(CUnit*, void*, std::int8_t, std::int8_t, int, std::int8_t);
static std::uintptr_t __fastcall Set254Verify(CUnit* self, void*, std::int8_t a, std::int8_t b, int c, std::int8_t d) {
    return SameUnitWrites<std::uintptr_t>("CUnit::SetUnknown254", self, nullptr,
                                          [&] { return self->SetUnknown254(a, b, c, d); },
                                          [&] { return s_set254(self, nullptr, a, b, c, d); });
}
EI_VERIFY(0x0052EA90, Set254Verify, 8, s_set254);

// 0052EAD0 / 0052EAF0: MOV EAX,[ESP+4]; MOV EDX,[ECX+0x25C / 0x260] (10 bytes).
static std::uintptr_t(__fastcall* s_set25C)(CUnit*, void*, int);
static std::uintptr_t __fastcall Set25CVerify(CUnit* self, void*, int value) {
    return SameUnitWrites<std::uintptr_t>("CUnit::SetUnknown25C", self, nullptr, [&] { return self->SetUnknown25C(value); },
                                          [&] { return s_set25C(self, nullptr, value); });
}
EI_VERIFY(0x0052EAD0, Set25CVerify, 10, s_set25C);

static std::uintptr_t(__fastcall* s_set260)(CUnit*, void*, int);
static std::uintptr_t __fastcall Set260Verify(CUnit* self, void*, int value) {
    return SameUnitWrites<std::uintptr_t>("CUnit::SetUnknown260", self, nullptr, [&] { return self->SetUnknown260(value); },
                                          [&] { return s_set260(self, nullptr, value); });
}
EI_VERIFY(0x0052EAF0, Set260Verify, 10, s_set260);

// 0052EB10: MOV EAX,[ESP+4]; MOV EDX,[ESP+8] (8 bytes).
static std::uintptr_t(__fastcall* s_setMoveModes)(CUnit*, void*, int, int);
static std::uintptr_t __fastcall SetMoveModesVerify(CUnit* self, void*, int mode, int mode2) {
    return SameUnitWrites<std::uintptr_t>("CUnit::SetMoveModes", self, nullptr,
                                          [&] { return self->SetMoveModes(mode, mode2); },
                                          [&] { return s_setMoveModes(self, nullptr, mode, mode2); });
}
EI_VERIFY(0x0052EB10, SetMoveModesVerify, 8, s_setMoveModes);

// 0052EBE0: MOV AL,[ESP+4]; TEST AL,AL (6 bytes).
static std::uintptr_t(__fastcall* s_setFlag40)(CUnit*, void*, bool);
static std::uintptr_t __fastcall SetFlag40Verify(CUnit* self, void*, bool on) {
    return SameUnitWrites<std::uintptr_t>("CUnit::SetFlag40000000Off", self, nullptr,
                                          [&] { return self->SetFlag40000000Off(on); },
                                          [&] { return s_setFlag40(self, nullptr, on); });
}
EI_VERIFY(0x0052EBE0, SetFlag40Verify, 6, s_setFlag40);

// 0052F7F0: MOV EAX,[ESP+4]; TEST EAX,EAX (6 bytes).
static std::uint32_t(__fastcall* s_setFlag1)(CUnit*, void*, int);
static std::uint32_t __fastcall SetFlag1Verify(CUnit* self, void*, int on) {
    return SameUnitWrites<std::uint32_t>("CUnit::SetFlag1000000", self, nullptr, [&] { return self->SetFlag1000000(on); },
                                         [&] { return s_setFlag1(self, nullptr, on); });
}
EI_VERIFY(0x0052F7F0, SetFlag1Verify, 6, s_setFlag1);

// --- Equipment -------------------------------------------------------------------------------

// 0052EC10 only writes `out` (11 ints): the original into a scratch array.
// PUSH EBX; MOV EBX,[ESP+8] (5 bytes).
static void(__fastcall* s_equipmentIds)(CUnit*, void*, int*);
static void __fastcall EquipmentIdsVerify(CUnit* self, void*, int* out) {
    int expected[11];
    s_equipmentIds(self, nullptr, expected);
    self->GetEquipmentIds(out);
    ei::verify::Check("CUnit::GetEquipmentIds", true, std::memcmp(expected, out, sizeof(expected)) == 0, "unit %d",
                      self->id_);
}
EI_VERIFY(0x0052EC10, EquipmentIdsVerify, 5, s_equipmentIds);

// 00531070 / 00531180 / 00531820: PUSH ESI; PUSH EDI; MOV EDI,[ESP+0xC] (6 bytes).
static int(__fastcall* s_addWeapon)(CUnit*, void*, CEngineObject*);
static int __fastcall AddWeaponVerify(CUnit* self, void*, CEngineObject* item) {
    return SameUnitWrites<int>("CUnit::AddWeapon", self, item, [&] { return self->AddWeapon(item); },
                               [&] { return s_addWeapon(self, nullptr, item); });
}
EI_VERIFY(0x00531070, AddWeaponVerify, 6, s_addWeapon);

static int(__fastcall* s_addQuickItem)(CUnit*, void*, CEngineObject*);
static int __fastcall AddQuickItemVerify(CUnit* self, void*, CEngineObject* item) {
    return SameUnitWrites<int>("CUnit::AddQuickItem", self, item, [&] { return self->AddQuickItem(item); },
                               [&] { return s_addQuickItem(self, nullptr, item); });
}
EI_VERIFY(0x00531180, AddQuickItemVerify, 6, s_addQuickItem);

static int(__fastcall* s_addSpell)(CUnit*, void*, CEngineObject*);
static int __fastcall AddSpellVerify(CUnit* self, void*, CEngineObject* item) {
    return SameUnitWrites<int>("CUnit::AddSpell", self, item, [&] { return self->AddSpell(item); },
                               [&] { return s_addSpell(self, nullptr, item); });
}
EI_VERIFY(0x00531820, AddSpellVerify, 6, s_addSpell);

// 005310D0 / 005311E0 release the item (it may be freed): ours only; the slot must be empty
// afterwards and the unit marked dirty, or the result is the slot when it is out of range.
static void* s_unusedRemove;
static std::uintptr_t __fastcall RemoveWeaponVerify(CUnit* self, void*, std::uint32_t slot) {
    std::uintptr_t result = self->RemoveWeapon(slot);
    bool ok = slot >= 4 ? result == slot : (self->weapons[slot] == nullptr && (self->flags_ & 2) != 0);
    ei::verify::Check("CUnit::RemoveWeapon", true, ok, "slot %u", slot);
    return result;
}
EI_VERIFY(0x005310D0, RemoveWeaponVerify, 0, s_unusedRemove);

static std::uintptr_t __fastcall RemoveQuickItemVerify(CUnit* self, void*, std::uint32_t slot) {
    std::uintptr_t result = self->RemoveQuickItem(slot);
    bool ok = slot >= 4 ? result == slot : (self->quickItems[slot] == nullptr && (self->flags_ & 2) != 0);
    ei::verify::Check("CUnit::RemoveQuickItem", true, ok, "slot %u", slot);
    return result;
}
EI_VERIFY(0x005311E0, RemoveQuickItemVerify, 0, s_unusedRemove);

// 005318B0 / 00531920 / 005319A0 / 00531A10: PUSH ESI; MOV ESI,ECX; MOV ECX,[ESI+0x240] (9 bytes).
static CEngineObject*(__fastcall* s_getWeapon)(CUnit*, void*, std::uint32_t);
static CEngineObject* __fastcall GetWeaponVerify(CUnit* self, void*, std::uint32_t slot) {
    CEngineObject* expected = s_getWeapon(self, nullptr, slot);
    ei::verify::Check("CUnit::GetWeapon", expected, self->GetWeapon(slot), "unit %d slot %u", self->id_, slot);
    return expected;
}
EI_VERIFY(0x005318B0, GetWeaponVerify, 9, s_getWeapon);

static CEngineObject*(__fastcall* s_getQuickItem)(CUnit*, void*, std::uint32_t);
static CEngineObject* __fastcall GetQuickItemVerify(CUnit* self, void*, std::uint32_t slot) {
    CEngineObject* expected = s_getQuickItem(self, nullptr, slot);
    ei::verify::Check("CUnit::GetQuickItem", expected, self->GetQuickItem(slot), "unit %d slot %u", self->id_, slot);
    return expected;
}
EI_VERIFY(0x00531920, GetQuickItemVerify, 9, s_getQuickItem);

static CEngineObject*(__fastcall* s_getSpell)(CUnit*, void*, std::uint32_t);
static CEngineObject* __fastcall GetSpellVerify(CUnit* self, void*, std::uint32_t slot) {
    CEngineObject* expected = s_getSpell(self, nullptr, slot);
    ei::verify::Check("CUnit::GetSpell", expected, self->GetSpell(slot), "unit %d slot %u", self->id_, slot);
    return expected;
}
EI_VERIFY(0x005319A0, GetSpellVerify, 9, s_getSpell);

static void*(__fastcall* s_getSpellObject)(CUnit*, void*, std::uint32_t);
static void* __fastcall GetSpellObjectVerify(CUnit* self, void*, std::uint32_t slot) {
    void* expected = s_getSpellObject(self, nullptr, slot);
    ei::verify::Check("CUnit::GetSpellObject", expected, self->GetSpellObject(slot), "unit %d slot %u", self->id_, slot);
    return expected;
}
EI_VERIFY(0x00531A10, GetSpellObjectVerify, 9, s_getSpellObject);

// 00530FA0: PUSH ESI; MOV ESI,ECX; PUSH EDI; MOV ECX,[ESI+0x240] (10 bytes). AL only.
static bool(__fastcall* s_hasRanged)(CUnit*);
static bool __fastcall HasRangedVerify(CUnit* self) {
    bool expected = s_hasRanged(self);
    ei::verify::Check("CUnit::HasRangedWeapon", expected, self->HasRangedWeapon(), "unit %d slot %u", self->id_,
                      self->activeWeapon);
    return expected;
}
EI_VERIFY(0x00530FA0, HasRangedVerify, 10, s_hasRanged);

// 005307B0: FLD [0x0073B858] (6 bytes).
static float(__fastcall* s_totalWeight)(CUnit*);
static float __fastcall TotalWeightVerify(CUnit* self) {
    float expected = s_totalWeight(self);
    float actual = self->TotalItemWeight();
    ei::verify::Check("CUnit::TotalItemWeight", Bits(expected), Bits(actual), "%g vs %g", expected, actual);
    return expected;
}
EI_VERIFY(0x005307B0, TotalWeightVerify, 6, s_totalWeight);

// 0052F820: PUSH ECX; PUSH ESI; PUSH EDI; MOV EDI,[ESP+0x10] (7 bytes).
static int(__fastcall* s_canCast)(CUnit*, void*, std::uint32_t, int);
static int __fastcall CanCastVerify(CUnit* self, void*, std::uint32_t slot, int unused) {
    int expected = s_canCast(self, nullptr, slot, unused);
    ei::verify::Check("CUnit::CanCastSpell", expected, self->CanCastSpell(slot, unused), "unit %d slot %u mana %u",
                      self->id_, slot, self->mana);
    return expected;
}
EI_VERIFY(0x0052F820, CanCastVerify, 7, s_canCast);

// 0052F8B0: MOV EAX,[ESP+4]; PUSH ESI (5 bytes).
static int(__fastcall* s_canUseQuick)(CUnit*, void*, std::uint32_t, int);
static int __fastcall CanUseQuickVerify(CUnit* self, void*, std::uint32_t slot, int unused) {
    int expected = s_canUseQuick(self, nullptr, slot, unused);
    ei::verify::Check("CUnit::CanUseQuickItem", expected, self->CanUseQuickItem(slot, unused), "unit %d slot %u",
                      self->id_, slot);
    return expected;
}
EI_VERIFY(0x0052F8B0, CanUseQuickVerify, 5, s_canUseQuick);

// --- Movement --------------------------------------------------------------------------------

// 0052F920 only writes its two outputs (which may be the same float): the original into scratch.
// PUSH ECX; XOR EAX,EAX; PUSH ESI; MOV AL,[ECX+0x344] (10 bytes).
static void(__fastcall* s_moveSpeeds)(CUnit*, void*, float*, float*);
static void __fastcall MoveSpeedsVerify(CUnit* self, void*, float* forward, float* backward) {
    float scratch[2] = {*forward, *backward};
    float* expectedBackward = backward == forward ? &scratch[0] : &scratch[1];
    s_moveSpeeds(self, nullptr, &scratch[0], expectedBackward);
    self->GetMoveSpeeds(forward, backward);
    bool same = Bits(scratch[0]) == Bits(*forward) && Bits(*expectedBackward) == Bits(*backward);
    ei::verify::Check("CUnit::GetMoveSpeeds", true, same, "mode %d: %g/%g vs %g/%g", self->moveMode, scratch[0],
                      *expectedBackward, *forward, *backward);
}
EI_VERIFY(0x0052F920, MoveSpeedsVerify, 10, s_moveSpeeds);

// 0052FA20 / 0052FAF0: PUSH ECX; XOR EAX,EAX; MOV AL,[ECX+0x344] (9 bytes).
static float(__fastcall* s_moveSpeed2)(CUnit*);
static float __fastcall MoveSpeed2Verify(CUnit* self) {
    float expected = s_moveSpeed2(self);
    float actual = self->MoveSpeed2();
    ei::verify::Check("CUnit::MoveSpeed2", Bits(expected), Bits(actual), "mode %d: %g vs %g", self->moveMode2, expected,
                      actual);
    return expected;
}
EI_VERIFY(0x0052FA20, MoveSpeed2Verify, 9, s_moveSpeed2);

static float(__fastcall* s_moveSpeedFor)(CUnit*, void*, int);
static float __fastcall MoveSpeedForVerify(CUnit* self, void*, int mode) {
    float expected = s_moveSpeedFor(self, nullptr, mode);
    float actual = self->MoveSpeedFor(mode);
    ei::verify::Check("CUnit::MoveSpeedFor", Bits(expected), Bits(actual), "mode %d: %g vs %g", mode, expected, actual);
    return expected;
}
EI_VERIFY(0x0052FAF0, MoveSpeedForVerify, 9, s_moveSpeedFor);

// 0052FBC0: PUSH EBP; MOV EBP,ESP; SUB ESP,8 (6 bytes).
static float(__fastcall* s_scaledValue)(CUnit*, void*, float);
static float __fastcall ScaledValueVerify(CUnit* self, void*, float value) {
    float expected = s_scaledValue(self, nullptr, value);
    float actual = self->ScaledValue(value);
    ei::verify::Check("CUnit::ScaledValue", Bits(expected), Bits(actual), "value %g: %g vs %g", value, expected, actual);
    return expected;
}
EI_VERIFY(0x0052FBC0, ScaledValueVerify, 6, s_scaledValue);

// 00530370: TEST byte [ECX+0x124],2 (7 bytes).
static float(__fastcall* s_baseSpeed)(CUnit*);
static float __fastcall BaseSpeedVerify(CUnit* self) {
    float expected = s_baseSpeed(self);
    float actual = self->BaseSpeed();
    ei::verify::Check("CUnit::BaseSpeed", Bits(expected), Bits(actual), "%g vs %g", expected, actual);
    return expected;
}
EI_VERIFY(0x00530370, BaseSpeedVerify, 7, s_baseSpeed);

// 005302F0: PUSH ECX; PUSH ESI; MOV ESI,ECX; TEST byte [ESI+0x124],2 (11 bytes).
static float(__fastcall* s_modeSpeed)(CUnit*);
static float __fastcall ModeSpeedVerify(CUnit* self) {
    float expected = s_modeSpeed(self);
    float actual = self->ModeSpeed();
    ei::verify::Check("CUnit::ModeSpeed", Bits(expected), Bits(actual), "mode %d: %g vs %g", self->moveMode, expected,
                      actual);
    return expected;
}
EI_VERIFY(0x005302F0, ModeSpeedVerify, 11, s_modeSpeed);

// --- Prototypes ------------------------------------------------------------------------------

// 00530930 / 00530970: PUSH ESI; MOV ESI,[ECX+0x18]; MOV ECX,[0x007AF35C] (10 bytes).
static char*(__fastcall* s_prototype)(CUnit*);
static char* __fastcall PrototypeVerify(CUnit* self) {
    char* expected = s_prototype(self);
    ei::verify::Check("CUnit::GetPrototype", expected, self->GetPrototype(), "index %u", self->unknown_18);
    return expected;
}
EI_VERIFY(0x00530930, PrototypeVerify, 10, s_prototype);

static char*(__fastcall* s_prototypeModel)(CUnit*);
static char* __fastcall PrototypeModelVerify(CUnit* self) {
    char* expected = s_prototypeModel(self);
    ei::verify::Check("CUnit::GetPrototypeModel", expected, self->GetPrototypeModel(), "index %u", self->unknown_18);
    return expected;
}
EI_VERIFY(0x00530970, PrototypeModelVerify, 10, s_prototypeModel);

// 005309E0: PUSH ESI; MOV ESI,[ECX+0x23C] (7 bytes).
static char*(__fastcall* s_prototype23C)(CUnit*);
static char* __fastcall Prototype23CVerify(CUnit* self) {
    char* expected = s_prototype23C(self);
    ei::verify::Check("CUnit::GetPrototype23C", expected, self->GetPrototype23C(), "index %d", self->prototype23C);
    return expected;
}
EI_VERIFY(0x005309E0, Prototype23CVerify, 7, s_prototype23C);

// 005308A0 writes prototype23C: MOV EDX,[ESP+4]; MOV EAX,[0x007AF358] (9 bytes).
static std::uint32_t(__fastcall* s_setPrototype23C)(CUnit*, void*, const char*);
static std::uint32_t __fastcall SetPrototype23CVerify(CUnit* self, void*, const char* record) {
    int before = self->prototype23C;
    std::uint32_t actual = self->SetPrototype23C(record);
    int actualIndex = self->prototype23C;
    self->prototype23C = before;
    std::uint32_t expected = s_setPrototype23C(self, nullptr, record);
    ei::verify::Check("CUnit::SetPrototype23C", true, expected == actual && actualIndex == self->prototype23C,
                      "record %p: %d/%u vs %d/%u", record, self->prototype23C, expected, actualIndex, actual);
    return expected;
}
EI_VERIFY(0x005308A0, SetPrototype23CVerify, 9, s_setPrototype23C);

// --- Combat ----------------------------------------------------------------------------------

// 005327D0: MOV EAX,[ESP+4]; MOV EAX,[EAX+0x14] (7 bytes).
static float(__fastcall* s_resistance)(CUnit*, void*, const void*);
static float __fastcall ResistanceVerify(CUnit* self, void*, const void* hit) {
    float expected = s_resistance(self, nullptr, hit);
    float actual = self->ResistanceTo(hit);
    ei::verify::Check("CUnit::ResistanceTo", Bits(expected), Bits(actual), "flags %08X: %g vs %g",
                      *reinterpret_cast<const std::uint32_t*>(static_cast<const char*>(hit) + 0x14), expected, actual);
    return expected;
}
EI_VERIFY(0x005327D0, ResistanceVerify, 7, s_resistance);

// 00532DA0: PUSH ESI; MOV ESI,ECX; CMP dword [ESI+0x10],0x32 (7 bytes). AL only.
static bool(__fastcall* s_acceptsTarget)(CUnit*, void*, const void*);
static bool __fastcall AcceptsTargetVerify(CUnit* self, void*, const void* target) {
    bool expected = s_acceptsTarget(self, nullptr, target);
    ei::verify::Check("CUnit::AcceptsTarget", expected, self->AcceptsTarget(target), "type %X", self->type_);
    return expected;
}
EI_VERIFY(0x00532DA0, AcceptsTargetVerify, 7, s_acceptsTarget);

// 00532E00: PUSH ECX; PUSH ESI; MOV ESI,ECX; MOV EAX,[ESI+0x1B4] (10 bytes).
static float(__fastcall* s_animationRatio)(CUnit*);
static float __fastcall AnimationRatioVerify(CUnit* self) {
    float expected = s_animationRatio(self);
    float actual = self->AnimationRatio();
    ei::verify::Check("CUnit::AnimationRatio", Bits(expected), Bits(actual), "%g vs %g", expected, actual);
    return expected;
}
EI_VERIFY(0x00532E00, AnimationRatioVerify, 10, s_animationRatio);

// 005271C0: PUSH ECX; XOR EAX,EAX; MOV AL,[ECX+0xE] (6 bytes).
static float(__fastcall* s_period)(UnitParams*);
static float __fastcall PeriodVerify(UnitParams* self) {
    float expected = s_period(self);
    float actual = self->Period();
    ei::verify::Check("UnitParams::Period", Bits(expected), Bits(actual), "rate %u", self->attackRate);
    return expected;
}
EI_VERIFY(0x005271C0, PeriodVerify, 6, s_period);

// --- Map object state ------------------------------------------------------------------------

// 005156A0: PUSH EBP; MOV EBP,ESP; SUB ESP,8 (6 bytes).
static BOOL(__fastcall* s_serverIsDead)(CMapObjectServer*);
static BOOL __fastcall ServerIsDeadVerify(CMapObjectServer* self) {
    BOOL expected = s_serverIsDead(self);
    ei::verify::Check("CMapObjectServer::IsDead", expected, self->IsDead(), "hp %g", self->hitPoints);
    return expected;
}
EI_VERIFY(0x005156A0, ServerIsDeadVerify, 6, s_serverIsDead);

// 005156C0: FLD [ECX+0x2C]; FMUL [0x0073DDC0] (9 bytes).
static BOOL(__fastcall* s_serverOverkilled)(CMapObjectServer*);
static BOOL __fastcall ServerOverkilledVerify(CMapObjectServer* self) {
    BOOL expected = s_serverOverkilled(self);
    ei::verify::Check("CMapObjectServer::IsOverkilled", expected, self->IsOverkilled(), "hp %g max %g", self->hitPoints,
                      self->maxHitPoints);
    return expected;
}
EI_VERIFY(0x005156C0, ServerOverkilledVerify, 9, s_serverOverkilled);

// 005104D0: MOV EAX,[ECX+0x1C]; XOR ECX,ECX (5 bytes).
static BOOL(__fastcall* s_clientIsDead)(CMapObjectClientSpecific*);
static BOOL __fastcall ClientIsDeadVerify(CMapObjectClientSpecific* self) {
    BOOL expected = s_clientIsDead(self);
    ei::verify::Check("CMapObjectClientSpecific::IsDead", expected, self->IsDead(), "hp %d", self->object->hitPoints);
    return expected;
}
EI_VERIFY(0x005104D0, ClientIsDeadVerify, 5, s_clientIsDead);

// 005104F0: MOV EAX,[ECX+0x1C]; MOVSX ECX,word [EAX+0x180] (10 bytes).
static BOOL(__fastcall* s_clientOverkilled)(CMapObjectClientSpecific*);
static BOOL __fastcall ClientOverkilledVerify(CMapObjectClientSpecific* self) {
    BOOL expected = s_clientOverkilled(self);
    ei::verify::Check("CMapObjectClientSpecific::IsOverkilled", expected, self->IsOverkilled(), "hp %d max %d",
                      self->object->hitPoints, self->object->maxHitPoints);
    return expected;
}
EI_VERIFY(0x005104F0, ClientOverkilledVerify, 10, s_clientOverkilled);
