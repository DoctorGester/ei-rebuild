// Checks for the footprint code (src/game/ai_footprint.cpp).
#include "verify.h"

#include "../../src/game/ai_map.h"
#include "../../src/lib/crt.h"

#include <cstring>
#include <vector>

namespace {
void* s_unused;

bool SameCell(const FootprintCell& a, const FootprintCell& b) {
    // Byte 0x13 is padding the original leaves uninitialised.
    return a.x == b.x && a.y == b.y && a.low == b.low && a.high == b.high && a.kind == b.kind &&
           a.transparency == b.transparency && a.edge == b.edge && a.next == b.next;
}

// A copy of a set with its own storage, laid out as SetSize allocates it, so original code can
// grow or free it.
FootprintCellSet Clone(const FootprintCellSet& set) {
    FootprintCellSet copy = set;
    if (set.cells) {
        auto raw = reinterpret_cast<std::uintptr_t>(
            gamecrt::operator_new(set.capacity * sizeof(FootprintCell) + 0x24));
        auto* aligned = reinterpret_cast<FootprintCell*>((raw + 0x23) & ~std::uintptr_t{0x1F});
        reinterpret_cast<std::uintptr_t*>(aligned)[-1] = raw;
        std::memcpy(aligned, set.cells, set.count * sizeof(FootprintCell));
        copy.cells = aligned;
    }
    return copy;
}

bool SameSet(const FootprintCellSet& a, const FootprintCellSet& b, int cellsToCompare) {
    if (a.count != b.count || a.capacity != b.capacity || a.growBy != b.growBy || a.hashed != b.hashed) return false;
    if (std::memcmp(a.buckets, b.buckets, sizeof(a.buckets)) != 0) return false;
    if ((a.cells == nullptr) != (b.cells == nullptr)) return false;
    for (int i = 0; i < cellsToCompare; ++i) {
        if (!SameCell(a.cells[i], b.cells[i])) return false;
    }
    return true;
}

// Cloning is linear in the set size; very large sets are only run (ours) to keep the verify build usable.
constexpr int kMaxClonedCells = 4096;
} // namespace

// 005BD350: MOV EAX,[ESP+8]; PUSH EBX (5 bytes). Deep copy: original on the clone, ours on the set.
static void(__fastcall* s_setSize)(FootprintCellSet*, void*, int, int);
static void __fastcall SetSizeVerify(FootprintCellSet* self, void*, int size, int growBy) {
    if (self->count > kMaxClonedCells) return self->SetSize(size, growBy);
    int before = self->count;
    FootprintCellSet clone = Clone(*self);
    s_setSize(&clone, nullptr, size, growBy);
    self->SetSize(size, growBy);
    int kept = before < size ? before : size;
    ei::verify::Check("FootprintCellSet::SetSize", true, SameSet(clone, *self, kept), "%d -> %d (grow %d)", before,
                      size, growBy);
    clone.SetSize(0, -1);
}
EI_VERIFY(0x005BD350, SetSizeVerify, 5, s_setSize);

// 005B2D10: SUB ESP,0x18; PUSH EBX; PUSH EBP (5 bytes). 005B2E10: MOV EDX,[ESP+8]; SUB ESP,0x18 (7 bytes).
using AddFn = void(__fastcall*)(FootprintCellSet*, void*, int, int, int, std::int8_t, std::int8_t);
static AddFn s_addTop, s_addBottom;

static void CheckAdd(const char* name, AddFn original, void (FootprintCellSet::*ours)(int, int, int, std::int8_t, std::int8_t),
                     FootprintCellSet* self, int x, int y, int height, std::int8_t kind, std::int8_t transparency) {
    if (self->count > kMaxClonedCells) return (self->*ours)(x, y, height, kind, transparency);
    FootprintCellSet clone = Clone(*self);
    original(&clone, nullptr, x, y, height, kind, transparency);
    (self->*ours)(x, y, height, kind, transparency);
    ei::verify::Check(name, true, SameSet(clone, *self, self->count), "(%d,%d) z %d, %d cells", x, y, height,
                      self->count);
    clone.SetSize(0, -1);
}

