// CWorldServer::CreateUnits (0058EF20): creates units from unit records (script AddUnitToServer,
// the party entering a zone, mercenaries), places them, gives them their equipment and AI, and
// adds the heroes to the "Heroes" script group. See world.h and docs/notes/world.md.
#include "ai_map.h"
#include "core_streams.h"
#include "lever.h"
#include "magic_trap.h"
#include "map_object_figures.h"
#include "object_diplomacy.h"
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
#include "lib/mfc.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "ei/x87.h"
#include "lib/nan_audit.h"
#include "lib/mfc_window.h"
#include "originals_ai.h"
#include "originals_library.h"
#include "originals_units.h"
#include "originals_world.h"
#include "originals_effects.h"
#include "originals_gameplay.h"
#include "originals_script.h"
#include "originals_core.h"
#include "game_data.h"
#include "ei/vcall.h"

#include <cstring>

namespace {

using mapspec::ObjectSpec;
using mapspec::Spec;
using mapspec::UnitSpec;

constexpr int kTypeHero = 0x32;                        // the map objects manager's type of hero prototypes
constexpr int kKindSinglePlayer = 1;
constexpr int kKindNoRangeTest = 3;
constexpr int kGameResultLost = 2;

// The placement of one unit (player_messages.h).
using Placement = UnitPlacement;
using PlacementArray = mfc::CArray<Placement>;

// ---- Callees not rewritten (EI_ORIGINAL forwarders; __thiscall called as __fastcall) ----------

__declspec(naked) void __fastcall MessageDestructBase(void*, void*) EI_ORIGINAL(0x0046AEB0)
// 006E859F. `eh vector destructor iterator` (array, element size, count, destructor).
void __stdcall VectorDestruct(void* a0, unsigned a1, int a2, void* a3) { libcrt::DestroyElements(a0, a1, a2, reinterpret_cast<libcrt::ElementFunction>(a3)); }
// 00513240 (RET 4). The map objects manager's type of a prototype name.
// The unit's AI brain (CUnitServer::brain, controller + 0x980).
// Placement on the maps.
// The unit's setup.
// 005308D0 (RET 4): the record's text 6 (a CString's characters, handed on as a dword).
// Items: the counted record at item + 0x1C (ItemSourceRef, item.h).
// 004D23A0 (RET 8). Queues a script command line on the first command queue.

CBaseManager* Manager(CEngineObject* manager) { return static_cast<CBaseManager*>(manager); }
CUnit* AsUnit(CEngineObject* object) { return static_cast<CUnit*>(object); }

// ---- The creation message ---------------------------------------------------------------------

// The inlined destructor of the message: its own members, then the base's (0046AEB0).
void DestroyMessage(UnitSpec* message) {
    ei::SetVtable(message, worlddata::kUnitSpecVtable);
    for (int i = 4; i >= 0; --i) message->lists[i].Destruct();
    message->prototype.~CString();
    VectorDestruct(message->behaviour, sizeof(mapspec::Behaviour), 5,
                   reinterpret_cast<void*>(worlddata::kBehaviourDestructor));
    MessageDestructBase(message, nullptr);
}

// The hero's figure parts, "hr" replaced by its numbered style (none when the style is -1).
void FillHeroParts(UnitSpec* message, const char* prototypeName, mfc::CString& scratch) {
    UnitPrototypeRecord* prototype;
    {
        prototype = unitdb::PrototypeNamed(stlp::string(prototypeName));   // find_or_insert of {name, null} (00514880)
    }
    int hairStyle = prototype ? prototype->hair : 1;

    mfc::CStringArray* parts = &message->object.parts;
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

    Spec& spec = message->object.base;
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

// An item from an item record, with its counted record (+0x1C) holding the item record's id.
// `refValueBase`: the record is built through the reference value constructor (00419720) and the
// vtable stored after it, instead of 0058FBB0.
CEngineObject* MakeItem(CWorldServer* world, CEngineObject* itemRecord, bool refValueBase) {
    auto* item = static_cast<CItem*>(world->DuplicateItem(itemRecord));
    auto* memory = static_cast<ItemSourceRef*>(gamecrt::operator_new(sizeof(ItemSourceRef)));
    memory->refs = 0;   // written before the null test
    ItemSourceRef* record = nullptr;
    if (memory) {
        if (refValueBase) {
            RefValueConstructBase(memory);
            memory->vtable = ei::GamePointer<void>(worlddata::kItemSourceRefVtable);
            record = memory;
        } else {
            record = reinterpret_cast<ItemSourceRef*>(reinterpret_cast<CItemLink*>(memory)->Construct());
        }
    }
    record->itemId = static_cast<std::uint32_t>(itemRecord->id_);
    RefPtrAssign(&item->link_, nullptr, record);
    return item;
}

} // namespace

void CWorldServer::CreateUnits(void* player, void* recordsArg, const Vector3* position, Quaternion rotation,
                               void* placementsArg) {
    auto* records = static_cast<RecordPtrArray*>(recordsArg);
    auto* placements = static_cast<PlacementArray*>(placementsArg);
    // Destroyed after the two strings (declared after it), as the original does. Plain storage:
    // the original's constructor (0046C010) and inlined destructor build and tear it down.
    struct Message {
        alignas(4) std::uint8_t bytes[sizeof(UnitSpec)];
        UnitSpec* Get() { return reinterpret_cast<UnitSpec*>(bytes); }
        Message() { mapspec::UnitSpecConstruct(Get()); }   // 0046C010
        ~Message() { DestroyMessage(Get()); }
    } holder;
    UnitSpec* message = holder.Get();
    ObjectSpec& object = message->object;
    mapspec::Spec& base = object.base;
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
        auto* record = reinterpret_cast<PartyUnitRecord*>(records->data[i]);
        message->prototype = record->texts[5];
        object.texture = record->texts[2];
        message->behaviour[0].type = 4;
        message->behaviour[0].enabled = 1;
        message->behaviour[0].flags = 0;
        base.kind = 5;
        std::memcpy(&object.scale, &record->scaleX, 12);
        object.visible = 1;
        object.side = 0;
        object.parentIname = record->texts[3];
        object.skinTexture = record->texts[4];
        object.parts.SetSize(0, -1);
        const char* prototypeName = record->texts[5];
        int type = PrototypeObjectClass(prototypeName);
        object.parentId = type;   // +0x50: the object's type
        if (type == kTypeHero) FillHeroParts(message, prototypeName, line);

        int id = static_cast<int>(record->unitId);
        base.id = id;
        if (ObjectWithId(id)) WorldRemoveObject(this, nullptr, base.id);

        Placement* placement = nullptr;
        if (placements) {
            for (int p = 0; p < placements->size; ++p) {
                if (placements->data[p].id == base.id) {
                    placement = &placements->data[p];
                    break;
                }
            }
        }
        if (placement) {
            std::memcpy(&base.position, placement->position, 12);
            std::memcpy(&base.rotation, placement->rotation, 16);
        } else {
            PlaceInLine(message, position, rotation, i);
        }

        CEngineObject* unit = WorldFindUnit(this, nullptr, WorldMakeObjectOriginal(this, nullptr, message));
        if (!unit) continue;
        CMapObjectVirtuals* slots = ObjectSlots(unit);

        // A player-controlled AI, standing on a free cell near the requested position.
        CEngineObject* controller = AsUnit(unit)->controller;
        void* brain = &static_cast<CUnitServer*>(controller)->brain;
        static_cast<AIBrain*>(brain)->ClearModes();
        void* memory = gamecrt::operator_new(0x24);
        static_cast<std::uint32_t*>(memory)[1] = 0;   // +4, written before the null test
        static_cast<AIBrain*>(brain)->AddMode((memory ? static_cast<PlayerMode*>(memory)->Construct() : nullptr));
        static_cast<AIBrain*>(brain)->StopAction();
        SectorMapRemove(AsSectorMap(sectorMap_), nullptr, unit);
        Vector3 free;
        std::memcpy(&free, &AsUnit(unit)->position.x, sizeof(free));
        AIMap()->FindFreeCellNear(AsUnit(unit), position, &free);
        std::memcpy(&AsUnit(unit)->position.x, &free, sizeof(free));
        SectorMapAdd(AsSectorMap(sectorMap_), nullptr, unit);

        static_cast<CUnit*>(unit)->SetPlayer(reinterpret_cast<char*>(player));
        slots->SetScriptName(record->texts[0]);
        static_cast<CUnit*>(unit)->SetUnknown438(static_cast<std::uint32_t>(record->fightAtWill));
        static_cast<CMapObject*>(unit)->ClearProtectionBases();
        slots->SetStats(reinterpret_cast<const UnitStats*>(record->stats));
        AsUnit(unit)->ReleaseAllItems();

        // Equipment: weapons, then the controller's slot 25 (ShowWeapon 0), armour
        // (UnitEquipArmourOriginal), quick items (two fields copied from the record's), spells.
        for (int n = 0; n < static_cast<int>(record->weaponCount); ++n)
            static_cast<CUnit*>(unit)->AddWeapon(static_cast<CEngineObject*>(MakeItem(this, record->weapons[n], false)));
        AsUnit(unit)->Controller()->ShowWeapon(0);
        for (int n = 0; n < static_cast<int>(record->armorCount); ++n)
            static_cast<CUnit*>(unit)->EquipArmour(MakeItem(this, record->armor[n], true));
        for (int n = 0; n < static_cast<int>(record->quickItemCount); ++n) {
            auto* item = static_cast<CItem*>(MakeItem(this, record->quickItems[n], true));
            item->count_ = static_cast<CItem*>(record->quickItems[n])->count_;
            std::memcpy(&item->charge_, &item->chargeMax_, 4);
            static_cast<CUnit*>(unit)->AddQuickItem(item);
        }
        for (int n = 0; n < static_cast<int>(record->spellCount); ++n)
            static_cast<CUnit*>(unit)->AddSpell(MakeItem(this, record->spells[n], true));

        slots->UpdateSizeClasses();
        slots->ScaleEffectChanged(0);
        AsUnit(unit)->SetPrototype23CByName(record->texts[6]);
        line.Format(worlddata::AddToHeroesFormat(), unit->id_);
        script += line;
        if (sessionKind_ == kKindSinglePlayer && !atStart) AsUnit(unit)->moveMode2 = record->moveMode2;
        if (placement) {
            bool flagged = AIMap()->IsPassableForClass(1, &AsUnit(unit)->position);
            if (flagged) static_cast<CUnit*>(unit)->SetMoveModes(placement->value20, placement->value20);
            else AsUnit(unit)->moveMode2 = static_cast<int>(placement->value20);
        }
        static_cast<CUnitServer*>(controller)->ResyncWithUnit();
        if (sessionKind_ == kKindSinglePlayer || sessionKind_ == kKindNoRangeTest)
            AsUnit(unit)->pendingWeapon = static_cast<int>(record->weaponIndex);
        if (placement && sessionKind_ != kKindNoRangeTest) AsUnit(unit)->pendingWeapon = static_cast<int>(placement->value24);
    }

