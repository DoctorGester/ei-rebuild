// The footprints of walls and clouds (spell effects): 005B2A00 fills a cell set with the cells of
// a disc around a point limited by a line along a direction, 005B2760 with the cells of a disc.
// Positions are scaled to cells by 1 / (0x73FD18 * 0.5) (2: half-metre cells); the radius is grown
// by a cell. Each cell gets low 0, high 1, kind 3, transparency 3 and is appended without hashing
// (the buckets are emptied when the set was hashed). See ai_map.h and docs/notes/ai.md.
#include "ai_map.h"
#include "core_maths.h"

#include "ei/hook.h"
#include "ei/x87.h"

namespace {

float GameFloat(std::uintptr_t address) { return *reinterpret_cast<const volatile float*>(address); }
constexpr std::uintptr_t kCellSize = 0x0073FD18;   // 1.0
constexpr std::uintptr_t kHalf = 0x0073B86C;       // 0.5
constexpr std::uintptr_t kOne = 0x0073B84C;        // 1.0

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
    cell.unknown_13 = 0;
    cell.next = -1;
}

struct Bounds {
    int x0, x1, y0, y1;
};

// The cells within the radius' square, rounded outwards by one and clamped to the map (the upper
// bounds to width - 1 / height - 1).
Bounds SquareAround(CAIMap* map, float cx, float cy, float r) {
    Bounds b;
    b.x0 = x87::fistp(StoreFloat(static_cast<float>(static_cast<double>(cx) - r))) - 1;
    b.y0 = x87::fistp(StoreFloat(static_cast<float>(static_cast<double>(cy) - r))) - 1;
    b.x1 = x87::fistp(StoreFloat(static_cast<float>(static_cast<double>(r) + cx))) + 1;
    b.y1 = x87::fistp(StoreFloat(static_cast<float>(static_cast<double>(r) + cy))) + 1;
    if (b.x0 < 0) b.x0 = 0;
    if (b.y0 < 0) b.y0 = 0;
    if (b.x1 >= map->width - 1) b.x1 = map->width - 1;
    if (b.y1 >= map->height - 1) b.y1 = map->height - 1;
    return b;
}

} // namespace

#pragma float_control(precise, on, push)

// 005B2A00 (thiscall on the set, RET 0x10).
void __fastcall FootprintAlongDirection(FootprintCellSet* set, void*, CAIMap* map, const Vector3* position, float width,
                                        const Vector3* direction) {
    double scale = static_cast<double>(GameFloat(kOne)) /
                   (static_cast<double>(GameFloat(kCellSize)) * static_cast<double>(GameFloat(kHalf)));
    float cx = StoreFloat(static_cast<float>(scale * position->x));
    float cy = StoreFloat(static_cast<float>(scale * position->y));
    double dx = direction->x, dy = direction->y, dz = direction->z;
    double length = x87::fsqrt(LengthSquaredUnrounded<Sum3::XYZ>(dx, dy, dz));
    double reach = length * scale + static_cast<double>(GameFloat(kOne));
    float r = StoreFloat(static_cast<float>(reach));
    float r2 = StoreFloat(static_cast<float>(reach * r));
    // The side normal (-dy, dx), divided by its length times the scaled width plus one.
    float nx = -direction->y;
    float ny = direction->x;
    double side = x87::fsqrt(LengthSquared2DUnrounded<double>(ny, nx));
    double divisor = side * (scale * width + static_cast<double>(GameFloat(kOne)));
    nx = StoreFloat(static_cast<float>(nx / divisor));
    ny = StoreFloat(static_cast<float>(ny / divisor));

    ResetSet(set);
    Bounds b = SquareAround(map, cx, cy, r);
    for (int y = b.y0; y <= b.y1; ++y)
        for (int x = b.x0; x <= b.x1; ++x) {
            float fy = StoreFloat(static_cast<float>(cy - static_cast<double>(y)));
            double fx = cx - static_cast<double>(x);
            if (!(LengthSquared2DUnrounded<double>(fy, fx) < r2)) continue;
            if (!(Dot2DUnrounded<double>(fy, fx, ny, nx) < static_cast<double>(GameFloat(kOne)))) continue;
            AddCell(set, x, y);
        }
}

// 005B2760 (thiscall on the set, RET 0xC).
void __fastcall FootprintDiscAt(FootprintCellSet* set, void*, CAIMap* map, const Vector3* position, float radius) {
    double scale = static_cast<double>(GameFloat(kOne)) /
                   (static_cast<double>(GameFloat(kCellSize)) * static_cast<double>(GameFloat(kHalf)));
    float cx = StoreFloat(static_cast<float>(scale * position->x));
    float cy = StoreFloat(static_cast<float>(scale * position->y));
    double reach = scale * radius + static_cast<double>(GameFloat(kOne));
    float r = StoreFloat(static_cast<float>(reach));
    float r2 = StoreFloat(static_cast<float>(reach * r));

    ResetSet(set);
    Bounds b = SquareAround(map, cx, cy, r);
    for (int y = b.y0; y <= b.y1; ++y)
        for (int x = b.x0; x <= b.x1; ++x) {
            double fy = cy - static_cast<double>(y);
            float fy2 = StoreFloat(static_cast<float>(fy * fy));
            double fx = cx - static_cast<double>(x);
            if (!(fx * fx + fy2 < r2)) continue;
            AddCell(set, x, y);
        }
}

#pragma float_control(pop)

EI_REPLACE(0x005B2A00, FootprintAlongDirection);
EI_REPLACE(0x005B2760, FootprintDiscAt);
