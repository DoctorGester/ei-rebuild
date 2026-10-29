#include "unit.h"

#include "ai_map.h"      // AIPath
#include "ai_motion.h"   // UnitMotion
#include "ei/x87.h"
#include "item.h"
#include "lib/nan_audit.h"
#include "logic_settings.h"
#include "party.h"
#include "player_messages.h"
#include "spell.h"
#include "unit_db.h"

#include <bit>
#include <cstring>

namespace {
constexpr float kSpeedByteScale = 0.039215688f;  // (1 / 25.5)
static_assert(std::bit_cast<std::uint32_t>(kSpeedByteScale) == 0x3D20A0A1u);
constexpr float kByteScale = 0.003921569f;       // (1 / 255)
static_assert(std::bit_cast<std::uint32_t>(kByteScale) == 0x3B808081u);
constexpr float kThree = 3.0f;
constexpr float kTen = 10.0f;
constexpr float kNoPlayerValue = 1000000.0f;
constexpr float kFastSpeedScale = 1.8f;
static_assert(std::bit_cast<std::uint32_t>(kFastSpeedScale) == 0x3FE66666u);
constexpr double kWalkSpeedScale = 0.7;                         // (a double)
constexpr float kSlowModeScale = 0.7f;
static_assert(std::bit_cast<std::uint32_t>(kSlowModeScale) == 0x3F333333u);
constexpr float kIdleScale = 0.2f;
static_assert(std::bit_cast<std::uint32_t>(kIdleScale) == 0x3E4CCCCDu);
constexpr float kOverkill = -5.0f;

constexpr int kPartRelations = 11;
constexpr int kPartEquipment = 5;

float ItemWeight(const Item* item) { return item->Weight(); }

float SpellCost(const CSpell* spell) { return spell->manaCost; }

// The party of the unit's player.
CParty* RecordsOf(CPlayer* player) { return player->Party(); }

// The units of a player's party have a client-specific controller; their items live in the player's inventory
// records.
bool IsPartyUnit(CUnit* unit) { return unit->controller->GetClassId() == ClassId::UnitClientSpecific; }

// Puts `item` into the first free slot of `slots`, taking a reference: the slot, 1 when it is
// already there (whatever its slot), -1 when full.
int AddToSlots(CUnit* unit, Item** slots, std::uint32_t count, Item* item) {
    for (std::uint32_t i = 0; i < count; ++i) {
        if (slots[i] == item) return 1;
        if (!slots[i]) {
            slots[i] = item;
            item->AddRef();
            unit->MarkChanged(kPartEquipment);
            return static_cast<int>(i);
        }
    }
    return -1;
}

void RemoveFromSlots(CUnit* unit, Item** slots, std::uint32_t slot) {
    if (slot >= 4) return;
    Item* item = slots[slot];
    slots[slot] = nullptr;
    if (item) item->Release();
    unit->MarkChanged(kPartEquipment);
}

// Speed byte of movement mode `mode` (0..4) times the base speed; the base speed for others.
double ModeSpeedOf(const CUnit* unit, std::uint32_t mode) {
    double base = static_cast<float>(unit->params.baseSpeed) * kSpeedByteScale;   // kept on the FPU stack
    std::uint8_t speed;
    switch (mode) {
    case 0: speed = unit->params.modeSpeeds[3]; break;
    case 1: speed = unit->params.modeSpeeds[2]; break;
    case 2:
    case 4: speed = unit->params.modeSpeeds[1]; break;
    case 3: speed = unit->params.modeSpeeds[0]; break;
    default: return base;
    }
    return base * (static_cast<float>(speed) * 2.0f) * kByteScale;
}

} // namespace

// --- Sides -----------------------------------------------------------------------------------

void CUnit::AddHostileMask(std::uint32_t mask) {
    std::uint32_t merged = relationsA | mask;
    if (relationsA == merged) return;
    relationsA = merged;
    flags |= MapObjectFlag::RelationsChanged;
    MarkChanged(kPartRelations);
}

void CUnit::SetRelationsB(std::uint32_t mask) {
    relationsB = mask;
    MarkChanged(kPartRelations);
}

void CUnit::SetSide(int value) {
    side = value;
    flags |= MapObjectFlag::RelationsChanged;
    if (controller) MarkChanged(kPartRelations);
}

