// Path search on the AI map: snapping the target to a reachable cell, planning, and feeding the
// planned cells to the unit's steering.
#include "ai_map.h"
#include "game_data.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/mfc.h"
#include "originals_ai.h"
#include "originals_units.h"
#include "originals_core.h"

#include <cstring>

// --- Forwarders to original code not rewritten yet -----------------------------------------------

__declspec(naked) int CRegionMap::RegionAt(int, int, int) EI_ORIGINAL(0x005B0600)

namespace {
// 005BD200: appends a copy of `cell` to the list.
__declspec(naked) void __fastcall AddPathCell(PathCellList*, void*, const PathCell*) EI_ORIGINAL(0x005BD200)
// MFC ConstructElements<PathCell> (zero-fills `count` elements).
// The direction a quaternion faces (the orientation at CEngineObject + 0x34).
void __fastcall FacingDirection(const Quaternion* q, void*, Vector3* out) { QuaternionAxisY(q, nullptr, out); }

// Local searches at CAIMap + 0x2594 / + 0x2614 (forward and backward), the path builder and the
// long-range planner.
bool BeginLocalSearch(LocalSearch* search, CUnit* unit, int x0, int y0, int x1, int y1) {
    return search->Begin(unit, x0, y0, x1, y1);
}
// 005C46C0 on CAIMap + 0x22CC: smooths the start of the path; the result goes to AIPath::smoothing.
__declspec(naked) int __fastcall SmoothPath(PathSmoother*, void*, CUnit*, CAIMap*, PathCellList*, int, std::uint32_t)
    EI_ORIGINAL(0x005C46C0)

// The sector map through its original addresses (rewritten in sector_map.cpp), so the verify
// build's wrappers see these calls.

const Vector3& PositionOf(const CUnit* unit) { return *&unit->position; }

bool IsCellPassable(CAIMap* map, CUnit* unit, int x, int y) {
    return ((map->cells[y][x].passable >> ((unit->sizeClass * 4) & 31)) & 0xF) != 0;
}

bool IsNaN(float f) {
    std::uint32_t bits;
    std::memcpy(&bits, &f, sizeof(bits));
    return (bits & 0x7FFFFFFF) > 0x7F800000;
}

float Abs(float f) {
    std::uint32_t bits;
    std::memcpy(&bits, &f, sizeof(bits));
    bits &= 0x7FFFFFFF;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

// 007AFFC4: the world tick, the start time of the order AdvancePath gives.
int WorldTick() { return ei::GameData<const int>(0x007AFFC4); }

constexpr float kDiagonal = 0.707f;     // 0073FDAC
constexpr float kInset = 0.49f;         // 0073FDA8
} // namespace

// 005B1B70: CList<PathCell>::NewNode.
EI_REPLACE_METHOD(0x005B1B70, PathCellList::NewNode);

bool CAIMap::SnapTarget(CUnit* unit, Vector3* target, bool sameRegion, float minDistance) {
    int tx = WorldToCell(target->x), ty = WorldToCell(target->y);
    target->x = 1.0f / (1.0f * 0.5f) * target->x;
    target->y = 1.0f / (1.0f * 0.5f) * target->y;
    int reason;
    if (!sameRegion) {
        if (unit->GetClassId() != kClassUnit) return true;
        if (CanStandAt(unit, tx, ty, &reason) && IsCellPassable(this, unit, tx, ty)) return true;
    }

    Vector3 from;
    std::memcpy(&from, &PositionOf(unit), sizeof(from));
    int ux = WorldToCell(from.x), uy = WorldToCell(from.y);
    int sizeClass = unit->sizeClass;
    int region = 0;
    auto** markRows = reinterpret_cast<std::uint8_t**>(marks.rows);
    if (sameRegion) {
        region = Regions()->RegionAt(sizeClass, ux, uy);
        // Clear the reachability marks in the box spanned by the unit and the target (and a margin).
        int dx = (tx - ux < 0 ? ux - tx : tx - ux) + 1;
        int x0 = tx - dx, x1 = tx + dx + 1;
        int dy = (ty - uy < 0 ? uy - ty : ty - uy) + 1;
        int y0 = ty - dy, y1 = ty + dy + 1;
        if (x0 < 0) x0 = 0;
        if (width < x1) x1 = width;
        if (y0 < 0) y0 = 0;
        if (height < y1) y1 = height;
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) markRows[y][x] = 0;
        }
    }

    // Unit steps along the line from the target back to the unit (the longer axis steps by 1).
    int dyCells = ty - uy, dxCells = tx - ux;
    int ady = dyCells < 0 ? -dyCells : dyCells;
    int adx = dxCells < 0 ? -dxCells : dxCells;
    int steps;
    float sx, sy;
    if (adx > ady) {
        sy = static_cast<float>(dyCells) / static_cast<float>(adx);
        steps = adx;
        sx = dxCells < 0 ? -1.0f : 1.0f;
    } else {
        sx = static_cast<float>(dxCells) / static_cast<float>(ady);
        steps = ady;
        sy = dyCells < 0 ? -1.0f : 1.0f;
    }
    // The step rotated by -45 and +45 degrees, scaled so that no component exceeds 1.
    float p = sx * kDiagonal - sy * kDiagonal;
    float q = (sy + sx) * kDiagonal;
    float scale = 1.0f / (Abs(q) < Abs(p) || IsNaN(p) || IsNaN(q) ? Abs(p) : Abs(q));   // FCOMPP: C0 if less or NaN
    if (!(scale >= 1.0f)) {
        p = scale * p;
        q = scale * q;
        sy = scale * sy;
        sx = scale * sx;
    }
    // Directions tried at each distance: the step, then the others round the circle.
    const float directions[8][2] = {{sx, sy}, {p, q}, {q, -p}, {sy, -sx}, {-sy, sx}, {-q, p}, {-p, -q}, {-sx, -sy}};

    int first = x87::fistp(1.0f / (1.0f * 0.5f) * minDistance) - 1;
    if (first < 0) first = 0;
    for (int r = first; r <= steps; ++r) {
        int count = r != 0 ? 8 : 1;
        float distance = static_cast<float>(r);
        for (int d = 0; d < count; ++d) {
            int cx = tx - x87::fistp(distance * directions[d][0]);
            int cy = ty - x87::fistp(distance * directions[d][1]);
            if (unit->GetClassId() == kClassUnit) {
                if (!CanStandAt(unit, cx, cy, &reason) || !IsCellPassable(this, unit, cx, cy)) continue;
            }
            if (sameRegion) {
                std::uint8_t mark = markRows[cy][cx];
                if (mark != 1) {
                    if (mark == 2) continue;
                    if (!Regions()->ReachesRegion(sizeClass, cx, cy, region, &marks)) continue;
                }
            }
            if (r != 0) {
                // The centre of the cell, pulled 0.49 back towards the target cell.
                float fx = static_cast<float>(cx) + 0.5f;
                target->x = fx;
                float fy = static_cast<float>(cy) + 0.5f;
                target->y = fy;
                if (fx > static_cast<float>(tx)) target->x = fx - kInset;
                if (static_cast<float>(tx) > target->x) target->x = target->x + kInset;
                if (fy > static_cast<float>(ty)) target->y = fy - kInset;
                if (static_cast<float>(ty) > target->y) target->y = target->y + kInset;
            }
            return true;
        }
    }
    return false;
}
EI_REPLACE_METHOD(0x005BAC10, CAIMap::SnapTarget);