    // Followers follow their leaders.
    if (sessionKind_ == kKindSinglePlayer) {
        for (int i = 0; i < records->size; ++i) {
            auto* record = reinterpret_cast<PartyUnitRecord*>(records->data[i]);
            CEngineObject* unit = WorldFindUnit(this, nullptr, static_cast<int>(record->unitId));
            if (!unit) continue;
            CEngineObject* controller = AsUnit(unit)->controller;
            if (record->leaderId <= 0) continue;
            void* brain = &static_cast<CUnitServer*>(controller)->brain;
            void* follow = static_cast<AIBrain*>(brain)->FindPlayerMode();
            if (!follow) continue;
            static_cast<PlayerMode*>(follow)->SetLeader(static_cast<AIBrain*>(brain), WorldFindUnit(this, nullptr, record->leaderId));
        }
    }

    if (atStart && gameResult_ != kGameResultLost) script += worlddata::HeroArrivalText();
    script += worlddata::ScriptEndText();
    if (player) ScriptLoadLine(commandsA_, nullptr, &script, 1);
}

// ---- UseQuickItem -------------------------------------------------------------------------------

namespace {

constexpr int kQuickEffectType = 9;
constexpr std::uint32_t kQuickEffectFlags = 0x3010;
constexpr std::uint32_t kFlagsNewEffect = 0x180000;            // 00529D20

} // namespace

