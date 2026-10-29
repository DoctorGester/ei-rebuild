// CWorldServer::CreateUnits: creates units from unit records (script AddUnitToServer,
// the party entering a zone, mercenaries), places them, gives them their equipment and AI, and
// adds the heroes to the "Heroes" script group. See world.h and docs/notes/world.md.
#include "ai_map.h"
#include "app.h"
#include "core_refcount.h"
#include "core_streams.h"
#include "item_manager.h"
#include "lever.h"
#include "magic_trap.h"
#include "map_object_figures.h"
#include "object_diplomacy.h"
#include "sector_map.h"
#include "spell.h"
#include "unit_ai.h"
#include "unit_create.h"
#include "unit_record.h"
#include "world.h"
#include "world_data.h"
#include "game_strings.h"

#include "base_manager.h"
#include "chunk_stream.h"
#include "combat.h"
#include "core_maths.h"
#include "item.h"
#include "map_object.h"
#include "map_spec.h"
#include "player_messages.h"
#include "server_specific_manager.h"
#include "torch.h"
#include "unit.h"
#include "unit_db.h"
#include "world_effect.h"

#include "lib/crt.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include "lib/text_case.h"

#include "ei/rebirth.h"
#include "ei/x87.h"
#include "lib/nan_audit.h"
#include "game_data.h"
#include "lib/str.h"
#include "lib/float_select.h"

#include <bit>
#include <type_traits>
#include <vector>

namespace {

using mapspec::ObjectSpec;
using mapspec::Spec;
using mapspec::UnitSpec;

constexpr int kKindSinglePlayer = 1;
constexpr int kKindNoRangeTest = 3;
constexpr int kGameResultLost = 2;

// The hero's figure parts, "hr" replaced by its numbered style (none when the style is -1).
void FillHeroParts(UnitSpec* message, const char* prototypeName, std::string& scratch) {
    UnitPrototypeRecord* prototype;
    {
        prototype = unitdb::PrototypeNamed(std::string(prototypeName));    // find_or_insert of {name, null}
    }
    int hairStyle = prototype ? prototype->hair : 1;

    std::vector<std::string>& parts = message->parts;
    for (int n = 0; n < worlddata::kHeroPartCount; ++n) {
        const char* part = worlddata::HeroPartText(n);
        if (std::strcmp(part, worlddata::HairPartText()) == 0) {
            if (hairStyle != -1) {
                scratch = str::Format(worlddata::HairFormat(), hairStyle);
                parts.push_back(scratch.c_str());
            }
        } else {
            parts.push_back(part);
        }
    }
}

// The place of the `index`th unit when no placement is given: a line from `position` along the
// rotation's heading.
void PlaceInLine(UnitSpec* message, const Vector3& position, const Quaternion& rotation, int index) {
    float rx, ry, rz, rw;
    rx = rotation.w;
    ry = rotation.x;
    rz = rotation.y;
    rw = rotation.z;
    double axis = LengthSquaredUnrounded<Sum3::XYZ, double>(ry, rz, rw);
    double angle;
    EI_NAN_AUDIT(axis);
    if (axis <= worlddata::kAxisEpsilon) {
        angle = 0.0f;
    } else {
        float axisStored = static_cast<float>(axis);
        angle = libcrt::CiAcos(rx);
        angle = angle + angle;
        double sign = 1.0f / std::sqrt(axisStored) * rw;
        EI_NAN_AUDIT(sign);
        if (sign < 0.0f) angle = -angle;
    }
    angle = angle + worlddata::kLineAngleOffset;

    Spec& spec = *message;
    float zStep = 0.0f;
    spec.position = position;
    spec.rotation = rotation;
    float cosine = static_cast<float>(x87::fcos(angle));
    double sine = x87::fsin(angle);
    float step = static_cast<float>(index);
    double dy = sine * step;
    float dx = static_cast<float>(step * static_cast<double>(cosine));
    float dz = static_cast<float>(static_cast<double>(zStep) * worlddata::kLineSpacing);
    dy = dy * worlddata::kLineSpacing;
    float& x = spec.position.x;
    float& y = spec.position.y;
    float& z = spec.position.z;
    x = static_cast<float>(static_cast<double>(dx) * worlddata::kLineSpacing + x);
    y = static_cast<float>(y + dy);
    z = static_cast<float>(static_cast<double>(z) + dz);
}

// An item from an item record, with its counted record holding the item record's id.
CItem* MakeItem(CWorldServer* world, CItem* itemRecord) {
    CItem* item = world->DuplicateItem(itemRecord);
    auto* record = new CItemLink();
    record->value = static_cast<std::uint32_t>(itemRecord->id);
    item->link.Assign(record);
    return item;
}

constexpr int kQuickEffectType = 9;
constexpr std::uint32_t kQuickEffectFlags = 0x3010;
constexpr std::uint32_t kFlagsNewEffect = 0x180000;

// The timed objects' types (world_effect.h).
constexpr int kTimedAmbientSound = 0x49, kTimedSound = 0x48, kTimedLight = 0x4B, kTimedParticle = 0x4A;
constexpr std::uint32_t kGrey = 0x3F4CCCCD, kOne = 0x3F800000;
constexpr std::uint32_t kTorchLightKind = static_cast<std::uint32_t>(ParticleType::CampFire);   // the torch's smoke particles

int UpdateCountdown(const CEngineObject* object) { return CWorldServer::UpdateCountdownFor(object); }

// Copies dwords (floats are not loaded on the FPU).
void Copy(void* to, const void* from, std::size_t n) { std::memcpy(to, from, n); }

} // namespace