bool CUnit::IsHostileTo(const CUnit* other) {
    return ((1u << (other->side & 31)) & relationsB & relationsA) != 0;
}

// --- Simple state setters --------------------------------------------------------------------

void CUnit::SetFightAtWill(int value) {
    fightAtWill = value;
    MarkChanged(13);
}

void CUnit::SetAnimationEntries(std::int8_t entry, std::int8_t second, int start, std::int8_t sound) {
    animationEntry = entry;
    secondEntry = second;
    actionSound = sound;
    animationStart = start;
    MarkChanged(8);
}

void CUnit::SetMotionMode(int value) {
    if (motionMode == value) return;
    motionMode = value;
    MarkChanged(7);
}

void CUnit::SetPathTimeShift(int value) {
    if (pathTimeShift == value) return;
    pathTimeShift = value;
    MarkChanged(7);
}

void CUnit::SetMoveModes(int mode, int mode2) {
    moveMode = mode;
    moveMode2 = mode2;
    MarkChanged(6);
}

void CUnit::ShowHelmet(bool show) {
    if (show) {
        flags &= ~MapObjectFlag::HelmetHidden;
    } else {
        flags |= MapObjectFlag::HelmetHidden;
    }
    MarkChanged(5);
}

std::uint32_t CUnit::SetStunned(int on) {
    if (on) {
        flags |= MapObjectFlag::Frozen;
    } else {
        flags &= ~MapObjectFlag::Frozen;
    }
    return flags;
}

// --- Equipment -------------------------------------------------------------------------------

void CUnit::GetEquipmentIds(std::uint32_t* out) {
    for (int i = 0; i < 4; ++i) {
        out[i] = weapons[i] ? weapons[i]->prototypeId : 0xFFFFFFFFu;
    }
    for (int i = 0; i < 7; ++i) {
        out[4 + i] = armor[i] ? armor[i]->prototypeId : 0xFFFFFFFFu;
    }
}

int CUnit::AddWeapon(Item* item) {
    if (!item) return 0;
    return AddToSlots(this, weapons, 4, item);
}

int CUnit::AddQuickItem(Item* item) {
    if (!item) return 0;
    return AddToSlots(this, quickItems, 4, item);
}

int CUnit::AddSpell(Item* item) {
    // No null check here: a null item "is already there" at the first empty slot (returns 1).
    return AddToSlots(this, spells, 8, item);
}

void CUnit::RemoveWeapon(std::uint32_t slot) { RemoveFromSlots(this, weapons, slot); }

void CUnit::RemoveQuickItem(std::uint32_t slot) { RemoveFromSlots(this, quickItems, slot); }

ItemWeapon* CUnit::GetWeapon(std::uint32_t slot) {
    if (IsPartyUnit(this)) {
        if (!player) return nullptr;
        PartyUnitRecord* record = RecordsOf(player)->Find(id);
        if (!record || slot >= static_cast<std::uint32_t>(std::ssize(record->weapons))) return nullptr;
        return AsWeapon(record->weapons[slot]);
    }
    return slot < 4 ? AsWeapon(weapons[slot]) : nullptr;
}

Item* CUnit::GetQuickItem(std::uint32_t slot) {
    if (IsPartyUnit(this)) {
        if (!player) return nullptr;
        PartyUnitRecord* record = RecordsOf(player)->Find(id);
        if (!record || slot >= static_cast<std::uint32_t>(std::ssize(record->quickItems))) return nullptr;
        Item* item = record->quickItems[slot];
        if (!item || static_cast<int>(item->count) < 1) return nullptr;
        return item;
    }
    return slot < 4 ? quickItems[slot] : nullptr;
}

Item* CUnit::GetSpell(std::uint32_t slot) {
    if (IsPartyUnit(this)) {
        if (!player) return nullptr;
        PartyUnitRecord* record = RecordsOf(player)->Find(id);
        if (!record || slot >= static_cast<std::uint32_t>(std::ssize(record->spells))) return nullptr;
        return record->spells[slot];
    }
    return slot < 8 ? spells[slot] : nullptr;
}

