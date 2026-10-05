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
//   cpuwindows the off-screen surfaces of 004EF7B0 (interface windows, cursor, movies) and the
//              system-memory picture surfaces of 004F0060 (cursor images, picture windows) keep
//              their pixels in game-owned memory the new renderer reads directly (off by default,
//              on with "present"); "nocpuwindows" turns it off
//   present    the switch-over: the old path draws nothing that is handed over (implies noterrain,
//              noobjects, nointerface, cpuwindows; movies too) and the new frame is copied into the
//              game's render target before the old present shows it; captures write the new frame
//              as frames\<name>.bmp
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

// "cpuwindows": the off-screen surfaces the game makes through 004EF7B0 (interface windows, the
// software cursor, movies) and 004F0060 (system-memory pictures) keep their pixels in memory the
// game code owns (a DIB section given to DirectDraw as the surface's memory). ClientSurface tells
// the new renderer where a holder's (CDXSurface) pixels are, so it reads them there instead of
// locking the surface; ClientSurfaceGone when the holder is destroyed.
bool CpuWindows();
void ClientSurface(const void* holder, void* bits, int pitch, int width, int height, int bitsPerPixel,
                   std::uint32_t redMask, std::uint32_t greenMask, std::uint32_t blueMask, std::uint32_t alphaMask);
void ClientSurfaceGone(const void* holder);
// A holder whose surface was loaded from an 8888 .mmp picture (004EFC60: the software cursor's
// images): the new renderer keeps the picture's pixels from the file data.
void PictureFromMmp(const void* holder, const void* data);

// The switch-over mode ("present"): the new frame is what the old present (004EF610) shows; the
// old path draws nothing that is handed over.
bool Presenting();

// The old Direct3D path still draws the terrain ("noterrain" off). The harness can change it.
bool DrawOldTerrain();
void SetDrawOldTerrain(bool on);

// The old path still draws the world's meshes and flat geometry ("noobjects" off) and the interface
// (meshes outside the world view, window surfaces, rectangles; "nointerface" off). The harness can
// change them.
bool DrawOldMeshes();         // the mesh draw being handed over now (in or outside the world view)
bool DrawOldInterface();      // the window surfaces' blits, the rectangle fills
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
    const std::uint16_t* corners;      // (position, colour, texture) index triples; null: vertex i
                                       // is position i with uv i and colours[0] / speculars[0]
                                       // (the flat geometry draw 004EA4A0)
    int cornerCount;                   // without corners: the vertex count
    const std::uint16_t* indices;      // a triangle list over the corners
    int indexCount;
    float uOffset, vOffset;            // added to the texture coordinates when `moveUv`
    bool moveUv;
    const float* matrix;               // row-major, out = M * (x, y, z, 1)
    bool screenMatrix;                 // `matrix` gives screen pixels after the divide (the pipeline's
                                       // +0x1C stack), else Direct3D clip space (+0x24)
    bool clipped;                      // the old path clips it (else only projects it: the depth is
                                       // not cut at the near and far planes)
    bool distanceFog;                  // the old path fogs the vertices by distance (device +0x158)
    float zScale;                      // the depth is multiplied by it (0 taken as 1)
};

// A texture loaded from its .mmp file (00690430): `data` the file in memory, the levels from
// `mip` on (`levels` of them) that the game loads into the surface of `holder` (a CDXSurface). The
// new renderer decodes them from the file instead of reading the surface back.
void TextureFromMmp(const void* holder, const void* data, unsigned mip, unsigned levels);

// The game made new pixels for a texture on the CPU (the shadow textures, 0051CE60) and copies
// them into its surface (CDXSurface): the new renderer takes them from the buffer (`pitch` bytes
// per row, `bitsPerPixel` 16 or 32, the channel masks of the surface's format).
void TexturePixels(const void* holder, const void* pixels, int width, int height, int pitch, int bitsPerPixel,
                   std::uint32_t redMask, std::uint32_t greenMask, std::uint32_t blueMask, std::uint32_t alphaMask);
