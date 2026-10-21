// CWorldServer: the world part of a save (mission.sav): SaveWorld writes it, LoadSave reads it
// back, LoadSavedMapObjects restores the map objects SaveMapObjects wrote, and UpdatePlayerTimes
// brings the players' "gtime" up to date before a save. See world.h and docs/notes/world.md.
#include "bink_video.h"
#include "combat.h"
#include "game_arrays.h"
#include "game_lists.h"
#include "map_object_figures.h"
#include "object_diplomacy.h"
#include "player_messages.h"
#include "party.h"
#include "world.h"

#include "ai_map.h"
#include "base_manager.h"
#include "core_streams.h"
#include "game_strings.h"
#include "item.h"
#include "map_object.h"
#include "net_delta.h"
#include "net_messages.h"
#include "net_session.h"
#include "object.h"
#include "script.h"
#include "unit.h"
#include "unit_ai.h"
#include "unit_db.h"
#include "mod_folders.h"
#include "model.h"
#include "world_effect.h"
#include "world_map_files.h"

#include "lib/crt.h"
#include "lib/mfc.h"
#include "lib/mfc_array.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "originals_ai.h"
#include "originals_core.h"
#include "originals_gameplay.h"
#include "originals_units.h"
#include "originals_world.h"
#include "originals_library.h"
#include "originals_effects.h"
#include "originals_engine.h"
#include "originals_script.h"
#include "originals_ui.h"

#include <cstring>

