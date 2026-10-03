// Combat, second part: what happens when an order's animation reaches its moment (attack hits and
// bow missiles, casts, skills, quick items), the spells items cast on hit, item charges, healing,
// death and the AI's reactions to it (witnesses, hostility claims). See combat.h and
// docs/notes/combat.md.
#include "combat.h"
#include "combat_internal.h"

#include "base_manager.h"
#include "item.h"
#include "maths.h"
#include "object_diplomacy.h"
#include "world.h"

#include "lib/crt.h"
#include "lib/mfc.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "ei/x87.h"

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

constexpr int kWorldSlotFactory = 14;   // CWorldServer slot 14: the object factory (+0x48)
constexpr int kWorldSlotItems = 18;     // slot 18: the item manager (+0x54), a CBaseManager
constexpr int kFactorySlotCreate = 23;  // factory slot 23: Create(kind, id)
constexpr int kServerSlotIsDead = 17;   // CMapObjectServer slot 17
constexpr int kUnitSlotHit = 31;        // CUnit slot 31: forwards a hit to its controller's TakeHit
constexpr int kUnitSlotPosition = 48;   // CUnit slot 48: Vector3* (Vector3* out)
constexpr int kUnitSlotAimPoint = 53;   // slot 53: Vector3* (Vector3* out), where spells aim
constexpr int kSlotRecord = 21;         // slot 21 of the science target record: int* (int* out)
constexpr int kSlotStudy = 41;          // slot 41 of the science target: bool (record*, CUnit*)

// Party messages (0066D6E0 codes).
constexpr int kMessageKilled = 0x30;
constexpr int kMessageScienceFailed = 0x11;
constexpr int kMessageStealFailed = 0x22;
constexpr int kMessageTameFailed = 0x16;

// Offsets in the AI brain (CUnitServer::brain, +0x980).
constexpr int kBrainClaimRadius = 0x38;     // +0x9B8 float
constexpr int kBrainMemory = 0xD0;          // +0xA50
constexpr int kBrainNearby = 0xF4;          // +0xA74 CList of nearby units
constexpr int kBrainWatched = 0x110;        // +0xA90 CList of units watched
constexpr int kVoice = 0xB10;               // CUnitServer + 0xB10

// CUnit + 0x274: dead (CUnit slot 55 reads it).
int& DeadFlag(CUnit* unit) { return *reinterpret_cast<int*>(reinterpret_cast<char*>(unit) + 0x274); }

CUnitServer* ControllerOf(const CUnit* unit) { return reinterpret_cast<CUnitServer*>(unit->controller); }

CDiplomacy* DiplomacyOf(void* world) {
    return reinterpret_cast<CDiplomacy*>(static_cast<char*>(world) + 0x12C64);
}

// The AI event manager at world + 0x2838.
void* AIEventsOf(void* world) { return static_cast<char*>(world) + 0x2838; }

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
struct UnitListNode {
    UnitListNode* next;
    UnitListNode* prev;
    CUnit* unit;
};
struct UnitList {
    void* vtable;
    UnitListNode* head;
    UnitListNode* tail;
    int count;
};

bool ListContains(const UnitList* list, const CUnit* unit) {
    for (const UnitListNode* node = list->head; node; node = node->next)
        if (node->unit == unit) return true;
    return false;
}

// A party's members: an STLport hash_map<id, CUnit*> at player + 0x40 (as in object_diplomacy.cpp).
struct PartyNode {
    PartyNode* next;
    std::uint32_t key;
    CUnit* unit;
};
struct PartyMembers {
    std::uint32_t functors;
    PartyNode** buckets;
    PartyNode** finish;
};

std::uint32_t BucketCount(const PartyMembers* map) {
    return static_cast<std::uint32_t>(static_cast<int>(reinterpret_cast<std::uintptr_t>(map->finish) -
                                                       reinterpret_cast<std::uintptr_t>(map->buckets)) >> 2);
}

