#include "game/units/unit.h"

#include "game/ai/ai_map.h"   // AIPath
#include "game/ai/ai_motion.h"   // UnitMotion
#include "game/app/logic_settings.h"
#include "game/items/item.h"
#include "game/objects/object3d.h"
#include "game/players/party.h"
#include "game/players/player.h"
#include "game/spells/spell.h"
#include "game/units/unit_client_models.h"
#include "game/units/unit_db.h"
#include "game/world/world.h"

#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/rounding.h"
#include "lib/scales.h"
#include "lib/str.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace {
constexpr float kSpeedByteScale = 0.039215688f;  // (1 / 25.5)
constexpr float kPatienceScale = 3.0f;
constexpr float kPatienceBase = 10.0f;
constexpr float kNoPlayerValue = 1000000.0f;
constexpr float kFastSpeedScale = 1.8f;
constexpr float kWalkSpeedScale = 0.7f;
constexpr float kSlowModeScale = 0.7f;
constexpr float kIdleScale = 0.2f;
constexpr float kOverkill = -5.0f;

float SpellCost(const Spell* spell) { return spell->manaCost; }

// The party of the unit's player.
Party* RecordsOf(Player* player) { return player->GetParty(); }

// The units of a player's party have a client-specific controller; their items live in the player's inventory
// records.
bool IsPartyUnit(Unit* unit) { return unit->controller->GetClassId() == ClassId::UnitClientSpecific; }

// Puts `item` into the first free slot of `slots`, taking a reference: the slot that holds it (the one it was
// in already, if any), -1 when it is null or every slot is taken.
int AddToSlots(Unit* unit, Item** slots, std::uint32_t count, Item* item) {
    if (!item) return -1;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (slots[i] == item) return static_cast<int>(i);
        if (!slots[i]) {
            slots[i] = item;
            item->AddRef();
            unit->MarkChanged(NetField::Equipment);
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
    unit->MarkChanged(NetField::Equipment);
}

// Speed byte of movement mode `mode` times the base speed; the base speed for others.
float ModeSpeedOf(const Unit* unit, MoveMode mode) {
    float base = static_cast<float>(unit->params.baseSpeed) * kSpeedByteScale;
    std::uint8_t speed;
    switch (mode) {
    case MoveMode::Lie: speed = unit->params.modeSpeeds[3]; break;
    case MoveMode::Crawl: speed = unit->params.modeSpeeds[2]; break;
    case MoveMode::Walk:
    case MoveMode::Stop: speed = unit->params.modeSpeeds[1]; break;
    case MoveMode::Run: speed = unit->params.modeSpeeds[0]; break;
    default: return base;
    }
    return base * (static_cast<float>(speed) * 2.0f) * lib::kByteScale;
}

constexpr std::uint8_t kFlies = 2;              // UnitParams::movementKind

// Animation keys (docs/notes/combat.md): the low 15 bits are variant bits, the rest the kind;
// kAnimationMoveMask are the movement bits (AnimationMoveBits).
constexpr std::uint32_t kKindBits = 0xFFFF8000;
constexpr std::uint32_t kEntryIndex = 0xFF;

const char* const kNoAnimationEntry = "Can't find animation entry index\n %i\n for \"%s\".\n";
const char* const kBodyPart = "bd";
const char* const kChestPart = "chest";
const char* const kMosquito = "unmosp";
const char* const kRightHand = "rh3";

constexpr float kNoExtent = 0.0f;
constexpr float kExtentScale = 1.0f;
constexpr float kMosquitoExtentScale = 2.0f;

} // namespace

// --- Sides -----------------------------------------------------------------------------------

void Unit::AddHostileMask(std::uint32_t mask) {
    std::uint32_t merged = relationsA | mask;
    if (relationsA == merged) return;
    relationsA = merged;
    flags |= MapObjectFlag::RelationsChanged;
    MarkChanged(NetField::Relations);
}

void Unit::SetRelationsB(std::uint32_t mask) {
    relationsB = mask;
    MarkChanged(NetField::Relations);
}

