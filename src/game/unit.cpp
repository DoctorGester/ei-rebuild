#include "unit.h"

#include "item.h"
#include "spell.h"
#include "unit_db.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "originals_engine.h"
#include "game_data.h"
#include "lib/nan_audit.h"
#include "logic_settings.h"

#include <bit>
#include <cstring>

namespace {
constexpr float kZero = 0.0f;                                   // 0073B858
constexpr float kOne = 1.0f;                                    // 0073B84C
constexpr float kSpeedByteScale = std::bit_cast<float>(0x3D20A0A1u);  // 0073EBC0 (1 / 25.5)
constexpr float kTwo = 2.0f;                                    // 0073E9A4
constexpr float kByteScale = std::bit_cast<float>(0x3B808081u);       // 0073C36C (1 / 255)
constexpr float kThree = 3.0f;                                  // 0073BAA4
constexpr float kTen = 10.0f;                                   // 0073BAAC
constexpr float kNoPlayerValue = 1000000.0f;                    // 0073EBC4
constexpr float kFastSpeedScale = std::bit_cast<float>(0x3FE66666u);  // 0073EBD0 (1.8)
constexpr double kWalkSpeedScale = 0.7;                         // 0073EBC8 (a double)
constexpr float kSlowModeScale = std::bit_cast<float>(0x3F333333u);   // 0073C538 (0.7)
constexpr float kIdleScale = std::bit_cast<float>(0x3E4CCCCDu);       // 0073BAD4 (0.2)
constexpr float kOverkill = -5.0f;                              // 0073DDC0

constexpr int kPartRelations = 11;
constexpr int kPartEquipment = 5;
constexpr std::uint32_t kFlagSideChanged = 0x80000;
constexpr int kClassQuickItem = 0x13;

// Globals read by the formulas.
float BackwardSpeedScale() { return GameLogicSettings().rotateSpeedMult; }
float HeroTransitionSpeed() { return GameLogicSettings().humanCrossAnimationSpeed; }

float ItemWeight(const CItem* item) { return item->Weight(); }

// A spell's mana cost (CSpell +0x40, a float kept as a dword).
float SpellCost(const void* spell) { return std::bit_cast<float>(static_cast<const CSpell*>(spell)->manaCost_); }

PartyRecords* RecordsOf(char* player) { return reinterpret_cast<PartyRecords*>(player + 0x70); }

bool IsPartyUnit(CUnit* unit) { return unit->controller->GetClassId() == kClassPartyController; }

// Puts `item` into the first free slot of `slots`, taking a reference: the slot, 1 when it is
// already there (whatever its slot), -1 when full.
int AddToSlots(CUnit* unit, CItem** slots, std::uint32_t count, CItem* item) {
    for (std::uint32_t i = 0; i < count; ++i) {
        if (slots[i] == item) return 1;
        if (!slots[i]) {
            slots[i] = item;
            ++item->refCount_;
            unit->MarkChanged(kPartEquipment);
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::uintptr_t RemoveFromSlots(CUnit* unit, CItem** slots, std::uint32_t slot) {
    if (slot >= 4) return slot;
    CItem* item = slots[slot];
    slots[slot] = nullptr;
    if (item) item->Release();
    return unit->MarkChanged(kPartEquipment);
}
} // namespace

// --- Sides -----------------------------------------------------------------------------------

std::uintptr_t CUnit::AddHostileMask(std::uint32_t mask) {
    std::uint32_t merged = relationsA | mask;
    if (relationsA == merged) return merged;
    relationsA = merged;
    flags_ |= kFlagSideChanged;
    return MarkChanged(kPartRelations);
}
EI_REPLACE_METHOD(0x0052E940, CUnit::AddHostileMask);

std::uintptr_t CUnit::SetRelationsB(std::uint32_t mask) {
    relationsB = mask;
    return MarkChanged(kPartRelations);
}
EI_REPLACE_METHOD(0x00530180, CUnit::SetRelationsB);

std::uintptr_t CUnit::SetSide(int value) {
    side = value;
    flags_ |= kFlagSideChanged;
    if (!controller) return 0;
    return MarkChanged(kPartRelations);
}
EI_REPLACE_METHOD(0x00530120, CUnit::SetSide);

std::uint32_t CUnit::IsHostileTo(const CUnit* other) {
    return (1u << (other->side & 31)) & relationsB & relationsA;
}
EI_REPLACE_METHOD(0x00530150, CUnit::IsHostileTo);

// --- Simple state setters --------------------------------------------------------------------

std::uintptr_t CUnit::SetUnknown438(int value) {
    fightAtWill = value;
    return MarkChanged(13);
}
EI_REPLACE_METHOD(0x0052E9B0, CUnit::SetUnknown438);

std::uintptr_t CUnit::SetAnimationEntries(std::int8_t a, std::int8_t b, int c, std::int8_t d) {
    animationEntry = a;
    secondEntry = b;
    actionSound = d;
    animationStart = c;
    return MarkChanged(8);
}
EI_REPLACE_METHOD(0x0052EA90, CUnit::SetAnimationEntries);

std::uintptr_t CUnit::SetMotionMode(int value) {
    if (motionMode == value) return static_cast<std::uintptr_t>(value);
    motionMode = value;
    return MarkChanged(7);
}
EI_REPLACE_METHOD(0x0052EAD0, CUnit::SetMotionMode);

std::uintptr_t CUnit::SetPathTimeShift(int value) {
    if (pathTimeShift == value) return static_cast<std::uintptr_t>(value);
    pathTimeShift = value;
    return MarkChanged(7);
}
EI_REPLACE_METHOD(0x0052EAF0, CUnit::SetPathTimeShift);

std::uintptr_t CUnit::SetMoveModes(int mode, int mode2) {
    moveMode = mode;
    moveMode2 = mode2;
    return MarkChanged(6);
}
EI_REPLACE_METHOD(0x0052EB10, CUnit::SetMoveModes);

std::uintptr_t CUnit::SetFlag40000000Off(bool on) {
    if (on) {
        flags_ &= ~0x40000000u;
    } else {
        flags_ |= 0x40000000u;
    }
    return MarkChanged(5);
}
EI_REPLACE_METHOD(0x0052EBE0, CUnit::SetFlag40000000Off);

std::uint32_t CUnit::SetStunnedImpl(int on) {
    if (on) {
        flags_ |= 0x1000000u;
    } else {
        flags_ &= ~0x1000000u;
    }
    return flags_;
}
EI_REPLACE_METHOD(0x0052F7F0, CUnit::SetStunnedImpl);

// --- Equipment -------------------------------------------------------------------------------

void CUnit::GetEquipmentIds(std::uint32_t* out) {
    for (int i = 0; i < 4; ++i) {
        out[i] = weapons[i] ? weapons[i]->prototypeId_ : 0xFFFFFFFFu;
    }
    for (int i = 0; i < 7; ++i) {
        out[4 + i] = armor[i] ? armor[i]->prototypeId_ : 0xFFFFFFFFu;
    }
}
EI_REPLACE_METHOD(0x0052EC10, CUnit::GetEquipmentIds);

int CUnit::AddWeapon(CItem* item) {
    if (!item) return 0;
    return AddToSlots(this, weapons, 4, item);
}
EI_REPLACE_METHOD(0x00531070, CUnit::AddWeapon);

int CUnit::AddQuickItem(CItem* item) {
    if (!item) return 0;
    return AddToSlots(this, quickItems, 4, item);
}
EI_REPLACE_METHOD(0x00531180, CUnit::AddQuickItem);

int CUnit::AddSpell(CItem* item) {
    // No null check here: a null item "is already there" at the first empty slot (returns 1).
    return AddToSlots(this, spells, 8, item);
}
EI_REPLACE_METHOD(0x00531820, CUnit::AddSpell);

std::uintptr_t CUnit::RemoveWeapon(std::uint32_t slot) { return RemoveFromSlots(this, weapons, slot); }
EI_REPLACE_METHOD(0x005310D0, CUnit::RemoveWeapon);

std::uintptr_t CUnit::RemoveQuickItem(std::uint32_t slot) { return RemoveFromSlots(this, quickItems, slot); }
EI_REPLACE_METHOD(0x005311E0, CUnit::RemoveQuickItem);

CItem* CUnit::GetWeapon(std::uint32_t slot) {
    if (IsPartyUnit(this)) {
        if (!player) return nullptr;
        PartyUnitRecord* record = RecordsOf(player)->Find(id_);
        if (!record || slot >= record->weaponCount) return nullptr;
        return record->weapons[slot];
    }
    return slot < 4 ? weapons[slot] : nullptr;
}
EI_REPLACE_METHOD(0x005318B0, CUnit::GetWeapon);

CItem* CUnit::GetQuickItem(std::uint32_t slot) {
    if (IsPartyUnit(this)) {
        if (!player) return nullptr;
        PartyUnitRecord* record = RecordsOf(player)->Find(id_);
        if (!record || slot >= record->quickItemCount) return nullptr;
        CItem* item = record->quickItems[slot];
        if (!item || static_cast<int>(item->count_) < 1) return nullptr;   // its count
        return item;
    }
    return slot < 4 ? quickItems[slot] : nullptr;
}
EI_REPLACE_METHOD(0x00531920, CUnit::GetQuickItem);

CItem* CUnit::GetSpell(std::uint32_t slot) {
    if (IsPartyUnit(this)) {
        if (!player) return nullptr;
        PartyUnitRecord* record = RecordsOf(player)->Find(id_);
        if (!record || slot >= record->spellCount) return nullptr;
        return record->spells[slot];
    }
    return slot < 8 ? spells[slot] : nullptr;
}
EI_REPLACE_METHOD(0x005319A0, CUnit::GetSpell);

void* CUnit::GetSpellObject(std::uint32_t slot) {
    if (IsPartyUnit(this)) {
        if (!player) return nullptr;
        PartyUnitRecord* record = RecordsOf(player)->Find(id_);
        if (!record || slot >= record->spellCount) return nullptr;
        // No null check on the item here (the original reads through it).
        return record->spells[slot]->spell_;
    }
    if (slot >= 8 || !spells[slot]) return nullptr;
    return spells[slot]->spell_;
}
EI_REPLACE_METHOD(0x00531A10, CUnit::GetSpellObject);

bool CUnit::HasRangedWeapon() {
    std::uint32_t slot = activeWeapon;
    CEngineObject* weapon;
    if (IsPartyUnit(this)) {
        if (!player) return false;
        PartyUnitRecord* record = RecordsOf(player)->Find(id_);
        if (!record || slot >= record->weaponCount) return false;
        weapon = record->weapons[slot];
    } else {
        if (slot >= 4) return false;
        weapon = weapons[slot];
    }
    if (!weapon) return false;
    CItemWeapon* w = static_cast<CItemWeapon*>(weapon);
    return w->GetType() == 5 || w->GetType() == 6;
}
EI_REPLACE_METHOD(0x00530FA0, CUnit::HasRangedWeapon);

double CUnit::TotalItemWeight() {
    double total = kZero;   // accumulated on the FPU stack in the original
    for (CItem* item : weapons) {
        if (item) total += ItemWeight(item);
    }
    for (CItem* item : quickItems) {
        if (item) total += ItemWeight(item);
    }
    for (CItem* item : spells) {
        if (item) total += ItemWeight(item);
    }
    for (CItem* item : armor) {
        if (item) total += ItemWeight(item);
    }
    return total;   // unrounded (ST0)
}
EI_REPLACE_METHOD(0x005307B0, CUnit::TotalItemWeight);

int CUnit::CanCastSpell(std::uint32_t slot, const float*) {
    if (slot >= 8) return 3;
    if (HasEffect(0x20)) return 4;
    void* spell = GetSpellObject(slot);
    if (!spell) return 3;
    float cost = SpellCost(spell);
    float available = static_cast<float>(packedVitals.mana);
    double needed = Params()->Period() * cost;
    EI_NAN_AUDIT(needed);
    return available < needed ? 1 : 0;
}
EI_REPLACE_METHOD(0x0052F820, CUnit::CanCastSpell);

int CUnit::CanUseQuickItem(std::uint32_t slot, const float*) {
    CItem* object = GetQuickItem(slot);
    if (!object || object->GetClassId() != kClassQuickItem) return 3;
    CItem* item = object;
    if (item->GetCategory() != 5 && item->GetCategory() != 8) return 3;
    if (item->GetCategory() == 5) {
        EI_NAN_AUDIT(item->charge_, SpellCost(item->spell_));
        if (item->charge_ < SpellCost(item->spell_)) return 1;
    }
    return 0;
}
EI_REPLACE_METHOD(0x0052F8B0, CUnit::CanUseQuickItem);

// --- Movement --------------------------------------------------------------------------------

void CUnit::GetMoveSpeeds(float* forward, float* backward) {
    float base = static_cast<float>(baseSpeed) * kSpeedByteScale;
    *forward = base;
    *backward = base;
    std::uint8_t speed;
    switch (static_cast<std::uint32_t>(moveMode)) {
    case 0: speed = modeSpeeds[3]; break;
    case 1: speed = modeSpeeds[2]; break;
    case 3: speed = modeSpeeds[0]; break;
    case 2:
    case 4:
        *backward = static_cast<float>(modeSpeeds[0]) * *forward * kTwo * kByteScale;
        *forward = static_cast<float>(modeSpeeds[1]) * *forward * kTwo * kByteScale;
        *backward = BackwardSpeedScale() * *backward;
        return;
    default:
        *backward = BackwardSpeedScale() * *backward;
        return;
    }
    float scaled = static_cast<float>(speed) * *forward * kTwo * kByteScale;
    *forward = scaled;
    *backward = scaled;
    *backward = BackwardSpeedScale() * *backward;
}
EI_REPLACE_METHOD(0x0052F920, CUnit::GetMoveSpeeds);

namespace {
// Speed byte of movement mode `mode` (0..4) times the base speed; the base speed for others.
double ModeSpeedOf(const CUnit* unit, std::uint32_t mode) {
    double base = static_cast<float>(unit->baseSpeed) * kSpeedByteScale;   // kept on the FPU stack
    std::uint8_t speed;
    switch (mode) {
    case 0: speed = unit->modeSpeeds[3]; break;
    case 1: speed = unit->modeSpeeds[2]; break;
    case 2:
    case 4: speed = unit->modeSpeeds[1]; break;
    case 3: speed = unit->modeSpeeds[0]; break;
    default: return base;
    }
    return base * (static_cast<float>(speed) * kTwo) * kByteScale;
}
} // namespace

double CUnit::MoveSpeed2() { return ModeSpeedOf(this, static_cast<std::uint32_t>(moveMode2)); }
EI_REPLACE_METHOD(0x0052FA20, CUnit::MoveSpeed2);

double CUnit::MoveSpeedFor(int mode) { return ModeSpeedOf(this, static_cast<std::uint32_t>(mode)); }
EI_REPLACE_METHOD(0x0052FAF0, CUnit::MoveSpeedFor);

double CUnit::ScaledValue(float value) {
    if (!player) return value * kThree + kTen;
    if (flags_ & 0x4000000) {
        float capped = value >= kTen ? kTen : value;   // NaN is kept
        return capped * kThree + kTen;
    }
    return kNoPlayerValue;
}
EI_REPLACE_METHOD(0x0052FBC0, CUnit::ScaledValue);

double CUnit::BaseSpeed() {
    if (boundsFlags & 2) return above.z * kFastSpeedScale;
    return radius * kWalkSpeedScale + center.z;
}
EI_REPLACE_METHOD(0x00530370, CUnit::BaseSpeed);

double CUnit::ModeSpeed() {
    float speed = static_cast<float>(BaseSpeed());   // FSTP to a float local in the original
    if (moveMode != 0 && IsDead() == 0) {
        if (moveMode == 1) return speed * kSlowModeScale;
        return speed;
    }
    return speed * kIdleScale;
}
EI_REPLACE_METHOD(0x005302F0, CUnit::ModeSpeed);

// --- Prototypes ------------------------------------------------------------------------------

char* CUnit::GetPrototype() { return unitdb::Prototypes().RecordAt(prototypeId_, unitdb::kPrototypeSize); }
EI_REPLACE_METHOD(0x00530930, CUnit::GetPrototype);

char* CUnit::GetPrototypeModel() {
    char* prototype = unitdb::Prototypes().RecordAt(prototypeId_, unitdb::kPrototypeSize);
    if (!prototype) return nullptr;
    return unitdb::Models().RecordAt(static_cast<std::uint32_t>(reinterpret_cast<const UnitPrototypeRecord*>(prototype)->modelIndex),
                               unitdb::kModelSize);
}
EI_REPLACE_METHOD(0x00530970, CUnit::GetPrototypeModel);

char* CUnit::GetPrototype23C() {
    return unitdb::Prototypes().RecordAt(static_cast<std::uint32_t>(prototype23C), unitdb::kPrototypeSize);
}
EI_REPLACE_METHOD(0x005309E0, CUnit::GetPrototype23C);

std::uint32_t CUnit::SetPrototype23C(const char* record) {
    int offset = static_cast<int>(record - unitdb::Prototypes().begin);
    prototype23C = offset / unitdb::kPrototypeSize;
    return offset < 0 ? 1u : 0u;   // EAX: the sign of the unadjusted quotient
}
EI_REPLACE_METHOD(0x005308A0, CUnit::SetPrototype23C);

// --- Combat ----------------------------------------------------------------------------------

float CUnit::ActionSpeed(const FigureAnimation* entry) {
    switch (entry->key & 0x3C0000) {
    case 0x80000: return actionSpeeds[0];
    case 0xC0000: return actionSpeeds[1];
    case 0x200000: return actionSpeeds[2];
    case 0x1C0000: return actionSpeeds[3];
    case 0x240000: return type_ == kUnitTypeHero ? HeroTransitionSpeed() : actionSpeeds[4];
    case 0x40000: return actionSpeeds[5];
    default: return kOne;
    }
}
EI_REPLACE_METHOD(0x005327D0, CUnit::ActionSpeed);

bool CUnit::AcceptsAnimationImpl(const void* entry) {
    if (type_ != kUnitTypeHero) return true;
    std::uint32_t required = 0;
    if (Controller()->HasOtherArmour()) required = 0x10;
    if (Controller()->HasMainArmour()) required |= 0x20;
    return (static_cast<const FigureAnimation*>(entry)->flags & required) == required;
}
EI_REPLACE_METHOD(0x00532DA0, CUnit::AcceptsAnimationImpl);

double CUnit::AnimationRatio() {
    float denominator = static_cast<float>(AnimationFactor(baseScale.z));   // FSTP to a float local
    return AnimationFactor(scale.z) / denominator;
}
EI_REPLACE_METHOD(0x00532E00, CUnit::AnimationRatio);

double UnitParams::Period() { return 25.0f / static_cast<float>(attackRate); }   // unrounded (ST0)
EI_REPLACE_METHOD(0x005271C0, UnitParams::Period);

// --- Map object state ------------------------------------------------------------------------

BOOL CMapObjectServer::IsDead() { return static_cast<std::int16_t>(x87::fistp(vitals.hitPoints)) <= 0; }
BOOL __fastcall ServerIsDeadHook(CMapObjectServer* self) { return self->CMapObjectServer::IsDead(); }
EI_REPLACE(0x005156A0, ServerIsDeadHook);

BOOL CMapObjectServer::IsOverkilled() { return vitals.maxHitPoints * kOverkill > vitals.hitPoints ? 1 : 0; }
BOOL __fastcall ServerIsOverkilledHook(CMapObjectServer* self) { return self->CMapObjectServer::IsOverkilled(); }
EI_REPLACE(0x005156C0, ServerIsOverkilledHook);

BOOL CMapObjectClientSpecific::IsDead() { return Unit()->packedVitals.hitPoints <= 0; }
BOOL __fastcall ClientIsDeadHook(CMapObjectClientSpecific* self) { return self->CMapObjectClientSpecific::IsDead(); }
EI_REPLACE(0x005104D0, ClientIsDeadHook);

BOOL CMapObjectClientSpecific::IsOverkilled() {
    int maximum = Unit()->packedVitals.maxHitPoints;
    return Unit()->packedVitals.hitPoints < -maximum * 4 - maximum;
}
BOOL __fastcall ClientIsOverkilledHook(CMapObjectClientSpecific* self) {
    return self->CMapObjectClientSpecific::IsOverkilled();
}
EI_REPLACE(0x005104F0, ClientIsOverkilledHook);
