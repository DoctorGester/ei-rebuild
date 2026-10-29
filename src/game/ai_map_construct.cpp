// The CAIMap constructor and reset (destroyed, then constructed again in place). Its members construct and destroy
// themselves; the constructor sizes the map's own grids 1 x 1 and fills the area lists. See ai_map.h and
// docs/notes/ai.md.
#include "ai_map.h"

#include <new>

CAIMap::CAIMap() {
    waterDepth.Resize(1, 1);
    cells.Resize(1, 1);
    occluders.Resize(1, 1, occluder::kNone);
    marks.Resize(1, 1);
    FillAreaLists(this);
}

void CAIMap::Reset() {
    this->~CAIMap();
    new (this) CAIMap();
}
