// The local view's orders (PlayerClient): the HUD and the battle screen queue them as messages for the
// server (PlayerServer's handlers, player_server_handlers.cpp). Each is a type byte and a payload written to
// the request stream; blocks the original writes in one piece are written in one piece here too.
#include "game/players/player_client.h"

#include "game/app/game_flow.h"
#include "game/items/item.h"
#include "game/players/party.h"
#include "game/units/unit.h"
#include "game/world/world.h"

#include "game/app/singletons.h"
#include "game/players/player.h"
#include "lib/containers.h"

#include <cstring>
#include <vector>

namespace {

constexpr std::uint32_t kItemSlot = 0x80000000u;   // a quick item, not a spell

#pragma pack(push, 1)
struct OnUnitBlock {        // 13 bytes
    std::uint32_t slot = 0;
    int caster = 0;
    int target = 0;
    std::uint8_t ctrl = 0;
};
struct AtPointBlock {       // 21 bytes
    std::uint32_t slot = 0;
    int caster = 0;
    Vector3 point;
    std::uint8_t ctrl = 0;
};
struct UnitByteBlock {      // 5 bytes
    int unit = 0;
    std::uint8_t value = 0;
};
#pragma pack(pop)
static_assert(sizeof(OnUnitBlock) == 13 && sizeof(AtPointBlock) == 21 && sizeof(UnitByteBlock) == 5);   // sent as these bytes
constexpr bool NoPadding(const AtPointBlock*) {
    return lib::FieldsFill<&AtPointBlock::slot, &AtPointBlock::caster, &AtPointBlock::point, &AtPointBlock::ctrl>();
}

void WriteUnits(MemoryStream& out, const std::vector<Unit*>& units) {
    out.WriteCount(units.size());
    for (const Unit* unit : units) out.WriteId(unit);
}

void SendMove(MemoryStream* out, ClientMessage type, const std::vector<Unit*>& units, const Vector3& to, int run) {
    WriteType(out, type);
    WriteUnits(*out, units);
    out->WriteValue(run);
    out->WriteValue(to);
}

// A potion used from a quick slot is counted off at once.
void SpendPotion(Unit* user, int slot) {
    auto* item = user->GetQuickItem(static_cast<std::uint32_t>(slot));
    if (item && static_cast<int>(item->count) > 0 && item->GetCategory() == QuickItemCategory::Potion) --item->count;
}

} // namespace

void PlayerClient::SendWalk(const std::vector<Unit*>& units, const Vector3& to, int run) {
    SendMove(&out, ClientMessage::Walk, units, to, run);
}
void PlayerClient::SendGoTo(const std::vector<Unit*>& units, const Vector3& to, int run) {
    SendMove(&out, ClientMessage::GoTo, units, to, run);
}

void PlayerClient::SendAttack(const std::vector<Unit*>& units, EngineObject* target, int how, int ctrl) {
    WriteType(&out, ClientMessage::Attack);
    WriteUnits(out, units);
    out.WriteValue(ctrl);
    out.WriteValue(how);
    out.WriteId(target);
}

void PlayerClient::SendCastOnUnit(int slot, Unit* caster, EngineObject* target, int ctrl) {
    OnUnitBlock block{static_cast<std::uint32_t>(slot), caster->id, target->id, static_cast<std::uint8_t>(ctrl)};
    WriteType(&out, ClientMessage::CastAtObject);
    out.WriteValue(block);
}

void PlayerClient::SendCastAtPoint(int slot, Unit* caster, const Vector3& point, int ctrl) {
    AtPointBlock block{static_cast<std::uint32_t>(slot), caster->id, point, static_cast<std::uint8_t>(ctrl)};
    WriteType(&out, ClientMessage::CastAtPoint);
    out.WriteValue(block);
}

void PlayerClient::SendItemOnUnit(int slot, Unit* user, EngineObject* target, int ctrl) {
    SpendPotion(user, slot);
    OnUnitBlock block{static_cast<std::uint32_t>(slot) | kItemSlot, user->id, target->id,
                      static_cast<std::uint8_t>(ctrl)};
    WriteType(&out, ClientMessage::CastAtObject);
    out.WriteValue(block);
}

