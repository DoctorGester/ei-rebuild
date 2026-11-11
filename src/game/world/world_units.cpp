// WorldServer::CreateUnits: creates units from unit records (script AddUnitToServer,
// the party entering a zone, mercenaries), places them, gives them their equipment and AI, and
// adds the heroes to the "Heroes" script group. See world.h and docs/notes/world.md.
#include "game/ai/ai_map.h"
#include "engine/core/base_manager.h"
#include "game/map/chunk_stream.h"
#include "game/combat/combat.h"
#include "engine/core/core_maths.h"
#include "engine/core/core_refcount.h"
#include "engine/core/core_streams.h"
#include "engine/core/program_window.h"
#include "game/items/item.h"
#include "game/items/item_manager.h"
#include "game/objects/lever.h"
#include "engine/scene/light.h"
#include "game/objects/traps.h"
#include "game/objects/map_object.h"
#include "game/objects/object3d.h"
#include "game/map/map_spec.h"
#include "game/world/diplomacy.h"
#include "game/players/player_messages.h"
#include "game/players/player.h"
#include "game/ai/sector_map.h"
#include "game/spells/spell.h"
#include "game/objects/torch.h"
#include "game/units/unit.h"
#include "game/ai/unit_ai.h"
#include "game/units/unit_create.h"
#include "game/objects/map_object_manager.h"
#include "game/units/unit_db.h"
#include "game/units/unit_record.h"
#include "game/world/world.h"
#include "game/effects/world_effect.h"

#include "lib/bytes.h"
#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/maths.h"
#include "lib/str.h"
#include "lib/text_case.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace {

constexpr float kAxisEpsilon = 1e-8f;
constexpr float kLineAngleOffset = 3.1415927f;
constexpr float kLineSpacing = 1.2f;
constexpr float kHeroStartX = 54.0f;        // the hero's start on zone1
constexpr float kHeroStartY = 138.0f;
constexpr float kHeroStartRadius2 = 300.0f;
constexpr float kCampfireHeight = 0.7f;
constexpr float kTorchLightScale = 10.0f;  // the flame's radius factor (10)

using mapspec::ObjectSpec;
using mapspec::Spec;
using mapspec::SpecKind;
using mapspec::UnitSpec;


// The hero's figure parts, "hr" replaced by its numbered style (none when the style is -1).
void FillHeroParts(UnitSpec* message, const std::string& prototypeName) {
    UnitPrototypeRecord* prototype = g_unitDatabase.prototypes.FindByName(prototypeName);
    int hairStyle = prototype ? prototype->hair : 1;

    std::vector<std::string>& parts = message->parts;
    for (int n = 0; n < kHeroPartCount; ++n) {
        const char* part = kHeroFigureParts[n];
        if (std::strcmp(part, "hr") == 0) {
            if (hairStyle != -1) {
                parts.push_back(str::Format("hr.%.2d", hairStyle));
            }
        } else {
            parts.push_back(part);
        }
    }
}

// The place of the `index`th unit when no placement is given: a line from `position` along the
// rotation's heading.
void PlaceInLine(UnitSpec* message, const Vector3& position, const Quaternion& rotation, int index) {
    float axis = LengthSquared(Vector3{rotation.x, rotation.y, rotation.z});
    float angle;
    if (axis <= kAxisEpsilon) {
        angle = 0.0f;
    } else {
        float arc = lib::Acos(rotation.w);
        angle = arc + arc;
        float sign = 1.0f / std::sqrt(axis) * rotation.z;
        if (sign < 0.0f) angle = -angle;
    }
    angle += kLineAngleOffset;

    Spec& spec = *message;
    spec.position = position;
    spec.rotation = rotation;
    float cosine = lib::Cos(angle);
    float sine = lib::Sin(angle);
    float step = static_cast<float>(index);
    spec.position += Vector3{step * cosine * kLineSpacing, sine * step * kLineSpacing, 0.0f};
}

// An item from an item record, with its counted record holding the item record's id.
Item* MakeItem(WorldServer* world, Item* itemRecord) {
    Item* item = world->DuplicateItem(itemRecord);
    auto* record = new ItemLink();
    record->value = static_cast<std::uint32_t>(itemRecord->id);
    item->serverLink.Assign(record);
    return item;
}