void CWorldServer::UseQuickItem(void* /*item*/, CEngineObject* unit) {
    alignas(4) std::uint8_t storage[sizeof(ObjectSpec)];
    auto* message = reinterpret_cast<ObjectSpec*>(storage);
    message->ObjectSpecConstruct();
    ei::SetVtable(message, worlddata::kQuickEffectSpecVtable);
    message->base.kind = kQuickEffectType;
    message->texture = worlddata::QuickEffectPrototype();
    message->base.id = -1;
    message->parentIname = worlddata::QuickEffectModel();
    message->parentId = static_cast<std::int32_t>(kQuickEffectFlags);
    std::memset(&message->scale.y, 0, 4);
    // At the unit's position, on the ground.
    std::memcpy(&message->base.position, &static_cast<CMapObject*>(unit)->position.x, 12);
    std::memset(&message->base.position.z, 0, 4);
    int id = WorldMakeObjectOriginal(this, nullptr, message);
    CEngineObject* effect = ObjectWithId(id);
    effect->flags_ |= kFlagsNewEffect;   // 00529D20, inlined
    MessageDestructBase(message, nullptr);
}

// ---- CreateObject (005927C0) -------------------------------------------------------------------

namespace {

// Object setup, through the original addresses (thiscall as fastcall).
__declspec(naked) void __fastcall ManagerRegister(void*, void*, CEngineObject*) EI_ORIGINAL(0x00513630)
__declspec(naked) void __fastcall ManagerRegisterUnit(void*, void*, CEngineObject*, const char*) EI_ORIGINAL(0x00513B60)
void __fastcall UnitSetDiplomacy(CEngineObject* a0, void*, int a2) { static_cast<CUnit*>((void*)a0)->SetHostileMask((std::uint32_t)a2); }

// The timed objects' types (world_effect.h).
constexpr int kTimedAmbientSound = 0x49, kTimedSound = 0x48, kTimedLight = 0x4B, kTimedParticle = 0x4A;
constexpr std::uint32_t kGrey = 0x3F4CCCCD, kOne = 0x3F800000;
constexpr std::uint32_t kTorchLightKind = 0x2001;   // the torch's smoke particles

int UpdateCountdown(const CEngineObject* object) { return CWorldServer::UpdateCountdownFor(object); }

// Copies dwords (the original's REP MOVSD / dword moves; floats are not loaded on the FPU).
void Copy(void* to, const void* from, std::size_t n) {
    auto* d = static_cast<std::uint32_t*>(to);
    auto* s = static_cast<const std::uint32_t*>(from);
    for (std::size_t i = 0; i < n / 4; ++i) d[i] = s[i];
}

// The map object's CString name (+0xA4) as the string it is.
mfc::CString* NameString(CMapObject* object) { return reinterpret_cast<mfc::CString*>(&object->name); }

} // namespace

