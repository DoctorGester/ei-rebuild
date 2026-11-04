#pragma once

// The client's per-frame object lists: what the view collects (world_client_view.cpp) and the draw walks
// (world_client_draw.cpp).

#include "ref.h"

#include <vector>

class MapObject;

enum class FrameList {
    TouchedModels,   // models to keep loaded (by last use)
    ShadowTrees,
    ShadowUnits,
    ShadowLevers,
    ShadowOther,
    ShadowBuildings,
    VisibleTrees,
    VisibleUnits,
    VisibleLevers,
    VisibleOther,
    VisibleBuildings,
    Count,
};
extern std::vector<MapObject*> g_frameLists[static_cast<int>(FrameList::Count)];   // world_client_view.cpp
inline std::vector<MapObject*>& FrameObjects(FrameList list) { return g_frameLists[static_cast<int>(list)]; }
extern std::vector<HeldRef<MapObject>> g_deferredUnits;   // world_client_view.cpp

