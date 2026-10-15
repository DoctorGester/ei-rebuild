// Checks for the AI's static initialisers (A-W15): the path smoother's transition tables
// (src/game/ai_path_smooth.cpp), the sector spiral (src/game/sector_map.cpp) and the unit shapes'
// cache (src/game/ai_shape_cache.cpp).
//
// The originals run among the game's static initialisers, before the hooks are installed, so no
// wrapper ever sees them. What they built is still untouched when the hooks go in (the entry of
// WinMain): each check below runs once at that point (EI_STARTUP_CHECK), builds the same thing with
// our code into storage of its own and compares the two.
//
//   - the smooth tables: all 81, each array's size, allocated size, growth step and elements;
//   - the spiral: the 441 offsets in order (the order of sectors at the same distance is the game's
//     qsort's, which our builder calls too), the lengths per distance, the tree's depth, mask and
//     keys. The distances are square roots stored as floats: both sides compute them at the x87
//     precision the process starts with, which is why the check runs here and not later (Direct3D
//     lowers the precision when the device is created);
//   - the shape cache: an empty hash map of 193 buckets under the cache's vtable. Its functor bytes
//     are stack garbage in the original and are not compared.
//
// Every game start reaches them; no scenario is needed. The exit halves (SmoothTable::Destruct,
// UnitShapeCache::Destruct) run in the game when it quits, as ours only; here they free the checks'
// own tables, the cache's with no shapes in it.
#include "verify.h"

#include "../../src/game/ai_map.h"
#include "../../src/game/ai_shape_cache.h"
#include "../../src/game/sector_map.h"
#include "../../src/lib/mfc.h"

#include <cstddef>
#include <cstring>

namespace {

// The same array: its header (the vtable too) and its elements. The element types here have no
// padding, and every element up to `size` is written by the builders.
template <typename T, typename E>
bool SameArray(const mfc::CArray<T, E>& expected, const mfc::CArray<T, E>& actual) {
    if (expected.vtable != actual.vtable || expected.size != actual.size || expected.maxSize != actual.maxSize ||
        expected.growBy != actual.growBy)
        return false;
    if (expected.size <= 0) return expected.size == 0 && !expected.data == !actual.data;
    return expected.data && actual.data &&
           std::memcmp(expected.data, actual.data, static_cast<std::size_t>(expected.size) * sizeof(T)) == 0;
}

constexpr int kSmoothTables = 81;
constexpr int kSpiralOffsets = 441;
constexpr std::size_t kShapeBuckets = 193;

} // namespace

// 005C4210 (the tables' constructor; MOV EAX,ECX; MOV EDX,imm32: 7). Only the static initialiser's
// vector constructor calls it, before the hooks; the start-up check below drives it once. The
// original constructs a probe, ours the table: every byte and the returned this compared.
static SmoothTable*(__fastcall* s_constructTable)(SmoothTable*);
static SmoothTable* __fastcall ConstructTableVerify(SmoothTable* self) {
    alignas(SmoothTable) unsigned char scratch[sizeof(SmoothTable)];
    std::memset(scratch, 0xCD, sizeof(scratch));
    auto* probe = reinterpret_cast<SmoothTable*>(scratch);
    SmoothTable* expected = s_constructTable(probe);
    std::memset(static_cast<void*>(self), 0xCD, sizeof(SmoothTable));
    SmoothTable* actual = self->Construct();
    ei::verify::Check("SmoothTable::Construct", true,
                      expected == probe && actual == self && std::memcmp(probe, self, sizeof(SmoothTable)) == 0, "%p", self);
    return actual;
}
EI_VERIFY(0x005C4210, ConstructTableVerify, 7, s_constructTable);