constexpr std::uint32_t kQuickEffectFlags = 0x3010;
constexpr std::uint32_t kFlagsNewEffect = 0x180000;

// The timed objects' types (world_effect.h).
constexpr int kTimedAmbientSound = 0x49, kTimedSound = 0x48, kTimedLight = 0x4B, kTimedParticle = 0x4A;

} // namespace

void WorldServer::MoveToFreeCell(Unit* unit, const Vector3& from) {
    aiMap.sectorMap.Remove(unit);
    Vector3 free = unit->localPosition;
    aiMap.FindFreeCellNear(unit, from, &free);
    unit->localPosition = free;
    aiMap.sectorMap.Add(unit);
}

void WorldServer::CreateUnits(Player* player, std::vector<PartyUnitRecord*>* records, const Vector3& position, Quaternion rotation,
                               std::vector<UnitPlacement>* placements) {
    UnitSpec spec;
    UnitSpec* message = &spec;
    std::string script;
    std::string line;

    // A hero created next to the start of zone1 plays its arrival animation.
    bool atStart = false;
    if (text::CompareIgnoringCase(std::string_view(mapName).substr(0, 5), "zone1") == 0) {
        const Vector3 heroStart = {kHeroStartX, kHeroStartY, 0.0f};
        float distance = LengthSquared(position - heroStart);
        atStart = distance < kHeroStartRadius2;
    }
    script = "WorldScript(";

    for (int i = 0; i < std::ssize(*records); ++i) {
        auto* record = (*records)[i];
        message->prototype = record->prototype;
        message->texture = record->figureName;
        message->behaviour[0].type = mapspec::BehaviourType::Party;
        message->behaviour[0].enabled = 1;
        message->behaviour[0].flags = 0;
        message->kind = SpecKind::Unit;
        message->scale = {record->scaleX, record->scaleY, record->height};
        message->visible = 1;
        message->side = 0;
        message->parentIname = record->parentIname;
        message->skinTexture = record->skinTexture;
        message->parts.clear();
        const std::string& prototypeName = record->prototype;
        MapObjectType objectClass = PrototypeObjectClass(prototypeName);
        message->parentId = static_cast<int>(objectClass);
        if (objectClass == MapObjectType::Hero) FillHeroParts(message, prototypeName);

        int unitId = static_cast<int>(record->unitId);
        message->id = unitId;
        if (ObjectWithId(unitId)) RemoveObject(message->id);

        UnitPlacement* placement = nullptr;
        if (placements) {
            for (UnitPlacement& entry : *placements) {
                if (entry.id == message->id) {
                    placement = &entry;
                    break;
                }
            }
        }
        if (placement) {
            message->position = placement->position;
            message->rotation = placement->rotation;
        } else {
            PlaceInLine(message, position, rotation, i);
        }

        Unit* unit = FindUnit(CreateObject(message));
        if (!unit) continue;

        // A player-controlled AI, standing on a free cell near the requested position.
        UnitServer* controller = unit->ServerController();
        AIBrain* brain = &controller->brain;
        brain->ClearModes();
        brain->AddMode(std::make_unique<PlayerMode>());
        brain->StopAction();
        MoveToFreeCell(unit, position);

        unit->SetPlayer(player);
        unit->SetScriptName(record->scriptName);
        unit->SetFightAtWill(static_cast<std::uint32_t>(record->fightAtWill));
        unit->ClearProtectionBases();
        unit->SetStats(&record->stats);
        unit->ReleaseAllItems();

        // Equipment: weapons, then the controller's ShowWeapon(0), armour, quick items (two fields copied from the
        // record's), spells.
        for (int n = 0; n < std::ssize(record->weapons); ++n)
            unit->AddWeapon(MakeItem(this, record->weapons[n]));
        unit->controller->ShowWeapon(0);
        for (int n = 0; n < std::ssize(record->armor); ++n)
            unit->EquipArmour(MakeItem(this, record->armor[n]));
        for (int n = 0; n < std::ssize(record->quickItems); ++n) {
            Item* item = MakeItem(this, record->quickItems[n]);
            item->count = record->quickItems[n]->count;
            item->charge = item->chargeMax;
            unit->AddQuickItem(item);
        }
        for (int n = 0; n < std::ssize(record->spells); ++n)
            unit->AddSpell(MakeItem(this, record->spells[n]));

        unit->UpdateSizeClasses();
        unit->ScaleEffectChanged(false);
        unit->SetSecondPrototypeByName(record->secondPrototype);
        line = str::Format("AddObject( Heroes, GetObjectByID( \"%i\" ) )", unit->id);
        script += line;
        if (zoneMode == ZoneMode::Battle && !atStart) unit->moveMode2 = record->moveMode2;
        if (placement) {
            bool flagged = aiMap.IsPassableForClass(1, unit->localPosition);
            if (flagged) unit->SetMoveModes(static_cast<MoveMode>(placement->moveMode), static_cast<MoveMode>(placement->moveMode));
            else unit->moveMode2 = static_cast<MoveMode>(placement->moveMode);
        }
        controller->ResyncWithUnit();
        if (IsSinglePlayerMode(zoneMode))
            unit->pendingWeapon = static_cast<int>(record->weaponIndex);
        if (placement && zoneMode != ZoneMode::Base) unit->pendingWeapon = static_cast<int>(placement->weapon);
    }

    // Followers follow their leaders.
    if (zoneMode == ZoneMode::Battle) {
        for (int i = 0; i < std::ssize(*records); ++i) {
            auto* record = (*records)[i];
            Unit* unit = FindUnit(static_cast<int>(record->unitId));
            if (!unit) continue;
            UnitServer* controller = unit->ServerController();
            if (record->leaderId <= 0) continue;
            AIBrain* brain = &controller->brain;
            PlayerMode* follow = brain->FindModeOf<PlayerMode>();
            if (!follow) continue;
            follow->SetLeader(brain, FindUnit(record->leaderId));
        }
    }

    if (atStart && gameState != GameState::Network)
        script += "PlayAnimation(GetObjectByName( \"hero\" ),\"uspecial25\")";
    script += ")";
    if (player) MainScript()->Load(&script, 1);
}

