// The loot of a dead unit (00461CB0; the server's world calls it on its CHoover, +0x140, which the
// function does not use): the prototype's items (new objects from the item manager, put on both
// lists), the gold (two rolls of the prototype: an amount between a minimum and a maximum, kept
// when a percent roll is within the chance), the quick items and carried items, and for units with
// a text key also the armour and weapons, all taken off the unit and put on the drop list. A unit
// is looted once (flag 0x100000). Plain float maths for the amounts (rule 7). See
// docs/notes/objects.md ("Sixth round").
#include "random.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "ei/x87.h"
#include "lib/crt.h"

#include <cstdint>
#include <cstring>

namespace {

template <typename T>
T& At(const void* base, std::size_t offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(const_cast<void*>(base)) + offset);
}

// MFC CPtrList (after its vtable): head, tail, count, free list, blocks, block size.
struct PtrListNode {
    PtrListNode* next;
    PtrListNode* prev;
    void* data;
};
struct PtrList {
    std::uintptr_t vtable;
    PtrListNode* head;
    PtrListNode* tail;
    int count;
    PtrListNode* free;
    void* blocks;
    int blockSize;
};

// CPtrList::NewNode(prev, next) (004621A0, data cleared). The original inlines it for the armour
// and weapons (with the same effects).
__declspec(naked) PtrListNode* __fastcall NewNode(PtrList*, void*, PtrListNode* /*prev*/, PtrListNode* /*next*/) EI_ORIGINAL(0x004621A0)
__declspec(naked) int __fastcall IsTextKey(int) EI_ORIGINAL(0x004613A0)
__declspec(naked) char* __fastcall UnitPrototype(void* /*unit*/) EI_ORIGINAL(0x00530930)
__declspec(naked) void* __fastcall CreateItem(void* /*manager*/, void*, const char* /*name*/) EI_ORIGINAL(0x006AC970)
__declspec(naked) void* __fastcall UnitQuickItem(void* /*unit*/, void*, int) EI_ORIGINAL(0x00531920)
__declspec(naked) void __fastcall UnitRemoveQuickItem(void* /*unit*/, void*, int) EI_ORIGINAL(0x005311E0)
__declspec(naked) void __fastcall UnitRemoveCarried(void* /*unit*/, void*, int) EI_ORIGINAL(0x005313B0)
__declspec(naked) void* __fastcall UnitArmour(void* /*unit*/, void*, int) EI_ORIGINAL(0x00530830)
__declspec(naked) int __fastcall ArmourType(void* /*armour*/) EI_ORIGINAL(0x006A39E0)
__declspec(naked) void __fastcall UnitUnequipArmour(void* /*unit*/, void*, int) EI_ORIGINAL(0x00530840)
__declspec(naked) void* __fastcall UnitWeapon(void* /*unit*/, void*, int) EI_ORIGINAL(0x005318B0)
__declspec(naked) void __fastcall UnitRemoveWeapon(void* /*unit*/, void*, int) EI_ORIGINAL(0x005310D0)

IsaacRandom* Rng() { return reinterpret_cast<IsaacRandom*>(0x007A9D40); }

// An object referenced once more and appended to `list`.
void Append(PtrList* list, void* object) {
    ++At<int>(object, 4);
    PtrListNode* node = NewNode(list, nullptr, list->tail, nullptr);
    node->data = object;
    if (list->tail) list->tail->next = node;
    else list->head = node;
    list->tail = node;
}
// The node appended without a new reference (the prototype items on the second list).
void AppendShared(PtrList* list, void* object) {
    PtrListNode* node = NewNode(list, nullptr, list->tail, nullptr);
    node->data = object;
    if (list->tail) list->tail->next = node;
    else list->head = node;
    list->tail = node;
}

// One gold roll of the prototype: the amount (the minimum rounded, or a random value between the
// minimum and a larger maximum, truncated), kept when the percent roll (CRT rand) is within the
// chance.
void RollGold(const char* prototype, std::size_t chance, std::size_t minimum, int* gold) {
    float low = At<float>(prototype, minimum);
    float high = At<float>(prototype, minimum + 4);
    int amount = x87::fistp(low);
    if (high > low) {
        double random = x87::FromUnsigned(Rng()->Next());
        double scale;
        std::memcpy(&scale, reinterpret_cast<const void*>(0x0073C468), sizeof(scale));
        amount = static_cast<int>(static_cast<double>(high - low) * random * scale + low);
    }
    int percent = gamecrt::rand() * 100 / 0x7FFF % 100 + 1;
    if (!(static_cast<float>(percent) > At<float>(prototype, chance))) *gold += amount;
}

} // namespace

int __fastcall HooverCollectLoot(void* /*hoover*/, void*, void* world, char* unit, void* dropList, void* itemList,
                                 void* /*unused*/, int* gold) {
    auto* drops = static_cast<PtrList*>(dropList);
    auto* items = static_cast<PtrList*>(itemList);
    bool keyed = IsTextKey(At<int>(unit, 0xC)) & 0xFF;
    if (At<std::uint32_t>(unit, 8) & 0x100000) return 0;
    void* manager = ei::VCall<void*>(world, 18);
    char* prototype = UnitPrototype(unit);
    *gold = 0;
    if (prototype) {
        auto names = [&] { return At<const char**>(prototype, 0xE8); };
        auto count = [&] {
            return static_cast<std::uint32_t>((At<std::intptr_t>(prototype, 0xEC) - At<std::intptr_t>(prototype, 0xE8)) >> 2);
        };
        if ((At<std::intptr_t>(prototype, 0xEC) - At<std::intptr_t>(prototype, 0xE8)) & ~std::intptr_t{3}) {
            for (std::uint32_t i = 0; i < count(); ++i) {
                void* item = CreateItem(manager, nullptr, names()[i]);
                if (!item) continue;
                Append(drops, item);
                AppendShared(items, item);
            }
        }
        RollGold(prototype, 0xC8, 0xD0, gold);
        RollGold(prototype, 0xD8, 0xE0, gold);
        if (*gold < 0) *gold = 0;
    }
    for (int slot = 0; slot < 4; ++slot) {
        if (void* item = UnitQuickItem(unit, nullptr, slot)) Append(drops, item);
        UnitRemoveQuickItem(unit, nullptr, slot);
    }
    for (int i = 0; i < At<int>(unit, 0x4A4); ++i) {
        if (void* item = At<void**>(unit, 0x4A0)[i]) Append(drops, item);
    }
    while (At<int>(unit, 0x4A4) != 0) UnitRemoveCarried(unit, nullptr, 0);
    if (keyed) {
        for (int slot = 0; slot < 7; ++slot) {
            void* armour = UnitArmour(unit, nullptr, slot);
            if (!armour) continue;
            Append(drops, armour);
            UnitUnequipArmour(unit, nullptr, ArmourType(armour));
        }
        for (int slot = 0; slot < 4; ++slot) {
            void* weapon = UnitWeapon(unit, nullptr, slot);
            if (!weapon) continue;
            Append(drops, weapon);
            UnitRemoveWeapon(unit, nullptr, slot);
        }
    }
    At<std::uint32_t>(unit, 8) |= 0x100000;
    return 1;
}
EI_REPLACE(0x00461CB0, HooverCollectLoot);
