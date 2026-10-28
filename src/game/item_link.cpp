// The item link record (CItemLink, item.h; a counted value the world server hangs on an item's +0x1C); its first
// virtual is the counted value's deleting destructor.
#include "item.h"

CItemLink::CItemLink() {
    refs_ = 0;
    flags6_ = 0;
    flags7_ = 0;
}

int CItemLink::OnShutdown() { return 0; }

// The count dword is cleared only after the allocation is tested.
CItemLink* CItemLink::Clone() {
    auto* copy = new CItemLink();
    if (!copy) return nullptr;
    copy->value = value;
    return copy;
}
