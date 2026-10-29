// Path search on the AI map: snapping the target to a reachable cell, planning, and feeding the
// planned cells to the unit's steering.
#include "ai_map.h"

#include "ei/x87.h"
#include <bit>
#include <cstdint>
#include <cstring>
#include "lib/nan_audit.h"
#include "lib/vecmath.h"
#include "game_singletons.h"
#include "sector_map.h"
#include "world.h"
#include "lib/float_select.h"


// --- Helpers -------------------------------------------------------------------------------------

namespace {

bool IsCellPassable(CAIMap* map, CUnit* unit, int x, int y) {
    return map->cells[y][x].Passability(unit->sizeClass * 4) != 0;
}

constexpr float kDiagonal = 0.707f;
constexpr float kInset = 0.49f;
} // namespace

bool CAIMap::SnapTarget(CUnit* unit, Vector3* target, bool sameRegion, float minDistance) {
    int tx = WorldToCell(target->x), ty = WorldToCell(target->y);
    target->x = 2.0f * target->x;
    target->y = 2.0f * target->y;
    int reason;
    if (!sameRegion) {
        if (unit->GetClassId() != ClassId::Unit) return true;
        if (CanStandAt(unit, tx, ty, &reason) && IsCellPassable(this, unit, tx, ty)) return true;
    }

    Vector3 from;
    from = unit->localPosition_;
    int ux = WorldToCell(from.x), uy = WorldToCell(from.y);
    int sizeClass = unit->sizeClass;
    int region = 0;
    if (sameRegion) {
        region = regionMap.DominantRegion(sizeClass, ux, uy);
        // Clear the reachability marks in the box spanned by the unit and the target (and a margin).
        int dx = (tx - ux < 0 ? ux - tx : tx - ux) + 1;
        int x0 = tx - dx, x1 = tx + dx + 1;
        int dy = (ty - uy < 0 ? uy - ty : ty - uy) + 1;
        int y0 = ty - dy, y1 = ty + dy + 1;
        if (x0 < 0) x0 = 0;
        if (cells.width < x1) x1 = cells.width;
        if (y0 < 0) y0 = 0;
        if (cells.height < y1) y1 = cells.height;
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) marks.rows[y][x] = 0;
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
    // No audit: p and q are NaN together (sx or sy is 0 / 0 when the unit stands on the target cell, which the
    // suite does), and then both choices are NaN.
    float scale = 1.0f / (AbsBits(q) < AbsBits(p) ? AbsBits(p) : AbsBits(q));
    // Kept as the original's test: a NaN scale (the case above) scales the step too, which makes the finite one
    // of sx / sy NaN as well; the plain scale < 1 would leave it.
    if (!(scale >= 1.0f)) {
        p = scale * p;
        q = scale * q;
        sy = scale * sy;
        sx = scale * sx;
    }
    // Directions tried at each distance: the step, then the others round the circle.
    const float directions[8][2] = {{sx, sy}, {p, q}, {q, -p}, {sy, -sx}, {-sy, sx}, {-q, p}, {-p, -q}, {-sx, -sy}};

    int first = x87::fistp(2.0f * minDistance) - 1;
    if (first < 0) first = 0;
    for (int r = first; r <= steps; ++r) {
        int count = r != 0 ? 8 : 1;
        float distance = static_cast<float>(r);
        for (int d = 0; d < count; ++d) {
            int cx = tx - x87::fistp(distance * directions[d][0]);
            int cy = ty - x87::fistp(distance * directions[d][1]);
            if (unit->GetClassId() == ClassId::Unit) {
                if (!CanStandAt(unit, cx, cy, &reason) || !IsCellPassable(this, unit, cx, cy)) continue;
            }
            if (sameRegion) {
                std::uint8_t mark = static_cast<std::uint8_t>(marks.rows[cy][cx]);
                if (mark != 1) {
                    if (mark == 2) continue;
                    if (!regionMap.ReachesRegion(sizeClass, cx, cy, region, &marks)) continue;
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

void CAIMap::AdvancePath(CUnit* unit, AIPath* path) {
    path->Reset();
    int count = g_serverWorld.tick_;
    Vector3 facing;
    unit->localRotation_.AxisY(&facing);
    Vector3 backwards = {-facing.x, -facing.y, -facing.z};
    path->Start(&unit->localPosition_, &backwards, count - 1);

    PathCells& pathCells = path->cells;
    PathCells& steps = path->steps;
    while (std::ssize(steps) < 32 && std::ssize(pathCells) > 0) {
        steps.push_back(pathCells.front());
        pathCells.pop_front();
    }

    float x, y;
    if (std::ssize(pathCells) > 0) {
        path->reached = false;
        // Put the last step back at the head of the path: it is where the steering heads.
        const PathCell* last = &steps.back();
        pathCells.push_front(*last);   // the element zeroed first
        y = static_cast<float>(last->y) + 0.5f;
        x = static_cast<float>(last->x) + 0.5f;
    } else {
        y = path->targetY;
        x = path->targetX;
        path->reached = true;
    }
    path->SetTarget(x, y);
    SetStepSpeeds(unit, &path->steps);
}

bool CAIMap::FindPath(CUnit* unit, const Vector3* target, AIPath* path, float range, float minDistance) {
    Vector3 from, goal;
    from = unit->localPosition_;
    goal = *target;
    path->goal = *target;
    if (LengthSquared2DUnrounded(target->x, target->y) > 100000000.0) return false;   // NaN goes on

    sectorMap.Remove(unit);
    if (!SnapTarget(unit, &goal, false, minDistance)) {
        sectorMap.Add(unit);
        return false;
    }
    path->targetY = goal.y;
    path->targetX = goal.x;

    // In half-cell units, like the snapped target.
    float fx = 2.0f * from.x;
    float fy = 2.0f * from.y;
    float ddx = fx - goal.x, ddy = fy - goal.y;
    EI_NAN_AUDIT(ddy, ddx);
    if (LengthSquared2DUnrounded(ddy, ddx) < 0.001f) {
        sectorMap.Add(unit);
        return false;
    }
    int tx = x87::fistp(goal.x - 0.5f);
    int ty = x87::fistp(goal.y - 0.5f);
    int ux = x87::fistp(fx - 0.5f);
    float uyHalf = fy - 0.5f;   // also ends up in the path cell's second half below
    int uy = x87::fistp(uyHalf);

    bool found = false;
    PathCell cell;
    PathCells& pathCells = path->cells;
    if (ux == tx && uy == ty) {
        int reason;
        if (unit->GetClassId() == ClassId::Unit && !(CanStandAt(unit, tx, ty, &reason) && IsCellPassable(this, unit, tx, ty))) {
            goto done;
        }
        cell.x = static_cast<std::int16_t>(ux);
        cell.y = static_cast<std::int16_t>(uy);
        std::memcpy(&cell.flags, &uyHalf, 4);
        pathCells.push_front(cell);
        found = true;
        pathCells.front().flags = 0x200;
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
            pathCells.clear();
            if (ux != tx || uy != ty) {
                if (forwardSearch.Begin(unit, ux, uy, tx, ty)) {
                    if (!backwardSearch.Begin(unit, tx, ty, ux, uy)) {
                        forwardSearch.Reset();
                    } else {
                        found = JoinLocalSearches(&pathCells, nullptr);
                        tried = found;
                    }
                }
            }
        }
        if (!tried) {
            // Far, or the local search failed: snap to a cell in the unit's region and plan.
            goal = *target;
            if (!SnapTarget(unit, &goal, true, minDistance)) {
                sectorMap.Add(unit);
                return false;
            }
            int gx = x87::fistp(goal.x - 0.5f);
            int gy = x87::fistp(goal.y - 0.5f);
            path->targetX = goal.x;
            path->targetY = goal.y;
            if (ux == gx && uy == gy) {
                PathCell& first = pathCells.emplace_front();
                first.x = static_cast<std::int16_t>(ux);
                first.y = static_cast<std::int16_t>(uy);
                std::memcpy(&first.flags, &shorter, 4);
                found = true;
                first.flags = 0x200;
            } else {
                int flag = path->mayExcludeRegions || path->firstSearch ? 1 : 0;
                int rangeCells = WorldToCell(range);
                found = PlanLongPath(unit, ux, uy, gx, gy, path, flag, rangeCells);
                if (!found) goto done;
            }
        }

        // Smooth the start of the path, entering from the side the unit faces.
        Vector3 facing;
        unit->localRotation_.AxisY(&facing);
        float bx = -facing.x, by = -facing.y;
        float px;
        px = path->targetX;
        float a = AbsBits(px - static_cast<float>(x87::fistp(px)));
        px = path->targetX;    // targetX twice (not targetY), like the original
        float b = AbsBits(px - static_cast<float>(x87::fistp(px)));
        float b2 = b + b, a2 = a + a;
        float fraction = b2 < a2 ? b2 : a2;   // a and b are the same value (see above)
        int octant = x87::fistp(static_cast<float>(x87::fpatan(by, bx) * 1.273239605931458));
        int direction = 6 - octant;
        if (direction < 0) direction += static_cast<int>((static_cast<unsigned>(7 - direction) >> 3) * 8);
        if (direction > 7) direction -= static_cast<int>((static_cast<unsigned>(direction) >> 3) * 8);
        path->smoothing = smoother.Smooth(unit, this, &pathCells, direction + 1, fraction);
        if (!found) goto done;
        AdvancePath(unit, path);
    }

done:
    sectorMap.Add(unit);
    if (!found) path->failed = true;
    path->firstSearch = false;
    return found;
}

bool CAIMap::ContinuePath(CUnit* unit, AIPath* path) {
    bool wasReached = path->reached;
    if (path->failed && std::ssize(path->cells) == 0) return FindPath(unit, &path->goal, path, 1000000.0f, 0.0f);
    AdvancePath(unit, path);
    if (path->failed) path->reached = false;
    return !wasReached;
}
