// Checks for combat_actions.cpp (the end of an order, missiles, item spells, healing, death).
//
// HitDirection is pure and runs both versions. Everything else kills, logs, spawns missiles,
// casts spells or releases references, so only ours runs (stolenBytes 0) and the wrapper checks
// what the function must have left behind. The rewrites call each other through the game's
// addresses, so these wrappers also see the calls between them.
#include "verify.h"

#include "../../src/game/combat.h"
#include "../../src/game/item.h"
#include "../../src/game/object_diplomacy.h"

#include "ei/vcall.h"

#include <bit>
#include <cstring>

namespace {
int UnitId(const CUnitServer* self) { return self->unit ? self->unit->id_ : -1; }

std::uint32_t Bits(float f) { return std::bit_cast<std::uint32_t>(f); }

// charge - cost, rounded to float (the original's FLD / FSUB / FSTP).
std::uint32_t ChargeAfter(float charge, float cost) {
    volatile float result = charge - cost;
    return Bits(result);
}

// What one item spell trigger must do to the item's charge: the charge after it, as bits.
struct ChargeExpectation {
    CItem* item = nullptr;
    std::uint32_t expected = 0;
};

// The charge `item` must have after a trigger that fires when `fires` and the spell can pay.
ChargeExpectation ExpectCharge(CItem* item, bool fires) {
    ChargeExpectation e;
    e.item = item;
    if (!item) return e;
    e.expected = Bits(item->charge_);
    auto* spell = static_cast<const CSpell*>(item->spell_);
    if (!fires || !spell || !(spell->targetFlags_ & 0x10000)) return e;
    float cost = std::bit_cast<float>(spell->manaCost_);
    if (cost > item->charge_) return e;
    e.expected = ChargeAfter(item->charge_, cost);
    return e;
}

bool ChargeAsExpected(const ChargeExpectation& e) { return !e.item || Bits(e.item->charge_) == e.expected; }

int DeadFlag(const CUnit* unit) { return *reinterpret_cast<const int*>(reinterpret_cast<const char*>(unit) + 0x274); }

CEngineObject* ServerObject(const CUnit* unit) {
    return *reinterpret_cast<CEngineObject* const*>(reinterpret_cast<const char*>(unit) + 0x170);
}

struct UnitListNode {
    UnitListNode* next;
    UnitListNode* prev;
    CUnit* unit;
};

bool ListContains(const void* list, const CUnit* unit) {
    for (auto* node = *reinterpret_cast<UnitListNode* const*>(static_cast<const char*>(list) + 4); node;
         node = node->next)
        if (node->unit == unit) return true;
    return false;
}
} // namespace

// 005518F0: PUSH EBP; MOV EBP,ESP; SUB ESP,0x20 (6 bytes). Pure.
static std::uint32_t(__fastcall* s_hitDirection)(CUnitServer*, void*, CUnit*, int);
static std::uint32_t __fastcall HitDirectionVerify(CUnitServer* self, void*, CUnit* attacker, int hitKind) {
    std::uint32_t expected = s_hitDirection(self, nullptr, attacker, hitKind);
    ei::verify::Check("CUnitServer::HitDirection", expected, self->HitDirection(attacker, hitKind),
                      "unit %d attacker %d kind %d", UnitId(self), attacker ? attacker->id_ : -1, hitKind);
    return expected;
}
EI_VERIFY(0x005518F0, HitDirectionVerify, 6, s_hitDirection);