// 00455830 (hashtable::begin).
PartyNode* FirstNode(const PartyMembers* map) {
    for (std::uint32_t i = 0; i < BucketCount(map); ++i)
        if (map->buckets[i]) return map->buckets[i];
    return nullptr;
}

// operator++ (004558B0 when the chain ends).
PartyNode* NextNode(const PartyMembers* map, const PartyNode* node) {
    if (node->next) return node->next;
    std::uint32_t bucket = node->key % BucketCount(map);
    for (;;) {
        ++bucket;
        if (BucketCount(map) <= bucket) return nullptr;
        if (PartyNode* head = map->buckets[bucket]) return head;
    }
}

// OnDamaged's search of the party: the first member that is not `self`; a null member ends it.
CUnit* FirstOtherPartyMember(char* player, const CUnit* self) {
    auto* map = reinterpret_cast<const PartyMembers*>(player + 0x40);
    for (PartyNode* node = FirstNode(map); node; node = NextNode(map, node)) {
        if (!node->unit) return nullptr;
        if (node->unit != self) return node->unit;
    }
    return nullptr;
}

// The death animation: a CArray of 0xC-byte entries (vtable 0073F01C over the base 0073EF2C)
// plus a dword the copy constructor leaves out. Passed by value to 00548DE0, which destroys it.
struct AnimationList {
    void* vtable;
    void* data;
    int size;
    int capacity;
    int growBy;
    int extra;
};
static_assert(sizeof(AnimationList) == 0x18);
void* const kAnimationListVtable = reinterpret_cast<void*>(0x0073F01C);
void* const kAnimationListBaseVtable = reinterpret_cast<void*>(0x0073EF2C);
void* const kPtrListVtable = reinterpret_cast<void*>(0x0073BE80);   // CPtrList

// What science (skill 0) passes the studied object: three ints, the third raised by the skill.
struct ScienceRecord {
    int a;
    int b;
    int c;
};
} // namespace

// Not rewritten (forwarders), as __fastcall with the unused EDX for __thiscall.
// 00525900. UnitStats: heals the slots of `serverStats` by `amount`; returns 1.
__declspec(naked) BOOL __fastcall StatsHeal(UnitStats*, void*, void*, float) EI_ORIGINAL(0x00525900)
// 00525E90. UnitStats: the value of skill `skill` for the unit, in ST0 (declared double).
__declspec(naked) double __fastcall StatsSkillValue(UnitStats*, void*, int, CUnit*) EI_ORIGINAL(0x00525E90)
// 0067AC70. Missile: aims at the object `id` (+0x14C = 0, +0x150 = id).
__declspec(naked) void __fastcall MissileSetTarget(CEngineObject*, void*, int) EI_ORIGINAL(0x0067AC70)
// 0058E850. CWorldServer: casts `spell` from `caster` at `from` on `target` at `at`.
__declspec(naked) void __fastcall WorldCastSpell(void*, void*, void*, CUnit*, const float*,
                                                 CEngineObject*, const void*, int) EI_ORIGINAL(0x0058E850)
