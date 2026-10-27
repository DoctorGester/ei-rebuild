// CWorldServer: casting spells into the world (CastSpell) and the missile that carries one (CreateMissile). See
// world.h and docs/notes/world.md.
#include "object_diplomacy.h"
#include "world.h"
#include "combat.h"

#include "core_maths.h"
#include "map_object.h"
#include "player_messages.h"
#include "script.h"
#include "spell.h"
#include "unit.h"
#include "unit_ai.h"

#include "lib/crt.h"

#include "ei/hook.h"

#include <bit>
#include <cstring>
#include "game_data.h"
#include "lib/nan_audit.h"
#include "lib/mfc_slots.h"

namespace {


constexpr int kKindSinglePlayer = 1;
constexpr std::uint32_t kSpellAreaFlag = 0x20000000;     // spell +0x3C: also hits the target itself
constexpr std::uint32_t kSpellNoTargetFlag = 0x20000;
constexpr std::uint32_t kFlagFought = 0x80000;           // flags_ of both sides of a hostile spell
constexpr std::uint32_t kFarAway = 0x7E967699;           // 1e38f

using UnitList = mfc::CTypedPtrList<CUnit>;
// SpellHasLineOfSight (combat.h): whether the spell may hit a target. The missile: constructor (returns this),
// CSpellEffect::Init, Update. AIBrain::SetSeen: the target's AI brain is attacked by `caster`. CDiplomacy: `a`
// attacked `b` (strength).

// The spell's type (the prototype index in the packed id). Its +0x34 is the radius (float bits) and +0x38
// the number of targets.
int SpellType(const CSpell* spell) { return static_cast<int>(spell->prototypeId_ >> 16); }

// Whether the spell type turns the target against the caster (a switch over 0x13, 0x19, 0x1E, 0x20, 0x22,
// 0x26).
bool IsHostileType(int type) {
    switch (type) {
    case 0x13: case 0x19: case 0x1E: case 0x20: case 0x22: case 0x26: return true;
    default: return false;
    }
}

void RemoveNode(UnitList& list, UnitList::Node* node) {
    if (node == list.head) list.head = node->next;
    else node->prev->next = node->next;
    if (node == list.tail) list.tail = node->prev;
    else node->next->prev = node->prev;
    node->next = list.free;
    list.free = node;
    if (--list.count == 0) list.RemoveAll();
}

void Unlink(UnitList& list, UnitList::Node* node) {
    if (node == list.head) list.head = node->next;
    else node->prev->next = node->next;
    if (node == list.tail) list.tail = node->prev;
    else node->next->prev = node->prev;
}

} // namespace

void CWorldServer::CreateMissile(CSpell* spell, CUnit* caster, Vector3 from, CMapObject* target,
                                 const Vector3* to, int extra) {
    const Vector3* aim = target ? &target->localPosition_ : to;
    Vector3 end;
    std::memcpy(&end, aim, sizeof(end));
    if (spell->targetFlags_ & kSpellAreaFlag) target = nullptr;
    end.z = static_cast<float>(RawSurfaceHeightAt(end.x, end.y) + 0.3f /*0073C54C*/);
    if (caster) std::memcpy(&from, &caster->localPosition_, sizeof(from));

    auto* missile = new CSpellEffect();
    missile->refs_ = 0;   // the count and flags are cleared before the null test
    missile->flags6_ = 0;
    missile->flags7_ = 0;
    missile->Init(this, spell, caster, &from, target, &end, static_cast<std::uint8_t>(extra));
    if (missile->Update()) {
        auto& list = projectiles_;
        ProjectileList::Node* node = projectiles_.NewNode(list.tail, nullptr);
        CSpellEffect* old = node->data;
        node->data = missile;
        if (missile) ++missile->refs_;
        if (old && --old->refs_ == 0) delete old;
        if (list.tail) list.tail->next = node;
        else list.head = node;
        list.tail = node;
    }
    CUnit* victim = UnitOrNull(target);
    if (victim && caster) {
        if (IsHostileType(SpellType(spell))) {
            victim->ServerController()->Brain()->SetSeen(caster);
            diplomacy_.OnAttack(caster, victim, 10);
            target->flags_ |= kFlagFought;
            caster->flags_ |= kFlagFought;
        }
    }
}

