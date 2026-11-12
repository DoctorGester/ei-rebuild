// PlayerServer: what the server does for its player beyond the per-tick sync, the leader's death and the
// client's messages (handlers by type, Receive's switch). See player_messages.h and
// docs/notes/objects.md ("Players").

#include "game/ai/ai_map.h"
#include "engine/core/asset_text.h"
#include "game/combat/combat.h"
#include "game/console/console.h"
#include "engine/core/core_maths.h"
#include "engine/core/core_streams.h"
#include "game/app/singletons.h"
#include "game/items/item_manager.h"
#include "game/app/logic_settings.h"
#include "game/objects/map_object.h"
#include "game/players/party.h"
#include "game/players/party_manager.h"
#include "game/players/party_records.h"
#include "game/players/player_messages.h"
#include "game/players/player.h"
#include "engine/data/registry.h"
#include "game/units/unit.h"
#include "game/ai/unit_ai.h"
#include "game/units/unit_db.h"
#include "game/world/world.h"
#include "game/map/map_database.h"
#include "game/net/net_session.h"

#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/rounding.h"
#include "lib/str.h"
#include "lib/text_case.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>

namespace {

constexpr int kRevivedIdBase = 1001000000;            // LeaderDied: ids below it are moved up
constexpr int kRevivedIdStep = 1000000;
constexpr int kNoId = -1;                             // see LeaderDied: the original's uninitialised ids
constexpr float kUnknownZ = -1313.0f;                 // lastPosition.z after a revival
constexpr float kPathRange = 1000000.0f;              // FindPath's range for the use order
constexpr float kNearestSearchStart = 1e30f;                     // the nearest-unit search's start

// The next object id for a new party's record (the party manager stores the party).
int g_nextRecordObjectId = /*00794E3C*/ 1000000013;

constexpr std::uint32_t kLoneHeroRecordId = 0x3B9AF5CE;   // 999999950

// The unit list of the walk / attack orders: a count, then that many ids (missing units and units
// flagged 0x800000 are left out).
void ReadUnits(MemoryStream& in, std::vector<Unit*>* units) {
    for (int count = in.ReadCount(); count > 0; --count) {
        Unit* unit = g_serverWorld.FindUnit(in.ReadId());
        if (unit && !(unit->flags & MapObjectFlag::Blocked)) lib::SetAtGrow(*units, lib::Count(*units), unit);
    }
}

constexpr const char* kOtherDatabaseText =
    "The server refused your party: it holds items or spells that the server's item and spell databases do not "
    "have.";

// A party from the client that names records this game's databases lack (a player with another items.idb or
// spells.sdb): the client gets message 0x11 with the reason and the player leaves (Receive then returns 0, and the
// world server drops the player), so the party is never deployed or stored. Returns true: the message was read.
bool DropForOtherDatabase(PlayerServer* server) {
    console::EndLine(console::g_netLogOut << "player " << server->player->playerId
                                          << ": the party names items or spells this game's databases lack, dropped");
    WriteType(&server->out, ServerMessage::Error);
    server->out.WriteString(kOtherDatabaseText);
    server->state = PlayerServer::State::Leaving;
    return true;
}

enum class MoveCommand { Walk, GoTo };
void MoveUnits(MemoryStream& in, MoveCommand command) {
    std::vector<Unit*> units{};
    std::vector<Vector3> offsets{};
    ReadUnits(in, &units);
    int run = 0;
    Vector3 to{};
    in.ReadValue(run);
    in.ReadValue(to);
    FormationOffsets(units, &offsets);
    for (int i = 0; i < std::ssize(units); ++i) {
        AIBrain* brain = &units[i]->ServerController()->brain;
        PlayerMode* mode = brain->FindModeOf<PlayerMode>();
        if (!mode) continue;
        if (command == MoveCommand::Walk) mode->CommandWalk(brain, to, std::ssize(units) > 1, run != 0, offsets[i]);
        else mode->CommandGoTo(brain, to, std::ssize(units) > 1, run != 0, offsets[i]);
    }
}

// The companions the host lets each player add (ServerSession::companions), 0 without a game server.
int HostCompanions() {
    net::ServerSession* server = net::g_gameServer;
    return IsLive(server) ? static_cast<int>(server->companions) : 0;
}

// The multiplayer base: a local zone of a multiplayer game, served by the player's own game.
bool InMultiplayerBase() {
    return IsMultiplayerClient() && g_serverWorld.gameState == GameState::Playing && g_serverWorld.zoneMode == ZoneMode::Base;
}

// A dead hero's experience penalty: lostXp of its experience above its class's floor (a NaN kept), taken from the
// pending experience.
void TakeDeathPenalty(UnitStats* stats, std::string_view prototype) {
    float experience = stats->pendingExperience + stats->experience;
    if (auto* npc = g_unitDatabase.npcs.FindByName(prototype)) {
        float above = experience - npc->experienceFloor;
        experience = MaxKeepB(0.0f, above);   // a NaN is kept
    }
    stats->TakePendingExperience(g_logicSettings.lostXp * experience);
}

constexpr int kClientVersion = 0x64;
constexpr int kLoginTextLength = 0x3F;
const char* const kResendingParty = "Resending party of player ";

} // namespace

