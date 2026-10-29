// CPlayerServer: what the server does for its player beyond the per-tick sync, the leader's death and the
// client's messages (handlers by type, from the table CreateServer builds). See player_messages.h and
// docs/notes/objects.md ("Players").

#include "player_messages.h"
#include "console.h"

#include "ai_map.h"
#include "core_maths.h"
#include "core_streams.h"
#include "map_object.h"
#include "party.h"
#include "party_manager.h"
#include "party_records.h"
#include "registry.h"
#include "unit.h"
#include "unit_ai.h"
#include "combat.h"
#include "unit_create.h"
#include "unit_db.h"
#include "world.h"

#include "asset_text.h"
#include "lib/crt.h"
#include "lib/str.h"
#include <cstddef>
#include <cstdint>
#include <new>
#include <string>
#include "lib/text_case.h"

#include "ei/x87.h"
#include "game_singletons.h"
#include "lib/nan_audit.h"
#include "logic_settings.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <list>
#include <memory>
#include <vector>

namespace {

constexpr std::uint8_t kMessageGameOver = 0x09;
constexpr int kRevivedIdBase = 1001000000;            // OnLeaderDied: ids below it are moved up
constexpr int kRevivedIdStep = 1000000;
constexpr int kNoId = -1;                             // see OnLeaderDied: the original's uninitialised ids
constexpr float kUnknownZ = -1313.0f;                 // lastPosition_.z after a revival
constexpr float kPathRange = 1000000.0f;              // FindPath's range for the use order
constexpr float kFarAway = 1e30f;                     // the nearest-unit search's start

// The game mode (1 single player, 2 multiplayer); the campaign mode is CWorldServer::sessionKind_ (1 and 3 end
// the game when the leader is gone).
// The next object id for a new party's record (the party manager stores the party).
int g_nextRecordObjectId = /*00794E3C*/ 1000000013;

constexpr std::uint8_t kMessageZone = 0x08;
constexpr std::uint32_t kLoneHeroRecordId = 0x3B9AF5CE;   // 999999950

// The unit list of the walk / attack orders: a count, then that many ids (missing units and units
// flagged 0x800000 are left out).
void ReadUnits(CMemoryStream& in, std::vector<CUnit*>* units) {
    int count = 0;
    in.ReadValue(count);
    for (int i = 0; i < count; ++i) {
        int id = 0;
        in.ReadValue(id);
        CUnit* unit = g_serverWorld.FindUnit(id);
        if (unit && !(unit->flags_ & MapObjectFlag::Blocked)) lib::SetAtGrow(*units, std::ssize(*units), unit);
    }
}

using MoveCommand = void (PlayerMode::*)(AIBrain*, const Vector3&, int, int, const Vector3&);
int MoveUnits(MoveCommand command) {
    CMemoryStream& in = g_receivedMessages;
    std::vector<CUnit*> units{};
    std::vector<Vector3> offsets{};
    ReadUnits(in, &units);
    int run = 0;
    Vector3 to{};
    in.ReadValue(run);
    in.ReadValue(to);
    FormationOffsets(&units, &offsets);
    for (int i = 0; i < std::ssize(units); ++i) {
        AIBrain* brain = &units[i]->ServerController()->brain;
        if (PlayerMode* mode = brain->FindPlayerMode())
            (mode->*command)(brain, to, std::ssize(units) > 1, run != 0, offsets[i]);
    }
    return 1;
}

constexpr int kClientVersion = 0x64;
constexpr int kLoginTextLength = 0x3F;
constexpr std::uint8_t kMessageLogin = 0x00;
constexpr std::uint8_t kMessageError = 0x11;

constexpr std::uint8_t kMessagePartyUpdate = 0x83;
constexpr std::uint8_t kMessage19 = 0x19;
const char* const kResendingParty = "Resending party of player ";

} // namespace

// The stream the received packets are decompressed into.
CMemoryStream g_receivedMessages;

// --- The leader died ----------------------------------------------------------------------

