// The world's player manager (player_manager.h; A-H15).
#include "player_manager.h"

#include "originals_gameplay.h"
#include "player_messages.h"

#include "../lib/crt.h"

#include "ei/hook.h"

#include <new>

namespace {

// The player's connection: the server side of it (only the server's manager sends to all).
CPlayerServer* Server(CEngineObject* player) {
    return reinterpret_cast<CPlayerServer*>(static_cast<CPlayer*>(player)->connection_);
}

// Walks every player of `manager`, as the original's inlined iterator does (the manager counted
// while it walks).
template <typename F> void ForEachPlayer(CBaseManager* manager, F f) {
    ObjectListIterator it{nullptr, nullptr};
    for (CEngineObject* player = it.First(manager); player; player = it.Next()) f(player);
    if (it.manager) --it.manager->refCount_;
}

} // namespace

BOOL CPlayerManager::CreatePlayers() { return CBaseManager::Create() != 0 ? 1 : 0; }

CEngineObject* CPlayerManager::NewPlayer(int type) {
    void* memory = gamecrt::operator_new(sizeof(CPlayer));
    if (!memory) return nullptr;
    CPlayer* player = static_cast<CPlayer*>(memory)->Construct();
    if (!player) return nullptr;
    player->type_ = type;
    return player;
}

void CPlayerManager::ReleasePlayer(CEngineObject* player) { player->Release(); }

void CPlayerManager::ChangeId(CEngineObject* object, int id) {
    if (ObjectMap::Node* old = objects_.Find(static_cast<unsigned>(object->id_))) objects_.Erase(old);
    object->id_ = id;
    if (id > maxId_) maxId_ = id;
    objects_[static_cast<unsigned>(id)] = object;
}

void CPlayerManager::SendValueToAll(int unused, int value) {
    ForEachPlayer(this, [&](CEngineObject* player) { PlayerServerSend12Original(Server(player), nullptr, unused, value); });
}

void CPlayerManager::SendTextToAll(int unused, const char* text) {
    ForEachPlayer(this, [&](CEngineObject* player) {
        Server(player)->SendText13(static_cast<std::uint32_t>(unused), text);
    });
}

EI_REPLACE_METHOD(0x0066C0F0, CPlayerManager::CreatePlayers);
EI_REPLACE_METHOD(0x0066C110, CPlayerManager::NewPlayer);
EI_REPLACE_METHOD(0x0066C1D0, CPlayerManager::ReleasePlayer);
EI_REPLACE_METHOD(0x0066C1E0, CPlayerManager::ChangeId);
EI_REPLACE_METHOD(0x0066C340, CPlayerManager::SendValueToAll);
EI_REPLACE_METHOD(0x0066C3C0, CPlayerManager::SendTextToAll);
