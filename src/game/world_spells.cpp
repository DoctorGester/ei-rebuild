// CWorldServer: casting spells into the world (CastSpell 0058E850) and the missile that carries
// one (CreateMissile 0058E670). See world.h and docs/notes/world.md (seventh round).
#include "world.h"

#include "core_maths.h"
#include "script.h"

#include "lib/crt.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "originals_ai.h"
#include "originals_engine.h"
#include "originals_gameplay.h"

#include <cstring>
#include "originals_library.h"

// The distances keep the original's operation order: no reassociation in this file.
#pragma float_control(precise, on, push)

namespace {

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(object) + offset);
}

float GameFloat(std::uintptr_t address) { return *reinterpret_cast<const volatile float*>(address); }

constexpr int kKindSinglePlayer = 1;
constexpr std::uint32_t kSpellAreaFlag = 0x20000000;     // spell +0x3C: also hits the target itself
constexpr std::uint32_t kSpellNoTargetFlag = 0x20000;
constexpr std::uint32_t kFlagFought = 0x80000;           // flags_ of both sides of a hostile spell
constexpr std::uintptr_t kUnitListVtable = 0x0073D454;
constexpr std::uint32_t kFarAway = 0x7E967699;           // 1e38f

// Spell fields (the spell object passed in).
constexpr int kSpellKind = 0x18;          // the high word is the spell type
constexpr int kSpellRadius = 0x34;
constexpr int kSpellTargets = 0x38;
constexpr int kSpellFlags = 0x3C;

using UnitList = mfc::CTypedPtrList<CEngineObject>;

inline void __fastcall UnitListRemoveAll(UnitList* l, void*) { PtrListRemoveAllOriginal(l); }
__declspec(naked) UnitList::Node* __fastcall UnitListNewNode(UnitList*, void*, UnitList::Node* /*prev*/,
                                                           UnitList::Node* /*next*/) EI_ORIGINAL(0x0052CEF0)
__declspec(naked) void __fastcall UnitListFreeNode(UnitList*, void*, UnitList::Node*) EI_ORIGINAL(0x005588A0)
// 004B3330 (RET 4). ScriptGroup::Contains (script_group.cpp), through its address.
__declspec(naked) int __fastcall GroupContains(const void*, void*, CEngineObject*) EI_ORIGINAL(0x004B3330)
// 0054BBC0 (RET 0x10). Whether the spell (EDX) may hit `target` (ECX = the world).
__declspec(naked) bool __fastcall SpellReaches(CWorldServer*, const void* /*spell*/, CEngineObject* /*caster*/,
                                               const Vector3* /*from*/, CEngineObject* /*target*/,
                                               const Vector3* /*at*/) EI_ORIGINAL(0x0054BBC0)
// 0068B4C0 (RET 8). Whether the spell affects `target` at all.
__declspec(naked) bool __fastcall SpellAffects(const void* /*spell*/, void*, CEngineObject* /*caster*/,
                                               CEngineObject* /*target*/) EI_ORIGINAL(0x0068B4C0)
// 0058E670, CreateMissile, through its address (rewritten here).
__declspec(naked) void __fastcall CreateMissileAt(CWorldServer*, void*, const void*, CEngineObject*, Vector3,
                                                  CEngineObject*, const Vector3*, int) EI_ORIGINAL(0x0058E670)
// The missile: constructor 0067E510 (returns this), Init 0067ECB0 (RET 0x1C), Update 0067E6D0.
__declspec(naked) void __fastcall MissileInit(Projectile*, void*, CWorldServer*, const void* /*spell*/,
                                              CEngineObject* /*caster*/, const Vector3* /*from*/,
                                              CEngineObject* /*target*/, const Vector3* /*to*/, int)
    EI_ORIGINAL(0x0067ECB0)
__declspec(naked) int __fastcall MissileUpdate(Projectile*, void*) EI_ORIGINAL(0x0067E6D0)
__declspec(naked) ProjectileList::Node* __fastcall ProjectileNewNode(ProjectileList*, void*, ProjectileList::Node*,
                                                                   ProjectileList::Node*) EI_ORIGINAL(0x0059C7A0)
// 005D7E90 (RET 4). The target's AI brain is attacked by `caster`.
__declspec(naked) void __fastcall BrainAttackedBy(void*, void*, CEngineObject*) EI_ORIGINAL(0x005D7E90)
// 00455680 (RET 0xC). CDiplomacy: `a` attacked `b` (strength).

constexpr int kSlotIsDead = 55;
constexpr int kSlotGroundHeight = 28;

