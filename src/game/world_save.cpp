// CWorldServer: the world part of a save (mission.sav): SaveWorld writes it, LoadSave reads it
// back, LoadSavedMapObjects restores the map objects SaveMapObjects wrote, and UpdatePlayerTimes
// brings the players' "gtime" up to date before a save. See world.h and docs/notes/world.md.
#include "bink_video.h"
#include "combat.h"
#include "core_refcount.h"
#include "game_arrays.h"
#include "game_lists.h"
#include "map_object_figures.h"
#include "object_diplomacy.h"
#include "player_messages.h"
#include "party.h"
#include "sector_map.h"
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

#include <cstring>
#include <memory>
#include <string>

namespace {

// ---- Constants --------------------------------------------------------------------------------

// The CArray<int> vtable is mfc::impl::IntArrayClass::Vtable(), the script group's is
// mfc::impl::ScriptGroupClass::Vtable().
constexpr int kTextKeyIdMin = 100000000;                      // ids above this are scripted (text-key) units

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

// Progress screen stages LoadSave shows.
constexpr int kProgressStart = 0;
constexpr int kProgressZone = 1;
constexpr int kProgressManagers = 5;
constexpr int kProgressObjects = 6;
constexpr int kProgressDone = 7;

constexpr int kSharedObjectNodeSize = 0x28;

// ---- Local layouts ----------------------------------------------------------------------------

// MFC CArray<int>.
using IdArray = mfc::CArray<int>;

// MFC CArray of counted item pointers (SharedObject::objects, world.h).
using ObjectPtrArray = CountedPtrs<CItem>;

// The shared-object list is CWorldServer::sharedObjects_ (+0x12E60, world.h).
using SharedObjectNode = SharedObjectList::Node;
static_assert(sizeof(SharedObjectNode) == kSharedObjectNodeSize);

// A player's MFC list of plain dwords (CPlayer::recharging_) and of objects (CPlayer::items_); only the head and
// the node layout are read here.
using DwordList = mfc::CList<std::uint32_t>;

void WriteString(WorldStream* stream, const char* text) {
    stream->Write(text, static_cast<unsigned>(std::strlen(text) + 1));
}

// A dword written through a stack copy, as most scalars are.
template <typename T>
void WriteCopy(WorldStream* stream, const T& value) {
    T copy = value;
    stream->Write(&copy, sizeof(T));
}

// ---- Helpers ----------------------------------------------------------------------------------

// Reads a zero-terminated string; constructs the CString at `out` and returns it.
// Shows the loading screen at `stage` (ECX).
void __fastcall ShowLoadProgress(int stage) { ShowLoadingMovie(stage); }

} // namespace

namespace {
void ObjectPtrArrayDestruct(ObjectPtrArray* a) { mfc::impl::DestructArrayOf<gamearrays::ObjectPtrArrayBaseClass>(mfc::impl::ArrayAs<gamearrays::ObjectPtrs>(*a)); }
// The shared-object list's NewNode (the element's array header built).
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
    std::destroy_at(read);
}

template <typename T>
T ReadValue(WorldStream* stream) {
    T value;
    stream->Read(&value, sizeof(T));
    return value;
}

} // namespace

// ---- UpdatePlayerTimes ------------------------------------------------------------------------

// The sum in the game's order ((elapsed * secondsPerTick + offset) + gtime); plain C++ maths, compared with
// CheckNear.
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


