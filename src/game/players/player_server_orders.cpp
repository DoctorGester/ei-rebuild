// PlayerServer: the last client messages: leave, the game state request, pause, casts, weapon and movement
// style, follow and two small ones. See player_messages.h and docs/notes/objects.md ("Players").
#include "game/combat/combat.h"
#include "engine/core/core_refcount.h"
#include "engine/core/core_streams.h"
#include "game/app/singletons.h"
#include "game/objects/map_object.h"
#include "game/net/net_session.h"
#include "game/net/net_messages.h"
#include "game/players/party.h"
#include "game/players/player_messages.h"
#include "game/players/player.h"
#include "game/ui/ui.h"
#include "game/units/unit.h"
#include "game/ai/unit_ai.h"
#include "game/world/world.h"

#include <bit>
#include <cstring>

namespace {

constexpr int kTextPause = 0xC;                       // the session's announcement for a pause


// The last digit of "reserved_02", the key of the pause announcement's text (Interface3D::MessageText,
// message 12): '2' for a pause, '3' ("reserved_03") for a resume (EI_Plugin's Pause). Only the host's own game reads
// it: a joined game shows its "reserved_02" for both.
void SetPauseTextDigit(std::uint8_t pause) { g_pauseMessageKey[10] = static_cast<char>('3' - pause); }

bool Able(Unit* unit) { return unit && !(unit->flags & MapObjectFlag::Blocked) && !unit->IsDead(); }

// The cast messages' layouts as they arrive (0xD / 0x15 bytes).
#pragma pack(push, 1)
struct CastAtObject {
    int spell = 0;
    int unitId = 0;
    int targetId = 0;
    std::uint8_t flag = 0;
};
struct CastAtPoint {
    int spell = 0;
    int unitId = 0;
    Vector3 point;
    std::uint8_t flag = 0;
};
struct UnitByte {
    int unitId = 0;
    std::uint8_t value = 0;
};
#pragma pack(pop)
static_assert(sizeof(CastAtObject) == 0xD && sizeof(CastAtPoint) == 0x15 && sizeof(UnitByte) == 5);   // received as these bytes
constexpr bool NoPadding(const CastAtPoint*) {
    return lib::FieldsFill<&CastAtPoint::spell, &CastAtPoint::unitId, &CastAtPoint::point, &CastAtPoint::flag>();
}

} // namespace

// Unreached in the shipped game: our client never sends message 0x01 (kept for a peer that does).
bool PlayerServer::OnLeave() {
    Leave();
    return true;
}

bool PlayerServer::OnGameStateRequest() {
    if (state != State::Joined) return false;
    Player* currentPlayer = player;
    int gameState;
    in.ReadValue(gameState);
    currentPlayer->sessionKind = gameState;
    g_serverWorld.CollectParty(currentPlayer, &placements);
    SendParty();
    WriteType(&out, ServerMessage::GameState);
    int value = currentPlayer->sessionKind;
    out.WriteValue(value);
    value = g_serverWorld.tick;
    out.WriteValue(value);
    currentPlayer->dirty &= ~PlayerDirty::GameState;
    state = State::GameStateSent;
    return true;
}

bool PlayerServer::OnPause() {
    std::uint8_t pause = 0;
    in.ReadValue(pause);
    net::ServerSession* server = net::g_gameServer;
    // The host's pause mode (net::PauseMode; the original's without a game server). A hosted game's pause comes
    // from the leader only, unless any player may; resumes go through the same test when they are announced. Off
    // announces nothing (and SetServerPaused does nothing).
    const net::PauseMode mode = IsLive(server) ? server->pauseMode : net::PauseMode::Original;
    const bool announceResume = mode == net::PauseMode::Announced || mode == net::PauseMode::AnyPlayer;
    if (IsMultiplayerClient() && IsLive(server) && (pause || announceResume)) {
        SetPauseTextDigit(pause);
        int leader = server->LeaderId();
        const int own = static_cast<int>(player->GetParty()->members[0].unitId);
        if (mode == net::PauseMode::AnyPlayer)
            leader = own;
        else if (own != leader)
            return true;
        net::PlayerMessage* record = server->FindPlayer(leader);
        if (IsLive(record) && mode != net::PauseMode::Off)
            server->AnnounceText(kTextPause, record->name.data(), nullptr);
    }
    player->ServerWorld()->SetServerPaused(pause != 0 ? 1 : 0);
    return true;
}

bool PlayerServer::OnCastAtObject() {
    CastAtObject message;
    in.ReadValue(message);
    Player* currentPlayer = player;
    MapObject* target = currentPlayer->OwningWorld()->ObjectWithId(message.targetId);
    Unit* unit = currentPlayer->OwningWorld()->FindUnit(message.unitId);
    if (target && Able(unit)) {
        AIBrain* brain = &unit->ServerController()->brain;
        if (PlayerMode* mode = brain->FindModeOf<PlayerMode>())
            mode->CommandCast(brain, message.spell, target);
    }
    return true;
}

bool PlayerServer::OnCastAtPoint() {
    CastAtPoint message;
    in.ReadValue(message);
    Unit* unit = player->OwningWorld()->FindUnit(message.unitId);
    if (Able(unit)) {
        AIBrain* brain = &unit->ServerController()->brain;
        if (PlayerMode* mode = brain->FindModeOf<PlayerMode>()) {
            const Vector3 point = message.point;
            mode->CommandCastAt(brain, message.spell, point);
        }
    }
    return true;
}

bool PlayerServer::OnWantedWeapon() {
    UnitByte message;
    in.ReadValue(message);
    Unit* unit = player->OwningWorld()->FindUnit(message.unitId);
    if (Able(unit)) unit->ServerController()->SetWantedWeapon(message.value);
    return true;
}

bool PlayerServer::OnMoveStyle() {
    UnitByte message;
    in.ReadValue(message);
    Unit* unit = player->OwningWorld()->FindUnit(message.unitId);
    if (unit && !unit->IsDead()) unit->ServerController()->SetMoveStyle(static_cast<MoveMode>(message.value));
    return true;
}

// Unreached in the shipped game: our client never sends message 0x38 (kept for a peer that does).
bool PlayerServer::OnUnitLookup() {
    int values[2];
    in.ReadValue(values[0]);
    in.ReadValue(values[1]);
    player->OwningWorld()->FindUnit(values[0]);
    return true;
}

bool PlayerServer::OnFollow() {
    int ids[2];   // unit, leader
    in.Read(ids, 8);
    Player* currentPlayer = player;
    Unit* unit = currentPlayer->OwningWorld()->FindUnit(ids[0]);
    if (!unit) return true;
    Unit* leader = currentPlayer->OwningWorld()->FindUnit(ids[1]);
    AIBrain* brain = &unit->ServerController()->brain;
    if (PlayerMode* mode = brain->FindModeOf<PlayerMode>()) mode->SetLeader(brain, leader);
    return true;
}

bool PlayerServer::OnFightAtWill() {
    const int objectId = in.ReadId();
    Unit* unit = player->OwningWorld()->FindUnit(objectId);
    std::int8_t value = 0;
    in.ReadValue(value);
    if (unit) unit->SetFightAtWill(value);
    return true;
}

bool PlayerServer::OnLastPosition() {
    Vector2 xy;   // bit for bit
    in.Read(&xy, 8);
    lastPosition = WithZ(xy, lastPosition.z);
    return true;
}
