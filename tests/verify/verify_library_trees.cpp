// Checks for the library's STLport maps (src/game/library_trees.cpp): the per-call wrappers of
// stlp_tree_verify.h, and a start-up check that runs each instantiation's registered originals
// (insert, insert with a hint, operator[], erase at a position, clear, destroy) against ours: two
// trees built side by side from one random script, after every step the same shape, colours, keys
// and values (strings and CStrings by their text, lists by their elements, null references).
#include "verify.h"

#include "stlp_tree_verify.h"

#include "../../src/game/library_trees.h"
#include "../../src/lib/mfc_string.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>

namespace {

using stlp_tree_verify::OriginalOf;
using stlp_tree_verify::Originals;
using stlp_tree_verify::Script;
using stlp_tree_verify::SharedCheck;
using stlp_tree_verify::TreeCheck;
using namespace lib_trees;

template <std::uintptr_t A> using StringPairsCheck = TreeCheck<A, StringPairs>;
template <std::uintptr_t A> using TintListsCheck = TreeCheck<A, TintLists>;
template <std::uintptr_t A> using LinkHeadersCheck = TreeCheck<A, LinkHeaders>;
template <std::uintptr_t A> using LinkRangesCheck = TreeCheck<A, LinkRanges>;
template <std::uintptr_t A> using MeshCacheCheck = TreeCheck<A, MeshCache>;
template <std::uintptr_t A> using ItemCacheCheck = TreeCheck<A, ItemCache>;
template <std::uintptr_t A> using CommandsCheck = TreeCheck<A, Commands>;
template <std::uintptr_t A> using HelpTopicsCheck = TreeCheck<A, HelpTopics>;
template <std::uintptr_t A> using PersonTextsCheck = TreeCheck<A, PersonTexts>;
template <std::uintptr_t A> using ArrivalsCheck = TreeCheck<A, Arrivals>;

// The two plain functions.
template <std::uintptr_t A>
struct OwnCheck {
    static inline void* original = nullptr;

