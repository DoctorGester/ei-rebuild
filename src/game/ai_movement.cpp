// Per-tick movement checks on the AI map: which unit blocks a moving unit, and the speed costs of
// the steps a unit is about to walk.
#include "ai_map.h"

#include "ai_shape_cache.h"
#include "core_refcount.h"
#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/mfc.h"

#include <cstring>
#include "originals_ai.h"

namespace {
// The sector map through its original address (rewritten in sector_map.cpp), so the verify build's
// wrapper sees the call.

// A handle into the cache of unit occupancy shapes (UnitShapes(), ai_shape_cache.h), keyed by
// standing height rounded to 0.1. 16x16 signed levels start at shape + 8.
using ShapeHandle = ShapeCacheHandle;
__declspec(naked) ShapeHandle* __fastcall ShapeHandleInit(ShapeHandle*, void*) EI_ORIGINAL(0x005BCCB0)
__declspec(naked) const std::int8_t* __fastcall ShapeCacheGet(UnitShapeCache*, void*, ShapeHandle*)
    EI_ORIGINAL(0x005BD7F0)

// 005B8BA0: the passability of cell (x, y) for a size class standing at `level` (a cell with
// occluders); the cost is at out[3].
// Standing height of each size class (007912BC).
const float* ClassHeights() { return reinterpret_cast<const float*>(0x007912BC); }

// LineOfSight through its original address (rewritten in ai_map.cpp), its ST0 taken unrounded.

// World coordinate to cell as the original computes it here: (x / (cell size 1.0 * 0.5) - 0.5)
// rounded to float, then FISTP.
int CellOf(float world) { return x87::fistp(static_cast<float>(2.0 * world - 0.5f)); }

constexpr float kShapeMinHeight = 0.2f;     // 0073BAD4
constexpr int kExpensive = 12000;           // cost above which a step is flagged 2
} // namespace

CUnit* CAIMap::BlockingUnitAt(CUnit* unit, const Vector3* position) {
    mfc::CTypedPtrList<CEngineObject> nearby{};
    nearby.vtable = reinterpret_cast<void*>(0x0073D454);
    nearby.blockSize = 10;
    float unitHeight = unit->standingHeight;
    SectorMapFindUnitsInRadius(&sectorMap, nullptr, position, &nearby, 10.0f);

    CUnit* blocker = nullptr;
    for (auto* node = nearby.head; node;) {
        CUnit* other = static_cast<CUnit*>(node->data);
        node = node->next;
        if (other == unit) continue;
        double reach = static_cast<double>(other->standingHeight) + unitHeight;
        double dy = static_cast<double>(position->y) - other->position.y;
        double dx = static_cast<double>(position->x) - other->position.x;
        if (LengthSquared2DUnrounded(dx, dy) >= reach * reach) continue;
        if (other->IsDead() != 0) continue;

        int unitX = CellOf(unit->position.x);
        int unitY = CellOf(unit->position.y);
        int otherX = CellOf(other->position.x);
        int otherY = CellOf(other->position.y);
        float otherHeight = other->standingHeight;
        ShapeHandle handle;
        ShapeHandleInit(&handle, nullptr);
        const std::int8_t* shape = nullptr;
        if (!(otherHeight > kShapeMinHeight)) {
            if (handle.ref) handle.ref->Release();
        } else {
            int tenths = x87::fistp(static_cast<float>(static_cast<double>(otherHeight) * 10.0f + 1.0f));
            handle.height = static_cast<float>(static_cast<double>(tenths) * 0.1f);
            std::memcpy(&handle.key, &handle.height, sizeof(handle.key));
            shape = ShapeCacheGet(UnitShapes(), nullptr, &handle);
            if (CRefObject* ref = handle.ref) {
                if (--ref->refs_ == 0) delete ref;
            }
        }

        int level = x87::fistp(static_cast<float>((2.0f - static_cast<double>(unit->standingHeight)) * 16.0f));
        int minLevel = level < 0 ? 0 : (level & 0xFF);
        int dxCells = unitX - otherX + 8;
        int dyCells = unitY - otherY + 8;
        if (!shape || dxCells < 0 || dyCells < 0 || dxCells >= 16 || dyCells >= 16 ||
            shape[8 + dyCells * 16 + dxCells] >= minLevel) {
            blocker = other;
            break;
        }
    }
    nearby.vtable = reinterpret_cast<void*>(0x0073D454);
    nearby.RemoveAll();
    return blocker;
}
EI_REPLACE_METHOD(0x005B7BB0, CAIMap::BlockingUnitAt);

