// The exe's STLport map instantiations outside the connection (library_trees.h), their members
// registered at the library addresses (stlp_tree.h, stlp_tree_abi.h) so the standalone image has
// them; the game's forwarders keep calling these addresses. Several members exist more than once
// (the same body compiled into different objects): one body serves each. Checked by
// tests/verify/verify_library_trees.cpp.
#include "library_trees.h"

#include "ui_item_info.h"   // UiReleaseCachedModel

#include "ei/hook.h"

namespace lib_trees {

void __fastcall ZeroLinkHeader(LinkPartHeader* p) { std::memset(p, 0, sizeof(*p)); }

void __fastcall ReleaseModelCache(ItemCache::Tree* self) {
    for (ItemCache::Node* n = self->begin(); n != self->end(); n = ItemCache::Tree::Next(n))
        UiReleaseCachedModel(n->second.object);
    self->Destroy();
}

HeroItemCache* __fastcall ConstructHeroItemCache(HeroItemCache* self) {
    ItemCache::Construct(&self->map);
    self->limit = 50;
    return self;
}

} // namespace lib_trees

using namespace lib_trees;

// --- Members shared by every tree (node pointers and plain words only) ---------------------------
EI_REPLACE(0x00445030, stlp::TreeIteratorCopy);
EI_REPLACE(0x004827C0, stlp::TreeIteratorCopy);
EI_REPLACE(0x004F4E60, stlp::TreeIteratorCopy);
EI_REPLACE(0x004F5040, stlp::TreeIteratorCopy);
EI_REPLACE(0x005E0D10, stlp::TreeIteratorCopy);
EI_REPLACE(0x005F3160, stlp::TreeIteratorCopy);
EI_REPLACE(0x005F32C0, stlp::TreeIteratorCopy);
EI_REPLACE(0x005F9870, stlp::TreeIteratorCopy);
EI_REPLACE(0x00631770, stlp::TreeIteratorCopy);
EI_REPLACE(0x00445D00, stlp::TreeInsertResultConstruct);
EI_REPLACE(0x00483FB0, stlp::TreeInsertResultConstruct);
EI_REPLACE(0x004F6830, stlp::TreeInsertResultConstruct);
EI_REPLACE(0x004F6850, stlp::TreeInsertResultConstruct);
EI_REPLACE(0x005E1680, stlp::TreeInsertResultConstruct);
EI_REPLACE(0x005F45B0, stlp::TreeInsertResultConstruct);
EI_REPLACE(0x005F45D0, stlp::TreeInsertResultConstruct);
EI_REPLACE(0x005FBF30, stlp::TreeInsertResultConstruct);
EI_REPLACE(0x0060B720, stlp::TreeInsertResultConstruct);
EI_REPLACE(0x006325C0, stlp::TreeInsertResultConstruct);
EI_REPLACE(0x004452B0, stlp::TreeAllocProxyConstruct);
EI_REPLACE(0x00482D80, stlp::TreeAllocProxyConstruct);
EI_REPLACE(0x005F9C50, stlp::TreeAllocProxyConstruct);
EI_REPLACE(0x005F9D50, stlp::TreeAllocProxyConstruct);
EI_REPLACE(0x0060A100, stlp::TreeAllocProxyConstruct);
EI_REPLACE(0x005F3CD0, stlp::TreeWordPairConstruct);
EI_REPLACE(0x005F3CF0, stlp::TreeWordPairConstruct);

// --- The debug panel's entries (map<string, string>) ---------------------------------------------
EI_REPLACE(0x00445290, StringPairs::EmptyInitialize);
EI_REPLACE(0x00445140, StringPairs::FreeHeader);
EI_REPLACE(0x00445310, StringPairs::EraseSubtree);
EI_REPLACE(0x004456E0, StringPairs::LowerBound);
EI_REPLACE(0x00445620, StringPairs::Find);
EI_REPLACE(0x004450E0, StringPairs::Begin);
EI_REPLACE(0x00445BD0, StringPairs::CreateNode);
EI_REPLACE(0x00445870, StringPairs::InsertAt);
EI_REPLACE(0x00445A00, StringPairs::InsertUnique);
EI_REPLACE(0x00445360, StringPairs::InsertHint);
EI_REPLACE(0x004450B0, StringPairs::InsertHint);
EI_REPLACE(0x004450F0, StringPairs::EraseAt);
EI_REPLACE(0x00445010, StringPairs::EraseAt);
EI_REPLACE(0x00445170, StringPairs::MakeValue);
EI_REPLACE(0x00445040, StringPairs::DestroyValue);
EI_REPLACE(0x004457F0, StringPairs::DeletingDestroyValue);

