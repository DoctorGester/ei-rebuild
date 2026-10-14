// Combat, second part: what happens when an order's animation reaches its moment (attack hits and
// bow missiles, casts, skills, quick items), the spells items cast on hit, item charges, healing,
// death and the AI's reactions to it (witnesses, hostility claims). See combat.h and
// docs/notes/combat.md.
#include "combat.h"
#include "originals_combat.h"
#include "combat_internal.h"
#include "combat_plan.h"

#include "base_manager.h"
#include "item.h"
#include "maths.h"
#include "object_diplomacy.h"
#include "party.h"
#include "resource_manager.h"
#include "world.h"

#include "lib/crt.h"
#include "lib/mfc.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "originals_ai.h"
#include "originals_gameplay.h"
#include "originals_units.h"
#include "originals_world.h"
#include "originals_library.h"

#include <bit>
#include <cstring>

// Keep the original's order of operations (see combat.cpp).
#pragma float_control(precise, on)

namespace {
constexpr float kZero = 0.0f;                                   // 0073B858
constexpr float kHitNoiseScale = 2.0f;                          // 0073EEF0
constexpr float kSkillNoiseScale = 1.5f;                        // 0073EEF8
constexpr std::uint32_t kOneBits = 0x3F800000u;                 // 1.0f, compared as a dword
constexpr int kNoiseKind = 0x1A;

constexpr int kWeaponBow = 5;
constexpr int kWeaponCrossbow = 6;
constexpr std::uint32_t kSkillOnTarget = 0x18A95;
constexpr std::uint32_t kSpellOnWeapon = 0x10000;       // CSpell::targetFlags_: cast by the item
constexpr std::uint32_t kSpellNotOnHit = 0x180000;      // spell prototype 0x18: fires when not hit
constexpr int kMissileObject = 0x5B;                    // object factory kind of a missile
constexpr int kQuickItemConsumable = 2;                 // category used up by order kind 10

// Party messages (0066D6E0 codes).
constexpr int kMessageKilled = 0x30;
constexpr int kMessageScienceFailed = 0x11;
constexpr int kMessageStealFailed = 0x22;
constexpr int kMessageTameFailed = 0x16;


CUnitServer* ControllerOf(const CUnit* unit) { return reinterpret_cast<CUnitServer*>(unit->controller); }

CDiplomacy* DiplomacyOf(void* world) { return &static_cast<CWorldServer*>(world)->diplomacy_; }

// The world's AI events.
AIEventTable* AIEventsOf(void* world) { return static_cast<CWorldServer*>(world)->AIEvents(); }

// x87 FCOM: "ST < operand or unordered".
bool LessOrUnordered(double a, double b) { return !(a >= b); }

float AbsBits(float v) { return std::bit_cast<float>(std::bit_cast<std::uint32_t>(v) & 0x7FFFFFFFu); }

// FLD/FSTP copy of a float.
void FpuCopy(void* dst, const void* src) {
    __asm {
        mov eax, src
        fld dword ptr [eax]
        mov eax, dst
        fstp dword ptr [eax]
    }
}

// An MFC CList of units, as the brain keeps them (CObList layout: vtable, head, tail, count, ...).
using UnitList = mfc::CTypedPtrList<CUnit>;
using UnitListNode = UnitList::Node;

bool ListContains(const UnitList* list, const CUnit* unit) {
    for (const UnitListNode* node = list->head; node; node = node->next)
        if (node->data == unit) return true;
    return false;
}

// OnDamaged's search of the party (the player's units, walked as 00455830 begin() and 004558B0
// operator++ do): the first member that is not `self`; a null member ends it.
CUnit* FirstOtherPartyMember(WorldPlayer* player, const CUnit* self) {
    const auto& map = player->units.map;
    for (auto* node = map.First(); node; node = map.After(node)) {
        auto* member = static_cast<CUnit*>(node->value);
        if (!member) return nullptr;
        if (member != self) return member;
    }
    return nullptr;
}

void* const kPtrListVtable = reinterpret_cast<void*>(0x0073BE80);   // CPtrList

} // namespace

