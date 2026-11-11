// The map object manager ("map objects", class id 0x2D): its lifetime, the objects it makes complete with
// their halves, the animation database lookup, the object factory, the creature builders (vitals and statistics
// from the prototype records, the prototype applied to a map object) and a new unit's starting equipment. See
// map_object_manager.h and docs/notes/units.md ("Creation, databases, save").
#include "game/objects/map_object_manager.h"

#include "engine/core/base_manager.h"
#include "engine/core/core_maths.h"
#include "engine/models/figure_manager.h"   // FigureObjectType
#include "game/app/singletons.h"
#include "game/app/logic_settings.h"
#include "game/items/item.h"
#include "game/items/item_manager.h"
#include "game/objects/lever.h"
#include "game/objects/map_object.h"
#include "game/objects/object3d.h"
#include "game/objects/torch.h"
#include "game/objects/traps.h"
#include "engine/data/resource_manager.h"
#include "game/units/unit.h"
#include "game/units/unit_create.h"
#include "game/units/unit_db.h"
#include "game/world/world.h"

#include "lib/bytes.h"
#include "lib/crt.h"
#include "lib/maths.h"
#include "lib/rounding.h"
#include "lib/str.h"
#include "lib/text_case.h"
#include "engine/data/resource.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstring>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr const char* kClassName = "CMapObjectManager";
constexpr const char* kNoDatabaseText = "Can't find animation database file for \"%s\".\n";

// The third half an object of type `type` gets (the world's specific manager makes the client's halves: the 3D
// object's, the map object's and the unit's; the server's theirs), or null for none.
std::optional<SideObjectType> ThirdHalfOf(MapObjectType type) {
    if (IsUnitType(type)) return SideObjectType::Unit;
    if (type == MapObjectType::Torch) return SideObjectType::Torch;
    return std::nullopt;
}

} // namespace

// --- Lifetime and class slots ---------------------------------------------------------------------

MapObjectManager::MapObjectManager() {}

MapObjectManager::~MapObjectManager() {}

const char* MapObjectManager::ClassName() { return kClassName; }
ClassId MapObjectManager::GetClassId() { return ClassId::MapObjectManager; }

void MapObjectManager::Create() {
    std::string folder = g_textFolder;
    BaseManager::Create();
    AddArchive((folder + "database.res"));
}

MapObject* MapObjectManager::FindMapObject(unsigned objectId) const { return FindObjectAs<MapObject>(objectId); }

EngineObject* MapObjectManager::CreateObject(int newType, int newId) {
    ResourceManager* manager = this;
    const auto kind = static_cast<MapObjectType>(newType);
    auto* object = static_cast<MapObject*>(BaseManager::CreateObject(newType, newId));
    if (!object) return nullptr;
    FigureManager* figures = manager->context->GetFigureManager();
    object->SetAnimationState(figures->CreateObjectAs<FigureAnimationState>(FigureObjectType::AnimationState, kNoObjectId));
    BaseManager* halves = manager->context->SpecificManager();
    object->AttachObject(halves->CreateObjectAs<EngineObject>(SideObjectType::Object3D, kNoObjectId));
    object->AttachObject(halves->CreateObjectAs<EngineObject>(SideObjectType::MapObject, kNoObjectId));
    if (auto half = ThirdHalfOf(kind)) object->AttachObject(halves->CreateObjectAs<EngineObject>(*half, kNoObjectId));
    return object;
}

void MapObjectManager::Destroy() {
    ResourceManager::Destroy();
    animationDatabases.clear();
}

// --- Animation databases (engine/models/animation_db.cpp reads them) -------------------------------

AnimationDatabase* MapObjectManager::AnimationDatabaseOf(const std::string& figure) {
    std::string file = figure + ".adb";
    if (auto found = animationDatabases.find(file); found != animationDatabases.end()) return found->second.get();
    const RecordFile record = FindRecord(file);
    if (!record.Any()) return nullptr;
    return animationDatabases.emplace(std::move(file), ReadAnimationDatabase(record)).first->second.get();
}

FigureAnimation* MapObjectManager::FindAnimation(const std::string& figure, std::uint32_t key) {
    AnimationDatabase* database = AnimationDatabaseOf(figure);
    if (!database) {
        game::DebugLog(kNoDatabaseText, figure);
        return nullptr;
    }
    for (FigureAnimation& entry : database->animations)
        if (AnimationKeyFits(entry.key, key)) return &entry;
    return nullptr;
}

