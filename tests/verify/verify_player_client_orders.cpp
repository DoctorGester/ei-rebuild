// Checks for the local view's orders (player_client_orders.cpp, A-H14). Each writer runs ours into
// the request stream; the bytes it appended are kept, the stream header is put back, the original
// runs, and both must have written the same bytes (skipped when ours had to grow the buffer). The
// unit fields the setters keep are written twice with the same value; a potion's count, which the
// item orders lower, is put back before the original runs.
// Scenarios: ui_battle_commands (walk, attack, spells and quick items on units and points through
// the battle screen's clicks), ui_hud (weapon slot, movement mode, the +0x438 flag); every
// battle-screen ground click sends 0x30.
#include "verify.h"

#include "../../src/game/item.h"
#include "../../src/game/player_client.h"
#include "../../src/game/unit.h"

#include <cstring>
#include <vector>

namespace {

// A copy of the stream header (never a BitStream by value: its vtable would not be copied).
struct Header {
    alignas(4) unsigned char bytes[sizeof(BitStream)];
    explicit Header(const BitStream& s) { std::memcpy(bytes, &s, sizeof(bytes)); }
    const BitStream& Get() const { return *reinterpret_cast<const BitStream*>(bytes); }
    void RestoreTo(BitStream& s) const { std::memcpy(static_cast<void*>(&s), bytes, sizeof(bytes)); }
};

template <typename Ours, typename Original>
int CompareSend(const char* name, CPlayerClient* self, Ours ours, Original original) {
    Header before(self->out_);
    int actual = ours();
    Header after(self->out_);
    if (after.Get().data_ != before.Get().data_ || after.Get().capacity_ != before.Get().capacity_) return actual;
    std::vector<std::uint8_t> written(after.Get().data_, after.Get().data_ + after.Get().size_);
    before.RestoreTo(self->out_);
    int expected = original();
    bool same = std::memcmp(&self->out_, after.bytes, sizeof(BitStream)) == 0 &&
                (written.empty() || std::memcmp(self->out_.data_, written.data(), written.size()) == 0) &&
                expected == actual;
    ei::verify::Check(name, true, same, "client %p: size %u vs %u", self, self->out_.size_, after.Get().size_);
    return expected;
}

// The quick item's count, saved and put back around ours (SpendPotion lowers it).
struct PotionCount {
    CItem* item;
    std::uint32_t count;
    PotionCount(CUnit* user, int slot)
        : item(static_cast<CItem*>(user->GetQuickItem(static_cast<std::uint32_t>(slot)))), count(item ? item->unknown_48 : 0) {}
    void Restore() const {
        if (item) item->unknown_48 = count;
    }
};

using Units = mfc::CArray<CUnit*>;

int(__fastcall* s_walk)(CPlayerClient*, void*, const Units*, const Vector3*, int);
int(__fastcall* s_goTo)(CPlayerClient*, void*, const Units*, const Vector3*, int);
int(__fastcall* s_attack)(CPlayerClient*, void*, const Units*, CEngineObject*, int, int);
int(__fastcall* s_castOnUnit)(CPlayerClient*, void*, int, CUnit*, CEngineObject*, int);
int(__fastcall* s_castAtPoint)(CPlayerClient*, void*, int, CUnit*, const Vector3*, int);
int(__fastcall* s_itemOnUnit)(CPlayerClient*, void*, int, CUnit*, CEngineObject*, int);
int(__fastcall* s_itemAtPoint)(CPlayerClient*, void*, int, CUnit*, const Vector3*, int);
int(__fastcall* s_weapon)(CPlayerClient*, void*, CUnit*, int);
int(__fastcall* s_moveMode)(CPlayerClient*, void*, CUnit*, int);
int(__fastcall* s_flag438)(CPlayerClient*, void*, CUnit*, int);
int(__fastcall* s_follow)(CPlayerClient*, void*, CUnit*, CEngineObject*);
int(__fastcall* s_flag10)(CPlayerClient*, void*, std::uint8_t);

int __fastcall WalkVerify(CPlayerClient* self, void*, const Units* units, const Vector3* to, int run) {
    return CompareSend("CPlayerClient::SendWalk", self, [&] { return self->SendWalk(units, to, run); },
                       [&] { return s_walk(self, nullptr, units, to, run); });
}
int __fastcall GoToVerify(CPlayerClient* self, void*, const Units* units, const Vector3* to, int run) {
    return CompareSend("CPlayerClient::SendGoTo", self, [&] { return self->SendGoTo(units, to, run); },
                       [&] { return s_goTo(self, nullptr, units, to, run); });
}
int __fastcall AttackVerify(CPlayerClient* self, void*, const Units* units, CEngineObject* target, int how, int ctrl) {
    return CompareSend("CPlayerClient::SendAttack", self, [&] { return self->SendAttack(units, target, how, ctrl); },
                       [&] { return s_attack(self, nullptr, units, target, how, ctrl); });
}
int __fastcall CastOnUnitVerify(CPlayerClient* self, void*, int slot, CUnit* caster, CEngineObject* target, int ctrl) {
    return CompareSend("CPlayerClient::SendCastOnUnit", self,
                       [&] { return self->SendCastOnUnit(slot, caster, target, ctrl); },
                       [&] { return s_castOnUnit(self, nullptr, slot, caster, target, ctrl); });
}
int __fastcall CastAtPointVerify(CPlayerClient* self, void*, int slot, CUnit* caster, const Vector3* point, int ctrl) {
    return CompareSend("CPlayerClient::SendCastAtPoint", self,
                       [&] { return self->SendCastAtPoint(slot, caster, point, ctrl); },
                       [&] { return s_castAtPoint(self, nullptr, slot, caster, point, ctrl); });
}
int __fastcall ItemOnUnitVerify(CPlayerClient* self, void*, int slot, CUnit* user, CEngineObject* target, int ctrl) {
    PotionCount potion(user, slot);
    return CompareSend("CPlayerClient::SendItemOnUnit", self,
                       [&] { int r = self->SendItemOnUnit(slot, user, target, ctrl); potion.Restore(); return r; },
                       [&] { return s_itemOnUnit(self, nullptr, slot, user, target, ctrl); });
}
int __fastcall ItemAtPointVerify(CPlayerClient* self, void*, int slot, CUnit* user, const Vector3* point, int ctrl) {
    PotionCount potion(user, slot);
    return CompareSend("CPlayerClient::SendItemAtPoint", self,
                       [&] { int r = self->SendItemAtPoint(slot, user, point, ctrl); potion.Restore(); return r; },
                       [&] { return s_itemAtPoint(self, nullptr, slot, user, point, ctrl); });
}
int __fastcall WeaponVerify(CPlayerClient* self, void*, CUnit* unit, int slot) {
    return CompareSend("CPlayerClient::SetWeapon", self, [&] { return self->SetWeapon(unit, slot); },
                       [&] { return s_weapon(self, nullptr, unit, slot); });
}
int __fastcall MoveModeVerify(CPlayerClient* self, void*, CUnit* unit, int mode) {
    return CompareSend("CPlayerClient::SetMoveMode", self, [&] { return self->SetMoveMode(unit, mode); },
                       [&] { return s_moveMode(self, nullptr, unit, mode); });
}
int __fastcall Flag438Verify(CPlayerClient* self, void*, CUnit* unit, int value) {
    return CompareSend("CPlayerClient::SetUnitFlag438", self, [&] { return self->SetUnitFlag438(unit, value); },
                       [&] { return s_flag438(self, nullptr, unit, value); });
}
int __fastcall FollowVerify(CPlayerClient* self, void*, CUnit* unit, CEngineObject* target) {
    return CompareSend("CPlayerClient::SendFollow", self, [&] { return self->SendFollow(unit, target); },
                       [&] { return s_follow(self, nullptr, unit, target); });
}
int __fastcall Flag10Verify(CPlayerClient* self, void*, std::uint8_t value) {
    return CompareSend("CPlayerClient::SendFlag10", self, [&] { return self->SendFlag10(value); },
                       [&] { return s_flag10(self, nullptr, value); });
}

} // namespace

EI_VERIFY(0x0066A7D0, WalkVerify, 5, s_walk);
EI_VERIFY(0x0066A720, GoToVerify, 5, s_goTo);
EI_VERIFY(0x0066A880, AttackVerify, 5, s_attack);
EI_VERIFY(0x0066AC70, CastOnUnitVerify, 7, s_castOnUnit);
EI_VERIFY(0x0066ACD0, CastAtPointVerify, 7, s_castAtPoint);
EI_VERIFY(0x0066AD40, ItemOnUnitVerify, 5, s_itemOnUnit);
EI_VERIFY(0x0066ADD0, ItemAtPointVerify, 5, s_itemAtPoint);
EI_VERIFY(0x0066AE70, WeaponVerify, 5, s_weapon);
EI_VERIFY(0x0066AED0, MoveModeVerify, 6, s_moveMode);
EI_VERIFY(0x0066AF30, Flag438Verify, 5, s_flag438);
EI_VERIFY(0x0066AFE0, FollowVerify, 7, s_follow);
EI_VERIFY(0x0066B040, Flag10Verify, 5, s_flag10);
