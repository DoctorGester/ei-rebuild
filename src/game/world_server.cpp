// CWorldServer: object and player lookups, the coarse height grid, shared objects, dirty flags and
// the id maps. See world.h.
#include "player_messages.h"
#include "world.h"

#include "ai_map.h"
#include "core_maths.h"
#include "map_object.h"
#include "script.h"
#include "world_effect.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "ei/x87.h"
#include "originals_ai.h"
#include "originals_world.h"
#include "originals_effects.h"
#include "originals_units.h"

namespace {

// The game server object of a multiplayer session (null or flagged inactive otherwise).
struct GameServer {
    void* FindPlayer(int id);   // 0041A2E0: player record with +0x2C == id, or null
};
__declspec(naked) void* GameServer::FindPlayer(int) EI_ORIGINAL(0x0041A2E0)

GameServer* ActiveGameServer() {
    auto* server = *reinterpret_cast<std::uint8_t**>(0x0079B5DC);
    if (!server || (*reinterpret_cast<std::uint32_t*>(server + 4) & 0x80000000u)) return nullptr;
    return reinterpret_cast<GameServer*>(server);
}

// Player index used by the shared-object list: -1 when not in a multiplayer game or unknown.
int SharedListPlayer(int playerId) {
    if (GameServer* server = ActiveGameServer()) {
        if (auto* record = static_cast<std::uint8_t*>(server->FindPlayer(playerId)))
            return *reinterpret_cast<int*>(record + 0x30);
    }
    return -1;
}

// 00675CA0 (unit code): marks an object new (flags_ |= 2) and stamps it with the global at 007AFFC8.
struct NewObjectMark {
    void Apply();
};
void NewObjectMark::Apply() { static_cast<CEffect*>((void*)this)->MarkChanged(); }

// 0055FB00 (world base): FindObject (slot 40) restricted to units.
struct WorldBase {
    CEngineObject* FindUnit(unsigned id);
};
CEngineObject* WorldBase::FindUnit(unsigned id) { return ::WorldFindUnit(this, nullptr, static_cast<int>(id)); }

// RemoveObject's callees (not rewritten).
// 00599A80 (RET 4). An id map's erase(key).
// 005C1010 (RET 4). CSectorMap::Remove on the server's sector map (+0x294).
// 005300D0 (RET 4). The unit's controller is replaced (here by none).
inline void __fastcall UnitSetController(CEngineObject* a0, void*, void* a2) { UnitSetControllerOriginal((void*)a0, nullptr, a2); }
// 004BED90. A script command queue (+0x12874, +0x12A6C) drops what refers to removed objects.
__declspec(naked) void __fastcall CommandQueuePurge(void*, void*) EI_ORIGINAL(0x004BED90)
// 0067EF00 (RET 4). A missile forgets `object`: 0 when it is the missile's owner (+0x1C; the missile
// is then dropped), else its target (+0x20) is released and cleared if it is `object`; 1.
__declspec(naked) int __fastcall ProjectileForget(Projectile*, void*, CEngineObject*) EI_ORIGINAL(0x0067EF00)
// 00599CC0 (RET 4). The missile list's RemoveAt (releases the missile, frees the node).
__declspec(naked) void __fastcall ProjectileRemoveAt(ProjectileList*, void*, ProjectileList::Node*) EI_ORIGINAL(0x00599CC0)

constexpr int kClassNoSector = 0x61;     // objects of this class are not in the sector map

// find + erase(iterator), as the original inlines it for each map.
void EraseId(ObjectIdMap& map, unsigned id) {
    if (auto* node = map.map.Find(id)) map.map.Erase(node);
}

} // namespace

CWorldServer* WorldServer() { return reinterpret_cast<CWorldServer*>(0x007AFF60); }

// ---- Id maps -------------------------------------------------------------------------------------

void ObjectIdMap::Set(const unsigned& key, CEngineObject* const& value) {
    if (static_cast<int>(key) > maxId) maxId = static_cast<int>(key);
    map[key] = value;
}