namespace {

void SmoothTablesStartupCheck() {
    static SmoothTable ours[kSmoothTables];
    ConstructTableVerify(&ours[0]);
    for (int i = 1; i < kSmoothTables; ++i) ours[i].Construct();
    BuildSmoothTablesAt(ours);

    const SmoothTable* theirs = SmoothTables();
    int wrong = 0, first = -1;
    int copies = 0, steps = 0, chained = 0;
    for (int i = 0; i < kSmoothTables; ++i) {
        copies += theirs[i].copies.size;
        steps += theirs[i].steps.size;
        chained += theirs[i].chained.size;
        if (SameArray(theirs[i].copies, ours[i].copies) && SameArray(theirs[i].steps, ours[i].steps) &&
            SameArray(theirs[i].chained, ours[i].chained))
            continue;
        ++wrong;
        if (first < 0) first = i;
    }
    const int at = first < 0 ? 0 : first;
    // Tables with nothing in them would compare equal: the original's must hold something.
    ei::verify::Check("static initialiser: smooth tables 005C42B0", true, wrong == 0 && copies > 0 && steps > 0 && chained > 0,
                      "%d of 81 tables differ, the first %d (sizes %d %d %d / ours %d %d %d); the original's totals %d %d %d", wrong,
                      first, theirs[at].copies.size, theirs[at].steps.size, theirs[at].chained.size, ours[at].copies.size,
                      ours[at].steps.size, ours[at].chained.size, copies, steps, chained);
    for (SmoothTable& table : ours) table.Destruct();
}
EI_STARTUP_CHECK(SmoothTablesStartupCheck);

void SectorSpiralStartupCheck() {
    const mfc::CArray<int>* theirLengths = SpiralLengthArray();
    const RadiusTree* theirTree = SpiralRadiusTree();
    const mfc::CArray<SpiralOffset>* theirOffsets = SpiralOffsetArray();

    // Ours start as the original's did: empty arrays under the same vtables.
    mfc::CArray<int> lengths;
    RadiusTree tree;
    mfc::CArray<SpiralOffset> offsets;
    lengths.Construct(reinterpret_cast<std::uintptr_t>(theirLengths->vtable));
    tree.keys.Construct(reinterpret_cast<std::uintptr_t>(theirTree->keys.vtable));
    tree.depth = 0;
    tree.mask = 0;
    offsets.Construct(reinterpret_cast<std::uintptr_t>(theirOffsets->vtable));
    BuildSectorSpiralAt(&lengths, &tree, &offsets);

    const bool sameOffsets = SameArray(*theirOffsets, offsets) && offsets.size == kSpiralOffsets;
    const bool sameLengths = SameArray(*theirLengths, lengths);
    const bool sameTree = theirTree->depth == tree.depth && theirTree->mask == tree.mask && SameArray(theirTree->keys, tree.keys);
    ei::verify::Check("static initialiser: sector spiral 005BF8F0", true, sameOffsets && sameLengths && sameTree,
                      "offsets %d (%d / %d, allocated %d / %d) lengths %d (%d / %d, allocated %d / %d) tree %d (depth %d / %d, "
                      "keys %d / %d)",
                      sameOffsets, theirOffsets->size, offsets.size, theirOffsets->maxSize, offsets.maxSize, sameLengths,
                      theirLengths->size, lengths.size, theirLengths->maxSize, lengths.maxSize, sameTree, theirTree->depth,
                      tree.depth, theirTree->keys.size, tree.keys.size);
    lengths.FreeData();
    tree.keys.FreeData();
    offsets.FreeData();
}
EI_STARTUP_CHECK(SectorSpiralStartupCheck);

void UnitShapeCacheStartupCheck() {
    // Raw storage: the class's C++ vtable is never instantiated (Construct stores the game's).
    alignas(UnitShapeCache) static unsigned char storage[sizeof(UnitShapeCache)];
    UnitShapeCache& ours = *reinterpret_cast<UnitShapeCache*>(storage);
    ours.Construct();
    const UnitShapeCache* theirs = UnitShapes();
    void* theirVtable = *reinterpret_cast<void* const*>(theirs);
    void* ourVtable = *reinterpret_cast<void* const*>(&ours);
    const bool same = theirVtable == ourVtable && theirs->shapes.BucketCount() == kShapeBuckets &&
                      ours.shapes.BucketCount() == kShapeBuckets && theirs->shapes.Size() == 0 && ours.shapes.Size() == 0 &&
                      !theirs->shapes.First() && !ours.shapes.First();
    ei::verify::Check("static initialiser: unit shape cache 005B32E0", true, same,
                      "vtable %p / %p buckets %u / %u shapes %u / %u", theirVtable, ourVtable,
                      static_cast<unsigned>(theirs->shapes.BucketCount()), static_cast<unsigned>(ours.shapes.BucketCount()),
                      static_cast<unsigned>(theirs->shapes.Size()), static_cast<unsigned>(ours.shapes.Size()));
    ours.Destruct();
}
EI_STARTUP_CHECK(UnitShapeCacheStartupCheck);

} // namespace