void WorldServer::UseQuickItem(Unit* unit) {
    // An object record (a class of its own with the same virtuals).
    ObjectSpec message;
    message.kind = SpecKind::QuickEffect;
    message.texture = "st.tr.130";
    message.id = -1;
    message.parentIname = "unhuma";
    message.parentId = static_cast<std::int32_t>(kQuickEffectFlags);
    message.scale.y = 0.0f;
    // At the unit's position, on the ground.
    message.position = WithZ(XY(unit->localPosition), 0.0f);
    int objectId = CreateObject(&message);
    EngineObject* effect = ObjectWithId(objectId);
    effect->flags |= kFlagsNewEffect;
}

int WorldServer::CreateObject(mapspec::Spec* message) {
    // Every kind starts with the base record (Spec); the object, unit, flame, trap, lever and quick effect kinds are
    // object records (ObjectSpec). `spec` is set by the object kinds' cases.
    ObjectSpec* spec = nullptr;
    Spec& base = *message;
    // The object of the spec's class (parentId), as the class the kind makes.
    auto created = [this, &spec]<class T>(std::type_identity<T>) {
        return GetMapObjectManager()->CreateObjectAs<T>(static_cast<MapObjectType>(spec->parentId), spec->id);
    };
    auto allowed = [this](MapObject* object) {
        return (object->IsSynchronised() ? loadedFresh : keepsState) != 0;
    };
    auto refuse = [](MapObject* object) {
        object->AddRef();
        object->Release();
        return -1;
    };
    auto companionTexts = [&spec](MapObjectServer* companion) {
        companion->parentIname = spec->parentIname;
        companion->skinTexture = spec->skinTexture;
        companion->strings = spec->parts;
    };
    auto race = [&spec](MapObject* object) {
        object->AddRef();
        const std::string_view prototype = spec->kind == SpecKind::Unit ? std::string_view(static_cast<UnitSpec*>(spec)->prototype)
                                                           : std::string_view(spec->primaryTexture);
        object->TakePrototype(prototype);
        object->WriteFullState();
    };
    auto place = [&spec](MapObject* object) {
        object->SetFigureScale(spec->scale);
        object->baseScale = spec->scale;
        object->ApplyBaseScale();
        object->localPosition = spec->position;
        object->localRotation = spec->rotation;
    };
    BaseManager* timedManager = timedObjectManager.get();
    MapObjectManager* newObjectManager = GetMapObjectManager();

    switch (base.kind) {
    case SpecKind::Flame: {   // a torch (flame) with its state object (a campfire when it stands on the ground)
        auto* flame = static_cast<mapspec::FlameSpec*>(message);
        spec = flame;
        TorchObject* object = created(std::type_identity<TorchObject>{});
        if (!allowed(object)) return refuse(object);
        int objectId = object->id;
        if (objectId == -1) return -1;
        if (base.id == -1) base.id = objectId;
        // |ground height + z| below 0.7 (a NaN counts as below).
        float height = flame->offset.z + base.position.z;
        float sum = height;
        float level = AbsBits(sum);
        if (level < kCampfireHeight) {
            Vector3 at = flame->offset + base.position;
            Spell* spell = GetSpellManager()->CreateSpellFromText("campfire");
            if (spell) spell->AddRef();
            CastSpell(spell, nullptr, at, nullptr, at, 0);
            if (spell) spell->Release();
        }
        spec->object = object;
        TorchObjectSpec* torch = object->spec;
        auto* companion = object->ServerCompanion();
        object->name = spec->texture;
        companionTexts(companion);
        race(object);
        torch->SetLightSlot(static_cast<std::uint32_t>(EffectTypeNamed(flame->effect).value_or(0)), &flame->offset.x,
                            flame->volume);
        const float colour[4] = {0.8f, 0.8f, 0.8f, 1.0f};
        float radius = flame->volume * kTorchLightScale;
        torch->SetFlameSlot(&flame->offset.x, radius, colour);
        torch->SetNameSlot(flame->sound);
        place(object);
        object->MarkChanged(NetField::Placement);
        object->MarkChanged(NetField::Stats);
        if (spec->visible == 0) object->flags |= MapObjectFlag::Hidden;
        newObjectManager->GiveDefaults(object);
        if (Unit* unit = UnitOrNull(object)) unit->ServerController()->ResyncWithUnit();
        object->SetUpAnimatedFigure(spec->texture);
        FileMapObject(object, true, true);
        return object->id;
    }
    case SpecKind::Trap: {   // a magic trap: updated every tick (no texts, not in the sector map)
        auto* trap = static_cast<mapspec::TrapSpec*>(message);
        spec = trap;
        MagicTrap* object = created(std::type_identity<MagicTrap>{});
        if (!allowed(object)) return refuse(object);
        int objectId = object->id;
        if (objectId == -1) return -1;
        if (base.id == -1) base.id = objectId;
        spec->object = object;
        object->AddRef();
        object->WriteFullState();
        object->MarkChanged(NetField::Placement);
        object->flags |= MapObjectFlag::Hidden;
        FileMapObject(object, true, false);
        object->SetUp(&trap->trap);
        return object->id;
    }
    case SpecKind::Object:
    case SpecKind::Unit:
    case SpecKind::Lever:
    case SpecKind::QuickEffect: {
        spec = static_cast<ObjectSpec*>(message);
        Unit* unit = nullptr;
        Lever* lever = nullptr;
        MapObject* object;
        if (base.kind == SpecKind::Unit)
            object = unit = created(std::type_identity<Unit>{});
        else if (base.kind == SpecKind::Lever)
            object = lever = created(std::type_identity<Lever>{});
        else
            object = created(std::type_identity<MapObject>{});
        if (spec->shadow) object->flags |= MapObjectFlag::Synchronised;
        if (!spec->model.empty())
            object->flags |= MapObjectFlag::Synchronised;
        if (!allowed(object)) return refuse(object);
        int objectId = object->id;
        if (objectId == -1) return -1;
        if (base.id == -1) base.id = objectId;
        spec->object = object;
        auto* companion = object->ServerCompanion();
        object->name = spec->texture;
        object->name2 = spec->model;
        companionTexts(companion);
        race(object);
        object->ScaleEffectChanged(false);
        const SpecKind baseKind = base.kind;
        if (baseKind == SpecKind::Unit) {
            auto* unitSpec = static_cast<UnitSpec*>(message);
            unit->SetPlayer(nullptr);
            unit->SetSide(spec->side);
            unit->SetHostileMask(static_cast<std::uint32_t>(diplomacy.GetHostileMask(spec->side)));
            newObjectManager->ApplyPrototype(object, unitSpec->prototype);
            newObjectManager->GiveStartingEquipment(unit, unitSpec->prototype);
            for (const std::string& name : unitSpec->record.lists[unitrecord::UnitRecord::List::QuickItems])
                unit->AddQuickItem(ItemFactory()->CreateItem(name));
            unitSpec->record.ApplyTo(unit);
            // The model's six values (UnitModelRecord) go to the unit.
            const std::string& prototypeName = unitSpec->prototype;
            UnitPrototypeRecord* prototype = g_unitDatabase.prototypes.FindByName(prototypeName);
            // No prototype record: model 0 (Rebirth's FixAddMapObject; the original read through the null record).
            const unsigned index = prototype ? static_cast<unsigned>(prototype->modelIndex) : 0;
            auto* model = unitdb::ModelRecord(index);
            std::copy_n(model->actionSpeeds, 6, unit->actionSpeeds);
            object->MarkChanged(NetField::Equipment);
            object->MarkChanged(NetField::Modes);
            object->MarkChanged(NetField::Relations);
        } else if (baseKind == SpecKind::Lever) {
            lever->SetUp(&static_cast<mapspec::LeverSpec*>(message)->lever);
            object->MarkChanged(NetField::LeverState);
        } else {
            newObjectManager->GiveDefaults(object);
        }
        place(object);
        object->MarkChanged(NetField::Placement);
        object->MarkChanged(NetField::Modes);
        object->MarkChanged(NetField::Motion);
        object->MarkChanged(NetField::Animation);
        if (spec->visible == 0) object->flags |= MapObjectFlag::Hidden;
        object->SetUpAnimatedFigure(spec->texture);
        object->UpdateSizeClasses();
        if (Unit* asUnit = UnitOrNull(object)) asUnit->ServerController()->ResyncWithUnit();
        FileMapObject(object, baseKind == SpecKind::Lever || baseKind == SpecKind::QuickEffect, true);
        if (!object->name2.empty()) namedObjects.Set(static_cast<unsigned>(object->id), object);
        return object->id;
    }
    case SpecKind::Sound: {
        if (!loadedFresh) return -1;
        auto* sound = static_cast<mapspec::SoundSpec*>(message);
        if (sound->ambient) {
            auto* ambient = timedManager->CreateObjectAs<EffectAmbientSound>(kTimedAmbientSound, base.id);
            ambient->names = sound->sounds;
            ambient->localPosition = base.position;
            ambient->volume = static_cast<float>(sound->volume);
            ambient->radius = static_cast<float>(sound->radius);
            ambient->minInterval = sound->minInterval;
            ambient->maxInterval = sound->maxInterval;
            AddTimedObject(ambient);
            return ambient->id;
        }
        auto* plain = timedManager->CreateObjectAs<EffectSound>(kTimedSound, base.id);
        if (sound->sounds.empty()) {
            ShowLibraryMessageBox("Adding sound without wave name");
        } else {
            str::CopyInto(plain->name, sound->sounds[0]);
        }
        plain->localPosition = base.position;
        plain->volume = static_cast<float>(sound->volume);
        plain->radius = static_cast<float>(sound->radius);
        AddTimedObject(plain);
        return plain->id;
    }
    case SpecKind::Light: {
        if (!loadedFresh) return -1;
        auto* lightSpec = static_cast<mapspec::LightSpec*>(message);
        auto* light = timedManager->CreateObjectAs<EffectPointLight>(kTimedLight, base.id);
        light->localPosition = base.position;
        light->range = lightSpec->radius;
        light->color = lightSpec->colour;
        AddTimedObject(light);
        return light->id;
    }
    case SpecKind::Particle: {
        if (!loadedFresh) return -1;
        auto* particleSpec = static_cast<mapspec::ParticleSpec*>(message);
        auto* particle = timedManager->CreateObjectAs<EffectParticle>(kTimedParticle, base.id);
        particle->localPosition = base.position;
        particle->type = EffectTypeNamed(particleSpec->effect).value_or(0);
        particle->size = particleSpec->scale;
        AddTimedObject(particle);
        return particle->id;
    }
    default:
        return -1;
    }
}

void WorldServer::SetUnitHeight(int unitId, float z) {
    Unit* unit = FindUnit(unitId);
    if (!unit) return;
    // Nothing when the heights are equal.
    if (unit->localPosition.z == z) return;
    Vector3 target = {unit->localPosition.x, unit->localPosition.y, z};
    aiMap.sectorMap.Move(unit, target);
    unit->MarkChanged(NetField::Placement);
}