// 00549EC0. Ours only. Orders of kinds 4, 5, 6 and 10 end cleared (kind 9) with no queued order
// of kind 9 left (Idle turns it into 0); an attack keeps its location or turns it into 6; other
// kinds are left alone.
static void* s_unusedFinishAction;
static void __fastcall FinishActionVerify(CUnitServer* self, void*, int location, char unused) {
    int kind = self->action.kind;
    std::uint32_t source = self->action.source;
    CEngineObject* target = self->action.target;
    self->FinishAction(location, unused);
    bool ok;
    switch (kind) {
    case 3:
        ok = self->action.kind != 3 || self->action.target != target || self->action.source == source ||
             self->action.source == 6;
        break;
    case 4:
    case 5:
    case 6:
    case 10:
        ok = self->action.kind == 9 && self->action2.kind != 9;
        break;
    default:
        ok = self->action.kind == kind && self->action.source == source;
        break;
    }
    ei::verify::Check("CUnitServer::FinishAction", true, ok, "unit %d kind %d -> %d, source %u -> %u, queued %d",
                      UnitId(self), kind, self->action.kind, source, self->action.source, self->action2.kind);
}
EI_VERIFY(0x00549EC0, FinishActionVerify, 0, s_unusedFinishAction);

// 0054F450. Ours only (spawns a missile or hits the target). The weapon's spell is paid for from
// its charge exactly when it can be cast.
static void* s_unusedDeliverAttack;
static void __fastcall DeliverAttackVerify(CUnitServer* self, void*, CEngineObject* target, DamageInfo* hit) {
    auto* weapon = static_cast<CItem*>(self->unit->GetWeapon(self->unit->activeWeapon));
    ChargeExpectation charge = ExpectCharge(weapon, true);
    std::uint32_t before = weapon ? Bits(weapon->charge_) : 0;
    // With a bow the function returns early when the missile cannot be created.
    int type = weapon ? static_cast<CItemWeapon*>(weapon)->GetType() : -1;
    bool ranged = type == 5 || type == 6;
    self->DeliverAttack(target, hit);
    bool ok = ChargeAsExpected(charge) || (ranged && Bits(weapon->charge_) == before);
    ei::verify::Check("CUnitServer::DeliverAttack", true, ok,
                      "unit %d target %d: weapon charge %08X -> %08X, expected %08X", UnitId(self),
                      target ? target->id_ : -1, before, weapon ? Bits(weapon->charge_) : 0, charge.expected);
}
EI_VERIFY(0x0054F450, DeliverAttackVerify, 0, s_unusedDeliverAttack);

// 0054F850. Ours only (casts). Same charge check.
static void* s_unusedWeaponSpell;
static void __fastcall WeaponSpellVerify(CUnitServer* self, void*, CEngineObject* target) {
    auto* weapon = static_cast<CItem*>(self->unit->GetWeapon(self->unit->activeWeapon));
    ChargeExpectation charge = ExpectCharge(weapon, true);
    std::uint32_t before = weapon ? Bits(weapon->charge_) : 0;
    self->TriggerWeaponSpell(target);
    ei::verify::Check("CUnitServer::TriggerWeaponSpell", true, ChargeAsExpected(charge),
                      "unit %d: weapon charge %08X -> %08X, expected %08X", UnitId(self), before,
                      weapon ? Bits(weapon->charge_) : 0, charge.expected);
}
EI_VERIFY(0x0054F850, WeaponSpellVerify, 0, s_unusedWeaponSpell);

// 0054F8E0. Ours only (casts). Each armour piece pays for its spell exactly when it fires.
static void* s_unusedItemSpells;
static void __fastcall ItemSpellsVerify(CUnitServer* self, void*, char onHit) {
    ChargeExpectation charges[7];
    for (int slot = 0; slot < 7; ++slot) {
        auto* item = static_cast<CItem*>(self->unit->armor[slot]);
        bool fires = onHit != 0;
        if (item && item->spell_ &&
            (static_cast<const CSpell*>(item->spell_)->unknown_18 & 0xFFFF0000u) == 0x180000u)
            fires = onHit == 0;
        charges[slot] = ExpectCharge(item, fires);
    }
    self->TriggerItemSpells(onHit);
    int bad = -1;
    for (int slot = 0; slot < 7; ++slot)
        if (!ChargeAsExpected(charges[slot])) bad = slot;
    ei::verify::Check("CUnitServer::TriggerItemSpells", -1, bad, "unit %d onHit %d: charge of slot %d wrong",
                      UnitId(self), onHit, bad);
}
EI_VERIFY(0x0054F8E0, ItemSpellsVerify, 0, s_unusedItemSpells);