const int* CAIMap::CostContext(int sizeClass, PassCostContext* out) {
    std::uint32_t tenth = 0x3DCCCCCD;   // 0.1f
    std::memcpy(&out->forwardSpeed, &tenth, sizeof(tenth));
    std::memcpy(&out->backwardSpeed, &tenth, sizeof(tenth));
    out->turnScale = 10.0f;
    out->shift = sizeClass * 4;
    const ClimbTable& climb = out->shift == 0 ? climbSmall : climbLarge;
    out->costs = passCosts;
    out->climb = climb.data + climb.size / 2;
    return out->climb;
}
EI_REPLACE_METHOD(0x005B9510, CAIMap::CostContext);

void CAIMap::SetStepSpeeds(CUnit* unit, PathCellList* steps) {
    // The original also asks the unit for its movement speeds twice (0052F920) and divides by one
    // of them, but never uses the results; the getter has no side effects.
    int shift = unit->sizeClass * 4;
    const ClimbTable& climbTable = shift == 0 ? climbSmall : climbLarge;
    const int* climb = climbTable.data + climbTable.size / 2;
        PassCostContext context;
    CostContext(unit->sizeClass, &context);

    PathCellList::Node* node = steps->head;
    if (!node) return;
    for (;;) {
        PathCellList::Node* next = node->next;
        const AIMapCell& cell = cells[node->data.y][node->data.x];
        if (!next) {
            int cost = context.costs[(cell.passable >> (context.shift & 31)) & 0xF];
            node->data.terrainBits =
                static_cast<std::uint16_t>((cell.flags >> 7) + (cost > kExpensive ? 2 : 0));
            return;
        }
        const AIMapCell& to = cells[next->data.y][next->data.x];
        int cost = stepCosts[(to.passable >> (shift & 31)) & 0xF];
        int contextCost = context.costs[(to.passable >> (context.shift & 31)) & 0xF];
        if (cost < 0) cost = 0x400;
        int climbCost = climb[static_cast<int>(to.groundLevel) - static_cast<int>(cell.groundLevel)];
        int speed = climbCost < 0 ? 0x200 : static_cast<int>(static_cast<unsigned>(cost) << 9) / climbCost;
        node->data.flags = static_cast<std::uint16_t>(speed);
        node->data.terrainBits = static_cast<std::uint16_t>((to.flags >> 7) + (contextCost > kExpensive ? 2 : 0));
        node = next;
    }
}
EI_REPLACE_METHOD(0x005BA400, CAIMap::SetStepSpeeds);

bool CAIMap::StandsOnCheapCell(CUnit* unit) {
    PassCostContext context;
    CostContext(unit->sizeClass, &context);
    // The original also asks the unit for its movement speeds (0052F920) and ignores them.
    int x = CellOf(unit->position.x);
    int y = CellOf(unit->position.y);
    if (x < 0 || y < 0 || x >= width || y >= height) return false;
    if (HasKind3Occluder(x, y)) {
        int sizeClass = unit->sizeClass;
        int level = x87::fistp(static_cast<float>(static_cast<double>(ClassHeights()[sizeClass]) * heightScale));
        std::uint32_t passability[6];
        AIMapCellMarkOriginal(this, nullptr, passability, sizeClass, level, x, y);
        return static_cast<int>(passability[3]) < kExpensive;
    }
    int cost = context.costs[(cells[y][x].passable >> (context.shift & 31)) & 0xF];
    return cost > 0 && cost < kExpensive;
}
EI_REPLACE_METHOD(0x005BA5A0, CAIMap::StandsOnCheapCell);

__declspec(naked) void VisibilityGrid::Include(int, int, int) EI_ORIGINAL(0x005C8D40)

namespace {
// 005B14C0: the byte grid's block and row table for its width and height.
void AllocateGrid(VisibilityGrid* grid) {
    grid->data = static_cast<std::uint8_t*>(gamecrt::operator_new(grid->width * grid->height));
    grid->rows = static_cast<std::uint8_t**>(gamecrt::operator_new(grid->height * sizeof(std::uint8_t*)));
    for (int i = 0; i < grid->height; ++i) grid->rows[i] = grid->data + i * grid->width;
}
// A square grid of `size`: reallocated when the size differs, then cleared.
void ResizeCleared(VisibilityGrid* grid, int size) {
    if (grid->width != size || grid->height != size) {
        gamecrt::operator_delete(grid->data);
        gamecrt::operator_delete(grid->rows);
        grid->width = size;
        grid->height = size;
        AllocateGrid(grid);
    }
    std::memset(grid->data, 0, static_cast<std::size_t>(grid->width * grid->height));
}
} // namespace