// --- The .mob loader's tint lists (map<string, list<T>>) ------------------------------------------
EI_REPLACE(0x00482640, TintLists::ConstructWith);
EI_REPLACE(0x00481E60, TintLists::Destroy);
EI_REPLACE(0x00482730, TintLists::Clear);
EI_REPLACE(0x00482DA0, TintLists::DeallocateNodesFast);
EI_REPLACE(0x00482EA0, TintLists::EraseSubtree);
EI_REPLACE(0x004825A0, TintLists::LowerBound);
EI_REPLACE(0x00482720, TintLists::Begin);
EI_REPLACE(0x00483DF0, TintLists::CreateNode);
EI_REPLACE(0x004835D0, TintLists::InsertAt);
EI_REPLACE(0x00483760, TintLists::InsertUnique);
EI_REPLACE(0x00482EF0, TintLists::InsertHint);
EI_REPLACE(0x00482570, TintLists::InsertHint);
EI_REPLACE(0x004824B0, TintLists::DestroyValue);
EI_REPLACE(0x00483D40, TintLists::DeletingDestroyValue);

// --- A link mesh's part headers (map<string, LinkPartHeader>) -------------------------------------
EI_REPLACE(0x004F4F30, LinkHeaders::ConstructWith);
EI_REPLACE(0x004F5010, LinkHeaders::FreeHeader);
EI_REPLACE(0x004F53B0, LinkHeaders::DeallocateNodesFast);
EI_REPLACE(0x004F53F0, LinkHeaders::EraseSubtree);
EI_REPLACE(0x004F5F00, LinkHeaders::DestroyNode);
EI_REPLACE(0x004F4B90, LinkHeaders::Subscript);
EI_REPLACE(0x004F5710, LinkHeaders::LowerBound);
EI_REPLACE(0x004F5C90, LinkHeaders::Find);
EI_REPLACE(0x005197B0, LinkHeaders::Find);
EI_REPLACE(0x004F5700, LinkHeaders::Begin);
EI_REPLACE(0x004F65F0, LinkHeaders::CreateNode);
EI_REPLACE(0x004F5F30, LinkHeaders::InsertAt);
EI_REPLACE(0x004F60C0, LinkHeaders::InsertUnique);
EI_REPLACE(0x004F5440, LinkHeaders::InsertHint);
EI_REPLACE(0x004F4F00, LinkHeaders::InsertHint);
EI_REPLACE(0x004F4E70, LinkHeaders::DestroyValue);
EI_REPLACE(0x004F4CD0, ZeroLinkHeader);

// --- A link mesh's part ranges (map<string, LinkPartRanges>) --------------------------------------
EI_REPLACE(0x004F51B0, LinkRanges::ConstructBase);
EI_REPLACE(0x004F5190, LinkRanges::EmptyInitialize);
EI_REPLACE(0x004F50E0, LinkRanges::Clear);
EI_REPLACE(0x004F5140, LinkRanges::FreeHeader);
EI_REPLACE(0x004F5360, LinkRanges::DeallocateNodes);
EI_REPLACE(0x004F58C0, LinkRanges::EraseSubtree);
EI_REPLACE(0x004F4CE0, LinkRanges::Subscript);
EI_REPLACE(0x004F5D50, LinkRanges::LowerBound);
EI_REPLACE(0x004F5BD0, LinkRanges::Find);
EI_REPLACE(0x004F5080, LinkRanges::Begin);
EI_REPLACE(0x004F6710, LinkRanges::CreateNode);
EI_REPLACE(0x004F6270, LinkRanges::InsertAt);
EI_REPLACE(0x004F6400, LinkRanges::InsertUnique);
EI_REPLACE(0x004F5910, LinkRanges::InsertHint);
EI_REPLACE(0x004F5050, LinkRanges::InsertHint);
EI_REPLACE(0x004F5090, LinkRanges::EraseAt);
EI_REPLACE(0x004F4E40, LinkRanges::EraseAt);
EI_REPLACE(0x004F4EC0, LinkRanges::DestroyValue);

// --- The link loader's mesh cache (multimap<string, counted CLinkMesh*>) --------------------------
EI_REPLACE(0x004FB760, MeshCache::ConstructWith);
EI_REPLACE(0x004F6B30, MeshCache::Destroy);
EI_REPLACE(0x004FB580, MeshCache::Clear);
EI_REPLACE(0x004FBA10, MeshCache::DeallocateNodesFast);
EI_REPLACE(0x004FBD30, MeshCache::EraseSubtree);
EI_REPLACE(0x004FB5E0, MeshCache::EqualRange);
EI_REPLACE(0x004FC6E0, MeshCache::CreateNode);
EI_REPLACE(0x004FC4F0, MeshCache::InsertAt);
EI_REPLACE(0x004FBD80, MeshCache::InsertEqual);
EI_REPLACE(0x004F7A80, MeshCache::DestroyValue);
EI_REPLACE(0x004FC6A0, MeshCache::DeletingDestroyValue);