CSpell* CUnit::GetSpellObject(std::uint32_t slot) {
    if (IsPartyUnit(this)) {
        if (!player) return nullptr;
        PartyUnitRecord* record = RecordsOf(player)->Find(id);
        if (!record || slot >= static_cast<std::uint32_t>(std::ssize(record->spells))) return nullptr;
        // No null check on the item here.
        return record->spells[slot]->spell;
    }
    if (slot >= 8 || !spells[slot]) return nullptr;
    return spells[slot]->spell;
}

double CUnit::TotalItemWeight() {
    double total = 0.0;   // accumulated on the FPU stack
    for (Item* item : weapons) {
        if (item) total += ItemWeight(item);
    }
    for (Item* item : quickItems) {
        if (item) total += ItemWeight(item);
    }
    for (Item* item : spells) {
        if (item) total += ItemWeight(item);
    }
    for (Item* item : armor) {
        if (item) total += ItemWeight(item);
    }
    return total;   // unrounded
}

bool CUnit::CanCastSpell(std::uint32_t slot) {
    if (slot >= 8) return false;
    if (HasEffect(SpellType::Feeblemind)) return false;
    CSpell* spell = GetSpellObject(slot);
    if (!spell) return false;
    float cost = SpellCost(spell);
    float available = static_cast<float>(packedVitals.mana);
    double needed = params.Period() * cost;
    EI_NAN_AUDIT(needed);
    return !(available < needed);   // a NaN cost can cast
}

bool CUnit::CanUseQuickItem(std::uint32_t slot) {
    Item* object = GetQuickItem(slot);
    if (!object || object->GetClassId() != ClassId::ItemQuick) return false;
    Item* item = object;
    if (item->GetCategory() != QuickItemCategory::Wand && item->GetCategory() != QuickItemCategory::Potion) return false;
    if (item->GetCategory() == QuickItemCategory::Wand) {
        EI_NAN_AUDIT(item->charge, SpellCost(item->spell));
        if (item->charge < SpellCost(item->spell)) return false;
    }
    return true;
}

// --- Movement --------------------------------------------------------------------------------

void CUnit::GetMoveSpeeds(float* forward, float* backward) {
    float base = static_cast<float>(params.baseSpeed) * kSpeedByteScale;
    *forward = base;
    *backward = base;
    std::uint8_t speed;
    switch (static_cast<std::uint32_t>(moveMode)) {
    case 0: speed = params.modeSpeeds[3]; break;
    case 1: speed = params.modeSpeeds[2]; break;
    case 3: speed = params.modeSpeeds[0]; break;
    case 2:
    case 4:
        *backward = static_cast<float>(params.modeSpeeds[0]) * *forward * 2.0f * kByteScale;
        *forward = static_cast<float>(params.modeSpeeds[1]) * *forward * 2.0f * kByteScale;
        *backward = g_logicSettings.rotateSpeedMult * *backward;
        return;
    default:
        *backward = g_logicSettings.rotateSpeedMult * *backward;
        return;
    }
    float scaled = static_cast<float>(speed) * *forward * 2.0f * kByteScale;
    *forward = scaled;
    *backward = scaled;
    *backward = g_logicSettings.rotateSpeedMult * *backward;
}

double CUnit::MoveSpeed2() { return ModeSpeedOf(this, static_cast<std::uint32_t>(moveMode2)); }

double CUnit::MoveSpeedFor(int mode) { return ModeSpeedOf(this, static_cast<std::uint32_t>(mode)); }

double CUnit::ScaledValue(float value) {
    if (!player) return value * kThree + kTen;
    if (flags & MapObjectFlag::ChoosingTarget) {
        float capped = value >= kTen ? kTen : value;   // NaN is kept
        return capped * kThree + kTen;
    }
    return kNoPlayerValue;
}

double CUnit::BaseSpeed() {
    if (lib::Has(bounds.flags, BoundsFlag::Box)) return bounds.boxMax.z * kFastSpeedScale;
    return bounds.radius * kWalkSpeedScale + bounds.center.z;
}

double CUnit::ModeSpeed() {
    float speed = static_cast<float>(BaseSpeed());   // stored as a float
    if (moveMode != 0 && IsDead() == 0) {
        if (moveMode == 1) return speed * kSlowModeScale;
        return speed;
    }
    return speed * kIdleScale;
}

