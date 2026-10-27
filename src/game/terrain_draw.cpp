// The terrain draw of one sector's texture page (called by the terrain pass per sector and page). The new
// renderer (src/render, render::TerrainPage) builds the page's tiles itself from the sector's data; the
// game side only applies the pending water level changes first (they move the water surface that height
// queries read). The game's draw (the quarters' tiles as 32-byte vertices and triangle lists in
// function-local statics, clipped and transformed through the draw context's clipper and drawn by the
// context) is gone with the DirectDraw fallback; render_terrain.cpp keeps its tile entries and index
// patterns. See docs/notes/models.md and docs/notes/renderer.md.
#include "terrain_sector.h"

#include "ei/hook.h"
#include "render/render.h"

// Returns 1 (the game's draw returned 0 when the context failed to draw).
int terrain::SectorData::DrawPage(void* context, void* /*unused*/, int page, int /*unused*/) {
    if (clipMode == 2) return 1;
    if (hasWater) ApplyWaterLevels();
    render::TerrainPage(this, context, page);
    return 1;
}
EI_REWRITTEN(0x006BFF40, terrain::SectorData::DrawPage);