void CWorldServer::LoadSavedMapObjects(WorldStream* stream) {
    for (;;) {
        int id = ReadValue<int>(stream);
        if (id == -1) return;
        CMapObject* object = ObjectSlots(manager40_->FindObject(id));   // a map object (manager40_)
        if (stream->version_ >= kVersionMapObjectName) {
            mfc::CString name;
            ReadStringInto(stream, name);
            object->TakePrototype(name.GetString());
        }
        object->WriteFullState();
        object->Load(stream);
        int changed = ReadValue<int>(stream);
        if (!object) continue;   // (the object was already used above)

        ++object->refCount_;
        object->MarkChanged(-1);
        if (object->type_ != 0x3B) {
            object->SetUpAnimatedFigure(object->name);
            Sectors()->Add(object);
        }
        objects_.Set(static_cast<unsigned>(object->id_), object);
        mapObjects_.Set(static_cast<unsigned>(object->id_), object);
        if (object->GetClassId() != world::kClassUnit)
            otherMapObjects_.Set(static_cast<unsigned>(object->id_), object);
        if (*object->name2 != '\0')
            namedObjects_.Set(static_cast<unsigned>(object->id_), object);

        if (CUnit* unit = UnitOrNull(object)) {
            // The unit's six dwords at +0x4B0 come from its prototype's model record (+0x2C). A prototype or
            // model out of
            // range is read through null.
            auto* prototype = unitdb::PrototypeRecord(unit->prototypeId_);
            auto* model = unitdb::ModelRecord(static_cast<std::uint32_t>(prototype->modelIndex));
            std::memcpy(unit->actionSpeeds, model->actionSpeeds, 24);
        }

        // Every part is marked changed when any change was saved (the part's bit 1 << i is tested itself, never
        // `changed & bit`).
        int parts = object->NetState()->fieldCount_;
        for (int i = 0; i < parts; ++i)
            if (changed != 0 && (1u << (i & 31)) != 0) object->MarkChanged(i);

        CUnit* unit = UnitOrNull(object);
        if (unit && unit->ServerController()->Brain()->alwaysUpdated == 0)
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
        CUnit* unit = UnitOrNull(node->value);
        if (!unit) continue;
        if (world->AIMap()->CanLeaveCell(unit)) continue;
        int id = unit->id_;
        int index = stranded.size;
        stranded.SetSize(index + 1, -1);
        stranded.data[index] = id;
    }
    for (int i = 0; i < stranded.size; ++i) {
        CUnit* unit = static_cast<CWorld*>(world)->FindUnit(stranded.data[i]);
        if (unit && unit->id_ > kTextKeyIdMin) {
            world->Sectors()->Remove(unit);
            float* position = &unit->localPosition_.x;   // x, y, z
            Vector3 from;
            Vector3 to;
            std::memcpy(&from, position, sizeof(Vector3));
            std::memcpy(&to, position, sizeof(Vector3));
            world->AIMap()->FindFreeCellNear(unit, &from, &to);
            std::memcpy(position, &to, sizeof(Vector3));
            world->Sectors()->Add(unit);
            unit->ServerController()->ResyncWithUnit();
        } else {
            static_cast<CWorldServer*>(world)->RemoveObject(stranded.data[i]);
        }
    }
    stranded.vtable = mfc::impl::IntArrayClass::Vtable();
    if (stranded.data) gamecrt::operator_delete(stranded.data);
}

