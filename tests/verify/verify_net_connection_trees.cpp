// Checks for the connection's STLport maps (src/game/net_connection_trees.cpp): the per-call
// wrappers of stlp_tree_verify.h, and a start-up check that runs the instantiations' originals
// against ours: for each tree two built side by side from one random script (insert, insert with a
// hint, operator[], erase by key, at a position and over a range, a copy, clear, destroy), after
// every step the same shape, colours, keys and value bytes (null references, so no object is
// touched).
#include "verify.h"

#include "stlp_tree_verify.h"

#include "../../src/game/net_connection_trees.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>

namespace {

using stlp_tree_verify::NodeAt;
using stlp_tree_verify::OriginalOf;
using stlp_tree_verify::Originals;
using stlp_tree_verify::Pick;
using stlp_tree_verify::Random;
using stlp_tree_verify::SameTree;
using stlp_tree_verify::SharedCheck;
using stlp_tree_verify::TreeCheck;

using SentIds = net::trees::SentIds;
using Objects = net::trees::Objects;
using Entries = net::trees::Entries;
using Records = net::trees::Records;
using Attached = net::trees::Attached;

template <std::uintptr_t A> using SentIdsCheck = TreeCheck<A, SentIds>;
template <std::uintptr_t A> using ObjectsCheck = TreeCheck<A, Objects>;
template <std::uintptr_t A> using EntriesCheck = TreeCheck<A, Entries>;
template <std::uintptr_t A> using RecordsCheck = TreeCheck<A, Records>;
template <std::uintptr_t A> using AttachedCheck = TreeCheck<A, Attached>;

// --- Start-up check -----------------------------------------------------------------------------

// Keys from a small set so inserts meet existing keys and erases find them; negative and high-bit
// keys so the signed and unsigned orders differ.
template <typename K>
K KeyFrom(std::uint32_t r) {
    std::uint32_t word = (r % 48) * 0x05000000u + (r & 1);
    if constexpr (std::is_pointer_v<K>) return reinterpret_cast<K>(static_cast<std::uintptr_t>(word));
    else return static_cast<K>(word);
}

template <typename Abi>
void Script(const char* name, const Originals<Abi>& o, std::uint32_t seed) {
    using Tree = typename Abi::Tree;
    using Node = typename Abi::Node;
    using Value = typename Abi::Value;
    using K = typename Abi::K;
    alignas(4) unsigned char storage[2][sizeof(Tree)] = {};
    Tree* trees[2] = {reinterpret_cast<Tree*>(storage[0]), reinterpret_cast<Tree*>(storage[1])};
    const std::uint8_t compare = 0;
    for (Tree* t : trees) Abi::ConstructWith(t, nullptr, &compare, &compare);
    Random random{seed};
    int failedStep = -1;
    const int kSteps = 600;
    for (int step = 0; step < kSteps && failedStep < 0; ++step) {
        std::uint32_t op = random.Next() % 9;
        K key = KeyFrom<K>(random.Next());
        Value v{};
        v.first = key;
        if constexpr (sizeof(typename Abi::V) == 4 && !std::is_same_v<typename Abi::V, RefPtr>) {
            std::uint32_t bits = random.Next();
            std::memcpy(&v.second, &bits, 4);
        }
        std::uint32_t r1 = random.Next(), r2 = random.Next();
        bool same = true;
        for (int i = 0; i < 2; ++i) {
            Tree* t = trees[i];
            bool orig = i == 0;
            std::uint32_t count = static_cast<std::uint32_t>(t->size());
            switch (op) {
            case 0:
            case 1: {
                typename Abi::InsertResult out{};
                Pick(o.insertUnique, &Abi::InsertUnique, orig)(t, nullptr, &out, &v);
                break;
            }
            case 2: {
                Node* out = nullptr;
                Node* hint = NodeAt<Abi>(*t, count ? r1 % (count + 1) : 0);
                Pick(o.insertHint, &Abi::InsertHint, orig)(t, nullptr, &out, hint, &v);
                break;
            }
            case 3:
                // The original's default value is partly uninitialised stack (the records' version,
                // the attached id); the game stores the value next, as this does.
                std::memcpy(Pick(o.subscript, &Abi::Subscript, orig)(t, nullptr, &key), &v.second, sizeof(v.second));
                break;
            case 4: Pick(o.eraseKey, &Abi::EraseKey, orig)(t, nullptr, &key); break;
            case 5:
                if (count) Pick(o.eraseAt, &Abi::EraseAt, orig)(t, nullptr, NodeAt<Abi>(*t, r1 % count));
                break;
            case 6: {
                std::uint32_t a = count ? r1 % (count + 1) : 0;
                std::uint32_t b = a + (count + 1 - a ? r2 % (count + 1 - a) : 0);
                if (r2 % 16 == 0) a = 0, b = count;   // now and then the whole tree (clear)
                Pick(o.eraseRange, &Abi::EraseRange, orig)(t, nullptr, NodeAt<Abi>(*t, a), NodeAt<Abi>(*t, b));
                break;
            }
            case 7: {
                // A copy, compared, destroyed; the tree stays.
                alignas(4) unsigned char copy[sizeof(Tree)];
                Tree* c = reinterpret_cast<Tree*>(copy);
                Pick(o.copy, &Abi::CopyConstruct, orig)(c, nullptr, t);
                same = same && SameTree<Abi>(*c, *t) && c->end() != t->end();
                Pick(o.destroy, &Abi::Destroy, orig)(c);
                break;
            }
            case 8:
                if (r1 % 8 == 0) Pick(o.clear, &Abi::Clear, orig)(t);
                break;
            }
        }
        if (!same || !SameTree<Abi>(*trees[0], *trees[1])) failedStep = step;
    }
    char detail[64] = "";
    if (failedStep >= 0) std::snprintf(detail, sizeof(detail), "seed %u: step %d", static_cast<unsigned>(seed), failedStep);
    ei::verify::Report(name, failedStep < 0, detail);
    Pick(o.destroy, &Abi::Destroy, true)(trees[0]);
    Abi::Destroy(trees[1]);
}

void TreesStartupCheck() {
    for (std::uint32_t seed = 1; seed <= 4; ++seed) {
        Originals<SentIds> s;
        s.insertUnique = OriginalOf<0x0043E3A0, SentIds>(&SentIds::InsertUnique);
        s.insertHint = OriginalOf<0x0043BBC0, SentIds>(&SentIds::InsertHint);
        s.subscript = OriginalOf<0x00437B90, SentIds>(&SentIds::Subscript);
        s.eraseKey = OriginalOf<0x0043BE60, SentIds>(&SentIds::EraseKey);
        s.eraseAt = OriginalOf<0x0043FD50, SentIds>(&SentIds::EraseAt);
        s.eraseRange = OriginalOf<0x0043E4E0, SentIds>(&SentIds::EraseRange);
        s.copy = OriginalOf<0x00437AA0, SentIds>(&SentIds::CopyConstruct);
        s.clear = OriginalOf<0x004394A0, SentIds>(&SentIds::Clear);
        s.destroy = OriginalOf<0x00433710, SentIds>(&SentIds::Destroy);
        Script<SentIds>("net::trees startup: by sent id", s, seed);

        Originals<Objects> b;
        b.insertUnique = OriginalOf<0x0043E7E0, Objects>(&Objects::InsertUnique);
        b.insertHint = OriginalOf<0x0043C100, Objects>(&Objects::InsertHint);
        b.eraseKey = OriginalOf<0x0043C3B0, Objects>(&Objects::EraseKey);
        b.eraseAt = OriginalOf<0x00439650, Objects>(&Objects::EraseAt);
        b.eraseRange = OriginalOf<0x0043E920, Objects>(&Objects::EraseRange);
        b.copy = OriginalOf<0x00437C50, Objects>(&Objects::CopyConstruct);
        b.clear = OriginalOf<0x004396A0, Objects>(&Objects::Clear);
        b.destroy = OriginalOf<0x004337B0, Objects>(&Objects::Destroy);
        Script<Objects>("net::trees startup: by object", b, seed);

        Originals<Entries> e;
        e.insertUnique = OriginalOf<0x0043EC20, Entries>(&Entries::InsertUnique);
        e.insertHint = OriginalOf<0x0043C650, Entries>(&Entries::InsertHint);
        e.eraseKey = OriginalOf<0x0043C900, Entries>(&Entries::EraseKey);
        e.eraseAt = OriginalOf<0x00439760, Entries>(&Entries::EraseAt);
        e.eraseRange = OriginalOf<0x0043ED60, Entries>(&Entries::EraseRange);
        e.copy = OriginalOf<0x00437E00, Entries>(&Entries::CopyConstruct);
        e.destroy = OriginalOf<0x00433850, Entries>(&Entries::Destroy);
        Script<Entries>("net::trees startup: priority entries", e, seed);

        Originals<Records> r;
        r.insertUnique = OriginalOf<0x0043F0A0, Records>(&Records::InsertUnique);
        r.insertHint = OriginalOf<0x0043CBC0, Records>(&Records::InsertHint);
        r.subscript = OriginalOf<0x004382A0, Records>(&Records::Subscript);
        r.copy = OriginalOf<0x004381B0, Records>(&Records::CopyConstruct);
        r.clear = OriginalOf<0x00439AE0, Records>(&Records::Clear);
        r.destroy = OriginalOf<0x004383A0, Records>(&Records::Destroy);
        Script<Records>("net::trees startup: records", r, seed);

        Originals<Attached> t;
        t.insertUnique = OriginalOf<0x0043F3A0, Attached>(&Attached::InsertUnique);
        t.insertHint = OriginalOf<0x0043D040, Attached>(&Attached::InsertHint);
        t.subscript = OriginalOf<0x00438530, Attached>(&Attached::Subscript);
        t.eraseKey = OriginalOf<0x0043D2E0, Attached>(&Attached::EraseKey);
        t.eraseAt = OriginalOf<0x0043FDB0, Attached>(&Attached::EraseAt);
        t.eraseRange = OriginalOf<0x0043F4E0, Attached>(&Attached::EraseRange);
        t.copy = OriginalOf<0x00438440, Attached>(&Attached::CopyConstruct);
        t.clear = OriginalOf<0x00439BB0, Attached>(&Attached::Clear);
        t.destroy = OriginalOf<0x004385A0, Attached>(&Attached::Destroy);
        Script<Attached>("net::trees startup: attached ids", t, seed);
    }
}

} // namespace