namespace {

// ---- Constants --------------------------------------------------------------------------------

// kMemoryStreamVtable (0073BD90) and kStreamVtable (0073BDB0, its CStream base): core_streams.h.
// The CArray<int> vtable is mfc::DWordArrayVtable, the script group's mfc::impl::ScriptGroupClass::Vtable() (set
// after 004C2030), the item link record's CItemLink::Vtable() (item.h).
constexpr std::uint32_t kStreamLimit = 2000000000;            // CStream +0x08 as constructed
constexpr int kTextKeyIdMin = 100000000;                      // ids above this are scripted (text-key) units
constexpr int kMissileSize = 0xA8;                            // operator new size of a Projectile

// Save format versions (WorldStream::version_) at which fields were added.
constexpr int kVersionTimeSpeedFloat = 0x4C;   // the time speed is a float (an int before)
constexpr int kVersionRainBoltTick = 0x74;
constexpr int kVersionCountdowns = 0x4E;
constexpr int kVersionPlayerLists = 0x55;      // a player's id list (+0x114); the object records
constexpr int kVersionPlayerByte = 0x49;       // before: a byte that is read and dropped
constexpr int kVersionPlayerGroup = 0x46;      // before: a script group that is read and dropped
constexpr int kVersionMobFiles = 0x60;
constexpr int kVersionSharedObjects = 0x6E;
constexpr int kVersionMapObjectName = 0x59;

// Progress screen stages LoadSave shows (005DA400).
constexpr int kProgressStart = 0;
constexpr int kProgressZone = 1;
constexpr int kProgressManagers = 5;
constexpr int kProgressObjects = 6;
constexpr int kProgressDone = 7;

constexpr int kSharedObjectNodeSize = 0x28;

// ---- Local layouts ----------------------------------------------------------------------------

// MFC CArray<int> (vtable 0073C3D0; SetSize 0059C660).
using IdArray = mfc::CArray<int>;

// MFC CArray of object pointers (SharedObject::objects, world.h).
using ObjectPtrArray = EngineObjectPtrs;

// The shared-object list is CWorldServer::sharedObjects_ (+0x12E60, world.h).
using SharedObjectNode = SharedObjectList::Node;
static_assert(sizeof(SharedObjectNode) == kSharedObjectNodeSize);

// A player's MFC list of plain dwords (CPlayer::recharging_, AddTail 0059CC60) and of objects
// (CPlayer::items_, AddTail 004C98F0); only head and the node layout are read here.
using DwordList = mfc::CList<std::uint32_t>;

// The item link record of an item of the item manager (CItem::link_).
CItemLink* LinkOf(CEngineObject* item) {
    return reinterpret_cast<CItemLink*>(static_cast<std::uintptr_t>(static_cast<CItem*>(item)->link_));
}

// The memory stream locals of LoadSave / SaveWorld are built in place.
void InitMemoryStream(CMemoryStream* stream) {
    std::memset(stream, 0, sizeof(*stream));
    ei::SetVtable(stream, kMemoryStreamVtable);
    stream->version_ = kStreamLimit;
    stream->ok_ = 1;
    stream->data_ = nullptr;
    stream->Reset();
}

void DestroyMemoryStream(CMemoryStream* stream) {
    ei::SetVtable(stream, kMemoryStreamVtable);
    if (stream->data_) gamecrt::operator_delete(stream->data_);
    ei::SetVtable(stream, kStreamVtable);
}

// A CMemoryStream local: built in place, its buffer freed when it goes out of scope.
struct LocalMemoryStream {
    CMemoryStream stream;
    LocalMemoryStream() { InitMemoryStream(&stream); }
    ~LocalMemoryStream() { DestroyMemoryStream(&stream); }
    LocalMemoryStream(const LocalMemoryStream&) = delete;
    LocalMemoryStream& operator=(const LocalMemoryStream&) = delete;
};

void WriteString(WorldStream* stream, const char* text) {
    stream->Write(text, static_cast<unsigned>(std::strlen(text) + 1));
}

// A dword written through a stack copy, as the original writes most scalars.
template <typename T>
void WriteCopy(WorldStream* stream, const T& value) {
    T copy = value;
    stream->Write(&copy, sizeof(T));
}

// ---- Callees not rewritten (EI_ORIGINAL forwarders; __thiscall called as __fastcall) ----------

// 00461190 (RET 4). Reads a zero-terminated string; constructs the CString at `out` and returns it.
// 00595080 (RET 4). Picks the loading movie for the zone. Patched by Rebirth: original address.
__declspec(naked) void __fastcall PickLoadingMovie(CWorldServer*, void*, const char* /*zone*/) EI_ORIGINAL(0x00595080)
// 005DA400. Shows the loading screen at `stage` (ECX).
void __fastcall ShowLoadProgress(int stage) { ShowLoadingMovie(stage); }
// 005B8B30 / 005B8B80 / 005BA830 / 005BA9D0. CAIMap::BeginRefresh / EndRefresh (ai_footprint.cpp) / CanLeaveCell /
// FindFreeCellNear (ai_map.cpp).
// 00596140 (RET 0xC). Loads the zone's AI graph state into `out` (map, .mob); non-zero when found.
// 00596730. SaveMapObjects (world_zone.cpp), through its address.
// 00403F40 (RET 4). CBaseManager::FindObject (base_manager.cpp).
// 00675CA0. Marks an object new (flags_ |= 2, stamp).
// 0067E510 / 0067E7D0 / 0067E960. Missile constructor (returns this), Load and Save: CSpellEffect's
// (Projectile is the missile's view of it).
// 0059C7A0 (RET 8). New node of the missile list (its counted pointer constructed empty).
// 00419CD0 (RET 4). RefPtr::Assign (core_refcount.cpp).
// 0040A3A0. CRefObject::Release.
// 00429EA0 (RET 8) / 00429B50 (RET 4). New node of the counted list (+0x12E44) and the counted
// pointer's assignment (reference added, old one released through slot 4).
// Load / Save of the time wheel (+0x2838), the first command queue (+0x12874), the diplomacy
// tables (+0x12C64) and the countdowns (+0x70).
__declspec(naked) void __fastcall TimeWheelLoad(void*, void*, WorldStream*) EI_ORIGINAL(0x005CAE30)
__declspec(naked) void __fastcall TimeWheelSave(void*, void*, WorldStream*) EI_ORIGINAL(0x005CAFA0)
__declspec(naked) void __fastcall CommandQueueLoad(void*, void*, WorldStream*) EI_ORIGINAL(0x004C0D00)
__declspec(naked) void __fastcall CommandQueueSave(void*, void*, WorldStream*) EI_ORIGINAL(0x004C0F90)
// 006618C0 / 00661C10 (RET 8). A player's party records, read / written.
__declspec(naked) void __fastcall PlayerRecordsLoad(void*, void*, WorldStream*, int) EI_ORIGINAL(0x006618C0)
__declspec(naked) void __fastcall PlayerRecordsSave(void*, void*, WorldStream*, int) EI_ORIGINAL(0x00661C10)
// 0055FB00 (RET 4). CWorld::FindUnit.
// 0055FBD0 (RET 8). The time of day and the time speed (as LoadSave hands them to slots 31 / 32).
// 005C1010 / 005C0DF0 (RET 4). CSectorMap::Remove / Add (sector_map.cpp) on +0x294.
// 00554450. The unit controller's reposition after a move.
// 0050EA40 / 0050E9F0 (RET 4) / 005179E0 (RET 4). Map object: prepares the record array before
// Load, MarkChanged(part), CObject3D's animated figure setup for its figure name.

} // namespace

namespace {
void ObjectPtrArrayDestruct(ObjectPtrArray* a) { mfc::impl::DestructArrayOf<gamearrays::ObjectPtrArrayBaseClass>(mfc::impl::ArrayAs<gamearrays::ObjectPtrs>(*a)); }
// 0059CA60: the shared-object list's NewNode (the element's array header built).
SharedObjectNode* SharedObjectsNewNode(SharedObjectList* list, SharedObjectNode* prev, SharedObjectNode* next) {
    return mfc::impl::NewNodeAs<mfc::impl::SharedObjectNewList>(list, prev, next);
}
} // namespace

namespace {


// Reads a string and assigns it to `target` (the temporary is destroyed at once).
void ReadStringInto(WorldStream* stream, mfc::CString& target) {
    alignas(4) unsigned char storage[sizeof(mfc::CString)];
    mfc::CString* read = stream->ReadString(storage);
    target = *read;
    read->~CString();
}

template <typename T>
T ReadValue(WorldStream* stream) {
    T value;
    stream->Read(&value, sizeof(T));
    return value;
}

} // namespace

