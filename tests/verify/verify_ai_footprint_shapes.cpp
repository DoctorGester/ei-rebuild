// Checks for the wall and cloud footprints (src/game/ai_footprint_shapes.cpp). Differential: the
// original fills the set, its cells are kept, ours fills it again; the cell lists (without byte
// 0x13 and `next`, which the original leaves as stack garbage), the buckets and the hashed flag
// are compared.
#include "verify.h"

#include "../../src/game/ai_map.h"

#include <cstring>
#include <vector>

void __fastcall FootprintAlongDirection(FootprintCellSet* set, void*, CAIMap* map, const Vector3* position, float width,
                                        const Vector3* direction);
void __fastcall FootprintDiscAt(FootprintCellSet* set, void*, CAIMap* map, const Vector3* position, float radius);

namespace {
using AlongFn = void(__fastcall*)(FootprintCellSet*, void*, CAIMap*, const Vector3*, float, const Vector3*);
using DiscFn = void(__fastcall*)(FootprintCellSet*, void*, CAIMap*, const Vector3*, float);
AlongFn s_originalAlong;
DiscFn s_originalDisc;

struct Snapshot {
    std::vector<FootprintCell> cells;
    int buckets[16];
    bool hashed;
};

Snapshot Take(const FootprintCellSet* set) {
    Snapshot s;
    for (int i = 0; i < set->cells.size; ++i) {
        FootprintCell c = set->cells[i];
        c.padding = 0;
        c.next = 0;
        s.cells.push_back(c);
    }
    std::memcpy(s.buckets, set->buckets, sizeof(s.buckets));
    s.hashed = set->hashed;
    return s;
}

bool Same(const Snapshot& a, const Snapshot& b) {
    return a.cells.size() == b.cells.size() &&
           (a.cells.empty() || std::memcmp(a.cells.data(), b.cells.data(), a.cells.size() * sizeof(FootprintCell)) == 0) &&
           std::memcmp(a.buckets, b.buckets, sizeof(a.buckets)) == 0 && a.hashed == b.hashed;
}
} // namespace

// 005B2A00.
static void __fastcall FootprintAlongVerify(FootprintCellSet* set, void*, CAIMap* map, const Vector3* position,
                                            float width, const Vector3* direction) {
    s_originalAlong(set, nullptr, map, position, width, direction);
    Snapshot expected = Take(set);
    FootprintAlongDirection(set, nullptr, map, position, width, direction);
    Snapshot actual = Take(set);
    ei::verify::Check("FootprintAlongDirection", true, Same(expected, actual), "%d cells (original %d)",
                      static_cast<int>(actual.cells.size()), static_cast<int>(expected.cells.size()));
}
EI_VERIFY(0x005B2A00, FootprintAlongVerify, 6, s_originalAlong);

// 005B2760.
static void __fastcall FootprintDiscVerify(FootprintCellSet* set, void*, CAIMap* map, const Vector3* position,
                                           float radius) {
    s_originalDisc(set, nullptr, map, position, radius);
    Snapshot expected = Take(set);
    FootprintDiscAt(set, nullptr, map, position, radius);
    Snapshot actual = Take(set);
    ei::verify::Check("FootprintDiscAt", true, Same(expected, actual), "%d cells (original %d)",
                      static_cast<int>(actual.cells.size()), static_cast<int>(expected.cells.size()));
}
EI_VERIFY(0x005B2760, FootprintDiscVerify, 6, s_originalDisc);
