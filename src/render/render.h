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
//   noobjects  the old path no longer draws the world's meshes (figures, objects, the sky) and
//              its flat geometry (shadows)
//   nointerface  ...nor the interface (window surfaces, sprites, 3D previews)
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

// The old path still draws the world's meshes and shadows ("noobjects" off) and the interface
// (meshes outside the world view, window surfaces; "nointerface" off). The harness can change them.
bool DrawOldMeshes();         // the mesh draw being handed over now (in or outside the world view)
bool DrawOldFlat();           // the flat geometry draw (004EA4A0: shadows, ground quads)
bool DrawOldInterface();      // the window surfaces' blits
void SetDrawOldObjects(bool on);
void SetDrawOldInterface(bool on);

// The world view's draw (CWorldClient::Render, 00569E40) begins and ends.
void BeginWorld();
void EndWorld();

// A mesh draw of the old path (004E9010 / 004E9A50 on the device object), after the game has
// decided it is visible and set its colours: the mesh in model space and the matrix the old path
// projects it with. Handed over between BeginMesh and EndMesh; every pass the old path draws in
// between (004EB550 / 004EB370) calls MeshPass, which takes the old device's state for that pass
// (blend, alpha test, depth, culling, fog, texture stages, the bound textures) and returns whether
// the old path still draws it.
struct MeshDraw {
    const float* positions;            // blocks of four vertices: x[4], y[4], z[4], 4 unused
    int positionCount;
    const std::uint32_t* colours;      // D3DCOLOR, by the corners' colour index
    const std::uint32_t* speculars;
    const float* uvs;                  // (u, v) pairs, by the corners' texture index
    const std::uint16_t* corners;      // (position, colour, texture) index triples
    int cornerCount;
    const std::uint16_t* indices;      // a triangle list over the corners
    int indexCount;
    float uOffset, vOffset;            // added to the texture coordinates when `moveUv`
    bool moveUv;
    const float* matrix;               // row-major, out = M * (x, y, z, 1)
    bool screenMatrix;                 // `matrix` gives screen pixels after the divide (the pipeline's
                                       // +0x1C stack), else Direct3D clip space (+0x24)
    bool clipped;                      // the old path clips it (else only projects it: the depth is
                                       // not cut at the near and far planes)
};
void BeginMesh(void* device, const MeshDraw& mesh);
bool MeshPass(void* device);
void EndMesh();

// A window surface's blit onto the frame (005FDA80): the surface holder (CDXSurface), the screen
// rectangle, the source rectangle (null: the whole surface, stretched) and the colour key (in the
// surface's pixel format) when `keyed`.
struct Rect {
    int left, top, right, bottom;
};
void SurfaceBlit(const void* holder, const Rect& destination, const Rect* source, bool keyed, std::uint32_t key);

// The old path clears the depth buffer to `z` in the middle of the frame (before the interface's
// panels and 3D previews draw): `count` screen rectangles, or all of it when `rects` is null.
void ClearDepth(float z, const Rect* rects, int count);

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