void CPlayerServer::OnLeaderDied() {
    CPlayer* player = player_;
    CParty* party = player->Party();
    if (!IsMultiplayerClient()) {
        std::uint8_t type = kMessageGameOver;
        out_.WriteValue(type);
        CUnit* hero = g_serverWorld.FindUnit(static_cast<int>(party->members_[0].unitId));
        if (hero) SendObject0B(hero, AckKind::StopSpeech);
        return;
    }

    // Both ids stay unset in the original (stack garbage) when the party is not a single hero;
    // here they are -1, which no object has.
    int oldId = kNoId;
    int newId = kNoId;
    if (std::ssize(party->members_) == 1) {
        int lost = x87::RoundToInt(static_cast<float>(static_cast<float>(party->money_) * g_logicSettings.lostMoney));
        party->money_ -= lost;
        oldId = static_cast<int>(party->members_[0].unitId);
        CUnit* hero = g_serverWorld.FindUnit(oldId);
        if (hero) SendObject0B(hero, AckKind::StopSpeech);
        newId = oldId;
        if (newId < kRevivedIdBase) newId += kRevivedIdStep;
        while (g_serverWorld.FindObject(static_cast<unsigned>(newId))) ++newId;
        g_serverWorld.AddSharedObject(oldId, static_cast<std::uint32_t>(lost), &player->items_);
        player->TakeAllItems();
    }
    if (partyDeployed_) {
        g_serverWorld.CollectParty(player, nullptr);
        partyDeployed_ = 0;
    }
    if (std::ssize(party->members_) == 1) {
        PartyUnitRecord& record = party->members_[0];
        record.unitId = static_cast<std::uint32_t>(newId);
        auto* stats = &record.stats;
        stats->RestoreSlots();
        float experience = static_cast<float>(stats->pendingExperience + stats->experience);
        auto* npc = g_unitDatabase.npcs.FindByName(record.texts[5]);
        if (npc) {
            float above = static_cast<float>(experience - npc->experienceFloor);
            experience = above <= 0.0f ? 0.0f : above;   // a NaN is kept
        }
        stats->TakePendingExperience(g_logicSettings.lostXp * experience);
    }
    SendParty();
    lastPosition_.z = kUnknownZ;
    if (!partyDeployed_) {
        g_serverWorld.Redeploy(player, nullptr, 1);
        partyDeployed_ = 1;
    }
    CUnit* revived = g_serverWorld.FindUnit(newId);
    CUnit* dead = g_serverWorld.FindUnit(oldId);
    if (!revived || !dead) return;
    HeldUnits& known = revived->ServerController()->brain.memory.seenCorpses;
    for (auto node = dead->ServerController()->brain.memory.seenCorpses.begin(); node != dead->ServerController()->brain.memory.seenCorpses.end();) {
        CUnit* object = *node;
        ++node;
        if (object && object->GetClassId() == ClassId::Unit && !lib::Contains(known, object)) known.push_back(object);
    }
    if (!lib::Contains(known, dead)) known.push_back(dead);
}

// --- Message 0x36, a use order --------------------------------------------------------

double UseOrderDistanceSquared(const Vector3& a, const Vector3& b) {
    double dx = a.x - b.x;
    double dy = a.y - b.y;
    double dz = a.z - b.z;
    return LengthSquaredUnrounded<Sum3::ZYX, double>(dx, dy, dz);
}

Vector3* AIPath::TargetPoint(Vector3* out) const {
    double scale = 0.5;
    out->z = 0.0f;
    out->x = static_cast<float>(scale * targetX);
    out->y = static_cast<float>(scale * targetY);
    return out;
}

void AIOrder::MakeUse(CMapObject* object, UseSource use) {
    Clear();
    kind = UnitOrderKind::Use;
    if (object != target) {
        target.Assign(object);
    }
    source = static_cast<std::uint32_t>(use);
}

Vector3* AIOrder::AimPoint(Vector3* out) const {
    if (target == nullptr) {
        out->x = point.x;
        out->y = point.y;
        out->z = point.z;
        return out;
    }
    target->AimPoint(out);
    return out;
}