// --- The item screens' caches (map<string, {object, last use}>): two instantiations ---------------
EI_REPLACE(0x005F98F0, ItemCache::Construct);
EI_REPLACE(0x005F9F80, ItemCache::ConstructWith);
EI_REPLACE(0x00619630, ItemCache::ConstructWith);
EI_REPLACE(0x005FA420, ItemCache::ConstructBase);
EI_REPLACE(0x005FA630, ItemCache::ConstructBase);
EI_REPLACE(0x005FA400, ItemCache::EmptyInitialize);
EI_REPLACE(0x005FA610, ItemCache::EmptyInitialize);
EI_REPLACE(0x005FA060, ItemCache::FreeHeader);
EI_REPLACE(0x005FA090, ItemCache::FreeHeader);
EI_REPLACE(0x005FA5D0, ItemCache::DeallocateNodes);
EI_REPLACE(0x005F8FC0, ReleaseModelCache);
// The hero screen's model and texture caches (S12e).
EI_REPLACE(0x00633F50, ConstructHeroItemCache);
EI_REPLACE(0x00633FF0, ConstructHeroItemCache);
EI_REPLACE(0x00619770, ItemCache::AllocateNodes);   // the hero screen's texture cache header (S12e)
// The first (models: 005F3000 and its members).
EI_REPLACE(0x005F9690, ItemCache::Destroy);
EI_REPLACE(0x00634160, ItemCache::Destroy);
EI_REPLACE(0x005FAFF0, ItemCache::EraseSubtree);
EI_REPLACE(0x005FB8E0, ItemCache::DestroyNode);
EI_REPLACE(0x005F3000, ItemCache::Subscript);
EI_REPLACE(0x005F37F0, ItemCache::LowerBound);
EI_REPLACE(0x005F3730, ItemCache::Find);
EI_REPLACE(0x005F3380, ItemCache::Begin);
EI_REPLACE(0x005F4390, ItemCache::CreateNode);
EI_REPLACE(0x005F3D10, ItemCache::InsertAt);
EI_REPLACE(0x005F3EA0, ItemCache::InsertUnique);
EI_REPLACE(0x005F3470, ItemCache::InsertHint);
EI_REPLACE(0x005F3350, ItemCache::InsertHint);
EI_REPLACE(0x005F3390, ItemCache::EraseAt);
EI_REPLACE(0x005F3140, ItemCache::EraseAt);
EI_REPLACE(0x005F32D0, ItemCache::DestroyValue);
EI_REPLACE(0x005FBCF0, ItemCache::DeletingDestroyValue);
// The second (textures: 005F3170 and its members).
EI_REPLACE(0x005F9730, ItemCache::Destroy);
EI_REPLACE(0x00634200, ItemCache::Destroy);
EI_REPLACE(0x005FB040, ItemCache::EraseSubtree);
EI_REPLACE(0x005FB910, ItemCache::DestroyNode);
EI_REPLACE(0x005F3170, ItemCache::Subscript);
EI_REPLACE(0x005F3C10, ItemCache::LowerBound);
EI_REPLACE(0x005F3B50, ItemCache::Find);
EI_REPLACE(0x005F3410, ItemCache::Begin);
EI_REPLACE(0x005F44A0, ItemCache::CreateNode);
EI_REPLACE(0x005F4050, ItemCache::InsertAt);
EI_REPLACE(0x005F41E0, ItemCache::InsertUnique);
EI_REPLACE(0x005F3890, ItemCache::InsertHint);
EI_REPLACE(0x005F33E0, ItemCache::InsertHint);
EI_REPLACE(0x005F3420, ItemCache::EraseAt);
EI_REPLACE(0x005F32A0, ItemCache::EraseAt);
EI_REPLACE(0x005F3310, ItemCache::DestroyValue);
EI_REPLACE(0x005FBD10, ItemCache::DeletingDestroyValue);

// --- The console's command maps (map<CString, handler>) -------------------------------------------
EI_REPLACE(0x005E0A90, Commands::Construct);
EI_REPLACE(0x005DC080, Commands::Destroy);
EI_REPLACE(0x005E0CE0, Commands::FreeHeader);
EI_REPLACE(0x005E0D70, Commands::DeallocateNodesFast);
EI_REPLACE(0x005E0E40, Commands::EraseSubtree);
EI_REPLACE(0x005E0B20, Commands::Subscript);
EI_REPLACE(0x005E11A0, Commands::LowerBound);
EI_REPLACE(0x005E0C90, Commands::LowerBound);
EI_REPLACE(0x005E1670, Commands::Begin);
EI_REPLACE(0x005E1580, Commands::CreateNode);
EI_REPLACE(0x005E11F0, Commands::InsertAt);
EI_REPLACE(0x005E13D0, Commands::InsertUnique);
EI_REPLACE(0x005E0E90, Commands::InsertHint);
EI_REPLACE(0x005E0C60, Commands::InsertHint);
EI_REPLACE(0x005E0D20, Commands::MakeValue);
EI_REPLACE(0x005E0BF0, Commands::DestroyValue);

