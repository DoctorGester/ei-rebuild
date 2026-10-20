#pragma once

// The exe's STLport map instantiations outside the connection (net_connection_trees.h has its five):
// their value policies and stlp::TreeAbi types, registered at their addresses by
// library_trees.cpp. Node sizes from each one's _M_create_node.

#include "console.h"       // CommandMap
#include "link_figure.h"   // LinkPartHeader, LinkPartRanges, LinkHeaderMap, LinkRangeMap
#include "ui_camp.h"       // PersonTextMap

#include "lib/stlp_list.h"
#include "lib/stlp_list_refs.h"
#include "lib/stlp_tree_abi.h"
#include "lib/stlp_tree_cstring.h"

#include <cstdint>
#include <cstring>

namespace lib_trees {

template <typename K, typename Second>
using PairTree = stlp::TreeAbi<stlp::map<K, typename Second::Type>,
                               stlp::PairElement<typename stlp::map<K, typename Second::Type>::Value, Second>>;

// The debug panel's entries (string_map.cpp; 0x28-byte nodes).
using StringPairs = stlp::TreeAbi<stlp::map<stlp::string, stlp::string>>;

// The .mob loader's tint lists by name (00481660's local, in a branch that never runs; 0x20-byte
// nodes): list<16-byte record> values, copied by list(const list&) (00483F00: a sentinel, then the
// elements in order) and destroyed with the list (004824B0: the nodes, then the sentinel).
using TintList = stlp::list<stlp::Opaque<16>>;
struct TintListValue {
    using Type = TintList;
    static void Copy(TintList* to, const TintList& from) {
        to->Construct();
        TintList::InsertRange(to->head, from.First(), from.End());
    }
    static void Destroy(TintList* p) { p->Release(); }
};
using TintLists = PairTree<stlp::string, TintListValue>;

// A link mesh's part headers (LinkHeaderMap, 0x6C-byte nodes): copied as bytes (004F65F0), zeroed by
// the pair's destructor (004F4E70) before the key goes.
struct ZeroedHeader {
    using Type = LinkPartHeader;
    static void Copy(LinkPartHeader* to, const LinkPartHeader& from) { std::memcpy(to, &from, sizeof(*to)); }
    static void Destroy(LinkPartHeader* p) { std::memset(p, 0, sizeof(*p)); }
};
using LinkHeaders = PairTree<stlp::string, ZeroedHeader>;
static_assert(sizeof(LinkHeaders::Tree) == sizeof(LinkHeaderMap) && sizeof(LinkHeaders::Node) == 0x6C);
// 005197B0: find(key) on a link mesh's part headers (LinkHeaders' instantiation): the node, or the
// header for none.
inline LinkHeaderMap::Node* FindLinkHeader(LinkHeaderMap* map, const stlp::string* key) {
    LinkHeaders::Node* node;
    LinkHeaders::Find(reinterpret_cast<LinkHeaders::Tree*>(map), nullptr, &node, key);
    return reinterpret_cast<LinkHeaderMap::Node*>(node);
}
// A link mesh's part ranges (LinkRangeMap, 0x4C-byte nodes; plain values).
using LinkRanges = stlp::TreeAbi<LinkRangeMap>;

// The link loader's mesh cache (a multimap<string, CLinkMesh*> of counted references; 0x20-byte
// nodes): one more reference per copy (004FC6E0), released by the pair's destructor (004F7A80).
struct CountedMesh : stlp::CountedRef {
    using Type = RefPtr;
};
using MeshCache = PairTree<stlp::string, CountedMesh>;

// The item screens' model and texture caches (ItemObjectCache, ui_item_info.h; 0x24-byte nodes):
// the cached object and when it was last used.
using ItemCache = stlp::TreeAbi<ItemObjectCache::Map>;

// The console's command maps (console.h; 0x18-byte nodes, CString keys by _mbscmp).
using Commands = stlp::TreeAbi<console::CommandMap>;
// The interface's help topics (CInterface3D +0x98; 0x18-byte nodes, CString keys by _mbscmp).
using HelpTopicMap = stlp::map<mfc::CString, int, stlp::CStringLess>;
using HelpTopics = stlp::TreeAbi<HelpTopicMap>;
// The "pers" texts by text key (ui_camp.h; 0x18-byte nodes).
using PersonTexts = stlp::TreeAbi<PersonTextMap>;
// The global map's arrival times by exit (CGlobalMap::arrivals_; 0x18-byte nodes).
using ArrivalMap = stlp::map<int, float>;
using Arrivals = stlp::TreeAbi<ArrivalMap>;
static_assert(sizeof(Commands::Node) == 0x18 && sizeof(HelpTopics::Node) == 0x18 && sizeof(PersonTexts::Node) == 0x18 &&
              sizeof(Arrivals::Node) == 0x18);

// The hero screen's item caches (ItemObjectCache, ui_item_info.h). Their constructors 00633F50 /
// 00633FF0 (S12e): map() (the header from the pool, empty), then the limit 50.
using HeroItemCache = ItemObjectCache;
HeroItemCache* __fastcall ConstructHeroItemCache(HeroItemCache* self);

// 004F4CD0: LinkPartHeader() as operator[]'s default value: the 0x50 bytes zeroed.
void __fastcall ZeroLinkHeader(LinkPartHeader* p);
// 005F8FC0: the model cache's destructor as the item screens' unwinding runs it: every cached model
// released (005EC010), then the map destroyed.
void __fastcall ReleaseModelCache(ItemCache::Tree* self);

} // namespace lib_trees