// --- Prototypes ------------------------------------------------------------------------------

UnitPrototypeRecord* CUnit::GetPrototype() { return unitdb::PrototypeRecord(prototypeId); }

UnitModelRecord* CUnit::GetPrototypeModel() {
    const UnitPrototypeRecord* prototype = unitdb::PrototypeRecord(prototypeId);
    if (!prototype) return nullptr;
    return unitdb::ModelRecord(static_cast<std::uint32_t>(prototype->modelIndex));
}

UnitPrototypeRecord* CUnit::GetSecondPrototype() {
    return unitdb::PrototypeRecord(static_cast<std::uint32_t>(secondPrototype));
}

std::uint32_t CUnit::SetSecondPrototype(const UnitPrototypeRecord* record) {
    int offset = unitdb::PrototypeByteOffset(record);
    secondPrototype = offset / static_cast<int>(sizeof(UnitPrototypeRecord));
    return offset < 0 ? 1u : 0u;   // the sign of the unadjusted quotient
}

// --- Combat ----------------------------------------------------------------------------------

float CUnit::ActionSpeed(const FigureAnimation* entry) {
    switch (entry->Kind()) {
    case AnimationKind::Attack:
        return actionSpeeds[0];
    case AnimationKind::Cast:
        return actionSpeeds[1];
    case AnimationKind::Hit:
        return actionSpeeds[2];
    case AnimationKind::Death:
        return actionSpeeds[3];
    case AnimationKind::Cross:
        return MapType() == MapObjectType::Hero ? g_logicSettings.humanCrossAnimationSpeed : actionSpeeds[4];
    case AnimationKind::Special:
        return actionSpeeds[5];
    default: return 1.0f;
    }
}

bool CUnit::AcceptsAnimation(const FigureAnimation* entry) {
    if (MapType() != MapObjectType::Hero) return true;
    std::uint32_t required = 0;
    if (Controller()->HasOtherArmour()) required = 0x10;
    if (Controller()->HasMainArmour()) required |= 0x20;
    return (entry->flags & required) == required;
}

double CUnit::AnimationRatio() {
    float denominator = static_cast<float>(AnimationFactor(baseScale.z));   // stored as a float
    return AnimationFactor(scale.z) / denominator;
}

double UnitParams::Period() { return 25.0f / static_cast<float>(intelligence); }   // unrounded

// --- Map object state ------------------------------------------------------------------------

bool MapObjectServer::IsDead() { return static_cast<std::int16_t>(x87::fistp(vitals.hitPoints)) <= 0; }

bool MapObjectServer::IsOverkilled() { return vitals.maxHitPoints * kOverkill > vitals.hitPoints; }

bool MapObjectClientSpecific::IsDead() { return Unit()->packedVitals.hitPoints <= 0; }

bool MapObjectClientSpecific::IsOverkilled() {
    int maximum = Unit()->packedVitals.maxHitPoints;
    return Unit()->packedVitals.hitPoints < -maximum * 4 - maximum;
}

// --- The class name and id, the motion given its speeds, the player test, twice the height above the
// centre and a worn armour piece.

void CUnit::SetMotionSpeeds(float speed, float turnRate) {
    PathOrder* path = Controller()->Path();
    path->ForgetObject(id);
    UnitMotion* unitMotion = &motion;
    unitMotion->segment = -1;
    unitMotion->speedScale = speed;
    unitMotion->baseTurnRate = turnRate;
    unitMotion->Build(path);
}

bool CUnit::HasPlayer() const { return player != nullptr; }

double CUnit::DoubleHeight() const { return bounds.boxMax.z + bounds.boxMax.z; }

ItemArmor* CUnit::WornItem(int slot) const { return armor[slot]; }

// --- Hits, forgotten objects and the script name ----------------------------------------------------

void CUnit::ReceiveHit(DamageInfo* hit, CUnit* attacker, CUnit* caster) {
    Controller()->TakeHit(hit, attacker, caster);
}

// Nothing reads the result.
int CUnit::ForgetObject(EngineObject* object) {
    Controller()->ObjectDeleted(object);
    return 0;
}

void CUnit::SetScriptName(std::string_view newName) { scriptName = newName; }