// 00552C60. Ours only (notifies the world and the party). The charge drops by the cost.
static void* s_unusedUseCharge;
static void __fastcall UseChargeVerify(CUnitServer* self, void*, CItem* item, float cost) {
    std::uint32_t expected = item ? ChargeAfter(item->charge_, cost) : 0;
    self->UseItemCharge(item, cost);
    std::uint32_t actual = item ? Bits(item->charge_) : 0;
    ei::verify::Check("CUnitServer::UseItemCharge", expected, actual, "unit %d cost %g", UnitId(self), cost);
}
EI_VERIFY(0x00552C60, UseChargeVerify, 0, s_unusedUseCharge);

// 005543B0 (slot 21). Ours only (logs). Returns 1 and a heal does not lower the hit points.
static void* s_unusedHeal;
static BOOL __fastcall HealVerify(CUnitServer* self, void*, float amount) {
    const char* server = reinterpret_cast<const char*>(ServerObject(self->unit));
    float before;
    std::memcpy(&before, server + 0x28, sizeof(before));
    BOOL result = self->Heal(amount);
    float after;
    std::memcpy(&after, server + 0x28, sizeof(after));
    bool ok = result == 1 && (amount < 0 || !(after < before));
    ei::verify::Check("CUnitServer::Heal", true, ok, "unit %d amount %g: result %d, hit points %g -> %g",
                      UnitId(self), amount, result, before, after);
    return result;
}
EI_VERIFY(0x005543B0, HealVerify, 0, s_unusedHeal);

// 00553AC0. Ours only (kills, logs, notifies the AI). A dead unit ends with its dead flag set; a
// living one has handled its hostility claims (flags_ bit 0x80000 clear) and stored the
// direction of the hit unless it keeps the old one; other unit types only get flags_ bit 1.
static void* s_unusedOnDamaged;
static void __fastcall OnDamagedVerify(CUnitServer* self, void*, CUnit* attacker, int hitKind) {
    CUnit* unit = self->unit;
    bool creature = unit->type_ >= 0x32 && unit->type_ <= 0x36;
    bool dead = creature && ei::VCall<int>(ServerObject(unit), 17) != 0;
    bool keep = self->keepHitDirection != 0;
    self->OnDamaged(attacker, hitKind);
    bool ok;
    if (!creature) ok = (unit->flags_ & 1) != 0;
    else if (dead) ok = DeadFlag(unit) == 1;
    else ok = !(unit->flags_ & 0x80000) && (keep || self->hitDirection == self->HitDirection(attacker, hitKind));
    ei::verify::Check("CUnitServer::OnDamaged", true, ok,
                      "unit %d attacker %d kind %d: dead %d (flag %d), flags %08X, direction %08X", UnitId(self),
                      attacker ? attacker->id_ : -1, hitKind, dead ? 1 : 0, DeadFlag(unit), unit->flags_,
                      self->hitDirection);
}
EI_VERIFY(0x00553AC0, OnDamagedVerify, 0, s_unusedOnDamaged);

// 005538D0. Ours only (changes lists and diplomacy, logs). Afterwards every allied witness that
// watches the victim also watches the murderer.
static void* s_unusedWitness;
static void __fastcall WitnessVerify(CUnitServer* self, void*, void* witnesses, CUnit* victim, CUnit* murderer) {
    self->WitnessMurder(witnesses, victim, murderer);
    auto* diplomacy = reinterpret_cast<const CDiplomacy*>(static_cast<char*>(self->unit->ManagerContext()) + 0x12C64);
    std::uint32_t bit = 1u << (static_cast<std::uint32_t>(self->unit->side) & 31);
    int bad = -1;
    for (auto* node = *reinterpret_cast<UnitListNode* const*>(static_cast<const char*>(witnesses) + 4); node;
         node = node->next) {
        CUnit* other = node->unit;
        if (!(diplomacy->allied_[other->side] & bit)) continue;
        auto* controller = reinterpret_cast<CUnitServer*>(other->controller);
        const void* watched = &controller->brain.visible;
        if (ListContains(watched, victim) && !ListContains(watched, murderer)) bad = other->id_;
    }
    ei::verify::Check("CUnitServer::WitnessMurder", -1, bad, "unit %d victim %d murderer %d: witness %d",
                      UnitId(self), victim ? victim->id_ : -1, murderer ? murderer->id_ : -1, bad);
}
EI_VERIFY(0x005538D0, WitnessVerify, 0, s_unusedWitness);