void Unit::SetSide(int value) {
    side = value;
    flags |= MapObjectFlag::RelationsChanged;
    if (controller) MarkChanged(NetField::Relations);
}

bool Unit::IsHostileTo(const Unit* other) {
    return ((1u << (other->side & 31)) & relationsB & relationsA) != 0;
}

// --- Simple state setters --------------------------------------------------------------------

void Unit::SetFightAtWill(int value) {
    fightAtWill = value;
    MarkChanged(NetField::FightAtWill);
}

void Unit::SetAnimationEntries(std::int8_t entry, std::int8_t second, int start, std::int8_t sound) {
    animationEntry = entry;
    secondEntry = second;
    actionSound = sound;
    animationStart = start;
    MarkChanged(NetField::Animation);
}

void Unit::SetMotionMode(int value) {
    if (motionMode == value) return;
    motionMode = value;
    MarkChanged(NetField::Motion);
}

void Unit::SetPathTimeShift(int value) {
    if (pathTimeShift == value) return;
    pathTimeShift = value;
    MarkChanged(NetField::Motion);
}

void Unit::SetMoveModes(MoveMode mode, MoveMode mode2) {
    moveMode = mode;
    moveMode2 = mode2;
    MarkChanged(NetField::Modes);
}

void Unit::ShowHelmet(bool show) {
    if (show) {
        flags &= ~MapObjectFlag::HelmetHidden;
    } else {
        flags |= MapObjectFlag::HelmetHidden;
    }
    MarkChanged(NetField::Equipment);
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

std::array<std::uint32_t, 11> Unit::GetEquipmentIds() {
    std::array<std::uint32_t, 11> out;
    for (int i = 0; i < 4; ++i) {
        out[i] = weapons[i] ? weapons[i]->prototypeId : 0xFFFFFFFFu;
    }
    for (int i = 0; i < 7; ++i) {
        out[4 + i] = armor[i] ? armor[i]->prototypeId : 0xFFFFFFFFu;
    }
    return out;
}

int Unit::AddWeapon(Item* item) { return AddToSlots(this, weapons, 4, item); }

int Unit::AddQuickItem(Item* item) { return AddToSlots(this, quickItems, 4, item); }

int Unit::AddSpell(Item* item) { return AddToSlots(this, spells, 8, item); }

void Unit::RemoveWeapon(std::uint32_t slot) { RemoveFromSlots(this, weapons, slot); }

void Unit::RemoveQuickItem(std::uint32_t slot) { RemoveFromSlots(this, quickItems, slot); }

ItemWeapon* Unit::GetWeapon(std::uint32_t slot) {
    if (IsPartyUnit(this)) {
        if (!player) return nullptr;
        PartyUnitRecord* record = RecordsOf(player)->Find(id);
        if (!record || slot >= static_cast<std::uint32_t>(std::ssize(record->weapons))) return nullptr;
        Item* item = record->weapons[slot];
        return item ? item->AsWeapon() : nullptr;
    }
    return slot < 4 && weapons[slot] ? weapons[slot]->AsWeapon() : nullptr;
}

Item* Unit::GetQuickItem(std::uint32_t slot) {
    Item* item = nullptr;
    if (IsPartyUnit(this)) {
        if (!player) return nullptr;
        PartyUnitRecord* record = RecordsOf(player)->Find(id);
        if (!record || slot >= static_cast<std::uint32_t>(std::ssize(record->quickItems))) return nullptr;
        item = record->quickItems[slot];
    } else if (slot < 4) {
        item = quickItems[slot];
    }
    // An item with nothing left is not there, for a party unit and any other.
    if (!item || static_cast<int>(item->count) < 1) return nullptr;
    return item;
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
        if (item) total += item->weight;
    }
    for (Item* item : quickItems) {
        if (item) total += item->weight;
    }
    for (Item* item : spells) {
        if (item) total += item->weight;
    }
    for (Item* item : armor) {
        if (item) total += item->weight;
    }
    return total;
}

