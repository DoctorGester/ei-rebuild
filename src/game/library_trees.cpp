// The exe's STLport map instantiations outside the connection (library_trees.h), their members
// registered at the library addresses (stlp_tree.h, stlp_tree_abi.h) so the standalone image has
// them; the game's forwarders keep calling these addresses. Several members exist more than once
// (the same body compiled into different objects): one body serves each. Checked in game by
// verify_library_trees.cpp until S21d.
#include "library_trees.h"

#include "ui_item_info.h"   // UiReleaseCachedModel

#include "ei/hook.h"

namespace lib_trees {

HeroItemCache* __fastcall ConstructHeroItemCache(HeroItemCache* self) {
    ItemCache::Construct(&self->map);
    self->limit = 50;
    return self;
}

} // namespace lib_trees

using namespace lib_trees;

// --- The console's command maps (map<CString, handler>) -------------------------------------------
EI_REPLACE(0x005E0A90, Commands::Construct);
EI_REPLACE(0x005DC080, Commands::Destroy);