int CPlayerServer::OnUseOrder() {
    CMemoryStream& in = g_receivedMessages;
    std::vector<CUnit*> units{};
    int count = 0;
    in.ReadValue(count);
    for (int i = 0; i < count; ++i) {
        int id = 0;
        in.ReadValue(id);
        CUnit* unit = g_serverWorld.FindUnit(id);
        if (unit && !(unit->flags_ & MapObjectFlag::Blocked)) lib::SetAtGrow(units, std::ssize(units), unit);
    }
    int flag = 0, value = 0, objectId = 0;
    int skillWord = 0;
    in.ReadValue(flag);
    in.ReadValue(skillWord);
    in.ReadValue(value);
    in.ReadValue(objectId);
    const UseSource kind = static_cast<UseSource>(skillWord);
    // A map object (the use order's target), or null.
    CMapObject* object = g_serverWorld.FindObject(static_cast<unsigned>(objectId));

    CUnit* chosen = nullptr;
    if (std::ssize(units) >= 2) {
        AIPath path;
        AIOrder order;
        order.Clear();
        order.MakeUse(object, kind);
        Vector3 aim;
        order.AimPoint(&aim);
        CUnit* first = nullptr;
        bool accepted = false;
        while (!units.empty()) {
            float best = kFarAway;
            CUnit* nearest = nullptr;
            int nearestIndex = 0;
            for (int i = 0; i < std::ssize(units); ++i) {
                double distance = UseOrderDistanceSquared(aim, units[i]->localPosition_);
                EI_NAN_AUDIT(distance, best);
                if (distance < best) {
                    best = static_cast<float>(distance);
                    nearest = units[i];
                    nearestIndex = i;
                }
            }
            if (!nearest) break;
            if (!first) first = nearest;
            CUnitServer* controller = nearest->ServerController();
            if (controller->CanReachAndPlan(&order)) {
                chosen = nearest;
                accepted = true;
                break;
            }
            Vector3 from;
            if (g_serverWorld.aiMap_.FindPath(nearest, aim, &path, kPathRange, 0.0f)) {
                Vector3 end;
                from = *path.TargetPoint(&end);
            } else {
                from = nearest->localPosition_;
            }
            if (controller->CanReachAndPlanFrom(&order, from)) {
                chosen = nearest;
                accepted = true;
                break;
            }
            units.erase(units.begin() + nearestIndex);
        }
        if (!accepted) chosen = first;
        order.target.Reset();
    } else if (std::ssize(units) == 1) {
        chosen = units[0];
    }

    if (chosen) {
        AIBrain* brain = &chosen->ServerController()->brain;
        if (PlayerMode* mode = brain->FindPlayerMode())
            mode->CommandUse(brain, object, kind, value, flag != 0);
    }
    return 1;
}

// --- Zone changes, joining, party blocks, the walk / attack orders -------------------------

void FormationOffsets(const std::vector<CUnit*>* units, std::vector<Vector3>* offsets) {
    int count = std::ssize(*units);
    if (count < 1) return;
    if (count == 1) {
        offsets->resize(1);
        (*offsets)[0] = Vector3{0.0f, 0.0f, 0.0f};
        return;
    }
    float sumX = 0.0f;
    float sumY = 0.0f;
    for (int i = 0; i < count; ++i) {
        const Vector3* at = &(*units)[i]->localPosition_;
        sumX = static_cast<float>(static_cast<double>(sumX) + at->x);
        sumY = static_cast<float>(static_cast<double>(sumY) + at->y);
    }
    float n = static_cast<float>(count);
    float centreX = static_cast<float>(static_cast<double>(sumX) / n);
    float centreY = static_cast<float>(static_cast<double>(sumY) / n);
    offsets->resize(count);
    for (int i = 0; i < std::ssize(*units); ++i) {
        const Vector3* at = &(*units)[i]->localPosition_;
        float dx = static_cast<float>(static_cast<double>(at->x) - centreX);
        float dy = static_cast<float>(static_cast<double>(at->y) - centreY);
        double length = std::sqrt(LengthSquared2DUnrounded<double>(dx, dy));
        if (length > 0.0) {   // the original skips zero and NaN
            double inverse = 1.0 / length;
            dx = static_cast<float>(dx * inverse);
            dy = static_cast<float>(dy * inverse);
        }
        (*offsets)[i] = Vector3{dx, dy, 0.0f};
    }
}