bool Unit::CanCastSpell(std::uint32_t slot) {
    if (slot >= 8) return false;
    // A client-side test: the client knows its units' effect kinds but not their stat totals (NoCasting).
    if (HasEffectKind([](const itemdb::SpellRecord& kind) { return kind.greysSpells; })) return false;
    Spell* spell = GetSpellObject(slot);
    if (!spell) return false;
    float cost = SpellCost(spell);
    float available = static_cast<float>(packedVitals.mana);
    float needed = params.Period() * cost;
    return !(available < needed);   // a NaN cost can cast
}

bool Unit::CanUseQuickItem(std::uint32_t slot) {
    Item* object = GetQuickItem(slot);
    if (!object || object->GetClassId() != ClassId::ItemQuick) return false;
    Item* item = object;
    if (item->GetCategory() != QuickItemCategory::Wand && item->GetCategory() != QuickItemCategory::Potion) return false;
    if (item->GetCategory() == QuickItemCategory::Wand) {
        if (item->charge < SpellCost(item->spell)) return false;
    }
    return true;
}

// --- Movement --------------------------------------------------------------------------------

Unit::MoveSpeeds Unit::GetMoveSpeeds() {
    float base = static_cast<float>(params.baseSpeed) * kSpeedByteScale;
    MoveSpeeds speeds{base, base};
    std::uint8_t speed;
    switch (moveMode) {
    case MoveMode::Lie: speed = params.modeSpeeds[3]; break;
    case MoveMode::Crawl: speed = params.modeSpeeds[2]; break;
    case MoveMode::Run: speed = params.modeSpeeds[0]; break;
    case MoveMode::Walk:
    case MoveMode::Stop:
        speeds.turn = static_cast<float>(params.modeSpeeds[0]) * speeds.forward * 2.0f * lib::kByteScale;
        speeds.forward = static_cast<float>(params.modeSpeeds[1]) * speeds.forward * 2.0f * lib::kByteScale;
        speeds.turn = g_logicSettings.rotateSpeedMult * speeds.turn;
        return speeds;
    default:
        speeds.turn = g_logicSettings.rotateSpeedMult * speeds.turn;
        return speeds;
    }
    float scaled = static_cast<float>(speed) * speeds.forward * 2.0f * lib::kByteScale;
    speeds.forward = scaled;
    speeds.turn = scaled;
    speeds.turn = g_logicSettings.rotateSpeedMult * speeds.turn;
    return speeds;
}

float Unit::MoveSpeed2() { return ModeSpeedOf(this, moveMode2); }

float Unit::MoveSpeedFor(MoveMode mode) { return ModeSpeedOf(this, mode); }

float Unit::ScaledValue(float value) {
    if (!player) return value * kPatienceScale + kPatienceBase;
    if (flags & MapObjectFlag::ChoosingTarget) {
        float capped = MinKeepA(value, kPatienceBase);   // NaN is kept
        return capped * kPatienceScale + kPatienceBase;
    }
    return kNoPlayerValue;
}

float Unit::BaseSpeed() {
    if (lib::Has(bounds.flags, BoundsFlag::Box)) return bounds.boxMax.z * kFastSpeedScale;
    return (bounds.radius * kWalkSpeedScale) + bounds.center.z;
}

