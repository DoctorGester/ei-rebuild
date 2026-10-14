// CWorldServer::CreateUnits (0058EF20): creates units from unit records (script AddUnitToServer,
// the party entering a zone, mercenaries), places them, gives them their equipment and AI, and
// adds the heroes to the "Heroes" script group. See world.h and docs/notes/world.md.
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
#include "originals_ai.h"
#include "originals_library.h"
#include "originals_units.h"
#include "originals_world.h"
#include "originals_effects.h"
#include "originals_gameplay.h"
#include "originals_script.h"
#include "originals_core.h"

#include <cstring>

// The placement maths keeps the original's operation order: no reassociation in this file.
#pragma float_control(precise, on, push)

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

__declspec(naked) void* __fastcall MessageConstruct(void*, void*) EI_ORIGINAL(0x0046C010)
__declspec(naked) void __fastcall MessageDestructBase(void*, void*) EI_ORIGINAL(0x0046AEB0)
void __fastcall StringArraySetAtGrowText(void* a0, void*, int a2, const char* a3) { StringArraySetAtGrowTextOriginal(a0, nullptr, a2, a3); }
void __fastcall StringArraySetAtGrow(void* a0, void*, int a2, const mfc::CString* a3) { StringArraySetAtGrowStringOriginal(a0, nullptr, a2, a3); }
// 006E859F. `eh vector destructor iterator` (array, element size, count, destructor).
void __stdcall VectorDestruct(void* a0, unsigned a1, int a2, void* a3) { EhVectorDestructorIterator(a0, a1, a2, a3); }
// 00513240 (RET 4). The map objects manager's type of a prototype name.
// The prototype table's name map (007AF364): an STLport string key (0040C5A0 / 0040C720), the
// pair (005729D0 builds {key, value}) and the insert (00514880, returns the stored pair).
struct RawString {
    std::uint8_t bytes[12];
};
// An entry of the name map: the STLport string key and the prototype.
struct PrototypeNamePair {
    RawString key;                     // +0x00
    UnitPrototypeRecord* prototype;    // +0x0C
};
static_assert(sizeof(PrototypeNamePair) == 0x10);
static RawString* RawStringInit(RawString* s, void*, const char* text, void* alloc) { return static_cast<RawString*>(::StringInitFromChars(s, nullptr, text, alloc)); }
__declspec(naked) void* __fastcall NamePairBuild(void* /*out*/, void*, const RawString*, void* const* /*value*/)
    EI_ORIGINAL(0x005729D0)
