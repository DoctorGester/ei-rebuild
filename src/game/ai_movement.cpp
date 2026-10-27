// Per-tick movement checks on the AI map: which unit blocks a moving unit, and the speed costs of
// the steps a unit is about to walk.
#include "ai_map.h"
#include "effects.h"

#include "ai_shape_cache.h"
#include "ai_unit_layer.h"
#include "core_refcount.h"
#include "game_hashes.h"
#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/nan_audit.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "lib/mfc_slots.h"

#include <cstring>
#include "game_data.h"

// The unit shape cache. Here, not in ai_shape_cache.cpp, so the path vectors' replay links it.
constinit lib::StaticObject<UnitShapeCache> g_unitShapes = /*007C2F70*/ {};

namespace {
// A handle into the cache of unit occupancy shapes (UnitShapes(), ai_shape_cache.h), keyed by
// standing height rounded to 0.1. 16x16 signed levels start at shape + 8.
using ShapeHandle = ShapeCacheHandle;

// World coordinate to cell: (x / (cell size 1.0 * 0.5) - 0.5) rounded to float, then to the nearest integer.
int CellOf(float world) { return x87::fistp(static_cast<float>(2.0 * world - 0.5f)); }

constexpr float kShapeMinHeight = 0.2f;
constexpr int kExpensive = 12000;           // cost above which a step is flagged 2
} // namespace

CUnit* CAIMap::BlockingUnitAt(CUnit* unit, const Vector3* position) {
    mfc::CTypedPtrList<CUnit> nearby{};
    nearby.vtable = mfc::impl::PtrListClass::Vtable();
    nearby.blockSize = 10;
    float unitHeight = unit->standingHeight;
    sectorMap.FindUnitsInRadius(position, &nearby, 10.0f);

    CUnit* blocker = nullptr;
    for (auto* node = nearby.head; node;) {
        CUnit* other = node->data;
        node = node->next;
        if (other == unit) continue;
        double reach = static_cast<double>(other->standingHeight) + unitHeight;
        double dy = static_cast<double>(position->y) - other->localPosition_.y;
        double dx = static_cast<double>(position->x) - other->localPosition_.x;
        if (LengthSquared2DUnrounded(dx, dy) >= reach * reach) continue;
        if (other->IsDead() != 0) continue;

        int unitX = CellOf(unit->localPosition_.x);
        int unitY = CellOf(unit->localPosition_.y);
        int otherX = CellOf(other->localPosition_.x);
        int otherY = CellOf(other->localPosition_.y);
        float otherHeight = other->standingHeight;
        ShapeHandle handle;
        gamehash::InitShapeHandle(&handle);
        const CUnitShape* shape = nullptr;
        EI_NAN_AUDIT(otherHeight);
        if (otherHeight <= kShapeMinHeight) {
            if (handle.ref) handle.ref->Release();
        } else {
            int tenths = x87::fistp(static_cast<float>(static_cast<double>(otherHeight) * 10.0f + 1.0f));
            handle.height = static_cast<float>(static_cast<double>(tenths) * 0.1f);
            std::memcpy(&handle.key, &handle.height, sizeof(handle.key));
            shape = static_cast<const CUnitShape*>(UnitShapes()->Get(&handle));
            if (CRefObject* ref = handle.ref) {
                if (--ref->refs_ == 0) delete ref;
            }
        }

        int level = x87::fistp(static_cast<float>((2.0f - static_cast<double>(unit->standingHeight)) * 16.0f));
        int minLevel = level < 0 ? 0 : (level & 0xFF);
        int dxCells = unitX - otherX + 8;
        int dyCells = unitY - otherY + 8;
        if (!shape || dxCells < 0 || dyCells < 0 || dxCells >= 16 || dyCells >= 16 ||
            static_cast<std::int8_t>(shape->levels[dyCells][dxCells]) >= minLevel) {
            blocker = other;
            break;
        }
    }
    nearby.vtable = mfc::impl::PtrListClass::Vtable();
    nearby.RemoveAll();
    return blocker;
}
EI_REWRITTEN(0x005B7BB0, CAIMap::BlockingUnitAt);

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
EI_REWRITTEN(0x005B9510, CAIMap::CostContext);

