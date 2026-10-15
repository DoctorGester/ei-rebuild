// The item link record's own functions (CItemLink, item.h; vtable 0073FA88, a counted value of
// 0xC bytes the world server hangs on an item's +0x1C): 0058FBB0, 0058FB90 (slot 2), 0058FB60
// (slot 3). Slot 0 is the counted value's deleting destructor (0058FBC0, core_streams.cpp).
#include "item.h"

#include "core_streams.h"

#include "ei/hook.h"
#include "lib/crt.h"

CItemLink* CItemLink::Construct() {
    vtable = Vtable();
    return this;
}
EI_REPLACE_METHOD(0x0058FBB0, CItemLink::Construct);

void CItemLink::Reset() {
    vtable = reinterpret_cast<void*>(CRefCounted::kVtableAddress);
    vtable = Vtable();
}
EI_REPLACE_METHOD(0x0058FB90, CItemLink::Reset);

// The original clears the count dword before testing the allocation; ours tests first. The
// counted value's vtable (0073BB58) stands while the value is copied.
CItemLink* CItemLink::Clone() {
    auto* copy = static_cast<CItemLink*>(gamecrt::operator_new(sizeof(CItemLink)));
    if (!copy) return nullptr;
    copy->references = 0;
    copy->refsHigh = 0;
    copy->value = value;
    copy->vtable = Vtable();
    return copy;
}
EI_REPLACE_METHOD(0x0058FB60, CItemLink::Clone);