// ---- UpdatePlayerTimes ------------------------------------------------------------------------

// The sum in the original's order ((elapsed * secondsPerTick + offset) + gtime); plain C++ maths
// (rewrite guide rule 7), compared with CheckNear.
void CWorldServer::UpdatePlayerTimes(float offset) {
    const char* name = game::GTimeText();
    int now = tick_;
    PlayerIterator it;
    for (CPlayer* player = it.First(PlayerList()); player; player = it.Next()) {
        int elapsed = static_cast<int>(static_cast<unsigned>(now) - static_cast<unsigned>(player->lastTick_));
        player->lastTick_ = now;
        if (elapsed < 0) elapsed = 0;
        double current = player->Party()->variables_.Get(name);
        float value = static_cast<float>(static_cast<double>(elapsed) * secondsPerTick_ + offset + current);
        player->Server()->SetVariable(name, std::bit_cast<std::uint32_t>(value));
    }
}

// ---- LoadSavedMapObjects ----------------------------------------------------------------------

void CWorldServer::LoadSavedMapObjects(WorldStream* stream) {
    for (;;) {
        int id = ReadValue<int>(stream);
        if (id == -1) return;
        CEngineObject* object = manager40_->FindObject(id);
        auto* mapObject = static_cast<CMapObject*>(object);
        if (stream->version_ >= kVersionMapObjectName) {
            mfc::CString name;
            ReadStringInto(stream, name);
            ObjectSlots(object)->TakePrototype(static_cast<const char*>(name));
        }
        static_cast<CMapObject*>(static_cast<void*>(object))->WriteFullState();
        object->Load(stream);
        int changed = ReadValue<int>(stream);
        if (!object) continue;   // (the object was already used above)

        ++object->refCount_;
        static_cast<CMapObject*>(object)->MarkChanged(-1);
        if (object->type_ != 0x3B) {
            static_cast<CObject3D*>((void*)object)->SetUpAnimatedFigure(mapObject->name);
            SectorMapAdd(Sectors(), nullptr, object);
        }
        objects_.Set(static_cast<unsigned>(object->id_), object);
        mapObjects_.Set(static_cast<unsigned>(object->id_), object);
        if (object->GetClassId() != world::kClassUnit)
            otherMapObjects_.Set(static_cast<unsigned>(object->id_), object);
        if (*mapObject->name2 != '\0')
            namedObjects_.Set(static_cast<unsigned>(object->id_), object);

        if (object->GetClassId() == world::kClassUnit) {
            // The unit's six dwords at +0x4B0 come from its prototype's model record (+0x2C). A
            // prototype or model out of range is read through null, as in the original.
            auto* prototype = reinterpret_cast<const UnitPrototypeRecord*>(
                unitdb::Prototypes().RecordAt(object->prototypeId_, unitdb::kPrototypeSize));
            auto* model = reinterpret_cast<const UnitModelRecord*>(
                unitdb::Models().RecordAt(static_cast<std::uint32_t>(prototype->modelIndex), unitdb::kModelSize));
            std::memcpy(static_cast<CUnit*>(object)->actionSpeeds, model->actionSpeeds, 24);
        }

        // Every part is marked changed when any change was saved (the original tests the part's
        // bit 1 << i itself, never `changed & bit`).
        int parts = mapObject->NetState()->fieldCount_;
        for (int i = 0; i < parts; ++i)
            if (changed != 0 && (1u << (i & 31)) != 0) static_cast<CMapObject*>(object)->MarkChanged(i);

        if (object->GetClassId() == world::kClassUnit &&
            reinterpret_cast<CUnitServer*>(static_cast<CUnit*>(object)->controller)->Brain()->alwaysUpdated == 0)
            continue;   // a unit whose controller does not ask for updates (its brain's +0x37)
        AddToUpdateMap(object);
    }
}

// ---- LoadSave ---------------------------------------------------------------------------------