// Not rewritten (forwarders), as __fastcall with the unused EDX for __thiscall.
// 00525900. UnitStats: heals the slots of `serverStats` by `amount`; returns 1.
__declspec(naked) BOOL __fastcall StatsHeal(UnitStats*, void*, void*, float) EI_ORIGINAL(0x00525900)
// 0067AC70. Missile: aims at the object `id` (+0x14C = 0, +0x150 = id).
// 0058ECE0. CWorldServer: applies the quick item `item` (a potion) to `unit`.
__declspec(naked) void __fastcall WorldUseQuickItem(void*, void*, CItem*, CUnit*) EI_ORIGINAL(0x0058ECE0)
// 00594F30. CWorldServer: an item's state changed.
__declspec(naked) void __fastcall WorldItemChanged(void*, void*, CItem*) EI_ORIGINAL(0x00594F30)
// 005935C0 (patched by Rebirth). CWorldServer::RemoveObject (world_server.cpp): removes object `id`.
// 00665BE0. Player: an inventory item changed.
__declspec(naked) void __fastcall PlayerItemChanged(WorldPlayer*, void*, CItem*) EI_ORIGINAL(0x00665BE0)
// 00665470. Player: receives the items in `list` (a CPtrList).
void __fastcall PlayerReceiveItems(char* a0, void*, void* a2, int a3) { PlayerGiveItemsOriginal((void*)a0, nullptr, a2, a3); }
// 0066D6E0. Player messages (player + 0x20 points at it): message `code` about `unit`.
void __fastcall PlayerMessage(void* messages, void*, CUnit* unit, int code) {
    PlayerSendObject0BOriginal(messages, nullptr, unit, code);
}
// 00531400. CUnit: takes a random carried item away (stealing); null when there is none
// (CUnit::TakeRandomItem, unit_steal.cpp; called through its address).
__declspec(naked) CItem* __fastcall UnitTakeRandomItem(CUnit*) EI_ORIGINAL(0x00531400)
// 00551AE0. CUnitServer (the target's): tries to be tamed by `tamer` with skill `skill`; 0 when it
// worked.
// 005AEA60. The controller's voice (+0xB10): says remark `what`.
void __fastcall VoiceSay(void* a0, void*, int a2) { VoiceSayOriginal(a0, nullptr, a2); }
// 005C98C0 / 005C97B0 / 005CAA00 (world + 0x2838, the AI events): a noise of `loudness` and
// `kind` made by `source`; `unit` died; `controller` updates its perception memory.
void __fastcall AIEventsNoise(void* a0, void*, CUnitServer* a2, float a3, int a4, int a5) { AiEventsNoiseOriginal(a0, nullptr, (void*)a2, a3, a4, (const void*)a5); }
__declspec(naked) void __fastcall AIEventsDied(void*, void*, CUnitServer*) EI_ORIGINAL(0x005C97B0)
void __fastcall AIEventsUpdateMemory(void* a0, void*, CUnitServer* a2, void* a3) { AiEventsUpdateMemoryOriginal(a0, nullptr, a2, a3); }
// 005CBE90. Perception memory (controller + 0xA50): a party member `dead` died near `member`.
__declspec(naked) void __fastcall MemoryPartyMemberDied(void*, void*, CUnit*, CUnit*)
    EI_ORIGINAL(0x005CBE90)
// 005C1010 / 005C0DF0. CSectorMap::Remove / Add (sector_map.cpp) on the world's sector map (+0x294).
// 0050F5B0. Map object base: drops all effects (+0x198), marks the object changed.
// 0052CDB0. CList::NewNode(prev, next) of the brain's unit lists.
UnitListNode* __fastcall UnitListNewNode(UnitList* a0, void*, UnitListNode* a2, UnitListNode* a3) { return (UnitListNode*)ListNewNode52CDB0Original((void*)a0, nullptr, (void*)a2, (void*)a3); }
// 00557B60 / 00556870. The death animation's (AnimationSteps) base constructor and copy constructor.
AnimationSteps* __fastcall AnimationListConstruct(AnimationSteps* a0) { return (AnimationSteps*)StepsConstructOriginal((void*)a0); }
AnimationSteps* __fastcall AnimationListCopy(AnimationSteps* a0, void*, const AnimationSteps* a2) { return (AnimationSteps*)StepsCopyConstructOriginal((void*)a0, nullptr, (const void*)a2); }
// 00548DE0 (CtlPlayAnimationOrder, originals_combat.h). CUnitServer: plays `animation` (taken by value and destroyed).
// 00545F40. UnitAction: kind 9, target released, flag18 cleared (+0x1C/+0x20 left alone).


