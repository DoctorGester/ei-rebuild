// Record mode for the "path" family (path_vectors.h): CAIMap::FindPath, CAIMap::PlanLongPath and
// PathSmoother::Smooth on the AI map they ran on. Stolen bytes from the disassembly (checked by
// check_stolen.py). Reached by every scenario whose units walk (movement, unit_ai, combat, ...).
//
// The map is saved once per map and run ("@AIMap", when the first search runs on it); each record
// holds its differences from that. FindPath takes the unit out of the sector map before it
// searches (its own footprint must not block it), so its map is taken at that point: the sector
// map's Remove has a recorder that saves nothing of its own and takes the map for the FindPath
// recording on its thread.
#include "path_vectors.h"
#include "vectors.h"

#include "../../src/game/ai_map.h"
#include "../../src/game/game_data.h"
#include "../../src/game/sector_map.h"
#include "../../src/hook/log.h"

#include "ei/hook.h"
#include "game/game_data.h"
#include "game/game_singletons.h"

#include <windows.h>

#include <cstring>
#include <vector>

namespace {

using namespace ei::vectors;
using namespace ei::vectors::path;

Sampler g_tables(kFamily, "@PathTables", kVariableSize, 1);
Sampler g_maps(kFamily, "@AIMap", kVariableSize, 64);
Sampler g_findPath(kFamily, "CAIMap::FindPath", kVariableSize, 24, kFindBuckets);
Sampler g_plan(kFamily, "CAIMap::PlanLongPath", kVariableSize, 24, kPlanBuckets);
Sampler g_smooth(kFamily, "PathSmoother::Smooth", kVariableSize, 32, 3);

// Paths longer than this are not kept (the records hold every cell twice).
constexpr int kMaxCells = 1024;


// ---- @PathTables ---------------------------------------------------------------------------------

void PutArray(Writer& w, const void* data, int size, std::uint32_t elementSize) {
    w.Put(static_cast<std::uint32_t>(size));
    if (size > 0) w.PutBytes(data, static_cast<std::size_t>(size) * elementSize);
}

void OfferTables() {
    static LONG offered = 0;
    if (InterlockedExchange(&offered, 1)) return;
    Writer w;
    w.Put(static_cast<std::uint32_t>(std::size(kTableRanges)));
    for (const Range& r : kTableRanges) {
        w.Put(r);
        w.PutBytes(ei::GamePointer<const void>(r.address), r.size);
    }
    w.Put(static_cast<std::uint32_t>(std::size(kTableArrays)));
    for (const Range& r : kTableArrays) {
        const auto& array = ei::GameData<const mfc::CArray<std::uint8_t>>(r.address);
        w.Put(r);
        PutArray(w, array.data, array.size, r.size);
    }
    const auto* tables = ei::GamePointer<const SmoothTable>(kSmoothTables);
    for (int i = 0; i < kSmoothTableCount; ++i) {
        PutArray(w, tables[i].copies.data, tables[i].copies.size, kSmoothCopySize);
        PutArray(w, tables[i].steps.data, tables[i].steps.size, kSmoothStepSize);
        PutArray(w, tables[i].chained.data, tables[i].chained.size, kSmoothStepSize);
    }
    g_tables.OfferBytes(w, ControlWord());
}

// ---- @AIMap and the differences from it ------------------------------------------------------------

// The region map's grids in the order @AIMap stores them: per size class the edge words, the
// representative cells and the regions.
struct GridView {
    const void* data;
    std::uint32_t bytes;
};
std::vector<GridView> RegionGrids(const CAIMap* map) {
    std::vector<GridView> grids;
    const CRegionMap& r = map->regionMap;
    for (int c = 0; c < 8; ++c) {
        grids.push_back({r.edges[c].data, static_cast<std::uint32_t>(r.edges[c].width * r.edges[c].height * 8 * 2)});
        grids.push_back({r.cells[c].data, static_cast<std::uint32_t>(r.cells[c].width * r.cells[c].height)});
        grids.push_back({r.regions[c].data, static_cast<std::uint32_t>(r.regions[c].width * r.regions[c].height * 2)});
    }
    return grids;
}

std::vector<std::uint8_t> RegionBytes(const CAIMap* map) {
    std::vector<std::uint8_t> bytes;
    for (const GridView& g : RegionGrids(map)) {
        const auto* p = static_cast<const std::uint8_t*>(g.data);
        bytes.insert(bytes.end(), p, p + g.bytes);
    }
    return bytes;
}

std::uint32_t CellBytes(const CAIMap* m) { return static_cast<std::uint32_t>(m->width * m->height * sizeof(AIMapCell)); }
std::uint32_t WaterBytes(const CAIMap* m) { return static_cast<std::uint32_t>(m->waterWidth * m->waterHeight); }
std::uint32_t MarkBytes(const CAIMap* m) { return static_cast<std::uint32_t>(m->marks.width * m->marks.height); }

// The map as saved: what identifies it in the game, and its content.
struct Saved {
    const CAIMap* map = nullptr;
    const void* identity[5] = {};
    int sizes[6] = {};
    std::uint64_t id = 0;
    std::vector<std::uint8_t> cells, water, marks, regions;
};
Saved g_saved;
SRWLOCK g_mapLock = SRWLOCK_INIT;

void Identify(const CAIMap* map, const void* (&identity)[5], int (&sizes)[6]) {
    const CRegionMap& r = map->regionMap;
    const void* ids[5] = {map, map->cellBlock, map->waterBlock, map->marks.data, r.edges[0].data};
    const int s[6] = {map->width, map->height, map->waterWidth, map->waterHeight, r.edges[0].width, r.edges[0].height};
    std::memcpy(identity, ids, sizeof(ids));
    std::memcpy(sizes, s, sizeof(s));
}

void PutGrid(Writer& w, int width, int height, const void* data, std::uint32_t perCell, std::uint32_t stride) {
    w.Put(width);
    w.Put(height);
    PutPacked(w, static_cast<const std::uint8_t*>(data), static_cast<std::uint32_t>(width * height) * perCell, stride);
}

// Saves `map` (and offers it as @AIMap) unless it is the one saved. Under g_mapLock.
void EnsureSaved(const CAIMap* map) {
    const void* identity[5];
    int sizes[6];
    Identify(map, identity, sizes);
    if (g_saved.map == map && std::memcmp(identity, g_saved.identity, sizeof(identity)) == 0 &&
        std::memcmp(sizes, g_saved.sizes, sizeof(sizes)) == 0)
        return;
    Writer body;
    body.Put(map->width);
    body.Put(map->height);
    body.Put(map->passCosts);
    body.Put(map->passCostsAlt);
    body.Put(map->stepCosts);
    PutArray(body, map->climbLarge.data, map->climbLarge.size, 4);
    PutArray(body, map->climbSmall.data, map->climbSmall.size, 4);
    body.Put(map->heightScale);
    PutGrid(body, map->width, map->height, map->cellBlock, 1, sizeof(AIMapCell));
    PutGrid(body, map->waterWidth, map->waterHeight, map->waterBlock, 1, 1);
    PutGrid(body, map->marks.width, map->marks.height, map->marks.data, 1, 1);
    const CRegionMap& r = map->regionMap;
    for (int c = 0; c < 8; ++c) {
        PutGrid(body, r.edges[c].width, r.edges[c].height, r.edges[c].data, 8, 2);
        PutGrid(body, r.cells[c].width, r.cells[c].height, r.cells[c].data, 1, 1);
        PutGrid(body, r.regions[c].width, r.regions[c].height, r.regions[c].data, 1, 2);
    }
    Writer w;
    const std::uint64_t id = HashBytes(body.data(), body.size());
    w.Put(id);
    w.PutBytes(body.data(), body.size());
    g_maps.OfferBytes(w, ControlWord());

    g_saved.map = map;
    std::memcpy(g_saved.identity, identity, sizeof(identity));
    std::memcpy(g_saved.sizes, sizes, sizeof(sizes));
    g_saved.id = id;
    auto copy = [](std::vector<std::uint8_t>& to, const void* from, std::uint32_t bytes) {
        const auto* p = static_cast<const std::uint8_t*>(from);
        to.assign(p, p + bytes);
    };
    copy(g_saved.cells, map->cellBlock, CellBytes(map));
    copy(g_saved.water, map->waterBlock, WaterBytes(map));
    copy(g_saved.marks, map->marks.data, MarkBytes(map));
    g_saved.regions = RegionBytes(map);
    ei::Log("vectors: path: AI map %016llX saved: %dx%d cells, %u bytes", static_cast<unsigned long long>(id), map->width,
            map->height, w.size());
}

// The map a search runs on: the saved map's id and the differences from it.
void PutCallMap(Writer& w, const CAIMap* map, std::uint8_t planes = kCallAll) {
    AcquireSRWLockExclusive(&g_mapLock);
    EnsureSaved(map);
    w.Put(g_saved.id);
    w.Put(planes);
    if (planes & kCallCells)
        PutDiff(w, g_saved.cells.data(), reinterpret_cast<const std::uint8_t*>(map->cellBlock), CellBytes(map) / 9, 9);
    if (planes & kCallWater)
        PutDiff(w, g_saved.water.data(), reinterpret_cast<const std::uint8_t*>(map->waterBlock), WaterBytes(map), 1);
    if (planes & kCallMarks)
        PutDiff(w, g_saved.marks.data(), reinterpret_cast<const std::uint8_t*>(map->marks.data), MarkBytes(map), 1);
    if (planes & kCallRegions) {
        const std::vector<std::uint8_t> regions = RegionBytes(map);
        PutDiff(w, g_saved.regions.data(), regions.data(), static_cast<std::uint32_t>(regions.size()), 1);
    }
    ReleaseSRWLockExclusive(&g_mapLock);
}

UnitState StateOf(CUnit* unit) {
    auto s = Zeroed<UnitState>();
    s.classId = unit->GetClassId();
    std::memcpy(s.position, &unit->position, sizeof(s.position));
    std::memcpy(s.orientation, &unit->orientation, sizeof(s.orientation));
    s.sizeClass = unit->sizeClass;
    s.standingHeight = unit->standingHeight;
    s.altPassCosts = unit->altPassCosts;
    unit->GetMoveSpeeds(&s.forwardSpeed, &s.backwardSpeed);   // no side effects
    return s;
}

std::vector<std::uint8_t> MarksOf(const CAIMap& map) {
    const auto* p = reinterpret_cast<const std::uint8_t*>(map.marks.data);
    return std::vector<std::uint8_t>(p, p + MarkBytes(&map));
}

// What the search changed in the marks.
void PutMarksAfter(Writer& w, const std::vector<std::uint8_t>& before, const CAIMap& map) {
    PutDiff(w, before.data(), reinterpret_cast<const std::uint8_t*>(map.marks.data), MarkBytes(&map), 1);
}

bool Small(const AIPath* path) { return path->cells.count <= kMaxCells && path->steps.count <= kMaxCells; }

// ---- FindPath, and the map it searches on ----------------------------------------------------------

// A FindPath being recorded on this thread: the sector map's Remove takes its map.
struct PendingFind {
    const CAIMap* map;
    const CEngineObject* unit;
    Writer state;
    std::vector<std::uint8_t> marks;   // the marks when the map was taken
    bool captured = false;
    int plans = 0;
};
thread_local PendingFind* t_find = nullptr;

// 005BB350: PUSH EBP; MOV EBP,ESP; SUB ESP,0x34 (6).
using FindPathFn = bool(__fastcall*)(CAIMap*, void*, CUnit*, const Vector3*, AIPath*, float, float);
FindPathFn s_findPath;
bool __fastcall FindPathRecord(CAIMap* self, void*, CUnit* unit, const Vector3* target, AIPath* path, float range,
                               float minDistance) {
    OfferTables();
    Writer head;
    head.Put(StateOf(unit));
    head.Put<std::int32_t>(CurrentWorldTick());
    head.Put(*target);
    head.Put(range);
    head.Put(minDistance);
    PutPath(head, *path);
    const bool fits = Small(path);
    PendingFind pending{self, unit};
    PendingFind* outer = t_find;
    t_find = &pending;
    std::uint16_t cw = ControlWord();
    bool result = s_findPath(self, nullptr, unit, target, path, range, minDistance);
    t_find = outer;
    if (!fits || !Small(path)) return result;
    Writer w;
    w.Put<std::uint8_t>(pending.captured);
    if (pending.captured) w.PutBytes(pending.state.data(), pending.state.size());
    w.PutBytes(head.data(), head.size());
    w.Put<std::uint8_t>(result);
    PutPath(w, *path);
    if (pending.captured) PutMarksAfter(w, pending.marks, *self);
    unsigned bucket = !result ? kFindFailed : pending.plans ? kFindPlanned : kFindNear;
    g_findPath.OfferBytes(w, cw, bucket);
    return result;
}

// 005C1010: PUSH EBP; PUSH ESI; PUSH EDI; MOV EDI,[ESP+0x10] (7). The capture point above.
using RemoveFn = bool(__fastcall*)(CSectorMap*, void*, CEngineObject*);
RemoveFn s_remove;
bool __fastcall RemoveRecord(CSectorMap* self, void*, CEngineObject* object) {
    bool result = s_remove(self, nullptr, object);
    PendingFind* pending = t_find;
    if (pending && !pending->captured && object == pending->unit) {
        PutCallMap(pending->state, pending->map);
        pending->marks = MarksOf(*pending->map);
        pending->captured = true;
    }
    return result;
}

// 005B9C90: PUSH -1; PUSH 0x00727B90 (7).
using PlanFn = bool(__fastcall*)(CAIMap*, void*, CUnit*, int, int, int, int, AIPath*, int, int);
PlanFn s_plan;
bool __fastcall PlanRecord(CAIMap* self, void*, CUnit* unit, int fromX, int fromY, int toX, int toY, AIPath* path,
                           int retry, int maxCells) {
    OfferTables();
    if (t_find) ++t_find->plans;
    Writer w;
    PutCallMap(w, self);
    w.Put(StateOf(unit));
    const std::int32_t args[6] = {fromX, fromY, toX, toY, retry, maxCells};
    w.Put(args);
    PutPath(w, *path);
    const bool fits = Small(path);
    const bool failedBefore = path->failed;
    const std::vector<std::uint8_t> marks = MarksOf(*self);
    std::uint16_t cw = ControlWord();
    bool result = s_plan(self, nullptr, unit, fromX, fromY, toX, toY, path, retry, maxCells);
    if (!fits || !Small(path)) return result;
    w.Put<std::uint8_t>(result);
    PutPath(w, *path);
    PutMarksAfter(w, marks, *self);
    unsigned bucket = !result ? kPlanFailed : path->failed && !failedBefore ? kPlanAimed : kPlanReached;
    g_plan.OfferBytes(w, cw, bucket);
    return result;
}

// 005C46C0: PUSH EBP; MOV EBP,ESP; SUB ESP,0xE0 (9). The original's rebuilt cells have stale stack
// in their second dwords (SV7d2): only x and y are kept.
using SmoothFn = int(__fastcall*)(PathSmoother*, void*, CUnit*, CAIMap*, PathCellList*, int, float);
SmoothFn s_smooth;
int __fastcall SmoothRecord(PathSmoother* self, void*, CUnit* unit, CAIMap* map, PathCellList* path,
                            int startDirection, float fraction) {
    OfferTables();
    if (path->count > kMaxCells) return s_smooth(self, nullptr, unit, map, path, startDirection, fraction);
    Writer w;
    PutCallMap(w, map, kCallCells);
    w.Put(StateOf(unit));
    PutCells(w, *path);
    w.Put<std::int32_t>(startDirection);
    w.Put(fraction);
    const int count = path->count;
    std::uint16_t cw = ControlWord();
    int result = s_smooth(self, nullptr, unit, map, path, startDirection, fraction);
    w.Put<std::int32_t>(result);
    w.Put(static_cast<std::uint32_t>(path->count));
    for (const PathCellList::Node* n = path->head; n; n = n->next) {
        w.Put(n->data.x);
        w.Put(n->data.y);
    }
    g_smooth.OfferBytes(w, cw, count < 3 ? 0u : count <= 12 ? 1u : 2u);
    return result;
}

} // namespace

// FindPath's callers are rewritten (ContinuePath calls ours directly).
EI_RECORD(0x005BB350, FindPathRecord, 6, s_findPath);
EI_RECORD(0x005C1010, RemoveRecord, 7, s_remove);
EI_RECORD(0x005B9C90, PlanRecord, 7, s_plan);
EI_RECORD(0x005C46C0, SmoothRecord, 9, s_smooth);
