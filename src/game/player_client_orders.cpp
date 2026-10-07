// The local view's orders (CPlayerClient, 0066A720-0066B07B; A-H14): the HUD and the battle
// screen queue them as messages for the server (CPlayerServer's handlers, player_server_handlers.cpp).
// Each is a type byte and a payload written to the request stream; blocks the original writes in one
// piece are written in one piece here too.
#include "player_client.h"

#include "item.h"
#include "unit.h"

#include "ei/hook.h"

namespace {

constexpr std::uint8_t kWalk = 0x30, kAttack = 0x31, kCastAtPoint = 0x32, kCastOnUnit = 0x33,
                       kWeapon = 0x34, kMoveMode = 0x35, kFollow = 0x39, kGoTo = 0x3A, kFlag438 = 0x3B,
                       kFlag10 = 0x10;
constexpr std::uint32_t kItemSlot = 0x80000000u;   // a quick item, not a spell
constexpr int kPotionCategory = 8;
constexpr int kNoId = -1;

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

void WriteType(BitStream& out, std::uint8_t type) { out.Write(&type, 1); }

void WriteUnits(BitStream& out, const mfc::CArray<CUnit*>* units) {
    int count = units->size;
    out.Write(&count, 4);
    for (int i = 0; i < units->size; ++i) {
        int id = units->data[i] ? units->data[i]->id_ : kNoId;
        out.Write(&id, 4);
    }
}

int SendMove(BitStream& out, std::uint8_t type, const mfc::CArray<CUnit*>* units, const Vector3* to, int run) {
    WriteType(out, type);
    WriteUnits(out, units);
    out.Write(&run, 4);
    out.Write(to, sizeof(Vector3));
    return 1;
}

// A potion used from a quick slot is counted off at once.
void SpendPotion(CUnit* user, int slot) {
    auto* item = static_cast<CItem*>(user->GetQuickItem(static_cast<std::uint32_t>(slot)));
    if (item && static_cast<int>(item->unknown_48) > 0 && item->GetCategory() == kPotionCategory) --item->unknown_48;
}

} // namespace

int CPlayerClient::SendWalk(const mfc::CArray<CUnit*>* units, const Vector3* to, int run) {
    return SendMove(out_, kWalk, units, to, run);
}
int CPlayerClient::SendGoTo(const mfc::CArray<CUnit*>* units, const Vector3* to, int run) {
    return SendMove(out_, kGoTo, units, to, run);
}

int CPlayerClient::SendAttack(const mfc::CArray<CUnit*>* units, CEngineObject* target, int how, int ctrl) {
    WriteType(out_, kAttack);
    WriteUnits(out_, units);
    out_.Write(&ctrl, 4);
    out_.Write(&how, 4);
    int id = target->id_;
    out_.Write(&id, 4);
    return 1;
}

int CPlayerClient::SendCastOnUnit(int slot, CUnit* caster, CEngineObject* target, int ctrl) {
    OnUnitBlock block{static_cast<std::uint32_t>(slot), caster->id_, target->id_, static_cast<std::uint8_t>(ctrl)};
    WriteType(out_, kCastOnUnit);
    out_.Write(&block, sizeof(block));
    return 1;
}

int CPlayerClient::SendCastAtPoint(int slot, CUnit* caster, const Vector3* point, int ctrl) {
    AtPointBlock block{static_cast<std::uint32_t>(slot), caster->id_, *point, static_cast<std::uint8_t>(ctrl)};
    WriteType(out_, kCastAtPoint);
    out_.Write(&block, sizeof(block));
    return 1;
}

int CPlayerClient::SendItemOnUnit(int slot, CUnit* user, CEngineObject* target, int ctrl) {
    SpendPotion(user, slot);
    OnUnitBlock block{static_cast<std::uint32_t>(slot) | kItemSlot, user->id_, target->id_,
                      static_cast<std::uint8_t>(ctrl)};
    WriteType(out_, kCastOnUnit);
    out_.Write(&block, sizeof(block));
    return 1;
}

int CPlayerClient::SendItemAtPoint(int slot, CUnit* user, const Vector3* point, int ctrl) {
    SpendPotion(user, slot);
    AtPointBlock block{static_cast<std::uint32_t>(slot) | kItemSlot, user->id_, *point,
                       static_cast<std::uint8_t>(ctrl)};
    WriteType(out_, kCastAtPoint);
    out_.Write(&block, sizeof(block));
    return 1;
}

int CPlayerClient::SetWeapon(CUnit* unit, int slot) {
    UnitByteBlock block{unit->id_, static_cast<std::uint8_t>(slot)};
    WriteType(out_, kWeapon);
    out_.Write(&block, sizeof(block));
    unit->unknown_270 = slot;
    return 1;
}

int CPlayerClient::SetMoveMode(CUnit* unit, int mode) {
    WriteType(out_, kMoveMode);
    UnitByteBlock block{unit->id_, static_cast<std::uint8_t>(mode)};
    out_.Write(&block, sizeof(block));
    unit->moveMode2 = mode;
    return 1;
}

int CPlayerClient::SetUnitFlag438(CUnit* unit, int value) {
    unit->SetUnknown438(value);
    WriteType(out_, kFlag438);
    int id = unit->id_;
    out_.Write(&id, 4);
    std::uint8_t byte = static_cast<std::uint8_t>(value);
    out_.Write(&byte, 1);
    return 1;
}

int CPlayerClient::SendFollow(CUnit* unit, CEngineObject* target) {
    int ids[2] = {unit->id_, target ? target->id_ : kNoId};
    WriteType(out_, kFollow);
    out_.Write(ids, sizeof(ids));
    return 1;
}

int CPlayerClient::SendFlag10(std::uint8_t value) {
    WriteType(out_, kFlag10);
    std::uint8_t set = value != 0 ? 1 : 0;
    out_.Write(&set, 1);
    return 1;
}

EI_REPLACE_METHOD(0x0066A7D0, CPlayerClient::SendWalk);
EI_REPLACE_METHOD(0x0066A720, CPlayerClient::SendGoTo);
EI_REPLACE_METHOD(0x0066A880, CPlayerClient::SendAttack);
EI_REPLACE_METHOD(0x0066AC70, CPlayerClient::SendCastOnUnit);
EI_REPLACE_METHOD(0x0066ACD0, CPlayerClient::SendCastAtPoint);
EI_REPLACE_METHOD(0x0066AD40, CPlayerClient::SendItemOnUnit);
EI_REPLACE_METHOD(0x0066ADD0, CPlayerClient::SendItemAtPoint);
EI_REPLACE_METHOD(0x0066AE70, CPlayerClient::SetWeapon);
EI_REPLACE_METHOD(0x0066AED0, CPlayerClient::SetMoveMode);
EI_REPLACE_METHOD(0x0066AF30, CPlayerClient::SetUnitFlag438);
EI_REPLACE_METHOD(0x0066AFE0, CPlayerClient::SendFollow);
EI_REPLACE_METHOD(0x0066B040, CPlayerClient::SendFlag10);
