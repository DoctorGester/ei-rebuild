// Combat orders: the setters of the queued order (+0x1B4) that the AI and the player's commands
// use in a fight (turn, move, attack, cast, stop) and the end of a cast. See combat.h and
// docs/notes/combat.md.
#include "combat.h"
#include "combat_internal.h"

#include "item.h"
#include "maths.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "ei/x87.h"
#include "originals_units.h"

#include <bit>
#include <cstring>

// Keep the original's order of operations (see combat.cpp).
#pragma float_control(precise, on)

namespace {
constexpr float kZero = 0.0f;                               // the dword 0 FinishCast compares with
constexpr float kCastNoiseScale = 2.0f;                     // 0073EEF0
constexpr int kNoiseKind = 0x1A;
constexpr std::uint32_t kQuickItemSource = 0x80000000u;
constexpr std::uint32_t kNoSource = 0xFFFFFFFFu;
constexpr int kClassQuickItem = 0x13;
constexpr int kCategoryCharged = 5;     // wands and the like: pay from the charge
constexpr int kCategoryScroll = 8;      // scrolls: cast at once
constexpr int kWeaponBow = 5;
constexpr int kWeaponCrossbow = 6;
constexpr std::uint32_t kNoPointZ = 0xC4A42000u;    // -1313.0f: "no point"
constexpr std::uint32_t kFlagSpellBlocked = 0x10000000;
constexpr int kUnitSlotAimPoint = 53;   // CUnit slot 53: Vector3* (Vector3* out)
constexpr int kUnitSlotIsDead = 55;
constexpr float kReachHeight = 1.5f;    // 0073C51C

// Party messages (0066D6E0 codes).
constexpr int kMessageOutOfReach = 0x0C;
constexpr int kMessageCannotCast = 0x13;

bool LessOrUnordered(double a, double b) { return !(a >= b); }

// FLD/FSTP copy of a float.
void FpuCopy(void* dst, const void* src) {
    __asm {
        mov eax, src
        fld dword ptr [eax]
        mov eax, dst
        fstp dword ptr [eax]
    }
}

void* AIEventsOf(void* world) { return static_cast<char*>(world) + 0x2838; }

// The inlined reset of an order: kind 9, target released, flag18 / +0x1C / +0x20 cleared.
void ResetOrder(UnitAction* order) {
    order->kind = 9;
    if (order->target) order->target->Release();
    order->target = nullptr;
    order->flag18 = 0;
    order->unknown_1C = 0;
    order->unknown_20 = 0;
}

// 0052B030 (a counted reference's reset), inlined: release and clear.
void ReleaseTarget(UnitAction* order) {
    if (order->target) order->target->Release();
    order->target = nullptr;
}

void SetTarget(UnitAction* order, CEngineObject* target) {
    if (target == order->target) return;
    ReleaseTarget(order);
    if (target) ++target->refCount_;
    order->target = target;
}

void TellParty(CUnit* unit, int message) {
    if (unit->player) PlayerMessage(*reinterpret_cast<void**>(unit->player + 0x20), nullptr, unit, message);
}
} // namespace

// Not rewritten (forwarders).
// 00527EB0. UnitStats: the unit's skill value for spell school `school`, in ST0 (declared double).
// 00595020. CWorldServer: `unit` used the scroll `item` up.
__declspec(naked) void __fastcall WorldUseScroll(void*, void*, CUnit*, CItem*) EI_ORIGINAL(0x00595020)

__declspec(naked) bool CUnitServer::PlanOrder(UnitAction*, int) EI_ORIGINAL(0x0054E600)
__declspec(naked) void CUnitServer::CallFinishCast() EI_ORIGINAL(0x0054EE10)