void VisibilityGrid::Grow(int x, int y, int n) {
    int x0 = x - n, x1 = x + n, y0 = y - n, y1 = y + n;
    if (originX == 0x7FFFFFFF) {
        int size = (y1 - y0 > x1 - x0 ? y1 - y0 : x1 - x0) + 1;
        ResizeCleared(this, size);
        originX = x0;
        originY = y0;
        return;
    }
    bool grow = x0 < originX || y0 < originY || x1 > originX + width - 1 || y1 > originY + height - 1;
    if (x0 >= originX) x0 = originX;
    if (x1 <= originX + width - 1) x1 = originX + width - 1;
    if (y0 >= originY) y0 = originY;
    if (y1 <= originY + height - 1) y1 = originY + height - 1;
    if (!grow) return;
    int size = (y1 - y0 > x1 - x0 ? y1 - y0 : x1 - x0) + 1;
    // 005CD410: a copy of the old marks (block and row table), moved into the cleared grid.
    VisibilityGrid old = *this;
    AllocateGrid(&old);
    std::memcpy(old.data, data, static_cast<std::size_t>(old.width * old.height));
    ResizeCleared(this, size);
    for (int row = 0; row < old.height; ++row)
        for (int col = 0; col < old.width; ++col)
            rows[row - y0 + originY][col - x0 + originX] = old.rows[row][col];
    originX = x0;
    originY = y0;
    gamecrt::operator_delete(old.data);
    gamecrt::operator_delete(old.rows);
}
// Registered in A-W11 (A-W6 wrote and verified it, but only the verify build ran it).
EI_REPLACE_METHOD(0x005C8D40, VisibilityGrid::Grow);

void CAIMap::MarkVisibleCells(const Vector3* eye, float radius, const Vector3* look, float cosLimit,
                              float targetHeight, VisibilityGrid* grid) {
    int eyeX = CellOf(eye->x);
    int eyeY = CellOf(eye->y);
    int reach = CellOf(radius) + 1;
    grid->Include(eyeX, eyeY, reach);
    bool anyDirection = !(cosLimit > -1.0f);
    bool noLineOfSight = !(targetHeight >= -100.0f);    // 0073FDC4

    const double half = 1.0f * 0.5f;    // cell size * 0.5
    for (int y = eyeY - reach; y < eyeY + reach; ++y) {
        for (int x = eyeX - reach; x < eyeX + reach; ++x) {
            int gx = x - grid->originX;
            int gy = y - grid->originY;
            if (x * 9 < 0 || y < 0 || x >= width || y >= height) continue;
            Vector3 cell;
            cell.x = static_cast<float>(static_cast<double>(x) * half);
            cell.y = static_cast<float>(static_cast<double>(y) * half);
            cell.z = 0.0f;
            float dx = static_cast<float>(static_cast<double>(cell.x) - eye->x);
            float dy = static_cast<float>(static_cast<double>(cell.y) - eye->y);
            float dz = static_cast<float>(-static_cast<double>(eye->z));
            float distance = static_cast<float>(x87::fsqrt(LengthSquared2DUnrounded(dy, dx)));
            if (radius < distance || x87::IsNaN(radius) || x87::IsNaN(distance)) continue;
            if (!anyDirection) {
                double dot = DotUnrounded<Sum3::ZYX, double>(dx, dy, dz, look->x, look->y, look->z);
                if (static_cast<double>(distance) * cosLimit > dot) continue;
            }
            if (!noLineOfSight) {
                cell.z = static_cast<float>(static_cast<double>(cells[y][x].groundLevel) / heightScale + targetHeight);
                double seen = LineOfSightOriginal(this, nullptr, eye, &cell) * radius;
                if (!(seen >= distance)) continue;
            }
            grid->rows[gy][gx] = 1;
        }
    }
}
EI_REPLACE_METHOD(0x005BC970, CAIMap::MarkVisibleCells);

