// CPlayerServer: the last client messages (A-H23): leave, the game state request, pause, casts,
// weapon and movement style, follow and two small ones. See player_messages.h and
// docs/notes/objects.md ("Players").
#include "player_messages.h"

#include "combat.h"
#include "core_refcount.h"
#include "core_streams.h"
#include "map_object.h"
#include "net_classes.h"
#include "net_messages.h"
#include "originals_world.h"
#include "party.h"
#include "unit.h"
#include "unit_ai.h"
#include "world.h"

#include "ei/hook.h"
#include "ei/rebirth.h"

#include <windows.h>

#include <cstring>

namespace {

constexpr std::uint8_t kMessageGameState = 0x07;
constexpr std::uint32_t kFlagBlocked = 0x800000;
constexpr int kDirtyGameState = 0x10;                 // the player's +0x130
constexpr int kTextPause = 0xC;                       // the session's announcement for a pause
constexpr int kStateJoined = 1;
constexpr int kStateSendingCamera = 2;

// The stream the received packets are decompressed into (007C83D0).
CMemoryStream& Received() { return *reinterpret_cast<CMemoryStream*>(std::uintptr_t(0x007C83D0)); }

// The last digit of "reserved_02" (007930EC), the key of the pause announcement's text
// (CInterface3D::MessageText, message 12). EI_Plugin's Pause writes it in .rdata: '2' for a pause,
// '3' ("reserved_03") for a resume.
void SetPauseTextDigit(std::uint8_t pause) {
    auto* digit = reinterpret_cast<char*>(std::uintptr_t(0x007930F6));
    DWORD old;
    VirtualProtect(digit, 1, PAGE_READWRITE, &old);
    *digit = static_cast<char>('3' - pause);
    VirtualProtect(digit, 1, old, &old);
}

// A unit of the player's world (the server's).
CUnit* FindUnit(CPlayer* player, int id) {
    return static_cast<CUnit*>(WorldFindUnit(player->ManagerContext(), nullptr, id));
}
bool Alive(CUnit* unit) { return ObjectSlots(unit)->IsDead() == 0; }
bool Able(CUnit* unit) { return unit && !(unit->flags_ & kFlagBlocked) && Alive(unit); }
AIBrain* BrainOf(CUnit* unit) { return reinterpret_cast<UnitController*>(unit->controller)->Brain(); }
CUnitServer* ServerControllerOf(CUnit* unit) { return static_cast<CUnitServer*>(unit->controller); }

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
    reinterpret_cast<CPlayerConnection*>(this)->Leave();
    return 1;
}

int CPlayerServer::OnGameStateRequest() {
    if (state_ != kStateJoined) return 0;
    CPlayer* player = Owner();
    int state;
    Received().Read(&state, 4);
    player->sessionKind100_ = state;
    WorldServer()->CollectParty(reinterpret_cast<WorldPlayer*>(player), &placements_);
    SendParty();
    std::uint8_t type = kMessageGameState;
    out_.Write(&type, 1);
    int value = player->sessionKind100_;
    out_.Write(&value, 4);
    value = WorldServer()->tick_;
    out_.Write(&value, 4);
    player->dirty_ &= ~kDirtyGameState;
    state_ = kStateSendingCamera;
    return 1;
}

int CPlayerServer::OnPause() {
    std::uint8_t pause = 0;
    Received().Read(&pause, 1);
    net::ServerSession* server = net::GameServer();
    // EI_Plugin's Pause (the mode from its ini): 1 and 2 jump from 0066FAFE to their bytes at 0066FB62,
    // which write the text key's digit and go on to announce resumes too; 2 also replaces the leader
    // test at 0066FB10 by the pausing player's own leader; 3 skips the announcement (0066FB2C).
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
        auto* record = static_cast<net::LoginMessage*>(server->FindPlayer(leader));
        if (record && !(record->flags7_ & CRefCounted::kDetached) && mode != 3)
            server->AnnounceText(kTextPause, record->name_.start, nullptr);
    }
    static_cast<CWorldServer*>(Owner()->ManagerContext())->SetServerPaused(pause != 0 ? 1 : 0);
    return 1;
}