void CUnitServer::FinishCast() {
    void* world = ManagerContext();
    std::uint32_t source = action.source;
    CItem* carrier = nullptr;
    CSpell* spell;
    if (source & kQuickItemSource) {
        auto* item = static_cast<CItem*>(unit->GetQuickItem(source & ~kQuickItemSource));
        if (!item || item->GetClassId() != kClassQuickItem) return;
        if (item->GetCategory() != kCategoryCharged && item->GetCategory() != kCategoryScroll) return;
        carrier = item;
        spell = static_cast<CSpell*>(item->spell_);
    } else {
        spell = static_cast<CSpell*>(unit->GetSpellObject(source));
    }
    if (!spell) return;
    Vector3 at;
    if (!action.target) {
        std::memcpy(&at, &action.point, sizeof(at));
    } else {
        Vector3 point;
        std::memcpy(&at, ei::VCall<const Vector3*>(action.target, kUnitSlotAimPoint, &point), sizeof(at));
    }
    if (carrier) {
        float cost = std::bit_cast<float>(spell->unknown_40);
        if (cost > carrier->charge_) {
            action.source = kNoSource;
            return;
        }
        CtlUseItemCharge(this, nullptr, carrier, cost);
    } else if (unit->player) {     // 005301A0
        float cost = std::bit_cast<float>(spell->unknown_40);
        char* server = reinterpret_cast<char*>(ServerObjectOf(unit));
        double needed = static_cast<double>(stats.Period20()) * cost;
        float maximum;
        std::memcpy(&maximum, server + 0x38, sizeof(maximum));
        float* mana = reinterpret_cast<float*>(server + 0x34);
        float left = static_cast<float>(-needed + *mana);
        *mana = left;
        // Branch-free clamps: min(left, maximum) (left when unordered), then max(0, that).
        float capped = LessOrUnordered(left, maximum) ? left : maximum;
        float result = LessOrUnordered(kZero, capped) ? capped : kZero;
        FpuCopy(mana, &result);
        (*reinterpret_cast<CUnit**>(server + 0x1C))->MarkChanged(0);
        unit->MarkChanged(0);
    }
    WorldCastSpell(world, nullptr, spell, unit, &unit->position.x, action.target, &at, 0);
    if (Field<int>(spell->GetPrototype(), 0x10) != 1) {
        float loudness = static_cast<float>(static_cast<double>(Combat().noise) * kCastNoiseScale);
        AIEventsNoise(AIEventsOf(world), nullptr, this, loudness, kNoiseKind, 0);
    }
    action.source = kNoSource;
}
EI_REPLACE_METHOD(0x0054EE10, CUnitServer::FinishCast);

void CUnitServer::OrderTurn(const float* direction, std::uint32_t time) {
    ResetOrder(&action2);
    action2.kind = 2;
    std::memcpy(&action2.point, direction, sizeof(action2.point));
    ReleaseTarget(&action2);
    action2.unknown_20 = 0;
    action2.unknown_1C = static_cast<int>(time);
}
EI_REPLACE_METHOD(0x005546B0, CUnitServer::OrderTurn);

void CUnitServer::OrderMove(const float* point, int run, int walk, std::uint32_t z) {
    const char* world = static_cast<const char*>(ManagerContext());
    ResetOrder(&action2);
    action2.kind = 1;
    std::memcpy(&action2.point, point, sizeof(action2.point));
    ReleaseTarget(&action2);
    action2.source = static_cast<std::uint8_t>(walk) != 0 ? 1 : 0;
    std::memcpy(&action2.point.z, &z, sizeof(z));
    action2.unknown_20 = static_cast<std::uint8_t>(run);
    action2.unknown_1C = 0;
    if (!unit->player) return;
    if (std::bit_cast<std::uint32_t>(point[2]) == kNoPointZ) return;
    if (world[0x6C] == 0) {
        action2.unknown_1C = 1;
        return;
    }
    ControllerPlanMoveOriginal(this, nullptr, point, walk, run, std::bit_cast<float>(z));
}
EI_REPLACE_METHOD(0x00554740, CUnitServer::OrderMove);

void CUnitServer::OrderAttack(CEngineObject* target, int location, int calledShot, int planArg) {
    if (!target || target == unit) return;
    if (target->GetClassId() == kClassUnit && location < 6) {
        const char* slots = reinterpret_cast<const char*>(static_cast<CUnit*>(target)->controller) + 0x3D0;
        if (*reinterpret_cast<const int*>(slots + location * 0xF4) != 3) location = 6;
    }
    ResetOrder(&action2);
    action2.kind = 3;
    SetTarget(&action2, target);
    action2.source = static_cast<std::uint32_t>(location);
    action2.unknown_20 = static_cast<std::uint8_t>(calledShot);
    // CanReachTarget (0054C800) inlined on the queued order.
    if (action2.kind == 3 || action2.kind == 6) {
        CEngineObject* other = action2.target;
        if (other && other->GetClassId() == kClassUnit && ei::VCall<int>(other, kUnitSlotIsDead) == 0 &&
            static_cast<CUnit*>(other)->sizeClass == 0) {
            bool ranged = false;
            if (action2.kind == 3) {
                auto* weapon = static_cast<CItemWeapon*>(unit->GetWeapon(unit->activeWeapon));
                ranged = weapon && (weapon->GetType() == kWeaponBow || weapon->GetType() == kWeaponCrossbow);
            }
            if (!ranged) {
                auto* victim = static_cast<CUnit*>(other);
                double height = static_cast<double>(Field<float>(victim->GetPrototype(), 0x24)) + victim->position.z;
                if (!LessOrUnordered(height, kReachHeight)) {
                    TellParty(unit, kMessageOutOfReach);
                    action2.kind = 9;
                    ReleaseTarget(&action2);
                    action2.flag18 = 0;
                    action2.unknown_1C = 0;
                    action2.unknown_20 = 0;
                    return;
                }
            }
        }
    }
    if (!PlanOrder(&action2, planArg)) ResetOrder(&action2);
}
EI_REPLACE_METHOD(0x00554B30, CUnitServer::OrderAttack);