int SpellType(const void* spell) { return static_cast<int>(At<std::uint32_t>(const_cast<void*>(spell), kSpellKind) >> 16); }

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
    if (--list.count == 0) UnitListRemoveAll(&list, nullptr);
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
    const Vector3* aim = target ? &At<Vector3>(target, 0x1C) : to;
    Vector3 end;
    std::memcpy(&end, aim, sizeof(end));
    if (At<std::uint32_t>(const_cast<void*>(spell), kSpellFlags) & kSpellAreaFlag) target = nullptr;
    end.z = StoreFloat(static_cast<float>(ei::VCall<double>(this, kSlotGroundHeight, end.x, end.y) + GameFloat(0x0073C54C)));
    if (caster) std::memcpy(&from, &At<Vector3>(caster, 0x1C), sizeof(from));

    void* memory = gamecrt::operator_new(0xA8);
    At<std::uint32_t>(memory, 4) = 0;   // written before the null test
    Projectile* missile = memory ? ProjectileConstruct(memory, nullptr) : nullptr;
    MissileInit(missile, nullptr, this, spell, caster, &from, target, &end, extra);
    if (MissileUpdate(missile, nullptr)) {
        auto& list = projectiles_.list;
        ProjectileList::Node* node = ProjectileNewNode(&projectiles_, nullptr, list.tail, nullptr);
        auto* old = reinterpret_cast<std::uint8_t*>(node->data);
        node->data = missile;
        if (missile) ++At<std::int16_t>(missile, 4);
        if (old && --At<std::int16_t>(old, 4) == 0) ei::VCall<void>(old, 0, 1u);   // deleting destructor
        if (list.tail) list.tail->next = node;
        else list.head = node;
        list.tail = node;
    }
    if (target && target->GetClassId() == world::kClassUnit && caster) {
        void* controller = At<void*>(target, 0x240);
        if (IsHostileType(SpellType(spell))) {
            BrainAttackedBy(static_cast<std::uint8_t*>(controller) + 0x980, nullptr, caster);
            DiplomacyOnAttack(diplomacy_, nullptr, caster, target, 10);
            target->flags_ |= kFlagFought;
            caster->flags_ |= kFlagFought;
        }
    }
}

void CWorldServer::CastSpell(const void* spell, CEngineObject* caster, const Vector3* from, CEngineObject* target,
                             const Vector3* to, int extra) {
    if (!spell) return;
    if (unknown12D64_ != kKindSinglePlayer && caster) return;
    int type = SpellType(spell);
    if (type == 0x15 || type == 0x1B || type == 0x24) return;
    std::uint32_t flags = At<std::uint32_t>(const_cast<void*>(spell), kSpellFlags);
    int targets = At<int>(const_cast<void*>(spell), kSpellTargets);
    if (!(flags & 0xFFFF) && targets <= 1) {
        CreateMissileAt(this, nullptr, spell, caster, *from, target, to, extra);
        return;
    }

    // Several targets: the units around the caster (or `from`), the requested target first.
    void* heroes = nullptr;   // the caster's player state: its group of heroes are friends
    UnitList units;
    units.vtable = reinterpret_cast<void*>(kUnitListVtable);
    units.count = 0;
    units.free = nullptr;
    units.tail = nullptr;
    units.head = nullptr;
    units.blocks = nullptr;
    units.blockSize = 10;
    if (caster) {
        if (void* owner = At<void*>(caster, 0x24C)) heroes = At<void*>(owner, 0x20);
    }
    if ((flags & kSpellAreaFlag) && !(flags & kSpellNoTargetFlag)) {
        --targets;
        CreateMissileAt(this, nullptr, spell, caster, *from, target, to, extra);
    }
    Vector3 center;
    std::memcpy(&center, caster ? &At<Vector3>(caster, 0x1C) : from, sizeof(center));
    float radius = At<float>(const_cast<void*>(spell), kSpellRadius);
    SectorMapFindUnitsInRadius(sectorMap_, nullptr, &center, &units, radius);
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
            if (ei::VCall<int>(unit, kSlotIsDead)) {
                RemoveNode(units, node);
            } else if (unit == target) {
                best = unit;
                bestNode = node;
                break;
            } else {
                double dz = static_cast<double>(to->z) - At<float>(unit, 0x24);
                double dy = static_cast<double>(to->y) - At<float>(unit, 0x20);
                double dx = static_cast<double>(to->x) - At<float>(unit, 0x1C);
                double distance = dx * dx + dy * dy + dz * dz;
                float bestDistance;
                std::memcpy(&bestDistance, &bestBits, 4);
                if (!(distance >= bestDistance)) {
                    float stored = StoreFloat(static_cast<float>(distance));
                    std::memcpy(&bestBits, &stored, 4);
                    best = unit;
                    bestNode = node;
                }
            }
            node = next;
        }
        if (!best) break;
        Unlink(units, bestNode);
        UnitListFreeNode(&units, nullptr, bestNode);

        bool hit;
        if (!caster) {
            hit = true;
        } else if (heroes) {
            hit = GroupContains(static_cast<std::uint8_t*>(heroes) + 0x510C, nullptr, best) != 0;
        } else {
            // The caster's friends (a list at controller +0xA94), else diplomacy says.
            auto* friends = At<UnitList::Node*>(At<void*>(caster, 0x240), 0xA94);
            while (friends && friends->data != best) friends = friends->next;
            if (friends) {
                hit = true;
            } else {
                int casterSide = At<int>(caster, 0x250);
                int bestSide = At<int>(best, 0x250);
                hit = (At<std::uint32_t>(diplomacy_, bestSide * 4) & (1u << (casterSide & 31))) != 0;
            }
        }
        if (!hit) continue;
        Vector3* at = &At<Vector3>(best, 0x1C);
        if (!SpellReaches(this, spell, caster, from, best, at)) continue;
        if (!SpellAffects(spell, nullptr, caster, best)) continue;
        CreateMissileAt(this, nullptr, spell, caster, *from, best, at, extra);
        --targets;
    }

    units.vtable = reinterpret_cast<void*>(kUnitListVtable);
    units.RemoveAll();
}

#pragma float_control(pop)

EI_REPLACE_METHOD(0x0058E850, CWorldServer::CastSpell);
EI_REPLACE_METHOD(0x0058E670, CWorldServer::CreateMissile);