namespace {
// The shape cache's map (UnitShapes()->shapes, 007C2F78): an STLport hash_map<ShapeHandle, RefPtr>,
// called through the game's own instantiation. Node: {next, handle (0x10), value}.
using ShapeMap = UnitShapeCache::Map;
using ShapeCacheIterator = ShapeMap::Iterator;
using ShapePair = ShapeMap::Pair;   // pair<const ShapeHandle, RefPtr> (0x14 bytes)
static_assert(sizeof(ShapePair) == 0x14);
__declspec(naked) ShapeCacheIterator* __fastcall ShapeMapFind(ShapeMap*, void*, ShapeCacheIterator*, const ShapeHandle*)
    EI_ORIGINAL(0x005BE320)
__declspec(naked) ShapeCacheIterator* __fastcall ShapeIteratorInit(ShapeCacheIterator*, void*, ShapeMap::Node*,
                                                                    const ShapeMap*) EI_ORIGINAL(0x005BE450)
// The pair: construct from a handle and a RefPtr, destroy.
__declspec(naked) ShapePair* __fastcall ShapePairInit(ShapePair*, void*, const ShapeHandle*, const RefPtr*)
    EI_ORIGINAL(0x005BE370)
__declspec(naked) void __fastcall ShapePairDestroy(ShapePair*, void*) EI_ORIGINAL(0x005BE280)
// hash_map insert_unique_noresize-style lookup or insert; returns the node's pair.
__declspec(naked) ShapePair* __fastcall ShapeMapInsert(ShapeMap*, void*, const ShapePair*) EI_ORIGINAL(0x005BE730)

void ReleaseHandle(ShapeHandle* handle) {
    if (CRefObject* ref = handle->ref) {
        if (--ref->refs_ == 0) delete ref;     // delete tests ref again, as the original does
    }
}
} // namespace

ShapePair* __fastcall ConstructShapePair(ShapePair* pair, void*, const ShapeHandle* key, const RefPtr* value) {
    ShapeHandle& to = const_cast<ShapeHandle&>(pair->key);
    to.cache = key->cache;
    to.ref = key->ref;
    if (to.ref) ++to.ref->refs_;
    to.key = key->key;
    to.height = key->height;
    pair->value.Share(value->ptr);
    return pair;
}
EI_REPLACE(0x005BE370, ConstructShapePair);

void __fastcall DestroyShapePair(ShapePair* pair) {
    pair->value.Destroy();
    ReleaseHandle(const_cast<ShapeHandle*>(&pair->key));
}
EI_REPLACE(0x005BE280, DestroyShapePair);

void* CAIMap::UnitShape(float standingHeight) {
    ShapeHandle handle;
    handle.cache = nullptr;
    handle.ref = nullptr;
    handle.key = 0xFFFFFFFF;
    if (!(standingHeight > kShapeMinHeight)) return nullptr;
    int tenths = x87::fistp(static_cast<float>(static_cast<double>(standingHeight) * 10.0f + 1.0f));
    handle.height = static_cast<float>(static_cast<double>(tenths) * 0.1f);
    std::memcpy(&handle.key, &handle.height, sizeof(handle.key));

    CRefObject* shape;
    ShapeCacheIterator found, end;
    ShapeMap* map = &UnitShapes()->shapes;
    ShapeMapFind(map, nullptr, &found, &handle);
    ShapeIteratorInit(&end, nullptr, nullptr, map);
    if (found.node != end.node) {
        shape = found.node->value.ptr;
        handle.cache = UnitShapes();
        reinterpret_cast<RefPtr*>(&handle.ref)->Assign(shape);
    } else {
        // Not cached yet: create it and store it under the handle.
        shape = UnitShapes()->CreateShape(&handle);
        RefPtr none = {nullptr};
        ShapePair pair;
        ShapePairInit(&pair, nullptr, &handle, &none);
        RefPtr* slot = &ShapeMapInsert(map, nullptr, &pair)->value;
        ShapePairDestroy(&pair, nullptr);
        none.Destroy();
        if (shape != slot->ptr) {
            slot->Destroy();
            slot->Share(shape);
        }
        handle.cache = UnitShapes();
        reinterpret_cast<RefPtr*>(&handle.ref)->Assign(shape);
    }
    ReleaseHandle(&handle);
    return shape;
}
EI_REPLACE_METHOD(0x005B8350, CAIMap::UnitShape);
