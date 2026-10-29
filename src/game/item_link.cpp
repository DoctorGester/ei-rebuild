// The item link record (CItemLink, item.h): a counted value the world server hangs on an item (CItem::link_).
#include "item.h"

CItemLink::CItemLink() {
    refs_ = 0;
    flags6_ = 0;
    flags7_ = 0;
}

void CItemLink::OnShutdown() {}

CItemLink* CItemLink::Clone() {
    auto* copy = new CItemLink();
    copy->value = value;
    return copy;
}
