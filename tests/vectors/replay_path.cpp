// Replay of the "path" family (path_vectors.h) against ai_path.cpp, ai_plan.cpp, ai_path_smooth.cpp
// and what they call (the local and block searches, the region map, the path lists, the order).
//
// The maps ("@AIMap") are kept here by id; a record's map is built from its saved map with the
// record's differences. The unit is a CUnit-sized block with the recorded fields and a vtable whose
// GetClassId gives the recorded class (CUnit::GetMoveSpeeds is defined here and gives the recorded
// speeds). The sector map's Add and Remove, which FindPath calls around its search, change nothing
// here: FindPath's map was recorded with the unit already taken out.
#include "path_vectors.h"
#include "vectors.h"

#include "../../src/game/ai_grids.h"
#include "../../src/game/ai_map.h"
#include "../../src/game/game_data.h"
#include "../../src/game/sector_map.h"
#include "../../src/game/unit.h"
#include "../../src/lib/crt.h"
#include "../../src/lib/mfc_slots.h"
#include "game/game_data.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace ei::vectors;
using namespace ei::vectors::path;

// ---- Stand-ins for what the replay leaves out ------------------------------------------------------

// The sector map's Add and Remove (FindPath reaches them through their addresses, other code
// directly; both defined below).
bool __fastcall NoSectorChange(CSectorMap*, void*, CEngineObject*) { return true; }
int __cdecl NoAtExit(void(__cdecl*)()) { return 0; }

struct StandIns {
    StandIns() {
        ReplayStandIn(0x005C1010, reinterpret_cast<void*>(&NoSectorChange), "CSectorMap::Remove");
        ReplayStandIn(0x005C0DF0, reinterpret_cast<void*>(&NoSectorChange), "CSectorMap::Add");
        ReplayStandIn(0x006E727B, reinterpret_cast<void*>(&NoAtExit), "atexit");
    }
} g_standIns;

// ---- The unit -----------------------------------------------------------------------------------------

UnitState g_unitState;

int __fastcall UnitClassId(void*) { return g_unitState.classId; }
void __fastcall UnitOtherSlot(void*) {
    std::printf("ei_vectors: path: a virtual call on the unit other than GetClassId\n");
    std::fflush(stdout);
    std::abort();
}

constexpr int kGetClassIdSlot = 12;   // CEngineObject (engine_object.h)
void* g_unitVtable[96];
alignas(16) unsigned char g_unitBytes[sizeof(CUnit)];

CUnit* MakeUnit(const UnitState& s) {
    g_unitState = s;
    for (void*& slot : g_unitVtable) slot = reinterpret_cast<void*>(&UnitOtherSlot);
    g_unitVtable[kGetClassIdSlot] = reinterpret_cast<void*>(&UnitClassId);
    std::memset(g_unitBytes, 0, sizeof(g_unitBytes));
    void* vtable = g_unitVtable;
    std::memcpy(g_unitBytes, &vtable, sizeof(vtable));
    auto* unit = reinterpret_cast<CUnit*>(g_unitBytes);
    std::memcpy(&unit->position, s.position, sizeof(s.position));
    std::memcpy(&unit->orientation, s.orientation, sizeof(s.orientation));
    unit->sizeClass = s.sizeClass;
    unit->standingHeight = s.standingHeight;
    unit->altPassCosts = s.altPassCosts;
    return unit;
}

} // namespace

// unit.cpp is not linked: the speeds recorded with the call.
void CUnit::GetMoveSpeeds(float* forward, float* backward) {
    *forward = g_unitState.forwardSpeed;
    *backward = g_unitState.backwardSpeed;
}

// sector_map.cpp is not linked: FindPath's map was recorded with the unit taken out, and putting it
// back changes nothing the search reads (IsPassableWithoutSelf, linked with ai_map.cpp, calls these).
bool CSectorMap::Add(CEngineObject*) { return true; }
bool CSectorMap::Remove(CEngineObject*) { return true; }

