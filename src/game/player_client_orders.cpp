// The local view's orders (CPlayerClient): the HUD and the battle screen queue them as messages for the
// server (CPlayerServer's handlers, player_server_handlers.cpp). Each is a type byte and a payload written to
// the request stream; blocks the original writes in one piece are written in one piece here too.
#include "player_client.h"

#include "item.h"
#include "party.h"
#include "unit.h"
#include "world.h"

#include "game_singletons.h"

#include <cstring>
#include <vector>

namespace {

constexpr std::uint8_t kWalk = 0x30, kAttack = 0x31, kCastAtPoint = 0x32, kCastOnUnit = 0x33,
                       kWeapon = 0x34, kMoveMode = 0x35, kFollow = 0x39, kGoTo = 0x3A, kFightAtWill = 0x3B,
                       kFlag10 = 0x10, kUseOrder = 0x36, kTakeExit = 0x03, kLeave04 = 0x04,
                       kJoined = 0x05, kGoToZone = 0x06, kResume = 0x09, kPartyUpdate = 0x50;
constexpr int kStateBetweenZones = 3, kStateJoining = 2;
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
static_assert(sizeof(OnUnitBlock) == 13 && sizeof(AtPointBlock) == 21 && sizeof(UnitByteBlock) == 5);

void WriteType(CMemoryStream& out, std::uint8_t type) { out.WriteValue(type); }

void WriteUnits(CMemoryStream& out, const std::vector<CUnit*>* units) {
    int count = std::ssize(*units);
    out.WriteValue(count);
    for (int i = 0; i < std::ssize(*units); ++i) {
        int id = (*units)[i] ? (*units)[i]->id : kNoObjectId;
        out.WriteValue(id);
    }
}

int SendMove(CMemoryStream& out, std::uint8_t type, const std::vector<CUnit*>* units, const Vector3& to, int run) {
    WriteType(out, type);
    WriteUnits(out, units);
    out.WriteValue(run);
    out.Write(&to, sizeof(Vector3));
    return 1;
}

// A potion used from a quick slot is counted off at once.
void SpendPotion(CUnit* user, int slot) {
    auto* item = user->GetQuickItem(static_cast<std::uint32_t>(slot));
    if (item && static_cast<int>(item->count) > 0 && item->GetCategory() == QuickItemCategory::Potion) --item->count;
}

} // namespace

int CPlayerClient::SendWalk(const std::vector<CUnit*>* units, const Vector3& to, int run) {
    return SendMove(out, kWalk, units, to, run);
}
int CPlayerClient::SendGoTo(const std::vector<CUnit*>* units, const Vector3& to, int run) {
    return SendMove(out, kGoTo, units, to, run);
}

int CPlayerClient::SendAttack(const std::vector<CUnit*>* units, CEngineObject* target, int how, int ctrl) {
    WriteType(out, kAttack);
    WriteUnits(out, units);
    out.WriteValue(ctrl);
    out.WriteValue(how);
    int targetId = target->id;
    out.WriteValue(targetId);
    return 1;
}

int CPlayerClient::SendCastOnUnit(int slot, CUnit* caster, CEngineObject* target, int ctrl) {
    OnUnitBlock block{static_cast<std::uint32_t>(slot), caster->id, target->id, static_cast<std::uint8_t>(ctrl)};
    WriteType(out, kCastOnUnit);
    out.WriteValue(block);
    return 1;
}

int CPlayerClient::SendCastAtPoint(int slot, CUnit* caster, const Vector3& point, int ctrl) {
    AtPointBlock block{static_cast<std::uint32_t>(slot), caster->id, point, static_cast<std::uint8_t>(ctrl)};
    WriteType(out, kCastAtPoint);
    out.WriteValue(block);
    return 1;
}

int CPlayerClient::SendItemOnUnit(int slot, CUnit* user, CEngineObject* target, int ctrl) {
    SpendPotion(user, slot);
    OnUnitBlock block{static_cast<std::uint32_t>(slot) | kItemSlot, user->id, target->id,
                      static_cast<std::uint8_t>(ctrl)};
    WriteType(out, kCastOnUnit);
    out.WriteValue(block);
    return 1;
}

int CPlayerClient::SendItemAtPoint(int slot, CUnit* user, const Vector3& point, int ctrl) {
    SpendPotion(user, slot);
    AtPointBlock block{static_cast<std::uint32_t>(slot) | kItemSlot, user->id, point,
                       static_cast<std::uint8_t>(ctrl)};
    WriteType(out, kCastAtPoint);
    out.WriteValue(block);
    return 1;
}

int CPlayerClient::SetWeapon(CUnit* unit, int slot) {
    UnitByteBlock block{unit->id, static_cast<std::uint8_t>(slot)};
    WriteType(out, kWeapon);
    out.WriteValue(block);
    unit->pendingWeapon = slot;
    return 1;
}

int CPlayerClient::SetMoveMode(CUnit* unit, int mode) {
    WriteType(out, kMoveMode);
    UnitByteBlock block{unit->id, static_cast<std::uint8_t>(mode)};
    out.WriteValue(block);
    unit->moveMode2 = mode;
    return 1;
}

int CPlayerClient::SetUnitFightAtWill(CUnit* unit, int value) {
    unit->SetFightAtWill(value);
    WriteType(out, kFightAtWill);
    int unitId = unit->id;
    out.WriteValue(unitId);
    std::uint8_t byte = static_cast<std::uint8_t>(value);
    out.WriteValue(byte);
    return 1;
}

int CPlayerClient::SendFollow(CUnit* unit, CEngineObject* target) {
    int ids[2] = {unit->id, target ? target->id : kNoObjectId};
    WriteType(out, kFollow);
    out.Write(ids, sizeof(ids));
    return 1;
}

int CPlayerClient::SendFlag10(std::uint8_t value) {
    WriteType(out, kFlag10);
    std::uint8_t set = value != 0 ? 1 : 0;
    out.WriteValue(set);
    return 1;
}

int CPlayerClient::SendUseOrder(CUnit* unit, CEngineObject* target, int kind, int value, int flag) {
    WriteType(out, kUseOrder);
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

int CPlayerClient::SendUseDead(const std::vector<CUnit*>* units, CEngineObject* target, int flag) {
    WriteType(out, kUseOrder);
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

int CPlayerClient::SendPartyUpdate() {
    CMemoryStream block;
    player->Party()->SaveParty(&block, 0);
    WriteType(out, kPartyUpdate);
    out.WriteSized(&block);
    return 1;
}

void CPlayerClient::Rejoin() {
    if (state != kStateBetweenZones) return;
    g_gameFlow.ReturnLocalParty();
    SendPartyUpdate();
    WriteType(out, kJoined);
    state = kStateJoining;
}

int CPlayerClient::TakeExit(int exit) {
    Rejoin();
    WriteType(out, kTakeExit);
    out.WriteValue(exit);
    return static_cast<int>(out.position);
}

int CPlayerClient::RequestZone(std::string_view zone, int entrance) {
    Rejoin();
    WriteType(out, kGoToZone);
    out.WriteString(zone);
    out.WriteValue(entrance);
    return static_cast<int>(out.position);
}

int CPlayerClient::SendLeave04(int value) {
    WriteType(out, kLeave04);
    out.WriteValue(value);
    state = kStateBetweenZones;
    return static_cast<int>(out.position);
}

int CPlayerClient::SendResume() {
    WriteType(out, kResume);
    return static_cast<int>(out.position);
}
