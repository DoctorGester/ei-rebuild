// CWorldServer::CreateUnits: creates units from unit records (script AddUnitToServer,
// the party entering a zone, mercenaries), places them, gives them their equipment and AI, and
// adds the heroes to the "Heroes" script group. See world.h and docs/notes/world.md.
#include "ai_map.h"
#include "core_refcount.h"
#include "core_streams.h"
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

#include "lib/static_object.h"
#include "lib/crt.h"
#include "lib/mfc.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "ei/x87.h"
#include "lib/nan_audit.h"
#include "lib/mfc_window.h"
#include "game_data.h"

#include <bit>
#include <cstring>
#include <type_traits>

namespace {

using mapspec::ObjectSpec;
using mapspec::Spec;
using mapspec::UnitSpec;

constexpr int kTypeHero = 0x32;                        // the map objects manager's type of hero prototypes
constexpr int kKindSinglePlayer = 1;
constexpr int kKindNoRangeTest = 3;
constexpr int kGameResultLost = 2;

using Placement = UnitPlacement;
using PlacementArray = mfc::CArray<Placement>;

// The hero's figure parts, "hr" replaced by its numbered style (none when the style is -1).
void FillHeroParts(UnitSpec* message, const char* prototypeName, mfc::CString& scratch) {
    UnitPrototypeRecord* prototype;
    {
        prototype = unitdb::PrototypeNamed(stlp::string(prototypeName));   // find_or_insert of {name, null}
    }
    int hairStyle = prototype ? prototype->hair : 1;

    mfc::CStringArray* parts = &message->parts;
    for (int n = 0; n < worlddata::kHeroPartCount; ++n) {
        const char* part = worlddata::HeroPartText(n);
        if (std::strcmp(part, worlddata::HairPartText()) == 0) {
            if (hairStyle != -1) {
                scratch.Format(worlddata::HairFormat(), hairStyle);
                parts->SetAtGrow(parts->size, scratch);
            }
        } else {
            parts->SetAtGrow(parts->size, part);
        }
    }
}

// The place of the `index`th unit when no placement is given: a line from `position` along the
// rotation's heading.
void PlaceInLine(UnitSpec* message, const Vector3* position, const Quaternion& rotation, int index) {
    float rx, ry, rz, rw;
    std::memcpy(&rx, &rotation.w, 4);
    std::memcpy(&ry, &rotation.x, 4);
    std::memcpy(&rz, &rotation.y, 4);
    std::memcpy(&rw, &rotation.z, 4);
    double axis = LengthSquaredUnrounded<Sum3::XYZ, double>(ry, rz, rw);
    double angle;
    EI_NAN_AUDIT(axis);
    if (axis <= worlddata::AxisEpsilon()) {
        angle = worlddata::Zero();
    } else {
        float axisStored = static_cast<float>(axis);
        angle = gamecrt::acos(rx);
        angle = angle + angle;
        double sign = worlddata::One() / std::sqrt(axisStored) * rw;
        EI_NAN_AUDIT(sign);
        if (sign < worlddata::Zero()) angle = -angle;
    }
    angle = angle + worlddata::LineAngleOffset();

    Spec& spec = *message;
    float zStep = 0.0f;
    std::memcpy(&spec.position, position, 12);
    std::memcpy(&spec.rotation, &rotation, 16);
    float cosine = static_cast<float>(x87::fcos(angle));
    double sine = x87::fsin(angle);
    float step = static_cast<float>(index);
    double dy = sine * step;
    float dx = static_cast<float>(step * static_cast<double>(cosine));
    float dz = static_cast<float>(static_cast<double>(zStep) * worlddata::LineSpacing());
    dy = dy * worlddata::LineSpacing();
    float& x = spec.position.x;
    float& y = spec.position.y;
    float& z = spec.position.z;
    x = static_cast<float>(static_cast<double>(dx) * worlddata::LineSpacing() + x);
    y = static_cast<float>(y + dy);
    z = static_cast<float>(static_cast<double>(z) + dz);
}

// An item from an item record, with its counted record holding the item record's id.
CItem* MakeItem(CWorldServer* world, CItem* itemRecord) {
    CItem* item = world->DuplicateItem(itemRecord);
    auto* record = new CItemLink();
    record->value = static_cast<std::uint32_t>(itemRecord->id_);
    reinterpret_cast<RefPtr*>(&item->link_)->Assign(reinterpret_cast<CRefObject*>(record));
    return item;
}

} // namespace

