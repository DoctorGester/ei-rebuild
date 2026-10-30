#include "unit.h"

#include "ai_map.h"      // AIPath
#include "ai_motion.h"   // UnitMotion
#include "item.h"
#include "lib/float_select.h"
#include "lib/nan_audit.h"
#include "lib/pc24.h"
#include "lib/rounding.h"
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

float SpellCost(const Spell* spell) { return spell->manaCost; }

// The party of the unit's player.
Party* RecordsOf(Player* player) { return player->GetParty(); }

// The units of a player's party have a client-specific controller; their items live in the player's inventory
// records.
bool IsPartyUnit(Unit* unit) { return unit->controller->GetClassId() == ClassId::UnitClientSpecific; }

// Puts `item` into the first free slot of `slots`, taking a reference: the slot, 1 when it is
// already there (whatever its slot), -1 when full.
int AddToSlots(Unit* unit, Item** slots, std::uint32_t count, Item* item) {
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

void RemoveFromSlots(Unit* unit, Item** slots, std::uint32_t slot) {
    if (slot >= 4) return;
    Item* item = slots[slot];
    slots[slot] = nullptr;
    if (item) item->Release();
    unit->MarkChanged(kPartEquipment);
}

// Speed byte of movement mode `mode` (0..4) times the base speed; the base speed for others.
float ModeSpeedOf(const Unit* unit, std::uint32_t mode) {
    float base = static_cast<float>(unit->params.baseSpeed) * kSpeedByteScale;
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

void Unit::AddHostileMask(std::uint32_t mask) {
    std::uint32_t merged = relationsA | mask;
    if (relationsA == merged) return;
    relationsA = merged;
    flags |= MapObjectFlag::RelationsChanged;
    MarkChanged(kPartRelations);
}

void Unit::SetRelationsB(std::uint32_t mask) {
    relationsB = mask;
    MarkChanged(kPartRelations);
}

void Unit::SetSide(int value) {
    side = value;
    flags |= MapObjectFlag::RelationsChanged;
    if (controller) MarkChanged(kPartRelations);
}

bool Unit::IsHostileTo(const Unit* other) {
    return ((1u << (other->side & 31)) & relationsB & relationsA) != 0;
}

// --- Simple state setters --------------------------------------------------------------------

void Unit::SetFightAtWill(int value) {
    fightAtWill = value;
    MarkChanged(13);
}

void Unit::SetAnimationEntries(std::int8_t entry, std::int8_t second, int start, std::int8_t sound) {
    animationEntry = entry;
    secondEntry = second;
    actionSound = sound;
    animationStart = start;
    MarkChanged(8);
}

void Unit::SetMotionMode(int value) {
    if (motionMode == value) return;
    motionMode = value;
    MarkChanged(7);
}

void Unit::SetPathTimeShift(int value) {
    if (pathTimeShift == value) return;
    pathTimeShift = value;
    MarkChanged(7);
}

void Unit::SetMoveModes(int mode, int mode2) {
    moveMode = mode;
    moveMode2 = mode2;
    MarkChanged(6);
}

void Unit::ShowHelmet(bool show) {
    if (show) {
        flags &= ~MapObjectFlag::HelmetHidden;
    } else {
        flags |= MapObjectFlag::HelmetHidden;
    }
    MarkChanged(5);
}

std::uint32_t Unit::SetStunned(int on) {
    if (on) {
        flags |= MapObjectFlag::Frozen;
    } else {
        flags &= ~MapObjectFlag::Frozen;
    }
    return flags;
}

// --- Equipment -------------------------------------------------------------------------------

void Unit::GetEquipmentIds(std::uint32_t* out) {
    for (int i = 0; i < 4; ++i) {
        out[i] = weapons[i] ? weapons[i]->prototypeId : 0xFFFFFFFFu;
    }
    for (int i = 0; i < 7; ++i) {
        out[4 + i] = armor[i] ? armor[i]->prototypeId : 0xFFFFFFFFu;
    }
}

int Unit::AddWeapon(Item* item) {
    if (!item) return 0;
    return AddToSlots(this, weapons, 4, item);
}

int Unit::AddQuickItem(Item* item) {
    if (!item) return 0;
    return AddToSlots(this, quickItems, 4, item);
}

int Unit::AddSpell(Item* item) {
    // No null check here: a null item "is already there" at the first empty slot (returns 1).
    return AddToSlots(this, spells, 8, item);
}

void Unit::RemoveWeapon(std::uint32_t slot) { RemoveFromSlots(this, weapons, slot); }

void Unit::RemoveQuickItem(std::uint32_t slot) { RemoveFromSlots(this, quickItems, slot); }

ItemWeapon* Unit::GetWeapon(std::uint32_t slot) {
    if (IsPartyUnit(this)) {
        if (!player) return nullptr;
        PartyUnitRecord* record = RecordsOf(player)->Find(id);
        if (!record || slot >= static_cast<std::uint32_t>(std::ssize(record->weapons))) return nullptr;
        return AsWeapon(record->weapons[slot]);
    }
    return slot < 4 ? AsWeapon(weapons[slot]) : nullptr;
}

Item* Unit::GetQuickItem(std::uint32_t slot) {
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

Item* Unit::GetSpell(std::uint32_t slot) {
    if (IsPartyUnit(this)) {
        if (!player) return nullptr;
        PartyUnitRecord* record = RecordsOf(player)->Find(id);
        if (!record || slot >= static_cast<std::uint32_t>(std::ssize(record->spells))) return nullptr;
        return record->spells[slot];
    }
    return slot < 8 ? spells[slot] : nullptr;
}

Spell* Unit::GetSpellObject(std::uint32_t slot) {
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

float Unit::TotalItemWeight() {
    float total = 0.0f;
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
    return total;
}

bool Unit::CanCastSpell(std::uint32_t slot) {
    if (slot >= 8) return false;
    if (HasEffect(SpellType::Feeblemind)) return false;
    Spell* spell = GetSpellObject(slot);
    if (!spell) return false;
    float cost = SpellCost(spell);
    float available = static_cast<float>(packedVitals.mana);
    float needed = params.Period() * cost;
    EI_NAN_AUDIT(needed);
    return !(available < needed);   // a NaN cost can cast
}

bool Unit::CanUseQuickItem(std::uint32_t slot) {
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

Unit::MoveSpeeds Unit::GetMoveSpeeds() {
    float base = static_cast<float>(params.baseSpeed) * kSpeedByteScale;
    MoveSpeeds speeds{base, base};
    std::uint8_t speed;
    switch (static_cast<std::uint32_t>(moveMode)) {
    case 0: speed = params.modeSpeeds[3]; break;
    case 1: speed = params.modeSpeeds[2]; break;
    case 3: speed = params.modeSpeeds[0]; break;
    case 2:
    case 4:
        speeds.turn = static_cast<float>(params.modeSpeeds[0]) * speeds.forward * 2.0f * kByteScale;
        speeds.forward = static_cast<float>(params.modeSpeeds[1]) * speeds.forward * 2.0f * kByteScale;
        speeds.turn = g_logicSettings.rotateSpeedMult * speeds.turn;
        return speeds;
    default:
        speeds.turn = g_logicSettings.rotateSpeedMult * speeds.turn;
        return speeds;
    }
    float scaled = static_cast<float>(speed) * speeds.forward * 2.0f * kByteScale;
    speeds.forward = scaled;
    speeds.turn = scaled;
    speeds.turn = g_logicSettings.rotateSpeedMult * speeds.turn;
    return speeds;
}

float Unit::MoveSpeed2() { return ModeSpeedOf(this, static_cast<std::uint32_t>(moveMode2)); }

float Unit::MoveSpeedFor(int mode) { return ModeSpeedOf(this, static_cast<std::uint32_t>(mode)); }

float Unit::ScaledValue(float value) {
    if (!player) return value * kThree + kTen;
    if (flags & MapObjectFlag::ChoosingTarget) {
        float capped = MinKeepA(value, kTen);   // NaN is kept
        return capped * kThree + kTen;
    }
    return kNoPlayerValue;
}

float Unit::BaseSpeed() {
    if (lib::Has(bounds.flags, BoundsFlag::Box)) return bounds.boxMax.z * kFastSpeedScale;
    return pc24::Mul(bounds.radius, kWalkSpeedScale) + bounds.center.z;
}

float Unit::ModeSpeed() {
    float speed = BaseSpeed();
    if (moveMode != 0 && IsDead() == 0) {
        if (moveMode == 1) return speed * kSlowModeScale;
        return speed;
    }
    return speed * kIdleScale;
}

// --- Prototypes ------------------------------------------------------------------------------

UnitPrototypeRecord* Unit::GetPrototype() { return unitdb::PrototypeRecord(prototypeId); }

UnitModelRecord* Unit::GetPrototypeModel() {
    const UnitPrototypeRecord* prototype = unitdb::PrototypeRecord(prototypeId);
    if (!prototype) return nullptr;
    return unitdb::ModelRecord(static_cast<std::uint32_t>(prototype->modelIndex));
}

UnitPrototypeRecord* Unit::GetSecondPrototype() {
    return unitdb::PrototypeRecord(static_cast<std::uint32_t>(secondPrototype));
}

std::uint32_t Unit::SetSecondPrototype(const UnitPrototypeRecord* record) {
    int offset = unitdb::PrototypeByteOffset(record);
    secondPrototype = offset / static_cast<int>(sizeof(UnitPrototypeRecord));
    return offset < 0 ? 1u : 0u;   // the sign of the unadjusted quotient
}

// --- Combat ----------------------------------------------------------------------------------

float Unit::ActionSpeed(const FigureAnimation* entry) {
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

bool Unit::AcceptsAnimation(const FigureAnimation* entry) {
    if (MapType() != MapObjectType::Hero) return true;
    std::uint32_t required = 0;
    if (Controller()->HasOtherArmour()) required = 0x10;
    if (Controller()->HasMainArmour()) required |= 0x20;
    return (entry->flags & required) == required;
}

float Unit::AnimationRatio() {
    float denominator = AnimationFactor(baseScale.z);
    return AnimationFactor(scale.z) / denominator;
}

float UnitParams::Period() { return 25.0f / static_cast<float>(intelligence); }

// --- Map object state ------------------------------------------------------------------------

bool MapObjectServer::IsDead() { return static_cast<std::int16_t>(lib::RoundEven(vitals.hitPoints)) <= 0; }

bool MapObjectServer::IsOverkilled() { return vitals.maxHitPoints * kOverkill > vitals.hitPoints; }

bool MapObjectClientSpecific::IsDead() { return OwnerUnit()->packedVitals.hitPoints <= 0; }

bool MapObjectClientSpecific::IsOverkilled() {
    int maximum = OwnerUnit()->packedVitals.maxHitPoints;
    return OwnerUnit()->packedVitals.hitPoints < -maximum * 4 - maximum;
}

// --- The class name and id, the motion given its speeds, the player test, twice the height above the
// centre and a worn armour piece.

void Unit::SetMotionSpeeds(float speed, float turnRate) {
    PathOrder* path = Controller()->Path();
    path->ForgetObject(id);
    UnitMotion* unitMotion = &motion;
    unitMotion->segment = -1;
    unitMotion->speedScale = speed;
    unitMotion->baseTurnRate = turnRate;
    unitMotion->Build(path);
}

bool Unit::HasPlayer() const { return player != nullptr; }

float Unit::DoubleHeight() const { return bounds.boxMax.z + bounds.boxMax.z; }


ItemArmor* Unit::WornItem(int slot) const { return armor[slot]; }

// --- Hits, forgotten objects and the script name ----------------------------------------------------

void Unit::ReceiveHit(DamageInfo* hit, Unit* attacker, Unit* caster) {
    Controller()->TakeHit(hit, attacker, caster);
}

// Nothing reads the result.
int Unit::ForgetObject(EngineObject* object) {
    Controller()->ObjectDeleted(object);
    return 0;
}

void Unit::SetScriptName(std::string_view newName) { scriptName = newName; }
