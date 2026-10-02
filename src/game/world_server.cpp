// CWorldServer: object and player lookups, the coarse height grid, shared objects, dirty flags and
// the id maps. See world.h.
#include "world.h"

#include "core_maths.h"
#include "script.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "ei/x87.h"

namespace {

// Vtable slots of the world classes used here.
constexpr int kSlotPlayers = 20;      // 0055EB50: the player list (+0x38)
constexpr int kSlotFindObject = 40;   // 00593AA0 on the server
// Units: slot 55 (+0xDC) is non-zero for dead units.
constexpr int kSlotUnitIsDead = 55;

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

// Node of the MFC list at +0x12E64.
struct SharedObjectNode {
    SharedObjectNode* next;
    SharedObjectNode* prev;
    int objectId;
    int player;   // -1: every player
};

// 00675CA0 (unit code): marks an object new (flags_ |= 2) and stamps it with the global at 007AFFC8.
struct NewObjectMark {
    void Apply();
};
__declspec(naked) void NewObjectMark::Apply() EI_ORIGINAL(0x00675CA0)

// 0055FB00 (world base): FindObject (slot 40) restricted to units.
struct WorldBase {
    CEngineObject* FindUnit(unsigned id);
};
__declspec(naked) CEngineObject* WorldBase::FindUnit(unsigned) EI_ORIGINAL(0x0055FB00)

} // namespace

// ---- Player iteration (original helpers, called out of line like the original) -----------------

__declspec(naked) WorldPlayer* PlayerIterator::First(CEngineObject*) EI_ORIGINAL(0x00404920)
__declspec(naked) WorldPlayer* PlayerIterator::Next() EI_ORIGINAL(0x00404960)

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
    for (WorldPlayer* player = it.First(ei::VCall<CEngineObject*>(this, kSlotPlayers)); player; player = it.Next())
        if (player->id == id) return player;
    return nullptr;
}

float CWorldServer::GridHeight(float x, float y) {
    // Cell size 1.0 at half-cell offset: (1 / (1 * 0.5)) * v - 0.5, rounded through a float.
    int cx = x87::fistp(StoreFloat(2.0f * x - 0.5f));
    int cy = x87::fistp(StoreFloat(2.0f * y - 0.5f));
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
        if (unit && ei::VCall<int>(unit, kSlotUnitIsDead) == 0) ++living;
    }
    return living - 1;
}

bool CWorldServer::IsObjectShared(int objectId, int playerId) {
    int player = SharedListPlayer(playerId);
    for (auto* node = static_cast<SharedObjectNode*>(sharedObjects_); node; node = node->next)
        if (node->objectId == objectId && (node->player == player || node->player == -1)) return true;
    return false;
}

void CWorldServer::CollectSharedObjects(ScriptGroup* group, int playerId) {
    group->SetSize(0, -1);
    int player = SharedListPlayer(playerId);
    for (auto* node = static_cast<SharedObjectNode*>(sharedObjects_); node;) {
        SharedObjectNode* current = node;
        node = node->next;
        if (current->player != player && current->player != -1) continue;
        if (CEngineObject* unit = reinterpret_cast<WorldBase*>(this)->FindUnit(current->objectId)) group->Insert(unit);
    }
}

// ---- State ---------------------------------------------------------------------------------------

void CWorldServer::MarkPlayersDirty(std::uint32_t mask) {
    PlayerIterator it;
    for (WorldPlayer* player = it.First(ei::VCall<CEngineObject*>(this, kSlotPlayers)); player; player = it.Next())
        player->dirty |= mask;
}

void CWorldServer::SetUnknownE0(int value) { unknownE0_ = value; }

void CWorldServer::SetUnknownDC(int value) {
    if (unknownE4_ != 0) {
        unknownE4_ = 0;
        unknownE8_ = 20.0f;
        MarkPlayersDirty(2);
    }
    unknownDC_ = value;
}

void CWorldServer::SetUnknownE4E8(int e4, float e8) {
    unknownE4_ = e4;
    unknownE8_ = e8;
    MarkPlayersDirty(2);
}

void CWorldServer::SetUnknown6C(std::uint8_t value) { unknown6C_ = value; }

BOOL CWorldServer::GetZoneExit(int index, mfc::CString* target, int* entrance) {
    if (index < 0 || index >= exitCount_) return 0;
    *target = *reinterpret_cast<const mfc::CString*>(&exits_[index].target);
    *entrance = exits_[index].entrance;
    return 1;
}

void CWorldServer::AddTimedObject(CEngineObject* object) {
    *reinterpret_cast<int*>(reinterpret_cast<std::uint8_t*>(object) + 0x94) = tick_;
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

void CWorldServer::AddToUpdateMap(CEngineObject* unit) {
    auto* raw = reinterpret_cast<std::uint8_t*>(unit);
    *reinterpret_cast<int*>(raw + 0x174) = ((reinterpret_cast<int>(unit) >> 5) & 0xF) + 0x80;
    unsigned id = static_cast<unsigned>(unit->id_);
    updateMap_.Set(id, unit);
}

EI_REPLACE_METHOD(0x00593AA0, CWorldServer::FindObject);
EI_REPLACE_METHOD(0x00594180, CWorldServer::GetPlayer);
EI_REPLACE_METHOD(0x00594220, CWorldServer::GridHeight);
EI_REPLACE_METHOD(0x00591740, CWorldServer::CountLivingMercs);
EI_REPLACE_METHOD(0x005949D0, CWorldServer::IsObjectShared);
EI_REPLACE_METHOD(0x00594A30, CWorldServer::CollectSharedObjects);
EI_REPLACE_METHOD(0x0058B6D0, CWorldServer::MarkPlayersDirty);
EI_REPLACE_METHOD(0x0058CC70, CWorldServer::SetUnknownE0);
EI_REPLACE_METHOD(0x0058CC80, CWorldServer::SetUnknownDC);
EI_REPLACE_METHOD(0x0058EE00, CWorldServer::SetUnknownE4E8);
EI_REPLACE_METHOD(0x005942C0, CWorldServer::SetUnknown6C);
EI_REPLACE_METHOD(0x0058E4C0, CWorldServer::GetZoneExit);
EI_REPLACE_METHOD(0x0058E520, CWorldServer::AddTimedObject);
EI_REPLACE_METHOD(0x0058E5D0, CWorldServer::RemoveTimedObject);
EI_REPLACE_METHOD(0x0058D5D0, CWorldServer::AddToUpdateMap);
EI_REPLACE_METHOD(0x005999E0, ObjectIdMap::Set);
EI_REPLACE_METHOD(0x00599BD0, ObjectIdMap::Set);
