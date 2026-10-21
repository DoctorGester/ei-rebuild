// The footprints of walls and clouds (spell effects): 005B2A00 fills a cell set with the cells of
// a disc around a point limited by a line along a direction, 005B2760 with the cells of a disc.
// Positions are scaled to cells by 1 / (0x73FD18 * 0.5) (2: half-metre cells); the radius is grown
// by a cell. Each cell gets low 0, high 1, kind 3, transparency 3 and is appended without hashing
// (the buckets are emptied when the set was hashed). See ai_map.h and docs/notes/ai.md.
#include "ai_map.h"
#include "core_maths.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "world_data.h"
#include "lib/nan_audit.h"

namespace {


// Empties the set (SetSize(0, -1)) and its buckets when it was hashed.
void ResetSet(FootprintCellSet* set) {
    set->SetSize(0, -1);
    if (set->hashed)
        for (int& bucket : set->buckets) bucket = -1;
}

// The cell record of these footprints. The original appends it from a stack copy whose byte 0x13
// and `next` it never writes (stack garbage): ours writes 0 and -1; nothing reads them, as the
// cells are in no bucket.
void AddCell(FootprintCellSet* set, int x, int y) {
    int index = set->cells.size;
    set->SetSize(index + 1, -1);
    FootprintCell& cell = set->cells[index];
    cell.x = x;
    cell.y = y;
    cell.low = 0;
    cell.high = 1;
    cell.kind = 3;
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
Bounds SquareAround(CAIMap* map, float cx, float cy, float r) {
    Bounds b;
    b.x0 = x87::fistp(static_cast<float>(static_cast<double>(cx) - r)) - 1;
    b.y0 = x87::fistp(static_cast<float>(static_cast<double>(cy) - r)) - 1;
    b.x1 = x87::fistp(static_cast<float>(static_cast<double>(r) + cx)) + 1;
    b.y1 = x87::fistp(static_cast<float>(static_cast<double>(r) + cy)) + 1;
    if (b.x0 < 0) b.x0 = 0;
    if (b.y0 < 0) b.y0 = 0;
    if (b.x1 >= map->width - 1) b.x1 = map->width - 1;
    if (b.y1 >= map->height - 1) b.y1 = map->height - 1;
    return b;
}

} // namespace

// 005B2A00 (thiscall on the set, RET 0x10).
void FootprintCellSet::AlongDirection(CAIMap* map, const Vector3* position, float width, const Vector3* direction) {
    double scale = static_cast<double>(worlddata::One()) /
                   (static_cast<double>(worlddata::CellSize()) * static_cast<double>(worlddata::Half()));
    float cx = static_cast<float>(scale * position->x);
    float cy = static_cast<float>(scale * position->y);
    double dx = direction->x, dy = direction->y, dz = direction->z;
    double length = std::sqrt(LengthSquaredUnrounded<Sum3::XYZ>(dx, dy, dz));
    double reach = length * scale + static_cast<double>(worlddata::One());
    float r = static_cast<float>(reach);
    float r2 = static_cast<float>(reach * r);
    // The side normal (-dy, dx), divided by its length times the scaled width plus one.
    float nx = -direction->y;
    float ny = direction->x;
    double side = std::sqrt(LengthSquared2DUnrounded<double>(ny, nx));
    double divisor = side * (scale * width + static_cast<double>(worlddata::One()));
    nx = static_cast<float>(nx / divisor);
    ny = static_cast<float>(ny / divisor);

    ResetSet(this);
    Bounds b = SquareAround(map, cx, cy, r);
    for (int y = b.y0; y <= b.y1; ++y)
        for (int x = b.x0; x <= b.x1; ++x) {
            float fy = static_cast<float>(cy - static_cast<double>(y));
            double fx = cx - static_cast<double>(x);
            EI_NAN_AUDIT(fy, fx, r2, ny, nx);
            if (LengthSquared2DUnrounded<double>(fy, fx) >= r2) continue;
            if (Dot2DUnrounded<double>(fy, fx, ny, nx) >= static_cast<double>(worlddata::One())) continue;
            AddCell(this, x, y);
        }
}

// 005B2760 (thiscall on the set, RET 0xC).
void FootprintCellSet::DiscAt(CAIMap* map, const Vector3* position, float radius) {
    double scale = static_cast<double>(worlddata::One()) /
                   (static_cast<double>(worlddata::CellSize()) * static_cast<double>(worlddata::Half()));
    float cx = static_cast<float>(scale * position->x);
    float cy = static_cast<float>(scale * position->y);
    double reach = scale * radius + static_cast<double>(worlddata::One());
    float r = static_cast<float>(reach);
    float r2 = static_cast<float>(reach * r);

    ResetSet(this);
    Bounds b = SquareAround(map, cx, cy, r);
    for (int y = b.y0; y <= b.y1; ++y)
        for (int x = b.x0; x <= b.x1; ++x) {
            double fy = cy - static_cast<double>(y);
            float fy2 = static_cast<float>(fy * fy);
            double fx = cx - static_cast<double>(x);
            EI_NAN_AUDIT(fx, fy2, r2);
            if (fx * fx + fy2 >= r2) continue;
            AddCell(this, x, y);
        }
}

EI_REWRITTEN(0x005B2A00, FootprintCellSet::AlongDirection);
EI_REWRITTEN(0x005B2760, FootprintCellSet::DiscAt);