void CWorldServer::CreateUnits(CPlayer* player, RecordPtrArray* records, const Vector3* position, Quaternion rotation,
                               PlacementArray* placements) {
    // Destroyed after the two strings (declared after it); its destructor is inlined.
    UnitSpec spec;
    UnitSpec* message = &spec;
    mfc::CString script;
    mfc::CString line;

    // A hero created next to the start of zone1 plays its arrival animation.
    bool atStart = false;
    if (gamecrt::strnicmp(mapName_, worlddata::StartZoneText(), 5) == 0) {
        double dz = position->z;
        double dy = static_cast<double>(position->y) - worlddata::HeroStartY();
        double dx = static_cast<double>(position->x) - worlddata::HeroStartX();
        double distance = LengthSquaredUnrounded<Sum3::XYZ>(dx, dy, dz);
        EI_NAN_AUDIT(distance);
        atStart = distance < worlddata::HeroStartRadius2();
    }
    script = worlddata::ScriptBeginText();

    for (int i = 0; i < records->size; ++i) {
        auto* record = records->data[i];
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
        message->parts.SetSize(0, -1);
        const char* prototypeName = record->texts[5].c_str();
        int type = PrototypeObjectClass(prototypeName);
        message->parentId = type;
        if (type == kTypeHero) FillHeroParts(message, prototypeName, line);

        int id = static_cast<int>(record->unitId);
        message->id = id;
        if (ObjectWithId(id)) RemoveObject(message->id);

        Placement* placement = nullptr;
        if (placements) {
            for (int p = 0; p < placements->size; ++p) {
                if (placements->data[p].id == message->id) {
                    placement = &placements->data[p];
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
        CMapObjectVirtuals* slots = ObjectSlots(unit);

        // A player-controlled AI, standing on a free cell near the requested position.
        CUnitServer* controller = unit->ServerController();
        AIBrain* brain = &controller->brain;
        brain->ClearModes();
        brain->AddMode(new PlayerMode());
        brain->StopAction();
        Sectors()->Remove(unit);
        Vector3 free;
        std::memcpy(&free, &unit->localPosition_.x, sizeof(free));
        AIMap()->FindFreeCellNear(unit, position, &free);
        std::memcpy(&unit->localPosition_.x, &free, sizeof(free));
        Sectors()->Add(unit);

        unit->SetPlayer(player);
        slots->SetScriptName(record->texts[0].c_str());
        unit->SetUnknown438(static_cast<std::uint32_t>(record->fightAtWill));
        unit->ClearProtectionBases();
        slots->SetStats(&record->stats);
        unit->ReleaseAllItems();

        // Equipment: weapons, then the controller's ShowWeapon(0), armour, quick items (two fields copied from the
        // record's), spells.
        for (int n = 0; n < static_cast<int>(record->weaponCount); ++n)
            unit->AddWeapon(MakeItem(this, record->weapons[n]));
        unit->Controller()->ShowWeapon(0);
        for (int n = 0; n < static_cast<int>(record->armorCount); ++n)
            unit->EquipArmour(MakeItem(this, record->armor[n]));
        for (int n = 0; n < static_cast<int>(record->quickItemCount); ++n) {
            CItem* item = MakeItem(this, record->quickItems[n]);
            item->count_ = record->quickItems[n]->count_;
            std::memcpy(&item->charge_, &item->chargeMax_, 4);
            unit->AddQuickItem(item);
        }
        for (int n = 0; n < static_cast<int>(record->spellCount); ++n)
            unit->AddSpell(MakeItem(this, record->spells[n]));

        slots->UpdateSizeClasses();
        slots->ScaleEffectChanged(0);
        unit->SetPrototype23CByName(record->texts[6].c_str());
        line.Format(worlddata::AddToHeroesFormat(), unit->id_);
        script += line;
        if (sessionKind_ == kKindSinglePlayer && !atStart) unit->moveMode2 = record->moveMode2;
        if (placement) {
            bool flagged = AIMap()->IsPassableForClass(1, &unit->localPosition_);
            if (flagged) unit->SetMoveModes(placement->value20, placement->value20);
            else unit->moveMode2 = static_cast<int>(placement->value20);
        }
        controller->ResyncWithUnit();
        if (sessionKind_ == kKindSinglePlayer || sessionKind_ == kKindNoRangeTest)
            unit->pendingWeapon = static_cast<int>(record->weaponIndex);
        if (placement && sessionKind_ != kKindNoRangeTest) unit->pendingWeapon = static_cast<int>(placement->value24);
    }

    // Followers follow their leaders.
    if (sessionKind_ == kKindSinglePlayer) {
        for (int i = 0; i < records->size; ++i) {
            auto* record = records->data[i];
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

    if (atStart && gameResult_ != kGameResultLost) script += worlddata::HeroArrivalText();
    script += worlddata::ScriptEndText();
    if (player) MainScript()->Load(&script, 1);
}

namespace {

constexpr int kQuickEffectType = 9;
constexpr std::uint32_t kQuickEffectFlags = 0x3010;
constexpr std::uint32_t kFlagsNewEffect = 0x180000;

} // namespace

void CWorldServer::UseQuickItem(CItem* /*item*/, CUnit* unit) {
    // An object record (a class of its own with the same virtuals).
    ObjectSpec message;
    message.kind = kQuickEffectType;
    message.texture = worlddata::QuickEffectPrototype();
    message.id = -1;
    message.parentIname = worlddata::QuickEffectModel();
    message.parentId = static_cast<std::int32_t>(kQuickEffectFlags);
    std::memset(&message.scale.y, 0, 4);
    // At the unit's position, on the ground.
    std::memcpy(&message.position, &unit->localPosition_.x, 12);
    std::memset(&message.position.z, 0, 4);
    int id = CreateObject(&message);
    CEngineObject* effect = ObjectWithId(id);
    effect->flags_ |= kFlagsNewEffect;
}

namespace {

void __fastcall UnitSetDiplomacy(CUnit* a0, void*, int a2) { a0->SetHostileMask((std::uint32_t)a2); }

// The timed objects' types (world_effect.h).
constexpr int kTimedAmbientSound = 0x49, kTimedSound = 0x48, kTimedLight = 0x4B, kTimedParticle = 0x4A;
constexpr std::uint32_t kGrey = 0x3F4CCCCD, kOne = 0x3F800000;
constexpr std::uint32_t kTorchLightKind = 0x2001;   // the torch's smoke particles

int UpdateCountdown(const CEngineObject* object) { return CWorldServer::UpdateCountdownFor(object); }

// Copies dwords (floats are not loaded on the FPU).
void Copy(void* to, const void* from, std::size_t n) { std::memcpy(to, from, n); }

mfc::CString* NameString(CMapObject* object) { return &object->name; }

} // namespace

int CWorldServer::CreateObject(mapspec::Spec* message) {
    // Every kind starts with the base record (Spec); kinds 1, 5-9 are object records (ObjectSpec), 5 a unit
    // record, 6 a flame, 7 a magic trap, 8 a lever; 2 a light, 3 a sound, 4 a particle source.
    auto* spec = static_cast<ObjectSpec*>(message);
    Spec& base = *message;
    // The object of the spec's class (parentId), as the class the kind makes.
    auto created = [this, spec]<class T>(std::type_identity<T>) {
        return MapObjectManager()->CreateObjectAs<T>(spec->parentId, spec->id);
    };
    auto allowed = [this](CMapObject* object) {
        return (ObjectSlots(object)->IsSynchronised() ? loadedFresh_ : keepsState_) != 0;
    };
    auto refuse = [](CMapObject* object) {
        ++object->refCount_;
        object->Release();
        return -1;
    };
    auto companionTexts = [spec](CMapObjectServer* companion) {
        companion->name78 = spec->parentIname;
        companion->name7C = spec->skinTexture;
        companion->strings.Copy(spec->parts);
    };
    auto race = [spec](CMapObject* object) {
        ++object->refCount_;
        const char* prototype = spec->kind == 5 ? static_cast<UnitSpec*>(spec)->prototype.GetString()
                                                     : spec->primaryTexture.GetString();
        ObjectSlots(object)->TakePrototype(prototype);
        object->WriteFullState();
    };
    auto place = [spec](CMapObject* object) {
        static_cast<CObject3D*>(object)->SetFigureScale(&spec->scale.x);
        Copy(&object->baseScale, &spec->scale, 12);
        ObjectSlots(object)->ApplyBaseScale();
        Copy(&object->localPosition_.x, &spec->position, 12);
        Copy(&object->localRotation_, &spec->rotation, 16);
    };
    auto timed = [this](CEffect* object) {
        object->startTick_ = tick_;
        ++object->refCount_;
        object->MarkChanged();
        timedObjects_.Set(static_cast<unsigned>(object->id_), object);
        return object->id_;
    };
    CBaseManager* timedManager = manager48_;
    CMapObjectManager* objectManager = MapObjectManager();

    switch (base.kind) {
    case 6: {   // a torch (flame) with its state object (a campfire when it stands on the ground)
        auto* flame = static_cast<mapspec::FlameSpec*>(message);
        CTorchObject* object = created(std::type_identity<CTorchObject>{});
        if (!allowed(object)) return refuse(object);
        int id = object->id_;
        if (id == -1) return -1;
        if (base.id == -1) base.id = id;
        // |ground height + z| below 0.7 (FCOMP with C0: a NaN counts as below).
        float height = flame->offset.z + base.position.z;
        float sum = height;
        std::uint32_t bits;
        std::memcpy(&bits, &sum, 4);
        bits &= 0x7FFFFFFFu;
        float level;
        std::memcpy(&level, &bits, 4);
        EI_NAN_AUDIT(level);
        if (level < worlddata::CampfireHeight()) {
            Vector3 at;
            at.z = flame->offset.z + base.position.z;
            at.y = flame->offset.y + base.position.y;
            at.x = flame->offset.x + base.position.x;
            CSpell* spell = SpellManager()->CreateSpellFromText(worlddata::CampfireText());
            if (spell) ++spell->refCount_;
            CastSpell(spell, nullptr, const_cast<const Vector3*>(&at), nullptr,
                                   const_cast<const Vector3*>(&at), 0);
            if (spell) spell->Release();
        }
        base.object = object;
        CTorchObjectSpec* torch = object->spec;
        auto* companion = object->ServerCompanion();
        *NameString(object) = spec->texture;
        companionTexts(companion);
        race(object);
        torch->SetLightSlot(kTorchLightKind, &flame->offset.x, flame->volume);
        const float colour[4] = {std::bit_cast<float>(kGrey), std::bit_cast<float>(kGrey), std::bit_cast<float>(kGrey),
                                 std::bit_cast<float>(kOne)};
        float radius = flame->volume * worlddata::TorchLightScale();
        torch->SetFlameSlot(&flame->offset.x, radius, colour);
        torch->SetNameSlot(flame->sound);
        place(object);
        object->MarkChanged(2);
        object->MarkChanged(4);
        if (spec->visible == 0) object->flags_ |= 8;
        objectManager->GiveDefaults(object);
        if (CUnit* unit = UnitOrNull(object)) unit->ServerController()->ResyncWithUnit();
        object->SetUpAnimatedFigure(spec->texture);
        id = object->id_;
        objects_.Set(static_cast<unsigned>(id), object);
        mapObjects_.Set(static_cast<unsigned>(object->id_), object);
        if (object->GetClassId() != world::kClassUnit) otherMapObjects_.Set(static_cast<unsigned>(object->id_), object);
        object->updateCountdown = UpdateCountdown(object);
        updateMap_.Set(static_cast<unsigned>(object->id_), object);
        Sectors()->Add(object);
        int result = object->id_;
        if (object->flags_ & 2) object->WriteChangedState();
        return result;
    }
    case 7: {   // a magic trap: updated every tick (no texts, not in the sector map)
        MagicTrap* object = created(std::type_identity<MagicTrap>{});
        if (!allowed(object)) return refuse(object);
        int id = object->id_;
        if (id == -1) return -1;
        if (base.id == -1) base.id = id;
        base.object = object;
        ++object->refCount_;
        object->WriteFullState();
        object->MarkChanged(2);
        object->flags_ |= 8;
        objects_.Set(static_cast<unsigned>(object->id_), object);
        mapObjects_.Set(static_cast<unsigned>(object->id_), object);
        if (object->GetClassId() != world::kClassUnit) otherMapObjects_.Set(static_cast<unsigned>(object->id_), object);
        object->updateCountdown = UpdateCountdown(object);
        updateMap_.Set(static_cast<unsigned>(object->id_), object);
        int result = object->id_;
        if (object->flags_ & 2) object->WriteChangedState();
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
        if (spec->shadow) object->flags_ |= 0x20000;
        if (gamecrt::mbscmp(spec->model.GetString(),
                            game::EmptyText()) != 0)
            object->flags_ |= 0x20000;
        if (!allowed(object)) return refuse(object);
        int id = object->id_;
        if (id == -1) return -1;
        if (base.id == -1) base.id = id;
        base.object = object;
        auto* companion = object->ServerCompanion();
        *NameString(object) = spec->texture;
        object->SetSecondName(spec->model);
        companionTexts(companion);
        race(object);
        ObjectSlots(object)->ScaleEffectChanged(0);
        int type = base.kind;
        if (type == 5) {
            auto* unitSpec = static_cast<UnitSpec*>(message);
            unit->SetPlayer(nullptr);
            unit->SetSide(spec->side);
            UnitSetDiplomacy(unit, nullptr, diplomacy_.GetHostileMask(spec->side));
            objectManager->ApplyPrototype(object, unitSpec->prototype);
            objectManager->GiveStartingEquipment(unit, unitSpec->prototype);
            const mfc::CStringArray& worn = unitSpec->record.lists[unitrecord::UnitRecord::kQuickItems];
            for (int i = 0; i < worn.size; ++i) {
                const char* name = worn.data[i];
                unit->AddQuickItem(ItemFactory()->CreateItem(name));
            }
            unitSpec->record.ApplyTo(unit);
            // The model's six values (UnitModelRecord) go to the unit.
            const char* prototypeName = unitSpec->prototype;
            UnitPrototypeRecord* prototype;
            {
                stlp::string key(prototypeName);
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
        } else if (type == 8) {
            lever->SetUp(&static_cast<mapspec::LeverSpec*>(message)->lever);
            object->MarkChanged(5);
        } else {
            objectManager->GiveDefaults(object);
        }
        place(object);
        object->MarkChanged(2);
        object->MarkChanged(6);
        object->MarkChanged(7);
        object->MarkChanged(8);
        if (spec->visible == 0) object->flags_ |= 8;
        object->SetUpAnimatedFigure(spec->texture);
        ObjectSlots(object)->UpdateSizeClasses();
        if (CUnit* asUnit = UnitOrNull(object)) asUnit->ServerController()->ResyncWithUnit();
        objects_.Set(static_cast<unsigned>(object->id_), object);
        mapObjects_.Set(static_cast<unsigned>(object->id_), object);
        if (object->GetClassId() != world::kClassUnit) otherMapObjects_.Set(static_cast<unsigned>(object->id_), object);
        Sectors()->Add(object);
        int result = object->id_;
        if (object->flags_ & 2) object->WriteChangedState();
        if (type >= 8 && type <= 9) {
            object->updateCountdown = UpdateCountdown(object);
            updateMap_.Set(static_cast<unsigned>(object->id_), object);
        }
        if (!object->name2.empty()) namedObjects_.Set(static_cast<unsigned>(object->id_), object);
        return result;
    }
    case 3: {   // a sound
        if (!loadedFresh_) return -1;
        auto* sound = static_cast<mapspec::SoundSpec*>(message);
        if (sound->ambient) {
            auto* ambient = timedManager->CreateObjectAs<CEffectAmbientSound>(kTimedAmbientSound, base.id);
            ambient->names_.Copy(sound->sounds);
            Copy(&ambient->localPosition_, &base.position, 12);
            ambient->volume_ = static_cast<float>(sound->volume);
            ambient->radius_ = static_cast<float>(sound->radius);
            ambient->minInterval_ = sound->minInterval;
            ambient->maxInterval_ = sound->maxInterval;
            return timed(ambient);
        }
        auto* plain = timedManager->CreateObjectAs<CEffectSound>(kTimedSound, base.id);
        if (sound->sounds.size < 1) {
            mfc::impl::MessageBoxText(worlddata::NoWaveNameText(), 0, 0);
        } else {
            const char* wave = sound->sounds.data[0];
            std::memcpy(plain->name_, wave, std::strlen(wave) + 1);
        }
        Copy(&plain->localPosition_, &base.position, 12);
        plain->volume_ = static_cast<float>(sound->volume);
        plain->radius_ = static_cast<float>(sound->radius);
        return timed(plain);
    }
    case 2: {   // a light
        if (!loadedFresh_) return -1;
        auto* lightSpec = static_cast<mapspec::LightSpec*>(message);
        auto* light = timedManager->CreateObjectAs<CEffectPointLight>(kTimedLight, base.id);
        Copy(&light->localPosition_, &base.position, 12);
        Copy(&light->range_, &lightSpec->radius, 4);
        Copy(&light->color_, &lightSpec->colour, 12);
        return timed(light);
    }
    case 4: {   // a particle source
        if (!loadedFresh_) return -1;
        auto* particleSpec = static_cast<mapspec::ParticleSpec*>(message);
        auto* particle = timedManager->CreateObjectAs<CEffectParticle>(kTimedParticle, base.id);
        Copy(&particle->localPosition_, &base.position, 12);
        Copy(&particle->type_, &particleSpec->type, 4);
        Copy(&particle->size_, &particleSpec->scale, 4);
        return timed(particle);
    }
    default:
        // The argument slot is returned untouched: the message's address.
        return reinterpret_cast<int>(message);
    }
}
EI_REWRITTEN(0x005927C0, CWorldServer::CreateObject);

EI_REWRITTEN(0x0058EF20, CWorldServer::CreateUnits);
EI_REWRITTEN(0x0058ECE0, CWorldServer::UseQuickItem);