// --- The leader died ----------------------------------------------------------------------

void PlayerServer::LeaderDied() {
    Player* currentPlayer = player;
    Party* party = currentPlayer->GetParty();
    if (!IsMultiplayerClient()) {
        WriteType(&out, ServerMessage::GameLost);
        Unit* hero = g_serverWorld.FindUnit(static_cast<int>(party->members[0].unitId));
        if (hero) SendUnitSpeech(hero, AckKind::StopSpeech);
        return;
    }

    // Both ids are -1 (which no object has) without a hero; the original left them unset.
    int oldId = kNoId;
    int newId = kNoId;
    // Companions keep what they did in the zone; the redeploy below brings them back with the hero.
    if (std::ssize(party->members) > 1) g_serverWorld.SyncCompanions(currentPlayer);
    if (!party->members.empty()) {
        int lost = lib::RoundEven(static_cast<float>(party->money) * g_logicSettings.lostMoney);
        party->money -= lost;
        oldId = static_cast<int>(party->members[0].unitId);
        Unit* hero = g_serverWorld.FindUnit(oldId);
        if (hero) SendUnitSpeech(hero, AckKind::StopSpeech);
        newId = oldId;
        if (newId < kRevivedIdBase) newId += kRevivedIdStep;
        while (g_serverWorld.FindObject(static_cast<unsigned>(newId))) ++newId;
        g_serverWorld.AddSharedObject(oldId, static_cast<std::uint32_t>(lost), currentPlayer->items);
        currentPlayer->TakeAllItems();
    }
    if (partyDeployed) {
        g_serverWorld.CollectParty(currentPlayer, nullptr);
        partyDeployed = 0;
    }
    if (!party->members.empty()) {
        PartyUnitRecord& record = party->members[0];
        record.unitId = static_cast<std::uint32_t>(newId);
        record.stats.RestoreSlots();
        TakeDeathPenalty(&record.stats, record.prototype);
    }
    SendParty();
    lastPosition.z = kUnknownZ;
    if (!partyDeployed) {
        g_serverWorld.Redeploy(currentPlayer, nullptr, 1);
        partyDeployed = 1;
    }
    Unit* revived = g_serverWorld.FindUnit(newId);
    Unit* dead = g_serverWorld.FindUnit(oldId);
    if (!revived || !dead) return;
    HeldUnits& known = revived->ServerController()->brain.memory.seenCorpses;
    for (auto node = dead->ServerController()->brain.memory.seenCorpses.begin(); node != dead->ServerController()->brain.memory.seenCorpses.end();) {
        Unit* object = *node;
        ++node;
        if (object && object->GetClassId() == ClassId::Unit && !lib::Contains(known, object)) known.push_back(object);
    }
    if (!lib::Contains(known, dead)) known.push_back(dead);
}

