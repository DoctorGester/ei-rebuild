// Checks for the world's player manager (player_manager.cpp, A-H15). All ours only: they allocate,
// change the id table or append to every player's stream.
// Scenarios: every newgame creates the manager (CreatePlayers) and the local player (NewPlayer);
// the client's login answer (message 0x00) re-files the local player under the server's id
// (ChangeId); the script commands that address every player (0x12 / 0x13 through the event
// dispatcher 007AFF98, SSCCmd::Execute 004B6ED0) reach SendValueToAll / SendTextToAll.
#include "verify.h"

#include "../../src/game/player_manager.h"
#include "../../src/game/player_messages.h"

#include <cstring>
#include <vector>

namespace {
void* s_unused;

std::vector<std::uint32_t> StreamSizes(CBaseManager* manager) {
    std::vector<std::uint32_t> sizes;
    for (auto* node = manager->objectList_.head; node; node = node->next) {
        auto* server = reinterpret_cast<CPlayerServer*>(static_cast<CPlayer*>(node->data)->connection_);
        sizes.push_back(server->out_.size_);
    }
    return sizes;
}
bool AllGrew(CBaseManager* manager, const std::vector<std::uint32_t>& before, std::uint32_t by) {
    std::vector<std::uint32_t> after = StreamSizes(manager);
    if (after.size() != before.size()) return false;
    for (std::size_t i = 0; i < after.size(); ++i)
        if (after[i] != before[i] + by) return false;
    return true;
}

BOOL __fastcall CreatePlayersVerify(CPlayerManager* self) {
    BOOL result = self->CreatePlayers();
    bool ok = (result == 0 || result == 1) && (result == 0 || self->objects_.Size() == 0);
    ei::verify::Check("CPlayerManager::CreatePlayers", true, ok, "manager %p result %d", self, result);
    return result;
}
CEngineObject* __fastcall NewPlayerVerify(CPlayerManager* self, void*, int type) {
    CEngineObject* result = self->NewPlayer(type);
    bool ok = !result || (*reinterpret_cast<std::uintptr_t*>(result) == CPlayer::kVtableAddress && result->type_ == type &&
                          result->id_ == -1);
    ei::verify::Check("CPlayerManager::NewPlayer", true, ok, "manager %p type %d", self, type);
    return result;
}
void __fastcall ReleasePlayerVerify(CPlayerManager* self, void*, CEngineObject* player) {
    self->ReleasePlayer(player);
    ei::verify::Check("CPlayerManager::ReleasePlayer", true, true, "manager %p", self);
}
void __fastcall ChangeIdVerify(CPlayerManager* self, void*, CEngineObject* object, int id) {
    int old = object->id_;
    self->ChangeId(object, id);
    bool ok = object->id_ == id && self->FindObject(static_cast<unsigned>(id)) == object && self->maxId_ >= id &&
              (old == id || self->FindObject(static_cast<unsigned>(old)) == nullptr);
    ei::verify::Check("CPlayerManager::ChangeId", true, ok, "manager %p id %d -> %d", self, old, id);
}
void __fastcall SendValueVerify(CPlayerManager* self, void*, int unused, int value) {
    std::vector<std::uint32_t> before = StreamSizes(self);
    self->SendValueToAll(unused, value);
    ei::verify::Check("CPlayerManager::SendValueToAll", true, AllGrew(self, before, 6), "manager %p", self);
}
void __fastcall SendTextVerify(CPlayerManager* self, void*, int unused, const char* text) {
    std::vector<std::uint32_t> before = StreamSizes(self);
    self->SendTextToAll(unused, text);
    bool ok = AllGrew(self, before, static_cast<std::uint32_t>(std::strlen(text) + 2));
    ei::verify::Check("CPlayerManager::SendTextToAll", true, ok, "manager %p", self);
}

} // namespace

EI_VERIFY(0x0066C0F0, CreatePlayersVerify, 0, s_unused);
EI_VERIFY(0x0066C110, NewPlayerVerify, 0, s_unused);
EI_VERIFY(0x0066C1D0, ReleasePlayerVerify, 0, s_unused);
EI_VERIFY(0x0066C1E0, ChangeIdVerify, 0, s_unused);
EI_VERIFY(0x0066C340, SendValueVerify, 0, s_unused);
EI_VERIFY(0x0066C3C0, SendTextVerify, 0, s_unused);