// ---- Lookups -------------------------------------------------------------------------------------

CEngineObject* CWorldServer::FindObject(unsigned id) {
    auto* node = objects_.map.Find(id);
    return node ? node->value : nullptr;
}

WorldPlayer* CWorldServer::GetPlayer(int id) {
    PlayerIterator it;
    for (WorldPlayer* player = it.First(PlayerList()); player; player = it.Next())
        if (player->id == id) return player;
    return nullptr;
}

float CWorldServer::GridHeight(float x, float y) {
    // Cell size 1.0 at half-cell offset: (1 / (1 * 0.5)) * v - 0.5, rounded through a float.
    int cx = x87::fistp(static_cast<float>(2.0f * x - 0.5f));
    int cy = x87::fistp(static_cast<float>(2.0f * y - 0.5f));
    if (cx < 0 || cy < 0 || cx >= gridWidth_ || cy >= gridHeight_) return 0.0f;
    int raw = *reinterpret_cast<const std::uint16_t*>(gridRows_[cy] + cx * 9 + 4);
    return static_cast<float>(raw) / gridHeightScale_;
}

int CWorldServer::CountLivingMercs(int id) {
    WorldPlayer* player = GetPlayer(id);
    if (!player) return 0;
    int living = 0;
    for (int i = 0; i < player->mercCount; ++i) {
        unsigned unitId = *reinterpret_cast<unsigned*>(player->mercs + i * 0x784 + 0x1C);
        CEngineObject* unit = reinterpret_cast<WorldBase*>(this)->FindUnit(unitId);
        if (unit && ObjectSlots(unit)->IsDead() == 0) ++living;
    }
    return living - 1;
}

bool CWorldServer::IsObjectShared(int objectId, int playerId) {
    int player = SharedListPlayer(playerId);
    for (auto* node = sharedObjects_.head; node; node = node->next)
        if (node->data.objectId == objectId && (node->data.player == player || node->data.player == -1)) return true;
    return false;
}

void CWorldServer::CollectSharedObjects(int playerId, ScriptGroup* group) {
    group->SetSize(0, -1);
    int player = SharedListPlayer(playerId);
    for (auto* node = sharedObjects_.head; node;) {
        const SharedObject& current = node->data;
        node = node->next;
        if (current.player != player && current.player != -1) continue;   // -1: every player
        if (CEngineObject* unit = reinterpret_cast<WorldBase*>(this)->FindUnit(current.objectId)) group->Insert(unit);
    }
}

// ---- State ---------------------------------------------------------------------------------------

void CWorldServer::MarkPlayersDirty(std::uint32_t mask) {
    PlayerIterator it;
    for (WorldPlayer* player = it.First(PlayerList()); player; player = it.Next())
        player->dirty |= mask;
}

void CWorldServer::SetCave(int value) { cave_ = value; }

void CWorldServer::SetClimate(int value) {
    if (weather_ != 0) {
        weather_ = 0;
        weatherStrength_ = 20.0f;
        MarkPlayersDirty(2);
    }
    climate_ = value;
}

void CWorldServer::SetWeather(int weather, float strength) {
    weather_ = weather;
    weatherStrength_ = strength;
    MarkPlayersDirty(2);
}

void CWorld::SetPaused(std::uint8_t value) { paused_ = value; }

void CWorld::SetServerPaused(std::uint8_t value) {
    if (ei::rebirth::Enabled(ei::rebirth::Patch::EiPluginPause) && ei::rebirth::PluginPause() == 3) return;
    SetPaused(value);
}

BOOL CWorldServer::GetZoneExit(int index, mfc::CString* target, int* entrance) {
    if (index < 0 || index >= exitCount_) return 0;
    *target = *reinterpret_cast<const mfc::CString*>(&exits_[index].target);
    *entrance = exits_[index].entrance;
    return 1;
}