FigureAnimation* MapObjectManager::FindFigureAnimation(const std::string& figure, std::string_view name) {
    AnimationDatabase* database = AnimationDatabaseOf(figure);
    if (!database) {
        game::DebugLog(kNoDatabaseText, figure);
        return nullptr;
    }
    for (FigureAnimation& entry : database->animations)
        if (text::CompareIgnoringCase(entry.name, name) == 0) return &entry;
    return nullptr;
}

void MapObjectManager::GiveDefaults(MapObject* object) {
    // The vitals and statistics start zeroed, so the fields Reset and SetDefaults leave (the shares and factors,
    // fixedMaxHitPoints, builtFlags, the layers' spare values, ...) are 0, not what the stack held: a determinism
    // fix. The original's were WorldServer::CreateObject's earlier callees' leftovers, heap addresses among them,
    // and gave the main menu's unmoco2 (the one unit added without a prototype) hit points that followed the
    // heap.
    UnitVitals vitals{};
    vitals.Reset();
    if (object->GetClassId() != ClassId::Unit) {
        SetDefaults(&vitals, nullptr);
        object->SetServerData(&vitals);
    } else {
        UnitStats stats{};
        stats.Reset();
        SetDefaults(&vitals, &stats);
        object->SetStats(&stats);
    }
    object->ScaleEffectChanged(false);
    object->prototypeId = 0;
}

// --- Creature builders ---------------------------------------------------------------------------