// Calls between the rewritten functions go through the game's addresses (which jump to the
// rewrites) so that the verify build's wrappers see them: most of these have no other callers.
__declspec(naked) void CUnitServer::CallDeliverAttack(CEngineObject*, DamageInfo*) EI_ORIGINAL(0x0054F450)
__declspec(naked) void CUnitServer::CallTriggerWeaponSpell(CEngineObject*) EI_ORIGINAL(0x0054F850)
__declspec(naked) void CUnitServer::CallTriggerItemSpells(char) EI_ORIGINAL(0x0054F8E0)
__declspec(naked) void CUnitServer::CallOnDamaged(CUnit*, int) EI_ORIGINAL(0x00553AC0)
__declspec(naked) void CUnitServer::CallWitnessMurder(void*, CUnit*, CUnit*) EI_ORIGINAL(0x005538D0)
__declspec(naked) std::uint32_t CUnitServer::CallHitDirection(CUnit*, int) EI_ORIGINAL(0x005518F0)
__declspec(naked) void CUnitServer::CallUseSkill(CEngineObject*) EI_ORIGINAL(0x0054F000)
__declspec(naked) void CUnitServer::CallSetDead(int) EI_ORIGINAL(0x00546720)

void CUnitServer::DropQueuedOrder() {
    action2.kind = 9;
    if (action2.target) action2.target->Release();
    action2.target = nullptr;
    action2.flag18 = 0;
    action2.param = 0;
    action2.modifier = 0;
    action2.kind = 0;
    if (action2.target) {
        action2.target->Release();
        action2.target = nullptr;
    }
    action2.modifier = 0;
}

void CUnitServer::FinishAction(int location, char) {
    switch (action.kind) {
    case 3: {
        CallDeliverAttack(action.target, &pendingHit);
        CEngineObject* target = action.target;
        if (!target || target->GetClassId() != kClassUnit) return;
        if (location >= 6) return;
        // The kind of the target's body slot `location`: 3 means it can still be aimed at
        // (OrderAttack aims at "all" otherwise); when it is no longer 3 the order aimed at it now
        // aims at "all" (6).
        if (ControllerOf(static_cast<CUnit*>(target))->stats.slots[location].kind == 3) return;
        if (location != static_cast<int>(action.source)) return;
        action.source = 6;
        return;
    }
    case 4:
    case 5:
        CallFinishCast();
        break;
    case 6:
        CallUseSkill(action.target);
        break;
    case 10: {
        std::uint32_t slot = action.source;
        auto* item = static_cast<CItem*>(unit->GetQuickItem(slot));
        if (item && item->GetCategory() == kQuickItemConsumable) {
            ++item->refCount_;
            unit->RemoveQuickItem(slot);
            WorldUseQuickItem(WorldServer(), nullptr, item, unit);
            item->Release();
        }
        action.kind = 9;
        if (action.target) action.target->Release();
        action.target = nullptr;
        action.flag18 = 0;
        action.param = 0;
        action.modifier = 0;
        if (action2.kind == 9 && action.kind != 0) DropQueuedOrder();
        return;
    }
    default:
        return;
    }
    ActionClear(&action);
    action.param = 0;
    action.modifier = 0;
    if (action2.kind == 9) reinterpret_cast<ControllerOriginals*>(this)->Idle();
}
EI_REPLACE_METHOD(0x00549EC0, CUnitServer::FinishAction);

void CUnitServer::DeliverAttack(CEngineObject* target, DamageInfo* hit) {
    void* world = ManagerContext();
    auto* weapon = static_cast<CItemWeapon*>(unit->GetWeapon(unit->activeWeapon));
    if (weapon && (weapon->GetType() == kWeaponBow || weapon->GetType() == kWeaponCrossbow)) {
        CBaseManager* factory = static_cast<CWorld*>(world)->TimedObjectManager();
        CEngineObject* missile = factory->CreateObject(kMissileObject, -1);
        if (!missile) return;
        Vector3 position;
        const Vector3* from = unit->EyePoint(&position);
        char* bytes = reinterpret_cast<char*>(missile);
        std::memcpy(bytes + 0x1C, from, sizeof(Vector3));
        MissileSetTarget(missile, nullptr, target->id_);
        bytes[0x154] = 1;
        *reinterpret_cast<int*>(bytes + 0x15C) = unit->id_;
        std::memcpy(bytes + 0x160, hit, sizeof(DamageInfo));
        static_cast<CWorldServer*>(world)->AddTimedObject(missile);
    } else {
        ObjectSlots(target)->ReceiveHit(hit, unit, unit);
    }
    CallTriggerWeaponSpell(target);
    if (std::bit_cast<std::uint32_t>(hit->backstabFactor) == kOneBits) {
        float loudness = static_cast<float>(static_cast<double>(Combat().noise) * kHitNoiseScale);
        AIEventsNoise(AIEventsOf(world), nullptr, this, loudness, kNoiseKind, 0);
    }
}
EI_REPLACE_METHOD(0x0054F450, CUnitServer::DeliverAttack);