float Unit::ModeSpeed() {
    float speed = BaseSpeed();
    if (moveMode != MoveMode::Lie && IsDead() == 0) {
        if (moveMode == MoveMode::Crawl) return speed * kSlowModeScale;
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
    if (controller->HasOtherArmour()) required = lib::Bits(AnimationFlag::WithUnderArmour);
    if (controller->HasMainArmour()) required |= AnimationFlag::WithOuterArmour;
    return (entry->flags & required) == required;
}

float Unit::AnimationRatio() {
    float denominator = AnimationFactor(baseScale.z);
    return AnimationFactor(scale.z) / denominator;
}

// --- Map object state ------------------------------------------------------------------------

bool MapObjectServer::IsDead() { return static_cast<std::int16_t>(lib::RoundEven(vitals.hitPoints)) <= 0; }

bool MapObjectServer::IsOverkilled() { return vitals.maxHitPoints * kOverkill > vitals.hitPoints; }

bool MapObjectClientSpecific::IsDead() { return owner->packedVitals.hitPoints <= 0; }

bool MapObjectClientSpecific::IsOverkilled() {
    int maximum = owner->packedVitals.maxHitPoints;
    return owner->packedVitals.hitPoints < -maximum * 4 - maximum;
}

// --- The class name and id, the motion given its speeds, the player test, twice the height above the
// centre and a worn armour piece.

void Unit::SetMotionSpeeds(float speed, float turnRate) {
    PathOrder* path = controller->Path();
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
    controller->TakeHit(hit, attacker, caster);
}

// Nothing reads the result.
int Unit::ForgetObject(EngineObject* object) {
    controller->ObjectDeleted(object);
    return 0;
}

void Unit::SetScriptName(std::string_view newName) { scriptName = newName; }

// --- Figure queries ----------------------------------------------------------------------------

// Unit queries on the figure: animation entries for a key, the body's extent, the height of the ground under
// a point and the point effects start from. See unit.h and docs/notes/units.md ("Creation, databases, save").

FigureAnimation* Unit::FindAnimation(std::uint32_t key, bool anyOfKind, AnimationRandom random) {
    if (!IsUnitType(MapType())) return nullptr;
    if (FigureAnimation* entry = RandomAnimation(key, random)) return entry;
    std::uint32_t kind = key & kKindBits;
    if (FigureAnimation* entry = RandomAnimation(kind, random)) return entry;
    if ((kind & kAnimationMoveMask) == AnimationKeyOf(AnimationMoveBits::Stop)) {
        if (FigureAnimation* entry = RandomAnimation(kind & ~kAnimationMoveMask, random)) return entry;
        if (FigureAnimation* entry = RandomAnimation((kind & ~(AnimationKeyOf(AnimationMoveBits::Crawl) | AnimationKeyOf(AnimationMoveBits::Stop))) | AnimationKeyOf(AnimationMoveBits::Walk), random)) return entry;
    }
    if (!anyOfKind) return nullptr;
    return RandomAnimationOfKind(kind, random);
}

FigureAnimation* Unit::AnimationEntry(std::uint32_t key) {
    FigureAnimation* entry = AnimationAt(key);
    if (!entry) game::DebugLog(kNoAnimationEntry, static_cast<int>(key & kEntryIndex), name);
    return entry;
}

float Unit::BodyExtent() {
    int part = FindPartIndex(kBodyPart);
    if (part < 0) {
        part = FindPartIndex(kChestPart);
        if (part < 0) return kNoExtent;
    }
    float extentScale = kExtentScale;
    if (MapType() == MapObjectType::Creature && str::Compare(name, kMosquito) == 0) extentScale = kMosquitoExtentScale;
    return extentScale * Length2D(XY(partBoxes[part].bounds.boxMax));
}

float Unit::GroundHeight(float x, float y) {
    auto* world = OwningWorld();
    if (params.movementKind != kFlies) return world->GroundHeightAt(x, y);
    if (controller->GetClassId() == ClassId::UnitServer) return world->SurfaceHeightAt(x, y) + localPosition.z;
    return world->SurfaceHeightAt(x, y) + heightOffset;
}

Vector3 Unit::EffectPoint() {
    // The game leaves this uninitialised on the server and for a hero without a right hand, and turns
    // whatever the stack holds; zero gives the position there (docs/notes/units.md).
    Vector3 local = {0.0f, 0.0f, 0.0f};
    auto* world = OwningWorld();
    if (!world->IsServer()) {
        if (MapType() == MapObjectType::Hero) {
            if (FigureMesh* hand = FindPartByPrefix(kRightHand)) {
                Vector3 at;
                at = PartWorldPosition(hand);
                return at;
            }
        } else if (std::optional<Vector3> weapon = ClientController()->WeaponPoint()) {
            local = *weapon;
        } else {
            return localPosition + bounds.center;
        }
    }
    Vector3 turned = localRotation.Rotate(local);
    return turned + localPosition;
}