// The players' part of the save (in the player list's order).
void LoadPlayers(CWorldServer* world, WorldStream* stream) {
    // The iterator lives on the heap.
    auto* it = gamecrt::operator_new_as<PlayerIterator>();
    if (it) it->manager = nullptr;
    for (CPlayer* player = it->First(world->PlayerList()); player; player = it->Next()) {
        CPlayerServer* state = player->Server();
        stream->Read(&player->partyId_, 4);
        static_cast<CParty*>(player->Party())->LoadParty(stream, 1);
        player->lastTick_ = world->tick_;
        stream->Read(&player->sessionKind100_, 4);
        CBaseManager* items = world->ItemFactory();
        for (;;) {
            int id = ReadValue<int>(stream);
            if (id == -1) break;
            CItem* object = FindItem(items, id);
            if (!object) continue;
            player->items_.AddTail(object);
            ++object->refCount_;
        }
        stream->Read(&player->godMode_, 1);
        if (stream->version_ >= kVersionPlayerLists) {
            DwordList& ids = player->recharging_;
            int count = ReadValue<int>(stream);
            for (int i = 0; i < count; ++i) ids.AddTail(0);
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
            static_cast<ScriptGroup*>((void*)&group)->DestructBody();
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
    CMemoryStream savedGraph;   // the AI graph state as the zone files have it
    CMemoryStream graph;        // ... with the save's differences applied
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
            static_cast<CPlayer*>((void*)player)->Clear();
        }
    }
    AIMap()->BeginRefresh();
    loaded_ = 0;
    AIMap()->refreshUnits = false;

    ReadStringInto(stream, mobName);
    ReadStringInto(stream, mapName);
    ReadStringInto(stream, zoneName);
    if (map) mapName = map;
    zoneName_ = zoneName;
    SelectLoadingMovie(zoneName);
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
    if (!ReadAIGraph(mapName, mobName, &savedGraph)) {
        result = 0;
    } else {
        graph.ApplyDiff(&savedGraph, stream);
        ShowLoadProgress(kProgressZone);
        if (!LoadMap(mapName, mobName, &graph, 1)) {
            result = game::FatalError(game::CantLoadSaveMapText());
        } else {
            // The map objects' identities: id, type and +0x18 (created by the manager at +0x40).
            for (;;) {
                int id = ReadValue<int>(stream);
                if (id == -1) break;
                int type = ReadValue<int>(stream);
                std::uint32_t value18 = ReadValue<std::uint32_t>(stream);
                CEngineObject* object = manager40_->CreateObject(type, id);
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

            // Missiles: a byte 1 before each one, 0 at the end. They find the world through the global server
            // while they
            // load.
            g_loadingProjectileWorld = this;
            for (;;) {
                std::uint8_t more = ReadValue<std::uint8_t>(stream);
                if (!more) break;
                auto* missile = new CSpellEffect();
                missile->refs_ = 0;   // the count and flags are cleared before the null test
                missile->flags6_ = 0;
                missile->flags7_ = 0;
                missile->Load(stream);
                auto& list = projectiles_;
                ProjectileList::Node* node = projectiles_.NewNode(list.tail, nullptr);
                reinterpret_cast<RefPtr*>(&node->data)->Assign(reinterpret_cast<CRefObject*>(missile));
                if (list.tail) list.tail->next = node;
                else list.head = node;
                list.tail = node;
            }

            static_cast<AIEventTable*>(AIEvents())->Load(stream);
            static_cast<CScript*>(MainScript())->LoadState(stream);
            diplomacy_.ReadFrom(stream);
            stream->Read(&climate_, 4);
            stream->Read(&cave_, 4);
            if (stream->version_ >= kVersionCountdowns)   // the countdowns are a tween array
                countdowns_.LoadEntries(stream);

            // Counted references to items (added at the head of the list).
            for (;;) {
                int id = ReadValue<int>(stream);
                if (id < 0) break;
                CEngineObject* item = itemManager_->FindObject(id);
                if (!item) continue;
                auto* node = counted_.NewNode(nullptr, counted_.head);
                HeldEngineObject::Of(&node->data)->Assign(item);
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
                    CItem* object = FindItem(ItemFactory(), id);
                    if (!object) continue;
                    auto* record = new CItemLink();
                    record->value = value;
                    CItemLink* old = object->ServerLink();
                    object->link_ = record;
                    if (record) ++record->refs_;
                    if (old) old->Release();
                }
            }

            ShowLoadProgress(kProgressDone);
            zoneName_ = zoneName;
            SetTimeSpeed(speed);
            SetTime(time);
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
                    std::destroy_at(name);
                }
            } else {
                mobFiles_.SetSize(0, -1);
            }

            if (stream->version_ >= kVersionSharedObjects) {
                SharedObjectList& shared = sharedObjects_;
                int count = ReadValue<int>(stream);
                for (int i = 0; i < count; ++i) {
                    // The element is copied from a local whose first three dwords are left unset; Load
                    // overwrites them.
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
    // The iterator lives on the heap.
    auto* it = gamecrt::operator_new_as<PlayerIterator>();
    if (it) it->manager = nullptr;
    for (CPlayer* player = it->First(world->PlayerList()); player; player = it->Next()) {
        stream->Write(&player->partyId_, 4);
        player->Party()->SaveParty(stream, 1);
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

} // namespace

void CWorldServer::SaveWorld(WorldStream* stream) {
    // The AI graph state as the zone files have it (`base`) and as it is now (`current`); only
    // their differences are saved.
    CMemoryStream base;
    CMemoryStream current;

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
    // Stored by the callee with FSTP; written on as their bytes (WriteCopy takes a reference).
    float time;
    float speed;
    GetTime(&time, &speed);
    WriteCopy(stream, time);
    WriteCopy(stream, speed);
    WriteCopy(stream, lastRainBoltTick_);
    stream->Write(&sessionKind_, 4);

    ReadAIGraph(mapName_, mobName_, &base);
    AIMap()->Regions()->SaveBlocks(&current);
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
        CSpellEffect* missile = node->data;
        WriteCopy(stream, static_cast<std::uint8_t>(1));
        missile->Save(stream);
        node = next;
    }
    WriteCopy(stream, static_cast<std::uint8_t>(0));

    static_cast<AIEventTable*>(AIEvents())->Save(stream);
    static_cast<CScript*>(MainScript())->SaveState(stream);
    diplomacy_.WriteTo(stream);
    stream->Write(&climate_, 4);
    stream->Write(&cave_, 4);
    countdowns_.Save(stream);

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
        ObjectListIteratorOf<CItem> it{};
        for (CItem* item = it.First(ItemFactory()); item; item = it.Next()) {
            CItemLink* record = item->ServerLink();
            if (!record) continue;
            WriteCopy(stream, item->id_);
            WriteCopy(stream, record->value);
        }
        WriteCopy(stream, -1);
        if (it.manager) --it.manager->refCount_;   // the iterator's inlined destructor
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
}

// ---- The shared-object list -------------------------------------------------------------------

namespace {

// CArray<CRefPtr>::Copy (gamearrays::ObjectPtrCopy).
void ObjectPtrArrayCopy(ObjectPtrArray* to, const ObjectPtrArray* from) {
    to->CopyFrom(*from);
}
// Releases `count` counted pointers from `data` (CArray DestructElements).
void CountedRangeRelease(CItem** items, int count) {
    for (int i = 0; i < count; ++i)
        if (items[i]) items[i]->Release();
}
// The item manager's (WorldItemManager) object by id, or null.

// SetAtGrow(size, object) on a CArray<CRefPtr>: the new slot holds a reference.
void Append(ObjectPtrArray& array, CItem* object) {
    int index = array.size;
    mfc::impl::ArrayAs<gamearrays::ObjectPtrs>(array).SetSize(index + 1, -1);
    CItem* old = array.data[index];
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
        CItem* object = FindItem(WorldItemManager(), id);
        if (object) Append(entry->objects, object);   // the SetSize is inlined
    }
}

void CWorldServer::AddSharedObject(int id, int /*unused*/, std::uint32_t value, const mfc::CTypedPtrList<CItem>* objects) {
    // The entry's player: the session player record's +0x30 in a multiplayer game, else -1.
    int player = -1;
    net::ServerSession* session = net::GameServer();
    if (session && !(session->flags7_ & CRefCounted::kDetached))
        if (net::PlayerMessage* record = session->FindPlayer(id)) player = record->serial_;

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

// UnitStats::RefillSlots (unit_vitals.cpp), on the controller's stats (+0x1E0).

} // namespace

void CWorldServer::PrepareSave() {
    for (auto* node = mapObjects_.map.First(); node && node->value; node = mapObjects_.map.After(node)) {
        CUnit* unit = UnitOrNull(node->value);
        if (!unit || ObjectSlots(unit)->IsDead()) continue;
        CUnitController* controller = unit->Controller();
        unit->ServerController()->stats.RefillSlots();
        controller->RefreshVitals(0);
    }
}

// ---- NewZone ----------------------------------------------------------------------------------

namespace {

constexpr const char* kCantLoadGraph = "CWorldServer::LoadMap.\n Can't load AIGraph.\n";
constexpr const char* kCantLoadMap = "CWorldServer::NewZone()\n Can't load map.\n";

// Whether the file `name` is neither there as named nor in the maps folder (Rebirth's Maps names it
// first: `site`).
bool MapFileMissing(const char* name, modfolders::MapsSite site) {
    if (libcrt::Access(name, 0) != -1) return false;
    mfc::CString folder;
    modfolders::SelectMaps(name, site);
    MapsFolder(&folder);
    std::string path = (folder + name).GetString();
    return libcrt::Access(path.c_str(), 0) == -1;
}

} // namespace

int CWorldServer::NewZone(const char* map, const char* mob, const char* zone) {
    cave_ = 0;
    SelectLoadingMovie(zone);
    ShowLoadProgress(kProgressStart);
    if (map && MapFileMissing(map, modfolders::MapsSite::NewZoneMap)) return 0;
    if (mob && MapFileMissing(mob, modfolders::MapsSite::NewZoneMob)) return 0;
    CMemoryStream graph;
    if (!ReadAIGraph(map, mob, &graph))
        return game::FatalError(kCantLoadGraph);
    mobFiles_.SetSize(0, -1);
    AIMap()->BeginRefresh();
    loaded_ = 0;
    std::memset(alarms_, 0, sizeof(alarms_));
    AIMap()->refreshUnits = false;
    if (!LoadMap(map, mob, &graph, 0))
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
EI_REWRITTEN(0x00595480, CWorldServer::NewZone);

EI_REWRITTEN(0x005956E0, CWorldServer::PrepareSave);
EI_REWRITTEN(0x00589CA0, SharedObject::Load);
EI_REWRITTEN(0x005947E0, CWorldServer::AddSharedObject);
EI_REWRITTEN(0x00596E70, CWorldServer::LoadSave);
EI_REWRITTEN(0x00597BD0, CWorldServer::SaveWorld);
EI_REWRITTEN(0x00596900, CWorldServer::LoadSavedMapObjects);
EI_REWRITTEN(0x0058D280, CWorldServer::UpdatePlayerTimes);