EI_STARTUP_CHECK(TreesStartupCheck);

// --- Registrations (stolen bytes: whole instructions covering five bytes) -----------------------
EI_VERIFY(0x00439530, SharedCheck<0x00439530>::IteratorCopy, 6, SharedCheck<0x00439530>::original);
EI_VERIFY(0x00437D60, SharedCheck<0x00437D60>::IteratorCopy, 6, SharedCheck<0x00437D60>::original);
EI_VERIFY(0x00437F10, SharedCheck<0x00437F10>::IteratorCopy, 6, SharedCheck<0x00437F10>::original);
EI_VERIFY(0x00439B70, SharedCheck<0x00439B70>::IteratorCopy, 6, SharedCheck<0x00439B70>::original);
EI_VERIFY(0x00439C40, SharedCheck<0x00439C40>::IteratorCopy, 6, SharedCheck<0x00439C40>::original);
EI_VERIFY(0x0043FDF0, SharedCheck<0x0043FDF0>::InsertResult, 6, SharedCheck<0x0043FDF0>::original);
EI_VERIFY(0x0043FE10, SharedCheck<0x0043FE10>::InsertResult, 6, SharedCheck<0x0043FE10>::original);
EI_VERIFY(0x0043FE30, SharedCheck<0x0043FE30>::InsertResult, 6, SharedCheck<0x0043FE30>::original);
EI_VERIFY(0x0043FE50, SharedCheck<0x0043FE50>::InsertResult, 6, SharedCheck<0x0043FE50>::original);
EI_VERIFY(0x0043FE70, SharedCheck<0x0043FE70>::InsertResult, 6, SharedCheck<0x0043FE70>::original);
EI_VERIFY(0x0043E5F0, SharedCheck<0x0043E5F0>::Distance, 5, SharedCheck<0x0043E5F0>::original);
EI_VERIFY(0x0043EA30, SharedCheck<0x0043EA30>::Distance, 5, SharedCheck<0x0043EA30>::original);
EI_VERIFY(0x0043EEB0, SharedCheck<0x0043EEB0>::Distance, 5, SharedCheck<0x0043EEB0>::original);
EI_VERIFY(0x0043F5E0, SharedCheck<0x0043F5E0>::Distance, 5, SharedCheck<0x0043F5E0>::original);
EI_VERIFY(0x00440040, SharedCheck<0x00440040>::DistanceTagged, 5, SharedCheck<0x00440040>::original);
EI_VERIFY(0x00440090, SharedCheck<0x00440090>::DistanceTagged, 5, SharedCheck<0x00440090>::original);
EI_VERIFY(0x004400E0, SharedCheck<0x004400E0>::DistanceTagged, 5, SharedCheck<0x004400E0>::original);
EI_VERIFY(0x00440130, SharedCheck<0x00440130>::DistanceTagged, 5, SharedCheck<0x00440130>::original);
EI_VERIFY(0x0043A460, SharedCheck<0x0043A460>::GetAllocator, 5, SharedCheck<0x0043A460>::original);
EI_VERIFY(0x0043A4C0, SharedCheck<0x0043A4C0>::GetAllocator, 5, SharedCheck<0x0043A4C0>::original);
EI_VERIFY(0x0043A520, SharedCheck<0x0043A520>::GetAllocator, 5, SharedCheck<0x0043A520>::original);
EI_VERIFY(0x0043A740, SharedCheck<0x0043A740>::GetAllocator, 5, SharedCheck<0x0043A740>::original);
EI_VERIFY(0x0043A930, SharedCheck<0x0043A930>::GetAllocator, 5, SharedCheck<0x0043A930>::original);
EI_VERIFY(0x0043AD80, SharedCheck<0x0043AD80>::AllocProxy, 6, SharedCheck<0x0043AD80>::original);
EI_VERIFY(0x0043ADA0, SharedCheck<0x0043ADA0>::AllocProxy, 6, SharedCheck<0x0043ADA0>::original);
EI_VERIFY(0x0043ADC0, SharedCheck<0x0043ADC0>::AllocProxy, 6, SharedCheck<0x0043ADC0>::original);
EI_VERIFY(0x0043B140, SharedCheck<0x0043B140>::AllocProxy, 6, SharedCheck<0x0043B140>::original);
EI_VERIFY(0x0043B160, SharedCheck<0x0043B160>::AllocProxy, 6, SharedCheck<0x0043B160>::original);
EI_VERIFY(0x00433710, SentIdsCheck<0x00433710>::Destroy, 7, SentIdsCheck<0x00433710>::original);
EI_VERIFY(0x004337B0, ObjectsCheck<0x004337B0>::Destroy, 7, ObjectsCheck<0x004337B0>::original);
EI_VERIFY(0x004338F0, RecordsCheck<0x004338F0>::Destroy, 7, RecordsCheck<0x004338F0>::original);
EI_VERIFY(0x00433990, AttachedCheck<0x00433990>::Destroy, 7, AttachedCheck<0x00433990>::original);
EI_VERIFY(0x004393C0, SentIdsCheck<0x004393C0>::ConstructWith, 7, SentIdsCheck<0x004393C0>::original);
EI_VERIFY(0x00437AA0, SentIdsCheck<0x00437AA0>::CopyConstruct, 7, SentIdsCheck<0x00437AA0>::original);
EI_VERIFY(0x004394A0, SentIdsCheck<0x004394A0>::Clear, 6, SentIdsCheck<0x004394A0>::original);
EI_VERIFY(0x00439500, SentIdsCheck<0x00439500>::FreeHeader, 0, SentIdsCheck<0x00439500>::original);
EI_VERIFY(0x00437B90, SentIdsCheck<0x00437B90>::Subscript, 6, SentIdsCheck<0x00437B90>::original);
EI_VERIFY(0x0043BF00, SentIdsCheck<0x0043BF00>::Find, 5, SentIdsCheck<0x0043BF00>::original);
EI_VERIFY(0x0043BF60, SentIdsCheck<0x0043BF60>::LowerBound, 5, SentIdsCheck<0x0043BF60>::original);
EI_VERIFY(0x0043FD40, SentIdsCheck<0x0043FD40>::Begin, 5, SentIdsCheck<0x0043FD40>::original);
EI_VERIFY(0x0043DD50, SentIdsCheck<0x0043DD50>::CreateNode, 6, SentIdsCheck<0x0043DD50>::original);
EI_VERIFY(0x0043BA60, SentIdsCheck<0x0043BA60>::CopySubtree, 5, SentIdsCheck<0x0043BA60>::original);
EI_VERIFY(0x0043BB70, SentIdsCheck<0x0043BB70>::EraseSubtree, 6, SentIdsCheck<0x0043BB70>::original);
EI_VERIFY(0x0043E140, SentIdsCheck<0x0043E140>::InsertAt, 5, SentIdsCheck<0x0043E140>::original);
EI_VERIFY(0x0043E3A0, SentIdsCheck<0x0043E3A0>::InsertUnique, 5, SentIdsCheck<0x0043E3A0>::original);
EI_VERIFY(0x0043BBC0, SentIdsCheck<0x0043BBC0>::InsertHint, 5, SentIdsCheck<0x0043BBC0>::original);
EI_VERIFY(0x00439390, SentIdsCheck<0x00439390>::InsertHint, 5, SentIdsCheck<0x00439390>::original);
EI_VERIFY(0x0043FD50, SentIdsCheck<0x0043FD50>::EraseAt, 6, SentIdsCheck<0x0043FD50>::original);
EI_VERIFY(0x0043E4E0, SentIdsCheck<0x0043E4E0>::EraseRange, 6, SentIdsCheck<0x0043E4E0>::original);
EI_VERIFY(0x0043BE60, SentIdsCheck<0x0043BE60>::EraseKey, 7, SentIdsCheck<0x0043BE60>::original);
EI_VERIFY(0x00439570, ObjectsCheck<0x00439570>::ConstructWith, 7, ObjectsCheck<0x00439570>::original);
EI_VERIFY(0x00437C50, ObjectsCheck<0x00437C50>::CopyConstruct, 7, ObjectsCheck<0x00437C50>::original);
EI_VERIFY(0x004396A0, ObjectsCheck<0x004396A0>::Clear, 6, ObjectsCheck<0x004396A0>::original);
EI_VERIFY(0x00439700, ObjectsCheck<0x00439700>::FreeHeader, 0, ObjectsCheck<0x00439700>::original);
EI_VERIFY(0x0043C450, ObjectsCheck<0x0043C450>::Find, 5, ObjectsCheck<0x0043C450>::original);
EI_VERIFY(0x0043C4B0, ObjectsCheck<0x0043C4B0>::LowerBound, 5, ObjectsCheck<0x0043C4B0>::original);
EI_VERIFY(0x0043C3A0, ObjectsCheck<0x0043C3A0>::Begin, 5, ObjectsCheck<0x0043C3A0>::original);
EI_VERIFY(0x0043DDE0, ObjectsCheck<0x0043DDE0>::CreateNode, 6, ObjectsCheck<0x0043DDE0>::original);
EI_VERIFY(0x0043BFA0, ObjectsCheck<0x0043BFA0>::CopySubtree, 5, ObjectsCheck<0x0043BFA0>::original);
EI_VERIFY(0x0043C0B0, ObjectsCheck<0x0043C0B0>::EraseSubtree, 6, ObjectsCheck<0x0043C0B0>::original);
EI_VERIFY(0x0043E620, ObjectsCheck<0x0043E620>::InsertAt, 5, ObjectsCheck<0x0043E620>::original);
EI_VERIFY(0x0043E7E0, ObjectsCheck<0x0043E7E0>::InsertUnique, 5, ObjectsCheck<0x0043E7E0>::original);
EI_VERIFY(0x0043C100, ObjectsCheck<0x0043C100>::InsertHint, 5, ObjectsCheck<0x0043C100>::original);
EI_VERIFY(0x00439540, ObjectsCheck<0x00439540>::InsertHint, 5, ObjectsCheck<0x00439540>::original);
EI_VERIFY(0x00439650, ObjectsCheck<0x00439650>::EraseAt, 6, ObjectsCheck<0x00439650>::original);
EI_VERIFY(0x00437D40, ObjectsCheck<0x00437D40>::EraseAt, 5, ObjectsCheck<0x00437D40>::original);
EI_VERIFY(0x0043E920, ObjectsCheck<0x0043E920>::EraseRange, 6, ObjectsCheck<0x0043E920>::original);
EI_VERIFY(0x0043C3B0, ObjectsCheck<0x0043C3B0>::EraseKey, 7, ObjectsCheck<0x0043C3B0>::original);
EI_VERIFY(0x0043A050, ObjectsCheck<0x0043A050>::MakeValue, 6, ObjectsCheck<0x0043A050>::original);
EI_VERIFY(0x00437D70, EntriesCheck<0x00437D70>::Construct, 7, EntriesCheck<0x00437D70>::original);
EI_VERIFY(0x00437E00, EntriesCheck<0x00437E00>::CopyConstruct, 7, EntriesCheck<0x00437E00>::original);
EI_VERIFY(0x00433850, EntriesCheck<0x00433850>::Destroy, 7, EntriesCheck<0x00433850>::original);
EI_VERIFY(0x004397B0, EntriesCheck<0x004397B0>::FreeHeader, 0, EntriesCheck<0x004397B0>::original);
EI_VERIFY(0x0043EE30, EntriesCheck<0x0043EE30>::DeallocateNodes, 6, EntriesCheck<0x0043EE30>::original);
EI_VERIFY(0x0043C9A0, EntriesCheck<0x0043C9A0>::Find, 5, EntriesCheck<0x0043C9A0>::original);
EI_VERIFY(0x0043CA00, EntriesCheck<0x0043CA00>::LowerBound, 5, EntriesCheck<0x0043CA00>::original);
EI_VERIFY(0x0043C8F0, EntriesCheck<0x0043C8F0>::Begin, 5, EntriesCheck<0x0043C8F0>::original);
EI_VERIFY(0x0043DE70, EntriesCheck<0x0043DE70>::CreateNode, 6, EntriesCheck<0x0043DE70>::original);
EI_VERIFY(0x0043C4F0, EntriesCheck<0x0043C4F0>::CopySubtree, 5, EntriesCheck<0x0043C4F0>::original);
EI_VERIFY(0x0043C600, EntriesCheck<0x0043C600>::EraseSubtree, 6, EntriesCheck<0x0043C600>::original);
EI_VERIFY(0x0043EA60, EntriesCheck<0x0043EA60>::InsertAt, 5, EntriesCheck<0x0043EA60>::original);
EI_VERIFY(0x0043EC20, EntriesCheck<0x0043EC20>::InsertUnique, 5, EntriesCheck<0x0043EC20>::original);
EI_VERIFY(0x0043C650, EntriesCheck<0x0043C650>::InsertHint, 5, EntriesCheck<0x0043C650>::original);
EI_VERIFY(0x00439730, EntriesCheck<0x00439730>::InsertHint, 5, EntriesCheck<0x00439730>::original);
EI_VERIFY(0x00439760, EntriesCheck<0x00439760>::EraseAt, 6, EntriesCheck<0x00439760>::original);
EI_VERIFY(0x00437EF0, EntriesCheck<0x00437EF0>::EraseAt, 5, EntriesCheck<0x00437EF0>::original);
EI_VERIFY(0x0043ED60, EntriesCheck<0x0043ED60>::EraseRange, 6, EntriesCheck<0x0043ED60>::original);
EI_VERIFY(0x0043C900, EntriesCheck<0x0043C900>::EraseKey, 7, EntriesCheck<0x0043C900>::original);
EI_VERIFY(0x0043A070, EntriesCheck<0x0043A070>::MakeValue, 6, EntriesCheck<0x0043A070>::original);
EI_VERIFY(0x0043A590, RecordsCheck<0x0043A590>::ConstructBase, 7, RecordsCheck<0x0043A590>::original);
EI_VERIFY(0x0043A570, RecordsCheck<0x0043A570>::EmptyInitialize, 5, RecordsCheck<0x0043A570>::original);
EI_VERIFY(0x004381B0, RecordsCheck<0x004381B0>::CopyConstruct, 7, RecordsCheck<0x004381B0>::original);
EI_VERIFY(0x00439AE0, RecordsCheck<0x00439AE0>::Clear, 6, RecordsCheck<0x00439AE0>::original);
EI_VERIFY(0x004383A0, RecordsCheck<0x004383A0>::Destroy, 7, RecordsCheck<0x004383A0>::original);
EI_VERIFY(0x00439B40, RecordsCheck<0x00439B40>::FreeHeader, 0, RecordsCheck<0x00439B40>::original);
EI_VERIFY(0x0043B280, RecordsCheck<0x0043B280>::DeallocateNodesFast, 0, RecordsCheck<0x0043B280>::original);
EI_VERIFY(0x004382A0, RecordsCheck<0x004382A0>::Subscript, 6, RecordsCheck<0x004382A0>::original);
EI_VERIFY(0x0043CE70, RecordsCheck<0x0043CE70>::Find, 5, RecordsCheck<0x0043CE70>::original);
EI_VERIFY(0x00439AA0, RecordsCheck<0x00439AA0>::LowerBound, 5, RecordsCheck<0x00439AA0>::original);
EI_VERIFY(0x0043CED0, RecordsCheck<0x0043CED0>::LowerBound, 5, RecordsCheck<0x0043CED0>::original);
EI_VERIFY(0x0043CE60, RecordsCheck<0x0043CE60>::Begin, 5, RecordsCheck<0x0043CE60>::original);
EI_VERIFY(0x0043DF00, RecordsCheck<0x0043DF00>::CreateNode, 5, RecordsCheck<0x0043DF00>::original);
EI_VERIFY(0x0043CA80, RecordsCheck<0x0043CA80>::CopySubtree, 5, RecordsCheck<0x0043CA80>::original);
EI_VERIFY(0x0043CB70, RecordsCheck<0x0043CB70>::EraseSubtree, 6, RecordsCheck<0x0043CB70>::original);
EI_VERIFY(0x0043EEE0, RecordsCheck<0x0043EEE0>::InsertAt, 5, RecordsCheck<0x0043EEE0>::original);
EI_VERIFY(0x0043F0A0, RecordsCheck<0x0043F0A0>::InsertUnique, 5, RecordsCheck<0x0043F0A0>::original);
EI_VERIFY(0x0043CBC0, RecordsCheck<0x0043CBC0>::InsertHint, 5, RecordsCheck<0x0043CBC0>::original);
EI_VERIFY(0x00439A70, RecordsCheck<0x00439A70>::InsertHint, 5, RecordsCheck<0x00439A70>::original);
EI_VERIFY(0x0043A090, RecordsCheck<0x0043A090>::MakeValue, 5, RecordsCheck<0x0043A090>::original);
EI_VERIFY(0x0043A780, AttachedCheck<0x0043A780>::ConstructBase, 7, AttachedCheck<0x0043A780>::original);
EI_VERIFY(0x0043A760, AttachedCheck<0x0043A760>::EmptyInitialize, 5, AttachedCheck<0x0043A760>::original);
EI_VERIFY(0x00438440, AttachedCheck<0x00438440>::CopyConstruct, 7, AttachedCheck<0x00438440>::original);
EI_VERIFY(0x00439BB0, AttachedCheck<0x00439BB0>::Clear, 6, AttachedCheck<0x00439BB0>::original);
EI_VERIFY(0x004385A0, AttachedCheck<0x004385A0>::Destroy, 7, AttachedCheck<0x004385A0>::original);
EI_VERIFY(0x00439C10, AttachedCheck<0x00439C10>::FreeHeader, 0, AttachedCheck<0x00439C10>::original);
EI_VERIFY(0x0043F5A0, AttachedCheck<0x0043F5A0>::DeallocateNodes, 6, AttachedCheck<0x0043F5A0>::original);
EI_VERIFY(0x0043B2C0, AttachedCheck<0x0043B2C0>::DeallocateNodesFast, 0, AttachedCheck<0x0043B2C0>::original);
EI_VERIFY(0x00438530, AttachedCheck<0x00438530>::Subscript, 7, AttachedCheck<0x00438530>::original);
EI_VERIFY(0x0043D380, AttachedCheck<0x0043D380>::Find, 5, AttachedCheck<0x0043D380>::original);
EI_VERIFY(0x0043D3E0, AttachedCheck<0x0043D3E0>::LowerBound, 5, AttachedCheck<0x0043D3E0>::original);
EI_VERIFY(0x0043FDA0, AttachedCheck<0x0043FDA0>::Begin, 5, AttachedCheck<0x0043FDA0>::original);
EI_VERIFY(0x0043E020, AttachedCheck<0x0043E020>::CreateNode, 6, AttachedCheck<0x0043E020>::original);
EI_VERIFY(0x0043CF10, AttachedCheck<0x0043CF10>::CopySubtree, 5, AttachedCheck<0x0043CF10>::original);
EI_VERIFY(0x0043D000, AttachedCheck<0x0043D000>::EraseSubtree, 5, AttachedCheck<0x0043D000>::original);
EI_VERIFY(0x0043F1E0, AttachedCheck<0x0043F1E0>::InsertAt, 5, AttachedCheck<0x0043F1E0>::original);
EI_VERIFY(0x0043F3A0, AttachedCheck<0x0043F3A0>::InsertUnique, 5, AttachedCheck<0x0043F3A0>::original);
EI_VERIFY(0x0043D040, AttachedCheck<0x0043D040>::InsertHint, 5, AttachedCheck<0x0043D040>::original);
EI_VERIFY(0x00439B80, AttachedCheck<0x00439B80>::InsertHint, 5, AttachedCheck<0x00439B80>::original);
EI_VERIFY(0x0043FDB0, AttachedCheck<0x0043FDB0>::EraseAt, 5, AttachedCheck<0x0043FDB0>::original);
EI_VERIFY(0x0043F4E0, AttachedCheck<0x0043F4E0>::EraseRange, 6, AttachedCheck<0x0043F4E0>::original);
EI_VERIFY(0x0043D2E0, AttachedCheck<0x0043D2E0>::EraseKey, 7, AttachedCheck<0x0043D2E0>::original);