void CPlayerServer::LeaveToZone(std::string_view zone, int entrance) {
    if (state_ != 1) return;
    CPlayer* player = player_;
    CParty* party = player->Party();
    int campaign = g_serverWorld.sessionKind_;
    if (campaign == 1 || campaign == 3) {
        CUnit* leader = g_serverWorld.FindUnit(static_cast<int>(party->members_[0].unitId));
        if (leader == nullptr || leader->IsDead() != 0) {
            LeaderDied();
            if (!IsMultiplayerClient()) return;
        }
    }
    if (partyDeployed_) {
        g_serverWorld.CollectParty(player, nullptr);
        partyDeployed_ = 0;
    }
    state_ = 3;
    party->entrance_ = static_cast<std::uint32_t>(entrance);
    party->zone_ = zone;
    int mode = g_serverWorld.gameResult_;
    if (mode == 1) {
        g_partyManager.BeginRedeploy(party);
    } else if (mode == 2) {
        if (std::ssize(party->members_) == 1) party->members_[0].unitId = kLoneHeroRecordId;
        SendParty();
    }
    std::uint8_t type = kMessageZone;
    out_.WriteValue(type);
    out_.WriteString(zone);
    out_.WriteValue(entrance);
    Flush();
}

void CPlayerServer::LeaveAndFlush() {
    if (state_ == 1) {
        if (partyDeployed_) {
            g_serverWorld.CollectParty(player_, nullptr);
            partyDeployed_ = 0;
        }
        if (g_serverWorld.gameResult_ == 2) SendParty();
    }
    if (state_ != 3) {
        state_ = 3;
        std::uint8_t type = 0x01;
        out_.WriteValue(type);
    }
    Flush();
}

int CPlayerServer::OnRefreshParty() {
    player_->ForgetKnownObjects();
    SendParty();
    return 1;
}

int CPlayerServer::OnTakeExit() {
    std::string zone;
    int exit = 0;
    int entrance = 0;
    g_receivedMessages.ReadValue(exit);
    if (!g_serverWorld.GetZoneExit(exit, &zone, &entrance)) return 0;
    LeaveToZone(zone, entrance);
    return 1;
}

int CPlayerServer::OnJoined() {
    if (state_ != 2) return 0;
    state_ = 1;
    CPlayer* player = player_;
    player->sessionKind_ = player->ServerWorld()->sessionKind_;
    g_serverWorld.Redeploy(player, &placements_, 1);
    player->dirty_ |= PlayerDirty::GameState;
    return 1;
}

int CPlayerServer::OnLeaveToZone() {
    std::string zone = g_receivedMessages.ReadString();
    int entrance = 0;
    g_receivedMessages.ReadValue(entrance);
    LeaveToZone(zone, entrance);
    return 1;
}

int CPlayerServer::OnBriefingDone() {
    std::string name = g_receivedMessages.ReadString();
    g_serverWorld.CompleteBriefing(player_->playerId_, name);
    return 1;
}

int CPlayerServer::OnNewParty() {
    CParty local;
    CParty* party = &local;
    party->LoadParty(&g_receivedMessages, 1);
    if (std::ssize(party->members_) != 1) return 0;
    int* key = &player_->partyId_;
    PartyNextId(key);
    int& next = g_nextRecordObjectId;
    while (g_serverWorld.FindObject(static_cast<unsigned>(next)) != nullptr) ++next;
    party->members_[0].unitId = static_cast<std::uint32_t>(next);
    ++next;
    g_partyManager.StoreParty(key, party);
    return 1;
}

int CPlayerServer::OnResume() {
    if (state_ != 4) return 0;
    state_ = 1;
    if (!partyDeployed_) {
        g_serverWorld.Redeploy(player_, nullptr, 1);
        partyDeployed_ = 1;
    }
    return 1;
}

int CPlayerServer::OnWalk() { return MoveUnits(&PlayerMode::CommandWalk); }
int CPlayerServer::OnGoTo() { return MoveUnits(&PlayerMode::CommandGoTo); }

