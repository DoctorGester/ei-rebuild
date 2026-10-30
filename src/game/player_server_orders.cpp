// PlayerServer: the last client messages: leave, the game state request, pause, casts, weapon and movement
// style, follow and two small ones. See player_messages.h and docs/notes/objects.md ("Players").
#include "player_messages.h"

#include "combat.h"
#include "core_refcount.h"
#include "core_streams.h"
#include "map_object.h"
#include "net_classes.h"
#include "net_messages.h"
#include "party.h"
#include "unit.h"
#include "ui.h"
#include "unit_ai.h"
#include "world.h"

#include "ei/rebirth.h"
#include "game_singletons.h"

#include <bit>

#include <cstring>

namespace {

constexpr std::uint8_t kMessageGameState = 0x07;
constexpr int kTextPause = 0xC;                       // the session's announcement for a pause
constexpr int kStateJoined = 1;
constexpr int kStateSendingCamera = 2;


// The last digit of "reserved_02", the key of the pause announcement's text (Interface3D::MessageText,
// message 12). EI_Plugin's Pause writes it: '2' for a pause, '3' ("reserved_03") for a resume.
void SetPauseTextDigit(std::uint8_t pause) { g_pauseMessageKey[10] = static_cast<char>('3' - pause); }

bool Able(CUnit* unit) { return unit && !(unit->flags & MapObjectFlag::Blocked) && !unit->IsDead(); }

// The cast messages' layouts as they arrive (0xD / 0x15 bytes).
#pragma pack(push, 1)
struct CastAtObject {
    int spell;
    int unitId;
    int targetId;
    std::uint8_t flag;
};
struct CastAtPoint {
    int spell;
    int unitId;
    Vector3 point;
    std::uint8_t flag;
};
struct UnitByte {
    int unitId;
    std::uint8_t value;
};
#pragma pack(pop)
static_assert(sizeof(CastAtObject) == 0xD && sizeof(CastAtPoint) == 0x15 && sizeof(UnitByte) == 5);

} // namespace

int PlayerServer::OnLeave() {
    Leave();
    return 1;
}

int PlayerServer::OnGameStateRequest() {
    if (state != kStateJoined) return 0;
    CPlayer* currentPlayer = player;
    int gameState;
    g_receivedMessages.ReadValue(gameState);
    currentPlayer->sessionKind = gameState;
    g_serverWorld.CollectParty(currentPlayer, &placements);
    SendParty();
    std::uint8_t messageType = kMessageGameState;
    out.WriteValue(messageType);
    int value = currentPlayer->sessionKind;
    out.WriteValue(value);
    value = g_serverWorld.tick;
    out.WriteValue(value);
    currentPlayer->dirty &= ~PlayerDirty::GameState;
    state = kStateSendingCamera;
    return 1;
}

int PlayerServer::OnPause() {
    std::uint8_t pause = 0;
    g_receivedMessages.ReadValue(pause);
    net::ServerSession* server = net::g_gameServer;
    // EI_Plugin's Pause (the mode from its ini): 1 and 2 jump to bytes
    // which write the text key's digit and go on to announce resumes too; 2 also replaces the leader
    // test by the pausing player's own leader; 3 skips the announcement.
    const int mode = ei::rebirth::Enabled(ei::rebirth::Patch::EiPluginPause) ? ei::rebirth::PluginPause() : 0;
    const bool announceResume = mode == 1 || mode == 2;
    if (IsMultiplayerClient() && server && !(server->flags7 & RefCounted::kDetached) && (pause || announceResume)) {
        if (announceResume) SetPauseTextDigit(pause);
        int leader = server->LeaderId();
        const int own = static_cast<int>(player->GetParty()->members[0].unitId);
        if (mode == 2)
            leader = own;
        else if (own != leader)
            return 1;
        net::PlayerMessage* record = server->FindPlayer(leader);
        if (IsLive(record) && mode != 3)
            server->AnnounceText(kTextPause, record->name.data(), nullptr);
    }
    player->ServerWorld()->SetServerPaused(pause != 0 ? 1 : 0);
    return 1;
}

int PlayerServer::OnCastAtObject() {
    CastAtObject message;
    g_receivedMessages.ReadValue(message);
    CPlayer* currentPlayer = player;
    CMapObject* target = currentPlayer->WorldContext()->ObjectWithId(message.targetId);
    CUnit* unit = currentPlayer->WorldContext()->FindUnit(message.unitId);
    if (target && Able(unit)) {
        AIBrain* brain = &unit->ServerController()->brain;
        if (PlayerMode* mode = brain->FindPlayerMode())
            mode->CommandCast(brain, message.spell, target);
    }
    return 1;
}

int PlayerServer::OnCastAtPoint() {
    CastAtPoint message;
    g_receivedMessages.ReadValue(message);
    CUnit* unit = player->WorldContext()->FindUnit(message.unitId);
    if (Able(unit)) {
        AIBrain* brain = &unit->ServerController()->brain;
        if (PlayerMode* mode = brain->FindPlayerMode()) {
            const Vector3 point = message.point;
            mode->CommandCastAt(brain, message.spell, point);
        }
    }
    return 1;
}

int PlayerServer::OnWantedWeapon() {
    UnitByte message;
    g_receivedMessages.ReadValue(message);
    CUnit* unit = player->WorldContext()->FindUnit(message.unitId);
    if (Able(unit)) unit->ServerController()->SetWantedWeapon(message.value);
    return 1;
}

int PlayerServer::OnMoveStyle() {
    UnitByte message;
    g_receivedMessages.ReadValue(message);
    CUnit* unit = player->WorldContext()->FindUnit(message.unitId);
    if (unit && !unit->IsDead()) unit->ServerController()->SetMoveStyle(message.value);
    return 1;
}

int PlayerServer::OnUnitLookup() {
    int values[2];
    g_receivedMessages.ReadValue(values[0]);
    g_receivedMessages.ReadValue(values[1]);
    player->WorldContext()->FindUnit(values[0]);
    return 1;
}

int PlayerServer::OnFollow() {
    int ids[2];   // unit, leader
    g_receivedMessages.Read(ids, 8);
    CPlayer* currentPlayer = player;
    CUnit* unit = currentPlayer->WorldContext()->FindUnit(ids[0]);
    if (!unit) return 1;
    CUnit* leader = currentPlayer->WorldContext()->FindUnit(ids[1]);
    AIBrain* brain = &unit->ServerController()->brain;
    if (PlayerMode* mode = brain->FindPlayerMode()) mode->SetLeader(brain, leader);
    return 1;
}

int PlayerServer::OnFightAtWill() {
    int objectId;
    g_receivedMessages.ReadValue(objectId);
    CUnit* unit = player->WorldContext()->FindUnit(objectId);
    std::int8_t value = 0;
    g_receivedMessages.ReadValue(value);
    if (unit) unit->SetFightAtWill(value);
    return 1;
}

int PlayerServer::OnLastPosition() {
    std::uint32_t xy[2];   // bit for bit
    g_receivedMessages.Read(xy, 8);
    lastPosition.x = std::bit_cast<float>(xy[0]);
    lastPosition.y = std::bit_cast<float>(xy[1]);
    return 1;
}