void CUnitServer::TriggerWeaponSpell(CEngineObject* target) {
    ManagerContext();
    auto* weapon = static_cast<CItem*>(unit->GetWeapon(unit->activeWeapon));
    if (!weapon) return;
    auto* spell = static_cast<CSpell*>(weapon->spell_);
    if (!spell || !(spell->targetFlags_ & kSpellOnWeapon)) return;
    float cost = std::bit_cast<float>(spell->manaCost_);
    if (cost > weapon->charge_) return;
    CtlUseItemCharge(this, nullptr, weapon, cost);
    CUnit* self = unit;
    Vector3 point;
    const Vector3* at = ObjectSlots(target)->AimPoint(&point);
    WorldCastSpellOriginal(WorldServer(), nullptr, spell, self, &self->position, target, at, 0);
}
EI_REPLACE_METHOD(0x0054F850, CUnitServer::TriggerWeaponSpell);

void CUnitServer::TriggerItemSpells(char onHit) {
    ManagerContext();
    for (std::uint32_t slot = 0; slot < 7; ++slot) {
        auto* item = static_cast<CItem*>(unit->armor[slot]);    // 00530830
        if (!item) continue;
        auto* spell = static_cast<CSpell*>(item->spell_);
        if (!spell || !(spell->targetFlags_ & kSpellOnWeapon)) continue;
        float cost = std::bit_cast<float>(spell->manaCost_);
        if (cost > item->charge_) continue;
        bool fires = (spell->unknown_18 & 0xFFFF0000u) == kSpellNotOnHit ? onHit == 0 : onHit != 0;
        if (!fires) continue;
        CtlUseItemCharge(this, nullptr, item, cost);
        CUnit* self = unit;
        WorldCastSpellOriginal(WorldServer(), nullptr, spell, self, &self->position, self, &self->position, 0);
    }
}
EI_REPLACE_METHOD(0x0054F8E0, CUnitServer::TriggerItemSpells);

void CUnitServer::UseItemCharge(CItem* item, float cost) {
    if (!item) return;
    void* world = ManagerContext();
    item->charge_ = item->charge_ - cost;
    WorldItemChanged(world, nullptr, item);
    auto* player = reinterpret_cast<WorldPlayer*>(unit->player);
    const auto* link = reinterpret_cast<const CItemLink*>(static_cast<std::uintptr_t>(item->link_));
    if (!player || !link) return;
    CResourceManager* items = player->Party()->ItemManager();
    auto* copy = static_cast<CItem*>(items->FindObject(link->value));
    if (!copy) return;
    FpuCopy(&copy->charge_, &item->charge_);
    PlayerItemChanged(player, nullptr, copy);
}
EI_REPLACE_METHOD(0x00552C60, CUnitServer::UseItemCharge);

BOOL CUnitServer::Heal(float amount) {
    if (!StatsHeal(&stats, nullptr, ServerStatsOf(unit), amount)) return 0;
    UnitStatsRecompute(&stats, nullptr, ServerStatsOf(unit), unit);
    unit->MarkChanged(0);
    unit->MarkChanged(4);
    GameLog()->Put("Unit ")->Put(static_cast<long>(unit->id_))->Put(" was healed")->PutChar(10)->Apply(kLogEndLine);
    return 1;
}
EI_REPLACE_METHOD(0x005543B0, CUnitServer::Heal);