static void __fastcall AddTopVerify(FootprintCellSet* self, void*, int x, int y, int height, std::int8_t kind,
                                    std::int8_t transparency) {
    CheckAdd("FootprintCellSet::AddTop", s_addTop, &FootprintCellSet::AddTop, self, x, y, height, kind, transparency);
}
EI_VERIFY(0x005B2D10, AddTopVerify, 5, s_addTop);

static void __fastcall AddBottomVerify(FootprintCellSet* self, void*, int x, int y, int height, std::int8_t kind,
                                       std::int8_t transparency) {
    CheckAdd("FootprintCellSet::AddBottom", s_addBottom, &FootprintCellSet::AddBottom, self, x, y, height, kind,
             transparency);
}
EI_VERIFY(0x005B2E10, AddBottomVerify, 7, s_addBottom);

// 005B2F40: MOV EAX,[ESP+4]; MOV EDX,[ECX+4] (7 bytes). Output only.
static void(__fastcall* s_getBounds)(const FootprintCellSet*, void*, int*);
static void __fastcall GetBoundsVerify(const FootprintCellSet* self, void*, int* bounds) {
    int expected[4];
    s_getBounds(self, nullptr, expected);
    self->GetBounds(bounds);
    ei::verify::Check("FootprintCellSet::GetBounds", true, std::memcmp(expected, bounds, sizeof(expected)) == 0,
                      "%d cells", self->count);
}
EI_VERIFY(0x005B2F40, GetBoundsVerify, 7, s_getBounds);

// --- MarkFootprint -------------------------------------------------------------------------------
// The original stamps the grid, so it cannot run twice. For non-units: collect our footprint into
// scratch sets first (before the grid changes), then run the original and capture its sets where it
// applies them (ApplyFootprint and ApplyBridges below), and compare. Units only touch the unit
// occupancy layers through original code: ours runs alone.

namespace {
struct Capture {
    bool active = false;
    CAIMap* map = nullptr;
    bool footprintSeen = false, bridgesSeen = false;
    std::vector<FootprintCell> footprint, bridges;
    int footprintBuckets[16], bridgeBuckets[16];
} g_capture;

void Snapshot(const FootprintCellSet* set, std::vector<FootprintCell>& cells, int* buckets) {
    cells.assign(set->cells, set->cells + set->count);
    std::memcpy(buckets, set->buckets, sizeof(set->buckets));
}

bool SameAsSnapshot(const FootprintCellSet& set, const std::vector<FootprintCell>& cells, const int* buckets) {
    if (set.count != static_cast<int>(cells.size())) return false;
    if (std::memcmp(set.buckets, buckets, sizeof(set.buckets)) != 0) return false;
    for (int i = 0; i < set.count; ++i) {
        if (!SameCell(set.cells[i], cells[i])) return false;
    }
    return true;
}

void PrepareScratch(FootprintCellSet* scratch, const FootprintCellSet& real) {
    std::memset(scratch, 0, sizeof(*scratch));
    scratch->growBy = real.growBy;
    scratch->hashed = real.hashed;
    std::memcpy(scratch->buckets, real.buckets, sizeof(real.buckets));
    if (scratch->hashed) {
        for (int& b : scratch->buckets) b = -1;
    }
}
} // namespace

// 005B65B0: MOV AL,[ESP+8]; PUSH EBX (5 bytes); not needed, ours runs alone.
static bool __fastcall ApplyFootprintVerify(CAIMap* self, void*, FootprintCellSet* set, bool remove) {
    if (g_capture.active && g_capture.map == self && set == &self->footprint) {
        Snapshot(set, g_capture.footprint, g_capture.footprintBuckets);
        g_capture.footprintSeen = true;
    }
    bool result = self->ApplyFootprint(set, remove);
    // A stamped cell with a non-empty height range must hold an occluder afterwards.
    bool ok = true;
    if (result && !remove) {
        for (int i = 0; i < set->count && ok; ++i) {
            const FootprintCell& c = set->cells[i];
            if (c.x < 0 || c.y < 0 || c.x >= self->width || c.y >= self->height || !(c.low < c.high)) continue;
            ok = !(self->occluders[c.y][c.x] & 0x100);
        }
    }
    ei::verify::Check("CAIMap::ApplyFootprint", true, ok, "%d cells, remove %d", set->count, remove);
    return result;
}
EI_VERIFY(0x005B65B0, ApplyFootprintVerify, 0, s_unused);