PrototypeNamePair* __fastcall NameMapInsert(void* a0, void*, const void* a2) { return static_cast<PrototypeNamePair*>(NameMapInsertOriginal(a0, nullptr, a2)); }
// 005927C0 (RET 4). Creates the unit described by the message; returns its id. Patched by Rebirth:
// original address.
__declspec(naked) int __fastcall CreateObjectAt(CWorldServer*, void*, void* /*message*/) EI_ORIGINAL(0x005927C0)
// _CIacos (006E7320): argument and result in ST0.
__declspec(naked) double __cdecl CIacos(double) {
    __asm fld qword ptr [esp + 4]
    __asm mov eax, 0x006E7320
    __asm call eax
    __asm ret
}
// The unit's AI brain (CUnitServer::brain, controller + 0x980).
void __fastcall BrainClearModes(void* a0, void*) { BrainClearModesOriginal(a0); }
void* __fastcall PlayerModeConstruct(void* a0, void*) { return ModePlayerConstructOriginal(a0); }
void __fastcall BrainAddMode(void* a0, void*, void* a2) { BrainAddModeOriginal(a0, nullptr, a2); }
__declspec(naked) void* __fastcall BrainFindFollowMode(void*, void*) EI_ORIGINAL(0x005D5E60)
__declspec(naked) void __fastcall FollowModeSetLeader(void*, void*, void* /*brain*/, CEngineObject*) EI_ORIGINAL(0x005D3DE0)
// Placement on the maps.
void __fastcall ControllerReposition(void* a0, void*) { ControllerRepositionOriginal(a0); }
// The unit's setup.
inline void __fastcall UnitSetController(CEngineObject* a0, void*, void* a2) { UnitSetControllerOriginal((void*)a0, nullptr, a2); }
__declspec(naked) void __fastcall UnitSet780(CEngineObject*, void*, std::uint32_t) EI_ORIGINAL(0x0052E9B0)
__declspec(naked) void __fastcall UnitRefreshStats(CEngineObject*, void*) EI_ORIGINAL(0x0050F080)
void __fastcall UnitGiveItem(CEngineObject* a0, void*, CEngineObject* a2) { UnitAddWeaponOriginal((void*)a0, nullptr, (void*)a2); }
// 005308D0 (RET 4): the record's text 6 (a CString's characters, handed on as a dword).
__declspec(naked) void __fastcall UnitSetPose(CEngineObject*, void*, std::uint32_t, std::uint32_t) EI_ORIGINAL(0x0052EB10)
// Items: the counted record at item + 0x1C (ItemSourceRef, item.h).
// 004D23A0 (RET 8). Queues a script command line on the first command queue.
int __fastcall CommandQueueAdd(void* a0, void*, const mfc::CString* a2, int a3) { return ScriptLoadLine(a0, nullptr, a2, a3); }

CBaseManager* Manager(CEngineObject* manager) { return static_cast<CBaseManager*>(manager); }
CUnit* AsUnit(CEngineObject* object) { return static_cast<CUnit*>(object); }

// ---- The creation message ---------------------------------------------------------------------

// The inlined destructor of the message: its own members, then the base's (0046AEB0).
void DestroyMessage(UnitSpec* message) {
    *reinterpret_cast<std::uintptr_t*>(message) = worlddata::kUnitSpecVtable;
    for (int i = 4; i >= 0; --i) StringArrayDestruct(&message->lists[i]);
    message->prototype.~CString();
    VectorDestruct(message->behaviour, sizeof(mapspec::Behaviour), 5,
                   reinterpret_cast<void*>(worlddata::kBehaviourDestructor));
    MessageDestructBase(message, nullptr);
}