std::uint32_t CUnitServer::HitDirection(CUnit* attacker, int hitKind) {
    if (!attacker) return 0;
    if (hitKind) return 0;
    const CUnit* self = unit;
    float dx = attacker->position.x - self->position.x;
    float dy = attacker->position.y - self->position.y;
    float dz = attacker->position.z - self->position.z;
    const Quaternion* rotation = &self->orientation;
    Vector3 axisY;
    rotation->AxisY(&axisY);
    // The unit's Y axis points backwards: "front" is the negated projection on it.
    double front = -DotUnrounded<Sum3::ZYX, double>(axisY.x, axisY.y, axisY.z, dx, dy, dz);
    float frontStored = static_cast<float>(front);
    // The X axis of the rotation, kept on the FPU stack (as 0040D940 computes it, unrounded).
    AxisUnrounded<double> axisX = AxisXUnrounded<double>(*rotation);
    double side = -DotUnrounded<Sum3::ZYX, double>(axisX.x, axisX.y, axisX.z, dx, dy, dz);
    float sideStored = static_cast<float>(side);
    if (LessOrUnordered(AbsBits(sideStored), AbsBits(frontStored)))
        return frontStored > kZero ? 0x800000 : 0x400000;
    return sideStored > kZero ? 0xC00000 : 0x1000000;
}
EI_REPLACE_METHOD(0x005518F0, CUnitServer::HitDirection);

void CUnitServer::WitnessMurder(void* witnesses, CUnit* victim, CUnit* murderer) {
    void* world = ManagerContext();
    CDiplomacy* diplomacy = DiplomacyOf(unit->ManagerContext());
    int side = unit->side;
    UnitListNode* node = static_cast<UnitList*>(witnesses)->head;
    if (!node) return;
    std::uint32_t bit = 1u << (static_cast<std::uint32_t>(side) & 31);
    do {
        CUnit* other = node->data;
        node = node->next;
        if (!(diplomacy->allied_[other->side] & bit)) continue;
        other->flags_ |= 0x2000000;
        CUnitServer* controller = ControllerOf(other);
        AIEventsUpdateMemory(AIEventsOf(world), nullptr, controller, controller->brain.memory);
        auto* watched = reinterpret_cast<UnitList*>(&controller->brain.visible);
        if (!ListContains(watched, victim)) continue;
        if (!ListContains(watched, murderer)) {
            UnitListNode* added = UnitListNewNode(watched, nullptr, watched->tail, nullptr);
            if (murderer != added->data) {
                if (added->data) added->data->Release();
                added->data = nullptr;
                if (murderer) ++murderer->refCount_;
                added->data = murderer;
            }
            if (watched->tail) watched->tail->next = added;
            else watched->head = added;
            watched->tail = added;
            GameLog()
                ->Put("Unit ")
                ->Put(static_cast<long>(other->id_))
                ->Put(" has seen murder and is forced to see unit ")
                ->Put(static_cast<long>(murderer->id_))
                ->PutChar(10)
                ->Apply(kLogEndLine);
        }
        DiplomacyOf(world)->OnAttack(murderer, other, 10);
    } while (node);
}
EI_REPLACE_METHOD(0x005538D0, CUnitServer::WitnessMurder);

void CUnitServer::PropagateHostility() {
    if (!(unit->flags_ & 0x80000)) return;
    unit->flags_ &= ~0x80000u;
    claimedHostile |= unit->relationsA;
    void* world = ManagerContext();
    double radius = brain.callRadius;
    // The squared radius goes through a global (007AF40C) and is compared as an int.
    volatile float* limit = reinterpret_cast<volatile float*>(0x007AF40C);
    *limit = static_cast<float>(radius * radius);
    CDiplomacy* diplomacy = DiplomacyOf(world);
    int side = unit->side;
    UnitListNode* node = reinterpret_cast<UnitList*>(&brain.unknown_0F4)->head;
    while (node) {
        CUnit* other = node->data;
        node = node->next;
        if (!(diplomacy->allied_[side] & (1u << (static_cast<std::uint32_t>(other->side) & 31)))) continue;
        CUnit* self = unit;
        double dx = static_cast<double>(other->position.x) - self->position.x;
        double dy = static_cast<double>(other->position.y) - self->position.y;
        float distance = static_cast<float>(LengthSquared2DUnrounded(dy, dx));
        float squaredRadius = *limit;
        if (!(std::bit_cast<std::int32_t>(distance) < std::bit_cast<std::int32_t>(squaredRadius))) continue;
        std::uint32_t mine = self->relationsA;
        CUnitServer* theirController = ControllerOf(other);
        std::uint32_t theirClaims = theirController->claimedHostile;
        if (mine == theirClaims) continue;
        std::uint32_t toThem = ~theirClaims & mine;
        std::uint32_t toMe = other->relationsA & ~claimedHostile;
        if (toThem) theirController->ClaimHostility(self, toThem);
        if (toMe) ClaimHostility(other, toMe);
    }
}
EI_REPLACE_METHOD(0x00552E00, CUnitServer::PropagateHostility);