    // Pure: the original on a copy, ours on the header; the same bytes.
    static void __fastcall ZeroLinkHeader(LinkPartHeader* p) {
        LinkPartHeader theirs;
        std::memcpy(&theirs, p, sizeof(theirs));
        reinterpret_cast<decltype(&lib_trees::ZeroLinkHeader)>(original)(&theirs);
        lib_trees::ZeroLinkHeader(p);
        ei::verify::Report(stlp_verify::Name<A>("LinkPartHeader::LinkPartHeader"),
                           std::memcmp(&theirs, p, sizeof(theirs)) == 0, "");
    }
    // Releases the models: ours alone.
    static void __fastcall ReleaseModelCache(ItemCache::Tree* self) {
        lib_trees::ReleaseModelCache(self);
        ei::verify::Report(stlp_verify::Name<A>("item model cache ~map"), true, "");
    }
};

// --- Start-up check -----------------------------------------------------------------------------

// Keys and values from small sets so inserts meet existing keys and erases find them. The string
// texts include high bytes (the order compares unsigned bytes); the CString texts stay ASCII
// (_mbscmp).
const char* const kTexts[] = {"",     "a",     "A",     "ab",    "abc",   "b",    "ba",   "head",
                              "Head", "torso", "arm.l", "arm.r", "leg01", "leg1", "leg10", "z",
                              "~",    "tail",  "wing",  "a b c", "\x80hi", "\xFFz", "\x7F"};
constexpr std::uint32_t kAsciiTexts = 20;
constexpr std::uint32_t kAllTexts = sizeof(kTexts) / sizeof(kTexts[0]);

struct StringPart {
    using T = stlp::string;
    static void Make(T* p, std::uint32_t r) { new (p) T(kTexts[r % kAllTexts]); }
    static void Destroy(T* p) { p->~T(); }
    static bool Same(const T& a, const T& b) { return a == b; }
    static void Assign(T* to, const T& from) { to->AssignFrom(from); }
};

struct CStringPart {
    using T = mfc::CString;
    static void Make(T* p, std::uint32_t r) { new (p) T(kTexts[r % kAsciiTexts]); }
    static void Destroy(T* p) { p->~CString(); }
    static bool Same(const T& a, const T& b) {
        return a.GetLength() == b.GetLength() && std::strcmp(static_cast<const char*>(a), static_cast<const char*>(b)) == 0;
    }
    static void Assign(T* to, const T& from) { *to = from; }
};

// Signed keys: negative and high-bit ones so the signed and unsigned orders differ.
struct IntPart {
    using T = int;
    static void Make(T* p, std::uint32_t r) { *p = static_cast<int>((r % 48) * 0x05000000u + (r & 1)); }
    static void Destroy(T*) {}
    static bool Same(const T& a, const T& b) { return a == b; }
    static void Assign(T* to, const T& from) { *to = from; }
};

// Plain values as bytes.
template <typename V>
struct BytesPart {
    using T = V;
    static void Make(T* p, std::uint32_t r) {
        auto* b = reinterpret_cast<unsigned char*>(p);
        for (std::size_t i = 0; i < sizeof(T); ++i) {
            r = r * 1103515245u + 12345u;
            b[i] = static_cast<unsigned char>(r >> 16);
        }
    }
    static void Destroy(T*) {}
    static bool Same(const T& a, const T& b) { return std::memcmp(&a, &b, sizeof(T)) == 0; }
    static void Assign(T* to, const T& from) { std::memcpy(to, &from, sizeof(T)); }
};

// A list of up to three 16-byte records.
struct TintPart {
    using T = TintList;
    static void Make(T* p, std::uint32_t r) {
        p->Construct();
        for (std::uint32_t i = 0; i < r % 4; ++i) {
            stlp::Opaque<16> record;
            BytesPart<stlp::Opaque<16>>::Make(&record, r + i);
            p->PushBack(record);
        }
    }
    static void Destroy(T* p) { p->Release(); }
    static bool Same(const T& a, const T& b) {
        auto* x = a.First();
        auto* y = b.First();
        for (; x != a.End() && y != b.End(); x = x->next, y = y->next)
            if (std::memcmp(&x->data, &y->data, sizeof(x->data)) != 0) return false;
        return x == a.End() && y == b.End();
    }
    static void Assign(T*, const T&) {}
};

// Null references (no object touched).
struct NullRefPart {
    using T = RefPtr;
    static void Make(T* p, std::uint32_t) { p->ptr = nullptr; }
    static void Destroy(T*) {}
    static bool Same(const T& a, const T& b) { return a.ptr == b.ptr; }
    static void Assign(T*, const T&) {}
};

template <typename Abi, typename KeyPart, typename ValuePart, bool kSubscript = true>
struct Traits {
    using Value = typename Abi::Value;
    static constexpr bool subscript = kSubscript;
    static void Make(Value* v, std::uint32_t r) {
        KeyPart::Make(&v->first, r);
        ValuePart::Make(&v->second, r >> 6);
    }
    static void Destroy(Value* v) {
        ValuePart::Destroy(&v->second);
        KeyPart::Destroy(&v->first);
    }
    static bool Same(const Value& a, const Value& b) {
        return KeyPart::Same(a.first, b.first) && ValuePart::Same(a.second, b.second);
    }
    static void Assign(typename Abi::V* slot, const Value& v) { ValuePart::Assign(slot, v.second); }
};

// The item caches' two copies of each member (the model cache's, the texture cache's).
template <std::uintptr_t S, std::uintptr_t U, std::uintptr_t H, std::uintptr_t E, std::uintptr_t D>
Originals<ItemCache> ItemCacheSet() {
    Originals<ItemCache> o;
    o.subscript = OriginalOf<S, ItemCache>(&ItemCache::Subscript);
    o.insertUnique = OriginalOf<U, ItemCache>(&ItemCache::InsertUnique);
    o.insertHint = OriginalOf<H, ItemCache>(&ItemCache::InsertHint);
    o.eraseAt = OriginalOf<E, ItemCache>(&ItemCache::EraseAt);
    o.destroy = OriginalOf<D, ItemCache>(&ItemCache::Destroy);
    return o;
}

void LibraryTreesStartupCheck() {
    for (std::uint32_t seed = 1; seed <= 4; ++seed) {
        Originals<StringPairs> sp;
        sp.insertUnique = OriginalOf<0x00445A00, StringPairs>(&StringPairs::InsertUnique);
        sp.insertHint = OriginalOf<0x00445360, StringPairs>(&StringPairs::InsertHint);
        sp.eraseAt = OriginalOf<0x004450F0, StringPairs>(&StringPairs::EraseAt);
        Script<StringPairs, Traits<StringPairs, StringPart, StringPart>>("lib_trees startup: string pairs", sp, seed);

        Originals<TintLists> tl;
        tl.insertUnique = OriginalOf<0x00483760, TintLists>(&TintLists::InsertUnique);
        tl.insertHint = OriginalOf<0x00482EF0, TintLists>(&TintLists::InsertHint);
        tl.clear = OriginalOf<0x00482730, TintLists>(&TintLists::Clear);
        tl.destroy = OriginalOf<0x00481E60, TintLists>(&TintLists::Destroy);
        Script<TintLists, Traits<TintLists, StringPart, TintPart, false>>("lib_trees startup: tint lists", tl, seed);

        Originals<LinkHeaders> lh;
        lh.subscript = OriginalOf<0x004F4B90, LinkHeaders>(&LinkHeaders::Subscript);
        lh.insertUnique = OriginalOf<0x004F60C0, LinkHeaders>(&LinkHeaders::InsertUnique);
        lh.insertHint = OriginalOf<0x004F5440, LinkHeaders>(&LinkHeaders::InsertHint);
        Script<LinkHeaders, Traits<LinkHeaders, StringPart, BytesPart<LinkPartHeader>>>("lib_trees startup: link headers", lh,
                                                                                          seed);

        Originals<LinkRanges> lr;
        lr.subscript = OriginalOf<0x004F4CE0, LinkRanges>(&LinkRanges::Subscript);
        lr.insertUnique = OriginalOf<0x004F6400, LinkRanges>(&LinkRanges::InsertUnique);
        lr.insertHint = OriginalOf<0x004F5910, LinkRanges>(&LinkRanges::InsertHint);
        lr.eraseAt = OriginalOf<0x004F5090, LinkRanges>(&LinkRanges::EraseAt);
        lr.clear = OriginalOf<0x004F50E0, LinkRanges>(&LinkRanges::Clear);
        Script<LinkRanges, Traits<LinkRanges, StringPart, BytesPart<LinkPartRanges>>>("lib_trees startup: link ranges", lr, seed);

        Originals<MeshCache> mc;
        mc.insertEqual = OriginalOf<0x004FBD80, MeshCache>(&MeshCache::InsertEqual);
        mc.clear = OriginalOf<0x004FB580, MeshCache>(&MeshCache::Clear);
        mc.destroy = OriginalOf<0x004F6B30, MeshCache>(&MeshCache::Destroy);
        Script<MeshCache, Traits<MeshCache, StringPart, NullRefPart, false>>("lib_trees startup: mesh cache", mc, seed, true);

        using ItemTraits = Traits<ItemCache, StringPart, BytesPart<ItemCacheEntry>>;
        Script<ItemCache, ItemTraits>("lib_trees startup: model cache",
                                      ItemCacheSet<0x005F3000, 0x005F3EA0, 0x005F3470, 0x005F3390, 0x005F9690>(), seed);
        Script<ItemCache, ItemTraits>("lib_trees startup: texture cache",
                                      ItemCacheSet<0x005F3170, 0x005F41E0, 0x005F3890, 0x005F3420, 0x005F9730>(), seed);

        Originals<Commands> cm;
        cm.subscript = OriginalOf<0x005E0B20, Commands>(&Commands::Subscript);
        cm.insertUnique = OriginalOf<0x005E13D0, Commands>(&Commands::InsertUnique);
        cm.insertHint = OriginalOf<0x005E0E90, Commands>(&Commands::InsertHint);
        cm.destroy = OriginalOf<0x005DC080, Commands>(&Commands::Destroy);
        Script<Commands, Traits<Commands, CStringPart, BytesPart<console::CommandHandler>>>("lib_trees startup: commands", cm,
                                                                                             seed);

        Originals<HelpTopics> ht;
        ht.subscript = OriginalOf<0x0065A770, HelpTopics>(&HelpTopics::Subscript);
        ht.insertUnique = OriginalOf<0x005FBB20, HelpTopics>(&HelpTopics::InsertUnique);
        ht.insertHint = OriginalOf<0x005FAB80, HelpTopics>(&HelpTopics::InsertHint);
        ht.destroy = OriginalOf<0x005F48F0, HelpTopics>(&HelpTopics::Destroy);
        Script<HelpTopics, Traits<HelpTopics, CStringPart, IntPart>>("lib_trees startup: help topics", ht, seed);

        Originals<PersonTexts> pt;
        pt.insertUnique = OriginalOf<0x0060B0A0, PersonTexts>(&PersonTexts::InsertUnique);
        pt.insertHint = OriginalOf<0x0060A320, PersonTexts>(&PersonTexts::InsertHint);
        pt.destroy = OriginalOf<0x005F5FE0, PersonTexts>(&PersonTexts::Destroy);
        Script<PersonTexts, Traits<PersonTexts, IntPart, CStringPart>>("lib_trees startup: person texts", pt, seed);

        Originals<Arrivals> ar;
        ar.insertUnique = OriginalOf<0x00632330, Arrivals>(&Arrivals::InsertUnique);
        ar.insertHint = OriginalOf<0x00631980, Arrivals>(&Arrivals::InsertHint);
        ar.clear = OriginalOf<0x00631710, Arrivals>(&Arrivals::Clear);
        ar.destroy = OriginalOf<0x005F6AD0, Arrivals>(&Arrivals::Destroy);
        Script<Arrivals, Traits<Arrivals, IntPart, BytesPart<float>>>("lib_trees startup: arrivals", ar, seed);
    }
}

} // namespace

