// A unit's path in the save stream: load and save through the stream's read and write virtuals (buffer,
// size). Layout: the cell count and each remaining cell (8 bytes), the order, the target (8 bytes), the goal
// (12), the four flag bytes one by one, the excluded regions' count and each {x, y} as two dwords, and the
// smoothing value. Loading appends the cells to the list (created as {0, 0}, then read over). See ai_map.h
// (AIPath).
#include "ai_map.h"
#include "ai_motion.h"
#include "core_streams.h"

#include "lib/mfc.h"

#include <cstdint>

void AIPath::LoadPath(CStream* stream) {
    int count;
    stream->Read(&count, 4);
    for (int i = 0; i < count; ++i) {
        // The cell's second dword is uninitialised in the original; it is read over below.
        PathCell cell{0, 0, 0, 0};
        cells.AddTail(cell);
    }
    for (PathCellList::Node* position = cells.head; position;)
        stream->Read(&cells.GetNext(position), 8);
    LoadOrder(stream);
    stream->Read(&targetX, 8);
    stream->Read(&goal, 0xC);
    stream->Read(&reached, 1);
    stream->Read(&failed, 1);
    stream->Read(&flag6E, 1);
    stream->Read(&flag6F, 1);
    BlockPointArray* excluded = &excludedBlocks;
    stream->Read(&count, 4);
    excluded->resize(count);
    for (int i = 0; i < std::ssize(*excluded); ++i) {
        stream->Read(&(*excluded)[i].x, 4);
        stream->Read(&(*excluded)[i].y, 4);
    }
    stream->Read(&smoothing, 4);
}

void AIPath::SavePath(CStream* stream) {
    int count = cells.count;
    stream->Write(&count, 4);
    for (PathCellList::Node* position = cells.head; position;)
        stream->Write(&cells.GetNext(position), 8);
    SaveOrder(stream);
    stream->Write(&targetX, 8);
    stream->Write(&goal, 0xC);
    stream->Write(&reached, 1);
    stream->Write(&failed, 1);
    stream->Write(&flag6E, 1);
    stream->Write(&flag6F, 1);
    const BlockPointArray* excluded = &excludedBlocks;
    count = std::ssize(*excluded);
    stream->Write(&count, 4);
    for (int i = 0; i < std::ssize(*excluded); ++i) {
        int x = (*excluded)[i].x;
        stream->Write(&x, 4);
        int y = (*excluded)[i].y;
        stream->Write(&y, 4);
    }
    int smoothingCopy = smoothing;
    stream->Write(&smoothingCopy, 4);
}

// ---- The order ----------------------------------------------------------

void PathOrder::LoadOrder(CStream* stream) {
    MotionOrder* order = OrderOf(this);
    stream->Read(&order->kind, 4);
    stream->Read(&order->x, 8);
    stream->Read(&order->angle, 4);
    stream->Read(&order->startTime, 4);
    stream->Read(&order->arg5, 0xC);
    int count = 0;   // left as it was when the read fails
    stream->Read(&count, 4);
    // The new step is {0, 0} and an uninitialised dword; every step is read over below.
    for (int i = 0; i < count; ++i) steps.AddTail(PathCell{0, 0, 0, 0});
    for (PathCellList::Node* node = steps.head; node;) {
        PathCellList::Node* next = node->next;
        stream->Read(&node->data, 8);
        node = next;
    }
}

void PathOrder::SaveOrder(CStream* stream) {
    MotionOrder* order = OrderOf(this);
    stream->Write(&order->kind, 4);
    stream->Write(&order->x, 8);
    stream->Write(&order->angle, 4);
    stream->Write(&order->startTime, 4);
    stream->Write(&order->arg5, 0xC);
    int count = steps.count;
    stream->Write(&count, 4);
    for (PathCellList::Node* node = steps.head; node;) {
        PathCellList::Node* next = node->next;
        stream->Write(&node->data, 8);
        node = next;
    }
}