namespace {

constexpr float kThird = 0.33333334f;
constexpr float kStat25 = 25.0f;
constexpr float kRate15 = 15.0f;
constexpr float kDefaultStat = 50.0f;
constexpr float kDefaultModeSpeed = 0.13333334f;
constexpr float kDefaultSpeed = 0.2f;
constexpr float kDefaultRateBase = 1.0f;
constexpr float kDefaultFullLevel = 100.0f;   // protection, perception, conspicuity and a slot's capacity at their start
constexpr float kDefaultVitalPool = 1000.0f;   // hit points and mana of a unit without stats
constexpr float kDefaultRegen = 10.0f;
constexpr int kSlotWorking = 3;

// A dword copied as its bits.
template <typename To, typename From>
void CopyDword(To& to, const From& from) {
    to = std::bit_cast<To>(from);
}

std::uint8_t RoundToByte(float value) { return static_cast<std::uint8_t>(lib::RoundEven(value)); }

const UnitPrototypeRecord* FindPrototype(std::string_view name) { return g_unitDatabase.prototypes.FindByName(name); }
const UnitNpcRecord* FindNpc(std::string_view name) { return g_unitDatabase.npcs.FindByName(name); }

// The prototype's model.
const UnitModelRecord* ModelOf(const UnitPrototypeRecord* prototype) {
    return unitdb::ModelRecord(static_cast<std::uint32_t>(prototype->modelIndex));
}

// --- The statistics both builders fill the same way ---------------------------------------

// cos(angle / 2) with the angle in degrees.
float HalfAngleCosine(const UnitModelRecord* model) {
    float half = model->angle * 0.5f;
    return lib::Cos(half * ei::kRadiansPerDegree);
}

float RightAngleCosine() { return lib::Cos(ei::kRadiansPerDegree * 90.0f); }

void SetAttributesAndSkills(UnitStats* stats, const UnitPrototypeRecord* prototype, const UnitNpcRecord* npc) {
    if (npc) {
        static_assert(offsetof(UnitNpcRecord, skills) - offsetof(UnitNpcRecord, attributes) == 3);
        std::memcpy(stats->attributes, npc->attributes, 3 + 8);   // the attributes and the skills
        return;
    }
    for (int i = 0; i < 3; ++i) stats->attributes[i] = RoundToByte(prototype->skillLevel);
    for (int i = 0; i < 8; ++i) stats->skills[i] = RoundToByte(prototype->skillLevel);
    stats->attributes[0] = RoundToByte(prototype->attributes01);
    stats->attributes[1] = RoundToByte(prototype->attributes01);
    stats->attributes[2] = RoundToByte(prototype->attribute2);
    // Truncated as _ftol does (the low half of the 64-bit integer; 0 for a NaN).
    for (int i = 0; i < 3; ++i)
        stats->skills[3 + i] = static_cast<std::uint8_t>(static_cast<std::int64_t>(prototype->skills3to5[i]));
}

// The body slots from the model: a slot with a share is working, with its type and vital mask
// from its level record and no layers; the others unused.
void SetSlots(UnitStats* stats, const UnitModelRecord* model) {
    for (int i = 0; i < 6; ++i) {
        const UnitModelSlot& entry = model->slots[i];
        UnitStatSlot& slot = stats->slots[i];
        float share = entry.share;
        if (share == 0.0f) {
            slot.kind = 0;
            continue;
        }
        const UnitLevelRecord* level = unitdb::ModelLevelAt(static_cast<std::uint32_t>(entry.levelIndex));
        slot.type = level->slotType;
        CopyDword(slot.share, entry.share);
        CopyDword(slot.factor, entry.factor);
        slot.vitalMask = level->vitalMask;
        slot.layers[0].layerMask = 0;
        slot.layers[0].state = 0;
        slot.layers[1].state = 0;
        slot.layers[2].state = 0;
        slot.kind = kSlotWorking;
    }
}

void SetCombat(UnitStats* stats, const UnitPrototypeRecord* prototype, const UnitModelRecord* model) {
    for (int i = 0; i < 5; ++i) CopyDword(stats->combat[i], prototype->combat[i]);
    CopyDword(stats->damage, prototype->damage);
    CopyDword(stats->damageSpread, prototype->damageSpread);
    static_assert(sizeof(stats->damageWeights) == sizeof(model->damageWeights));
    std::memcpy(stats->damageWeights, model->damageWeights, sizeof(stats->damageWeights));
    CopyDword(stats->naturalWeapon, prototype->naturalWeapon);
}

void LearnNpcSkills(UnitStats* stats, const UnitNpcRecord* npc) {
    stats->spentExperience = 0.0f;
    if (!npc) return;
    stats->AddScaledExperience(npc->experience);
    for (const auto& name : npc->skillNames) {
        ::SkillLevelRecord* skill = g_skillDatabase.FindLevel(name);
        if (skill) stats->LearnSkill(&g_skillDatabase, skill);
    }
    stats->spentExperience = stats->experience;
}

struct FactoryEntry {
    MapObjectType type = {};
    EngineObject* (*create)() = nullptr;
};
// The class's entry: its id and a new object of it.
template <class T>
constexpr FactoryEntry Make(MapObjectType type) {
    return {type, [] () -> EngineObject* { return new T(); }};
}
constexpr FactoryEntry kFactory[] = {
    Make<Unit>(MapObjectType::Hero),
    Make<Unit>(MapObjectType::Creature),
    Make<Unit>(MapObjectType::UnitSpare),
    Make<CTree>(MapObjectType::Tree),
    Make<CBuilding>(MapObjectType::Building),
    Make<CStone>(MapObjectType::Stone),
    Make<CStuffObject>(MapObjectType::StuffObject),
    Make<TorchObject>(MapObjectType::Torch),
    Make<MagicTrap>(MapObjectType::MagicTrap),
    Make<Lever>(MapObjectType::Lever),
    Make<FigureAnimationState>(MapObjectType::AnimationState),
    Make<TrapObject>(MapObjectType::Trap),
};

std::uint32_t Count(const std::vector<std::string>& names) { return static_cast<std::uint32_t>(names.size()); }

// A weapon name other than "none" (_mbscmp) and not empty.
bool Named(const std::string& name) {
    return str::Compare(name,
                           "none") != 0 &&
           static_cast<int>(name.size()) != 0;
}

// The weapon named `name`; a warning when it cannot be made (null is returned then).
Item* MakeWeapon(ItemManager* items, const std::string& name, const std::string& prototype) {
    Item* weapon = items->CreateItem(name);
    if (!weapon) game::Warning("Can't create weapon \"%s\" for unit \"%s\"", name, prototype);
    return weapon;
}

} // namespace

// --- The factory ----------------------------------------------------------------------------------

EngineObject* MapObjectManager::NewObject(int objectType) {
    for (const FactoryEntry& entry : kFactory) {
        if (entry.type != static_cast<MapObjectType>(objectType)) continue;
        return entry.create();
    }
    return nullptr;
}

// --- Defaults and builders ------------------------------------------------------------------------

