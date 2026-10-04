#pragma once

// The new renderer: a sidecar module that draws the game's frame with sokol_gfx (Direct3D 11) from
// data pulled out of the game (docs/notes/renderer.md). The game code that used to talk to the old
// Direct3D 7 path hands its data here instead; while the port is under way the old path keeps
// drawing everything that is not ported yet.
//
// Switch: the environment variable EI_RENDER (or the harness step "render <spec>"), a comma-separated
// list:
//   new        the sidecar renders every frame into its own off-screen target (game resolution);
//              captures write it next to the old frame (frames\<name>.new.bmp)
//   show       ...and a child window over the game's client area shows it (input passes through)
//   split      ...the child window covers only the right half (old frame left, new frame right)
//   noterrain  the old path no longer draws the terrain (the new one does)
// Without "new" nothing here runs and the game draws exactly as before.

#include <cstdint>
#include <vector>

namespace terrain {
struct SectorData;
}

namespace render {

// Sets the switch from `spec` (the format of EI_RENDER). Without a call the environment is read at
// the first use.
void Configure(const char* spec);

// The sidecar renders frames.
bool Active();

// The old Direct3D path still draws the terrain ("noterrain" off). The harness can change it.
bool DrawOldTerrain();
void SetDrawOldTerrain(bool on);

// 006BFF40 (DrawSectorPage) hands every sector page the game draws: the sector, the renderer object
// (007AF060, whose transform pipeline holds the current clip matrix) and the page (ground pages
// 0..pages+1, water pages after them).
void TerrainPage(const terrain::SectorData* sector, void* renderer, int page);

// The end of the game's frame: draws what was handed over since the last present into the
// off-screen target (and the child window). Once per frame; captures call it before reading.
void EndFrame();

// The game presents (004EF610): EndFrame if not done yet, then a new frame begins.
void Present();

// The last frame EndFrame drew, as 24-bit BGR rows from the top. False when the sidecar is off or
// has no frame.
bool ReadFrame(int* width, int* height, std::vector<std::uint8_t>* bgr);

} // namespace render
