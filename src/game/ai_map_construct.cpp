// The CAIMap constructor and reset (destroyed, then constructed again in place). Its members construct and destroy
// themselves; the constructor sizes the map's own grids 1 x 1 and fills the area lists. See ai_map.h and
// docs/notes/ai.md.
#include "ai_map.h"
#include "ai_grids.h"

#include <new>

CAIMap::CAIMap() {
    ResizeGrid(waterDepth, 1, 1);
    ResizeGrid(cells, 1, 1, &AllocateCellGrid);
    ResizeGrid(occluders, 1, 1, &AllocateOccluderGrid);
    ResizeGrid(marks, 1, 1);
    FillAreaLists(this);
}

void CAIMap::Reset() {
    this->~CAIMap();
    new (this) CAIMap();
}