void CAIMap::AdvancePath(CUnit* unit, AIPath* path) {
    path->Reset();
    int count = WorldTick();
    Vector3 facing;
    FacingDirection(&unit->orientation, nullptr, &facing);
    Vector3 backwards = {-facing.x, -facing.y, -facing.z};
    path->Start(&PositionOf(unit), &backwards, count - 1);

    // Move cells from the head of the path to the tail of the steps.
    PathCellList& pathCells = path->cells;
    PathCellList& steps = path->steps;
    while (steps.count < 32 && pathCells.count > 0) {
        PathCellList::Node* node = pathCells.head;
        PathCell cell = node->data;
        pathCells.head = node->next;
        if (node->next) node->next->prev = nullptr;
        else pathCells.tail = nullptr;
        node->next = pathCells.free;
        pathCells.free = node;
        if (--pathCells.count == 0) pathCells.RemoveAll();

        PathCellList::Node* added = steps.NewNode(steps.tail, nullptr);
        added->data = cell;
        if (steps.tail) steps.tail->next = added;
        else steps.head = added;
        steps.tail = added;
    }

    float x, y;
    if (pathCells.count > 0) {
        path->reached = false;
        // Put the last step back at the head of the path: it is where the steering heads.
        const PathCell* last = &steps.tail->data;
        PathCellList::Node* oldHead = pathCells.head;
        if (!pathCells.free) {
            auto* block = mfc::PlexCreate(reinterpret_cast<mfc::CPlex*&>(pathCells.blocks), pathCells.blockSize,
                                          sizeof(PathCellList::Node));
            PathCellList::Node* node =
                reinterpret_cast<PathCellList::Node*>(reinterpret_cast<char*>(block) + 4) + (pathCells.blockSize - 1);
            for (int i = pathCells.blockSize - 1; i >= 0; --i, --node) {
                node->next = pathCells.free;
                pathCells.free = node;
            }
        }
        PathCellList::Node* node = pathCells.free;
        pathCells.free = node->next;
        node->prev = nullptr;
        node->next = oldHead;
        ++pathCells.count;
        ConstructPathCells(&node->data, 1);
        node->data = *last;
        if (pathCells.head) pathCells.head->prev = node;
        else pathCells.tail = node;
        pathCells.head = node;
        y = static_cast<float>(last->y) + 0.5f;
        x = static_cast<float>(last->x) + 0.5f;
    } else {
        std::memcpy(&y, &path->targetY, sizeof(y));
        std::memcpy(&x, &path->targetX, sizeof(x));
        path->reached = true;
    }
    path->SetTarget(x, y);
    SetStepSpeeds(unit, &path->steps);
}
EI_REPLACE_METHOD(0x005BB190, CAIMap::AdvancePath);