int CPlayerServer::OnAttack() {
    CMemoryStream& in = g_receivedMessages;
    std::vector<CUnit*> units{};
    ReadUnits(in, &units);
    int ctrl = 0, how = 0, objectId = 0;
    in.ReadValue(ctrl);
    in.ReadValue(how);
    in.ReadValue(objectId);
    if (CMapObject* object = g_serverWorld.FindObject(static_cast<unsigned>(objectId))) {
        for (int i = 0; i < std::ssize(units); ++i) {
            AIBrain* brain = &units[i]->ServerController()->brain;
            if (PlayerMode* mode = brain->FindPlayerMode())
                mode->CommandAttack(brain, object, how, ctrl != 0);
        }
    }
    return 1;
}

int CPlayerServer::OnPartyUpdate() {
    CMemoryStream block;
    CMemoryStream& in = g_receivedMessages;
    in.ReadSized(&block);
    block.CMemoryStream::Seek(0);
    if (in.ok_) player_->Party()->LoadParty(&block, 0);
    return 1;
}

// --- The login -----------------------------------------------------------------------------

bool IsPrintableText(const char* text) {
    // By character, each a valid UTF-8 sequence of a printable code point, within the login field's
    // kLoginTextLength bytes; an empty text fails. (The original's isprint of each signed byte passed UTF-8's
    // bytes only through the "english" locale's table and the bytes before it.)
    for (int i = 0; i < kLoginTextLength;) {
        if (text[i] == 0) return i != 0;
        unsigned c;
        i += text::DecodeUtf8(text + i, static_cast<std::size_t>(kLoginTextLength - i), &c);
        if (!text::IsPrintableCodePoint(c)) return false;
    }
    return true;
}

int CPlayerServer::OnLogin() {
    PlayerLoginMessage login;
    login.partyId = 0;
    g_receivedMessages.ReadValue(login);
    if (state_ != 0) return 0;
    CPlayer* player = player_;
    if (login.version != kClientVersion) {
        int length = 0x100;
        char format[0x100];
        auto* world = player->ServerWorld();
        world->messages_.GetStringBuffer("SrvErrMsg", "WrongClientVersion", format, &length, "");
        const std::string text = str::Format(format, 1, 0);
        std::uint8_t type = kMessageError;
        out_.WriteValue(type);
        out_.WriteString(text);
        state_ = 3;
        return 0;
    }
    if (!IsPrintableText(login.name)) return 0;
    if (!IsPrintableText(login.password)) return 0;
    login.name[kLoginTextLength] = 0;
    login.password[kLoginTextLength] = 0;
    CParty* party = player->Party();
    switch (g_serverWorld.gameResult_) {
    case 1:
        g_partyManager.EndRedeploy(party);
        party->OpenEdgeExits();
        break;
    case 2:
    case 3:
        if (!g_partyManager.LoadParty(&player->partyId_, party))
            return 0;
        break;
    case 4:
        party->Clear(1, 0);
        break;
    default:
        break;
    }
    std::uint8_t type = kMessageLogin;
    int id = player->id_;
    out_.WriteValue(type);
    out_.WriteValue(id);
    SendParty();
    state_ = 1;
    return 1;
}

// --- Party resends, message 0x19 -----------------------------------------------------------

void CPlayerServer::SendPartyUpdate() {
    CMemoryStream block;
    player_->Party()->SaveParty(&block, 0);
    std::uint8_t type = kMessagePartyUpdate;
    out_.WriteValue(type);
    out_.WriteSized(&block);
}

void CPlayerServer::ResendParty() {
    if (state_ != 1 || !partyDeployed_) return;
    CPlayer* player = player_;
    console::EndLine(console::g_netLogOut << kResendingParty << player->playerId_);
    g_serverWorld.SyncPartyRecords(player);
    SendParty();
    g_serverWorld.ClearPlayerItems(player);
}

void CPlayerServer::SendPair19(std::uint8_t a, std::uint32_t b) {
    std::uint8_t type = kMessage19;
    out_.WriteValue(type);
    out_.WriteValue(a);
    out_.WriteValue(b);
}
