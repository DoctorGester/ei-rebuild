// The unit shapes (UnitShape, ai/unit_shape.h): the 16 x 16 cell levels a unit of a radius stamps on the AI
// map's unit layers. Shared through the shape cache, whose creator virtual makes them. See docs/notes/ai.md.
#include "game/ai/ai_map.h"
#include "game/ai/unit_shape.h"
#include "engine/core/core_maths.h"
#include "engine/core/core_refcount.h"

#include "lib/containers.h"
#include "lib/rounding.h"

#include <cstring>

namespace {

// Constants of the shape maths (the cell size, halved, among them).
constexpr float kMargin = 2.0f;
constexpr float kHalfCell = 0.5f;
constexpr float kLevelsPerUnit = 16.0f;
constexpr int kMiddle = 8;

} // namespace

// The unit shape cache.
UnitShapeCache g_unitShapes;

UnitShape::UnitShape(float newRadius) {
    radius = newRadius;
    std::memset(levels, 0, sizeof(levels));
    for (int dy = -kMiddle; dy < kMiddle; ++dy) {
        int rowSquared = dy * dy;
        for (int dx = -kMiddle; dx < kMiddle; ++dx) {
            float distance = std::sqrt(static_cast<float>(dx * dx + rowSquared));
            float level = ((kMargin + radius) - distance * kHalfCell) * kLevelsPerUnit;
            int value = lib::RoundEven(level);
            if (value > 0) levels[dy + kMiddle][dx + kMiddle] = static_cast<std::uint8_t>(value);
        }
    }
}

UnitShape::UnitShape() {
    OnShutdown();
}

void UnitShape::OnShutdown() {
    std::memset(levels, 0, sizeof(levels));
    radius = -1.0f;
}

UnitShape* UnitShape::Clone() {
    auto* copy = new UnitShape();
    std::memcpy(copy->levels, levels, sizeof(levels));
    copy->radius = radius;
    return copy;
}

UnitShape* CreateUnitShape(const ShapeCacheHandle* handle) { return new UnitShape(handle->height); }

// --- Ai shape cache ----------------------------------------------------------------------------------

// The object for a handle {cache, ref, key, height} of a reference-counted object cache. A handle that
// already holds a live (not detached) object of this cache returns it. Otherwise the cache's hash map (nodes
// {next, handle (0x10 bytes), value}) is searched by key (bucket key % count) and height (compared as floats,
// unordered counting as equal); a found value, or else a new object from the cache's creator virtual that is
// stored under the handle (the old value released as an owner and the new one shared), becomes the handle's
// reference. Used for the unit shapes (ai_movement.cpp, which defines the cache).

UnitShape* UnitShapeCache::Get(ShapeCacheHandle* handle) {
    if (handle->cache == this && IsLive(handle->ref)) return handle->ref;
    return Resolve(handle);
}

UnitShape* UnitShapeCache::CreateShape(const ShapeCacheHandle* handle) { return CreateUnitShape(handle); }

// --- Ai footprint shapes -----------------------------------------------------------------------------

// The footprints of walls and clouds (spell effects): one function fills a cell set with the cells of a disc
// around a point limited by a line along a direction, the other with the cells of a disc. Positions are
// scaled to cells by 1 / (0x73FD18 * 0.5) (2: half-metre cells); the radius is grown by a cell. Each cell gets
// low 0, high 1, kind SpellArea, transparency 3 and is appended without hashing (the buckets are emptied). See
// ai_map.h and docs/notes/ai.md.

namespace {

// Empties the set and its buckets.
void ResetSet(FootprintCellSet* set) {
    set->SetSize(0);
    for (int& bucket : set->buckets) bucket = -1;
}

// The cell record of these footprints. The original appends it from a stack copy whose byte 0x13 and `next`
// it never writes (stack garbage): ours writes 0 and -1; nothing reads them, as the cells are in no bucket.
void AddCell(FootprintCellSet* set, int x, int y) {
    int index = lib::Count(set->cells);
    set->SetSize(index + 1);
    FootprintCell& cell = set->cells[index];
    cell.x = x;
    cell.y = y;
    cell.low = 0;
    cell.high = 1;
    cell.kind = OccluderKind::SpellArea;
    cell.transparency = 3;
    cell.edge = 0;
    cell.padding = 0;
    cell.next = -1;
}

struct Bounds {
    int x0 = 0, x1 = 0, y0 = 0, y1 = 0;
};

// The cells within the radius' square, rounded outwards by one and clamped to the map (the upper
// bounds to width - 1 / height - 1).
Bounds SquareAround(AIMap* map, float cx, float cy, float r) {
    Bounds b;
    b.x0 = lib::RoundEven(cx - r) - 1;
    b.y0 = lib::RoundEven(cy - r) - 1;
    b.x1 = lib::RoundEven(r + cx) + 1;
    b.y1 = lib::RoundEven(r + cy) + 1;
    if (b.x0 < 0) b.x0 = 0;
    if (b.y0 < 0) b.y0 = 0;
    if (b.x1 >= map->cells.width - 1) b.x1 = map->cells.width - 1;
    if (b.y1 >= map->cells.height - 1) b.y1 = map->cells.height - 1;
    return b;
}

} // namespace

// The disc limited by a line, at half a unit's width from it.
void FootprintCellSet::AlongDirection(AIMap* map, const Vector3& position, const Vector3& direction) {
    constexpr float kWidth = 0.5f;
    float scale = 1.0f / 0.5f;
    float cx = scale * position.x;
    float cy = scale * position.y;
    float length = Length(direction);
    float reach = length * scale + 1.0f;
    float r = reach;
    float r2 = reach * r;
    // The side normal (-dy, dx), divided by its length times the scaled width plus one.
    float nx = -direction.y;
    float ny = direction.x;
    float side = std::sqrt(LengthSquared2D(ny, nx));
    float divisor = side * (scale * kWidth + 1.0f);
    nx /= divisor;
    ny /= divisor;

    ResetSet(this);
    Bounds b = SquareAround(map, cx, cy, r);
    for (int y = b.y0; y <= b.y1; ++y)
        for (int x = b.x0; x <= b.x1; ++x) {
            float fy = cy - static_cast<float>(y);
            float fx = cx - static_cast<float>(x);
            if (LengthSquared2D(fy, fx) >= r2) continue;
            if (Dot2D(fy, fx, ny, nx) >= 1.0f) continue;
            AddCell(this, x, y);
        }
}

// The disc.
void FootprintCellSet::DiscAt(AIMap* map, const Vector3& position, float radius) {
    float scale = 1.0f / 0.5f;
    float cx = scale * position.x;
    float cy = scale * position.y;
    float reach = scale * radius + 1.0f;
    float r = reach;
    float r2 = reach * r;

    ResetSet(this);
    Bounds b = SquareAround(map, cx, cy, r);
    for (int y = b.y0; y <= b.y1; ++y)
        for (int x = b.x0; x <= b.x1; ++x) {
            float fy = cy - static_cast<float>(y);
            float fy2 = fy * fy;
            float fx = cx - static_cast<float>(x);
            if (fx * fx + fy2 >= r2) continue;
            AddCell(this, x, y);
        }
}