void CUnitServer::OnDamaged(CUnit* attacker, int hitKind) {
    void* world = ManagerContext();
    if (unit->type_ < 0x32 || unit->type_ > 0x36) {
        unit->flags_ |= 1;
        return;
    }
    if (CompanionSlots(ServerObjectOf(unit))->IsDead() == 0) {
        CallTriggerItemSpells(0);
        if (attacker && attacker->GetClassId() == kClassUnit) DiplomacyOf(world)->OnAttack(attacker, unit, 10);
        BrainSetSeenOriginal(&brain, nullptr, attacker);
        unit->flags_ |= 0x80000;
        CtlPropagateHostility(this);
        std::uint32_t direction = CallHitDirection(attacker, hitKind);
        if (!keepHitDirection) hitDirection = direction;
        return;
    }

    // Dead.
    if (attacker) {
        if (attacker->player) {     // 005301A0
            CUnit* killer = ControllerOf(attacker)->unit;
            if (killer->player)
                PlayerMessage(reinterpret_cast<WorldPlayer*>(killer->player)->server, nullptr, killer, kMessageKilled);
        } else if (unit->player) {
            VoiceSay(&ControllerOf(attacker)->voice, nullptr, 9);
        }
        if (attacker->GetClassId() == kClassUnit) {
            ManagerContext();
            CUnitServer* theirs = ControllerOf(attacker);
            CallWitnessMurder(&brain.unknown_0F4, unit, attacker);
            CallWitnessMurder(&theirs->brain.unknown_0F4, attacker, attacker);
        }
    }
    MapObjectClearEffectsOriginal(unit);
    CallSetDead(1);
    GameLog()->Put("Unit ")->Put(static_cast<long>(unit->id_))->Put(" damaged to death")->PutChar(10)->Apply(kLogEndLine);

    AnimationSteps animation;
    AnimationListConstruct(&animation);
    animation.vtable = kAnimationStepsVtable;
    std::uint32_t direction = CallHitDirection(attacker, hitKind);
    CtlBuildDeathAnimation(this, nullptr, &animation, direction, 1, 0);
    animationHeld = 0;
    frozen = 0;
    AnimationSteps copy;
    AnimationListCopy(&copy, nullptr, &animation);
    copy.extra = animation.extra;
    CtlPlayAnimationOrder(this, nullptr, copy);
    AIEventsDied(AIEventsOf(world), nullptr, this);
    if (char* player = unit->player) {
        if (CUnit* member = FirstOtherPartyMember(reinterpret_cast<WorldPlayer*>(player), unit))
            MemoryPartyMemberDied(ControllerOf(member)->brain.memory, nullptr, member, unit);
        if (IsMultiplayerClient() & 0xFF) unit->flags_ |= 0x20000000;
    }
    animation.vtable = kAnimationStepsBaseVtable;
    if (animation.steps) gamecrt::operator_delete(animation.steps);
}
EI_REPLACE_METHOD(0x00553AC0, CUnitServer::OnDamaged);

