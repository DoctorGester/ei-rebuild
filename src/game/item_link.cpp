// The item link record's own functions (CItemLink, item.h; a counted value of 0xC bytes the world
// server hangs on an item's +0x1C): 0058FBB0, 0058FB90 (slot 2), 0058FB60 (slot 3), and its compiled
// table (C19) in place of game.exe's 0073FA88. Slot 0 is the counted value's deleting destructor
// (0058FBC0, core_streams.cpp).
#include "item.h"

#include "core_streams.h"

#include "ei/hook.h"
#include "lib/compiled_vtable.h"
#include "lib/crt.h"
#include "game_data.h"

namespace {
// Slot 2's original sets no result (EAX as left); no caller of the link's reads it.
class CItemLinkClass final : public ei::CompiledTag<CItemLinkClass> {
public:
    virtual void* DeleteScalar(unsigned flags) {   // 0: 0058FBC0
        return ObjectAs<CRefObject>()->ValueDeletingDestruct(flags);
    }
    virtual void Serialize(ChunkArchive*) {}                    // 1: 00412580 (empty)
    virtual void OnShutdown() { Link()->Reset(); }              // 2: 0058FB90
    virtual CItemLink* Clone() { return Link()->Clone(); }      // 3: 0058FB60

private:
    CItemLink* Link() { return ObjectAs<CItemLink>(); }
};
} // namespace

void* CItemLink::Vtable() { return CItemLinkClass::Vtable(); }

CItemLink* CItemLink::Construct() {
    vtable = Vtable();
    return this;
}
EI_REWRITTEN(0x0058FBB0, CItemLink::Construct);

void CItemLink::Reset() {
    vtable = CRefCounted::RootVtable();
    vtable = Vtable();
}
EI_REWRITTEN(0x0058FB90, CItemLink::Reset);

// The original clears the count dword before testing the allocation; ours tests first. The
// counted value's vtable (0073BB58) stands while the value is copied.
CItemLink* CItemLink::Clone() {
    auto* copy = gamecrt::operator_new_as<CItemLink>();
    if (!copy) return nullptr;
    copy->references = 0;
    copy->refsHigh = 0;
    copy->value = value;
    copy->vtable = Vtable();
    return copy;
}
EI_REWRITTEN(0x0058FB60, CItemLink::Clone);
