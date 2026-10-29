// Path cell operations used by the path search: dropping repeated cells, stepping a cell in one of the eight
// directions, a path as a motion order. See ai_map.h and docs/notes/ai.md.
#include "ai_map.h"
#include "ai_motion.h"

#include <cstdint>
#include <cstring>



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