void MapObjectManager::SetDefaults(UnitVitals* vitals, UnitStats* stats) {
    if (!stats) {
        vitals->hitPoints = kDefaultVitalPool;
        vitals->maxHitPoints = kDefaultVitalPool;
        vitals->hitPointRegen = kDefaultRegen;
        vitals->mana = kDefaultVitalPool;
        vitals->maxMana = kDefaultVitalPool;
        vitals->manaRegen = kDefaultRegen;
        for (UnitStatPair& protection : vitals->protection) {
            protection.current = kDefaultFullLevel;
            protection.base = kDefaultFullLevel;
        }
        return;
    }
    stats->experience = 0.0f;
    stats->strengthBase = kDefaultStat;
    stats->dexterityBase = kDefaultStat;
    stats->intelligenceBase = kDefaultStat;
    for (float& modeSpeed : stats->modeSpeeds) modeSpeed = kDefaultModeSpeed;
    stats->spentExperience = 0.0f;
    stats->speedBase = kDefaultSpeed;
    stats->rateBase = kDefaultRateBase;
    stats->capacity = 0.0f;
    stats->movementKind = 0;
    for (int i = 0; i < 6; ++i) {
        stats->perception[i].base = kDefaultFullLevel;
        stats->perception[i].current = kDefaultFullLevel;
        stats->conspicuity[i].base = kDefaultFullLevel;
        stats->conspicuity[i].current = kDefaultFullLevel;
    }
    for (UnitStatSlot& slot : stats->slots) {
        slot.type = 1;
        slot.vitalMask = 0;
        slot.kind = kSlotWorking;
        slot.current = kDefaultFullLevel;
        slot.maximum = kDefaultFullLevel;
        for (UnitStatLayer& layer : slot.layers) {
            layer.layerMask = 0;
            layer.state = 0;
            layer.reserved[0] = 0;
            layer.reserved[1] = 0;
            std::memset(layer.absorb, 0, sizeof(layer.absorb));
        }
    }
}

int MapObjectManager::BuildFromPrototype(UnitVitals* vitals, UnitStats* stats, std::string_view name) {
    const UnitPrototypeRecord* prototype = FindPrototype(name);
    if (!prototype) {
        SetDefaults(vitals, stats);
        return 1;
    }
    const UnitModelRecord* model = ModelOf(prototype);
    const UnitNpcRecord* npc = FindNpc(name);
    if (!vitals || !stats) return 0;
    // The vitals start zeroed: the fields their reset and this builder leave are 0, not what the caller's stack
    // held (a determinism fix; the harness zeroes them in the original too). The statistics' reset zeroes all.
    std::memset(vitals, 0, sizeof(UnitVitals));
    vitals->Reset();
    stats->Reset();

    vitals->maxHitPoints = prototype->hitPoints;
    vitals->hitPoints = prototype->hitPoints;
    CopyDword(vitals->hitPointRegen, model->hitPointRegen);
    vitals->maxMana = prototype->mana;
    vitals->mana = prototype->mana;
    CopyDword(vitals->manaRegen, model->manaRegen);
    stats->fixedMaxHitPoints = vitals->maxHitPoints;

    if (npc) {
        stats->strengthBase = npc->stats[0];
        stats->strength = npc->stats[0];
        stats->dexterityBase = npc->stats[1];
        stats->dexterity = npc->stats[1];
        stats->intelligenceBase = npc->stats[2];
        stats->intelligence = npc->stats[2];
        float rate = stats->dexterity * 0.5f;
        CopyDword(stats->speedBase, prototype->speed);
        stats->rateBase = rate;
    } else {
        float strength = prototype->hitPoints * kThird;
        stats->strengthBase = strength;
        stats->strength = strength;
        float dexterity = prototype->mana * kThird;
        stats->dexterityBase = dexterity;
        stats->dexterity = dexterity;
        stats->intelligenceBase = kStat25;
        stats->intelligence = kStat25;
        stats->rateBase = kRate15;
        CopyDword(stats->speedBase, prototype->speed);
    }
    CopyDword(stats->attackCategory, prototype->attackCategory);
    std::memcpy(stats->modeSpeeds, model->modeSpeeds, sizeof(stats->modeSpeeds));
    for (int i = 0; i < 6; ++i) {
        CopyDword(stats->perception[i].base, prototype->perception[i]);
        CopyDword(stats->conspicuity[i].base, prototype->conspicuity[i]);
    }
    stats->sightCone = HalfAngleCosine(model);
    CopyDword(stats->peripheralRange, prototype->peripheralRange);
    stats->sideCone = RightAngleCosine();
    CopyDword(stats->movementKind, model->movementKind);
    SetAttributesAndSkills(stats, prototype, npc);
    CopyDword(stats->reachHeightScale, model->reachHeightScale);
    stats->sizeClass = static_cast<std::int16_t>(model->sizeClass);
    auto smallClass = static_cast<std::int16_t>(model->sizeClassSmall);
    stats->sizeClass20 = smallClass;
    stats->sizeClass60 = smallClass;

    for (int i = 0; i < 7; ++i) {
        float value = model->protection[i] * prototype->protectionScale;
        vitals->protection[i].base = value;
        vitals->protection[i].current = value;
    }
    SetSlots(stats, model);
    SetCombat(stats, prototype, model);
    LearnNpcSkills(stats, npc);
    return 1;
}