void CWorldServer::AddTimedObject(CEngineObject* object) {
    // The timed objects are the world's effects (world_effect.h): the tick they began.
    static_cast<CEffect*>(object)->startTick_ = tick_;
    ++object->refCount_;
    reinterpret_cast<NewObjectMark*>(object)->Apply();
    unsigned id = static_cast<unsigned>(object->id_);
    timedObjects_.Set(id, object);
}

void CWorldServer::RemoveTimedObject(unsigned id) {
    auto* node = timedObjects_.map.Find(id);
    if (!node) return;
    CEngineObject* object = node->value;
    object->flags_ |= 1;
    object->Release();
    // Looked up again: releasing may have changed the map.
    if (auto* again = timedObjects_.map.Find(id)) timedObjects_.map.Erase(again);
}

namespace {
bool g_countdownsById = false;   // SpreadUpdateCountdownsById (harness runs only)
} // namespace

int CWorldServer::UpdateCountdownFor(const CEngineObject* object) {
    if (g_countdownsById) return (object->id_ & 0xF) + 0x80;
    return ((reinterpret_cast<int>(object) >> 5) & 0xF) + 0x80;
}

void CWorldServer::SpreadUpdateCountdownsById() { g_countdownsById = true; }

void CWorldServer::AddToUpdateMap(CEngineObject* unit) {
    // The unit's update slot.
    ObjectSlots(unit)->updateCountdown = UpdateCountdownFor(unit);
    unsigned id = static_cast<unsigned>(unit->id_);
    updateMap_.Set(id, unit);
}

// ---- Removal ---------------------------------------------------------------------------------------

void CWorldServer::RemoveObject(unsigned id) {
    auto* found = objects_.map.Find(id);
    CEngineObject* const* slot = found ? &found->value : nullptr;
    // Rebirth's FixRemoveMapObject (context hook at 00593615): an unknown id is logged and ignored.
    // The original reads through the null pointer (and crashes).
    if (!slot && ei::rebirth::Enabled(ei::rebirth::Patch::FixRemoveMapObject)) return;
    CEngineObject* object = *static_cast<CEngineObject* const volatile*>(slot);
    if (!object) return;
    if (object->GetClassId() != kClassNoSector) SectorMapRemove(AsSectorMap(sectorMap_), nullptr, *slot);

    IdMapEraseOriginal(&updateMap_, nullptr, &id);
    EraseId(objects_, id);
    if (namedObjects_.map.Find(id)) EraseId(namedObjects_, id);
    if (sentObjects_.map.Find(id)) {
        EraseId(sentObjects_, id);
    } else {
        EraseId(mapObjects_, id);
        if (otherMapObjects_.map.Find(id)) EraseId(otherMapObjects_, id);
    }

    object->ClearState();   // slot 5: it leaves the server
    bool unit = object->GetClassId() == world::kClassUnit;
    ++object->refCount_;   // held to the end, released twice there (with the maps' reference)
    if (unit) UnitSetController(object, nullptr, nullptr);
    object->flags_ |= 1;
    CommandQueuePurge(commandsA_, nullptr);
    CommandQueuePurge(commandsB_, nullptr);

    // Every world object forgets it (the walk stops at an entry without an object).
    for (auto* node = objects_.map.First(); node && node->value; node = objects_.map.After(node))
        ObjectSlots(node->value)->ForgetObject(object);
    // The missiles it shot are dropped; those aimed at it lose their target.
    for (auto* node = projectiles_.head; node;) {
        auto* current = node;
        node = node->next;
        if (!ProjectileForget(current->data, nullptr, object)) ProjectileRemoveAt(&projectiles_, nullptr, current);
    }
    {
        PlayerIterator players;
        for (WorldPlayer* player = players.First(PlayerList()); player; player = players.Next())
            player->server->ForgetObject(object);
        object->Release();
        object->Release();
    }
}