// 0058ECE0. CWorldServer: applies the quick item `item` (a potion) to `unit`.
__declspec(naked) void __fastcall WorldUseQuickItem(void*, void*, CItem*, CUnit*) EI_ORIGINAL(0x0058ECE0)
// 00594F30. CWorldServer: an item's state changed.
__declspec(naked) void __fastcall WorldItemChanged(void*, void*, CItem*) EI_ORIGINAL(0x00594F30)
// 005942D0. CWorldServer: `target` meets `player` (talk / trade); AL non-zero when handled.
__declspec(naked) int __fastcall WorldMeetUnit(void*, void*, CEngineObject*, char*) EI_ORIGINAL(0x005942D0)
// 005935C0 (patched by Rebirth). CWorldServer: runs the dialog of object `id`.
__declspec(naked) void __fastcall WorldStartDialog(void*, void*, int) EI_ORIGINAL(0x005935C0)
// 00665BE0. Player: an inventory item changed.
__declspec(naked) void __fastcall PlayerItemChanged(char*, void*, CItem*) EI_ORIGINAL(0x00665BE0)
// 00665470. Player: receives the items in `list` (a CPtrList).
__declspec(naked) void __fastcall PlayerReceiveItems(char*, void*, void*, int) EI_ORIGINAL(0x00665470)
// 0066D6E0. Player messages (player + 0x20 points at it): message `code` about `unit`.
__declspec(naked) void __fastcall PlayerMessage(void*, void*, CUnit*, int) EI_ORIGINAL(0x0066D6E0)
// 00531400. CUnit: takes a random carried item away (stealing); null when there is none.
__declspec(naked) CItem* __fastcall UnitTakeRandomItem(CUnit*) EI_ORIGINAL(0x00531400)
// 00551AE0. CUnitServer (the target's): tries to be tamed by `tamer` with skill `skill`; 0 when it
// worked.
__declspec(naked) int __fastcall ControllerTame(void*, void*, CUnit*, float) EI_ORIGINAL(0x00551AE0)
// 005AEA60. The controller's voice (+0xB10): says remark `what`.
__declspec(naked) void __fastcall VoiceSay(void*, void*, int) EI_ORIGINAL(0x005AEA60)
// 005C98C0 / 005C97B0 / 005CAA00 (world + 0x2838, the AI events): a noise of `loudness` and
// `kind` made by `source`; `unit` died; `controller` updates its perception memory.
__declspec(naked) void __fastcall AIEventsNoise(void*, void*, CUnitServer*, float, int, int)
    EI_ORIGINAL(0x005C98C0)
__declspec(naked) void __fastcall AIEventsDied(void*, void*, CUnitServer*) EI_ORIGINAL(0x005C97B0)
__declspec(naked) void __fastcall AIEventsUpdateMemory(void*, void*, CUnitServer*, void*)
    EI_ORIGINAL(0x005CAA00)
// 005CBE90. Perception memory (controller + 0xA50): a party member `dead` died near `member`.
__declspec(naked) void __fastcall MemoryPartyMemberDied(void*, void*, CUnit*, CUnit*)
    EI_ORIGINAL(0x005CBE90)
// 005C1010 / 005C0DF0. CSectorMap::Remove / Add (sector_map.cpp) on the world's sector map (+0x294).
__declspec(naked) void __fastcall UnitMapRemove(void*, void*, CUnit*) EI_ORIGINAL(0x005C1010)
__declspec(naked) void __fastcall UnitMapAdd(void*, void*, CUnit*) EI_ORIGINAL(0x005C0DF0)
// 0050F5B0. Map object base: drops all effects (+0x198), marks the object changed.
__declspec(naked) void __fastcall UnitClearEffects(CUnit*) EI_ORIGINAL(0x0050F5B0)
// 0052CDB0. CList::NewNode(prev, next) of the brain's unit lists.
__declspec(naked) UnitListNode* __fastcall UnitListNewNode(UnitList*, void*, UnitListNode*,
                                                           UnitListNode*) EI_ORIGINAL(0x0052CDB0)
// 00557B60 / 00556870. AnimationList's base constructor and copy constructor.
__declspec(naked) AnimationList* __fastcall AnimationListConstruct(AnimationList*) EI_ORIGINAL(0x00557B60)
__declspec(naked) AnimationList* __fastcall AnimationListCopy(AnimationList*, void*, const AnimationList*)
    EI_ORIGINAL(0x00556870)
// 00548DE0. CUnitServer: plays `animation` (taken by value and destroyed).
__declspec(naked) void __fastcall ControllerPlayAnimation(CUnitServer*, void*, AnimationList)
    EI_ORIGINAL(0x00548DE0)