namespace {

// The units saved standing where they cannot move (a cell they cannot leave) are put on a free
// cell nearby when they are scripted units (text-key ids), otherwise removed.
void RelocateStrandedUnits(CWorldServer* world) {
    IdArray stranded;
    stranded.vtable = mfc::impl::IntArrayClass::Vtable();
    stranded.data = nullptr;
    stranded.growBy = 0;
    stranded.maxSize = 0;
    stranded.size = 0;
    for (auto* node = world->objects_.map.First(); node; node = world->objects_.map.After(node)) {
        CEngineObject* object = node->value;
        if (object->GetClassId() != world::kClassUnit) continue;
        if (world->AIMap()->CanLeaveCell(static_cast<CUnit*>(object))) continue;
        int id = object->id_;
        int index = stranded.size;
        stranded.SetSize(index + 1, -1);
        stranded.data[index] = id;
    }
    for (int i = 0; i < stranded.size; ++i) {
        CUnit* unit = WorldFindUnit(world, nullptr, stranded.data[i]);
        if (unit && unit->id_ > kTextKeyIdMin) {
            SectorMapRemove(world->Sectors(), nullptr, unit);
            float* position = &unit->position.x;   // x, y, z
            Vector3 from;
            Vector3 to;
            std::memcpy(&from, position, sizeof(Vector3));
            std::memcpy(&to, position, sizeof(Vector3));
            world->AIMap()->FindFreeCellNear(unit, &from, &to);
            std::memcpy(position, &to, sizeof(Vector3));
            SectorMapAdd(world->Sectors(), nullptr, unit);
            unit->ServerController()->ResyncWithUnit();
        } else {
            WorldRemoveObject(world, nullptr, stranded.data[i]);
        }
    }
    stranded.vtable = mfc::impl::IntArrayClass::Vtable();
    if (stranded.data) gamecrt::operator_delete(stranded.data);
}

// The players' part of the save (in the player list's order).
void LoadPlayers(CWorldServer* world, WorldStream* stream) {
    // The original keeps this iterator on the heap.
    auto* it = static_cast<PlayerIterator*>(gamecrt::operator_new(sizeof(PlayerIterator)));
    if (it) it->manager = nullptr;
    for (CPlayer* player = it->First(world->PlayerList()); player; player = it->Next()) {
        CPlayerServer* state = player->Server();
        stream->Read(&player->partyId_, 4);
        PlayerRecordsLoad(player->Party(), nullptr, stream, 1);
        player->lastTick_ = world->tick_;
        stream->Read(&player->sessionKind100_, 4);
        CBaseManager* items = world->ItemFactory();
        for (;;) {
            int id = ReadValue<int>(stream);
            if (id == -1) break;
            auto* object = static_cast<CItem*>(items->FindObject(id));
            if (!object) continue;
            player->items_.AddTail(object);
            ++object->refCount_;
        }
        stream->Read(&player->godMode_, 1);
        if (stream->version_ >= kVersionPlayerLists) {
            DwordList& ids = player->recharging_;
            int count = ReadValue<int>(stream);
            for (int i = 0; i < count; ++i) ids.AddTail(0);   // 0059CC60
            for (auto* node = ids.head; node;) {
                auto* next = node->next;
                stream->Read(&node->data, 4);
                node = next;
            }
        }
        if (stream->version_ < kVersionPlayerByte) ReadValue<std::uint8_t>(stream);
        if (stream->version_ < kVersionPlayerGroup) {
            ScriptGroup group;
            mfc::impl::ConstructHeader<mfc::impl::ObjectRefArrayClass>(&group);
            group.vtable = mfc::impl::ScriptGroupClass::Vtable();
            group.Load(stream);
            ScriptGroupDestructOriginal((void*)&group);
        }
        state->SendParty();
        state->SetFlag70(1);
    }
    if (it) {
        if (it->manager) {
            --it->manager->refCount_;
            it->manager = nullptr;
        }
        gamecrt::operator_delete(it);
    }
}

} // namespace