void PlayerServer::CompanionDied(Unit* unit) {
    if (!IsMultiplayerClient()) return;
    PartyUnitRecord* record = player->GetParty()->Find(unit->id);
    if (!record) return;
    TakeDeathPenalty(&unit->ServerController()->stats, record->prototype);
    TakeDeathPenalty(&record->stats, record->prototype);
    console::EndLine(console::g_netLogOut << "player " << player->playerId << ": companion " << unit->id << " died");
}

// --- Message 0x36, a use order --------------------------------------------------------

bool PlayerServer::OnUseOrder() {
    std::vector<Unit*> units{};
    for (int count = in.ReadCount(); count > 0; --count) {
        Unit* unit = g_serverWorld.FindUnit(in.ReadId());
        if (unit && !(unit->flags & MapObjectFlag::Blocked)) lib::SetAtGrow(units, lib::Count(units), unit);
    }
    int flag = 0, value = 0;
    int skillWord = 0;
    in.ReadValue(flag);
    in.ReadValue(skillWord);
    in.ReadValue(value);
    const int objectId = in.ReadId();
    const UseSource kind = static_cast<UseSource>(skillWord);
    // A map object (the use order's target), or null.
    MapObject* object = g_serverWorld.FindObject(static_cast<unsigned>(objectId));

    Unit* chosen = nullptr;
    if (std::ssize(units) >= 2) {
        AIPath path;
        AIOrder order;
        order.Clear();
        order.MakeUse(object, kind);
        Vector3 aim = order.AimPoint();
        Unit* first = nullptr;
        bool accepted = false;
        while (!units.empty()) {
            float best = kNearestSearchStart;
            Unit* nearest = nullptr;
            int nearestIndex = 0;
            for (int i = 0; i < std::ssize(units); ++i) {
                const float distance = DistanceSquared(aim, units[i]->localPosition);
                if (distance < best) {
                    best = distance;
                    nearest = units[i];
                    nearestIndex = i;
                }
            }
            if (!nearest) break;
            if (!first) first = nearest;
            UnitServer* controller = nearest->ServerController();
            if (controller->CanReachAndPlan(&order)) {
                chosen = nearest;
                accepted = true;
                break;
            }
            Vector3 from;
            if (g_serverWorld.aiMap.FindPath(nearest, aim, &path, kPathRange, 0.0f)) {
                from = path.TargetPoint();
            } else {
                from = nearest->localPosition;
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
        if (PlayerMode* mode = brain->FindModeOf<PlayerMode>())
            mode->CommandUse(brain, object, kind, value, flag != 0);
    }
    return true;
}

// --- Zone changes, joining, party blocks, the walk / attack orders -------------------------

void FormationOffsets(const std::vector<Unit*>& units, std::vector<Vector3>* offsets) {
    int count = lib::Count(units);
    if (count < 1) return;
    if (count == 1) {
        offsets->resize(1);
        (*offsets)[0] = Vector3{0.0f, 0.0f, 0.0f};
        return;
    }
    Vector2 sum{0.0f, 0.0f};
    for (int i = 0; i < count; ++i) sum += XY(units[i]->localPosition);
    const Vector2 centre = sum / static_cast<float>(count);
    offsets->resize(count);
    for (int i = 0; i < std::ssize(units); ++i) {
        Vector2 d = XY(units[i]->localPosition) - centre;
        const float length = Length2D(d);
        if (length > 0.0f) d *= 1.0f / length;   // the original skips zero and NaN
        (*offsets)[i] = WithZ(d, 0.0f);
    }
}

void PlayerServer::LeaveToZone(std::string_view zone, int entrance) {
    if (state != State::Joined) return;
    Player* currentPlayer = player;
    Party* party = currentPlayer->GetParty();
    const GameState gameState = g_serverWorld.gameState;
    if (IsSinglePlayerMode(g_serverWorld.zoneMode)) {
        Unit* leader = g_serverWorld.FindUnit(static_cast<int>(party->members[0].unitId));
        if (leader == nullptr || leader->IsDead() != 0) {
            LeaderDied();
            if (!IsMultiplayerClient()) return;
        }
    }
    // Companions take what they did in the zone home: the client writes them back into their own files.
    if (gameState == GameState::Network && std::ssize(party->members) > 1) g_serverWorld.SyncCompanions(currentPlayer);
    if (partyDeployed) {
        g_serverWorld.CollectParty(currentPlayer, nullptr);
        partyDeployed = 0;
    }
    state = State::Leaving;
    party->entrance = static_cast<std::uint32_t>(entrance);
    party->zone = zone;
    if (gameState == GameState::Playing) {
        g_partyManager.records.BeginRedeploy(party);
    } else if (gameState == GameState::Network) {
        if (!party->members.empty()) party->members[0].unitId = kLoneHeroRecordId;
        SendParty();
    }
    WriteType(&out, ServerMessage::ZoneChange);
    out.WriteString(zone);
    out.WriteValue(entrance);
    Flush();
}

void PlayerServer::Leave() {
    if (state == State::Joined) {
        if (g_serverWorld.gameState == GameState::Network && std::ssize(player->GetParty()->members) > 1)
            g_serverWorld.SyncCompanions(player);
        if (partyDeployed) {
            g_serverWorld.CollectParty(player, nullptr);
            partyDeployed = 0;
        }
        if (g_serverWorld.gameState == GameState::Network) SendParty();
    }
    if (state != State::Leaving) {
        state = State::Leaving;
        WriteType(&out, ServerMessage::Disconnect);
    }
    Flush();
}

// Unreached in the shipped game: our client never sends message 0x02 (kept for a peer that does).
bool PlayerServer::OnRefreshParty() {
    player->ForgetKnownObjects();
    SendParty();
    return true;
}

bool PlayerServer::OnTakeExit() {
    int exit = 0;
    in.ReadValue(exit);
    const ZoneExit* taken = g_serverWorld.FindZoneExit(exit);
    if (!taken) return false;
    const std::string zone = taken->target;   // a copy: leaving may replace the exits
    LeaveToZone(zone, taken->entrance);
    return true;
}

bool PlayerServer::OnJoined() {
    if (state != State::GameStateSent) return false;
    state = State::Joined;
    Player* currentPlayer = player;
    currentPlayer->sessionKind = static_cast<int>(currentPlayer->ServerWorld()->zoneMode);
    g_serverWorld.Redeploy(currentPlayer, &placements, 1);
    currentPlayer->dirty |= PlayerDirty::GameState;
    return true;
}

bool PlayerServer::OnLeaveToZone() {
    std::string zone = in.ReadString();
    int entrance = 0;
    in.ReadValue(entrance);
    LeaveToZone(zone, entrance);
    return true;
}

// Unreached in the shipped game: our client never sends message 0x07 (kept for a peer that does).
bool PlayerServer::OnBriefingDone() {
    std::string name = in.ReadString();
    g_serverWorld.CompleteBriefing(player->playerId, name);
    return true;
}

bool PlayerServer::OnNewParty() {
    Party local;
    Party* party = &local;
    party->LoadParty(&in, true);
    if (!HasEveryRecord(party)) return DropForOtherDatabase(this);
    if (std::ssize(party->members) != 1) return false;
    player->partyId = PartyNextId();
    int& next = g_nextRecordObjectId;
    while (g_serverWorld.FindObject(static_cast<unsigned>(next)) != nullptr) ++next;
    party->members[0].unitId = static_cast<std::uint32_t>(next);
    ++next;
    g_partyManager.StoreParty(player->partyId, party);
    return true;
}

bool PlayerServer::OnResume() {
    if (state != State::Disconnected) return false;
    state = State::Joined;
    if (!partyDeployed) {
        g_serverWorld.Redeploy(player, nullptr, 1);
        partyDeployed = 1;
    }
    return true;
}

bool PlayerServer::OnWalk() {
    MoveUnits(in, MoveCommand::Walk);
    return true;
}
bool PlayerServer::OnGoTo() {
    MoveUnits(in, MoveCommand::GoTo);
    return true;
}

bool PlayerServer::OnAttack() {
    std::vector<Unit*> units{};
    ReadUnits(in, &units);
    int ctrl = 0, how = 0;
    in.ReadValue(ctrl);
    in.ReadValue(how);
    const int objectId = in.ReadId();
    if (MapObject* object = g_serverWorld.FindObject(static_cast<unsigned>(objectId))) {
        for (int i = 0; i < std::ssize(units); ++i) {
            AIBrain* brain = &units[i]->ServerController()->brain;
            if (PlayerMode* mode = brain->FindModeOf<PlayerMode>())
                mode->CommandAttack(brain, object, how, ctrl != 0);
        }
    }
    return true;
}

bool PlayerServer::OnPartyUpdate() {
    MemoryStream block;
    in.ReadSized(&block);
    block.MemoryStream::Seek(0);
    if (!in.ok) return true;
    Party* party = player->GetParty();
    party->LoadParty(&block, false);
    if (HasEveryRecord(party)) return true;
    party->Clear(false, false);
    return DropForOtherDatabase(this);
}

// The list of companions changes only in the multiplayer base zone, a local zone served by the player's own game:
// there a pick joins (any `restore`) within the base's limit (net::BaseCompanionLimit); a network zone only takes the
// client's list asked for again at its start (`restore`), within the host's limit; every other add is read and dropped
// (logged).
bool PlayerServer::OnAddCompanion() {
    std::uint32_t restore = 0;
    in.ReadValue(restore);
    MemoryStream block;
    in.ReadSized(&block);
    block.MemoryStream::Seek(0);
    if (!in.ok) return true;
    const bool base = InMultiplayerBase();
    if (!restore && !base) {
        console::EndLine(console::g_netLogOut << "player " << player->playerId
                                              << ": companion refused outside the base zone");
        return true;
    }
    Party companion;
    companion.LoadParty(&block, false);
    Party* party = player->GetParty();
    Unit* leader = party->members.empty() ? nullptr : g_serverWorld.FindUnit(static_cast<int>(party->members[0].unitId));
    const int limit = base ? net::BaseCompanionLimit() : g_serverWorld.gameState == GameState::Network ? HostCompanions() : 0;
    const bool room = std::ssize(party->members) <= limit;
    if (state != State::Joined || !room || !leader || leader->IsDead() || std::ssize(companion.members) != 1 ||
        !HasEveryRecord(&companion)) {
        console::EndLine(console::g_netLogOut << "player " << player->playerId << ": companion refused");
        return true;
    }
    int& next = g_nextRecordObjectId;
    while (g_serverWorld.FindObject(static_cast<unsigned>(next)) != nullptr) ++next;
    PartyUnitRecord& record = party->members.emplace_back();
    record.CopyFrom(companion.members[0], party);
    record.unitId = static_cast<std::uint32_t>(next);
    ++next;
    record.leaderId = leader->id;
    std::vector<PartyUnitRecord*> records{&record};
    g_serverWorld.CreateUnits(player, &records, leader->localPosition, leader->localRotation, nullptr);
    console::EndLine(console::g_netLogOut << "player " << player->playerId << ": companion " << record.scriptName
                                          << " joined as " << record.unitId);
    SendParty();
    return true;
}

// A companion sent away in the multiplayer base: its record and its unit go (the client has written the record
// into its file). Elsewhere read and dropped (logged).
bool PlayerServer::OnRemoveCompanion() {
    int unitId = 0;
    in.ReadValue(unitId);
    if (!in.ok) return true;
    std::vector<PartyUnitRecord>& members = player->GetParty()->members;
    if (state == State::Joined && InMultiplayerBase()) {
        for (int i = 1; i < std::ssize(members); ++i) {
            if (static_cast<int>(members[i].unitId) != unitId) continue;
            members.erase(members.begin() + i);
            g_serverWorld.RemoveObject(static_cast<unsigned>(unitId));
            console::EndLine(console::g_netLogOut << "player " << player->playerId << ": companion " << unitId
                                                  << " sent away");
            SendParty();
            return true;
        }
    }
    console::EndLine(console::g_netLogOut << "player " << player->playerId
                                          << ": companion removal refused outside the base zone");
    return true;
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

bool PlayerServer::OnLogin() {
    PlayerLoginMessage login;
    login.partyId = 0;
    in.ReadValue(login);
    if (state != State::NotLoggedIn) return false;
    Player* currentPlayer = player;
    if (login.version != kClientVersion) {
        int length = 0x100;
        char format[0x100];
        auto* world = currentPlayer->ServerWorld();
        world->messages.GetStringBuffer("SrvErrMsg", "WrongClientVersion", format, &length, "");
        const std::string text = str::Format(format, 1, 0);
        WriteType(&out, ServerMessage::Error);
        out.WriteString(text);
        state = State::Leaving;
        return false;
    }
    if (!IsPrintableText(login.name)) return false;
    if (!IsPrintableText(login.password)) return false;
    login.name[kLoginTextLength] = 0;
    login.password[kLoginTextLength] = 0;
    Party* party = currentPlayer->GetParty();
    switch (g_serverWorld.gameState) {
    case GameState::Playing:
        g_partyManager.records.EndRedeploy(party);
        party->OpenEdgeExits();
        break;
    case GameState::Network:
    case GameState::Network3:
        if (!g_partyManager.LoadParty(currentPlayer->partyId, party))
            return false;
        break;
    case GameState::MainMenu:
        party->Clear(true, false);
        break;
    default:
        break;
    }
    int playerId = currentPlayer->id;
    WriteType(&out, ServerMessage::LoginAccepted);
    out.WriteValue(playerId);
    SendParty();
    state = State::Joined;
    // Our clients mark their login; in a network zone they learn the host's companions.
    const bool marked =
        std::memcmp(login.password + kCompanionMarkAt, kCompanionLoginMark, sizeof(kCompanionLoginMark)) == 0;
    if (marked && HostCompanions() > 0 && g_serverWorld.gameState == GameState::Network) {
        WriteType(&out, ServerMessage::Companions);
        out.WriteValue(static_cast<std::uint32_t>(HostCompanions()));
    }
    return true;
}

// --- Party resends, message 0x19 -----------------------------------------------------------

void PlayerServer::SendPartyUpdate() {
    MemoryStream block;
    player->GetParty()->SaveParty(&block, false);
    WriteType(&out, ServerMessage::PartyUpdate);
    out.WriteSized(&block);
}

void PlayerServer::ResendParty() {
    if (state != State::Joined || !partyDeployed) return;
    Player* currentPlayer = player;
    console::EndLine(console::g_netLogOut << kResendingParty << currentPlayer->playerId);
    g_serverWorld.SyncPartyRecords(currentPlayer);
    SendParty();
    g_serverWorld.ClearPlayerItems(currentPlayer);
}

void PlayerServer::SendUnitControl(std::uint8_t event, std::uint32_t subject) {
    WriteType(&out, ServerMessage::UnitControl);
    out.WriteValue(event);
    out.WriteValue(subject);
}
