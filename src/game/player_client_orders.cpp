// The local view's orders (PlayerClient): the HUD and the battle screen queue them as messages for the
// server (PlayerServer's handlers, player_server_handlers.cpp). Each is a type byte and a payload written to
// the request stream; blocks the original writes in one piece are written in one piece here too.
#include "player_client.h"

#include "item.h"
#include "party.h"
#include "unit.h"
#include "world.h"

#include "game_singletons.h"
#include "lib/containers.h"

#include <cstring>
#include <vector>

namespace {

constexpr std::uint32_t kItemSlot = 0x80000000u;   // a quick item, not a spell

#pragma pack(push, 1)
struct OnUnitBlock {        // 13 bytes
    std::uint32_t slot;
    int caster;
    int target;
    std::uint8_t ctrl;
};
struct AtPointBlock {       // 21 bytes
    std::uint32_t slot;
    int caster;
    Vector3 point;
    std::uint8_t ctrl;
};
struct UnitByteBlock {      // 5 bytes
    int unit;
    std::uint8_t value;
};
#pragma pack(pop)
static_assert(sizeof(OnUnitBlock) == 13 && sizeof(AtPointBlock) == 21 && sizeof(UnitByteBlock) == 5);   // sent as these bytes

void WriteUnits(MemoryStream& out, const std::vector<Unit*>* units) {
    int count = lib::Count(*units);
    out.WriteValue(count);
    for (int i = 0; i < std::ssize(*units); ++i) {
        int id = (*units)[i] ? (*units)[i]->id : kNoObjectId;
        out.WriteValue(id);
    }
}

int SendMove(MemoryStream* out, ClientMessage type, const std::vector<Unit*>* units, const Vector3& to, int run) {
    WriteType(out, type);
    WriteUnits(*out, units);
    out->WriteValue(run);
    out->Write(&to, sizeof(Vector3));
    return 1;
}

// A potion used from a quick slot is counted off at once.
void SpendPotion(Unit* user, int slot) {
    auto* item = user->GetQuickItem(static_cast<std::uint32_t>(slot));
    if (item && static_cast<int>(item->count) > 0 && item->GetCategory() == QuickItemCategory::Potion) --item->count;
}

} // namespace

int PlayerClient::SendWalk(const std::vector<Unit*>* units, const Vector3& to, int run) {
    return SendMove(&out, ClientMessage::Walk, units, to, run);
}
int PlayerClient::SendGoTo(const std::vector<Unit*>* units, const Vector3& to, int run) {
    return SendMove(&out, ClientMessage::GoTo, units, to, run);
}

int PlayerClient::SendAttack(const std::vector<Unit*>* units, EngineObject* target, int how, int ctrl) {
    WriteType(&out, ClientMessage::Attack);
    WriteUnits(out, units);
    out.WriteValue(ctrl);
    out.WriteValue(how);
    int targetId = target->id;
    out.WriteValue(targetId);
    return 1;
}

int PlayerClient::SendCastOnUnit(int slot, Unit* caster, EngineObject* target, int ctrl) {
    OnUnitBlock block{static_cast<std::uint32_t>(slot), caster->id, target->id, static_cast<std::uint8_t>(ctrl)};
    WriteType(&out, ClientMessage::CastAtObject);
    out.WriteValue(block);
    return 1;
}

int PlayerClient::SendCastAtPoint(int slot, Unit* caster, const Vector3& point, int ctrl) {
    AtPointBlock block{static_cast<std::uint32_t>(slot), caster->id, point, static_cast<std::uint8_t>(ctrl)};
    WriteType(&out, ClientMessage::CastAtPoint);
    out.WriteValue(block);
    return 1;
}

int PlayerClient::SendItemOnUnit(int slot, Unit* user, EngineObject* target, int ctrl) {
    SpendPotion(user, slot);
    OnUnitBlock block{static_cast<std::uint32_t>(slot) | kItemSlot, user->id, target->id,
                      static_cast<std::uint8_t>(ctrl)};
    WriteType(&out, ClientMessage::CastAtObject);
    out.WriteValue(block);
    return 1;
}