int CWorldServer::LoadSave(WorldStream* stream, const char* map) {
    LocalMemoryStream savedGraph;   // the AI graph state as the zone files have it
    LocalMemoryStream graph;        // ... with the save's differences applied
    mfc::CString mobName;
    mfc::CString mapName;
    mfc::CString zoneName;

    gameResult_ = 0;
    scriptsActive_ = 0;
    if (PlayerList()) {
        PlayerIterator it;
        for (CPlayer* player = it.First(PlayerList()); player; player = it.Next()) {
            gameResult_ = 1;
            scriptsActive_ = 1;
            PlayerClearOriginal((void*)player);
        }
    }
    AIMap()->BeginRefresh();
    loaded_ = 0;
    AIMap()->refreshUnits = false;   // +0x172

    ReadStringInto(stream, mobName);
    ReadStringInto(stream, mapName);
    ReadStringInto(stream, zoneName);
    if (map) mapName = map;
    zoneName_ = zoneName;
    PickLoadingMovie(this, nullptr, zoneName);
    ShowLoadProgress(kProgressStart);
    {
        PlayerIterator it;
        for (CPlayer* player = it.First(PlayerList()); player; player = it.Next())
            player->dirty_ = 0xFFFFFFFFu;
    }

    stream->Read(windDirection_, 12);
    stream->Read(&windStrength_, 4);
    stream->Read(&nightLevel_, 4);
    for (WorldAlarm& alarm : alarms_) stream->Read(&alarm, 16);
    stream->Read(&tick_, 4);
    stream->Read(&weather_, 4);
    stream->Read(&weatherStrength_, 4);
    float time = ReadValue<float>(stream);
    float speed;
    if (stream->version_ >= kVersionTimeSpeedFloat) {
        stream->Read(&speed, 4);
        if (stream->version_ >= kVersionRainBoltTick) stream->Read(&lastRainBoltTick_, 4);
    } else {
        speed = static_cast<float>(ReadValue<int>(stream));
    }
    stream->Read(&sessionKind_, 4);

    int result;
    if (!ReadAIGraph(mapName, mobName, &savedGraph.stream)) {
        result = 0;
    } else {
        graph.stream.ApplyDiff(&savedGraph.stream, stream);
        ShowLoadProgress(kProgressZone);
        if (!LoadMap(mapName, mobName, &graph.stream, 1)) {
            result = game::FatalError(game::CantLoadSaveMapText());
        } else {
            // The map objects' identities: id, type and +0x18 (created by the manager at +0x40).
            for (;;) {
                int id = ReadValue<int>(stream);
                if (id == -1) break;
                int type = ReadValue<int>(stream);
                std::uint32_t value18 = ReadValue<std::uint32_t>(stream);
                CEngineObject* object = manager40_->CreateObject(type, id);   // slot 23
                object->prototypeId_ = value18;
            }
            manager48_->Load(stream);
            manager4C_->Load(stream);
            itemManager_->Load(stream);
            ShowLoadProgress(kProgressManagers);
            LoadSavedMapObjects(stream);
            manager48_->LoadObjects(stream);
            manager4C_->LoadObjects(stream);
            itemManager_->LoadObjects(stream);
            ShowLoadProgress(kProgressObjects);

            // Timed objects (spells, effects) by id.
            for (;;) {
                int id = ReadValue<int>(stream);
                if (id == -1) break;
                CEngineObject* object = manager48_->FindObject(id);
                ++object->refCount_;
                static_cast<CEffect*>((void*)object)->MarkChanged();
                timedObjects_.Set(static_cast<unsigned>(object->id_), object);
            }

            // Missiles: a byte 1 before each one, 0 at the end. They find the world through
            // the global at 007C84EC while they load.
            g_loadingProjectileWorld = this;
            for (;;) {
                std::uint8_t more = ReadValue<std::uint8_t>(stream);
                if (!more) break;
                void* memory = gamecrt::operator_new(kMissileSize);
                static_cast<Projectile*>(memory)->references = 0;   // written before the null test
                // (Projectile is the missile's view of the spell effect CSpellEffect::Construct builds.)
                auto* missile = memory ? reinterpret_cast<Projectile*>(static_cast<CSpellEffect*>(memory)->Construct()) : nullptr;
                reinterpret_cast<CSpellEffect*>(missile)->Load(stream);
                auto& list = projectiles_;
                ProjectileList::Node* node = projectiles_.NewNode(list.tail, nullptr);
                RefPtrAssign(&node->data, nullptr, missile);
                if (list.tail) list.tail->next = node;
                else list.head = node;
                list.tail = node;
            }

            TimeWheelLoad(timeWheel_, nullptr, stream);
            CommandQueueLoad(commandsA_, nullptr, stream);
            diplomacy_.ReadFrom(stream);
            stream->Read(&climate_, 4);
            stream->Read(&cave_, 4);
            if (stream->version_ >= kVersionCountdowns)   // the countdowns are a tween array (006D4660)
                countdowns_.LoadEntries(stream);

            // Counted references to items (added at the head of the list).
            for (;;) {
                int id = ReadValue<int>(stream);
                if (id < 0) break;
                CEngineObject* item = itemManager_->FindObject(id);
                if (!item) continue;
                auto* node = counted_.NewNode(nullptr, counted_.head);
                ObjectHolderAssign(reinterpret_cast<CEngineObject**>(&node->data), nullptr, item);
                if (counted_.head) counted_.head->prev = node;
                else counted_.tail = node;
                counted_.head = node;
            }

            if (PlayerList()) LoadPlayers(this, stream);
            stream->Read(&paused_, 1);

            // The item manager's object records.
            if (stream->version_ >= kVersionPlayerLists) {
                for (;;) {
                    int id = -1;
                    stream->Read(&id, 4);
                    if (id == -1) break;
                    std::uint32_t value = ReadValue<std::uint32_t>(stream);
                    CEngineObject* object = ItemFactory()->FindObject(id);
                    if (!object) continue;
                    auto* record = static_cast<CItemLink*>(gamecrt::operator_new(sizeof(CItemLink)));
                    // The count's whole dword is cleared, before the null test.
                    record->references = 0;
                    record->refsHigh = 0;
                    if (record) record->vtable = CItemLink::Vtable();
                    record->value = value;
                    CItemLink* old = LinkOf(object);
                    static_cast<CItem*>(object)->link_ = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(record));
                    if (record) ++record->references;
                    if (old) RefObjectRelease(old);
                }
            }

            ShowLoadProgress(kProgressDone);
            zoneName_ = zoneName;
            SetTimeSpeed(speed);   // slot 32 (virtual)
            SetTime(time);   // slot 31 (virtual)
            loaded_ = 1;
            AIMap()->EndRefresh();
            AIMap()->refreshUnits = true;

            RelocateStrandedUnits(this);

            if (stream->version_ >= kVersionMobFiles) {
                int count = ReadValue<int>(stream);
                for (int i = 0; i < count; ++i) {
                    alignas(4) unsigned char storage[sizeof(mfc::CString)];
                    mfc::CString* name = stream->ReadString(storage);
                    mobFiles_.SetAtGrow(mobFiles_.size, *name);
                    name->~CString();
                }
            } else {
                mobFiles_.SetSize(0, -1);
            }

            if (stream->version_ >= kVersionSharedObjects) {
                SharedObjectList& shared = sharedObjects_;
                int count = ReadValue<int>(stream);
                for (int i = 0; i < count; ++i) {
                    // The element is copied from a local whose first three dwords the original
                    // leaves unset; Load overwrites them.
                    SharedObject empty;
                    empty.objectId = 0;
                    empty.player = 0;
                    empty.money = 0;
                    mfc::impl::ConstructArrayOf<mfc::impl::ObjectPtrArrayClass>(empty.objects);
                    SharedObjectNode* node = SharedObjectsNewNode(&shared, shared.tail, nullptr);
                    node->data.CopyFrom(&empty);
                    if (shared.tail) shared.tail->next = node;
                    else shared.head = node;
                    shared.tail = node;
                    ObjectPtrArrayDestruct(&empty.objects);
                }
                for (SharedObjectNode* node = shared.head; node;) {
                    SharedObjectNode* next = node->next;
                    node->data.Load(stream);
                    node = next;
                }
            }

            manager40_->ReleaseUnused();
            manager48_->ReleaseUnused();
            manager4C_->ReleaseUnused();
            itemManager_->ReleaseUnused();
            result = 1;
        }
    }
    return result;   // the strings and the two memory streams are destroyed on the way out
}

