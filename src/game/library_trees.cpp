// The exe's STLport map instantiations outside the connection (stlp_tree.h, stlp_tree_abi.h). Several members
// exist more than once (the same body compiled into different objects): one body serves each.
#include "library_trees.h"

#include "ui_item_info.h"   // UiReleaseCachedModel

#include "lib/stlp_pair.h"

namespace lib_trees {

template <typename Object>
ItemObjectCache<Object>* __fastcall ConstructHeroItemCache(ItemObjectCache<Object>* self) {
    ItemCacheOf<Object>::Construct(&self->map);
    self->limit = 50;
    return self;
}
template ItemModelCache* __fastcall ConstructHeroItemCache<CUiMesh>(ItemModelCache*);
template ItemTextureCache* __fastcall ConstructHeroItemCache<CTexture>(ItemTextureCache*);

} // namespace lib_trees
