// The item link record (CItemLink, item.h; a counted value the world server hangs on an item's
// +0x1C). game.exe's table was 0073FA88; its slot 0 is the counted value's deleting destructor
#include "item.h"

#include "ei/hook.h"

CItemLink::CItemLink() {
    refs_ = 0;
    flags6_ = 0;
    flags7_ = 0;
}
EI_REWRITTEN_SPECIAL(0x0058FBB0, CItemLink::CItemLink);

int CItemLink::OnShutdown() { return 0; }
EI_REWRITTEN(0x0058FB90, CItemLink::OnShutdown);

// The original clears the count dword before testing the allocation; ours tests first.
CItemLink* CItemLink::Clone() {
    auto* copy = new CItemLink();
    if (!copy) return nullptr;
    copy->value = value;
    return copy;
}
EI_REWRITTEN(0x0058FB60, CItemLink::Clone);