// ---- SaveWorld --------------------------------------------------------------------------------

namespace {

// The players' part of the save (what LoadPlayers reads).
void SavePlayers(CWorldServer* world, WorldStream* stream) {
    // The original keeps this iterator on the heap.
    auto* it = static_cast<PlayerIterator*>(gamecrt::operator_new(sizeof(PlayerIterator)));
    if (it) it->manager = nullptr;
    for (CPlayer* player = it->First(world->PlayerList()); player; player = it->Next()) {
        stream->Write(&player->partyId_, 4);
        PlayerRecordsSave(player->Party(), nullptr, stream, 1);
        stream->Write(&player->sessionKind100_, 4);
        for (auto* node = player->items_.head; node;) {
            auto* next = node->next;
            WriteCopy(stream, node->data->id_);
            node = next;
        }
        WriteCopy(stream, -1);
        WriteCopy(stream, player->godMode_);
        DwordList& ids = player->recharging_;
        WriteCopy(stream, ids.count);
        for (auto* node = ids.head; node;) {
            auto* next = node->next;
            stream->Write(&node->data, 4);
            node = next;
        }
    }
    if (it) {
        if (it->manager) {
            --it->manager->refCount_;
            it->manager = nullptr;
        }
        gamecrt::operator_delete(it);
    }
}

// A memory stream local of SaveWorld: CStream's constructor (0047A200), then the memory stream's
// vtable and an empty buffer.
void ConstructSaveStream(CMemoryStream* stream) {
    std::memset(stream, 0, sizeof(*stream));
    stream->ConstructBase();
    ei::SetVtable(stream, kMemoryStreamVtable);
    stream->data_ = nullptr;
    stream->Reset();
}

} // namespace