void CWorldServer::RemoveAllUnits() {
    auto* node = objects_.map.First();
    if (!node) return;
    // The next object is read before the current one is removed, as the original does.
    CEngineObject* object = node->value;
    do {
        unsigned id = static_cast<unsigned>(object->id_);
        bool isUnit = object->GetClassId() == world::kClassUnit;
        node = objects_.map.After(node);
        object = node ? node->value : nullptr;
        if (isUnit) WorldRemoveObject(this, nullptr, static_cast<int>(id));   // 005935C0: Rebirth patches it (rule 9)
    } while (node);
}
EI_REPLACE_METHOD(0x005939E0, CWorldServer::RemoveAllUnits);

CEngineObject* ObjectMapIterator::First(ObjectIdMap* objects) {
    map = objects;
    node = objects->map.First();
    table = objects;
    return node ? node->value : nullptr;
}
EI_REPLACE_METHOD(0x0059AF20, ObjectMapIterator::First);
EI_REPLACE_METHOD(0x0059AFF0, ObjectMapIterator::First);   // byte-identical instantiation

CEngineObject* ObjectMapIterator::Next() {
    if (!node) return nullptr;
    node = table->map.After(node);
    return node ? node->value : nullptr;
}
EI_REPLACE_METHOD(0x0059AF80, ObjectMapIterator::Next);
EI_REPLACE_METHOD(0x0059B050, ObjectMapIterator::Next);   // byte-identical instantiation

double CWorldServer::ServerRawSurfaceHeight(float x, float y) {
    Vector3 point = {x, y, 0.0f};
    return AIMap()->TopHeightAt(&point);
}
EI_REPLACE_METHOD(0x0058A390, CWorldServer::ServerRawSurfaceHeight);

double CWorldServer::ServerGroundHeight(float x, float y) { return GridHeightAt(x, y); }
EI_REPLACE_METHOD(0x0058A3D0, CWorldServer::ServerGroundHeight);

double CWorldServer::ServerSurfaceHeight(float x, float y) { return RawSurfaceHeightAt(x, y); }
EI_REPLACE_METHOD(0x0058A3F0, CWorldServer::ServerSurfaceHeight);

EI_REPLACE_METHOD(0x005935C0, CWorldServer::RemoveObject);
EI_REPLACE_METHOD(0x00593AA0, CWorldServer::FindObject);
EI_REPLACE_METHOD(0x00594180, CWorldServer::GetPlayer);
EI_REPLACE_METHOD(0x00594220, CWorldServer::GridHeight);
EI_REPLACE_METHOD(0x00591740, CWorldServer::CountLivingMercs);
EI_REPLACE_METHOD(0x005949D0, CWorldServer::IsObjectShared);
EI_REPLACE_METHOD(0x00594A30, CWorldServer::CollectSharedObjects);
EI_REPLACE_METHOD(0x0058B6D0, CWorldServer::MarkPlayersDirty);
EI_REPLACE_METHOD(0x0058CC70, CWorldServer::SetCave);
EI_REPLACE_METHOD(0x0058CC80, CWorldServer::SetClimate);
EI_REPLACE_METHOD(0x0058EE00, CWorldServer::SetWeather);
EI_REPLACE_METHOD(0x005942C0, CWorld::SetServerPaused);
EI_REPLACE_METHOD(0x00566350, CWorld::SetPaused);   // byte-identical copy (the client's, message 0x60; A-W12)
EI_REPLACE_METHOD(0x0058E4C0, CWorldServer::GetZoneExit);
EI_REPLACE_METHOD(0x0058E520, CWorldServer::AddTimedObject);
EI_REPLACE_METHOD(0x0058E5D0, CWorldServer::RemoveTimedObject);
EI_REPLACE_METHOD(0x0058D5D0, CWorldServer::AddToUpdateMap);
EI_REPLACE_METHOD(0x005999E0, ObjectIdMap::Set);
EI_REPLACE_METHOD(0x00599BD0, ObjectIdMap::Set);