// The hero's figure parts, "hr" replaced by its numbered style (none when the style is -1).
void FillHeroParts(UnitSpec* message, const char* prototypeName, mfc::CString& scratch) {
    RawString key;
    char allocator;
    RawStringInit(&key, nullptr, prototypeName, &allocator);
    void* none = nullptr;
    PrototypeNamePair pair;
    NamePairBuild(&pair, nullptr, &key, &none);
    PrototypeNamePair* stored = NameMapInsert(worlddata::PrototypeNameMap(), nullptr, &pair);
    StlStringDestroy(&pair);
    UnitPrototypeRecord* prototype = stored->prototype;
    StlStringDestroy(&key);
    int hairStyle = prototype ? prototype->hair : 1;

    mfc::CStringArray* parts = &message->object.parts;
    for (int n = 0; n < worlddata::kHeroPartCount; ++n) {
        const char* part = worlddata::HeroPartText(n);
        if (std::strcmp(part, worlddata::HairPartText()) == 0) {
            if (hairStyle != -1) {
                scratch.Format(worlddata::HairFormat(), hairStyle);
                StringArraySetAtGrow(parts, nullptr, parts->size, &scratch);
            }
        } else {
            StringArraySetAtGrowText(parts, nullptr, parts->size, part);
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
    double axis = static_cast<double>(ry) * ry + static_cast<double>(rz) * rz + static_cast<double>(rw) * rw;
    double angle;
    if (!(axis > worlddata::AxisEpsilon())) {
        angle = worlddata::Zero();
    } else {
        float axisStored = StoreFloat(static_cast<float>(axis));
        angle = CIacos(rx);
        angle = angle + angle;
        double sign = worlddata::One() / x87::fsqrt(axisStored) * rw;
        if (!(sign >= worlddata::Zero())) angle = -angle;
    }
    angle = angle + worlddata::LineAngleOffset();

    Spec& spec = message->object.base;
    float zStep = 0.0f;
    std::memcpy(&spec.position, position, 12);
    std::memcpy(&spec.rotation, &rotation, 16);
    float cosine = StoreFloat(static_cast<float>(x87::fcos(angle)));
    double sine = x87::fsin(angle);
    float step = StoreFloat(static_cast<float>(index));
    double dy = sine * step;
    float dx = StoreFloat(static_cast<float>(step * static_cast<double>(cosine)));
    float dz = StoreFloat(static_cast<float>(static_cast<double>(zStep) * worlddata::LineSpacing()));
    dy = dy * worlddata::LineSpacing();
    float& x = spec.position.x;
    float& y = spec.position.y;
    float& z = spec.position.z;
    x = StoreFloat(static_cast<float>(static_cast<double>(dx) * worlddata::LineSpacing() + x));
    y = StoreFloat(static_cast<float>(y + dy));
    z = StoreFloat(static_cast<float>(static_cast<double>(z) + dz));
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
            RefValueConstructOriginal(memory);
            memory->vtable = reinterpret_cast<void*>(worlddata::kItemSourceRefVtable);
            record = memory;
        } else {
            record = static_cast<ItemSourceRef*>(ItemRecordConstruct(memory, nullptr));
        }
    }
    record->itemId = static_cast<std::uint32_t>(itemRecord->id_);
    RefPtrAssign(&item->unknown_1C, nullptr, record);
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
        Message() { MessageConstruct(bytes, nullptr); }
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
        double distance = dx * dx + dy * dy + dz * dz;
        atStart = !(distance >= worlddata::HeroStartRadius2());
    }
    script = worlddata::ScriptBeginText();

    for (int i = 0; i < records->size; ++i) {
        auto* record = reinterpret_cast<PartyUnitRecord*>(records->data[i]);
        message->prototype = record->texts[5];
        object.texture = record->texts[2];
        message->behaviour[0].type = 4;
        message->behaviour[0].unknown_38 = 1;
        message->behaviour[0].flags = 0;
        base.kind = 5;
        std::memcpy(&object.scale, &record->unknown_724, 12);
        object.visible = 1;
        object.unknown_6E = 0;
        object.parentIname = record->texts[3];
        object.unknown_74 = record->texts[4];
        StringArraySetSize(&object.parts, nullptr, 0, -1);
        const char* prototypeName = record->texts[5];
        int type = PrototypeClass(MapObjectManager(), nullptr, prototypeName);
        object.parentId = type;   // +0x50: the object's type
        if (type == kTypeHero) FillHeroParts(message, prototypeName, line);

        int id = static_cast<int>(record->unknown_01C);
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

        CEngineObject* unit = WorldFindUnit(this, nullptr, CreateObjectAt(this, nullptr, message));
        if (!unit) continue;
        CMapObjectVirtuals* slots = ObjectSlots(unit);

        // A player-controlled AI, standing on a free cell near the requested position.
        CEngineObject* controller = AsUnit(unit)->controller;
        void* brain = static_cast<CUnitServer*>(controller)->brain;
        BrainClearModes(brain, nullptr);
        void* memory = gamecrt::operator_new(0x24);
        static_cast<std::uint32_t*>(memory)[1] = 0;   // +4, written before the null test
        BrainAddMode(brain, nullptr, memory ? PlayerModeConstruct(memory, nullptr) : nullptr);
        BrainStopActionOriginal(brain);
        SectorMapRemove(AsSectorMap(sectorMap_), nullptr, unit);
        Vector3 free;
        std::memcpy(&free, &AsUnit(unit)->position.x, sizeof(free));
        FindFreeCellNearOriginal(aiMap_, nullptr, unit, position, &free);
        std::memcpy(&AsUnit(unit)->position.x, &free, sizeof(free));
        SectorMapAdd(AsSectorMap(sectorMap_), nullptr, unit);

        UnitSetController(unit, nullptr, player);
        slots->SetScriptName(record->texts[0]);
        UnitSet780(unit, nullptr, static_cast<std::uint32_t>(record->unknown_780));
        UnitRefreshStats(unit, nullptr);
        slots->SetStats(reinterpret_cast<const UnitStats*>(record->stats));
        UnitReleaseAllItems(AsUnit(unit));

        // Equipment: weapons, then the controller's slot 25 (ShowWeapon 0), armour
        // (UnitEquipArmourOriginal), quick items (two fields copied from the record's), spells.
        for (int n = 0; n < static_cast<int>(record->weaponCount); ++n)
            UnitGiveItem(unit, nullptr, MakeItem(this, record->weapons[n], false));
        AsUnit(unit)->Controller()->ShowWeapon(0);
        for (int n = 0; n < static_cast<int>(record->armorCount); ++n)
            UnitEquipArmourOriginal(unit, nullptr, MakeItem(this, record->armor[n], true));
        for (int n = 0; n < static_cast<int>(record->quickItemCount); ++n) {
            auto* item = static_cast<CItem*>(MakeItem(this, record->quickItems[n], true));
            item->unknown_48 = static_cast<CItem*>(record->quickItems[n])->unknown_48;
            std::memcpy(&item->charge_, &item->chargeMax_, 4);
            UnitAddQuickItemOriginal(unit, nullptr, item);
        }
        for (int n = 0; n < static_cast<int>(record->spellCount); ++n)
            UnitAddSpellOriginal(unit, nullptr, MakeItem(this, record->spells[n], true));

        slots->UpdateSizeClasses();
        slots->ScaleEffectChanged(0);
        UnitSetPrototype23CByName(AsUnit(unit), nullptr, record->texts[6]);
        line.Format(worlddata::AddToHeroesFormat(), unit->id_);
        script += line;
        if (unknown12D64_ == kKindSinglePlayer && !atStart) AsUnit(unit)->moveMode2 = record->unknown_770;
        if (placement) {
            bool flagged = IsPassableForClassOriginal(aiMap_, nullptr, 1, &AsUnit(unit)->position);
            if (flagged) UnitSetPose(unit, nullptr, placement->value20, placement->value20);
            else AsUnit(unit)->moveMode2 = static_cast<int>(placement->value20);
        }
        ControllerReposition(controller, nullptr);
        if (unknown12D64_ == kKindSinglePlayer || unknown12D64_ == kKindNoRangeTest)
            AsUnit(unit)->pendingWeapon = static_cast<int>(record->weaponIndex);
        if (placement && unknown12D64_ != kKindNoRangeTest) AsUnit(unit)->pendingWeapon = static_cast<int>(placement->value24);
    }

    // Followers follow their leaders.
    if (unknown12D64_ == kKindSinglePlayer) {
        for (int i = 0; i < records->size; ++i) {
            auto* record = reinterpret_cast<PartyUnitRecord*>(records->data[i]);
            CEngineObject* unit = WorldFindUnit(this, nullptr, static_cast<int>(record->unknown_01C));
            if (!unit) continue;
            CEngineObject* controller = AsUnit(unit)->controller;
            if (record->unknown_77C <= 0) continue;
            void* brain = static_cast<CUnitServer*>(controller)->brain;
            void* follow = BrainFindFollowMode(brain, nullptr);
            if (!follow) continue;
            FollowModeSetLeader(follow, nullptr, brain, WorldFindUnit(this, nullptr, record->unknown_77C));
        }
    }

    if (atStart && gameResult_ != kGameResultLost) script += worlddata::HeroArrivalText();
    script += worlddata::ScriptEndText();
    if (player) CommandQueueAdd(commandsA_, nullptr, &script, 1);
}