void CWorldServer::SaveWorld(WorldStream* stream) {
    // The AI graph state as the zone files have it (`base`) and as it is now (`current`); only
    // their differences are saved.
    CMemoryStream base;
    ConstructSaveStream(&base);
    CMemoryStream current;
    ConstructSaveStream(&current);

    if (sessionKind_ == 1 || sessionKind_ == 3) UpdatePlayerTimes(0.0f);
    manager40_->ReleaseUnused();
    manager48_->ReleaseUnused();
    manager4C_->ReleaseUnused();
    itemManager_->ReleaseUnused();

    WriteString(stream, mobName_);
    WriteString(stream, mapName_);
    WriteString(stream, zoneName_);
    stream->Write(windDirection_, 12);
    WriteCopy(stream, windStrength_);
    WriteCopy(stream, nightLevel_);
    for (WorldAlarm& alarm : alarms_) stream->Write(&alarm, 16);
    WriteCopy(stream, tick_);
    stream->Write(&weather_, 4);
    std::uint32_t e8;
    std::memcpy(&e8, &weatherStrength_, 4);
    WriteCopy(stream, e8);
    // Stored by the callee with FSTP; copied on as dwords.
    std::uint32_t time;
    std::uint32_t speed;
    GetTime(reinterpret_cast<float*>(&time), reinterpret_cast<float*>(&speed));
    WriteCopy(stream, time);
    WriteCopy(stream, speed);
    WriteCopy(stream, lastRainBoltTick_);
    stream->Write(&sessionKind_, 4);

    ReadAIGraph(mapName_, mobName_, &base);
    reinterpret_cast<CRegionMap*>(aiLayer_)->SaveBlocks(&current);
    stream->WriteDiff(&base, &current);

    // The map objects' identities (the walk stops at the first empty entry).
    for (auto* node = mapObjects_.map.First(); node && node->value; node = mapObjects_.map.After(node)) {
        CEngineObject* object = node->value;
        WriteCopy(stream, object->id_);
        WriteCopy(stream, object->type_);
        WriteCopy(stream, object->prototypeId_);
    }
    WriteCopy(stream, -1);
    manager48_->Save(stream);
    manager4C_->Save(stream);
    itemManager_->Save(stream);
    SaveMapObjects(stream);
    manager48_->SaveObjects(stream);
    manager4C_->SaveObjects(stream);
    itemManager_->SaveObjects(stream);

    for (auto* node = timedObjects_.map.First(); node; node = timedObjects_.map.After(node))
        WriteCopy(stream, node->value->id_);
    WriteCopy(stream, -1);

    for (auto* node = projectiles_.head; node;) {
        auto* next = node->next;
        Projectile* missile = node->data;
        WriteCopy(stream, static_cast<std::uint8_t>(1));
        reinterpret_cast<CSpellEffect*>(missile)->Save(stream);
        node = next;
    }
    WriteCopy(stream, static_cast<std::uint8_t>(0));

    TimeWheelSave(timeWheel_, nullptr, stream);
    CommandQueueSave(commandsA_, nullptr, stream);
    diplomacy_.WriteTo(stream);
    stream->Write(&climate_, 4);
    stream->Write(&cave_, 4);
    countdowns_.Save(stream);   // 006D47E0

    for (auto* node = counted_.head; node;) {
        auto* next = node->next;
        WriteCopy(stream, node->data->id_);
        node = next;
    }
    WriteCopy(stream, -1);

    if (PlayerList()) SavePlayers(this, stream);
    WriteCopy(stream, paused_);

    // The item manager's object records.
    {
        PlayerIterator it;
        for (auto* item = reinterpret_cast<CEngineObject*>(it.First(ItemFactory())); item;
             item = reinterpret_cast<CEngineObject*>(it.Next())) {
            CItemLink* record = LinkOf(item);
            if (!record) continue;
            WriteCopy(stream, item->id_);
            WriteCopy(stream, record->value);
        }
        WriteCopy(stream, -1);
    }

    WriteCopy(stream, mobFiles_.size);
    for (int i = 0; i < mobFiles_.size; ++i) WriteString(stream, mobFiles_.data[i]);

    SharedObjectList& shared = sharedObjects_;
    WriteCopy(stream, shared.count);
    for (SharedObjectNode* node = shared.head; node;) {
        SharedObjectNode* next = node->next;
        WriteCopy(stream, node->data.objectId);
        WriteCopy(stream, node->data.player);
        WriteCopy(stream, node->data.money);
        int count = node->data.objects.size;
        WriteCopy(stream, count);
        for (int i = 0; i < count; ++i) WriteCopy(stream, node->data.objects.data[i]->id_);
        node = next;
    }

    ei::SetVtable(&current, kMemoryStreamVtable);
    if (current.data_) gamecrt::operator_delete(current.data_);
    ei::SetVtable(&base, kMemoryStreamVtable);
    if (base.data_) gamecrt::operator_delete(base.data_);
    ei::SetVtable(&current, kStreamVtable);
}

// ---- The shared-object list -------------------------------------------------------------------

namespace {

// 0059B8F0 (RET 4). CArray<CRefPtr>::Copy (gamearrays::ObjectPtrCopy).
void ObjectPtrArrayCopy(ObjectPtrArray* to, const ObjectPtrArray* from) {
    to->CopyFrom(*from);
}
// 0059C890 (RET 8). Releases `count` counted pointers from `data` (CArray DestructElements).
void __stdcall CountedRangeRelease(CEngineObject** a0, int a1) { gamearrays::ObjectPtrs::DestructElements(a0, a1); }
// 00403F40 (RET 4). The item manager's (007AFFB4, WorldItemManager) object by id, or null.

// SetAtGrow(size, object) on a CArray<CRefPtr>: the new slot holds a reference.
void Append(ObjectPtrArray& array, CEngineObject* object) {
    int index = array.size;
    mfc::impl::ArrayAs<gamearrays::ObjectPtrs>(array).SetSize(index + 1, -1);   // 0059C8C0
    CEngineObject* old = array.data[index];
    array.data[index] = object;
    if (object) ++object->refCount_;
    if (old) old->Release();
}

} // namespace

SharedObject* SharedObject::CopyFrom(const SharedObject* from) {
    objectId = from->objectId;
    player = from->player;
    money = from->money;
    ObjectPtrArrayCopy(&objects, &from->objects);
    return this;
}
EI_REWRITTEN(0x0059B510, SharedObject::CopyFrom);

