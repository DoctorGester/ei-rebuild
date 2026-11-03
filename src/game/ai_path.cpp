// Path search on the AI map: snapping the target to a reachable cell, planning, and feeding the
// planned cells to the unit's steering.
#include "ai_map.h"

#include "ai_motion.h"
#include "core_maths.h"
#include "game_singletons.h"
#include "map_object.h"
#include "sector_map.h"
#include "world.h"
#include "lib/float_select.h"
#include "lib/nan_audit.h"
#include "lib/rounding.h"
#include "lib/vecmath.h"

#include <bit>
#include <cstdint>
#include <cstring>

// --- Helpers -------------------------------------------------------------------------------------

namespace {

bool IsCellPassable(AIMap* map, Unit* unit, int x, int y) {
    return map->cells[y][x].Passability(unit->sizeClass * 4) != 0;
}

constexpr float kDiagonal = 0.707f;
constexpr float kInset = 0.49f;
} // namespace

bool AIMap::SnapTarget(Unit* unit, Vector3* target, bool sameRegion, float minDistance) {
    int tx = WorldToCell(target->x), ty = WorldToCell(target->y);
    target->x = 2.0f * target->x;
    target->y = 2.0f * target->y;
    int reason;
    if (!sameRegion) {
        if (unit->GetClassId() != ClassId::Unit) return true;
        if (CanStandAt(unit, tx, ty, &reason) && IsCellPassable(this, unit, tx, ty)) return true;
    }

    Vector3 from;
    from = unit->localPosition;
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
            for (int x = x0; x < x1; ++x) marks[y][x] = 0;
        }
    }

    // Unit steps along the line from the target back to the unit (the longer axis steps by 1).
    int dyCells = ty - uy, dxCells = tx - ux;
    int ady = dyCells < 0 ? -dyCells : dyCells;
    int adx = dxCells < 0 ? -dxCells : dxCells;
    // The unit on the target cell: no line to walk back, so only the target cell itself is a candidate.
    int steps;
    float sx, sy;
    if (adx == 0 && ady == 0) {
        steps = 0;
        sx = sy = 0.0f;
    } else if (adx > ady) {
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
    float scale = 1.0f / (AbsBits(q) < AbsBits(p) ? AbsBits(p) : AbsBits(q));
    if (scale < 1.0f) {
        p = scale * p;
        q = scale * q;
        sy = scale * sy;
        sx = scale * sx;
    }
    // Directions tried at each distance: the step, then the others round the circle.
    const float directions[8][2] = {{sx, sy}, {p, q}, {q, -p}, {sy, -sx}, {-sy, sx}, {-q, p}, {-p, -q}, {-sx, -sy}};

    int first = lib::RoundEven(2.0f * minDistance) - 1;
    if (first < 0) first = 0;
    for (int r = first; r <= steps; ++r) {
        int count = r != 0 ? 8 : 1;
        float distance = static_cast<float>(r);
        for (int d = 0; d < count; ++d) {
            int cx = tx - lib::RoundEven(distance * directions[d][0]);
            int cy = ty - lib::RoundEven(distance * directions[d][1]);
            if (unit->GetClassId() == ClassId::Unit) {
                if (!CanStandAt(unit, cx, cy, &reason) || !IsCellPassable(this, unit, cx, cy)) continue;
            }
            if (sameRegion) {
                std::uint8_t mark = static_cast<std::uint8_t>(marks[cy][cx]);
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

void AIMap::AdvancePath(Unit* unit, AIPath* path) {
    path->Reset();
    int count = g_serverWorld.tick;
    Vector3 facing = unit->localRotation.AxisY();
    Vector3 backwards = -facing;
    path->Start(unit->localPosition, backwards, count - 1);

    PathCells& pathCells = path->cells;
    PathCells& steps = path->steps;
    std::ptrdiff_t taken = 0;
    while (std::ssize(steps) < 32 && taken < std::ssize(pathCells)) steps.push_back(pathCells[taken++]);
    pathCells.erase(pathCells.begin(), pathCells.begin() + taken);

    float x, y;
    if (std::ssize(pathCells) > 0) {
        path->reached = false;
        // Put the last step back at the head of the path: it is where the steering heads.
        const PathCell* last = &steps.back();
        pathCells.insert(pathCells.begin(), *last);
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

bool AIMap::FindPath(Unit* unit, const Vector3& target, AIPath* path, float range, float minDistance) {
    Vector3 from, goal;
    from = unit->localPosition;
    goal = target;
    path->goal = target;
    if (LengthSquared2D(target.x, target.y) > 100000000.0f) return false;   // NaN goes on

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
    if (LengthSquared2D(ddy, ddx) < 0.001f) {
        sectorMap.Add(unit);
        return false;
    }
    int tx = lib::RoundEven(goal.x - 0.5f);
    int ty = lib::RoundEven(goal.y - 0.5f);
    int ux = lib::RoundEven(fx - 0.5f);
    int uy = lib::RoundEven(fy - 0.5f);

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
        SetPlainCellWords(&cell);
        pathCells.insert(pathCells.begin(), cell);
        found = true;
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
            if (forwardSearch.Begin(unit, ux, uy, tx, ty)) {
                if (!backwardSearch.Begin(unit, tx, ty, ux, uy)) {
                    forwardSearch.Reset();
                } else {
                    found = JoinLocalSearches(&pathCells, nullptr);
                    tried = found;
                }
            }
        }
        if (!tried) {
            // Far, or the local search failed: snap to a cell in the unit's region and plan.
            goal = target;
            if (!SnapTarget(unit, &goal, true, minDistance)) {
                sectorMap.Add(unit);
                return false;
            }
            int gx = lib::RoundEven(goal.x - 0.5f);
            int gy = lib::RoundEven(goal.y - 0.5f);
            path->targetX = goal.x;
            path->targetY = goal.y;
            if (ux == gx && uy == gy) {
                PathCell& first = *pathCells.emplace(pathCells.begin());
                first.x = static_cast<std::int16_t>(ux);
                first.y = static_cast<std::int16_t>(uy);
                SetPlainCellWords(&first);
                found = true;
            } else {
                int flag = path->mayExcludeRegions || path->firstSearch ? 1 : 0;
                int rangeCells = WorldToCell(range);
                found = PlanLongPath(unit, ux, uy, gx, gy, path, flag, rangeCells);
                if (!found) goto done;
            }
        }

        // Smooth the start of the path, entering from the side the unit faces.
        Vector3 facing = unit->localRotation.AxisY();
        float bx = -facing.x, by = -facing.y;
        float a = AbsBits(path->targetX - static_cast<float>(lib::RoundEven(path->targetX)));
        float b = AbsBits(path->targetY - static_cast<float>(lib::RoundEven(path->targetY)));
        float b2 = b + b, a2 = a + a;
        float fraction = b2 < a2 ? b2 : a2;
        int octant = lib::RoundEven(std::atan2(by, bx) * 1.273239605931458f);
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

bool AIMap::ContinuePath(Unit* unit, AIPath* path) {
    bool wasReached = path->reached;
    if (path->failed && std::ssize(path->cells) == 0) return FindPath(unit, path->goal, path, 1000000.0f, 0.0f);
    AdvancePath(unit, path);
    if (path->failed) path->reached = false;
    return !wasReached;
}

// --- Cells -------------------------------------------------------------------------------------------

// Path cell operations used by the path search: dropping repeated cells, stepping a cell in one of the eight
// directions, a path as a motion order. See ai_map.h and docs/notes/ai.md.

PathCell* PathCell::Step(PathCell* out, int direction) const {
    switch (direction) {
    case 1: out->x = x; out->y = static_cast<std::int16_t>(y - 1); break;
    case 2: out->x = static_cast<std::int16_t>(x - 1); out->y = static_cast<std::int16_t>(y - 1); break;
    case 3: out->x = static_cast<std::int16_t>(x - 1); out->y = y; break;
    case 4: out->x = static_cast<std::int16_t>(x - 1); out->y = static_cast<std::int16_t>(y + 1); break;
    case 5: out->x = x; out->y = static_cast<std::int16_t>(y + 1); break;
    case 6: out->x = static_cast<std::int16_t>(x + 1); out->y = static_cast<std::int16_t>(y + 1); break;
    case 7: out->x = static_cast<std::int16_t>(x + 1); out->y = y; break;
    case 8: out->x = static_cast<std::int16_t>(x + 1); out->y = static_cast<std::int16_t>(y - 1); break;
    default: *out = *this; break;
    }
    return out;
}

void PathCellsRemoveRepeats(PathCells* list) {
    std::int16_t lastX = -1, lastY = -1;
    auto kept = list->begin();
    for (const PathCell& cell : *list) {
        bool repeat = cell.x == lastX && cell.y == lastY;
        lastX = cell.x;
        lastY = cell.y;
        if (!repeat) *kept++ = cell;
    }
    list->erase(kept, list->end());
}

void AIPath::ToMotionOrder(PathOrder* out) const {
    constexpr int kFollowSteps = 2;
    std::memcpy(&out->motionOrder, &motionOrder, sizeof(motionOrder));
    out->steps = steps;
    if (out->motionOrder.kind != kFollowSteps) return;
    out->steps.insert(out->steps.end(), cells.begin(), cells.end());
    PathCellsRemoveRepeats(&out->steps);
    out->SetStepSpeeds(0x200);
    out->SetTarget(targetX, targetY);
}

void AIMap::ClearMarksAround(const PathCells* chain) {
    constexpr int kBlock = 8;
    constexpr int kMargin = 4 * kBlock;
    if (chain->empty()) return;
    auto node = chain->begin();
    int minX = node->x, maxX = minX;
    int minY = node->y, maxY = minY;
    for (; node != chain->end(); ++node) {
        int x = node->x, y = node->y;
        if (x < minX) minX = x;
        if (y < minY) minY = y;
        if (x > maxX) maxX = x;
        if (y > maxY) maxY = y;
    }
    int x0 = minX * kBlock - kMargin, x1 = maxX * kBlock + kBlock + kMargin;
    int y0 = minY * kBlock - kMargin, y1 = maxY * kBlock + kBlock + kMargin;
    if (x0 < 0) x0 = 0;
    if (cells.width < x1) x1 = cells.width;
    if (y0 < 0) y0 = 0;
    if (cells.height < y1) y1 = cells.height;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) marks[y][x] = 0;
}

// --- Stand -------------------------------------------------------------------------------------------

// A unit's path told to stand where the unit is (four variants): the remaining cells are dropped, the order
// reset, the target becomes the unit's cell (position * cell scale) with the flags {reached, -, -, 1}, the
// excluded regions emptied, and the order starts at the unit's position facing the direction its orientation
// quaternion gives, negated. Then the variants add a turn, a
// tracked object or a turn with an end; one starts at the order's own start time. See ai_map.h (AIPath) and
// docs/notes/ai.md.

namespace {

// The common part; `time` is read after the reset (one variant passes the path's own start time).
void StandAt(AIPath* path, const int* time, const MapObject* object) {
    path->cells.clear();
    path->Reset();
    path->targetX = 0.0f;
    path->targetY = 0.0f;
    path->reached = true;
    path->failed = false;
    path->mayExcludeRegions = false;
    path->firstSearch = true;
    path->excludedBlocks.clear();
    path->smoothing = 0;
    path->reached = true;
    float scale = 1.0f / 0.5f;
    path->targetX = scale * object->localPosition.x;
    path->targetY = scale * object->localPosition.y;
    path->Start(object->localPosition, -object->localRotation.AxisY(), *time);
}

} // namespace

void AIPath::StandAndTurn(int time, const MapObject* unit, const Vector3& direction, std::uint32_t rateBits) {
    StandAt(this, &time, unit);
    SetTurn(direction, rateBits);
}

void AIPath::StandAndTrack(int time, const MapObject* unit, int id, int from, int to) {
    StandAt(this, &time, unit);
    SetTrack(id, from, to);
}

void AIPath::StandAndTurnUntil(int time, const MapObject* unit, const Vector3& direction, int fromTime, int toTime) {
    StandAt(this, &time, unit);
    SetTurnUntil(direction, fromTime, toTime);
}

void AIPath::StandStill(const MapObject* unit) {
    StandAt(this, &motionOrder.startTime, unit);
}