EI_STARTUP_CHECK(LibraryTreesStartupCheck);

// --- Registrations (stolen bytes: whole instructions covering five bytes; 0 where they hold a
// relative jump or call) -------------------------------------------------------------------------
EI_VERIFY(0x00445030, SharedCheck<0x00445030>::IteratorCopy, 6, SharedCheck<0x00445030>::original);
EI_VERIFY(0x004827C0, SharedCheck<0x004827C0>::IteratorCopy, 6, SharedCheck<0x004827C0>::original);
EI_VERIFY(0x004F4E60, SharedCheck<0x004F4E60>::IteratorCopy, 6, SharedCheck<0x004F4E60>::original);
EI_VERIFY(0x004F5040, SharedCheck<0x004F5040>::IteratorCopy, 6, SharedCheck<0x004F5040>::original);
EI_VERIFY(0x005E0D10, SharedCheck<0x005E0D10>::IteratorCopy, 6, SharedCheck<0x005E0D10>::original);
EI_VERIFY(0x005F3160, SharedCheck<0x005F3160>::IteratorCopy, 6, SharedCheck<0x005F3160>::original);
EI_VERIFY(0x005F32C0, SharedCheck<0x005F32C0>::IteratorCopy, 6, SharedCheck<0x005F32C0>::original);
EI_VERIFY(0x005F9870, SharedCheck<0x005F9870>::IteratorCopy, 6, SharedCheck<0x005F9870>::original);
EI_VERIFY(0x00631770, SharedCheck<0x00631770>::IteratorCopy, 6, SharedCheck<0x00631770>::original);
EI_VERIFY(0x00445D00, SharedCheck<0x00445D00>::InsertResult, 6, SharedCheck<0x00445D00>::original);
EI_VERIFY(0x00483FB0, SharedCheck<0x00483FB0>::InsertResult, 6, SharedCheck<0x00483FB0>::original);
EI_VERIFY(0x004F6830, SharedCheck<0x004F6830>::InsertResult, 6, SharedCheck<0x004F6830>::original);
EI_VERIFY(0x004F6850, SharedCheck<0x004F6850>::InsertResult, 6, SharedCheck<0x004F6850>::original);
EI_VERIFY(0x005E1680, SharedCheck<0x005E1680>::InsertResult, 6, SharedCheck<0x005E1680>::original);
EI_VERIFY(0x005F45B0, SharedCheck<0x005F45B0>::InsertResult, 6, SharedCheck<0x005F45B0>::original);
EI_VERIFY(0x005F45D0, SharedCheck<0x005F45D0>::InsertResult, 6, SharedCheck<0x005F45D0>::original);
EI_VERIFY(0x005FBF30, SharedCheck<0x005FBF30>::InsertResult, 6, SharedCheck<0x005FBF30>::original);
EI_VERIFY(0x0060B720, SharedCheck<0x0060B720>::InsertResult, 6, SharedCheck<0x0060B720>::original);
EI_VERIFY(0x006325C0, SharedCheck<0x006325C0>::InsertResult, 6, SharedCheck<0x006325C0>::original);
EI_VERIFY(0x004452B0, SharedCheck<0x004452B0>::AllocProxy, 6, SharedCheck<0x004452B0>::original);
EI_VERIFY(0x00482D80, SharedCheck<0x00482D80>::AllocProxy, 6, SharedCheck<0x00482D80>::original);
EI_VERIFY(0x005F9C50, SharedCheck<0x005F9C50>::AllocProxy, 6, SharedCheck<0x005F9C50>::original);
EI_VERIFY(0x005F9D50, SharedCheck<0x005F9D50>::AllocProxy, 6, SharedCheck<0x005F9D50>::original);
EI_VERIFY(0x0060A100, SharedCheck<0x0060A100>::AllocProxy, 6, SharedCheck<0x0060A100>::original);
EI_VERIFY(0x005F3CD0, SharedCheck<0x005F3CD0>::WordPair, 6, SharedCheck<0x005F3CD0>::original);
EI_VERIFY(0x005F3CF0, SharedCheck<0x005F3CF0>::WordPair, 6, SharedCheck<0x005F3CF0>::original);
EI_VERIFY(0x00445290, StringPairsCheck<0x00445290>::EmptyInitialize, 5, StringPairsCheck<0x00445290>::original);
EI_VERIFY(0x00445140, StringPairsCheck<0x00445140>::FreeHeader, 0, StringPairsCheck<0x00445140>::original);
EI_VERIFY(0x00445310, StringPairsCheck<0x00445310>::EraseSubtree, 6, StringPairsCheck<0x00445310>::original);
EI_VERIFY(0x004456E0, StringPairsCheck<0x004456E0>::LowerBound, 5, StringPairsCheck<0x004456E0>::original);
EI_VERIFY(0x00445620, StringPairsCheck<0x00445620>::Find, 8, StringPairsCheck<0x00445620>::original);
EI_VERIFY(0x004450E0, StringPairsCheck<0x004450E0>::Begin, 5, StringPairsCheck<0x004450E0>::original);
EI_VERIFY(0x00445BD0, StringPairsCheck<0x00445BD0>::CreateNode, 5, StringPairsCheck<0x00445BD0>::original);
EI_VERIFY(0x00445870, StringPairsCheck<0x00445870>::InsertAt, 5, StringPairsCheck<0x00445870>::original);
EI_VERIFY(0x00445A00, StringPairsCheck<0x00445A00>::InsertUnique, 8, StringPairsCheck<0x00445A00>::original);
EI_VERIFY(0x00445360, StringPairsCheck<0x00445360>::InsertHint, 5, StringPairsCheck<0x00445360>::original);
EI_VERIFY(0x004450B0, StringPairsCheck<0x004450B0>::InsertHint, 5, StringPairsCheck<0x004450B0>::original);
EI_VERIFY(0x004450F0, StringPairsCheck<0x004450F0>::EraseAt, 6, StringPairsCheck<0x004450F0>::original);
EI_VERIFY(0x00445010, StringPairsCheck<0x00445010>::EraseAt, 5, StringPairsCheck<0x00445010>::original);
EI_VERIFY(0x00445170, StringPairsCheck<0x00445170>::MakeValue, 7, StringPairsCheck<0x00445170>::original);
EI_VERIFY(0x00445040, StringPairsCheck<0x00445040>::DestroyValue, 6, StringPairsCheck<0x00445040>::original);
EI_VERIFY(0x004457F0, StringPairsCheck<0x004457F0>::DeletingDestroyValue, 0, StringPairsCheck<0x004457F0>::original);
EI_VERIFY(0x00482640, TintListsCheck<0x00482640>::ConstructWith, 7, TintListsCheck<0x00482640>::original);
EI_VERIFY(0x00481E60, TintListsCheck<0x00481E60>::Destroy, 7, TintListsCheck<0x00481E60>::original);
EI_VERIFY(0x00482730, TintListsCheck<0x00482730>::Clear, 6, TintListsCheck<0x00482730>::original);
EI_VERIFY(0x00482DA0, TintListsCheck<0x00482DA0>::DeallocateNodesFast, 0, TintListsCheck<0x00482DA0>::original);
EI_VERIFY(0x00482EA0, TintListsCheck<0x00482EA0>::EraseSubtree, 6, TintListsCheck<0x00482EA0>::original);
EI_VERIFY(0x004825A0, TintListsCheck<0x004825A0>::LowerBound, 5, TintListsCheck<0x004825A0>::original);
EI_VERIFY(0x00482720, TintListsCheck<0x00482720>::Begin, 5, TintListsCheck<0x00482720>::original);
EI_VERIFY(0x00483DF0, TintListsCheck<0x00483DF0>::CreateNode, 5, TintListsCheck<0x00483DF0>::original);
EI_VERIFY(0x004835D0, TintListsCheck<0x004835D0>::InsertAt, 5, TintListsCheck<0x004835D0>::original);
EI_VERIFY(0x00483760, TintListsCheck<0x00483760>::InsertUnique, 8, TintListsCheck<0x00483760>::original);
EI_VERIFY(0x00482EF0, TintListsCheck<0x00482EF0>::InsertHint, 5, TintListsCheck<0x00482EF0>::original);
EI_VERIFY(0x00482570, TintListsCheck<0x00482570>::InsertHint, 5, TintListsCheck<0x00482570>::original);
EI_VERIFY(0x004824B0, TintListsCheck<0x004824B0>::DestroyValue, 7, TintListsCheck<0x004824B0>::original);
EI_VERIFY(0x00483D40, TintListsCheck<0x00483D40>::DeletingDestroyValue, 0, TintListsCheck<0x00483D40>::original);
EI_VERIFY(0x004F4F30, LinkHeadersCheck<0x004F4F30>::ConstructWith, 7, LinkHeadersCheck<0x004F4F30>::original);
EI_VERIFY(0x004F5010, LinkHeadersCheck<0x004F5010>::FreeHeader, 0, LinkHeadersCheck<0x004F5010>::original);
EI_VERIFY(0x004F53B0, LinkHeadersCheck<0x004F53B0>::DeallocateNodesFast, 0, LinkHeadersCheck<0x004F53B0>::original);
EI_VERIFY(0x004F53F0, LinkHeadersCheck<0x004F53F0>::EraseSubtree, 6, LinkHeadersCheck<0x004F53F0>::original);
EI_VERIFY(0x004F5F00, LinkHeadersCheck<0x004F5F00>::DestroyNode, 5, LinkHeadersCheck<0x004F5F00>::original);
EI_VERIFY(0x004F4B90, LinkHeadersCheck<0x004F4B90>::Subscript, 6, LinkHeadersCheck<0x004F4B90>::original);
EI_VERIFY(0x004F5710, LinkHeadersCheck<0x004F5710>::LowerBound, 5, LinkHeadersCheck<0x004F5710>::original);
EI_VERIFY(0x004F5C90, LinkHeadersCheck<0x004F5C90>::Find, 8, LinkHeadersCheck<0x004F5C90>::original);
EI_VERIFY(0x005197B0, LinkHeadersCheck<0x005197B0>::Find, 8, LinkHeadersCheck<0x005197B0>::original);
EI_VERIFY(0x004F5700, LinkHeadersCheck<0x004F5700>::Begin, 5, LinkHeadersCheck<0x004F5700>::original);
EI_VERIFY(0x004F65F0, LinkHeadersCheck<0x004F65F0>::CreateNode, 5, LinkHeadersCheck<0x004F65F0>::original);
EI_VERIFY(0x004F5F30, LinkHeadersCheck<0x004F5F30>::InsertAt, 5, LinkHeadersCheck<0x004F5F30>::original);
EI_VERIFY(0x004F60C0, LinkHeadersCheck<0x004F60C0>::InsertUnique, 8, LinkHeadersCheck<0x004F60C0>::original);
EI_VERIFY(0x004F5440, LinkHeadersCheck<0x004F5440>::InsertHint, 5, LinkHeadersCheck<0x004F5440>::original);
EI_VERIFY(0x004F4F00, LinkHeadersCheck<0x004F4F00>::InsertHint, 5, LinkHeadersCheck<0x004F4F00>::original);
EI_VERIFY(0x004F4E70, LinkHeadersCheck<0x004F4E70>::DestroyValue, 8, LinkHeadersCheck<0x004F4E70>::original);
EI_VERIFY(0x004F4CD0, OwnCheck<0x004F4CD0>::ZeroLinkHeader, 8, OwnCheck<0x004F4CD0>::original);
EI_VERIFY(0x004F51B0, LinkRangesCheck<0x004F51B0>::ConstructBase, 7, LinkRangesCheck<0x004F51B0>::original);
EI_VERIFY(0x004F5190, LinkRangesCheck<0x004F5190>::EmptyInitialize, 5, LinkRangesCheck<0x004F5190>::original);
EI_VERIFY(0x004F50E0, LinkRangesCheck<0x004F50E0>::Clear, 6, LinkRangesCheck<0x004F50E0>::original);
EI_VERIFY(0x004F5140, LinkRangesCheck<0x004F5140>::FreeHeader, 0, LinkRangesCheck<0x004F5140>::original);
EI_VERIFY(0x004F5360, LinkRangesCheck<0x004F5360>::DeallocateNodes, 6, LinkRangesCheck<0x004F5360>::original);
EI_VERIFY(0x004F58C0, LinkRangesCheck<0x004F58C0>::EraseSubtree, 6, LinkRangesCheck<0x004F58C0>::original);
EI_VERIFY(0x004F4CE0, LinkRangesCheck<0x004F4CE0>::Subscript, 7, LinkRangesCheck<0x004F4CE0>::original);
EI_VERIFY(0x004F5D50, LinkRangesCheck<0x004F5D50>::LowerBound, 5, LinkRangesCheck<0x004F5D50>::original);
EI_VERIFY(0x004F5BD0, LinkRangesCheck<0x004F5BD0>::Find, 8, LinkRangesCheck<0x004F5BD0>::original);
EI_VERIFY(0x004F5080, LinkRangesCheck<0x004F5080>::Begin, 5, LinkRangesCheck<0x004F5080>::original);
EI_VERIFY(0x004F6710, LinkRangesCheck<0x004F6710>::CreateNode, 5, LinkRangesCheck<0x004F6710>::original);
EI_VERIFY(0x004F6270, LinkRangesCheck<0x004F6270>::InsertAt, 5, LinkRangesCheck<0x004F6270>::original);
EI_VERIFY(0x004F6400, LinkRangesCheck<0x004F6400>::InsertUnique, 8, LinkRangesCheck<0x004F6400>::original);
EI_VERIFY(0x004F5910, LinkRangesCheck<0x004F5910>::InsertHint, 5, LinkRangesCheck<0x004F5910>::original);
EI_VERIFY(0x004F5050, LinkRangesCheck<0x004F5050>::InsertHint, 5, LinkRangesCheck<0x004F5050>::original);
EI_VERIFY(0x004F5090, LinkRangesCheck<0x004F5090>::EraseAt, 6, LinkRangesCheck<0x004F5090>::original);
EI_VERIFY(0x004F4E40, LinkRangesCheck<0x004F4E40>::EraseAt, 5, LinkRangesCheck<0x004F4E40>::original);
EI_VERIFY(0x004F4EC0, LinkRangesCheck<0x004F4EC0>::DestroyValue, 5, LinkRangesCheck<0x004F4EC0>::original);
EI_VERIFY(0x004FB760, MeshCacheCheck<0x004FB760>::ConstructWith, 7, MeshCacheCheck<0x004FB760>::original);
EI_VERIFY(0x004F6B30, MeshCacheCheck<0x004F6B30>::Destroy, 7, MeshCacheCheck<0x004F6B30>::original);
EI_VERIFY(0x004FB580, MeshCacheCheck<0x004FB580>::Clear, 6, MeshCacheCheck<0x004FB580>::original);
EI_VERIFY(0x004FBA10, MeshCacheCheck<0x004FBA10>::DeallocateNodesFast, 0, MeshCacheCheck<0x004FBA10>::original);
EI_VERIFY(0x004FBD30, MeshCacheCheck<0x004FBD30>::EraseSubtree, 6, MeshCacheCheck<0x004FBD30>::original);
EI_VERIFY(0x004FB5E0, MeshCacheCheck<0x004FB5E0>::EqualRange, 5, MeshCacheCheck<0x004FB5E0>::original);
EI_VERIFY(0x004FC6E0, MeshCacheCheck<0x004FC6E0>::CreateNode, 5, MeshCacheCheck<0x004FC6E0>::original);
EI_VERIFY(0x004FC4F0, MeshCacheCheck<0x004FC4F0>::InsertAt, 5, MeshCacheCheck<0x004FC4F0>::original);
EI_VERIFY(0x004FBD80, MeshCacheCheck<0x004FBD80>::InsertEqual, 5, MeshCacheCheck<0x004FBD80>::original);
EI_VERIFY(0x004F7A80, MeshCacheCheck<0x004F7A80>::DestroyValue, 7, MeshCacheCheck<0x004F7A80>::original);
EI_VERIFY(0x004FC6A0, MeshCacheCheck<0x004FC6A0>::DeletingDestroyValue, 0, MeshCacheCheck<0x004FC6A0>::original);
EI_VERIFY(0x005F98F0, ItemCacheCheck<0x005F98F0>::Construct, 7, ItemCacheCheck<0x005F98F0>::original);
EI_VERIFY(0x005F9F80, ItemCacheCheck<0x005F9F80>::ConstructWith, 7, ItemCacheCheck<0x005F9F80>::original);
EI_VERIFY(0x00619630, ItemCacheCheck<0x00619630>::ConstructWith, 7, ItemCacheCheck<0x00619630>::original);
EI_VERIFY(0x005FA420, ItemCacheCheck<0x005FA420>::ConstructBase, 7, ItemCacheCheck<0x005FA420>::original);
EI_VERIFY(0x005FA630, ItemCacheCheck<0x005FA630>::ConstructBase, 7, ItemCacheCheck<0x005FA630>::original);
EI_VERIFY(0x005FA400, ItemCacheCheck<0x005FA400>::EmptyInitialize, 5, ItemCacheCheck<0x005FA400>::original);
EI_VERIFY(0x005FA610, ItemCacheCheck<0x005FA610>::EmptyInitialize, 5, ItemCacheCheck<0x005FA610>::original);
EI_VERIFY(0x005FA060, ItemCacheCheck<0x005FA060>::FreeHeader, 0, ItemCacheCheck<0x005FA060>::original);
EI_VERIFY(0x005FA090, ItemCacheCheck<0x005FA090>::FreeHeader, 0, ItemCacheCheck<0x005FA090>::original);
EI_VERIFY(0x005FA5D0, ItemCacheCheck<0x005FA5D0>::DeallocateNodes, 6, ItemCacheCheck<0x005FA5D0>::original);
EI_VERIFY(0x005F8FC0, OwnCheck<0x005F8FC0>::ReleaseModelCache, 7, OwnCheck<0x005F8FC0>::original);
EI_VERIFY(0x005F9690, ItemCacheCheck<0x005F9690>::Destroy, 7, ItemCacheCheck<0x005F9690>::original);
EI_VERIFY(0x00634160, ItemCacheCheck<0x00634160>::Destroy, 7, ItemCacheCheck<0x00634160>::original);
EI_VERIFY(0x005FAFF0, ItemCacheCheck<0x005FAFF0>::EraseSubtree, 6, ItemCacheCheck<0x005FAFF0>::original);
EI_VERIFY(0x005FB8E0, ItemCacheCheck<0x005FB8E0>::DestroyNode, 5, ItemCacheCheck<0x005FB8E0>::original);
EI_VERIFY(0x005F3000, ItemCacheCheck<0x005F3000>::Subscript, 7, ItemCacheCheck<0x005F3000>::original);
EI_VERIFY(0x005F37F0, ItemCacheCheck<0x005F37F0>::LowerBound, 5, ItemCacheCheck<0x005F37F0>::original);
EI_VERIFY(0x005F3730, ItemCacheCheck<0x005F3730>::Find, 8, ItemCacheCheck<0x005F3730>::original);
EI_VERIFY(0x005F3380, ItemCacheCheck<0x005F3380>::Begin, 5, ItemCacheCheck<0x005F3380>::original);
EI_VERIFY(0x005F4390, ItemCacheCheck<0x005F4390>::CreateNode, 5, ItemCacheCheck<0x005F4390>::original);
EI_VERIFY(0x005F3D10, ItemCacheCheck<0x005F3D10>::InsertAt, 5, ItemCacheCheck<0x005F3D10>::original);
EI_VERIFY(0x005F3EA0, ItemCacheCheck<0x005F3EA0>::InsertUnique, 8, ItemCacheCheck<0x005F3EA0>::original);
EI_VERIFY(0x005F3470, ItemCacheCheck<0x005F3470>::InsertHint, 5, ItemCacheCheck<0x005F3470>::original);
EI_VERIFY(0x005F3350, ItemCacheCheck<0x005F3350>::InsertHint, 5, ItemCacheCheck<0x005F3350>::original);
EI_VERIFY(0x005F3390, ItemCacheCheck<0x005F3390>::EraseAt, 6, ItemCacheCheck<0x005F3390>::original);
EI_VERIFY(0x005F3140, ItemCacheCheck<0x005F3140>::EraseAt, 5, ItemCacheCheck<0x005F3140>::original);
EI_VERIFY(0x005F32D0, ItemCacheCheck<0x005F32D0>::DestroyValue, 5, ItemCacheCheck<0x005F32D0>::original);
EI_VERIFY(0x005FBCF0, ItemCacheCheck<0x005FBCF0>::DeletingDestroyValue, 0, ItemCacheCheck<0x005FBCF0>::original);
EI_VERIFY(0x005F9730, ItemCacheCheck<0x005F9730>::Destroy, 7, ItemCacheCheck<0x005F9730>::original);
EI_VERIFY(0x00634200, ItemCacheCheck<0x00634200>::Destroy, 7, ItemCacheCheck<0x00634200>::original);
EI_VERIFY(0x005FB040, ItemCacheCheck<0x005FB040>::EraseSubtree, 6, ItemCacheCheck<0x005FB040>::original);
EI_VERIFY(0x005FB910, ItemCacheCheck<0x005FB910>::DestroyNode, 5, ItemCacheCheck<0x005FB910>::original);
EI_VERIFY(0x005F3170, ItemCacheCheck<0x005F3170>::Subscript, 7, ItemCacheCheck<0x005F3170>::original);
EI_VERIFY(0x005F3C10, ItemCacheCheck<0x005F3C10>::LowerBound, 5, ItemCacheCheck<0x005F3C10>::original);
EI_VERIFY(0x005F3B50, ItemCacheCheck<0x005F3B50>::Find, 8, ItemCacheCheck<0x005F3B50>::original);
EI_VERIFY(0x005F3410, ItemCacheCheck<0x005F3410>::Begin, 5, ItemCacheCheck<0x005F3410>::original);
EI_VERIFY(0x005F44A0, ItemCacheCheck<0x005F44A0>::CreateNode, 5, ItemCacheCheck<0x005F44A0>::original);
EI_VERIFY(0x005F4050, ItemCacheCheck<0x005F4050>::InsertAt, 5, ItemCacheCheck<0x005F4050>::original);
EI_VERIFY(0x005F41E0, ItemCacheCheck<0x005F41E0>::InsertUnique, 8, ItemCacheCheck<0x005F41E0>::original);
EI_VERIFY(0x005F3890, ItemCacheCheck<0x005F3890>::InsertHint, 5, ItemCacheCheck<0x005F3890>::original);
EI_VERIFY(0x005F33E0, ItemCacheCheck<0x005F33E0>::InsertHint, 5, ItemCacheCheck<0x005F33E0>::original);
EI_VERIFY(0x005F3420, ItemCacheCheck<0x005F3420>::EraseAt, 6, ItemCacheCheck<0x005F3420>::original);
EI_VERIFY(0x005F32A0, ItemCacheCheck<0x005F32A0>::EraseAt, 5, ItemCacheCheck<0x005F32A0>::original);
EI_VERIFY(0x005F3310, ItemCacheCheck<0x005F3310>::DestroyValue, 5, ItemCacheCheck<0x005F3310>::original);
EI_VERIFY(0x005FBD10, ItemCacheCheck<0x005FBD10>::DeletingDestroyValue, 0, ItemCacheCheck<0x005FBD10>::original);
EI_VERIFY(0x005E0A90, CommandsCheck<0x005E0A90>::Construct, 7, CommandsCheck<0x005E0A90>::original);
EI_VERIFY(0x005DC080, CommandsCheck<0x005DC080>::Destroy, 7, CommandsCheck<0x005DC080>::original);
EI_VERIFY(0x005E0CE0, CommandsCheck<0x005E0CE0>::FreeHeader, 0, CommandsCheck<0x005E0CE0>::original);
EI_VERIFY(0x005E0D70, CommandsCheck<0x005E0D70>::DeallocateNodesFast, 0, CommandsCheck<0x005E0D70>::original);
EI_VERIFY(0x005E0E40, CommandsCheck<0x005E0E40>::EraseSubtree, 6, CommandsCheck<0x005E0E40>::original);
EI_VERIFY(0x005E0B20, CommandsCheck<0x005E0B20>::Subscript, 6, CommandsCheck<0x005E0B20>::original);
EI_VERIFY(0x005E11A0, CommandsCheck<0x005E11A0>::LowerBound, 7, CommandsCheck<0x005E11A0>::original);
EI_VERIFY(0x005E0C90, CommandsCheck<0x005E0C90>::LowerBound, 7, CommandsCheck<0x005E0C90>::original);
EI_VERIFY(0x005E1670, CommandsCheck<0x005E1670>::Begin, 5, CommandsCheck<0x005E1670>::original);
EI_VERIFY(0x005E1580, CommandsCheck<0x005E1580>::CreateNode, 5, CommandsCheck<0x005E1580>::original);
EI_VERIFY(0x005E11F0, CommandsCheck<0x005E11F0>::InsertAt, 5, CommandsCheck<0x005E11F0>::original);
EI_VERIFY(0x005E13D0, CommandsCheck<0x005E13D0>::InsertUnique, 6, CommandsCheck<0x005E13D0>::original);
EI_VERIFY(0x005E0E90, CommandsCheck<0x005E0E90>::InsertHint, 5, CommandsCheck<0x005E0E90>::original);
EI_VERIFY(0x005E0C60, CommandsCheck<0x005E0C60>::InsertHint, 5, CommandsCheck<0x005E0C60>::original);
EI_VERIFY(0x005E0D20, CommandsCheck<0x005E0D20>::MakeValue, 5, CommandsCheck<0x005E0D20>::original);
EI_VERIFY(0x005E0BF0, CommandsCheck<0x005E0BF0>::DestroyValue, 0, CommandsCheck<0x005E0BF0>::original);
EI_VERIFY(0x005F9D90, HelpTopicsCheck<0x005F9D90>::ConstructBase, 7, HelpTopicsCheck<0x005F9D90>::original);
EI_VERIFY(0x005F9D70, HelpTopicsCheck<0x005F9D70>::EmptyInitialize, 5, HelpTopicsCheck<0x005F9D70>::original);
EI_VERIFY(0x005F48F0, HelpTopicsCheck<0x005F48F0>::Destroy, 7, HelpTopicsCheck<0x005F48F0>::original);
EI_VERIFY(0x005F9840, HelpTopicsCheck<0x005F9840>::FreeHeader, 0, HelpTopicsCheck<0x005F9840>::original);
EI_VERIFY(0x005FA820, HelpTopicsCheck<0x005FA820>::DeallocateNodesFast, 0, HelpTopicsCheck<0x005FA820>::original);
EI_VERIFY(0x005FAB30, HelpTopicsCheck<0x005FAB30>::EraseSubtree, 6, HelpTopicsCheck<0x005FAB30>::original);
EI_VERIFY(0x005FB8B0, HelpTopicsCheck<0x005FB8B0>::DestroyNode, 5, HelpTopicsCheck<0x005FB8B0>::original);
EI_VERIFY(0x0065A770, HelpTopicsCheck<0x0065A770>::Subscript, 6, HelpTopicsCheck<0x0065A770>::original);
EI_VERIFY(0x005FAF30, HelpTopicsCheck<0x005FAF30>::LowerBound, 7, HelpTopicsCheck<0x005FAF30>::original);
EI_VERIFY(0x005FAEA0, HelpTopicsCheck<0x005FAEA0>::Find, 5, HelpTopicsCheck<0x005FAEA0>::original);
EI_VERIFY(0x005FAE90, HelpTopicsCheck<0x005FAE90>::Begin, 5, HelpTopicsCheck<0x005FAE90>::original);
EI_VERIFY(0x005FBE40, HelpTopicsCheck<0x005FBE40>::CreateNode, 5, HelpTopicsCheck<0x005FBE40>::original);
EI_VERIFY(0x005FB940, HelpTopicsCheck<0x005FB940>::InsertAt, 5, HelpTopicsCheck<0x005FB940>::original);
EI_VERIFY(0x005FBB20, HelpTopicsCheck<0x005FBB20>::InsertUnique, 6, HelpTopicsCheck<0x005FBB20>::original);
EI_VERIFY(0x005FAB80, HelpTopicsCheck<0x005FAB80>::InsertHint, 5, HelpTopicsCheck<0x005FAB80>::original);
EI_VERIFY(0x005F9810, HelpTopicsCheck<0x005F9810>::InsertHint, 5, HelpTopicsCheck<0x005F9810>::original);
EI_VERIFY(0x005F8F30, PersonTextsCheck<0x005F8F30>::Construct, 7, PersonTextsCheck<0x005F8F30>::original);
EI_VERIFY(0x005F5FE0, PersonTextsCheck<0x005F5FE0>::Destroy, 7, PersonTextsCheck<0x005F5FE0>::original);
EI_VERIFY(0x005F98C0, PersonTextsCheck<0x005F98C0>::FreeHeader, 0, PersonTextsCheck<0x005F98C0>::original);
EI_VERIFY(0x005FA860, PersonTextsCheck<0x005FA860>::DeallocateNodesFast, 0, PersonTextsCheck<0x005FA860>::original);
EI_VERIFY(0x005FAFA0, PersonTextsCheck<0x005FAFA0>::EraseSubtree, 6, PersonTextsCheck<0x005FAFA0>::original);
EI_VERIFY(0x0060A620, PersonTextsCheck<0x0060A620>::LowerBound, 5, PersonTextsCheck<0x0060A620>::original);
EI_VERIFY(0x0060A5C0, PersonTextsCheck<0x0060A5C0>::Find, 5, PersonTextsCheck<0x0060A5C0>::original);
EI_VERIFY(0x0060B5F0, PersonTextsCheck<0x0060B5F0>::Begin, 5, PersonTextsCheck<0x0060B5F0>::original);
EI_VERIFY(0x0060B500, PersonTextsCheck<0x0060B500>::CreateNode, 5, PersonTextsCheck<0x0060B500>::original);
EI_VERIFY(0x0060AEE0, PersonTextsCheck<0x0060AEE0>::InsertAt, 5, PersonTextsCheck<0x0060AEE0>::original);
EI_VERIFY(0x0060B0A0, PersonTextsCheck<0x0060B0A0>::InsertUnique, 5, PersonTextsCheck<0x0060B0A0>::original);
EI_VERIFY(0x0060A320, PersonTextsCheck<0x0060A320>::InsertHint, 5, PersonTextsCheck<0x0060A320>::original);
EI_VERIFY(0x00609E70, PersonTextsCheck<0x00609E70>::InsertHint, 5, PersonTextsCheck<0x00609E70>::original);
EI_VERIFY(0x005FBE30, PersonTextsCheck<0x005FBE30>::DestroyValue, 0, PersonTextsCheck<0x005FBE30>::original);
EI_VERIFY(0x006062B0, PersonTextsCheck<0x006062B0>::DestroyValue, 0, PersonTextsCheck<0x006062B0>::original);
EI_VERIFY(0x005FBCD0, PersonTextsCheck<0x005FBCD0>::DeletingDestroyValue, 0, PersonTextsCheck<0x005FBCD0>::original);
EI_VERIFY(0x0060AC00, PersonTextsCheck<0x0060AC00>::DeletingDestroyValue, 0, PersonTextsCheck<0x0060AC00>::original);
EI_VERIFY(0x005FA100, ArrivalsCheck<0x005FA100>::ConstructBase, 7, ArrivalsCheck<0x005FA100>::original);
EI_VERIFY(0x005FA0E0, ArrivalsCheck<0x005FA0E0>::EmptyInitialize, 5, ArrivalsCheck<0x005FA0E0>::original);
EI_VERIFY(0x005F6AD0, ArrivalsCheck<0x005F6AD0>::Destroy, 7, ArrivalsCheck<0x005F6AD0>::original);
EI_VERIFY(0x00631710, ArrivalsCheck<0x00631710>::Clear, 6, ArrivalsCheck<0x00631710>::original);
EI_VERIFY(0x005F9980, ArrivalsCheck<0x005F9980>::FreeHeader, 0, ArrivalsCheck<0x005F9980>::original);
EI_VERIFY(0x005FA8A0, ArrivalsCheck<0x005FA8A0>::DeallocateNodesFast, 0, ArrivalsCheck<0x005FA8A0>::original);
EI_VERIFY(0x006317A0, ArrivalsCheck<0x006317A0>::DeallocateNodes, 6, ArrivalsCheck<0x006317A0>::original);
EI_VERIFY(0x005FB7D0, ArrivalsCheck<0x005FB7D0>::EraseSubtree, 5, ArrivalsCheck<0x005FB7D0>::original);
EI_VERIFY(0x00631C80, ArrivalsCheck<0x00631C80>::LowerBound, 5, ArrivalsCheck<0x00631C80>::original);
EI_VERIFY(0x00631C20, ArrivalsCheck<0x00631C20>::Find, 5, ArrivalsCheck<0x00631C20>::original);
EI_VERIFY(0x006325B0, ArrivalsCheck<0x006325B0>::Begin, 5, ArrivalsCheck<0x006325B0>::original);
EI_VERIFY(0x00632530, ArrivalsCheck<0x00632530>::CreateNode, 6, ArrivalsCheck<0x00632530>::original);
EI_VERIFY(0x00632170, ArrivalsCheck<0x00632170>::InsertAt, 5, ArrivalsCheck<0x00632170>::original);
EI_VERIFY(0x00632330, ArrivalsCheck<0x00632330>::InsertUnique, 5, ArrivalsCheck<0x00632330>::original);
EI_VERIFY(0x00631980, ArrivalsCheck<0x00631980>::InsertHint, 5, ArrivalsCheck<0x00631980>::original);
EI_VERIFY(0x006316E0, ArrivalsCheck<0x006316E0>::InsertHint, 5, ArrivalsCheck<0x006316E0>::original);
EI_VERIFY(0x00631780, ArrivalsCheck<0x00631780>::MakeValue, 6, ArrivalsCheck<0x00631780>::original);