namespace {
// The spell checks OrderCastAt and OrderCast share: the spell behind `source`; for a party unit's
// own spell (not an item) its skill requirement (+0x50) against the unit's skill (00527EB0).
// True when the order may be queued.
bool MayCast(CUnitServer* self, std::uint32_t source) {
    CSpell* spell;
    if (source & kQuickItemSource) {
        CItem* carrier = self->SpellCarrier(source);
        if (!carrier) return !(self->unit->flags_ & kFlagSpellBlocked);
        spell = static_cast<CSpell*>(carrier->spell_);
    } else {
        spell = static_cast<CSpell*>(self->unit->GetSpellObject(source));
    }
    if (spell && self->unit->player && !(source & kQuickItemSource)) {
        int needed = static_cast<int>(spell->unknown_50);
        float skill = static_cast<float>(
            UnitStatsSpellSchoolSkill(&self->stats, nullptr, Field<int>(spell->GetPrototype(), 0x14)));
        if (x87::fistp(skill) < needed) return false;
    }
    return !(self->unit->flags_ & kFlagSpellBlocked);
}
} // namespace

void CUnitServer::OrderCastAt(std::uint32_t source, const float* point, int run) {
    Vector3 at;
    std::memcpy(&at, point, sizeof(at));
    if (source & kQuickItemSource) {
        std::uint32_t slot = source & ~kQuickItemSource;
        auto* item = static_cast<CItem*>(unit->GetQuickItem(slot));
        if (item && item->GetClassId() == kClassQuickItem &&
            (item->GetCategory() == kCategoryCharged || item->GetCategory() == kCategoryScroll) &&
            item->GetCategory() == kCategoryScroll) {
            void* world = ManagerContext();
            auto* scroll = static_cast<CItem*>(unit->GetQuickItem(slot));
            if (!scroll || !scroll->unknown_48) return;
            CItem* carrier = SpellCarrier(source);
            if (!carrier || !carrier->spell_) return;
            WorldCastSpell(world, nullptr, carrier->spell_, unit, &unit->position.x, nullptr, point, 0);
            WorldUseScroll(world, nullptr, unit, scroll);
            return;
        }
    }
    if (!MayCast(this, source)) {
        TellParty(unit, kMessageCannotCast);
        return;
    }
    action2.kind = 9;
    ReleaseTarget(&action2);
    action2.flag18 = 0;
    action2.unknown_1C = 0;
    action2.unknown_20 = 0;
    action2.kind = 4;
    if (action2.target) ReleaseTarget(&action2);
    action2.source = source;
    std::memcpy(&action2.point, &at, sizeof(at));
    action2.unknown_20 = static_cast<std::uint8_t>(run);
    if (!PlanOrder(&action2, CanCast(&action2) ? 3 : -1)) ResetOrder(&action2);
}
EI_REPLACE_METHOD(0x00554D00, CUnitServer::OrderCastAt);

void CUnitServer::OrderCast(std::uint32_t source, CEngineObject* target, int run) {
    Vector3 aim;
    ei::VCall<const Vector3*>(target, kUnitSlotAimPoint, &aim);    // result unused (sic)
    if (source & kQuickItemSource) {
        CItem* carrier = SpellCarrier(source);
        if (carrier && carrier->GetCategory() == kCategoryScroll) {
            void* world = ManagerContext();
            auto* scroll = static_cast<CItem*>(unit->GetQuickItem(source & ~kQuickItemSource));
            if (!scroll || !scroll->unknown_48) return;
            CItem* spellCarrier = SpellCarrier(source);
            if (!spellCarrier || !spellCarrier->spell_) return;
            WorldCastSpell(world, nullptr, spellCarrier->spell_, unit, &unit->position.x, target,
                           &reinterpret_cast<CUnit*>(target)->position.x, 0);
            WorldUseScroll(world, nullptr, unit, scroll);
            return;
        }
    }
    if (!MayCast(this, source)) {
        TellParty(unit, kMessageCannotCast);
        return;
    }
    ResetOrder(&action2);
    action2.kind = 5;
    SetTarget(&action2, target);
    action2.source = source;
    std::memset(&action2.point, 0, sizeof(action2.point));
    action2.unknown_20 = static_cast<std::uint8_t>(run);
    if (!PlanOrder(&action2, CanCast(&action2) ? 3 : -1)) ResetOrder(&action2);
}
EI_REPLACE_METHOD(0x00554F50, CUnitServer::OrderCast);

void CUnitServer::StopQueued() {
    if (action.kind == 11) return;
    ResetOrder(&action2);
    action2.kind = 11;
    ReleaseTarget(&action2);
    action2.unknown_20 = 0;
}
EI_REPLACE_METHOD(0x00555200, CUnitServer::StopQueued);
