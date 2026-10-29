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
    int count;
    stream->ReadValue(count);
    for (int i = 0; i < count; ++i) {
        // The cell's second dword is uninitialised in the original; it is read over below.
        PathCell cell{0, 0, 0, 0};
        cells.push_back(cell);
    }
    for (PathCell& cell : cells) stream->ReadValue(cell);
    LoadOrder(stream);
    stream->Read(&targetX, 8);
    stream->ReadValue(goal);
    stream->ReadValue(reached);
    stream->ReadValue(failed);
    stream->ReadValue(mayExcludeRegions);
    stream->ReadValue(firstSearch);
    std::vector<BlockPoint>* excluded = &excludedBlocks;
    stream->ReadValue(count);
    excluded->resize(count);
    for (int i = 0; i < std::ssize(*excluded); ++i) {
        stream->ReadValue((*excluded)[i].x);
        stream->ReadValue((*excluded)[i].y);
    }
    stream->ReadValue(smoothing);
}

void AIPath::SavePath(ByteStream* stream) {
    int count = std::ssize(cells);
    stream->WriteValue(count);
    for (const PathCell& cell : cells) stream->WriteValue(cell);
    SaveOrder(stream);
    stream->Write(&targetX, 8);
    stream->WriteValue(goal);
    stream->WriteValue(reached);
    stream->WriteValue(failed);
    stream->WriteValue(mayExcludeRegions);
    stream->WriteValue(firstSearch);
    const std::vector<BlockPoint>* excluded = &excludedBlocks;
    count = std::ssize(*excluded);
    stream->WriteValue(count);
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
    stream->Read(&order->x, 8);
    stream->ReadValue(order->angle);
    stream->ReadValue(order->startTime);
    stream->Read(&order->arg5, 0xC);
    int count = 0;   // left as it was when the read fails
    stream->ReadValue(count);
    // The new step is {0, 0} and an uninitialised dword; every step is read over below.
    for (int i = 0; i < count; ++i) steps.push_back(PathCell{0, 0, 0, 0});
    for (auto node = steps.begin(); node != steps.end();) {
        auto next = std::next(node);
        stream->Read(&*node, 8);
        node = next;
    }
}

void PathOrder::SaveOrder(ByteStream* stream) {
    MotionOrder* order = OrderOf(this);
    stream->WriteValue(order->kind);
    stream->Write(&order->x, 8);
    stream->WriteValue(order->angle);
    stream->WriteValue(order->startTime);
    stream->Write(&order->arg5, 0xC);
    int count = std::ssize(steps);
    stream->WriteValue(count);
    for (auto node = steps.begin(); node != steps.end();) {
        auto next = std::next(node);
        stream->Write(&*node, 8);
        node = next;
    }
}
