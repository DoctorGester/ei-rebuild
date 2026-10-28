// CPlayerServer: the last client messages: leave, the game state request, pause, casts, weapon and movement
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
#include "game_data.h"
#include "game_singletons.h"

#include <windows.h>

#include <cstring>

namespace {

constexpr std::uint8_t kMessageGameState = 0x07;
constexpr std::uint32_t kFlagBlocked = 0x800000;
constexpr int kDirtyGameState = 0x10;                 // the player's +0x130
constexpr int kTextPause = 0xC;                       // the session's announcement for a pause
constexpr int kStateJoined = 1;
constexpr int kStateSendingCamera = 2;


// The last digit of "reserved_02", the key of the pause announcement's text (CInterface3D::MessageText,
// message 12). EI_Plugin's Pause writes it: '2' for a pause, '3' ("reserved_03") for a resume.
void SetPauseTextDigit(std::uint8_t pause) { g_pauseMessageKey[10] = static_cast<char>('3' - pause); }

// A unit of the player's world (the server's).
CUnit* FindUnit(CPlayer* player, int id) {
    return player->WorldContext()->FindUnit(id);
}
bool Alive(CUnit* unit) { return unit->IsDead() == 0; }
bool Able(CUnit* unit) { return unit && !(unit->flags_ & kFlagBlocked) && Alive(unit); }

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

int CPlayerServer::OnLeave() {
    Leave();
    return 1;
}

int CPlayerServer::OnGameStateRequest() {
    if (state_ != kStateJoined) return 0;
    CPlayer* player = Owner();
    int state;
    ReceivedMessages().Read(&state, 4);
    player->sessionKind100_ = state;
    TheServerWorld()->CollectParty(player, &placements_);
    SendParty();
    std::uint8_t type = kMessageGameState;
    out_.Write(&type, 1);
    int value = player->sessionKind100_;
    out_.Write(&value, 4);
    value = TheServerWorld()->tick_;
    out_.Write(&value, 4);
    player->dirty_ &= ~kDirtyGameState;
    state_ = kStateSendingCamera;
    return 1;
}

int CPlayerServer::OnPause() {
    std::uint8_t pause = 0;
    ReceivedMessages().Read(&pause, 1);
    net::ServerSession* server = net::GameServer();
    // EI_Plugin's Pause (the mode from its ini): 1 and 2 jump to bytes
    // which write the text key's digit and go on to announce resumes too; 2 also replaces the leader
    // test by the pausing player's own leader; 3 skips the announcement.
    const int mode = ei::rebirth::Enabled(ei::rebirth::Patch::EiPluginPause) ? ei::rebirth::PluginPause() : 0;
    const bool announceResume = mode == 1 || mode == 2;
    if (IsMultiplayerClient() && server && !(server->flags7_ & CRefCounted::kDetached) && (pause || announceResume)) {
        if (announceResume) SetPauseTextDigit(pause);
        int leader = server->LeaderId();
        const int own = static_cast<int>(Owner()->Party()->members_.data[0].unitId);
        if (mode == 2)
            leader = own;
        else if (own != leader)
            return 1;
        net::PlayerMessage* record = server->FindPlayer(leader);
        if (IsLive(record) && mode != 3)
            server->AnnounceText(kTextPause, record->name_.data(), nullptr);
    }
    Owner()->ServerWorld()->SetServerPaused(pause != 0 ? 1 : 0);
    return 1;
}

int CPlayerServer::OnCastAtObject() {
    CastAtObject message;
    ReceivedMessages().Read(&message, sizeof(message));
    CPlayer* player = Owner();
    CMapObject* target = player->WorldContext()->ObjectWithId(message.targetId);
    CUnit* unit = FindUnit(player, message.unitId);
    if (target && Able(unit)) {
        AIBrain* brain = unit->ServerController()->Brain();
        if (PlayerMode* mode = brain->FindPlayerMode())
            mode->CommandCast(brain, message.spell, target, message.flag != 0);
    }
    return 1;
}

int CPlayerServer::OnCastAtPoint() {
    CastAtPoint message;
    ReceivedMessages().Read(&message, sizeof(message));
    CUnit* unit = FindUnit(Owner(), message.unitId);
    if (Able(unit)) {
        AIBrain* brain = unit->ServerController()->Brain();
        if (PlayerMode* mode = brain->FindPlayerMode()) {
            const Vector3 point = message.point;
            mode->CommandCastAt(brain, message.spell, &point, message.flag != 0);
        }
    }
    return 1;
}

int CPlayerServer::OnWantedWeapon() {
    UnitByte message;
    ReceivedMessages().Read(&message, sizeof(message));
    CUnit* unit = FindUnit(Owner(), message.unitId);
    if (Able(unit)) unit->ServerController()->SetWantedWeapon(message.value);
    return 1;
}

int CPlayerServer::OnMoveStyle() {
    UnitByte message;
    ReceivedMessages().Read(&message, sizeof(message));
    CUnit* unit = FindUnit(Owner(), message.unitId);
    if (unit && Alive(unit)) unit->ServerController()->SetMoveStyle(message.value);
    return 1;
}

int CPlayerServer::OnUnitLookup() {
    int values[2];
    ReceivedMessages().Read(&values[0], 4);
    ReceivedMessages().Read(&values[1], 4);
    FindUnit(Owner(), values[0]);
    return 1;
}

int CPlayerServer::OnFollow() {
    int ids[2];   // unit, leader
    ReceivedMessages().Read(ids, 8);
    CPlayer* player = Owner();
    CUnit* unit = FindUnit(player, ids[0]);
    if (!unit) return 1;
    CUnit* leader = FindUnit(player, ids[1]);
    AIBrain* brain = unit->ServerController()->Brain();
    if (PlayerMode* mode = brain->FindPlayerMode()) mode->SetLeader(brain, leader);
    return 1;
}

int CPlayerServer::OnUnitFlag438() {
    int id;
    ReceivedMessages().Read(&id, 4);
    CUnit* unit = FindUnit(Owner(), id);
    std::int8_t value = 0;
    ReceivedMessages().Read(&value, 1);
    if (unit) unit->SetUnknown438(value);
    return 1;
}

int CPlayerServer::OnLastPosition() {
    std::uint32_t xy[2];   // copied as dwords
    ReceivedMessages().Read(xy, 8);
    std::memcpy(&lastPosition_.x, &xy[0], 4);
    std::memcpy(&lastPosition_.y, &xy[1], 4);
    return 1;
}