int PlayerClient::SendItemAtPoint(int slot, Unit* user, const Vector3& point, int ctrl) {
    SpendPotion(user, slot);
    AtPointBlock block{static_cast<std::uint32_t>(slot) | kItemSlot, user->id, point,
                       static_cast<std::uint8_t>(ctrl)};
    WriteType(&out, ClientMessage::CastAtPoint);
    out.WriteValue(block);
    return 1;
}

int PlayerClient::SetWeapon(Unit* unit, int slot) {
    UnitByteBlock block{unit->id, static_cast<std::uint8_t>(slot)};
    WriteType(&out, ClientMessage::WantedWeapon);
    out.WriteValue(block);
    unit->pendingWeapon = slot;
    return 1;
}

int PlayerClient::SetMoveMode(Unit* unit, int mode) {
    WriteType(&out, ClientMessage::MoveStyle);
    UnitByteBlock block{unit->id, static_cast<std::uint8_t>(mode)};
    out.WriteValue(block);
    unit->moveMode2 = static_cast<MoveMode>(mode);
    return 1;
}

int PlayerClient::SetUnitFightAtWill(Unit* unit, int value) {
    unit->SetFightAtWill(value);
    WriteType(&out, ClientMessage::FightAtWill);
    int unitId = unit->id;
    out.WriteValue(unitId);
    std::uint8_t byte = static_cast<std::uint8_t>(value);
    out.WriteValue(byte);
    return 1;
}

int PlayerClient::SendFollow(Unit* unit, EngineObject* target) {
    int ids[2] = {unit->id, target ? target->id : kNoObjectId};
    WriteType(&out, ClientMessage::Follow);
    out.Write(ids, sizeof(ids));
    return 1;
}

int PlayerClient::SendPause(std::uint8_t value) {
    WriteType(&out, ClientMessage::Pause);
    std::uint8_t set = value != 0 ? 1 : 0;
    out.WriteValue(set);
    return 1;
}

int PlayerClient::SendUseOrder(Unit* unit, EngineObject* target, int kind, int value, int flag) {
    WriteType(&out, ClientMessage::UseOrder);
    int count = 1;
    out.WriteValue(count);
    int unitId = unit ? unit->id : kNoObjectId;
    out.WriteValue(unitId);
    out.WriteValue(flag);
    out.WriteValue(kind);
    out.WriteValue(value);
    int targetId = target->id;
    out.WriteValue(targetId);
    return 1;
}

int PlayerClient::SendUseDead(const std::vector<Unit*>* units, EngineObject* target, int flag) {
    WriteType(&out, ClientMessage::UseOrder);
    WriteUnits(out, units);
    out.WriteValue(flag);
    int kind = static_cast<int>(UseSource::SearchBody);
    out.WriteValue(kind);
    int value = kNoObjectId;
    out.WriteValue(value);
    int targetId = target->id;
    out.WriteValue(targetId);
    return 1;
}

int PlayerClient::SendPartyUpdate() {
    MemoryStream block;
    player->GetParty()->SaveParty(&block, 0);
    WriteType(&out, ClientMessage::PartyUpdate);
    out.WriteSized(&block);
    return 1;
}

void PlayerClient::Rejoin() {
    if (state != State::BetweenZones) return;
    g_gameFlow.ReturnLocalParty();
    SendPartyUpdate();
    WriteType(&out, ClientMessage::Joined);
    state = State::SendingCamera;
}

int PlayerClient::TakeExit(int exit) {
    Rejoin();
    WriteType(&out, ClientMessage::TakeExit);
    out.WriteValue(exit);
    return static_cast<int>(out.position);
}

int PlayerClient::RequestZone(std::string_view zone, int entrance) {
    Rejoin();
    WriteType(&out, ClientMessage::LeaveToZone);
    out.WriteString(zone);
    out.WriteValue(entrance);
    return static_cast<int>(out.position);
}

int PlayerClient::RequestGameState(int value) {
    WriteType(&out, ClientMessage::GameStateRequest);
    out.WriteValue(value);
    state = State::BetweenZones;
    return static_cast<int>(out.position);
}

int PlayerClient::SendResume() {
    WriteType(&out, ClientMessage::Resume);
    return static_cast<int>(out.position);
}