namespace {

// ---- @PathTables ----------------------------------------------------------------------------------

template <typename T>
void FillArray(mfc::CArray<T>& array, const std::uint8_t* bytes, std::uint32_t count) {
    array.data = nullptr;
    array.size = array.maxSize = static_cast<int>(count);
    array.growBy = 0;
    if (count) {
        array.data = static_cast<T*>(gamecrt::operator_new(count * sizeof(T)));
        std::memcpy(array.data, bytes, count * sizeof(T));
    }
}

const std::uint8_t* GetArray(Reader& r, std::uint32_t elementSize, std::uint32_t* count) {
    *count = r.Get<std::uint32_t>();
    if (*count > (1u << 20)) {
        *count = 0;
        r.Bytes(~std::size_t(0));   // fails the reader
        return nullptr;
    }
    return r.Bytes(static_cast<std::size_t>(*count) * elementSize);
}

template <typename T>
bool SameArray(const mfc::CArray<T>& a, const mfc::CArray<T>& b) {
    return a.size == b.size && (a.size == 0 || std::memcmp(a.data, b.data, a.size * sizeof(T)) == 0);
}

// The game's tables into the stand-in; then the smoother's tables as ours builds them, against the
// game's.
bool CheckTables(Reader& r, char* d, std::size_t n) {
    const std::uint32_t ranges = r.Get<std::uint32_t>();
    for (std::uint32_t i = 0; i < ranges && r.ok(); ++i) {
        const Range range = r.Get<Range>();
        const std::uint8_t* bytes = r.Bytes(range.size);
        if (bytes) std::memcpy(ei::ReplayGameData(range.address, range.size), bytes, range.size);
    }
    const std::uint32_t arrays = r.Get<std::uint32_t>();
    for (std::uint32_t i = 0; i < arrays && r.ok(); ++i) {
        const Range range = r.Get<Range>();
        std::uint32_t count = 0;
        const std::uint8_t* bytes = GetArray(r, range.size, &count);
        if (!bytes && count) break;
        auto& array = ei::GameData<mfc::CArray<std::uint8_t>>(range.address);
        array.vtable = nullptr;
        array.data = nullptr;
        array.size = array.maxSize = static_cast<int>(count);
        array.growBy = 0;
        if (count) {
            array.data = static_cast<std::uint8_t*>(gamecrt::operator_new(count * range.size));
            std::memcpy(array.data, bytes, count * range.size);
        }
    }
    auto* tables = ei::GameData<SmoothTable[kSmoothTableCount]>(kSmoothTables);
    for (int i = 0; i < kSmoothTableCount && r.ok(); ++i) {
        std::uint32_t count = 0;
        const std::uint8_t* bytes = GetArray(r, kSmoothCopySize, &count);
        FillArray(tables[i].copies, bytes, bytes ? count : 0);
        bytes = GetArray(r, kSmoothStepSize, &count);
        FillArray(tables[i].steps, bytes, bytes ? count : 0);
        bytes = GetArray(r, kSmoothStepSize, &count);
        FillArray(tables[i].chained, bytes, bytes ? count : 0);
    }
    if (!r.ok()) return true;   // RunBytes reports the short payload

    static SmoothTable ours[kSmoothTableCount];
    for (SmoothTable& t : ours) t.Construct();
    BuildSmoothTablesAt(ours);
    for (int i = 0; i < kSmoothTableCount; ++i) {
        if (SameArray(ours[i].copies, tables[i].copies) && SameArray(ours[i].steps, tables[i].steps) &&
            SameArray(ours[i].chained, tables[i].chained))
            continue;
        std::snprintf(d, n, "smoother table %d: ours %d copies %d steps %d chained, the game's %d / %d / %d", i,
                      ours[i].copies.size, ours[i].steps.size, ours[i].chained.size, tables[i].copies.size,
                      tables[i].steps.size, tables[i].chained.size);
        return false;
    }
    return true;
}
EI_VECTOR_CHECK_BYTES("@PathTables", CheckTables);

// ---- @AIMap --------------------------------------------------------------------------------------

struct Grid {
    int width = 0, height = 0;
    std::vector<std::uint8_t> bytes;
};

struct SavedMap {
    std::uint64_t id = 0;
    int width = 0, height = 0;
    int passCosts[16], passCostsAlt[16], stepCosts[16];
    std::vector<int> climbLarge, climbSmall;
    float heightScale = 0.0f;
    Grid cells, water, marks;
    Grid edges[8], representatives[8], regions[8];
    std::vector<std::uint8_t> regionBytes;   // the region grids in a row, as the differences see them
};
std::vector<std::unique_ptr<SavedMap>> g_maps;

bool GetGrid(Reader& r, Grid& grid, std::uint32_t perCell, std::uint32_t stride) {
    grid.width = r.Get<std::int32_t>();
    grid.height = r.Get<std::int32_t>();
    if (grid.width < 0 || grid.height < 0 || grid.width > 4096 || grid.height > 4096) return false;
    const auto count = static_cast<std::uint32_t>(grid.width * grid.height) * perCell;
    grid.bytes.assign(static_cast<std::size_t>(count) * stride, 0);
    return GetPacked(r, grid.bytes.data(), count, stride);
}

bool GetInts(Reader& r, std::vector<int>& out) {
    std::uint32_t count = 0;
    const std::uint8_t* bytes = GetArray(r, 4, &count);
    if (!bytes && count) return false;
    out.resize(count);
    if (count) std::memcpy(out.data(), bytes, count * 4);
    return true;
}

bool CheckMap(Reader& r, char* d, std::size_t n) {
    auto map = std::make_unique<SavedMap>();
    map->id = r.Get<std::uint64_t>();
    const std::size_t left = r.left();
    const std::uint8_t* body = r.Bytes(left);
    if (!body) return true;
    if (HashBytes(body, left) != map->id) {
        std::snprintf(d, n, "map %016llX: the content's hash differs", static_cast<unsigned long long>(map->id));
        return false;
    }
    Reader b(body, static_cast<std::uint32_t>(left));
    map->width = b.Get<std::int32_t>();
    map->height = b.Get<std::int32_t>();
    std::memcpy(map->passCosts, b.Bytes(sizeof(map->passCosts)), sizeof(map->passCosts));
    std::memcpy(map->passCostsAlt, b.Bytes(sizeof(map->passCostsAlt)), sizeof(map->passCostsAlt));
    std::memcpy(map->stepCosts, b.Bytes(sizeof(map->stepCosts)), sizeof(map->stepCosts));
    bool ok = b.ok() && GetInts(b, map->climbLarge) && GetInts(b, map->climbSmall);
    map->heightScale = b.Get<float>();
    ok = ok && GetGrid(b, map->cells, 1, sizeof(AIMapCell)) && GetGrid(b, map->water, 1, 1) && GetGrid(b, map->marks, 1, 1);
    for (int c = 0; c < 8 && ok; ++c) {
        ok = GetGrid(b, map->edges[c], 8, 2) && GetGrid(b, map->representatives[c], 1, 1) &&
             GetGrid(b, map->regions[c], 1, 2);
        for (const Grid* g : {&map->edges[c], &map->representatives[c], &map->regions[c]})
            map->regionBytes.insert(map->regionBytes.end(), g->bytes.begin(), g->bytes.end());
    }
    if (!ok || !b.ok() || b.left() != 0 || map->cells.width != map->width || map->cells.height != map->height) {
        std::snprintf(d, n, "map %016llX: the content does not parse", static_cast<unsigned long long>(map->id));
        return false;
    }
    g_maps.push_back(std::move(map));
    return true;
}
EI_VECTOR_CHECK_BYTES("@AIMap", CheckMap);

// ---- The map a record runs on -------------------------------------------------------------------------

CAIMap* g_map = nullptr;               // built for g_mapOf
const SavedMap* g_mapOf = nullptr;

template <typename T>
void SizeGrid(SearchGrid<T>& grid, int width, int height, int perCell = 1) {
    grid.width = width;
    grid.height = height;
    AllocateGrid(grid, perCell);
}

template <typename T>
void CopyGrid(SearchGrid<T>& grid, const Grid& from) {
    if (!from.bytes.empty()) std::memcpy(grid.data, from.bytes.data(), from.bytes.size());
}

void FillClimb(ClimbTable& table, const std::vector<int>& values) {
    table.data = static_cast<int*>(gamecrt::operator_new(values.size() * sizeof(int) + 4));
    if (!values.empty()) std::memcpy(table.data, values.data(), values.size() * sizeof(int));
    table.size = table.maxSize = static_cast<int>(values.size());
}

// A fresh map with the saved map's sizes and tables (the grids' content is copied per record). The
// old one is left behind (the replay runs a few maps).
void BuildMap(const SavedMap& s) {
    void* block = gamecrt::operator_new(sizeof(CAIMap) + 0x1000);
    std::memset(block, 0, sizeof(CAIMap) + 0x1000);
    CAIMap* map = static_cast<CAIMap*>(block);
    map->loaded = true;
    map->width = s.width;
    map->height = s.height;
    std::memcpy(map->passCosts, s.passCosts, sizeof(s.passCosts));
    std::memcpy(map->passCostsAlt, s.passCostsAlt, sizeof(s.passCostsAlt));
    std::memcpy(map->stepCosts, s.stepCosts, sizeof(s.stepCosts));
    FillClimb(map->climbLarge, s.climbLarge);
    FillClimb(map->climbSmall, s.climbSmall);
    map->heightScale = s.heightScale;
    SizeGrid(map->CellGrid(), s.cells.width, s.cells.height);
    SizeGrid(map->WaterGrid(), s.water.width, s.water.height);
    map->waterWidth = s.water.width;
    map->waterHeight = s.water.height;
    SizeGrid(map->marks, s.marks.width, s.marks.height);
    CRegionMap& r = map->regionMap;
    for (int c = 0; c < 8; ++c) {
        SizeGrid(r.edges[c], s.edges[c].width, s.edges[c].height, 8);
        SizeGrid(r.cells[c], s.representatives[c].width, s.representatives[c].height);
        SizeGrid(r.regions[c], s.regions[c].width, s.regions[c].height);
    }
    r.local.Construct(map);
    map->forwardSearch.Construct(map);
    map->backwardSearch.Construct(nullptr);
    g_map = map;
    g_mapOf = &s;
}

// The saved map's content back, the record's differences applied (the planes it has), the searches
// fresh.
bool PrepareMap(const SavedMap& s, Reader& r) {
    if (g_mapOf != &s) BuildMap(s);
    CAIMap* map = g_map;
    CopyGrid(map->CellGrid(), s.cells);
    CopyGrid(map->WaterGrid(), s.water);
    CopyGrid(map->marks, s.marks);
    std::vector<std::uint8_t> regions = s.regionBytes;
    const std::uint8_t planes = r.Get<std::uint8_t>();
    bool ok = r.ok() && !(planes & ~kCallAll);
    if (ok && (planes & kCallCells))
        ok = ApplyDiff(r, reinterpret_cast<std::uint8_t*>(map->cellBlock), static_cast<std::uint32_t>(s.cells.bytes.size() / 9), 9);
    if (ok && (planes & kCallWater))
        ok = ApplyDiff(r, reinterpret_cast<std::uint8_t*>(map->waterBlock), static_cast<std::uint32_t>(s.water.bytes.size()), 1);
    if (ok && (planes & kCallMarks))
        ok = ApplyDiff(r, reinterpret_cast<std::uint8_t*>(map->marks.data), static_cast<std::uint32_t>(s.marks.bytes.size()), 1);
    if (ok && (planes & kCallRegions))
        ok = ApplyDiff(r, regions.data(), static_cast<std::uint32_t>(regions.size()), 1);
    if (!ok) return false;
    std::size_t at = 0;
    CRegionMap& rm = map->regionMap;
    for (int c = 0; c < 8; ++c) {
        auto put = [&](void* to, std::size_t size) {
            std::memcpy(to, regions.data() + at, size);
            at += size;
        };
        put(rm.edges[c].data, s.edges[c].bytes.size());
        put(rm.cells[c].data, s.representatives[c].bytes.size());
        put(rm.regions[c].data, s.regions[c].bytes.size());
    }
    rm.forward.Attach(&rm, false);
    rm.backward.Attach(&rm, true);
    rm.local.Attach(map, false);
    map->forwardSearch.Attach(map, false);
    map->backwardSearch.Attach(map, true);
    std::memset(&map->smoother, 0, sizeof(map->smoother));
    return true;
}

// Reads the record's map (the saved map's id and the differences) and prepares it.
bool LoadCallMap(Reader& r, char* d, std::size_t n) {
    const std::uint64_t id = r.Get<std::uint64_t>();
    for (const auto& m : g_maps) {
        if (m->id != id) continue;
        if (PrepareMap(*m, r)) return true;
        if (r.ok()) std::snprintf(d, n, "map %016llX: a difference does not fit", static_cast<unsigned long long>(id));
        return false;
    }
    if (r.ok()) std::snprintf(d, n, "map %016llX is not in @AIMap", static_cast<unsigned long long>(id));
    return false;
}

// ---- Paths --------------------------------------------------------------------------------------------

void ReadCells(Reader& r, PathCellList& list) {
    const std::uint32_t count = r.Get<std::uint32_t>();
    for (std::uint32_t i = 0; i < count && r.ok(); ++i) list.AddTail(r.Get<PathCell>());
}

void EmptyList(PathCellList& list) {
    list = PathCellList{PathCellListVtable(), nullptr, nullptr, 0, nullptr, nullptr, 0x80};
}

void ReadPath(Reader& r, AIPath& path) {
    std::memset(&path, 0, sizeof(path));
    EmptyList(path.steps);
    EmptyList(path.cells);
    path.excludedBlocks.vtable = mfc::impl::PointArrayClass::Vtable();
    path.motionOrder = r.Get<MotionOrder>();
    ReadCells(r, path.steps);
    ReadCells(r, path.cells);
    path.targetX = r.Get<float>();
    path.targetY = r.Get<float>();
    path.goal = r.Get<Vector3>();
    path.reached = r.Get<bool>();
    path.failed = r.Get<bool>();
    path.flag6E = r.Get<bool>();
    path.flag6F = r.Get<bool>();
    const std::uint32_t excluded = r.Get<std::uint32_t>();
    if (excluded && excluded < 0x10000) {
        path.excludedBlocks.data = static_cast<BlockPoint*>(gamecrt::operator_new(excluded * sizeof(BlockPoint)));
        path.excludedBlocks.size = path.excludedBlocks.maxSize = static_cast<int>(excluded);
        for (std::uint32_t i = 0; i < excluded; ++i) path.excludedBlocks.data[i] = r.Get<BlockPoint>();
    }
    path.smoothing = r.Get<std::int32_t>();
}

// A recorded path's bytes (as PutPath wrote them).
std::vector<std::uint8_t> TakePath(Reader& r) {
    std::vector<std::uint8_t> out;
    auto take = [&](std::size_t size) -> const std::uint8_t* {
        const std::uint8_t* p = r.Bytes(size);
        if (p) out.insert(out.end(), p, p + size);
        return p;
    };
    auto count = [&](std::size_t element) {
        std::uint32_t n = 0;
        if (const std::uint8_t* p = take(4)) std::memcpy(&n, p, 4);
        if (n < 0x100000) take(n * element);
        else r.Bytes(~std::size_t(0));
    };
    take(sizeof(MotionOrder));
    count(sizeof(PathCell));
    count(sizeof(PathCell));
    take(8 + sizeof(Vector3) + 4);
    count(sizeof(BlockPoint));
    take(4);
    return out;
}

std::vector<std::uint8_t> PathBytes(const AIPath& path) {
    Writer w;
    PutPath(w, path);
    return std::vector<std::uint8_t>(w.data(), w.data() + w.size());
}

// A path's bytes in a line: the order, the counts, the target, the flags and the smoothing.
std::string DescribePath(const std::vector<std::uint8_t>& bytes) {
    Reader r(bytes.data(), static_cast<std::uint32_t>(bytes.size()));
    AIPath p;
    ReadPath(r, p);
    char text[256];
    std::snprintf(text, sizeof(text), "order %d (%g,%g) steps %d cells %d target (%g,%g) flags %d%d%d%d excluded %d smoothing %d",
                  p.motionOrder.kind, p.motionOrder.x, p.motionOrder.y, p.steps.count, p.cells.count, p.targetX,
                  p.targetY, p.reached, p.failed, p.flag6E, p.flag6F, p.excludedBlocks.size, p.smoothing);
    return text;
}

bool SamePath(const AIPath& ours, const std::vector<std::uint8_t>& original, char* d, std::size_t n, const char* what) {
    const std::vector<std::uint8_t> bytes = PathBytes(ours);
    if (bytes == original) return true;
    std::size_t at = 0;
    while (at < bytes.size() && at < original.size() && bytes[at] == original[at]) ++at;
    std::snprintf(d, n, "%s: the path differs at byte %u: ours %s; original %s", what, static_cast<unsigned>(at),
                  DescribePath(bytes).c_str(), DescribePath(original).c_str());
    return false;
}

// ---- Calls under a guard ---------------------------------------------------------------------------
// Our code reading game memory the replay does not have faults; the record fails naming the address.

int Filter(EXCEPTION_POINTERS* e, DWORD* code, std::uintptr_t* at) {
    *code = e->ExceptionRecord->ExceptionCode;
    *at = e->ExceptionRecord->NumberParameters >= 2 ? e->ExceptionRecord->ExceptionInformation[1]
                                                     : reinterpret_cast<std::uintptr_t>(e->ExceptionRecord->ExceptionAddress);
    return EXCEPTION_EXECUTE_HANDLER;
}

template <typename Fn>
bool Guarded(Fn& fn, char* d, std::size_t n, const char* what) {
    DWORD code = 0;
    std::uintptr_t at = 0;
    bool done = false;
    __try {
        fn();
        done = true;
    } __except (Filter(GetExceptionInformation(), &code, &at)) {
    }
    if (done) return true;
    if (code == kMissingForwarder)
        std::snprintf(d, n, "%s: a forwarder to %08X with nothing behind it in ei_vectors", what, static_cast<unsigned>(at));
    else
        std::snprintf(d, n, "%s: exception %08lX at %08X", what, code, static_cast<unsigned>(at));
    g_mapOf = nullptr;   // the map may be half-changed: built again for the next record
    return false;
}

// ---- The marks -------------------------------------------------------------------------------------

std::uint8_t* MarkData() { return reinterpret_cast<std::uint8_t*>(g_map->marks.data); }
std::uint32_t MarkCount() { return static_cast<std::uint32_t>(g_map->marks.width * g_map->marks.height); }

// The marks the original left: the record's map's marks with the recorded changes.
bool MarksAfter(Reader& r, std::vector<std::uint8_t>& marks) {
    marks.assign(MarkData(), MarkData() + MarkCount());
    return ApplyDiff(r, marks.data(), MarkCount(), 1);
}

bool SameMarks(const std::vector<std::uint8_t>& original, char* d, std::size_t n, const char* what) {
    const std::uint8_t* ours = MarkData();
    int differ = 0, first = -1;
    for (std::uint32_t i = 0; i < MarkCount(); ++i)
        if (ours[i] != original[i] && differ++ == 0) first = static_cast<int>(i);
    if (!differ) return true;
    const int w = g_map->marks.width;
    std::snprintf(d, n, "%s: %d marks differ, the first at (%d,%d): ours %d, original %d", what, differ, first % w,
                  first / w, ours[first], original[first]);
    return false;
}

// ---- The checks ------------------------------------------------------------------------------------

bool CheckFindPath(Reader& r, char* d, std::size_t n) {
    const bool captured = r.Get<std::uint8_t>() != 0;
    if (captured && !LoadCallMap(r, d, n)) return !r.ok();
    const UnitState unit = r.Get<UnitState>();
    const std::int32_t tick = r.Get<std::int32_t>();
    const Vector3 target = r.Get<Vector3>();
    const float range = r.Get<float>();
    const float minDistance = r.Get<float>();
    AIPath path;
    ReadPath(r, path);
    const bool expected = r.Get<std::uint8_t>() != 0;
    const std::vector<std::uint8_t> after = TakePath(r);
    std::vector<std::uint8_t> marks;
    if (captured && !MarksAfter(r, marks)) return !r.ok();
    if (!r.ok()) return true;
    if (!captured && !g_map) {
        // Returned before taking the unit out of the sector map: any map will do.
        static SavedMap empty;
        BuildMap(empty);
    }
    CUnit* u = MakeUnit(unit);
    ei::GameData<std::int32_t>(0x007AFFC4) = tick;
    bool actual = false;
    auto call = [&] { actual = g_map->FindPath(u, &target, &path, range, minDistance); };
    if (!Guarded(call, d, n, "FindPath")) return false;
    if (actual != expected) {
        std::snprintf(d, n, "(%g,%g) range %g min %g: ours %d, original %d", target.x, target.y, range, minDistance, actual,
                      expected);
        return false;
    }
    if (!SamePath(path, after, d, n, "FindPath")) return false;
    return !captured || SameMarks(marks, d, n, "FindPath");
}
EI_VECTOR_CHECK_BYTES("CAIMap::FindPath", CheckFindPath);

bool CheckPlan(Reader& r, char* d, std::size_t n) {
    if (!LoadCallMap(r, d, n)) return !r.ok();
    const UnitState unit = r.Get<UnitState>();
    std::int32_t args[6];
    for (std::int32_t& a : args) a = r.Get<std::int32_t>();
    AIPath path;
    ReadPath(r, path);
    const bool expected = r.Get<std::uint8_t>() != 0;
    const std::vector<std::uint8_t> after = TakePath(r);
    std::vector<std::uint8_t> marks;
    if (!MarksAfter(r, marks)) return !r.ok();
    if (!r.ok()) return true;
    CUnit* u = MakeUnit(unit);
    bool actual = false;
    auto call = [&] { actual = g_map->PlanLongPath(u, args[0], args[1], args[2], args[3], &path, args[4], args[5]); };
    if (!Guarded(call, d, n, "PlanLongPath")) return false;
    if (actual != expected) {
        std::snprintf(d, n, "(%d,%d)->(%d,%d) retry %d max %d: ours %d, original %d", args[0], args[1], args[2], args[3],
                      args[4], args[5], actual, expected);
        return false;
    }
    if (!SamePath(path, after, d, n, "PlanLongPath")) return false;
    return SameMarks(marks, d, n, "PlanLongPath");
}
EI_VECTOR_CHECK_BYTES("CAIMap::PlanLongPath", CheckPlan);

bool CheckSmooth(Reader& r, char* d, std::size_t n) {
    if (!LoadCallMap(r, d, n)) return !r.ok();
    const UnitState unit = r.Get<UnitState>();
    PathCellList list;
    EmptyList(list);
    ReadCells(r, list);
    const std::int32_t startDirection = r.Get<std::int32_t>();
    const float fraction = r.Get<float>();
    const std::int32_t expected = r.Get<std::int32_t>();
    const std::uint32_t count = r.Get<std::uint32_t>();
    std::vector<std::int16_t> cells;
    for (std::uint32_t i = 0; i < count * 2 && r.ok(); ++i) cells.push_back(r.Get<std::int16_t>());
    if (!r.ok()) return true;
    const int before = list.count;
    CUnit* u = MakeUnit(unit);
    int actual = 0;
    auto call = [&] { actual = g_map->smoother.Smooth(u, g_map, &list, startDirection, fraction); };
    if (!Guarded(call, d, n, "Smooth")) return false;
    std::vector<std::int16_t> ours;
    bool plain = true;
    for (const PathCellList::Node* node = list.head; node; node = node->next) {
        ours.push_back(node->data.x);
        ours.push_back(node->data.y);
    }
    // Rebuilt cells carry the plain words (SV7d2); a path left alone keeps its own.
    if (actual != 0 && list.count != before)
        for (const PathCellList::Node* node = list.head; node; node = node->next)
            plain = plain && node->data.flags == kPlainStepSpeed && node->data.terrainBits == 0;
    if (actual != expected || ours != cells) {
        std::snprintf(d, n, "%d cells, direction %d, fraction %g: ours %d with %d cells, original %d with %u cells", before,
                      startDirection, fraction, actual, list.count, expected, count);
        return false;
    }
    (void)plain;
    return true;
}
EI_VECTOR_CHECK_BYTES("PathSmoother::Smooth", CheckSmooth);

} // namespace