// 00545F40. UnitAction: kind 9, target released, flag18 cleared (+0x1C/+0x20 left alone).
__declspec(naked) void __fastcall ActionClear(UnitAction*) EI_ORIGINAL(0x00545F40)

__declspec(naked) void CUnitServer::ClaimHostility(CUnit*, std::uint32_t) EI_ORIGINAL(0x00552D00)
__declspec(naked) void CUnitServer::CallBuildDeathAnimation(void*, std::uint32_t, int, int) EI_ORIGINAL(0x00550A80)

// Calls between the rewritten functions go through the game's addresses (which jump to the
// rewrites) so that the verify build's wrappers see them: most of these have no other callers.
__declspec(naked) void CUnitServer::CallDeliverAttack(CEngineObject*, DamageInfo*) EI_ORIGINAL(0x0054F450)
__declspec(naked) void CUnitServer::CallTriggerWeaponSpell(CEngineObject*) EI_ORIGINAL(0x0054F850)
__declspec(naked) void CUnitServer::CallTriggerItemSpells(char) EI_ORIGINAL(0x0054F8E0)
__declspec(naked) void CUnitServer::CallUseItemCharge(CItem*, float) EI_ORIGINAL(0x00552C60)
__declspec(naked) void CUnitServer::CallOnDamaged(CUnit*, int) EI_ORIGINAL(0x00553AC0)
__declspec(naked) void CUnitServer::CallWitnessMurder(void*, CUnit*, CUnit*) EI_ORIGINAL(0x005538D0)
__declspec(naked) void CUnitServer::CallPropagateHostility() EI_ORIGINAL(0x00552E00)
__declspec(naked) std::uint32_t CUnitServer::CallHitDirection(CUnit*, int) EI_ORIGINAL(0x005518F0)
__declspec(naked) void CUnitServer::CallUseSkill(CEngineObject*) EI_ORIGINAL(0x0054F000)
__declspec(naked) void CUnitServer::CallIdle() EI_ORIGINAL(0x00555180)
__declspec(naked) void CUnitServer::CallSetDead(int) EI_ORIGINAL(0x00546720)

void CUnitServer::DropQueuedOrder() {
    action2.kind = 9;
    if (action2.target) action2.target->Release();
    action2.target = nullptr;
    action2.flag18 = 0;
    action2.unknown_1C = 0;
    action2.unknown_20 = 0;
    action2.kind = 0;
    if (action2.target) {
        action2.target->Release();
        action2.target = nullptr;
    }
    action2.unknown_20 = 0;
}

void CUnitServer::FinishAction(int location, char) {
    switch (action.kind) {
    case 3: {
        CallDeliverAttack(action.target, &pendingHit);
        CEngineObject* target = action.target;
        if (!target || target->GetClassId() != kClassUnit) return;
        if (location >= 6) return;
        // The kind (+0xE0 of the 0xF4-byte slot) of the target's body part `location`: 3 means it
        // can still be aimed at (OrderAttack aims at "all" otherwise); when it is no longer 3 the
        // order aimed at it now aims at "all" (6).
        const char* slots = reinterpret_cast<const char*>(static_cast<CUnit*>(target)->controller) + 0x3D0;
        if (*reinterpret_cast<const int*>(slots + location * 0xF4) == 3) return;
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
        action.unknown_1C = 0;
        action.unknown_20 = 0;
        if (action2.kind == 9 && action.kind != 0) DropQueuedOrder();
        return;
    }
    default:
        return;
    }
    ActionClear(&action);
    action.unknown_1C = 0;
    action.unknown_20 = 0;
    if (action2.kind == 9) CallIdle();
}
EI_REPLACE_METHOD(0x00549EC0, CUnitServer::FinishAction);