void CWorldServer::CreateUnits(CPlayer* player, std::vector<PartyUnitRecord*>* records, const Vector3& position, Quaternion rotation,
                               std::vector<UnitPlacement>* placements) {
    // Destroyed after the two strings (declared after it); its destructor is inlined.
    UnitSpec spec;
    UnitSpec* message = &spec;
    std::string script;
    std::string line;

    // A hero created next to the start of zone1 plays its arrival animation.
    bool atStart = false;
    if (text::CompareIgnoringCase(mapName.c_str(), worlddata::StartZoneText(), 5) == 0) {
        double dz = position.z;
        double dy = static_cast<double>(position.y) - worlddata::kHeroStartY;
        double dx = static_cast<double>(position.x) - worlddata::kHeroStartX;
        double distance = LengthSquaredUnrounded<Sum3::XYZ>(dx, dy, dz);
        EI_NAN_AUDIT(distance);
        atStart = distance < worlddata::kHeroStartRadius2;
    }
    script = worlddata::ScriptBeginText();

    for (int i = 0; i < std::ssize(*records); ++i) {
        auto* record = (*records)[i];
        message->prototype = record->texts[5];
        message->texture = record->texts[2];
        message->behaviour[0].type = 4;
        message->behaviour[0].enabled = 1;
        message->behaviour[0].flags = 0;
        message->kind = 5;
        std::memcpy(&message->scale, &record->scaleX, 12);
        message->visible = 1;
        message->side = 0;
        message->parentIname = record->texts[3];
        message->skinTexture = record->texts[4];
        message->parts.clear();
        const char* prototypeName = record->texts[5].c_str();
        MapObjectType objectClass = PrototypeObjectClass(prototypeName);
        message->parentId = static_cast<int>(objectClass);
        if (objectClass == MapObjectType::Hero) FillHeroParts(message, prototypeName, line);

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
            std::memcpy(&message->position, placement->position, 12);
            std::memcpy(&message->rotation, placement->rotation, 16);
        } else {
            PlaceInLine(message, position, rotation, i);
        }

        CUnit* unit = FindUnit(CreateObject(message));
        if (!unit) continue;
        CMapObject* slots = unit;

        // A player-controlled AI, standing on a free cell near the requested position.
        CUnitServer* controller = unit->ServerController();
        AIBrain* brain = &controller->brain;
        brain->ClearModes();
        brain->AddMode(std::make_unique<PlayerMode>());
        brain->StopAction();
        aiMap.sectorMap.Remove(unit);
        Vector3 free;
        std::memcpy(&free, &unit->localPosition.x, sizeof(free));
        aiMap.FindFreeCellNear(unit, position, &free);
        std::memcpy(&unit->localPosition.x, &free, sizeof(free));
        aiMap.sectorMap.Add(unit);

        unit->SetPlayer(player);
        slots->SetScriptName(record->texts[0]);
        unit->SetFightAtWill(static_cast<std::uint32_t>(record->fightAtWill));
        unit->ClearProtectionBases();
        slots->SetStats(&record->stats);
        unit->ReleaseAllItems();

        // Equipment: weapons, then the controller's ShowWeapon(0), armour, quick items (two fields copied from the
        // record's), spells.
        for (int n = 0; n < std::ssize(record->weapons); ++n)
            unit->AddWeapon(MakeItem(this, record->weapons[n]));
        unit->Controller()->ShowWeapon(0);
        for (int n = 0; n < std::ssize(record->armor); ++n)
            unit->EquipArmour(MakeItem(this, record->armor[n]));
        for (int n = 0; n < std::ssize(record->quickItems); ++n) {
            CItem* item = MakeItem(this, record->quickItems[n]);
            item->count = record->quickItems[n]->count;
            item->charge = item->chargeMax;
            unit->AddQuickItem(item);
        }
        for (int n = 0; n < std::ssize(record->spells); ++n)
            unit->AddSpell(MakeItem(this, record->spells[n]));

        slots->UpdateSizeClasses();
        slots->ScaleEffectChanged(false);
        unit->SetSecondPrototypeByName(record->texts[6]);
        line = str::Format(worlddata::AddToHeroesFormat(), unit->id);
        script += line;
        if (sessionKind == kKindSinglePlayer && !atStart) unit->moveMode2 = record->moveMode2;
        if (placement) {
            bool flagged = aiMap.IsPassableForClass(1, unit->localPosition);
            if (flagged) unit->SetMoveModes(placement->moveMode, placement->moveMode);
            else unit->moveMode2 = static_cast<int>(placement->moveMode);
        }
        controller->ResyncWithUnit();
        if (sessionKind == kKindSinglePlayer || sessionKind == kKindNoRangeTest)
            unit->pendingWeapon = static_cast<int>(record->weaponIndex);
        if (placement && sessionKind != kKindNoRangeTest) unit->pendingWeapon = static_cast<int>(placement->weapon);
    }

    // Followers follow their leaders.
    if (sessionKind == kKindSinglePlayer) {
        for (int i = 0; i < std::ssize(*records); ++i) {
            auto* record = (*records)[i];
            CUnit* unit = FindUnit(static_cast<int>(record->unitId));
            if (!unit) continue;
            CUnitServer* controller = unit->ServerController();
            if (record->leaderId <= 0) continue;
            AIBrain* brain = &controller->brain;
            PlayerMode* follow = brain->FindPlayerMode();
            if (!follow) continue;
            follow->SetLeader(brain, FindUnit(record->leaderId));
        }
    }

    if (atStart && gameResult != kGameResultLost) script += worlddata::HeroArrivalText();
    script += worlddata::ScriptEndText();
    if (player) MainScript()->Load(&script, 1);
}