void CWorldServer::CastSpell(CSpell* spell, CUnit* caster, const Vector3* from, CMapObject* target,
                             const Vector3* to, int extra) {
    if (!spell) return;
    if (sessionKind_ != kKindSinglePlayer && caster) return;
    int type = SpellType(spell);
    if (type == 0x15 || type == 0x1B || type == 0x24) return;
    std::uint32_t flags = spell->targetFlags_;
    int targets = static_cast<int>(spell->targetCount_);
    if (!(flags & 0xFFFF) && targets <= 1) {
        CreateMissile(spell, caster, *from, target, to, extra);
        return;
    }

    // Several targets: the units around the caster (or `from`), the requested target first.
    CPlayerServer* heroes = nullptr;   // the caster's player state: its party's units are friends
    UnitList units;
    units.vtable = mfc::impl::PtrListClass::Vtable();
    units.count = 0;
    units.free = nullptr;
    units.tail = nullptr;
    units.head = nullptr;
    units.blocks = nullptr;
    units.blockSize = 10;
    if (caster) {
        if (CPlayer* owner = caster->player) heroes = owner->Server();
    }
    if ((flags & kSpellAreaFlag) && !(flags & kSpellNoTargetFlag)) {
        --targets;
        CreateMissile(spell, caster, *from, target, to, extra);
    }
    Vector3 center;
    std::memcpy(&center, caster ? &caster->localPosition_ : from, sizeof(center));
    float radius = spell->range_;
    Sectors()->FindUnitsInRadius(&center, &units, radius);
    if (CUnit* targetUnit = UnitOrNull(target)) {
        UnitList::Node* found = nullptr;
        for (UnitList::Node* node = units.head; node; node = node->next)
            if (node->data == target) {
                found = node;
                break;
            }
        if (found) RemoveNode(units, found);
        UnitList::Node* node = units.NewNode(nullptr, units.head);
        node->data = targetUnit;
        if (units.head) units.head->prev = node;
        else units.tail = node;
        units.head = node;
    }

    while (targets > 0) {
        if (!units.head) break;
        CUnit* best = nullptr;
        UnitList::Node* bestNode = nullptr;
        std::uint32_t bestBits = kFarAway;
        for (UnitList::Node* node = units.head; node;) {
            UnitList::Node* next = node->next;
            CUnit* unit = node->data;
            if (ObjectSlots(unit)->IsDead()) {
                RemoveNode(units, node);
            } else if (unit == target) {
                best = unit;
                bestNode = node;
                break;
            } else {
                double dz = static_cast<double>(to->z) - unit->localPosition_.z;
                double dy = static_cast<double>(to->y) - unit->localPosition_.y;
                double dx = static_cast<double>(to->x) - unit->localPosition_.x;
                double distance = LengthSquaredUnrounded<Sum3::XYZ>(dx, dy, dz);
                float bestDistance;
                std::memcpy(&bestDistance, &bestBits, 4);
                EI_NAN_AUDIT(distance, bestDistance);
                if (distance < bestDistance) {
                    float stored = static_cast<float>(distance);
                    std::memcpy(&bestBits, &stored, 4);
                    best = unit;
                    bestNode = node;
                }
            }
            node = next;
        }
        if (!best) break;
        Unlink(units, bestNode);
        units.FreeNode(bestNode);

        bool hit;
        if (!caster) {
            hit = true;
        } else if (heroes) {
            hit = heroes->partyUnits_.Contains(best) != 0;
        } else {
            // The units the caster's AI sees (its brain's visible list), else diplomacy says.
            UnitList::Node* friends =
                caster->ServerController()->Brain()->memory.visible.head;
            while (friends && friends->data != best) friends = friends->next;
            if (friends) {
                hit = true;
            } else {
                int casterSide = caster->side;
                int bestSide = best->side;
                hit = (diplomacy_.allied_[bestSide] & (1u << (casterSide & 31))) != 0;
            }
        }
        if (!hit) continue;
        Vector3* at = &best->localPosition_;
        if (!SpellHasLineOfSight(this, spell, caster, from, best, at))
            continue;
        if (!spell->CanAffect(caster, best)) continue;
        CreateMissile(spell, caster, *from, best, at, extra);
        --targets;
    }

    units.vtable = mfc::impl::PtrListClass::Vtable();
    units.RemoveAll();
}

EI_REWRITTEN(0x0058E850, CWorldServer::CastSpell);
EI_REWRITTEN(0x0058E670, CWorldServer::CreateMissile);
