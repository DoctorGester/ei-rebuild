#pragma once

// The client's per-frame object lists: what the view collects (world_client_view.cpp) and the draw walks
// (world_client_draw.cpp).

#include "engine/core/ref.h"
#include "lib/vecmath.h"

#include <cstdint>
#include <vector>

class MapObject;

enum class FrameList {
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

// An object grid cell's casters as the view placed them this frame (WorldClient::CollectCells), for the casters'
// walk outside the view: the box of the centres of the world spheres it would test (but units'), their largest
// radius and a bound on the coordinates' magnitudes; `complete` when the cell holds no unit and no sphere that is not
// finite.
struct CasterCell {
    std::uint32_t pass = 0;   // the view pass that filed it
    bool complete = false;
    bool any = false;         // any sphere in the box
    Vector3 low, high, size;
    float radius = 0.0f;
};
extern std::vector<CasterCell> g_casterCells;   // world_client_view.cpp; row by row
// The cell (x, y) as this frame's view filed it, or null (no view this frame, out of the grid).
const CasterCell* ViewCasterCell(std::uint32_t x, std::uint32_t y);

