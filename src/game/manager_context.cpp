// ManagerContext's own members (manager_context.h): the base Create and the copy of an item that
// the party and the client world make (their DuplicateItem).
#include "manager_context.h"

#include "item.h"
#include "item_manager.h"

#include <cstdint>

void ManagerContext::Create() {}

Item* ManagerContext::MakeCopyOf(Item* item) {
    std::uint32_t packed = item->prototypeId;
    Item* copy = ItemFactory()->MakeItem(item->Kind(), packed >> 16, packed & 0xFFFF);
    item->CopyTo(copy, GetSpellManager());
    return copy;
}
