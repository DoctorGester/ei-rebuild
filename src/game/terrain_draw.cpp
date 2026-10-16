// The terrain draw of one sector's texture page (006BFF40; called by the terrain pass 006C6460 per
// sector and page). The new renderer (src/render, render::TerrainPage) builds the page's tiles itself
// from the sector's data; the game side only applies the pending water level changes first (they
// move the water surface that height queries read). The original's draw (the quarters' tiles as
// 32-byte vertices and triangle lists in function-local statics at 007C8848..007C88C8, clipped and
// transformed through the draw context's clipper and drawn by the context) is gone with the DirectDraw
// fallback (R11a); render_terrain.cpp keeps its tile entries and index patterns. See
// docs/notes/models.md and docs/notes/renderer.md.
#include "terrain_sector.h"

#include "ei/hook.h"
#include "render/render.h"

// 006BFF40. Returns 1 (the original: 0 when the context fails to draw).
int terrain::SectorData::DrawPage(void* context, void* /*unused*/, int page, int /*unused*/) {
    if (clipMode == 2) return 1;
    if (hasWater) ApplyWaterLevels();
    render::TerrainPage(this, context, page);
    return 1;
}
EI_REPLACE_METHOD(0x006BFF40, terrain::SectorData::DrawPage);