void CUnitServer::DeliverAttack(CEngineObject* target, DamageInfo* hit) {
    void* world = ManagerContext();
    auto* weapon = static_cast<CItemWeapon*>(unit->GetWeapon(unit->activeWeapon));
    if (weapon && (weapon->GetType() == kWeaponBow || weapon->GetType() == kWeaponCrossbow)) {
        auto* factory = ei::VCall<CEngineObject*>(world, kWorldSlotFactory);
        auto* missile = ei::VCall<CEngineObject*>(factory, kFactorySlotCreate, kMissileObject, -1);
        if (!missile) return;
        Vector3 position;
        const Vector3* from = ei::VCall<const Vector3*>(unit, kUnitSlotPosition, &position);
        char* bytes = reinterpret_cast<char*>(missile);
        std::memcpy(bytes + 0x1C, from, sizeof(Vector3));
        MissileSetTarget(missile, nullptr, target->id_);
        bytes[0x154] = 1;
        *reinterpret_cast<int*>(bytes + 0x15C) = unit->id_;
        std::memcpy(bytes + 0x160, hit, sizeof(DamageInfo));
        static_cast<CWorldServer*>(world)->AddTimedObject(missile);
    } else {
        ei::VCall<void>(target, kUnitSlotHit, hit, unit, unit);
    }
    CallTriggerWeaponSpell(target);
    if (std::bit_cast<std::uint32_t>(hit->unknown_34) == kOneBits) {
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
    float cost = std::bit_cast<float>(spell->unknown_40);
    if (cost > weapon->charge_) return;
    CallUseItemCharge(weapon, cost);
    CUnit* self = unit;
    Vector3 point;
    const void* at = ei::VCall<const Vector3*>(target, kUnitSlotAimPoint, &point);
    WorldCastSpell(WorldServer(), nullptr, spell, self, &self->positionX, target, at, 0);
}
EI_REPLACE_METHOD(0x0054F850, CUnitServer::TriggerWeaponSpell);

void CUnitServer::TriggerItemSpells(char onHit) {
    ManagerContext();
    for (std::uint32_t slot = 0; slot < 7; ++slot) {
        auto* item = static_cast<CItem*>(unit->armor[slot]);    // 00530830
        if (!item) continue;
        auto* spell = static_cast<CSpell*>(item->spell_);
        if (!spell || !(spell->targetFlags_ & kSpellOnWeapon)) continue;
        float cost = std::bit_cast<float>(spell->unknown_40);
        if (cost > item->charge_) continue;
        bool fires = (spell->unknown_18 & 0xFFFF0000u) == kSpellNotOnHit ? onHit == 0 : onHit != 0;
        if (!fires) continue;
        CallUseItemCharge(item, cost);
        CUnit* self = unit;
        WorldCastSpell(WorldServer(), nullptr, spell, self, &self->positionX, self, &self->positionX, 0);
    }
}
EI_REPLACE_METHOD(0x0054F8E0, CUnitServer::TriggerItemSpells);

void CUnitServer::UseItemCharge(CItem* item, float cost) {
    if (!item) return;
    void* world = ManagerContext();
    item->charge_ = item->charge_ - cost;
    WorldItemChanged(world, nullptr, item);
    char* player = unit->player;
    const char* ref = reinterpret_cast<const char*>(static_cast<std::uintptr_t>(item->unknown_1C));
    if (!player || !ref) return;
    auto* items = ei::VCall<CBaseManager*>(player + 0x70, kWorldSlotItems);
    auto* copy = static_cast<CItem*>(items->FindObject(Field<std::uint32_t>(ref, 8)));
    if (!copy) return;
    FpuCopy(&copy->charge_, &item->charge_);
    PlayerItemChanged(player, nullptr, copy);
}
EI_REPLACE_METHOD(0x00552C60, CUnitServer::UseItemCharge);

BOOL CUnitServer::Heal(float amount) {
    if (!StatsHeal(&stats, nullptr, ServerStatsOf(unit), amount)) return 0;
    StatsRecompute(&stats, nullptr, ServerStatsOf(unit), unit);
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
    float dx = attacker->positionX - self->positionX;
    float dy = attacker->positionY - self->positionY;
    float dz = attacker->positionZ - self->positionZ;
    const auto* rotation = reinterpret_cast<const Quaternion*>(reinterpret_cast<const char*>(self) + 0x34);
    Vector3 axisY;
    rotation->AxisY(&axisY);
    // The unit's Y axis points backwards: "front" is the negated projection on it.
    double front = -((static_cast<double>(axisY.z) * dz + static_cast<double>(axisY.y) * dy) +
                     static_cast<double>(axisY.x) * dx);
    float frontStored = static_cast<float>(front);
    // The X axis of the rotation, kept on the FPU stack (as 0040D940 computes it, unrounded).
    double w = rotation->w, x = rotation->x, y = rotation->y, z = rotation->z;
    double xx = x * x;
    double axisX = (w * w - ((y * y + z * z) + xx)) + (xx + xx);
    double axisXy = w * z + x * y;
    axisXy = axisXy + axisXy;
    double axisXz = x * z - w * y;
    axisXz = axisXz + axisXz;
    double side = -((axisXz * dz + axisXy * dy) + axisX * dx);
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
        CUnit* other = node->unit;
        node = node->next;
        if (!(diplomacy->allied_[other->side] & bit)) continue;
        other->flags_ |= 0x2000000;
        CUnitServer* controller = ControllerOf(other);
        AIEventsUpdateMemory(AIEventsOf(world), nullptr, controller, controller->brain + kBrainMemory);
        auto* watched = reinterpret_cast<UnitList*>(controller->brain + kBrainWatched);
        if (!ListContains(watched, victim)) continue;
        if (!ListContains(watched, murderer)) {
            UnitListNode* added = UnitListNewNode(watched, nullptr, watched->tail, nullptr);
            if (murderer != added->unit) {
                if (added->unit) added->unit->Release();
                added->unit = nullptr;
                if (murderer) ++murderer->refCount_;
                added->unit = murderer;
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
    double radius = *reinterpret_cast<const float*>(brain + kBrainClaimRadius);
    // The squared radius goes through a global (007AF40C) and is compared as an int.
    volatile float* limit = reinterpret_cast<volatile float*>(0x007AF40C);
    *limit = static_cast<float>(radius * radius);
    CDiplomacy* diplomacy = DiplomacyOf(world);
    int side = unit->side;
    UnitListNode* node = reinterpret_cast<UnitList*>(brain + kBrainNearby)->head;
    while (node) {
        CUnit* other = node->unit;
        node = node->next;
        if (!(diplomacy->allied_[side] & (1u << (static_cast<std::uint32_t>(other->side) & 31)))) continue;
        CUnit* self = unit;
        double dx = static_cast<double>(other->positionX) - self->positionX;
        double dy = static_cast<double>(other->positionY) - self->positionY;
        float distance = static_cast<float>(dy * dy + dx * dx);
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
    if (ei::VCall<int>(ServerObjectOf(unit), kServerSlotIsDead) == 0) {
        CallTriggerItemSpells(0);
        if (attacker && attacker->GetClassId() == kClassUnit) DiplomacyOf(world)->OnAttack(attacker, unit, 10);
        RememberAttacker(brain, nullptr, attacker);
        unit->flags_ |= 0x80000;
        CallPropagateHostility();
        std::uint32_t direction = CallHitDirection(attacker, hitKind);
        if (!keepHitDirection) hitDirection = direction;
        return;
    }

    // Dead.
    if (attacker) {
        if (attacker->player) {     // 005301A0
            CUnit* killer = ControllerOf(attacker)->unit;
            if (killer->player) PlayerMessage(*reinterpret_cast<void**>(killer->player + 0x20), nullptr, killer, kMessageKilled);
        } else if (unit->player) {
            VoiceSay(reinterpret_cast<char*>(attacker->controller) + kVoice, nullptr, 9);
        }
        if (attacker->GetClassId() == kClassUnit) {
            ManagerContext();
            CUnitServer* theirs = ControllerOf(attacker);
            CallWitnessMurder(brain + kBrainNearby, unit, attacker);
            CallWitnessMurder(theirs->brain + kBrainNearby, attacker, attacker);
        }
    }
    UnitClearEffects(unit);
    CallSetDead(1);
    GameLog()->Put("Unit ")->Put(static_cast<long>(unit->id_))->Put(" damaged to death")->PutChar(10)->Apply(kLogEndLine);

    AnimationList animation;
    AnimationListConstruct(&animation);
    animation.vtable = kAnimationListVtable;
    std::uint32_t direction = CallHitDirection(attacker, hitKind);
    CallBuildDeathAnimation(&animation, direction, 1, 0);
    unknown_021 = 0;
    unknown_020 = 0;
    AnimationList copy;
    AnimationListCopy(&copy, nullptr, &animation);
    copy.extra = animation.extra;
    ControllerPlayAnimation(this, nullptr, copy);
    AIEventsDied(AIEventsOf(world), nullptr, this);
    if (char* player = unit->player) {
        if (CUnit* member = FirstOtherPartyMember(player, unit))
            MemoryPartyMemberDied(ControllerOf(member)->brain + kBrainMemory, nullptr, member, unit);
        if (IsMultiplayerClient() & 0xFF) unit->flags_ |= 0x20000000;
    }
    animation.vtable = kAnimationListBaseVtable;
    if (animation.data) gamecrt::operator_delete(animation.data);
}
EI_REPLACE_METHOD(0x00553AC0, CUnitServer::OnDamaged);

void CUnitServer::UseSkill(CEngineObject* target) {
    void* world = ManagerContext();
    if (action.kind == 6 && action.source == kSkillOnTarget) {
        if (!target || target->GetClassId() != kClassUnit) return;
        if (char* player = unit->player) WorldMeetUnit(WorldServer(), nullptr, target, player);
        WorldStartDialog(WorldServer(), nullptr, target->id_);
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
        ScienceRecord record = {1, 0, 0};
        auto* items = ei::VCall<CBaseManager*>(world, kWorldSlotItems);
        CEngineObject* studied = items->FindObject(action.unknown_1C);
        record = {1, 0, 0};
        if (studied) {
            ScienceRecord scratch;
            if (*ei::VCall<const int*>(studied, kSlotRecord, &scratch) != 0) {
                ScienceRecord scratch2;
                const auto* found = ei::VCall<const ScienceRecord*>(studied, kSlotRecord, &scratch2);
                record = *found;
            }
        }
        record.c += x87::fistp(mine);
        if (ei::VCall<int>(target, kSlotStudy, &record, unit) & 0xFF) return;
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
        if (player && (WorldMeetUnit(world, nullptr, other, player) & 0xFF)) return;
        if (!unit->player) return;
        message = kMessageStealFailed;
        break;
    }
    case 2: {
        // Not reached: skill 2 returned above (sic).
        if (!other) return;
        if (ControllerTame(other->controller, nullptr, unit, mine) != 0) return;
        if (!unit->player) return;
        message = kMessageTameFailed;
        break;
    }
    default:
        return;
    }
    CUnit* self = unit;
    PlayerMessage(*reinterpret_cast<void**>(self->player + 0x20), nullptr, self, message);
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
    if (DeadFlag(unit) == dead) return;
    void* units = reinterpret_cast<char*>(world) + 0x294;
    UnitMapRemove(units, nullptr, unit);
    DeadFlag(unit) = dead;
    UnitMapAdd(units, nullptr, unit);
    if (dead && effectObject) {
        world->RemoveTimedObject(effectObject->id_);
        CEngineObject* old = effectObject;
        effectObject = nullptr;
        if (old) old->Release();
    }
    unit->MarkChanged(7);
}
EI_REPLACE_METHOD(0x00546720, CUnitServer::SetDead);