void BeginMesh(void* device, const MeshDraw& mesh);
bool MeshPass(void* device);
void EndMesh();

// Pre-transformed vertices the old path draws as they are (FVF 0xC4: screen x, y, depth z, rhw,
// colour, specular; rhw taken as 1): the renderer's rectangle fill and outline (005041F0). A
// triangle list over `indices`, or a line strip over the vertices (`indices` null). Takes the old
// device's state for the draw (`device`: the device object); returns whether the old path still
// draws it.
struct ScreenVertex {
    float x, y, z, rhw;
    std::uint32_t colour, specular;
};
bool ScreenDraw(void* device, const ScreenVertex* vertices, int count, const std::uint16_t* indices, int indexCount);

// A Direct3D 7 draw of pre-transformed vertices the old path is about to make (particles and
// sprites 00503680, ribbons and lightning 00502A20, the renderer's triangle draw 00501BB0: flying hit
// points): `fvf` 0x1C4 (x, y, z, rhw, colour, specular, u, v) or 0xC4 (no texture coordinates),
// `primitive` a D3DPRIMITIVETYPE (2 line list, 3 line strip, 4 triangle list, 5 strip, 6 fan),
// indexed when `indices`. The vertices' fog (specular alpha) is already applied. Takes the old
// device's state for the draw; returns whether the old path still draws it.
bool TlDraw(void* device, const void* vertices, int fvf, int vertexCount, const std::uint16_t* indices, int indexCount,
            int primitive);

// A window surface's blit onto the frame (005FDA80): the surface holder (CDXSurface), the screen
// rectangle, the source rectangle (null: the whole surface, stretched) and the colour key (in the
// surface's pixel format) when `keyed`.
struct Rect {
    int left, top, right, bottom;
};
void SurfaceBlit(const void* holder, const Rect& destination, const Rect* source, bool keyed, std::uint32_t key);

// A movie's decoded frame (CBinkVideo 005DBD80): R, G, B, X bytes per pixel (BinkCopyToBuffer's
// 32-bit "R" type), kept for `key` (the video object) until the next one; MovieBlit draws the last
// frame into the screen rectangle `to`.
void MovieFrame(const void* key, const std::uint8_t* rgbx, int width, int height);
void MovieBlit(const void* key, const Rect& to);

// The software cursor (005E54A0): the part `from` of the cursor image's surface (holder) with its
// mask (`mask`: the image's full width, the surface's pixel format; the old path draws
// (background & mask) | image) at the screen rectangle `to`. Texels whose mask is not zero are left
// out.
void Cursor(const void* imageHolder, const void* mask, const Rect& from, const Rect& to);

// The old path clears the depth buffer to `z` in the middle of the frame (before the interface's
// panels and 3D previews draw): `count` screen rectangles, or all of it when `rects` is null.
void ClearDepth(float z, const Rect* rects, int count);

// 006BFF40 (DrawSectorPage) hands every sector page the game draws: the sector, the renderer object
// (007AF060, whose transform pipeline holds the current clip matrix) and the page (ground pages
// 0..pages+1, water pages after them).
void TerrainPage(const terrain::SectorData* sector, void* renderer, int page);

// Copies the new frame's `source` rectangle (screen pixels, as drawn so far this frame) into the
// DirectDraw surface of `holder` (a CDXSurface), stretched to its size: the screen copies of
// 005FD3D0 (the escape menu's background, the save game's picture). False when the sidecar is off
// or the copy failed.
bool CopyFrameTo(const void* holder, const Rect& source);

// The end of the game's frame: draws what was handed over since the last present into the
// off-screen target (and the child window). Once per frame; captures call it before reading.
void EndFrame();

// The game presents (004EF610): EndFrame if not done yet, then a new frame begins.
void Present();

// The last frame EndFrame drew, as 24-bit BGR rows from the top. False when the sidecar is off or
// has no frame.
bool ReadFrame(int* width, int* height, std::vector<std::uint8_t>* bgr);

} // namespace render