void CWorldServer::UseQuickItem(CUnit* unit) {
    // An object record (a class of its own with the same virtuals).
    ObjectSpec message;
    message.kind = kQuickEffectType;
    message.texture = worlddata::QuickEffectPrototype();
    message.id = -1;
    message.parentIname = worlddata::QuickEffectModel();
    message.parentId = static_cast<std::int32_t>(kQuickEffectFlags);
    message.scale.y = 0.0f;
    // At the unit's position, on the ground.
    std::memcpy(&message.position, &unit->localPosition.x, 12);
    message.position.z = 0.0f;
    int objectId = CreateObject(&message);
    CEngineObject* effect = ObjectWithId(objectId);
    effect->flags |= kFlagsNewEffect;
}

int CWorldServer::CreateObject(mapspec::Spec* message) {
    // Every kind starts with the base record (Spec); kinds 1, 5-9 are object records (ObjectSpec), 5 a unit
    // record, 6 a flame, 7 a magic trap, 8 a lever; 2 a light, 3 a sound, 4 a particle source.
    auto* spec = static_cast<ObjectSpec*>(message);
    Spec& base = *message;
    // The object of the spec's class (parentId), as the class the kind makes.
    auto created = [this, spec]<class T>(std::type_identity<T>) {
        return MapObjectManager()->CreateObjectAs<T>(static_cast<MapObjectType>(spec->parentId), spec->id);
    };
    auto allowed = [this](CMapObject* object) {
        return (object->IsSynchronised() ? loadedFresh : keepsState) != 0;
    };
    auto refuse = [](CMapObject* object) {
        object->AddRef();
        object->Release();
        return -1;
    };
    auto companionTexts = [spec](CMapObjectServer* companion) {
        companion->parentIname = spec->parentIname;
        companion->skinTexture = spec->skinTexture;
        companion->strings = spec->parts;
    };
    auto race = [spec](CMapObject* object) {
        object->AddRef();
        const std::string_view prototype = spec->kind == 5 ? std::string_view(static_cast<UnitSpec*>(spec)->prototype)
                                                           : std::string_view(spec->primaryTexture);
        object->TakePrototype(prototype);
        object->WriteFullState();
    };
    auto place = [spec](CMapObject* object) {
        object->SetFigureScale(&spec->scale.x);
        Copy(&object->baseScale, &spec->scale, 12);
        object->ApplyBaseScale();
        Copy(&object->localPosition.x, &spec->position, 12);
        Copy(&object->localRotation, &spec->rotation, 16);
    };
    auto timed = [this](CEffect* object) {
        object->startTick = tick;
        object->AddRef();
        object->MarkChanged();
        timedObjects.Set(static_cast<unsigned>(object->id), object);
        return object->id;
    };
    CBaseManager* timedManager = timedObjectManager;
    CMapObjectManager* newObjectManager = MapObjectManager();

    switch (base.kind) {
    case 6: {   // a torch (flame) with its state object (a campfire when it stands on the ground)
        auto* flame = static_cast<mapspec::FlameSpec*>(message);
        CTorchObject* object = created(std::type_identity<CTorchObject>{});
        if (!allowed(object)) return refuse(object);
        int objectId = object->id;
        if (objectId == -1) return -1;
        if (base.id == -1) base.id = objectId;
        // |ground height + z| below 0.7 (a NaN counts as below).
        float height = flame->offset.z + base.position.z;
        float sum = height;
        float level = AbsBits(sum);
        EI_NAN_AUDIT(level);
        if (level < worlddata::kCampfireHeight) {
            Vector3 at;
            at.z = flame->offset.z + base.position.z;
            at.y = flame->offset.y + base.position.y;
            at.x = flame->offset.x + base.position.x;
            CSpell* spell = SpellManager()->CreateSpellFromText(worlddata::CampfireText());
            if (spell) spell->AddRef();
            CastSpell(spell, nullptr, at, nullptr, at, 0);
            if (spell) spell->Release();
        }
        base.object = object;
        CTorchObjectSpec* torch = object->spec;
        auto* companion = object->ServerCompanion();
        object->name = spec->texture;
        companionTexts(companion);
        race(object);
        torch->SetLightSlot(kTorchLightKind, &flame->offset.x, flame->volume);
        const float colour[4] = {std::bit_cast<float>(kGrey), std::bit_cast<float>(kGrey), std::bit_cast<float>(kGrey),
                                 std::bit_cast<float>(kOne)};
        float radius = flame->volume * worlddata::kTorchLightScale;
        torch->SetFlameSlot(&flame->offset.x, radius, colour);
        torch->SetNameSlot(flame->sound.c_str());
        place(object);
        object->MarkChanged(2);
        object->MarkChanged(4);
        if (spec->visible == 0) object->flags |= MapObjectFlag::Hidden;
        newObjectManager->GiveDefaults(object);
        if (CUnit* unit = UnitOrNull(object)) unit->ServerController()->ResyncWithUnit();
        object->SetUpAnimatedFigure(spec->texture);
        objectId = object->id;
        objects.Set(static_cast<unsigned>(objectId), object);
        mapObjects.Set(static_cast<unsigned>(object->id), object);
        if (object->GetClassId() != ClassId::Unit) otherMapObjects.Set(static_cast<unsigned>(object->id), object);
        object->updateCountdown = UpdateCountdown(object);
        updateMap.Set(static_cast<unsigned>(object->id), object);
        aiMap.sectorMap.Add(object);
        int result = object->id;
        if (object->flags & MapObjectFlag::Changed) object->WriteChangedState();
        return result;
    }
    case 7: {   // a magic trap: updated every tick (no texts, not in the sector map)
        MagicTrap* object = created(std::type_identity<MagicTrap>{});
        if (!allowed(object)) return refuse(object);
        int objectId = object->id;
        if (objectId == -1) return -1;
        if (base.id == -1) base.id = objectId;
        base.object = object;
        object->AddRef();
        object->WriteFullState();
        object->MarkChanged(2);
        object->flags |= MapObjectFlag::Hidden;
        objects.Set(static_cast<unsigned>(object->id), object);
        mapObjects.Set(static_cast<unsigned>(object->id), object);
        if (object->GetClassId() != ClassId::Unit) otherMapObjects.Set(static_cast<unsigned>(object->id), object);
        object->updateCountdown = UpdateCountdown(object);
        updateMap.Set(static_cast<unsigned>(object->id), object);
        int result = object->id;
        if (object->flags & MapObjectFlag::Changed) object->WriteChangedState();
        object->SetUp(&static_cast<mapspec::TrapSpec*>(message)->trap);
        return result;
    }
    case 1:
    case 5:
    case 8:
    case 9: {   // map objects, units (5) and levers (8)
        CUnit* unit = nullptr;
        Lever* lever = nullptr;
        CMapObject* object;
        if (base.kind == 5)
            object = unit = created(std::type_identity<CUnit>{});
        else if (base.kind == 8)
            object = lever = created(std::type_identity<Lever>{});
        else
            object = created(std::type_identity<CMapObject>{});
        if (spec->shadow) object->flags |= MapObjectFlag::Synchronised;
        if (str::Compare(spec->model,
                            game::kEmptyText) != 0)
            object->flags |= MapObjectFlag::Synchronised;
        if (!allowed(object)) return refuse(object);
        int objectId = object->id;
        if (objectId == -1) return -1;
        if (base.id == -1) base.id = objectId;
        base.object = object;
        auto* companion = object->ServerCompanion();
        object->name = spec->texture;
        object->SetSecondName(spec->model.c_str());
        companionTexts(companion);
        race(object);
        object->ScaleEffectChanged(false);
        int baseKind = base.kind;
        if (baseKind == 5) {
            auto* unitSpec = static_cast<UnitSpec*>(message);
            unit->SetPlayer(nullptr);
            unit->SetSide(spec->side);
            unit->SetHostileMask(static_cast<std::uint32_t>(diplomacy.GetHostileMask(spec->side)));
            newObjectManager->ApplyPrototype(object, unitSpec->prototype);
            newObjectManager->GiveStartingEquipment(unit, unitSpec->prototype);
            for (const std::string& name : unitSpec->record.lists[unitrecord::UnitRecord::kQuickItems])
                unit->AddQuickItem(ItemFactory()->CreateItem(name));
            unitSpec->record.ApplyTo(unit);
            // The model's six values (UnitModelRecord) go to the unit.
            const char* prototypeName = unitSpec->prototype.c_str();
            UnitPrototypeRecord* prototype;
            {
                std::string key(prototypeName);
                prototype = unitdb::PrototypeNamed(key);
            }
            unsigned index;
            if (prototype) {
                index = static_cast<unsigned>(prototype->modelIndex);
            } else {
                // Rebirth's FixAddMapObject (context hook): no prototype record is logged ("Invalid unit's prototype") and
                // index 0 used. Vanilla reads through the null record (and crashes).
                if (!ei::rebirth::Enabled(ei::rebirth::Patch::FixAddMapObject))
                    index = *reinterpret_cast<const volatile unsigned*>(reinterpret_cast<std::uintptr_t>(prototype) + 8);
                else
                    index = 0;
            }
            auto* model = unitdb::ModelRecord(index);
            std::memcpy(unit->actionSpeeds, model->actionSpeeds, 24);
            object->MarkChanged(5);
            object->MarkChanged(6);
            object->MarkChanged(0xB);
        } else if (baseKind == 8) {
            lever->SetUp(&static_cast<mapspec::LeverSpec*>(message)->lever);
            object->MarkChanged(5);
        } else {
            newObjectManager->GiveDefaults(object);
        }
        place(object);
        object->MarkChanged(2);
        object->MarkChanged(6);
        object->MarkChanged(7);
        object->MarkChanged(8);
        if (spec->visible == 0) object->flags |= MapObjectFlag::Hidden;
        object->SetUpAnimatedFigure(spec->texture);
        object->UpdateSizeClasses();
        if (CUnit* asUnit = UnitOrNull(object)) asUnit->ServerController()->ResyncWithUnit();
        objects.Set(static_cast<unsigned>(object->id), object);
        mapObjects.Set(static_cast<unsigned>(object->id), object);
        if (object->GetClassId() != ClassId::Unit) otherMapObjects.Set(static_cast<unsigned>(object->id), object);
        aiMap.sectorMap.Add(object);
        int result = object->id;
        if (object->flags & MapObjectFlag::Changed) object->WriteChangedState();
        if (baseKind >= 8 && baseKind <= 9) {
            object->updateCountdown = UpdateCountdown(object);
            updateMap.Set(static_cast<unsigned>(object->id), object);
        }
        if (!object->name2.empty()) namedObjects.Set(static_cast<unsigned>(object->id), object);
        return result;
    }
    case 3: {   // a sound
        if (!loadedFresh) return -1;
        auto* sound = static_cast<mapspec::SoundSpec*>(message);
        if (sound->ambient) {
            auto* ambient = timedManager->CreateObjectAs<CEffectAmbientSound>(kTimedAmbientSound, base.id);
            ambient->names = sound->sounds;
            Copy(&ambient->localPosition, &base.position, 12);
            ambient->volume = static_cast<float>(sound->volume);
            ambient->radius = static_cast<float>(sound->radius);
            ambient->minInterval = sound->minInterval;
            ambient->maxInterval = sound->maxInterval;
            return timed(ambient);
        }
        auto* plain = timedManager->CreateObjectAs<CEffectSound>(kTimedSound, base.id);
        if (sound->sounds.empty()) {
            app::ShowLibraryMessageBox(worlddata::NoWaveNameText());
        } else {
            const char* wave = sound->sounds[0].c_str();
            std::memcpy(plain->name, wave, std::strlen(wave) + 1);
        }
        Copy(&plain->localPosition, &base.position, 12);
        plain->volume = static_cast<float>(sound->volume);
        plain->radius = static_cast<float>(sound->radius);
        return timed(plain);
    }
    case 2: {   // a light
        if (!loadedFresh) return -1;
        auto* lightSpec = static_cast<mapspec::LightSpec*>(message);
        auto* light = timedManager->CreateObjectAs<CEffectPointLight>(kTimedLight, base.id);
        Copy(&light->localPosition, &base.position, 12);
        Copy(&light->range, &lightSpec->radius, 4);
        Copy(&light->color, &lightSpec->colour, 12);
        return timed(light);
    }
    case 4: {   // a particle source
        if (!loadedFresh) return -1;
        auto* particleSpec = static_cast<mapspec::ParticleSpec*>(message);
        auto* particle = timedManager->CreateObjectAs<CEffectParticle>(kTimedParticle, base.id);
        Copy(&particle->localPosition, &base.position, 12);
        Copy(&particle->type, &particleSpec->type, 4);
        Copy(&particle->size, &particleSpec->scale, 4);
        return timed(particle);
    }
    default:
        // The argument slot is returned untouched: the message's address.
        return reinterpret_cast<int>(message);
    }
}