int CWorldServer::CreateObject(std::uint8_t* message) {
    // Every kind starts with the base record (Spec); kinds 1, 5-9 are object records (ObjectSpec),
    // 5 a unit record, 6 a flame, 7 a magic trap, 8 a lever; 2 a light, 3 a sound, 4 a particle
    // source.
    auto* spec = reinterpret_cast<ObjectSpec*>(message);
    Spec& base = spec->base;
    auto created = [this, spec]() {
        return static_cast<CMapObject*>(Manager(MapObjectManager())->CreateObject(spec->parentId, spec->base.id));
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
        const char* prototype = spec->base.kind == 5 ? static_cast<const char*>(reinterpret_cast<UnitSpec*>(spec)->prototype)
                                                     : static_cast<const char*>(spec->primaryTexture);
        ObjectSlots(object)->TakePrototype(prototype);
        static_cast<CMapObject*>((void*)object)->WriteFullState();
    };
    auto place = [spec](CMapObject* object) {
        ObjectSetFigureScaleOriginal(object, nullptr, &spec->scale.x);
        Copy(&object->baseScale, &spec->scale, 12);
        ObjectSlots(object)->ApplyBaseScale();
        Copy(&object->position.x, &spec->base.position, 12);
        Copy(&object->orientation, &spec->base.rotation, 16);
    };
    auto timed = [this](CEffect* object) {
        object->startTick_ = tick_;
        ++object->refCount_;
        static_cast<CEffect*>((void*)object)->MarkChanged();
        timedObjects_.Set(static_cast<unsigned>(object->id_), object);
        return object->id_;
    };
    CBaseManager* timedManager = Manager(manager48_);

    switch (base.kind) {
    case 6: {   // a torch (flame) with its state object (a campfire when it stands on the ground)
        auto* flame = reinterpret_cast<mapspec::FlameSpec*>(message);
        CMapObject* object = created();
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
            CEngineObject* spell = reinterpret_cast<CEngineObject*>(reinterpret_cast<CSpellManager*>(SpellManager())->CreateSpellFromText(worlddata::CampfireText()));
            if (spell) ++spell->refCount_;
            CastSpell(spell, nullptr, const_cast<const Vector3*>(&at), nullptr,
                                   const_cast<const Vector3*>(&at), 0);
            if (spell) spell->Release();
        }
        base.object = object;
        CTorchObjectSpecSlots* torch = TorchSpecSlots(static_cast<CTorchObject*>(object)->spec);
        auto* companion = static_cast<CMapObjectServer*>(object->companion);
        *NameString(object) = spec->texture;
        companionTexts(companion);
        race(object);
        torch->SetLightSlot(kTorchLightKind, &flame->offset.x, flame->volume);
        std::uint32_t colour[4] = {kGrey, kGrey, kGrey, kOne};
        float radius = flame->volume * worlddata::TorchLightScale();
        torch->SetFlameSlot(&flame->offset.x, radius, reinterpret_cast<const float*>(colour));
        torch->SetNameSlot(flame->sound);
        place(object);
        static_cast<CMapObject*>(object)->MarkChanged(2);
        static_cast<CMapObject*>(object)->MarkChanged(4);
        if (spec->visible == 0) object->flags_ |= 8;
        ManagerRegister(MapObjectManager(), nullptr, object);
        if (object->GetClassId() == world::kClassUnit) static_cast<CUnitServer*>(AsUnit(object)->controller)->ResyncWithUnit();
        static_cast<CObject3D*>((void*)object)->SetUpAnimatedFigure(spec->texture);
        id = object->id_;
        objects_.Set(static_cast<unsigned>(id), object);
        mapObjects_.Set(static_cast<unsigned>(object->id_), object);
        if (object->GetClassId() != world::kClassUnit) otherMapObjects_.Set(static_cast<unsigned>(object->id_), object);
        object->updateCountdown = UpdateCountdown(object);
        updateMap_.Set(static_cast<unsigned>(object->id_), object);
        SectorMapAdd(AsSectorMap(sectorMap_), nullptr, object);
        int result = object->id_;
        if (object->flags_ & 2) static_cast<CMapObject*>(object)->WriteChangedState();
        return result;
    }
    case 7: {   // a magic trap: updated every tick (no texts, not in the sector map)
        CMapObject* object = created();
        if (!allowed(object)) return refuse(object);
        int id = object->id_;
        if (id == -1) return -1;
        if (base.id == -1) base.id = id;
        base.object = object;
        ++object->refCount_;
        static_cast<CMapObject*>((void*)object)->WriteFullState();
        static_cast<CMapObject*>(object)->MarkChanged(2);
        object->flags_ |= 8;
        objects_.Set(static_cast<unsigned>(object->id_), object);
        mapObjects_.Set(static_cast<unsigned>(object->id_), object);
        if (object->GetClassId() != world::kClassUnit) otherMapObjects_.Set(static_cast<unsigned>(object->id_), object);
        object->updateCountdown = UpdateCountdown(object);
        updateMap_.Set(static_cast<unsigned>(object->id_), object);
        int result = object->id_;
        if (object->flags_ & 2) static_cast<CMapObject*>(object)->WriteChangedState();
        static_cast<MagicTrap*>(object)->SetUp(reinterpret_cast<const MagicTrapRecord*>(&reinterpret_cast<mapspec::TrapSpec*>(message)->diplomacy));
        return result;
    }
    case 1:
    case 5:
    case 8:
    case 9: {   // map objects, units (5) and levers (8)
        CMapObject* object = created();
        if (spec->shadow) object->flags_ |= 0x20000;
        if (gamecrt::mbscmp(static_cast<const char*>(spec->model),
                            game::EmptyText()) != 0)
            object->flags_ |= 0x20000;
        if (!allowed(object)) return refuse(object);
        int id = object->id_;
        if (id == -1) return -1;
        if (base.id == -1) base.id = id;
        base.object = object;
        auto* companion = static_cast<CMapObjectServer*>(object->companion);
        *NameString(object) = spec->texture;
        object->SetSecondName(spec->model);
        companionTexts(companion);
        race(object);
        ObjectSlots(object)->ScaleEffectChanged(0);
        int type = base.kind;
        if (type == 5) {
            auto* unitSpec = reinterpret_cast<UnitSpec*>(message);
            static_cast<CUnit*>(object)->SetPlayer(nullptr);
            reinterpret_cast<CUnit*>(object)->SetSide(spec->side);
            UnitSetDiplomacy(object, nullptr, diplomacy_.GetHostileMask(spec->side));
            ManagerRegisterUnit(MapObjectManager(), nullptr, object, unitSpec->prototype);
            reinterpret_cast<CMapObjectManager*>(MapObjectManager())->GiveStartingEquipment(static_cast<CUnit*>(object), unitSpec->prototype);
            const mfc::CStringArray& worn = unitSpec->lists[1];
            for (int i = 0; i < worn.size; ++i) {
                const char* name = worn.data[i];
                static_cast<CUnit*>(object)->AddQuickItem(ItemManagerCreateItemOriginal(ItemFactory(), nullptr, name));
            }
            reinterpret_cast<unitrecord::UnitRecord*>(unitSpec->stats)->ApplyTo(static_cast<CUnit*>(object));
            // The model's six values (UnitModelRecord +0x2C) go to the unit's +0x4B0.
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
                // Rebirth's FixAddMapObject (context hook at 00592F68): no prototype record is
                // logged ("Invalid unit's prototype") and index 0 used. The original reads +8 of
                // the null record (and crashes).
                if (!ei::rebirth::Enabled(ei::rebirth::Patch::FixAddMapObject))
                    index = *reinterpret_cast<const volatile unsigned*>(reinterpret_cast<std::uintptr_t>(prototype) + 8);
                else
                    index = 0;
            }
            auto* model = reinterpret_cast<const UnitModelRecord*>(unitdb::Models().RecordAt(index, unitdb::kModelSize));
            std::memcpy(AsUnit(object)->actionSpeeds, model->actionSpeeds, 24);
            static_cast<CMapObject*>(object)->MarkChanged(5);
            static_cast<CMapObject*>(object)->MarkChanged(6);
            static_cast<CMapObject*>(object)->MarkChanged(0xB);
        } else if (type == 8) {
            static_cast<Lever*>(object)->SetUp(reinterpret_cast<const LeverSetup*>(reinterpret_cast<mapspec::LeverSpec*>(message)->stats));
            static_cast<CMapObject*>(object)->MarkChanged(5);
        } else {
            ManagerRegister(MapObjectManager(), nullptr, object);
        }
        place(object);
        static_cast<CMapObject*>(object)->MarkChanged(2);
        static_cast<CMapObject*>(object)->MarkChanged(6);
        static_cast<CMapObject*>(object)->MarkChanged(7);
        static_cast<CMapObject*>(object)->MarkChanged(8);
        if (spec->visible == 0) object->flags_ |= 8;
        static_cast<CObject3D*>((void*)object)->SetUpAnimatedFigure(spec->texture);
        ObjectSlots(object)->UpdateSizeClasses();
        if (object->GetClassId() == world::kClassUnit) static_cast<CUnitServer*>(AsUnit(object)->controller)->ResyncWithUnit();
        objects_.Set(static_cast<unsigned>(object->id_), object);
        mapObjects_.Set(static_cast<unsigned>(object->id_), object);
        if (object->GetClassId() != world::kClassUnit) otherMapObjects_.Set(static_cast<unsigned>(object->id_), object);
        SectorMapAdd(AsSectorMap(sectorMap_), nullptr, object);
        int result = object->id_;
        if (object->flags_ & 2) static_cast<CMapObject*>(object)->WriteChangedState();
        if (type >= 8 && type <= 9) {
            object->updateCountdown = UpdateCountdown(object);
            updateMap_.Set(static_cast<unsigned>(object->id_), object);
        }
        if (*object->name2 != 0) namedObjects_.Set(static_cast<unsigned>(object->id_), object);
        return result;
    }
    case 3: {   // a sound
        if (!loadedFresh_) return -1;
        auto* sound = reinterpret_cast<mapspec::SoundSpec*>(message);
        if (sound->ambient) {
            auto* ambient = static_cast<CEffectAmbientSound*>(timedManager->CreateObject(kTimedAmbientSound, base.id));
            ambient->names_.Copy(sound->sounds);
            Copy(&ambient->localPosition_, &base.position, 12);
            ambient->volume_ = static_cast<float>(sound->volume);
            ambient->radius_ = static_cast<float>(sound->radius);
            ambient->minInterval_ = sound->minInterval;
            ambient->maxInterval_ = sound->maxInterval;
            return timed(ambient);
        }
        auto* plain = static_cast<CEffectSound*>(timedManager->CreateObject(kTimedSound, base.id));
        if (sound->sounds.size < 1) {
            mfc::impl::MessageBoxText(worlddata::NoWaveNameText(), 0, 0);
        } else {
            // strcpy, inlined in the original (REP MOVS of the length with its terminator).
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
        auto* lightSpec = reinterpret_cast<mapspec::LightSpec*>(message);
        auto* light = static_cast<CEffectPointLight*>(timedManager->CreateObject(kTimedLight, base.id));
        Copy(&light->localPosition_, &base.position, 12);
        Copy(&light->range_, &lightSpec->radius, 4);
        Copy(&light->color_, &lightSpec->colour, 12);
        return timed(light);
    }
    case 4: {   // a particle source
        if (!loadedFresh_) return -1;
        auto* particleSpec = reinterpret_cast<mapspec::ParticleSpec*>(message);
        auto* particle = static_cast<CEffectParticle*>(timedManager->CreateObject(kTimedParticle, base.id));
        Copy(&particle->localPosition_, &base.position, 12);
        Copy(&particle->type_, &particleSpec->type, 4);
        Copy(&particle->size_, &particleSpec->scale, 4);
        return timed(particle);
    }
    default:
        // The original returns its argument slot untouched: the message's address.
        return reinterpret_cast<int>(message);
    }
}
EI_REPLACE_METHOD(0x005927C0, CWorldServer::CreateObject);

EI_REPLACE_METHOD(0x0058EF20, CWorldServer::CreateUnits);
EI_REPLACE_METHOD(0x0058ECE0, CWorldServer::UseQuickItem);
