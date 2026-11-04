// A unit's path in the save stream: load and save through the stream's read and write virtuals (buffer,
// size). Layout: the cell count and each remaining cell (8 bytes), the order, the target (8 bytes), the goal
// (12), the four flag bytes one by one, the excluded regions' count and each {x, y} as two dwords, and the
// smoothing value. Loading appends the cells to the list (created as {0, 0}, then read over). See ai_map.h
// (AIPath).
#include "ai_map.h"
#include "ai_motion.h"
#include "core_streams.h"

#include <cstdint>

void AIPath::LoadPath(ByteStream* stream) {
    for (int count = stream->ReadCount(); count > 0; --count) {
        // The cell's second dword is uninitialised in the original; it is read over below.
        PathCell cell{0, 0, 0, 0};
        cells.push_back(cell);
    }
    for (PathCell& cell : cells) stream->ReadValue(cell);
    LoadOrder(stream);
    stream->ReadValue(target);
    stream->ReadValue(goal);
    stream->ReadValue(reached);
    stream->ReadValue(failed);
    stream->ReadValue(mayExcludeRegions);
    stream->ReadValue(firstSearch);
    std::vector<Point>* excluded = &excludedBlocks;
    excluded->resize(stream->ReadCount());
    for (int i = 0; i < std::ssize(*excluded); ++i) {
        stream->ReadValue((*excluded)[i].x);
        stream->ReadValue((*excluded)[i].y);
    }
    stream->ReadValue(smoothing);
}

void AIPath::SavePath(ByteStream* stream) {
    stream->WriteCount(cells.size());
    for (const PathCell& cell : cells) stream->WriteValue(cell);
    SaveOrder(stream);
    stream->WriteValue(target);
    stream->WriteValue(goal);
    stream->WriteValue(reached);
    stream->WriteValue(failed);
    stream->WriteValue(mayExcludeRegions);
    stream->WriteValue(firstSearch);
    const std::vector<Point>* excluded = &excludedBlocks;
    stream->WriteCount(excluded->size());
    for (int i = 0; i < std::ssize(*excluded); ++i) {
        int x = (*excluded)[i].x;
        stream->WriteValue(x);
        int y = (*excluded)[i].y;
        stream->WriteValue(y);
    }
    int smoothingCopy = smoothing;
    stream->WriteValue(smoothingCopy);
}

// ---- The order ----------------------------------------------------------

void PathOrder::LoadOrder(ByteStream* stream) {
    MotionOrder* order = OrderOf(this);
    stream->ReadValue(order->kind);
    stream->ReadValue(order->position);
    stream->ReadValue(order->angle);
    stream->ReadValue(order->startTime);
    stream->Read(&order->arg5, 0xC);
    // The new step is {0, 0} and an uninitialised dword; every step is read over below.
    for (int count = stream->ReadCount(); count > 0; --count) steps.push_back(PathCell{0, 0, 0, 0});
    for (PathCell& step : steps) stream->ReadValue(step);
}

void PathOrder::SaveOrder(ByteStream* stream) {
    MotionOrder* order = OrderOf(this);
    stream->WriteValue(order->kind);
    stream->WriteValue(order->position);
    stream->WriteValue(order->angle);
    stream->WriteValue(order->startTime);
    stream->Write(&order->arg5, 0xC);
    stream->WriteCount(steps.size());
    for (const PathCell& step : steps) stream->WriteValue(step);
}
