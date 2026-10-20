// CWorldServer: casting spells into the world (CastSpell 0058E850) and the missile that carries
// one (CreateMissile 0058E670). See world.h and docs/notes/world.md.
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
#include "originals_effects.h"
#include "originals_ai.h"
#include "originals_engine.h"
#include "originals_gameplay.h"

#include <bit>
#include <cstring>
#include "originals_library.h"
#include "originals_script.h"

namespace {

float GameFloat(std::uintptr_t address) { return *reinterpret_cast<const volatile float*>(address); }

constexpr int kKindSinglePlayer = 1;
constexpr std::uint32_t kSpellAreaFlag = 0x20000000;     // spell +0x3C: also hits the target itself
constexpr std::uint32_t kSpellNoTargetFlag = 0x20000;
constexpr std::uint32_t kFlagFought = 0x80000;           // flags_ of both sides of a hostile spell
constexpr std::uint32_t kFarAway = 0x7E967699;           // 1e38f

// The spell passed in. Its +0x34 is the radius (float bits) and +0x38 the number of targets.
const CSpell* AsSpell(const void* spell) { return static_cast<const CSpell*>(spell); }

using UnitList = mfc::CTypedPtrList<CEngineObject>;
UnitList::Node* __fastcall UnitListNewNode(UnitList* a0, void*, UnitList::Node* a2, UnitList::Node* a3) { return (UnitList::Node*)PtrListNewNode52CEF0Original((void*)a0, nullptr, (void*)a2, (void*)a3); }
// SpellHasLineOfSight (0054BBC0, combat.h): whether the spell may hit a target. The missile:
// constructor 0067E510 (returns this), CSpellEffect::Init 0067ECB0, Update 0067E6D0.
// 005D7E90 (AIBrain::SetSeen, RET 4). The target's AI brain is attacked by `caster`.
// 00455680 (RET 0xC). CDiplomacy: `a` attacked `b` (strength).

int SpellType(const void* spell) { return static_cast<int>(static_cast<const CSpell*>(spell)->prototypeId_ >> 16); }

// Whether the spell type turns the target against the caster (the original's switch at
// 0058E7DC: 0x13, 0x19, 0x1E, 0x20, 0x22, 0x26).
bool IsHostileType(int type) {
    switch (type) {
    case 0x13: case 0x19: case 0x1E: case 0x20: case 0x22: case 0x26: return true;
    default: return false;
    }
}

// The list's inlined RemoveAt: unlink, give the node back, empty the list with the last one.
void RemoveNode(UnitList& list, UnitList::Node* node) {
    if (node == list.head) list.head = node->next;
    else node->prev->next = node->next;
    if (node == list.tail) list.tail = node->prev;
    else node->next->prev = node->prev;
    node->next = list.free;
    list.free = node;
    if (--list.count == 0) PtrListRemoveAllOriginal(&list);
}

void Unlink(UnitList& list, UnitList::Node* node) {
    if (node == list.head) list.head = node->next;
    else node->prev->next = node->next;
    if (node == list.tail) list.tail = node->prev;
    else node->next->prev = node->prev;
}

} // namespace

void CWorldServer::CreateMissile(const void* spell, CEngineObject* caster, Vector3 from, CEngineObject* target,
                                 const Vector3* to, int extra) {
    const Vector3* aim = target ? &static_cast<CUnit*>(target)->position : to;
    Vector3 end;
    std::memcpy(&end, aim, sizeof(end));
    if (static_cast<const CSpell*>(spell)->targetFlags_ & kSpellAreaFlag) target = nullptr;
    end.z = static_cast<float>(RawSurfaceHeightAt(end.x, end.y) + GameFloat(0x0073C54C));
    if (caster) std::memcpy(&from, &static_cast<CUnit*>(caster)->position, sizeof(from));

    void* memory = gamecrt::operator_new(0xA8);
    static_cast<Projectile*>(memory)->references = 0;   // written before the null test
    // (Projectile is the missile's view of the spell effect CSpellEffect::Construct builds.)
    auto* missile = memory ? reinterpret_cast<Projectile*>(static_cast<CSpellEffect*>(memory)->Construct()) : nullptr;
    reinterpret_cast<CSpellEffect*>(missile)->Init(this, const_cast<CSpell*>(static_cast<const CSpell*>(spell)), caster,
                                                   &from, target, &end, static_cast<std::uint8_t>(extra));
    if (reinterpret_cast<CSpellEffect*>(missile)->Update()) {
        auto& list = projectiles_;
        ProjectileList::Node* node = projectiles_.NewNode(list.tail, nullptr);
        Projectile* old = node->data;
        node->data = missile;
        if (missile) ++missile->referenceCount;
        if (old && --old->referenceCount == 0) old->DeletingDestructor(1u);
        if (list.tail) list.tail->next = node;
        else list.head = node;
        list.tail = node;
    }
    if (target && target->GetClassId() == world::kClassUnit && caster) {
        CEngineObject* controller = static_cast<CUnit*>(target)->controller;
        if (IsHostileType(SpellType(spell))) {
            reinterpret_cast<CUnitServer*>(controller)->Brain()->SetSeen(caster);
            static_cast<CDiplomacy*>(&diplomacy_)->OnAttack(static_cast<CUnit*>(caster), static_cast<CUnit*>(target), 10);
            target->flags_ |= kFlagFought;
            caster->flags_ |= kFlagFought;
        }
    }
}

