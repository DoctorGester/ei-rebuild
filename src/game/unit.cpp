#include "unit.h"

#include "item.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "ei/x87.h"
#include "originals_engine.h"

#include <bit>
#include <cstring>

// /fp:fast reassociates and distributes (e.g. (a - 25) + b became (a + b) - 25); the formulas here
// must keep the original's order of operations.
#pragma float_control(precise, on)

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
constexpr std::uint32_t kFlagDirty = 2;
constexpr std::uint32_t kFlagSideChanged = 0x80000;
constexpr int kClassQuickItem = 0x13;

// Globals read by the formulas.
float BackwardSpeedScale() { return *reinterpret_cast<const float*>(0x007C5438); }
float HeroResistance9() { return *reinterpret_cast<const float*>(0x007C542C); }

float ItemWeight(const CEngineObject* item) { return Field<float>(reinterpret_cast<const char*>(item), 0x34); }

PartyRecords* RecordsOf(char* player) { return reinterpret_cast<PartyRecords*>(player + 0x70); }

bool IsPartyUnit(CUnit* unit) { return unit->controller->GetClassId() == kClassPartyController; }

// Puts `item` into the first free slot of `slots`, taking a reference: the slot, 1 when it is
// already there (whatever its slot), -1 when full.
int AddToSlots(CUnit* unit, CEngineObject** slots, std::uint32_t count, CEngineObject* item) {
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

std::uintptr_t RemoveFromSlots(CUnit* unit, CEngineObject** slots, std::uint32_t slot) {
    if (slot >= 4) return slot;
    CEngineObject* item = slots[slot];
    slots[slot] = nullptr;
    if (item) item->Release();
    return unit->MarkChanged(kPartEquipment);
}
} // namespace

void* CUnit::World() { return ObjectWorld(this); }
__declspec(naked) double CUnit::AnimationFactor(float) EI_ORIGINAL(0x00518D30)
__declspec(naked) PartyUnitRecord* PartyRecords::Find(int) EI_ORIGINAL(0x006621B0)

// --- Change tracking -------------------------------------------------------------------------

std::uintptr_t CUnit::MarkChanged(int part) {
    if (part >= 0) changedParts = static_cast<std::uint16_t>(changedParts | (1u << (part & 31)));
    flags_ |= kFlagDirty;
    if (refCount_ == 0) return 0;
    void* world = World();
    if (world) changeStamp = Field<int>(static_cast<const char*>(world), 0x68);
    return reinterpret_cast<std::uintptr_t>(world);
}
EI_REPLACE_METHOD(0x0050E9F0, CUnit::MarkChanged);

BOOL CUnit::HasEffect(std::uint32_t type) {
    for (int i = 0; i < effectCount; ++i) {
        UnitEffect effect = effects[i];   // the original copies the whole record
        if ((effect.kind & 0xFF) == type) return 1;
    }
    return 0;
}
EI_REPLACE_METHOD(0x0050F630, CUnit::HasEffect);

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
    unknown_438 = value;
    return MarkChanged(13);
}
EI_REPLACE_METHOD(0x0052E9B0, CUnit::SetUnknown438);

std::uintptr_t CUnit::SetUnknown254(std::int8_t a, std::int8_t b, int c, std::int8_t d) {
    unknown_254 = a;
    unknown_255 = b;
    unknown_256 = d;
    unknown_258 = c;
    return MarkChanged(8);
}
EI_REPLACE_METHOD(0x0052EA90, CUnit::SetUnknown254);

std::uintptr_t CUnit::SetUnknown25C(int value) {
    if (unknown_25C == value) return static_cast<std::uintptr_t>(value);
    unknown_25C = value;
    return MarkChanged(7);
}
EI_REPLACE_METHOD(0x0052EAD0, CUnit::SetUnknown25C);

std::uintptr_t CUnit::SetUnknown260(int value) {
    if (unknown_260 == value) return static_cast<std::uintptr_t>(value);
    unknown_260 = value;
    return MarkChanged(7);
}
EI_REPLACE_METHOD(0x0052EAF0, CUnit::SetUnknown260);

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

std::uint32_t CUnit::SetFlag1000000(int on) {
    if (on) {
        flags_ |= 0x1000000u;
    } else {
        flags_ &= ~0x1000000u;
    }
    return flags_;
}
EI_REPLACE_METHOD(0x0052F7F0, CUnit::SetFlag1000000);

// --- Equipment -------------------------------------------------------------------------------

void CUnit::GetEquipmentIds(int* out) {
    for (int i = 0; i < 4; ++i) {
        out[i] = weapons[i] ? static_cast<int>(weapons[i]->unknown_18) : -1;
    }
    for (int i = 0; i < 7; ++i) {
        out[4 + i] = armor[i] ? static_cast<int>(armor[i]->unknown_18) : -1;
    }
}
EI_REPLACE_METHOD(0x0052EC10, CUnit::GetEquipmentIds);