SharedObject* SharedObject::DeletingDestruct(unsigned flags) {
    mfc::impl::DestructArrayOf<gamearrays::ObjectPtrArrayBaseClass>(mfc::impl::ArrayAs<gamearrays::ObjectPtrs>(objects));
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REWRITTEN(0x0059DDE0, SharedObject::DeletingDestruct);

void SharedObject::Load(WorldStream* stream) {
    SharedObject* entry = this;
    stream->Read(&entry->objectId, 4);
    stream->Read(&entry->player, 4);
    stream->Read(&entry->money, 4);
    int count = ReadValue<int>(stream);
    for (int i = 0; i < count; ++i) {
        int id = ReadValue<int>(stream);
        CEngineObject* object = WorldItemManager()->FindObject(id);
        if (object) Append(entry->objects, object);   // the original inlines the SetSize
    }
}

void CWorldServer::AddSharedObject(int id, int /*unused*/, std::uint32_t value, const mfc::CTypedPtrList<CItem>* objects) {
    // The entry's player: the session player record's +0x30 in a multiplayer game, else -1.
    int player = -1;
    net::ServerSession* session = net::GameServer();
    if (session && !(session->flags7_ & CRefCounted::kDetached))
        if (auto* record = static_cast<net::PlayerMessage*>(session->FindPlayer(id))) player = record->serial_;   // 0041A2E0

    // AddHead(SharedObject()): the new head copies an empty local (whose first three dwords the
    // original leaves unset; they are overwritten below).
    SharedObjectList& shared = sharedObjects_;
    ObjectPtrArray empty{mfc::impl::ObjectPtrArrayClass::Vtable(), nullptr, 0, 0, 0};
    SharedObjectNode* node = SharedObjectsNewNode(&shared, nullptr, shared.head);
    ObjectPtrArrayCopy(&node->data.objects, &empty);
    if (shared.head) shared.head->prev = node;
    else shared.tail = node;
    shared.head = node;
    empty.vtable = gamearrays::ObjectPtrArrayBaseClass::Vtable();
    if (empty.data) {
        CountedRangeRelease(empty.data, empty.size);
        gamecrt::operator_delete(empty.data);
    }

    SharedObject& entry = shared.head->data;
    entry.objectId = id;
    entry.player = player;
    entry.money = value;
    for (auto* it = objects->head; it;) {
        auto* next = it->next;
        Append(entry.objects, it->data);
        it = next;
    }
}

// ---- PrepareSave --------------------------------------------------------------------------------

namespace {

// 00525AF0. UnitStats::RefillSlots (unit_vitals.cpp), on the controller's stats (+0x1E0).

} // namespace

void CWorldServer::PrepareSave() {
    for (auto* node = mapObjects_.map.First(); node && node->value; node = mapObjects_.map.After(node)) {
        CEngineObject* unit = node->value;
        if (unit->GetClassId() != world::kClassUnit || ObjectSlots(unit)->IsDead()) continue;   // slot 55
        CEngineObject* controller = static_cast<CUnit*>(unit)->controller;
        UnitStatsRefillSlotsOriginal(&reinterpret_cast<CUnitServer*>(controller)->stats);
        static_cast<CUnitController*>(controller)->RefreshVitals(0);   // slot 19 (+0x4C)
    }
}

// ---- NewZone (00595480, A-W14) ----------------------------------------------------------------

namespace {

constexpr const char* kCantLoadGraph = "CWorldServer::LoadMap.\n Can't load AIGraph.\n";
constexpr const char* kCantLoadMap = "CWorldServer::NewZone()\n Can't load map.\n";

// Whether the file `name` is neither there as named nor in the maps folder (Rebirth's Maps names it
// first: `site`).
bool MapFileMissing(const char* name, modfolders::MapsSite site) {
    if (libcrt::Access(name, 0) != -1) return false;
    mfc::CString folder;
    modfolders::SelectMaps(name, site);
    MapsFolderAt(&folder);
    mfc::CString path = folder + name;
    return libcrt::Access(path, 0) == -1;
}

} // namespace

int CWorldServer::NewZone(const char* map, const char* mob, const char* zone) {
    cave_ = 0;
    PickLoadingMovie(this, nullptr, zone);
    ShowLoadProgress(kProgressStart);
    if (map && MapFileMissing(map, modfolders::MapsSite::NewZoneMap)) return 0;
    if (mob && MapFileMissing(mob, modfolders::MapsSite::NewZoneMob)) return 0;
    LocalMemoryStream graph;
    if (!ReadAIGraph(map, mob, &graph.stream))
        return game::FatalError(kCantLoadGraph);
    mobFiles_.SetSize(0, -1);
    AIMap()->BeginRefresh();
    loaded_ = 0;
    std::memset(alarms_, 0, sizeof(alarms_));
    AIMap()->refreshUnits = false;
    if (!LoadMap(map, mob, &graph.stream, 0))
        return game::FatalError(kCantLoadMap);
    ShowLoadProgress(kProgressObjects);
    loaded_ = 1;
    AIMap()->EndRefresh();
    AIMap()->refreshUnits = true;
    ShowLoadProgress(kProgressDone);
    if (zone) zoneName_ = zone;
    scriptsActive_ = 0;
    return 1;
}
EI_REPLACE_METHOD(0x00595480, CWorldServer::NewZone);

EI_REWRITTEN(0x005956E0, CWorldServer::PrepareSave);
EI_REWRITTEN(0x00589CA0, SharedObject::Load);
EI_REWRITTEN(0x005947E0, CWorldServer::AddSharedObject);
EI_REWRITTEN(0x00596E70, CWorldServer::LoadSave);
EI_REWRITTEN(0x00597BD0, CWorldServer::SaveWorld);
EI_REWRITTEN(0x00596900, CWorldServer::LoadSavedMapObjects);
EI_REWRITTEN(0x0058D280, CWorldServer::UpdatePlayerTimes);
