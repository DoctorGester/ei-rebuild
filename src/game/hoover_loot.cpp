// The loot of a dead unit (00461CB0; the server's world calls it on its CHoover, +0x140, which the
// function does not use): the prototype's items (new objects from the item manager, put on both
// lists), the gold (two rolls of the prototype: an amount between a minimum and a maximum, kept
// when a percent roll is within the chance), the quick items and carried items, and for units with
// a text key also the armour and weapons, all taken off the unit and put on the drop list. A unit
// is looted once (flag 0x100000). Plain float maths for the amounts (rule 7). See
// docs/notes/objects.md ("Rewritten").
#include "item.h"
#include "object_tables.h"
#include "random.h"
#include "unit.h"
#include "unit_db.h"
#include "world.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "originals_engine.h"
#include "originals_gameplay.h"
#include "originals_units.h"

#include <cstdint>
#include <cstring>
#include "originals_library.h"
#include "lib/mfc.h"
#include "game_data.h"
#include "lib/nan_audit.h"

namespace {

// MFC CPtrList (mfc::CTypedPtrList layout).
using PtrList = mfc::CTypedPtrList<void>;
using PtrListNode = PtrList::Node;

// CPtrList::NewNode(prev, next) (004621A0, data cleared). The original inlines it for the armour
// and weapons (with the same effects).
static char* UnitPrototype(void* unit) { return reinterpret_cast<char*>(static_cast<CUnit*>(unit)->GetPrototype()); }


// An object referenced once more and appended to `list`.
void Append(PtrList* list, void* object) {
    ++static_cast<CEngineObject*>(object)->refCount_;
    PtrListNode* node = list->NewNode(list->tail, nullptr);
    node->data = object;
    if (list->tail) list->tail->next = node;
    else list->head = node;
    list->tail = node;
}
// The node appended without a new reference (the prototype items on the second list).
void AppendShared(PtrList* list, void* object) {
    PtrListNode* node = list->NewNode(list->tail, nullptr);
    node->data = object;
    if (list->tail) list->tail->next = node;
    else list->head = node;
    list->tail = node;
}

// One gold roll of the prototype: the amount (the minimum rounded, or a random value between the
// minimum and a larger maximum, truncated), kept when the percent roll (CRT rand) is within the
// chance.
void RollGold(const UnitGoldRoll& roll, int* gold) {
    float low = roll.minimum;
    float high = roll.maximum;
    int amount = x87::fistp(low);
    if (high > low) {
        double random = x87::FromUnsigned(IsaacRandom::Global().Next());
        double scale;
        std::memcpy(&scale, ei::GamePointer<const void>(0x0073C468), sizeof(scale));
        amount = static_cast<int>(static_cast<double>(high - low) * random * scale + low);
    }
    int percent = gamecrt::rand() * 100 / 0x7FFF % 100 + 1;
    EI_NAN_AUDIT(roll.chance);
    if (static_cast<float>(percent) <= roll.chance) *gold += amount;
}

} // namespace

int __fastcall HooverCollectLoot(void* /*hoover*/, void*, void* world, char* unit, void* dropList, void* itemList,
                                 void* /*unused*/, int* gold) {
    auto* drops = static_cast<PtrList*>(dropList);
    auto* items = static_cast<PtrList*>(itemList);
    auto* cunit = reinterpret_cast<CUnit*>(unit);
    bool keyed = IsTextKey(cunit->id_) & 0xFF;
    if (cunit->flags_ & 0x100000) return 0;
    void* manager = static_cast<CWorld*>(world)->ItemFactory();
    char* prototype = UnitPrototype(unit);
    *gold = 0;
    if (prototype) {
        const CStringVector& itemNames = reinterpret_cast<const UnitPrototypeRecord*>(prototype)->lootItems;
        auto names = [&] { return reinterpret_cast<const char* const*>(itemNames.begin()); };
        auto bytes = [&] { return reinterpret_cast<std::intptr_t>(itemNames.end()) - reinterpret_cast<std::intptr_t>(itemNames.begin()); };
        auto count = [&] { return static_cast<std::uint32_t>(bytes() >> 2); };
        if (bytes() & ~std::intptr_t{3}) {
            for (std::uint32_t i = 0; i < count(); ++i) {
                void* item = ItemManagerCreateItemOriginal(manager, nullptr, names()[i]);
                if (!item) continue;
                Append(drops, item);
                AppendShared(items, item);
            }
        }
        const auto* record = reinterpret_cast<const UnitPrototypeRecord*>(prototype);
        RollGold(record->gold, gold);
        RollGold(record->secondGold, gold);
        if (*gold < 0) *gold = 0;
    }
    for (int slot = 0; slot < 4; ++slot) {
        if (void* item = cunit->GetQuickItem(slot)) Append(drops, item);
        cunit->RemoveQuickItem(slot);
    }
    for (int i = 0; i < cunit->carriedCount; ++i) {
        if (void* item = cunit->carried[i]) Append(drops, item);
    }
    while (cunit->carriedCount != 0) cunit->RemoveCarried(0);
    if (keyed) {
        for (int slot = 0; slot < 7; ++slot) {
            void* armour = (void*)static_cast<const CUnit*>((const void*)unit)->WornItem(slot);
            if (!armour) continue;
            Append(drops, armour);
            cunit->UnequipArmour(reinterpret_cast<CItemArmor*>(armour)->GetType());
        }
        for (int slot = 0; slot < 4; ++slot) {
            void* weapon = cunit->GetWeapon(slot);
            if (!weapon) continue;
            Append(drops, weapon);
            cunit->RemoveWeapon(slot);
        }
    }
    cunit->flags_ |= 0x100000;
    return 1;
}
EI_REPLACE(0x00461CB0, HooverCollectLoot);