void CWorldServer::CastSpell(const void* spell, CEngineObject* caster, const Vector3* from, CEngineObject* target,
                             const Vector3* to, int extra) {
    if (!spell) return;
    if (sessionKind_ != kKindSinglePlayer && caster) return;
    int type = SpellType(spell);
    if (type == 0x15 || type == 0x1B || type == 0x24) return;
    std::uint32_t flags = static_cast<const CSpell*>(spell)->targetFlags_;
    int targets = static_cast<int>(AsSpell(spell)->targetCount_);
    if (!(flags & 0xFFFF) && targets <= 1) {
        CreateMissile(spell, caster, *from, target, to, extra);
        return;
    }

    // Several targets: the units around the caster (or `from`), the requested target first.
    CPlayerServer* heroes = nullptr;   // the caster's player state: its party's units are friends
    UnitList units;
    units.vtable = mfc::PtrListVtable();
    units.count = 0;
    units.free = nullptr;
    units.tail = nullptr;
    units.head = nullptr;
    units.blocks = nullptr;
    units.blockSize = 10;
    if (caster) {
        if (char* owner = static_cast<CUnit*>(caster)->player) heroes = reinterpret_cast<WorldPlayer*>(owner)->server;
    }
    if ((flags & kSpellAreaFlag) && !(flags & kSpellNoTargetFlag)) {
        --targets;
        CreateMissile(spell, caster, *from, target, to, extra);
    }
    Vector3 center;
    std::memcpy(&center, caster ? &static_cast<CUnit*>(caster)->position : from, sizeof(center));
    float radius = std::bit_cast<float>(AsSpell(spell)->range_);
    Sectors()->FindUnitsInRadius(&center, &units, radius);
    if (target && target->GetClassId() == world::kClassUnit) {
        UnitList::Node* found = nullptr;
        for (UnitList::Node* node = units.head; node; node = node->next)
            if (node->data == target) {
                found = node;
                break;
            }
        if (found) RemoveNode(units, found);
        UnitList::Node* node = UnitListNewNode(&units, nullptr, nullptr, units.head);
        node->data = target;
        if (units.head) units.head->prev = node;
        else units.tail = node;
        units.head = node;
    }

    while (targets > 0) {
        if (!units.head) break;
        CEngineObject* best = nullptr;
        UnitList::Node* bestNode = nullptr;
        std::uint32_t bestBits = kFarAway;
        for (UnitList::Node* node = units.head; node;) {
            UnitList::Node* next = node->next;
            CEngineObject* unit = node->data;
            if (ObjectSlots(unit)->IsDead()) {
                RemoveNode(units, node);
            } else if (unit == target) {
                best = unit;
                bestNode = node;
                break;
            } else {
                double dz = static_cast<double>(to->z) - static_cast<CUnit*>(unit)->position.z;
                double dy = static_cast<double>(to->y) - static_cast<CUnit*>(unit)->position.y;
                double dx = static_cast<double>(to->x) - static_cast<CUnit*>(unit)->position.x;
                double distance = LengthSquaredUnrounded<Sum3::XYZ>(dx, dy, dz);
                float bestDistance;
                std::memcpy(&bestDistance, &bestBits, 4);
                if (!(distance >= bestDistance)) {
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
        UnitListFreeNodeOriginal(&units, nullptr, bestNode);

        bool hit;
        if (!caster) {
            hit = true;
        } else if (heroes) {
            hit = heroes->partyUnits_.Contains(best) != 0;
        } else {
            // The units the caster's AI sees (its brain's visible list), else diplomacy says.
            UnitList::Node* friends =
                reinterpret_cast<CUnitServer*>(static_cast<CUnit*>(caster)->controller)->Brain()->visible.head;
            while (friends && friends->data != best) friends = friends->next;
            if (friends) {
                hit = true;
            } else {
                int casterSide = static_cast<CUnit*>(caster)->side;
                int bestSide = static_cast<CUnit*>(best)->side;
                hit = (diplomacy_.allied_[bestSide] & (1u << (casterSide & 31))) != 0;
            }
        }
        if (!hit) continue;
        Vector3* at = &static_cast<CUnit*>(best)->position;
        if (!SpellHasLineOfSight(this, const_cast<CSpell*>(static_cast<const CSpell*>(spell)), static_cast<CUnit*>(caster),
                                 from, static_cast<CUnit*>(best), at))
            continue;
        if (!static_cast<const CSpell*>(spell)->CanAffect(caster, best)) continue;
        CreateMissile(spell, caster, *from, best, at, extra);
        --targets;
    }

    units.vtable = mfc::PtrListVtable();
    units.RemoveAll();
}

EI_REPLACE_METHOD(0x0058E850, CWorldServer::CastSpell);
EI_REPLACE_METHOD(0x0058E670, CWorldServer::CreateMissile);