void BuildNpcStats(UnitStats* stats, const std::string& name) {
    const UnitPrototypeRecord* prototype = FindPrototype(name);
    const UnitNpcRecord* npc = FindNpc(name);
    if (!prototype) game::FatalExit("Can't find prototype \"%s\" for new HERO.\n", name);
    const UnitModelRecord* model = ModelOf(prototype);
    if (!npc) game::DebugLog("Can't find NPC prototype \"%s\" for new HERO.\n", name);
    stats->Reset();

    const float* speed = &prototype->speed;
    if (npc) {
        stats->strengthBase = npc->stats[0];
        stats->strength = npc->stats[0];
        stats->dexterityBase = npc->stats[1];
        stats->dexterity = npc->stats[1];
        stats->intelligenceBase = npc->stats[2];
        stats->intelligence = npc->stats[2];
        stats->speedBase = *speed;
        stats->speed = *speed;
        float rate = stats->dexterity * 0.5f;
        stats->rateBase = rate;
        stats->rate = rate;
    } else {
        float strength = prototype->hitPoints * kThird;
        stats->strengthBase = strength;
        stats->strength = strength;
        float dexterity = prototype->mana * kThird;
        stats->dexterityBase = dexterity;
        stats->dexterity = dexterity;
        stats->intelligenceBase = kStat25;
        stats->intelligence = kStat25;
        stats->speedBase = *speed;
        stats->speed = *speed;
        stats->rateBase = kRate15;
        stats->rate = kRate15;
    }
    CopyDword(stats->attackCategory, prototype->attackCategory);
    std::memcpy(stats->modeSpeeds, model->modeSpeeds, sizeof(stats->modeSpeeds));
    for (int i = 0; i < 6; ++i) {
        stats->perception[i].base = prototype->perception[i];
        stats->perception[i].current = prototype->perception[i];
        stats->conspicuity[i].base = prototype->conspicuity[i];
        stats->conspicuity[i].current = prototype->conspicuity[i];
    }
    stats->sightCone = HalfAngleCosine(model);
    CopyDword(stats->peripheralRange, prototype->peripheralRange);
    stats->sideCone = RightAngleCosine();
    CopyDword(stats->movementKind, model->movementKind);
    SetAttributesAndSkills(stats, prototype, npc);
    CopyDword(stats->reachHeightScale, model->reachHeightScale);
    stats->sizeClass = static_cast<std::int16_t>(model->sizeClass);
    auto smallClass = static_cast<std::int16_t>(model->sizeClassSmall);
    stats->sizeClass20 = smallClass;
    stats->sizeClass60 = smallClass;
    SetSlots(stats, model);
    SetCombat(stats, prototype, model);
    LearnNpcSkills(stats, npc);
    stats->ResetToBase(1.0f);
    stats->RefillSlots();
    stats->ResetToBase(1.0f);
}

MapObjectType PrototypeObjectClass(std::string_view name) {
    constexpr MapObjectType kDefaultClass = MapObjectType::Creature;
    const UnitPrototypeRecord* prototype = FindPrototype(name);
    if (!prototype) return kDefaultClass;
    const UnitModelRecord* model = ModelOf(prototype);
    if (!model) return kDefaultClass;
    return static_cast<MapObjectType>(model->objectClass);
}

// --- The prototype applied to a map object --------------------------------------------------------