int CUnit::AddWeapon(CEngineObject* item) {
    if (!item) return 0;
    return AddToSlots(this, weapons, 4, item);
}
EI_REPLACE_METHOD(0x00531070, CUnit::AddWeapon);

int CUnit::AddQuickItem(CEngineObject* item) {
    if (!item) return 0;
    return AddToSlots(this, quickItems, 4, item);
}
EI_REPLACE_METHOD(0x00531180, CUnit::AddQuickItem);

int CUnit::AddSpell(CEngineObject* item) {
    // No null check here: a null item "is already there" at the first empty slot (returns 1).
    return AddToSlots(this, spells, 8, item);
}
EI_REPLACE_METHOD(0x00531820, CUnit::AddSpell);

std::uintptr_t CUnit::RemoveWeapon(std::uint32_t slot) { return RemoveFromSlots(this, weapons, slot); }
EI_REPLACE_METHOD(0x005310D0, CUnit::RemoveWeapon);

std::uintptr_t CUnit::RemoveQuickItem(std::uint32_t slot) { return RemoveFromSlots(this, quickItems, slot); }
EI_REPLACE_METHOD(0x005311E0, CUnit::RemoveQuickItem);

CEngineObject* CUnit::GetWeapon(std::uint32_t slot) {
    if (IsPartyUnit(this)) {
        if (!player) return nullptr;
        PartyUnitRecord* record = RecordsOf(player)->Find(id_);
        if (!record || slot >= record->weaponCount) return nullptr;
        return record->weapons[slot];
    }
    return slot < 4 ? weapons[slot] : nullptr;
}
EI_REPLACE_METHOD(0x005318B0, CUnit::GetWeapon);

CEngineObject* CUnit::GetQuickItem(std::uint32_t slot) {
    if (IsPartyUnit(this)) {
        if (!player) return nullptr;
        PartyUnitRecord* record = RecordsOf(player)->Find(id_);
        if (!record || slot >= record->quickItemCount) return nullptr;
        CEngineObject* item = record->quickItems[slot];
        if (!item || Field<int>(reinterpret_cast<const char*>(item), 0x48) < 1) return nullptr;
        return item;
    }
    return slot < 4 ? quickItems[slot] : nullptr;
}
EI_REPLACE_METHOD(0x00531920, CUnit::GetQuickItem);

CEngineObject* CUnit::GetSpell(std::uint32_t slot) {
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
        return Field<void*>(reinterpret_cast<const char*>(record->spells[slot]), 0x44);
    }
    if (slot >= 8 || !spells[slot]) return nullptr;
    return static_cast<CItem*>(spells[slot])->spell_;
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
    for (CEngineObject* item : weapons) {
        if (item) total += ItemWeight(item);
    }
    for (CEngineObject* item : quickItems) {
        if (item) total += ItemWeight(item);
    }
    for (CEngineObject* item : spells) {
        if (item) total += ItemWeight(item);
    }
    for (CEngineObject* item : armor) {
        if (item) total += ItemWeight(item);
    }
    return total;   // unrounded (ST0)
}
EI_REPLACE_METHOD(0x005307B0, CUnit::TotalItemWeight);

int CUnit::CanCastSpell(std::uint32_t slot, int) {
    if (slot >= 8) return 3;
    if (HasEffect(0x20)) return 4;
    void* spell = GetSpellObject(slot);
    if (!spell) return 3;
    float cost = Field<float>(static_cast<const char*>(spell), 0x40);
    float available = static_cast<float>(mana);
    double needed = reinterpret_cast<UnitParams*>(unknown_330)->Period() * cost;
    return !(available >= needed) ? 1 : 0;
}
EI_REPLACE_METHOD(0x0052F820, CUnit::CanCastSpell);

int CUnit::CanUseQuickItem(std::uint32_t slot, int) {
    CEngineObject* object = GetQuickItem(slot);
    if (!object || object->GetClassId() != kClassQuickItem) return 3;
    CItem* item = static_cast<CItem*>(object);
    if (item->GetCategory() != 5 && item->GetCategory() != 8) return 3;
    const char* spell = static_cast<const char*>(item->spell_);
    if (item->GetCategory() == 5 && !(item->charge_ >= Field<float>(spell, 0x40))) return 1;
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
        float capped = !(value >= kTen) ? value : kTen;   // NaN is kept
        return capped * kThree + kTen;
    }
    return kNoPlayerValue;
}
EI_REPLACE_METHOD(0x0052FBC0, CUnit::ScaledValue);