// ---- UseQuickItem -------------------------------------------------------------------------------

namespace {

constexpr int kQuickEffectType = 9;
constexpr std::uint32_t kQuickEffectFlags = 0x3010;
constexpr std::uint32_t kFlagsNewEffect = 0x180000;            // 00529D20

__declspec(naked) void* __fastcall BaseMessageConstruct(void*, void*) EI_ORIGINAL(0x0046A5C0)

} // namespace

void CWorldServer::UseQuickItem(void* /*item*/, CEngineObject* unit) {
    alignas(4) std::uint8_t storage[sizeof(ObjectSpec)];
    auto* message = reinterpret_cast<ObjectSpec*>(storage);
    BaseMessageConstruct(message, nullptr);
    *reinterpret_cast<std::uintptr_t*>(message) = worlddata::kQuickEffectSpecVtable;
    message->base.kind = kQuickEffectType;
    message->texture = worlddata::QuickEffectPrototype();
    message->base.id = -1;
    message->parentIname = worlddata::QuickEffectModel();
    message->parentId = static_cast<std::int32_t>(kQuickEffectFlags);
    std::memset(&message->scale.y, 0, 4);
    // At the unit's position, on the ground.
    std::memcpy(&message->base.position, &static_cast<CMapObject*>(unit)->position.x, 12);
    std::memset(&message->base.position.z, 0, 4);
    int id = CreateObjectAt(this, nullptr, message);
    CEngineObject* effect = ObjectWithId(id);
    effect->flags_ |= kFlagsNewEffect;   // 00529D20, inlined
    MessageDestructBase(message, nullptr);
}

