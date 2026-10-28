#pragma once

// The exe's STLport map instantiations outside the connection (net_connection_trees.h has its five): their value
// policies and stlp::TreeAbi types. Node sizes from each one's _M_create_node.

#include "console.h"
#include "link_figure.h"
#include "ui_camp.h"

#include "lib/stlp_list.h"
#include "lib/stlp_list_refs.h"
#include "lib/stlp_tree_abi.h"
#include "lib/stlp_tree_mbs.h"

#include <cstdint>
#include <cstring>
#include <type_traits>

namespace lib_trees {

template <typename K, typename Second>
using PairTree = stlp::TreeAbi<stlp::map<K, typename Second::Type>,
                               stlp::PairElement<typename stlp::map<K, typename Second::Type>::Value, Second>>;

// The debug panel's entries (string_map.cpp).
using StringPairs = stlp::TreeAbi<stlp::map<std::string, std::string>>;

// The .mob loader's tint lists by name (a local in a branch that never runs): list<16-byte record> values,
// copied by list(const list&) (a sentinel, then the elements in order) and destroyed with the list (the nodes,
// then the sentinel).
using TintList = stlp::list<stlp::Opaque<16>>;
struct TintListValue {
    using Type = TintList;
    static void Copy(TintList* to, const TintList& from) {
        to->Construct();
        TintList::InsertRange(to->head, from.First(), from.End());
    }
    static void Destroy(TintList* p) { p->Release(); }
};

// A link mesh's part headers (LinkHeaderMap): copied as bytes, zeroed by
// the pair's destructor before the key goes.
struct ZeroedHeader {
    using Type = LinkPartHeader;
    static void Copy(LinkPartHeader* to, const LinkPartHeader& from) { std::memcpy(to, &from, sizeof(*to)); }
    static void Destroy(LinkPartHeader* p) { std::memset(p, 0, sizeof(*p)); }
};
using LinkHeaders = PairTree<std::string, ZeroedHeader>;
static_assert(sizeof(LinkHeaders::Tree) == sizeof(LinkHeaderMap));
// The headers' element policy, for calling the map's members on CLinkMesh::headers_
// (`headers_.Subscript<LinkHeaderElement>(name)`).
using LinkHeaderElement = stlp::PairElement<LinkHeaderMap::Value, ZeroedHeader>;
static_assert(std::is_same_v<LinkHeaders, stlp::TreeAbi<LinkHeaderMap, LinkHeaderElement>>);
// Find(key) on a link mesh's part headers (LinkHeaders' instantiation): the node, or the header for none.
inline LinkHeaderMap::Node* FindLinkHeader(LinkHeaderMap* map, const std::string* key) { return map->find(*key); }
// A link mesh's part ranges (LinkRangeMap; plain values).

// The link loader's mesh cache (a multimap<string, CLinkMesh*> of counted references): one more reference per copy, released by the pair's destructor.
struct CountedMesh : stlp::CountedRef {
    using Type = RefPtr;
};
using MeshCache = PairTree<std::string, CountedMesh>;

// The item screens' model and texture caches (ItemObjectCache, ui_item_info.h):
// the cached object and when it was last used. The game has one instantiation for both.
template <typename Object>
using ItemCacheOf = stlp::TreeAbi<typename ItemObjectCache<Object>::Map>;
using ItemCache = ItemCacheOf<CTexture>;

// The console's command maps (console.h; std::string keys by _mbscmp).
using Commands = stlp::TreeAbi<console::CommandMap>;
// The interface's help topics (std::string keys by _mbscmp).
using HelpTopics = stlp::TreeAbi<stlp::map<std::string, int, stlp::MbsLess>>;
// The "pers" texts by text key (ui_camp.h; 0x18-byte nodes).
using PersonTexts = stlp::TreeAbi<PersonTextMap>;
// The global map's arrival times by exit (CGlobalMap::arrivals_; 0x18-byte nodes).
using Arrivals = stlp::TreeAbi<stlp::map<int, float>>;
static_assert(sizeof(PersonTexts::Node) == 0x18 &&
              sizeof(Arrivals::Node) == 0x18);

// The hero screen's item caches (ItemObjectCache, ui_item_info.h). Their constructors:
// Map() (the header from the pool, empty), then the limit 50.
template <typename Object>
ItemObjectCache<Object>* ConstructHeroItemCache(ItemObjectCache<Object>* self);

} // namespace lib_trees