void MapObjectManager::ApplyPrototype(MapObject* object, const std::string& name) {
    UnitVitals vitals{};   // zeroed (the stack's old contents are not kept)
    UnitStats stats{};
    const UnitPrototypeRecord* prototype = FindPrototype(name);
    if (!prototype) {
        vitals.Reset();
        if (object->GetClassId() != ClassId::Unit) {
            SetDefaults(&vitals, nullptr);
            object->SetServerData(&vitals);
        } else {
            stats.Reset();
            SetDefaults(&vitals, &stats);
            object->SetStats(&stats);
        }
        object->ScaleEffectChanged(false);
        object->prototypeId = 0;
        game::DebugLog("Can't find prototype \"%s\" for object %d.\n", name, object->id);
        return;
    }
    const UnitModelRecord* model = ModelOf(prototype);
    if (object->GetClassId() == ClassId::Unit) {
        BuildFromPrototype(&vitals, &stats, name);
        stats.builtFlags |= 1;
        vitals.builtFlags |= 1;
        object->MapObject::SetServerData(&vitals);
        object->SetStats(&stats);
        float lift = -model->height;

        Object3D* figure = object;
        figure->defaultScale.x = 0.0f;
        figure->defaultScale.y = lift;
        figure->defaultScale.z = 0.0f;
        Vector3 scale;
        static_assert(sizeof(scale) == sizeof(prototype->scale));
        scale = prototype->scale;
        object->SetFigureScale(scale);
        object->baseScale = scale;
        object->ApplyBaseScale();
        UnitOrNull(object)->SetSecondPrototype(prototype);
    }
    UnitPrototypeRecord* record = g_unitDatabase.prototypes.FindByName(name);
    object->prototypeId = record ? static_cast<std::uint32_t>(record - g_unitDatabase.prototypes.records.data()) : 0xFFFFFFFFu;
}

// --- Starting equipment ---------------------------------------------------------------------------

// A new unit's starting equipment from its prototype (the world adds zone units with it after
// ApplyPrototype). See objects/map_object_manager.h and docs/notes/units.md ("Creation helpers").

int MapObjectManager::GiveStartingEquipment(Unit* unit, const std::string& name) {
    SpellManager* spellOwner = context->GetSpellManager();   // passed on to the spell factory
    ItemManager* items = context->ItemFactory();
    auto* prototype = g_unitDatabase.prototypes.FindByName(name);
    if (!prototype) {   // a map's unit of a prototype the units database lacks (Lost in Astral's gz8g, gz17g)
        game::Warning("Can't find prototype \"%s\" for unit %d: no starting equipment", name, unit->id);
        return 0;
    }
    if (unit->MapType() == MapObjectType::Hero) {
        // The loop bounds are read again on every pass.
        for (std::uint32_t i = 0; i < Count(prototype->armourNames); ++i) {
            Item* armour = items->CreateItem(prototype->armourNames.begin()[i]);
            if (armour) unit->EquipArmour(armour);
            else game::Warning("Can't create armor \"%s\" for unit \"%s\"", prototype->armourNames.begin()[i], name);
        }
        if (Named(prototype->weaponName)) {
            int slot = unit->AddWeapon(MakeWeapon(items, prototype->weaponName, name));
            if (slot != -1) unit->pendingWeapon = slot;
        }
        if (Named(prototype->secondWeaponName))
            unit->AddWeapon(MakeWeapon(items, prototype->secondWeaponName, name));
    }
    for (std::uint32_t i = 0; i < Count(prototype->spellNames); ++i) {
        Item* spell = items->CreateSpellItem(prototype->spellNames.begin()[i], spellOwner);
        if (spell) unit->AddSpell(spell);
        else game::Warning("Can't create spell \"%s\" for unit \"%s\"", prototype->spellNames.begin()[i], name);
    }
    return 1;
}

// --- References by id -----------------------------------------------------------------------------

template <typename T>
void LoadRef(Ref<T>& ref, ByteStream* stream, World* world) {
    const int id = stream->ReadId();
    if (id == kNoObjectId) {
        ref.Reset();
        return;
    }
    auto* manager = world->GetMapObjectManager();
    ref.Assign(manager->FindObjectAs<T>(static_cast<unsigned>(id)));
}

// The references loaded: the AI's map objects and units, the effects' figures.
template void LoadRef(Ref<MapObject>&, ByteStream*, World*);
template void LoadRef(Ref<Unit>&, ByteStream*, World*);