bool CAIMap::FindPath(CUnit* unit, const Vector3* target, AIPath* path, float range, float minDistance) {
    Vector3 from, goal;
    std::memcpy(&from, &PositionOf(unit), sizeof(from));
    std::memcpy(&goal, target, sizeof(goal));
    std::memcpy(&path->goal, target, sizeof(goal));
    if (LengthSquared2DUnrounded(target->x, target->y) > 100000000.0) return false;   // NaN goes on

    SectorMapRemove(SectorMap(), nullptr, unit);
    if (!SnapTarget(unit, &goal, false, minDistance)) {
        SectorMapAdd(SectorMap(), nullptr, unit);
        return false;
    }
    std::memcpy(&path->targetY, &goal.y, 4);
    std::memcpy(&path->targetX, &goal.x, 4);

    // In half-cell units, like the snapped target.
    float fx = 1.0f / (1.0f * 0.5f) * from.x;
    float fy = 1.0f / (1.0f * 0.5f) * from.y;
    float ddx = fx - goal.x, ddy = fy - goal.y;
    if (!(LengthSquared2DUnrounded(ddy, ddx) >= 0.001f)) {
        SectorMapAdd(SectorMap(), nullptr, unit);
        return false;
    }
    int tx = x87::fistp(goal.x - 0.5f);
    int ty = x87::fistp(goal.y - 0.5f);
    int ux = x87::fistp(fx - 0.5f);
    float uyHalf = fy - 0.5f;   // also ends up in the path cell's second half below
    int uy = x87::fistp(uyHalf);

    bool found = false;
    PathCell cell;
    PathCellList& pathCells = path->cells;
    if (ux == tx && uy == ty) {
        // Already in the target cell.
        int reason;
        if (unit->GetClassId() == kClassUnit && !(CanStandAt(unit, tx, ty, &reason) && IsCellPassable(this, unit, tx, ty))) {
            goto done;
        }
        cell.x = static_cast<std::int16_t>(ux);
        cell.y = static_cast<std::int16_t>(uy);
        std::memcpy(&cell.flags, &uyHalf, 4);
        AddPathCell(&pathCells, nullptr, &cell);
        found = true;
        pathCells.head->data.flags = 0x200;
        AdvancePath(unit, path);
        goto done;
    }

    {
        int adx = ux - tx < 0 ? tx - ux : ux - tx;
        int ady = uy - ty < 0 ? ty - uy : uy - ty;
        int shorter = adx < ady ? adx : ady;
        int diff = adx - ady < 0 ? ady - adx : adx - ady;
        bool tried = false;
        if (diff + ((shorter * 0x5A8) >> 10) <= 24) {
            // Close: a two-way local search.
            pathCells.RemoveAll();
            if (ux != tx || uy != ty) {
                if (BeginLocalSearch(&forwardSearch, unit, ux, uy, tx, ty)) {
                    if (!BeginLocalSearch(&backwardSearch, unit, tx, ty, ux, uy)) {
                        static_cast<LocalSearch*>(&forwardSearch)->Reset();
                    } else {
                        found = JoinLocalSearches(&pathCells, nullptr);
                        tried = found;
                    }
                }
            }
        }
        if (!tried) {
            // Far, or the local search failed: snap to a cell in the unit's region and plan.
            std::memcpy(&goal, target, sizeof(goal));
            if (!SnapTarget(unit, &goal, true, minDistance)) {
                SectorMapAdd(SectorMap(), nullptr, unit);
                return false;
            }
            int gx = x87::fistp(goal.x - 0.5f);
            int gy = x87::fistp(goal.y - 0.5f);
            std::memcpy(&path->targetX, &goal.x, 4);
            std::memcpy(&path->targetY, &goal.y, 4);
            if (ux == gx && uy == gy) {
                PathCellList::Node* node = pathCells.NewNode(nullptr, pathCells.head);
                node->data.x = static_cast<std::int16_t>(ux);
                node->data.y = static_cast<std::int16_t>(uy);
                std::memcpy(&node->data.flags, &shorter, 4);
                if (pathCells.head) pathCells.head->prev = node;
                else pathCells.tail = node;
                pathCells.head = node;
                found = true;
                pathCells.head->data.flags = 0x200;
            } else {
                int flag = path->flag6E || path->flag6F ? 1 : 0;
                int rangeCells = WorldToCell(range);
                found = PlanPath(this, nullptr, unit, ux, uy, gx, gy, path, flag, rangeCells);
                if (!found) goto done;
            }
        }

        // Smooth the start of the path, entering from the side the unit faces.
        Vector3 facing;
        FacingDirection(&unit->orientation, nullptr, &facing);
        float bx = -facing.x, by = -facing.y;
        float px;
        std::memcpy(&px, &path->targetX, 4);
        float a = Abs(px - static_cast<float>(x87::fistp(px)));
        std::memcpy(&px, &path->targetX, 4);    // the original reads targetX twice (not targetY)
        float b = Abs(px - static_cast<float>(x87::fistp(px)));
        float b2 = b + b, a2 = a + a;
        float fraction = b2 < a2 ? b2 : a2;   // a and b are the same value (see above)
        std::uint32_t fractionBits;
        std::memcpy(&fractionBits, &fraction, 4);
        int octant = x87::fistp(static_cast<float>(x87::fpatan(by, bx) * 1.273239605931458));
        int direction = 6 - octant;
        if (direction < 0) direction += static_cast<int>((static_cast<unsigned>(7 - direction) >> 3) * 8);
        if (direction > 7) direction -= static_cast<int>((static_cast<unsigned>(direction) >> 3) * 8);
        path->smoothing = SmoothPath(&smoother, nullptr, unit, this, &pathCells,
                                     direction + 1, fractionBits);
        if (!found) goto done;
        AdvancePath(unit, path);
    }

done:
    SectorMapAdd(SectorMap(), nullptr, unit);
    if (!found) path->failed = true;
    path->flag6F = false;
    return found;
}
EI_REPLACE_METHOD(0x005BB350, CAIMap::FindPath);

bool CAIMap::ContinuePath(CUnit* unit, AIPath* path) {
    bool wasReached = path->reached;
    if (path->failed && path->cells.count == 0) return FindPath(unit, &path->goal, path, 1000000.0f, 0.0f);
    AdvancePath(unit, path);
    if (path->failed) path->reached = false;
    return !wasReached;
}
EI_REPLACE_METHOD(0x005BB900, CAIMap::ContinuePath);