#pragma float_control(pop)

// ---- CreateObject (005927C0) -------------------------------------------------------------------

namespace {

// Object setup, through the original addresses (thiscall as fastcall).
void __fastcall ObjectMarkNew(CEngineObject* a0, void*) { ObjectPrepareOriginal((void*)a0); }
void __fastcall ObjectAnnounce(CEngineObject* a0, void*) { MapObjectSendDeltaOriginal(a0); }
void __fastcall ObjectSetName(CEngineObject* a0, void*, const char* a2) { ObjectSetupFigureOriginal((void*)a0, nullptr, a2); }
__declspec(naked) void __fastcall ObjectSetSecondName(CEngineObject*, void*, const char*) EI_ORIGINAL(0x0050F150)
__declspec(naked) void __fastcall ManagerRegister(void*, void*, CEngineObject*) EI_ORIGINAL(0x00513630)
__declspec(naked) void __fastcall ManagerRegisterUnit(void*, void*, CEngineObject*, const char*) EI_ORIGINAL(0x00513B60)
__declspec(naked) void __fastcall ManagerRegisterUnit2(void*, void*, CEngineObject*, const char*) EI_ORIGINAL(0x005141F0)
inline void __fastcall TextAssignChars(mfc::CString* s, void*, const char* t) { CStringAssignTextOriginal(s, nullptr, t); }
void __fastcall TextAssign(mfc::CString* a0, void*, const mfc::CString* a2) { CStringAssignOriginal((void*)a0, nullptr, (const void*)a2); }
void __fastcall TextArrayCopy(void* a0, void*, const void* a2) { StringArrayCopyOriginal(a0, nullptr, a2); }
void __fastcall UnitSetDiplomacy(CEngineObject* a0, void*, int a2) { UnitSetHostileMaskOriginal((void*)a0, nullptr, (std::uint32_t)a2); }
__declspec(naked) void __fastcall UnitRecordApply(void*, void*, CEngineObject*) EI_ORIGINAL(0x00477B00)
UnitPrototypeRecord** __fastcall UnitPrototypeAt(void* a0, void*, const void* a2) { return (UnitPrototypeRecord**)PrototypeMapAtOriginal(a0, nullptr, a2); }
__declspec(naked) void __fastcall ObjectSetup8(CEngineObject*, void*, const void*) EI_ORIGINAL(0x0052B3F0)
__declspec(naked) void __fastcall ObjectSetup7(CEngineObject*, void*, const void*) EI_ORIGINAL(0x0052A2C0)
void __fastcall MarkNewObject(CEngineObject* a0, void*) { ObjectMarkNewOriginal((void*)a0); }

// The timed objects' types (world_effect.h).
constexpr int kTimedAmbientSound = 0x49, kTimedSound = 0x48, kTimedLight = 0x4B, kTimedParticle = 0x4A;
constexpr std::uint32_t kGrey = 0x3F4CCCCD, kOne = 0x3F800000;
constexpr std::uint32_t kTorchLightKind = 0x2001;   // the torch's smoke particles

int UpdateCountdown(const CEngineObject* object) { return ((reinterpret_cast<int>(object) >> 5) & 0xF) + 0x80; }

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
        TextAssign(&companion->name78, nullptr, &spec->parentIname);
        TextAssign(&companion->name7C, nullptr, &spec->unknown_74);
        TextArrayCopy(&companion->stringsVtable, nullptr, &spec->parts);
    };
    auto race = [spec](CMapObject* object) {
        ++object->refCount_;
        const char* prototype = spec->base.kind == 5 ? static_cast<const char*>(reinterpret_cast<UnitSpec*>(spec)->prototype)
                                                     : static_cast<const char*>(spec->primaryTexture);
        ObjectSlots(object)->TakePrototype(prototype);
        ObjectMarkNew(object, nullptr);
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
        MarkNewObject(object, nullptr);
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
        volatile float height = flame->offset.z + base.position.z;
        float sum = height;
        std::uint32_t bits;
        std::memcpy(&bits, &sum, 4);
        bits &= 0x7FFFFFFFu;
        float level;
        std::memcpy(&level, &bits, 4);
        if (x87::IsNaN(level) || level < worlddata::CampfireHeight()) {
            volatile float at[3];
            at[2] = flame->offset.z + base.position.z;
            at[1] = flame->offset.y + base.position.y;
            at[0] = flame->offset.x + base.position.x;
            CEngineObject* spell = reinterpret_cast<CEngineObject*>(SpellManagerCreateSpellFromTextOriginal(
                reinterpret_cast<CSpellManager*>(SpellManager()), nullptr, worlddata::CampfireText()));
            if (spell) ++spell->refCount_;
            WorldCastSpellOriginal(this, nullptr, spell, nullptr, reinterpret_cast<const Vector3*>(const_cast<float*>(at)), nullptr,
                                   reinterpret_cast<const Vector3*>(const_cast<float*>(at)), 0);
            if (spell) spell->Release();
        }
        base.object = object;
        CTorchObjectSpecSlots* torch = TorchSpecSlots(static_cast<CTorchObject*>(object)->spec);
        auto* companion = static_cast<CMapObjectServer*>(object->companion);
        TextAssignChars(NameString(object), nullptr, spec->texture);
        companionTexts(companion);
        race(object);
        torch->SetLightSlot(kTorchLightKind, &flame->offset.x, flame->volume);
        std::uint32_t colour[4] = {kGrey, kGrey, kGrey, kOne};
        float radius = flame->volume * worlddata::TorchLightScale();
        torch->SetFlameSlot(&flame->offset.x, radius, reinterpret_cast<const float*>(colour));
        torch->SetNameSlot(flame->sound);
        place(object);
        UnitMarkChanged(object, nullptr, 2);
        UnitMarkChanged(object, nullptr, 4);
        if (spec->visible == 0) object->flags_ |= 8;
        ManagerRegister(MapObjectManager(), nullptr, object);
        if (object->GetClassId() == world::kClassUnit) ControllerReposition(AsUnit(object)->controller, nullptr);
        ObjectSetName(object, nullptr, spec->texture);
        id = object->id_;
        objects_.Set(static_cast<unsigned>(id), object);
        mapObjects_.Set(static_cast<unsigned>(object->id_), object);
        if (object->GetClassId() != world::kClassUnit) otherMapObjects_.Set(static_cast<unsigned>(object->id_), object);
        object->unknown_174 = UpdateCountdown(object);
        updateMap_.Set(static_cast<unsigned>(object->id_), object);
        SectorMapAdd(AsSectorMap(sectorMap_), nullptr, object);
        int result = object->id_;
        if (object->flags_ & 2) ObjectAnnounce(object, nullptr);
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
        ObjectMarkNew(object, nullptr);
        UnitMarkChanged(object, nullptr, 2);
        object->flags_ |= 8;
        objects_.Set(static_cast<unsigned>(object->id_), object);
        mapObjects_.Set(static_cast<unsigned>(object->id_), object);
        if (object->GetClassId() != world::kClassUnit) otherMapObjects_.Set(static_cast<unsigned>(object->id_), object);
        object->unknown_174 = UpdateCountdown(object);
        updateMap_.Set(static_cast<unsigned>(object->id_), object);
        int result = object->id_;
        if (object->flags_ & 2) ObjectAnnounce(object, nullptr);
        ObjectSetup7(object, nullptr, &reinterpret_cast<mapspec::TrapSpec*>(message)->diplomacy);
        return result;
    }
    case 1:
    case 5:
    case 8:
    case 9: {   // map objects, units (5) and levers (8)
        CMapObject* object = created();
        if (spec->shadow) object->flags_ |= 0x20000;
        if (gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(static_cast<const char*>(spec->model)),
                            reinterpret_cast<const unsigned char*>(game::EmptyText())) != 0)
            object->flags_ |= 0x20000;
        if (!allowed(object)) return refuse(object);
        int id = object->id_;
        if (id == -1) return -1;
        if (base.id == -1) base.id = id;
        base.object = object;
        auto* companion = static_cast<CMapObjectServer*>(object->companion);
        TextAssignChars(NameString(object), nullptr, spec->texture);
        ObjectSetSecondName(object, nullptr, spec->model);
        companionTexts(companion);
        race(object);
        ObjectSlots(object)->ScaleEffectChanged(0);
        int type = base.kind;
        if (type == 5) {
            auto* unitSpec = reinterpret_cast<UnitSpec*>(message);
            UnitSetController(object, nullptr, nullptr);
            UnitSetSide(reinterpret_cast<CUnit*>(object), nullptr, spec->unknown_6E);
            UnitSetDiplomacy(object, nullptr, DiplomacyHostileMask(reinterpret_cast<CDiplomacy*>(diplomacy_), nullptr, spec->unknown_6E));
            ManagerRegisterUnit(MapObjectManager(), nullptr, object, unitSpec->prototype);
            ManagerRegisterUnit2(MapObjectManager(), nullptr, object, unitSpec->prototype);
            const mfc::CStringArray& worn = unitSpec->lists[1];
            for (int i = 0; i < worn.size; ++i) {
                const char* name = worn.data[i];
                UnitAddQuickItemOriginal(object, nullptr, ItemManagerCreateItemOriginal(ItemFactory(), nullptr, name));
            }
            UnitRecordApply(unitSpec->stats, nullptr, object);
            // The model's six values (UnitModelRecord +0x2C) go to the unit's +0x4B0.
            const char* prototypeName = unitSpec->prototype;
            UnitPrototypeRecord* prototype;
            {
                alignas(4) std::uint8_t key[12];
                char allocator;
                StringInitWithAllocator(key, nullptr, &allocator);
                StringAssignSpan(key, nullptr, prototypeName, prototypeName + std::strlen(prototypeName));
                prototype = *UnitPrototypeAt(worlddata::PrototypeNameMap(), nullptr, key);
                StlStringDestroy(key);
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
            auto* model = reinterpret_cast<const UnitModelRecord*>(unitdb::Models().At(index, unitdb::kModelSize));
            std::memcpy(AsUnit(object)->actionSpeeds, model->actionSpeeds, 24);
            UnitMarkChanged(object, nullptr, 5);
            UnitMarkChanged(object, nullptr, 6);
            UnitMarkChanged(object, nullptr, 0xB);
        } else if (type == 8) {
            ObjectSetup8(object, nullptr, reinterpret_cast<mapspec::LeverSpec*>(message)->stats);
            UnitMarkChanged(object, nullptr, 5);
        } else {
            ManagerRegister(MapObjectManager(), nullptr, object);
        }
        place(object);
        UnitMarkChanged(object, nullptr, 2);
        UnitMarkChanged(object, nullptr, 6);
        UnitMarkChanged(object, nullptr, 7);
        UnitMarkChanged(object, nullptr, 8);
        if (spec->visible == 0) object->flags_ |= 8;
        ObjectSetName(object, nullptr, spec->texture);
        ObjectSlots(object)->UpdateSizeClasses();
        if (object->GetClassId() == world::kClassUnit) ControllerReposition(AsUnit(object)->controller, nullptr);
        objects_.Set(static_cast<unsigned>(object->id_), object);
        mapObjects_.Set(static_cast<unsigned>(object->id_), object);
        if (object->GetClassId() != world::kClassUnit) otherMapObjects_.Set(static_cast<unsigned>(object->id_), object);
        SectorMapAdd(AsSectorMap(sectorMap_), nullptr, object);
        int result = object->id_;
        if (object->flags_ & 2) ObjectAnnounce(object, nullptr);
        if (type >= 8 && type <= 9) {
            object->unknown_174 = UpdateCountdown(object);
            updateMap_.Set(static_cast<unsigned>(object->id_), object);
        }
        if (*object->name2 != 0) namedObjects_.Set(static_cast<unsigned>(object->id_), object);
        return result;
    }
    case 3: {   // a sound
        if (!loadedFresh_) return -1;
        auto* sound = reinterpret_cast<mapspec::SoundSpec*>(message);
        if (sound->unknown_84) {
            auto* ambient = static_cast<CEffectAmbientSound*>(timedManager->CreateObject(kTimedAmbientSound, base.id));
            TextArrayCopy(&ambient->names_, nullptr, &sound->sounds);
            Copy(&ambient->localPosition_, &base.position, 12);
            ambient->volume_ = static_cast<float>(sound->unknown_68);
            ambient->radius_ = static_cast<float>(sound->unknown_6C);
            ambient->unknown_168 = sound->unknown_74;
            ambient->unknown_16C = sound->unknown_78;
            return timed(ambient);
        }
        auto* plain = static_cast<CEffectSound*>(timedManager->CreateObject(kTimedSound, base.id));
        if (sound->sounds.size < 1) {
            AfxMessageBox(worlddata::NoWaveNameText(), 0, 0);
        } else {
            // strcpy, inlined in the original (REP MOVS of the length with its terminator).
            const char* wave = sound->sounds.data[0];
            std::memcpy(plain->name_, wave, std::strlen(wave) + 1);
        }
        Copy(&plain->localPosition_, &base.position, 12);
        plain->volume_ = static_cast<float>(sound->unknown_68);
        plain->radius_ = static_cast<float>(sound->unknown_6C);
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
EI_REBIRTH_IMPLEMENTS(0x005927C0, ei::rebirth::Patch::FixAddMapObject);

EI_REPLACE_METHOD(0x0058EF20, CWorldServer::CreateUnits);
EI_REPLACE_METHOD(0x0058ECE0, CWorldServer::UseQuickItem);