void PlayerClient::SendItemAtPoint(int slot, Unit* user, const Vector3& point, int ctrl) {
    SpendPotion(user, slot);
    AtPointBlock block{static_cast<std::uint32_t>(slot) | kItemSlot, user->id, point,
                       static_cast<std::uint8_t>(ctrl)};
    WriteType(&out, ClientMessage::CastAtPoint);
    out.WriteValue(block);
}

void PlayerClient::SetWeapon(Unit* unit, int slot) {
    UnitByteBlock block{unit->id, static_cast<std::uint8_t>(slot)};
    WriteType(&out, ClientMessage::WantedWeapon);
    out.WriteValue(block);
    unit->pendingWeapon = slot;
}

void PlayerClient::SetMoveMode(Unit* unit, int mode) {
    WriteType(&out, ClientMessage::MoveStyle);
    UnitByteBlock block{unit->id, static_cast<std::uint8_t>(mode)};
    out.WriteValue(block);
    unit->moveMode2 = static_cast<MoveMode>(mode);
}

void PlayerClient::SetUnitFightAtWill(Unit* unit, int value) {
    unit->SetFightAtWill(value);
    WriteType(&out, ClientMessage::FightAtWill);
    int unitId = unit->id;
    out.WriteValue(unitId);
    std::uint8_t byte = static_cast<std::uint8_t>(value);
    out.WriteValue(byte);
}

void PlayerClient::SendFollow(Unit* unit, EngineObject* target) {
    int ids[2] = {unit->id, target ? target->id : kNoObjectId};
    WriteType(&out, ClientMessage::Follow);
    out.Write(ids, sizeof(ids));
}

void PlayerClient::SendPause(std::uint8_t value) {
    WriteType(&out, ClientMessage::Pause);
    std::uint8_t set = value != 0 ? 1 : 0;
    out.WriteValue(set);
}

void PlayerClient::SendUseOrder(Unit* unit, EngineObject* target, int kind, int value, int flag) {
    WriteType(&out, ClientMessage::UseOrder);
    out.WriteCount(1);
    out.WriteId(unit);
    out.WriteValue(flag);
    out.WriteValue(kind);
    out.WriteValue(value);
    out.WriteId(target);
}

void PlayerClient::SendUseDead(const std::vector<Unit*>& units, EngineObject* target, int flag) {
    WriteType(&out, ClientMessage::UseOrder);
    WriteUnits(out, units);
    out.WriteValue(flag);
    int kind = static_cast<int>(UseSource::SearchBody);
    out.WriteValue(kind);
    int value = kNoObjectId;
    out.WriteValue(value);
    out.WriteId(target);
}

void PlayerClient::SendPartyUpdate() {
    MemoryStream block;
    player->GetParty()->SaveParty(&block, false);
    WriteType(&out, ClientMessage::PartyUpdate);
    out.WriteSized(&block);
}

void PlayerClient::SendAddCompanion(Party* companion, bool restore) {
    MemoryStream block;
    companion->SaveParty(&block, false);
    WriteType(&out, ClientMessage::AddCompanion);
    out.WriteValue(static_cast<std::uint32_t>(restore));
    out.WriteSized(&block);
}

void PlayerClient::SendRemoveCompanion(int unitId) {
    WriteType(&out, ClientMessage::RemoveCompanion);
    out.WriteValue(unitId);
}

void PlayerClient::Rejoin() {
    if (state != State::BetweenZones) return;
    g_gameFlow.ReturnLocalParty();
    SendPartyUpdate();
    WriteType(&out, ClientMessage::Joined);
    state = State::SendingCamera;
}

void PlayerClient::TakeExit(int exit) {
    Rejoin();
    WriteType(&out, ClientMessage::TakeExit);
    out.WriteValue(exit);
}

void PlayerClient::RequestZone(std::string_view zone, int entrance) {
    Rejoin();
    WriteType(&out, ClientMessage::LeaveToZone);
    out.WriteString(zone);
    out.WriteValue(entrance);
}

void PlayerClient::RequestGameState(int value) {
    WriteType(&out, ClientMessage::GameStateRequest);
    out.WriteValue(value);
    state = State::BetweenZones;
}

void PlayerClient::SendResume() {
    WriteType(&out, ClientMessage::Resume);
}