// 00552E00. Ours only (claims hostility). With flags_ bit 0x80000 set, it ends cleared and the
// unit's claims include its hostile mask; without it nothing changes.
static void* s_unusedPropagate;
static void __fastcall PropagateVerify(CUnitServer* self) {
    std::uint32_t flags = self->unit->flags_;
    std::uint32_t claims = self->claimedHostile;
    std::uint32_t hostile = self->unit->relationsA;
    self->PropagateHostility();
    bool ok;
    if (flags & 0x80000) ok = !(self->unit->flags_ & 0x80000) && (self->claimedHostile & hostile) == hostile;
    else ok = self->claimedHostile == claims && self->unit->flags_ == flags;
    ei::verify::Check("CUnitServer::PropagateHostility", true, ok, "unit %d flags %08X -> %08X claims %08X -> %08X",
                      UnitId(self), flags, self->unit->flags_, claims, self->claimedHostile);
}
EI_VERIFY(0x00552E00, PropagateVerify, 0, s_unusedPropagate);

// 0054F000. Ours only (logs, steals, talks). Science and stealing leave the order alone (the talk
// branch, kind 6 with source 0x18A95, starts a dialog that may change it, so it is not checked).
static void* s_unusedUseSkill;
static void __fastcall UseSkillVerify(CUnitServer* self, void*, CEngineObject* target) {
    int kind = self->action.kind;
    std::uint32_t source = self->action.source;
    bool talk = kind == 6 && source == 0x18A95;
    self->UseSkill(target);
    bool ok = talk || (self->action.kind == kind && self->action.source == source);
    ei::verify::Check("CUnitServer::UseSkill", true, ok, "unit %d: order %d/%u -> %d/%u", UnitId(self), kind, source,
                      self->action.kind, self->action.source);
}
EI_VERIFY(0x0054F000, UseSkillVerify, 0, s_unusedUseSkill);

// 00555180. Ours only (releases the queued target). Without a current order, or with a queued
// order of kind 7 or 8, nothing changes; else the queued order is kind 0 without a target.
static void* s_unusedIdle;
static void __fastcall IdleVerify(CUnitServer* self) {
    UnitAction before;
    std::memcpy(&before, &self->action2, sizeof(before));
    bool untouched = self->action.kind == 0 || before.kind == 7 || before.kind == 8;
    self->Idle();
    bool ok = untouched ? std::memcmp(&before, &self->action2, sizeof(before)) == 0
                        : self->action2.kind == 0 && !self->action2.target && !self->action2.flag18 &&
                              !self->action2.param && !self->action2.modifier;
    ei::verify::Check("CUnitServer::Idle", true, ok, "unit %d: order %d, queued %d -> %d", UnitId(self),
                      self->action.kind, before.kind, self->action2.kind);
}
EI_VERIFY(0x00555180, IdleVerify, 0, s_unusedIdle);

// 00546720. Ours only (re-registers the unit, releases its effect object). The dead flag ends as
// asked; a dying unit has no effect object left.
static void* s_unusedSetDead;
static void __fastcall SetDeadVerify(CUnitServer* self, void*, int dead) {
    bool changes = DeadFlag(self->unit) != dead;
    self->SetDead(dead);
    bool ok = DeadFlag(self->unit) == dead && (!changes || !dead || !self->effectObject);
    ei::verify::Check("CUnitServer::SetDead", true, ok, "unit %d dead %d: flag %d, effect object %p", UnitId(self),
                      dead, DeadFlag(self->unit), self->effectObject);
}
EI_VERIFY(0x00546720, SetDeadVerify, 0, s_unusedSetDead);