void CUnitServer::UseSkill(CEngineObject* target) {
    void* world = ManagerContext();
    if (action.kind == 6 && action.source == kSkillOnTarget) {
        if (!target || target->GetClassId() != kClassUnit) return;
        if (char* player = unit->player) WorldLootOriginal(WorldServer(), nullptr, target, reinterpret_cast<WorldPlayer*>(player));
        WorldRemoveObject(WorldServer(), nullptr, target->id_);
        float loudness = static_cast<float>(static_cast<double>(Combat().noise) * kSkillNoiseScale);
        AIEventsNoise(AIEventsOf(world), nullptr, this, loudness, kNoiseKind, 0);
        return;
    }
    std::uint32_t skill = action.source;
    if (skill != 1 && skill != 0) return;
    CUnit* other = target->GetClassId() == kClassUnit ? static_cast<CUnit*>(target) : nullptr;
    float mine = static_cast<float>(StatsSkillValue(&stats, nullptr, 0, unit));
    float theirs = 0;
    if (other) theirs = static_cast<float>(StatsSkillValue(&ControllerOf(other)->stats, nullptr, 0, other));

    GameLog()->Put("Unit")->Put(static_cast<long>(unit->id_))->Put(" is using skill ");
    const char* name;
    switch (action.source) {
    case 0: name = "science"; break;
    case 1: name = "stealing"; break;
    case 2: name = "tame"; break;
    default: name = "unknown"; break;
    }
    GameLog()->Put(name);
    GameOstream* log = GameLog()->Put(" his_skill = ");
    log->SetField4();
    log = log->Put(static_cast<double>(mine))->Put(" target_skill = ");
    log->SetField4();
    log->Put(static_cast<double>(theirs))->PutChar(10)->Apply(kLogEndLine);

    skill = action.source;
    if (skill != 0 && !LessOrUnordered(theirs, mine)) {
        DiplomacyOf(world)->OnAttack(unit, other, 10);
        return;
    }
    int message;
    switch (skill) {
    case 0: {
        ItemUse record = {1, 0, 0};
        auto* items = static_cast<CBaseManager*>(static_cast<CWorld*>(world)->ItemFactory());
        CEngineObject* studied = items->FindObject(action.param);
        record = {1, 0, 0};
        if (studied) {
            ItemUse scratch;
            if (static_cast<CItem*>(studied)->GetUse(&scratch)->kind != 0) {
                ItemUse scratch2;
                const ItemUse* found = static_cast<CItem*>(studied)->GetUse(&scratch2);
                record = *found;
            }
        }
        record.extra += x87::fistp(mine);
        // Slot 41 (declared with two ints) takes the use record and the unit.
        if (ObjectSlots(target)->ApplyScience(reinterpret_cast<int>(&record), reinterpret_cast<int>(unit))) return;
        if (!unit->player) return;
        message = kMessageScienceFailed;
        break;
    }
    case 1: {
        if (!other) return;
        if (other->player) return;
        CItem* item = UnitTakeRandomItem(other);
        if (item) {
            GameLog()->Put("Item has been stolen from")->PutChar(10)->Apply(kLogEndLine);
            if (char* player = unit->player) {
                mfc::CTypedPtrList<CItem> list;
                list.blocks = nullptr;
                list.vtable = kPtrListVtable;
                list.count = 0;
                list.tail = nullptr;
                list.head = nullptr;
                list.free = nullptr;
                list.blockSize = 10;
                list.AddHead(item);
                PlayerReceiveItems(player, nullptr, &list, 0);
                list.vtable = kPtrListVtable;
                list.RemoveAll();
            }
            item->Release();
            return;
        }
        char* player = unit->player;
        if (player && WorldLootOriginal(world, nullptr, other, reinterpret_cast<WorldPlayer*>(player))) return;
        if (!unit->player) return;
        message = kMessageStealFailed;
        break;
    }
    case 2: {
        // Not reached: skill 2 returned above (sic).
        if (!other) return;
        if (ControllerTame(static_cast<CUnitServer*>(other->controller), nullptr, unit, mine) != 0) return;
        if (!unit->player) return;
        message = kMessageTameFailed;
        break;
    }
    default:
        return;
    }
    CUnit* self = unit;
    PlayerMessage(reinterpret_cast<WorldPlayer*>(self->player)->server, nullptr, self, message);
}
EI_REPLACE_METHOD(0x0054F000, CUnitServer::UseSkill);

void CUnitServer::Idle() {
    if (action.kind == 0) return;
    if (action2.kind == 7 || action2.kind == 8) return;
    DropQueuedOrder();
}
EI_REPLACE_METHOD(0x00555180, CUnitServer::Idle);

void CUnitServer::SetDead(int dead) {
    auto* world = static_cast<CWorldServer*>(ManagerContext());
    if (unit->dead == dead) return;
    CSectorMap* units = world->Sectors();
    SectorMapRemove(units, nullptr, unit);
    unit->dead = dead;
    SectorMapAdd(units, nullptr, unit);
    if (dead && effectObject) {
        world->RemoveTimedObject(effectObject->id_);
        CEngineObject* old = effectObject;
        effectObject = nullptr;
        if (old) old->Release();
    }
    unit->MarkChanged(7);
}
EI_REPLACE_METHOD(0x00546720, CUnitServer::SetDead);