void CAIMap::SetStepSpeeds(CUnit* unit, PathCellList* steps) {
    // The original also asks the unit for its movement speeds twice and divides by one of them, but never uses
    // the results; the getter has no side effects.
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
EI_REWRITTEN(0x005BA400, CAIMap::SetStepSpeeds);

bool CAIMap::StandsOnCheapCell(CUnit* unit) {
    PassCostContext context;
    CostContext(unit->sizeClass, &context);
    // The original also asks the unit for its movement speeds and ignores them.
    int x = CellOf(unit->localPosition_.x);
    int y = CellOf(unit->localPosition_.y);
    if (x < 0 || y < 0 || x >= cells.width || y >= cells.height) return false;
    if (HasKind3Occluder(x, y)) {
        int sizeClass = unit->sizeClass;
        int level = x87::fistp(static_cast<float>(static_cast<double>(kLevelHeights[sizeClass]) * heightScale));
        std::uint32_t passability[6];
        CellMark(passability, sizeClass, level, x, y);
        return static_cast<int>(passability[3]) < kExpensive;
    }
    int cost = context.costs[(cells[y][x].passable >> (context.shift & 31)) & 0xF];
    return cost > 0 && cost < kExpensive;
}
EI_REWRITTEN(0x005BA5A0, CAIMap::StandsOnCheapCell);


namespace {
void AllocateGrid(SearchGrid<std::uint8_t>* grid) {
    grid->data = static_cast<std::uint8_t*>(gamecrt::operator_new(grid->width * grid->height));
    grid->rows = static_cast<std::uint8_t**>(gamecrt::operator_new(grid->height * sizeof(std::uint8_t*)));
    for (int i = 0; i < grid->height; ++i) grid->rows[i] = grid->data + i * grid->width;
}
// A square grid of `size`: reallocated when the size differs, then cleared.
void ResizeCleared(SearchGrid<std::uint8_t>* grid, int size) {
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

void FogGrid::Grow(int x, int y, int n) {
    int left = x - n, right = x + n, top = y - n, bottom = y + n;
    if (x0 == 0x7FFFFFFF) {
        int size = (bottom - top > right - left ? bottom - top : right - left) + 1;
        ResizeCleared(&cells, size);
        x0 = left;
        y0 = top;
        return;
    }
    const int width = cells.width, height = cells.height;
    bool grow = left < x0 || top < y0 || right > x0 + width - 1 || bottom > y0 + height - 1;
    if (left >= x0) left = x0;
    if (right <= x0 + width - 1) right = x0 + width - 1;
    if (top >= y0) top = y0;
    if (bottom <= y0 + height - 1) bottom = y0 + height - 1;
    if (!grow) return;
    int size = (bottom - top > right - left ? bottom - top : right - left) + 1;
    // A copy of the old marks (block and row table), moved into the cleared grid.
    SearchGrid<std::uint8_t> old = cells;
    AllocateGrid(&old);
    std::memcpy(old.data, cells.data, static_cast<std::size_t>(old.width * old.height));
    ResizeCleared(&cells, size);
    for (int row = 0; row < old.height; ++row)
        for (int col = 0; col < old.width; ++col)
            cells.rows[row - top + y0][col - left + x0] = old.rows[row][col];
    x0 = left;
    y0 = top;
    gamecrt::operator_delete(old.data);
    gamecrt::operator_delete(old.rows);
}
EI_REWRITTEN(0x005C8D40, FogGrid::Grow);

void CAIMap::MarkVisibleCells(const Vector3* eye, float radius, const Vector3* look, float cosLimit,
                              float targetHeight, FogGrid* grid) {
    int eyeX = CellOf(eye->x);
    int eyeY = CellOf(eye->y);
    int reach = CellOf(radius) + 1;
    grid->Grow(eyeX, eyeY, reach);
    EI_NAN_AUDIT(cosLimit, targetHeight);
    bool anyDirection = cosLimit <= -1.0f;
    bool noLineOfSight = targetHeight < -100.0f;

    const double half = 1.0f * 0.5f;    // cell size * 0.5
    for (int y = eyeY - reach; y < eyeY + reach; ++y) {
        for (int x = eyeX - reach; x < eyeX + reach; ++x) {
            int gx = x - grid->x0;
            int gy = y - grid->y0;
            if (x * 9 < 0 || y < 0 || x >= cells.width || y >= cells.height) continue;
            Vector3 cell;
            cell.x = static_cast<float>(static_cast<double>(x) * half);
            cell.y = static_cast<float>(static_cast<double>(y) * half);
            cell.z = 0.0f;
            float dx = static_cast<float>(static_cast<double>(cell.x) - eye->x);
            float dy = static_cast<float>(static_cast<double>(cell.y) - eye->y);
            float dz = static_cast<float>(-static_cast<double>(eye->z));
            float distance = static_cast<float>(std::sqrt(static_cast<double>(LengthSquared2DUnrounded(dy, dx))));
            EI_NAN_AUDIT(radius, distance);
            if (radius < distance) continue;
            if (!anyDirection) {
                double dot = DotUnrounded<Sum3::ZYX, double>(dx, dy, dz, look->x, look->y, look->z);
                if (static_cast<double>(distance) * cosLimit > dot) continue;
            }
            if (!noLineOfSight) {
                cell.z = static_cast<float>(static_cast<double>(cells[y][x].groundLevel) / heightScale + targetHeight);
                double seen = LineOfSight(eye, &cell) * radius;
                EI_NAN_AUDIT(seen, distance);
                if (seen < distance) continue;
            }
            grid->cells.rows[gy][gx] = 1;
        }
    }
}
EI_REWRITTEN(0x005BC970, CAIMap::MarkVisibleCells);

namespace {
// The shape cache's map (UnitShapes()->shapes): an STLport hash_map<ShapeHandle, RefPtr>, the game's
// instantiation ShapeTable.
void ReleaseHandle(ShapeHandle* handle) {
    if (CRefObject* ref = handle->ref) {
        if (--ref->refs_ == 0) delete ref;     // delete tests ref again, as the original does
    }
}
} // namespace

ShapePair* ShapePair::Construct(const ShapeHandle* from, const RefPtr* shared) {
    ShapeHandle& to = key;
    to.cache = from->cache;
    to.ref = from->ref;
    if (to.ref) ++to.ref->refs_;
    to.key = from->key;
    to.height = from->height;
    value.Share(shared->ptr);
    return this;
}
EI_REWRITTEN(0x005BE370, ShapePair::Construct);

void __fastcall DestroyShapePair(ShapePair* pair) {
    pair->value.Destroy();
    ReleaseHandle(&pair->key);
}
EI_REWRITTEN(0x005BE280, DestroyShapePair);

void* CAIMap::UnitShape(float standingHeight) {
    ShapeHandle handle;
    handle.cache = nullptr;
    handle.ref = nullptr;
    handle.key = 0xFFFFFFFF;
    EI_NAN_AUDIT(standingHeight);
    if (standingHeight <= kShapeMinHeight) return nullptr;
    int tenths = x87::fistp(static_cast<float>(static_cast<double>(standingHeight) * 10.0f + 1.0f));
    handle.height = static_cast<float>(static_cast<double>(tenths) * 0.1f);
    std::memcpy(&handle.key, &handle.height, sizeof(handle.key));

    CRefObject* shape;
    ShapeTable* map = &UnitShapes()->shapes;
    if (const ShapeTable::Node* found = map->Find(handle)) {
        shape = found->value.value.ptr;
        handle.cache = UnitShapes();
        handle.counted.Assign(shape);
    } else {
        shape = UnitShapes()->CreateShape(&handle);
        RefPtr none = {nullptr};
        ShapePair pair;
        pair.Construct(&handle, &none);
        RefPtr* slot = &map->FindOrInsert(pair).value;
        DestroyShapePair(&pair);
        none.Destroy();
        if (shape != slot->ptr) {
            slot->Destroy();
            slot->Share(shape);
        }
        handle.cache = UnitShapes();
        handle.counted.Assign(shape);
    }
    ReleaseHandle(&handle);
    return shape;
}
EI_REWRITTEN(0x005B8350, CAIMap::UnitShape);