// 005B6740 is not rewritten: a pass-through that captures the bridge set. MOV AL,[ESP+8]; SUB ESP,0x2C (7 bytes).
static bool(__fastcall* s_applyBridges)(CAIMap*, void*, FootprintCellSet*, bool);
static bool __fastcall ApplyBridgesSpy(CAIMap* self, void*, FootprintCellSet* set, bool remove) {
    if (g_capture.active && g_capture.map == self && set == &self->bridges) {
        Snapshot(set, g_capture.bridges, g_capture.bridgeBuckets);
        g_capture.bridgesSeen = true;
    }
    return s_applyBridges(self, nullptr, set, remove);
}
EI_VERIFY(0x005B6740, ApplyBridgesSpy, 7, s_applyBridges);

// 005B6A80: PUSH EBP; MOV EBP,ESP; PUSH -1; PUSH 0x727A76 (10 bytes).
static bool(__fastcall* s_markFootprint)(CAIMap*, void*, CEngineObject*, bool);
static bool __fastcall MarkFootprintVerify(CAIMap* self, void*, CEngineObject* object, bool remove) {
    if (object->GetClassId() == kClassUnit || g_capture.active) return self->MarkFootprint(object, remove);

    FootprintCellSet footprint, bridges;
    PrepareScratch(&footprint, self->footprint);
    PrepareScratch(&bridges, self->bridges);
    bool hasBridge = self->CollectFootprint(object, &footprint, &bridges);

    g_capture.active = true;
    g_capture.map = self;
    g_capture.footprintSeen = g_capture.bridgesSeen = false;
    bool result = s_markFootprint(self, nullptr, object, remove);
    g_capture.active = false;

    bool same = g_capture.footprintSeen && hasBridge == g_capture.bridgesSeen &&
                SameAsSnapshot(footprint, g_capture.footprint, g_capture.footprintBuckets) &&
                (!hasBridge || SameAsSnapshot(bridges, g_capture.bridges, g_capture.bridgeBuckets));
    ei::verify::Check("CAIMap::MarkFootprint", true, same, "remove %d: %d/%d cells, bridges %d/%d", remove,
                      footprint.count, static_cast<int>(g_capture.footprint.size()), hasBridge, g_capture.bridgesSeen);
    footprint.SetSize(0, -1);
    bridges.SetSize(0, -1);
    return result;
}
EI_VERIFY(0x005B6A80, MarkFootprintVerify, 10, s_markFootprint);

// Stamp/clear with refresh, and the refresh batching: stateful, ours only, then check the fields
// they promise to leave.
static bool __fastcall StampFootprintVerify(CAIMap* self, void*, int owner, FootprintCellSet* set) {
    bool refreshUnits = self->refreshUnits;
    bool result = self->StampFootprint(owner, set);
    ei::verify::Check("CAIMap::StampFootprint", true,
                      self->lastFootprintOwner == owner && self->refreshUnits == refreshUnits, "owner %d", owner);
    return result;
}
EI_VERIFY(0x005B7820, StampFootprintVerify, 0, s_unused);

static bool __fastcall ClearFootprintVerify(CAIMap* self, void*, FootprintCellSet* set) {
    bool refreshUnits = self->refreshUnits;
    bool result = self->ClearFootprint(set);
    ei::verify::Check("CAIMap::ClearFootprint", true, self->refreshUnits == refreshUnits, "%d cells", set->count);
    return result;
}
EI_VERIFY(0x005B78F0, ClearFootprintVerify, 0, s_unused);

static void __fastcall BeginRefreshVerify(CAIMap* self, void*) {
    self->BeginRefresh();
    ei::verify::Check("CAIMap::BeginRefresh", true,
                      self->batching && self->dirtyMinX == 10000 && self->dirtyMaxX == 0 && self->dirtyMinY == 10000 &&
                          self->dirtyMaxY == 0,
                      "");
}
EI_VERIFY(0x005B8B30, BeginRefreshVerify, 0, s_unused);

static void __fastcall EndRefreshVerify(CAIMap* self, void*) {
    self->EndRefresh();
    ei::verify::Check("CAIMap::EndRefresh", true, !self->batching, "");
}
EI_VERIFY(0x005B8B80, EndRefreshVerify, 0, s_unused);
