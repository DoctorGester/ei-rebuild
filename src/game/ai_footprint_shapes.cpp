// The footprints of walls and clouds (spell effects): one function fills a cell set with the cells of a disc
// around a point limited by a line along a direction, the other with the cells of a disc. Positions are
// scaled to cells by 1 / (0x73FD18 * 0.5) (2: half-metre cells); the radius is grown by a cell. Each cell gets
// low 0, high 1, kind SpellArea, transparency 3 and is appended without hashing (the buckets are emptied when the set
// was hashed). See ai_map.h and docs/notes/ai.md.
#include "ai_map.h"
#include "core_maths.h"

#include "lib/containers.h"
#include "lib/rounding.h"
#include "world_data.h"
#include "lib/nan_audit.h"

namespace {


// Empties the set (SetSize(0, -1)) and its buckets when it was hashed.
void ResetSet(FootprintCellSet* set) {
    set->SetSize(0);
    if (set->hashed)
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
    int x0, x1, y0, y1;
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
    float dx = direction.x, dy = direction.y, dz = direction.z;
    float length = std::sqrt(LengthSquaredUnrounded<Sum3::XYZ>(dx, dy, dz));
    float reach = length * scale + 1.0f;
    float r = reach;
    float r2 = reach * r;
    // The side normal (-dy, dx), divided by its length times the scaled width plus one.
    float nx = -direction.y;
    float ny = direction.x;
    float side = std::sqrt(LengthSquared2DUnrounded(ny, nx));
    float divisor = side * (scale * kWidth + 1.0f);
    nx = nx / divisor;
    ny = ny / divisor;

    ResetSet(this);
    Bounds b = SquareAround(map, cx, cy, r);
    for (int y = b.y0; y <= b.y1; ++y)
        for (int x = b.x0; x <= b.x1; ++x) {
            float fy = cy - static_cast<float>(y);
            float fx = cx - static_cast<float>(x);
            EI_NAN_AUDIT(fy, fx, r2, ny, nx);
            if (LengthSquared2DUnrounded(fy, fx) >= r2) continue;
            if (Dot2DUnrounded(fy, fx, ny, nx) >= 1.0f) continue;
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
            EI_NAN_AUDIT(fx, fy2, r2);
            if (fx * fx + fy2 >= r2) continue;
            AddCell(this, x, y);
        }
}
