// The AIMap constructor and Reset: every member as a new map has it, the map's own grids 1 x 1 and the area lists
// filled. Of the members only the event table's reset reaches outside the map (its events go back to the free
// list). See ai_map.h and docs/notes/ai.md.
#include "game/ai_map.h"

AIMap::AIMap() { Reset(); }

void AIMap::Reset() {
    loaded = false;
    batching = false;
    refreshUnits = 0;
    dirty.Reset();
    climbLarge = {};
    climbSmall = {};
    materials = {};
    bridgeHeights = {};
    heightScale = 0.0f;
    sectorMap = {};
    footprint = {};
    bridges = {};
    waterDepth = {};
    waterDepth.Resize(1, 1);
    FillAreaLists(this);
    unitLayerSmall.Clear();
    unitLayerLarge.Clear();
    smoother = {};
    regionMap.Reset();
    forwardSearch = {};
    backwardSearch = {};
    footprintTick = 0;
    cells = {};
    cells.Resize(1, 1);
    occluders = {};
    occluders.Resize(1, 1, occluder::kNone);
    marks = {};
    marks.Resize(1, 1);
    events.Reset();
}