int CPlayerServer::OnCastAtObject() {
    CastAtObject message;
    Received().Read(&message, sizeof(message));
    CPlayer* player = Owner();
    CEngineObject* target = static_cast<CWorld*>(player->ManagerContext())->ObjectWithId(message.targetId);
    CUnit* unit = FindUnit(player, message.unitId);
    if (target && Able(unit)) {
        AIBrain* brain = BrainOf(unit);
        if (AIMode* mode = brain->FindPlayerMode())
            static_cast<PlayerMode*>(mode)->CommandCast(brain, message.spell, target, message.flag != 0);
    }
    return 1;
}

int CPlayerServer::OnCastAtPoint() {
    CastAtPoint message;
    Received().Read(&message, sizeof(message));
    CUnit* unit = FindUnit(Owner(), message.unitId);
    if (Able(unit)) {
        AIBrain* brain = BrainOf(unit);
        if (AIMode* mode = brain->FindPlayerMode()) {
            const Vector3 point = message.point;
            static_cast<PlayerMode*>(mode)->CommandCastAt(brain, message.spell, &point, message.flag != 0);
        }
    }
    return 1;
}

int CPlayerServer::OnWantedWeapon() {
    UnitByte message;
    Received().Read(&message, sizeof(message));
    CUnit* unit = FindUnit(Owner(), message.unitId);
    if (Able(unit)) ServerControllerOf(unit)->SetWantedWeapon(message.value);
    return 1;
}

int CPlayerServer::OnMoveStyle() {
    UnitByte message;
    Received().Read(&message, sizeof(message));
    CUnit* unit = FindUnit(Owner(), message.unitId);
    if (unit && Alive(unit)) ServerControllerOf(unit)->SetMoveStyle(message.value);
    return 1;
}

int CPlayerServer::OnUnitLookup() {
    int values[2];
    Received().Read(&values[0], 4);
    Received().Read(&values[1], 4);
    FindUnit(Owner(), values[0]);
    return 1;
}

int CPlayerServer::OnFollow() {
    int ids[2];   // unit, leader
    Received().Read(ids, 8);
    CPlayer* player = Owner();
    CUnit* unit = FindUnit(player, ids[0]);
    if (!unit) return 1;
    CUnit* leader = FindUnit(player, ids[1]);
    AIBrain* brain = BrainOf(unit);
    if (AIMode* mode = brain->FindPlayerMode()) static_cast<PlayerMode*>(mode)->SetLeader(brain, leader);
    return 1;
}

int CPlayerServer::OnUnitFlag438() {
    int id;
    Received().Read(&id, 4);
    CUnit* unit = FindUnit(Owner(), id);
    std::int8_t value = 0;
    Received().Read(&value, 1);
    if (unit) unit->SetUnknown438(value);
    return 1;
}

int CPlayerServer::OnLastPosition() {
    std::uint32_t xy[2];   // copied as dwords, as the original does
    Received().Read(xy, 8);
    std::memcpy(&lastPosition_.x, &xy[0], 4);
    std::memcpy(&lastPosition_.y, &xy[1], 4);
    return 1;
}

EI_REPLACE_METHOD(0x0066FA90, CPlayerServer::OnLeave);
EI_REPLACE_METHOD(0x00670CE0, CPlayerServer::OnGameStateRequest);
EI_REPLACE_METHOD(0x0066FAC0, CPlayerServer::OnPause);
EI_REBIRTH_IMPLEMENTS(0x0066FAC0, ei::rebirth::Patch::EiPluginPause);
EI_REPLACE_METHOD(0x00670820, CPlayerServer::OnCastAtObject);
EI_REPLACE_METHOD(0x006708D0, CPlayerServer::OnCastAtPoint);
EI_REPLACE_METHOD(0x00670960, CPlayerServer::OnWantedWeapon);
EI_REPLACE_METHOD(0x006709D0, CPlayerServer::OnMoveStyle);
EI_REPLACE_METHOD(0x00670A40, CPlayerServer::OnUnitLookup);
EI_REPLACE_METHOD(0x00670A90, CPlayerServer::OnFollow);
EI_REPLACE_METHOD(0x0066FB70, CPlayerServer::OnUnitFlag438);
EI_REPLACE_METHOD(0x00670B20, CPlayerServer::OnLastPosition);