double CUnit::BaseSpeed() {
    if (boundsFlags & 2) return above[2] * kFastSpeedScale;
    return radius * kWalkSpeedScale + center[2];
}
EI_REPLACE_METHOD(0x00530370, CUnit::BaseSpeed);

double CUnit::ModeSpeed() {
    volatile float speed = static_cast<float>(BaseSpeed());   // FSTP to a float local in the original
    if (moveMode != 0 && ei::VCall<int>(this, 0xDC / 4) == 0) {
        if (moveMode == 1) return speed * kSlowModeScale;
        return speed;
    }
    return speed * kIdleScale;
}
EI_REPLACE_METHOD(0x005302F0, CUnit::ModeSpeed);

// --- Prototypes ------------------------------------------------------------------------------

char* CUnit::GetPrototype() { return unitdb::Prototypes().At(unknown_18, unitdb::kPrototypeSize); }
EI_REPLACE_METHOD(0x00530930, CUnit::GetPrototype);

char* CUnit::GetPrototypeModel() {
    char* prototype = unitdb::Prototypes().At(unknown_18, unitdb::kPrototypeSize);
    if (!prototype) return nullptr;
    return unitdb::Models().At(Field<std::uint32_t>(prototype, 0x08), unitdb::kModelSize);
}
EI_REPLACE_METHOD(0x00530970, CUnit::GetPrototypeModel);

char* CUnit::GetPrototype23C() {
    return unitdb::Prototypes().At(static_cast<std::uint32_t>(prototype23C), unitdb::kPrototypeSize);
}
EI_REPLACE_METHOD(0x005309E0, CUnit::GetPrototype23C);

std::uint32_t CUnit::SetPrototype23C(const char* record) {
    int offset = static_cast<int>(record - unitdb::Prototypes().begin);
    prototype23C = offset / unitdb::kPrototypeSize;
    return offset < 0 ? 1u : 0u;   // EAX: the sign of the unadjusted quotient
}
EI_REPLACE_METHOD(0x005308A0, CUnit::SetPrototype23C);

// --- Combat ----------------------------------------------------------------------------------

float CUnit::ResistanceTo(const void* hit) {
    switch (Field<std::uint32_t>(static_cast<const char*>(hit), 0x14) & 0x3C0000) {
    case 0x80000: return resistances[0];
    case 0xC0000: return resistances[1];
    case 0x200000: return resistances[2];
    case 0x1C0000: return resistances[3];
    case 0x240000: return type_ == kUnitTypeHero ? HeroResistance9() : resistances[4];
    case 0x40000: return resistances[5];
    default: return kOne;
    }
}
EI_REPLACE_METHOD(0x005327D0, CUnit::ResistanceTo);

bool CUnit::AcceptsTarget(const void* target) {
    if (type_ != kUnitTypeHero) return true;
    std::uint32_t required = 0;
    if (ei::VCall<bool>(controller, 0x90 / 4)) required = 0x10;
    if (ei::VCall<bool>(controller, 0x94 / 4)) required |= 0x20;
    return (Field<std::uint32_t>(static_cast<const char*>(target), 0x18) & required) == required;
}
EI_REPLACE_METHOD(0x00532DA0, CUnit::AcceptsTarget);

double CUnit::AnimationRatio() {
    volatile float denominator = static_cast<float>(AnimationFactor(animation1B4));   // FSTP to a float local
    return AnimationFactor(animationCC) / denominator;
}
EI_REPLACE_METHOD(0x00532E00, CUnit::AnimationRatio);

double UnitParams::Period() { return 25.0f / static_cast<float>(attackRate); }   // unrounded (ST0)
EI_REPLACE_METHOD(0x005271C0, UnitParams::Period);

// --- Map object state ------------------------------------------------------------------------

BOOL CMapObjectServer::IsDead() { return static_cast<std::int16_t>(x87::fistp(vitals.hitPoints)) <= 0; }
EI_REPLACE_METHOD(0x005156A0, CMapObjectServer::IsDead);

BOOL CMapObjectServer::IsOverkilled() { return vitals.maxHitPoints * kOverkill > vitals.hitPoints ? 1 : 0; }
EI_REPLACE_METHOD(0x005156C0, CMapObjectServer::IsOverkilled);

BOOL CMapObjectClientSpecific::IsDead() { return object->hitPoints <= 0; }
EI_REPLACE_METHOD(0x005104D0, CMapObjectClientSpecific::IsDead);

BOOL CMapObjectClientSpecific::IsOverkilled() {
    int maximum = object->maxHitPoints;
    return object->hitPoints < -maximum * 4 - maximum;
}
EI_REPLACE_METHOD(0x005104F0, CMapObjectClientSpecific::IsOverkilled);