// --- The interface's help topics (map<CString, int>) ----------------------------------------------
EI_REPLACE(0x005F9D90, HelpTopics::ConstructBase);
EI_REPLACE(0x005F9D70, HelpTopics::EmptyInitialize);
EI_REPLACE(0x005F48F0, HelpTopics::Destroy);
EI_REPLACE(0x005F9840, HelpTopics::FreeHeader);
EI_REPLACE(0x005FA820, HelpTopics::DeallocateNodesFast);
EI_REPLACE(0x005FAB30, HelpTopics::EraseSubtree);
EI_REPLACE(0x005FB8B0, HelpTopics::DestroyNode);
EI_REPLACE(0x0065A770, HelpTopics::Subscript);
EI_REPLACE(0x005FAF30, HelpTopics::LowerBound);
EI_REPLACE(0x005FAEA0, HelpTopics::Find);
EI_REPLACE(0x005FAE90, HelpTopics::Begin);
EI_REPLACE(0x005FBE40, HelpTopics::CreateNode);
EI_REPLACE(0x005FB940, HelpTopics::InsertAt);
EI_REPLACE(0x005FBB20, HelpTopics::InsertUnique);
EI_REPLACE(0x005FAB80, HelpTopics::InsertHint);
EI_REPLACE(0x005F9810, HelpTopics::InsertHint);

// --- The "pers" texts (map<int, CString>) ---------------------------------------------------------
EI_REPLACE(0x005F8F30, PersonTexts::Construct);
EI_REPLACE(0x005F5FE0, PersonTexts::Destroy);
EI_REPLACE(0x005F98C0, PersonTexts::FreeHeader);
EI_REPLACE(0x005FA860, PersonTexts::DeallocateNodesFast);
EI_REPLACE(0x005FAFA0, PersonTexts::EraseSubtree);
EI_REPLACE(0x0060A620, PersonTexts::LowerBound);
EI_REPLACE(0x0060A5C0, PersonTexts::Find);
EI_REPLACE(0x0060B5F0, PersonTexts::Begin);
EI_REPLACE(0x0060B500, PersonTexts::CreateNode);
EI_REPLACE(0x0060AEE0, PersonTexts::InsertAt);
EI_REPLACE(0x0060B0A0, PersonTexts::InsertUnique);
EI_REPLACE(0x0060A320, PersonTexts::InsertHint);
EI_REPLACE(0x00609E70, PersonTexts::InsertHint);
EI_REPLACE(0x005FBE30, PersonTexts::DestroyValue);
EI_REPLACE(0x006062B0, PersonTexts::DestroyValue);
EI_REPLACE(0x005FBCD0, PersonTexts::DeletingDestroyValue);
EI_REPLACE(0x0060AC00, PersonTexts::DeletingDestroyValue);

// --- The global map's arrival times (map<int, float>) ---------------------------------------------
EI_REPLACE(0x005FA100, Arrivals::ConstructBase);
EI_REPLACE(0x005FA0E0, Arrivals::EmptyInitialize);
EI_REPLACE(0x005F6AD0, Arrivals::Destroy);
EI_REPLACE(0x00631710, Arrivals::Clear);
EI_REPLACE(0x005F9980, Arrivals::FreeHeader);
EI_REPLACE(0x005FA8A0, Arrivals::DeallocateNodesFast);
EI_REPLACE(0x006317A0, Arrivals::DeallocateNodes);
EI_REPLACE(0x005FB7D0, Arrivals::EraseSubtree);
EI_REPLACE(0x00631C80, Arrivals::LowerBound);
EI_REPLACE(0x00631C20, Arrivals::Find);
EI_REPLACE(0x006325B0, Arrivals::Begin);
EI_REPLACE(0x00632530, Arrivals::CreateNode);
EI_REPLACE(0x00632170, Arrivals::InsertAt);
EI_REPLACE(0x00632330, Arrivals::InsertUnique);
EI_REPLACE(0x00631980, Arrivals::InsertHint);
EI_REPLACE(0x006316E0, Arrivals::InsertHint);
EI_REPLACE(0x00631780, Arrivals::MakeValue);
