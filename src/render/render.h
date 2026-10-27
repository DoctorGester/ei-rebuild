#pragma once

// The new renderer: a sidecar module that draws the game's frame with sokol_gfx (Direct3D 11) from the
// data the game hands over, and presents it through its own swap chain on the game window. There is no
// DirectDraw fallback: when it cannot start the game ends with an error. It is off in the original
// game's own process (the harness and the vector recorders). See docs/notes/renderer.md.
// EI_RENDER (or the harness step "render <spec>") is a comma-separated list: "smooth", "smoothcamera",
// "uiboth" / "uinative" (render_ui.h), the harness build's "removable"; "new", "present" and
// "cpuwindows" are accepted and change nothing.

#include "lib/vecmath.h"

#include <cstdint>
#include <vector>

struct Matrix4;

namespace terrain {
struct SectorData;
}

namespace render {

// Sets the options from `spec` (the format of EI_RENDER) and EI_RENDER's; without a call the
// environment is read at the first use.
void Configure(const char* spec);
// Starts making the Direct3D 11 device on a thread of its own (the image's start-up, before WinMain); the
// renderer's first use takes it (saves 0.15 s of each start).
void PrepareDevice();

// The smooth option (off by default): anisotropic 16x sampling of the world view's models and 8x
// multisampling of the world view, as the user's dgVoodoo setup draws it (renderer.md, "The smooth
// option"). Switch: EI_RENDER's word "smooth" (read at the start) or SetSmooth (from the next frame).
void SetSmooth(bool on);
bool Smooth();
// The world view's draws (CWorldClient::Render) come between BeginWorld and EndWorld.
void BeginWorld();
void EndWorld();
// The smooth option's state in the last presented frame.
struct SmoothInfo {
    bool on = false;
    int deviceSamples = 1, targetSamples = 1;
    int multisampledPasses = 0;
    int copiesInto = 0, depthCopiesBack = 0;
    int anisotropicModels = 0, plainModels = 0;
};
SmoothInfo SmoothState();
// Harness build only ("smoothorder"): the smooth option's passes in an order the game never draws, from
// the next frame. Before: an empty pass on the frame target first. Split: the world view's first run
// ends at its middle command and an empty pass on the frame target follows. Game: as the game draws.
enum class SmoothOrder : std::uint8_t { Game, Before, Split };
void SetSmoothOrder(SmoothOrder order);

// Object shadows (renderer.md section 13): figures and map objects cast shadows from the sun through a
// shadow map made every frame; the game's 64 x 64 shadow decals are neither made nor drawn.

// The smooth camera (renderer.md section 14; off by default): the battle camera moves once a frame by
// the frame's time, its drift and focus lag ease by time, and it follows its target's position blended
// between the target's last two ticks. Read by the main loop and CCamera::Update, not by the renderer.
// Switch: EI_RENDER's word "smoothcamera" or SetSmoothCamera (from the next pass).
void SetSmoothCamera(bool on);
bool SmoothCamera();
// The performance overlay (render_perf.h; renderer.md section 15): frames a second, frame and GPU times,
// draws, drawn over the presented frame (never into the frame target). Switched by the settings file;
// off by default. The harness build has none: PerfOverlaySupported() is false there.
bool PerfOverlaySupported();
void SetPerfOverlay(bool on);
// The sun for this frame's world view: the direction its light travels (CLight::direction_, z negative
// by day). Without it no shadow map is made.
void ShadowSun(const float direction[3]);
// The GPU lighting (renderer.md section 16). The world's draw hands over the frame's light: the sun, the
// ambient colour, the point lights the landscape lights its ground with (position in the landscape's
// space, 1 / range squared, colour, flag 0x40 specular) and the camera's position (the nearest
// kMostSceneLights are kept), until the next hand-over. GpuTerrainLighting (true when Active()): the
// terrain shader lights each pixel; the game leaves its per-vertex lighting out (SectorData::Light writes
// only the edge fade).
struct ScenePointLight {
    Vector3 position;
    float inverseRangeSquared;
    float colour[3];
    bool specular;
};
struct SceneLights {
    Vector3 sunDirection;
    float sunColour[3];
    float ambientColour[3];
    Vector3 camera;
    const ScenePointLight* points;
    int pointCount;
};
constexpr int kMostSceneLights = 16;
void SceneLighting(const SceneLights& lights);
bool GpuTerrainLighting();
// The ground casts into the shadow map (landscape flag 0x10). ShadowTerrainArea: as ShadowCasterArea for a
// caster no higher than `highestTop`; false: no map this frame. ShadowTerrainSector: a loaded sector in
// that area casts with its ground layer's 33 x 33 vertices when ShadowCasterReaches its box.
struct ShadowCasterRect;
bool ShadowTerrainArea(float highestTop, ShadowCasterRect* area);
void ShadowTerrainSector(const terrain::SectorData* sector);
// The meshes handed over between BeginCaster and EndCaster belong to `object`; CastsShadow (from the
// object's shadow draw) lets them cast into this frame's shadow map, not at all weaker than `strength`
// 0.5 (a unit fading in). `figure`: a unit's draw: its meshes take only the shadows other objects cast,
// on every face; any other object's meshes are shadowed on the faces turned to the sun.
void BeginCaster(const void* object, bool figure);
void EndCaster();
void CastsShadow(const void* object, float strength);
// Casters outside the view: an object the game does not draw this frame, or a part of a drawn one it left
// out, whose shadow can reach the visible ground casts through ShadowCasterMesh (the world's draw reads
// them from the objects' parts, changing nothing of the game's). ShadowCasterArea: whether this frame gets
// a shadow map and the rectangle of the ground (landscape's space) a caster's centre must stand in: the
// visible ground's box swept back towards the sun as far as a caster no higher than `highestTop` can
// stand, widened by `widest` (the largest radius). ShadowCasterReaches: whether a caster inside the sphere
// can shadow the visible ground (exact up to the bounds). CasterPartDrawn: whether the figure mesh `part`
// (MeshSource::owner) was drawn this frame as a caster's mesh. CasterStrength: the strength CastsShadow
// gave `object` this frame, -1 when it was not called. SetOffscreenCasters (the harness build's
// "shadowcasters view|all|walked"): all (default) as above; view: only what the game draws casts; walked:
// every caster comes from the walk.
struct ShadowCasterRect {
    float x0, y0, x1, y1;
};
bool ShadowCasterArea(float highestTop, float widest, ShadowCasterRect* area);
struct ShadowCasterSphere {
    double x, y, z, radius;
};
bool ShadowCasterReaches(const ShadowCasterSphere& sphere);
bool CasterPartDrawn(const void* part);
float CasterStrength(const void* object);
struct ShadowMesh {
    const float* positions;            // blocks of four vertices, as MeshDraw's
    int positionCount;
    const float* uvs;                  // (u, v) pairs
    int uvCount;
    const std::uint16_t* corners;      // (position, colour, texture) index triples
    int cornerCount;
    const std::uint16_t* indices;      // a triangle list over the corners
    int indexCount;
    float uOffset, vOffset;            // added to the texture coordinates
    double model[16];                  // model space to the landscape's, row-major: out = M * (x, y, z, 1)
    const void* texture;               // stage 0's surface holder, or null
};
void ShadowCasterMesh(const void* object, const ShadowMesh& mesh);
// The walk's time, from BeginShadowWalk to EndShadowWalk, counts as the shadow map's CPU time.
void BeginShadowWalk();
void EndShadowWalk();
enum class OffscreenCasters { View, All, Walked };
void SetOffscreenCasters(OffscreenCasters mode);
// Harness build shadow map checks. SetShadowView ("shadowview on|off"): the receivers show only how much
// sun the map hides. SetShadowShift ("shadowshift <texels>"): the map's origin moved by that many whole
// texels across and half as many along, to show a filter that depends on the range cells.
void SetShadowView(bool on);
void SetShadowShift(int texels);
// With the shadow map nothing reads an object shadow's classic texture or its decal's flat draw; the game
// reads only the parts' points, the bounds, the quad and the decal's projection. ClassicShadowSkipped:
// true when Active().
bool ClassicShadowSkipped();
// The last presented frame's shadow map: made or not, its size, the world extent it covers, the casters'
// objects and draws, the meshes and terrain batches that sampled it. Of the objects whose shadow the game
// draws: `offScreen` cast nothing and handed over no mesh, `lost` handed over meshes of which none went
// into the map (none should); `outsideCasters` cast through ShadowCasterMesh in `outsideDraws` draws of
// `outsideCorners` corners.
struct ShadowInfo {
    bool made = false;
    int size = 0;
    float extent = 0.0f;
    int casters = 0, casterDraws = 0, receivingMeshes = 0, receivingTerrain = 0;
    int offScreen = 0, lost = 0;
    int outsideCasters = 0, outsideDraws = 0, outsideCorners = 0;
    int terrainCasters = 0;   // the ground's sectors drawn into the map
};
ShadowInfo ShadowState();

// What the new renderer did for the presented frames (renderer.md section 13), summed over `frames`.
// TakeFrameStats returns the sums since its last call and starts new ones (the harness's "renderstats").
// The CPU times and the reuse counts are the harness build's only.
struct FrameStats {
    enum Upload { Mmp, CpuPixels, MemoryTexture, WindowSurface, Movie, Small, UploadKinds };
    std::uint32_t frames = 0;
    int width = 0, height = 0;   // the last frame's size
// Hand-over: terrain pages and batches, meshes (BeginMesh with corners built) and their passes,
// pre-transformed draws, window blits, depth clears, the old device's state reads.
    std::uint64_t terrainPages = 0, terrainBatches = 0, meshes = 0, meshPasses = 0, transformedDraws = 0;
    std::uint64_t blits = 0, depthClears = 0, stateReads = 0, matrices = 0;
// Of the meshes: the flat geometry (object shadows, ground prints, selection markers, light decals).
    std::uint64_t flatMeshes = 0, flatVertexBytes = 0;
// Vertices handed over against the last frame's of the same terrain page or mesh: all the same, only
// colours changed (relit), positions or texture coordinates changed (moved), none last frame (new);
// meshes equal to another's of the same frame (shared: instancing candidates).
    enum Reuse { Same, Relit, Moved, New, ReuseKinds };
    std::uint64_t terrainReuse[ReuseKinds] = {}, meshReuse[ReuseKinds] = {}, sharedMeshes = 0;
    std::uint64_t terrainVertexBytes = 0, terrainIndexBytes = 0, meshVertexBytes = 0, meshIndexBytes = 0;
    std::uint64_t uniformBytes = 0;
// Submission: passes, draws, pipeline and bindings applies and changes, first-texture changes.
    std::uint64_t passes = 0, draws = 0, pipelineApplies = 0, pipelineChanges = 0, bindings = 0, textureChanges = 0;
// Images made (or updated in place) per kind and their bytes; bytes compared with the snapshots of the
// renderer's memory.
    std::uint64_t uploads[UploadKinds] = {}, uploadBytes[UploadKinds] = {}, largestUpload[UploadKinds] = {};
    std::uint64_t comparedBytes = 0;
// CPU time (ms): hand-over, texture work (windowMs: the window surfaces'), EndFrame. statsMs: these
// counts' own hashing and checks; terrainMs: the terrain pages' hand-over and uploads; both left out of
// the others.
    double terrainMs = 0.0;
    double handOverMs = 0.0, textureMs = 0.0, windowMs = 0.0, endFrameMs = 0.0, statsMs = 0.0;
// The old device path's mesh draws (DeviceMeshTimer), the hand-over in them left out.
    double deviceMeshMs = 0.0;
// The ground decals' projection (ProjectDecal) for object shadows and ground prints, with calls and
// vertices; the device's flat draws (hand-over left out); the object shadow's build (RenderShadow).
    double decalShadowMs = 0.0, decalPrintMs = 0.0, flatMs = 0.0, shadowBuildMs = 0.0;
    std::uint64_t decalShadows = 0, decalPrints = 0, decalVertices = 0;
    std::uint64_t flatDraws = 0, flatDrawVertices = 0, shadowBuilds = 0;
// The shadow map's draws and CPU time (left out of the times around it) and, harness build, GPU time
// (timestamp queries read a few frames later, summed over gpuFrames) of the shadow pass and all passes.
    std::uint64_t shadowDraws = 0;
    double shadowMs = 0.0;
    std::uint32_t gpuFrames = 0;
    double gpuShadowMs = 0.0, gpuFrameMs = 0.0;
// Meshes handed over with per-vertex offsets (an animation's morph) and their corners.
    std::uint64_t morphMeshes = 0, morphCorners = 0;
};
FrameStats TakeFrameStats();

// The new renderer draws: in the standalone image, until it failed (no Direct3D 11 device, a sokol
// panic, a lost device that could not be made again). Every call below does nothing when it is off.
bool Active();
// Whether the old device path's mesh draws do only the per-corner work game code reads: the new renderer
// draws every mesh from the model-space hand-over, so the TL vertices, their fog and the clipped
// vertices' screen positions are read only by the screen rectangle of draw flag 1 (and the clipper, whose
// counts the device's counters take). True when Active(), unless the harness asked for the old work in
// full ("meshcorners full", harness build only).
bool MeshCornersSkipped();
void SetMeshCornersFull(bool full);
// Harness build: times one device mesh draw into FrameStats::deviceMeshMs (nothing elsewhere).
void BeginDeviceMeshTime();
void EndDeviceMeshTime();
// Harness build: times one decal projection (`shadow`: an object shadow's, else a ground print's), one
// flat draw and one object shadow build into FrameStats, counting the vertices.
void BeginDecalTime(bool shadow);
void EndDecalTime(int vertices);
void BeginFlatDrawTime(int vertices);
void EndFlatDrawTime();
void BeginShadowBuildTime();
void EndShadowBuildTime();

// Off-screen surfaces (interface windows, the software cursor, movies) and system-memory pictures keep
// their pixels in memory the new renderer owns and have no DirectDraw surface; the new renderer reads a
// holder's (CDXSurface) pixels there wherever it draws it. Texture holders have such memory too (the
// save / load screen's pictures, the shadow and .bmp textures): a mesh pass that binds one reads the
// memory again whenever it changed.
// A surface holder's pixels: a zeroed top-down DIB section (GDI can draw into it through a memory DC) of
// `width` x `height` pixels, 16 or 32 bits each, channels at the given masks (`alphaMask` 0 without
// alpha). The game code writes it and the new renderer reads it.
struct SurfaceMemory {
    void* bits = nullptr;
    void* bitmap = nullptr;   // the DIB section (HBITMAP)
    int pitch = 0, width = 0, height = 0, bitsPerPixel = 0;
    std::uint32_t redMask = 0, greenMask = 0, blueMask = 0, alphaMask = 0;
};
// Makes the memory of `holder` (replacing any it had); null when it cannot be made. The pointer stays
// valid until the memory is freed.
const SurfaceMemory* NewSurfaceMemory(const void* holder, int width, int height, int bitsPerPixel, std::uint32_t redMask,
                                      std::uint32_t greenMask, std::uint32_t blueMask, std::uint32_t alphaMask);
// The memory of `holder`, or null.
const SurfaceMemory* SurfaceMemoryOf(const void* holder);
void FreeSurfaceMemory(const void* holder);
// The holder is destroyed: its memory freed, what the renderer kept for it forgotten.
void HolderGone(const void* holder);
// A holder whose surface was loaded from an 8888 .mmp picture (the software cursor's images): the
// renderer keeps the picture's pixels from the file data.
void PictureFromMmp(const void* holder, const void* data);

// The device object is being built: the new renderer starts now and presents through its own swap chain
// on the game window; the game builds its device without DirectDraw (device.h). False when it cannot
// start (the game then ends with an error).
bool StartPresenting();
bool PresentsItself();
// Whether presenting waits for the vertical blank (default on); the harness turns it off under its
// virtual clock.
void SetWaitForVsync(bool on);
// The settings file's "VSync" (directdraw_holder.cpp, after StartPresenting, before the first present):
// off presents with sync interval 0 and, where supported, tearing (DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING).
// The harness build has no such switch: VsyncSwitchSupported() is false there.
bool VsyncSwitchSupported();
void SetVsync(bool on);

// A mesh draw of the old path, after the game decided it is visible and set its colours: the mesh in model
// space and the matrix the old path projects it with. Handed over between BeginMesh and EndMesh; every
// pass the old path draws in between calls MeshPass, which takes the old device's state for that pass
// and returns whether the old path still draws it (only when the new renderer did not take it).
struct MeshDraw {
    const float* positions;            // blocks of four vertices: x[4], y[4], z[4], 4 unused
    int positionCount;
    const std::uint32_t* colours;      // D3DCOLOR, by the corners' colour index
    const std::uint32_t* speculars;
    const float* uvs;                  // (u, v) pairs, by the corners' texture index
    const std::uint16_t* corners;      // (position, colour, texture) index triples; null: vertex i
                                       // is position i with uv i and colours[0] / speculars[0]
                                       // (the flat geometry draw)
    int cornerCount;                   // without corners: the vertex count
    const std::uint16_t* indices;      // a triangle list over the corners
    int indexCount;
    float uOffset, vOffset;            // added to the texture coordinates when `moveUv`
    bool moveUv;
    const Matrix4* matrix;             // row-major, out = M * (x, y, z, 1)
    bool screenMatrix;                 // `matrix` gives screen pixels after the divide (the pipeline's
                                       // screen stack), else Direct3D clip space
    bool clipped;                      // the old path clips it (else only projects it: the depth is
                                       // not cut at the near and far planes)
    bool distanceFog;                  // the old path fogs the vertices by distance
    float zScale;                      // the depth is multiplied by it (0 taken as 1)
    bool coloursWritten;               // the draw set every colour to the device's (flag 4)
// The normals (blocks of four as the positions) and the words ((position, normal) index pairs, by the
// corners' colour index) of a figure mesh lit on the GPU (MeshSource::lighting); null without.
    const float* normals;
    int normalCount;
    const std::uint16_t* words;
    int wordCount;
};

// A texture loaded from its .mmp file: `data` the file in memory, the levels from `mip` on (`levels` of
// them) that the game loads for `holder`: decoded from the file, kept until the holder goes. False when
// it was not decoded (the renderer is off, or the format is not one it reads).
bool TextureFromMmp(const void* holder, const void* data, unsigned mip, unsigned levels);

// From now on the rectangle `to` of the texture of `target` shows the rectangle `from` of the texture of
// `source` (rectangles left, top, right, bottom in texels of the first level): every mesh draw sampling
// `target` in its first stage takes those texels from `source`; the target's texture is not changed. One
// per target (a new one replaces it); forgotten when either holder goes. Nothing is recorded when a
// rectangle is empty or outside its texture. Used for the unit's face (the blink).
void TexturePatch(const void* target, const int to[4], const void* source, const int from[4]);

// The game made new pixels for a texture on the CPU (the shadow textures) and copies them into its
// surface: the renderer takes them from the buffer (`pitch` bytes per row, `bitsPerPixel` 16 or 32, the
// surface's channel masks). Nothing for a holder with SurfaceMemory: it is read from that memory.
void TexturePixels(const void* holder, const void* pixels, int width, int height, int pitch, int bitsPerPixel,
                   std::uint32_t redMask, std::uint32_t greenMask, std::uint32_t blueMask, std::uint32_t alphaMask);
void BeginMesh(void* device, const MeshDraw& mesh);
bool MeshPass(void* device);
void EndMesh();

// Pre-transformed vertices the old path draws as they are (FVF 0xC4: screen x, y, depth z, rhw, colour,
// specular; rhw taken as 1): the rectangle fill and outline. A triangle list over `indices`, or a line
// strip over the vertices (`indices` null). Takes the old device's state for the draw; returns whether the
// old path still draws it.
struct ScreenVertex {
    float x, y, z, rhw;
    std::uint32_t colour, specular;
};
bool ScreenDraw(void* device, const ScreenVertex* vertices, int count, const std::uint16_t* indices, int indexCount);

// A Direct3D 7 draw of pre-transformed vertices the old path is about to make: `fvf` 0x1C4 (x, y, z, rhw,
// colour, specular, u, v) or 0xC4 (no texture coordinates), `primitive` a D3DPRIMITIVETYPE (2 line list,
// 3 line strip, 4 triangle list, 5 strip, 6 fan), indexed when `indices`. The vertices' fog is already
// applied. Takes the old device's state; returns whether the old path still draws it.
bool TlDraw(void* device, const void* vertices, int fvf, int vertexCount, const std::uint16_t* indices, int indexCount,
            int primitive);

// A window surface's blit onto the frame: the holder, the screen rectangle, the source rectangle (null:
// the whole surface, stretched) and the colour key (in the surface's pixel format) when `keyed`.
struct Rect {
    int left, top, right, bottom;
};
void SurfaceBlit(const void* holder, const Rect& destination, const Rect* source, bool keyed, std::uint32_t key);
// The game wrote `written` (pixels; null: all of it) of the SurfaceMemory of `holder`: a window surface
// is read again only where marked (and there only the rows and columns that differ from its texture), and
// uploaded. Writers: the DC release, DirectPixels for writing, CopyFrameTo.
void SurfaceWritten(const void* holder, const Rect* written);
// A texture holder's memory is read again only when marked written since its texture last read it (and it
// differs from what the texture was made from), into the same image.
// Harness build only: the window surfaces and memory textures whose memory changed with no mark (a
// writer without a mark: stale pixels in the product; the run fails), and the first one's description.
int MissedSurfaceWrites(const char** first);
// Harness build only ("memorytextures"): memory textures updated in place with their levels made on the
// GPU (on, the default), or made again as new images with levels box-filtered on the CPU (off, for frame
// comparisons). Every memory texture is read again at its next use.
void SetMemoryTexturesInPlace(bool on);

// A movie's decoded frame (CBinkVideo): R, G, B, X bytes per pixel, kept for `key` (the video object)
// until the next one; MovieBlit draws the last frame into the screen rectangle `to`.
void MovieFrame(const void* key, const std::uint8_t* rgbx, int width, int height);
void MovieBlit(const void* key, const Rect& to);

// A solid rectangle `to` in `colour` (D3DCOLOR; alpha ignored): the movie player's fill before its first
// frame.
void Fill(const Rect& to, std::uint32_t colour);

// The software cursor: the part `from` of the cursor image's surface with its mask (the image's full width,
// the surface's pixel format; the old path draws (background & mask) | image) at the screen rectangle `to`.
// Texels whose mask is not zero are left out.
void Cursor(const void* imageHolder, const void* mask, const Rect& from, const Rect& to);

// The software cursor between frames (render_cursor.cpp, renderer.md section 9). Called from the cursor
// thread with the cursor's lock held: the last presented frame (as drawn before its cursor) is presented
// again with the part `from` of the cursor image (holder and mask as for Cursor, which must have drawn the
// image before) at `to`. Only when the renderer presents itself and nothing was presented for 40 ms;
// false when nothing was presented. Thread-safe: the Direct3D 11 work is recorded on a deferred context
// and run as one command list.
bool CursorBetweenFrames(const void* imageHolder, const void* mask, const Rect& from, const Rect& to);
// The cursor thread's presents so far, the last one's rectangle and, in the verify build, the checks of
// the frame it presented (the cursor's texels where its mask is clear) and how many differed.
struct CursorPresentInfo {
    std::uint32_t presents = 0;
    std::uint32_t calls = 0;
    std::uint32_t refused[4] = {};   // not presented: picture not drawn yet, no frame, a frame within 40 ms, outside
    Rect lastTo{0, 0, 0, 0};
    int checks = 0, mismatches = 0;
};
CursorPresentInfo CursorPresents();
// Verify build: the frame the cursor thread presented last (after the gamma ramp), as 24-bit BGR rows from
// the top. False when there is none.
bool ReadCursorPresent(int* width, int* height, std::vector<std::uint8_t>* bgr);

// The old path clears the depth buffer to `z` mid-frame (before the interface's panels and 3D previews
// draw): `count` screen rectangles, or all of it when `rects` is null.
void ClearDepth(float z, const Rect* rects, int count);

// The sector, the renderer object (whose transform pipeline holds the current clip matrix) and the page
// (ground pages 0..pages+1, water pages after them) of every sector page the game draws.
void TerrainPage(const terrain::SectorData* sector, void* renderer, int page);
// The game changed what the sector's pages are built from: Rebuilt its tile lists and records; Moved
// vertices or texture coordinates (animated tiles); WaterMoved only the water layer's positions (waves,
// ApplyWaterLevels).
enum class TerrainChange { Rebuilt, Moved, WaterMoved };
void TerrainChanged(const terrain::SectorData* sector, TerrainChange change);
// Harness build only ("terraincache"): terrain pages kept on the GPU (on, the default) or built and
// uploaded every frame (off, for frame comparisons); the pages kept and the checks of kept pages against
// a fresh build (every 16th frame) with how many found one stale (a change TerrainChanged was not told of).
void SetTerrainCache(bool on);
struct TerrainCacheInfo {
    bool on = true;
    std::size_t pages = 0;
    std::uint64_t checks = 0, stale = 0;
};
TerrainCacheInfo TerrainCacheState();

// A figure part lit on the GPU (renderer.md section 16), as 3DfpFPU's LightMesh lit its words: per channel
// the largest of the ambient colour, the sun (diffuse colour times the sun's colour times `coefficient`,
// times the cosine to the sun) and each light (the same with its colour, times the cosine and
// 1 - distance^2 / range^2 where not below 0), plus the emissive colour, at most 1; the alpha the diffuse
// colour's. Lights and sun are taken into the part's model space (the inverse of `model`); `crown`: the
// colours pulled 0.6 of the way to the part's average (the flora's light on the "crown" meshes).
struct MeshLightSource {
    Vector3 position;              // the landscape's space
    float inverseRangeSquared;
    float colour[3];
};
struct MeshLighting {
    Matrix4 model;                 // the part's model space to the landscape's (row-major); with `camera`, the
                                   // camera times that
    const Matrix4* camera;         // the scene's camera (worlddata::SceneCamera), else null
    bool sun;                      // a sun (else no sun light)
    Vector3 sunDirection;          // the direction its light travels
    float sunColour[3];
    float ambientColour[3];        // the transform object's (CVisualizator::SetBaseColour's last)
    float diffuseColour[4];        // the material's
    float emissiveColour[3];
    float coefficient;             // the options' objects lighting coefficient (CLight::SetLitColors)
    bool crown;
    const MeshLightSource* lights;   // the lights within the object's reach (its draw's list)
    int lightCount;
};

// The figure mesh the next mesh draws draw. The renderer keeps the mesh's corners on the GPU under `owner`
// and builds and uploads again only what the game says changed: the colours when the draw wrote them, the
// positions on MeshChanged. Set by the figure part draws right before the device's draw and cleared right
// after it; a mesh draw without one is built and uploaded every frame.
struct MeshSource {
    const void* owner;     // the figure mesh (CFigureMesh) drawn
    const void* shared;    // the link figure (CLinkMesh) whose part it draws, else null
    bool moved;            // the positions handed over are a copy with the per-vertex offsets added
    bool coloursWritten;   // this draw wrote the mesh's colour arrays (lit, flat colours)
    const MeshLighting* lighting = nullptr;   // lit on the GPU (the draw's colours: only their alpha)
};
void SetMeshSource(const MeshSource* source);
// The game changed a figure mesh or link figure in place: its vertices' positions (per-vertex offsets, a
// part morphed again), its colours outside its draw (Colours: the sky's lighting; a link figure's part lit
// or filled), or its buffers (Rebuilt: loaded, cleared, mirrored, a part added or removed, destroyed).
enum class MeshChange { Positions, Colours, Rebuilt };
void MeshChanged(const void* object, MeshChange change);
// Mesh draws between SetInterfaceDraw(true) and (false) (CUiMesh::Draw) are built and uploaded every
// frame: the interface rewrites its sprites' corners in place in many places, a few corners each.
void SetInterfaceDraw(bool on);
// Harness build only ("meshcache"): figure meshes kept on the GPU (on, the default) or built and uploaded
// every frame (off, for frame comparisons); the meshes kept and the checks of kept meshes against a fresh
// build (every 16th frame) with how many found one stale.
void SetMeshCache(bool on);
struct MeshCacheInfo {
    bool on = true;
    std::size_t meshes = 0;
    std::uint64_t checks = 0, stale = 0;
};
MeshCacheInfo MeshCacheState();

// Copies the new frame's `source` rectangle (screen pixels, as drawn so far) into the SurfaceMemory of
// `holder`, stretched to its size: the escape menu's background, the save game's picture. False when the
// renderer is off, the holder has no such memory or the copy failed.
bool CopyFrameTo(const void* holder, const Rect& source);

// The end of the game's frame: draws what was handed over since the last present into the off-screen
// target and presents it on the game window. Once per frame; captures call it before reading.
void EndFrame();

// The game presents: EndFrame if not done yet, then a new frame begins.
void Present();

// The last frame EndFrame drew, as 24-bit BGR rows from the top. False when the sidecar is off or
// has no frame.
bool ReadFrame(int* width, int* height, std::vector<std::uint8_t>* bgr);

// The switch-over's own present on the game window (render_window.cpp). The display's gamma ramp (three
// 256-entry ramps, full scale 65280; null channels identity) is applied as the frame goes to the window;
// captures read the frame before it. GammaRamp copies the lookup in use (256 RGBA8 entries).
void SetGammaRamp(const std::uint16_t* red, const std::uint16_t* green, const std::uint16_t* blue);
void GammaRamp(std::uint8_t* rgba256);
// Frames presented on the game window so far; the verify build's checks of the presented frame against
// the gamma ramp (after each SetGammaRamp, at a grid of points, when the frame shows one to one): how many
// ran, the last one's points and differing points, whether one is still due.
struct WindowPresentInfo {
    std::uint32_t presents = 0;
    int rampChecks = 0, rampPoints = 0, rampDiffering = 0;
    int cursorChecks = 0, cursorMismatches = 0;   // verify build: the cursor the present draws, checked in the back buffer
    bool rampCheckPending = false;
    bool rampIdentity = true;   // the ramp in use is the identity
    bool failed = false;   // no swap chain, or the device was lost and could not be made again
    int deviceLosses = 0;       // Direct3D 11 devices lost (or LoseDevice) ...
    int deviceRecoveries = 0;   // ... and made again with everything on them
    std::uint32_t presentsAtRecovery = 0;   // `presents` when the last new device was made
    long lastLossResult = 0;   // the last loss: the DXGI error that reported it ...
    long lastLossReason = 0;   // ... and GetDeviceRemovedReason then (S_OK: LoseDevice's, not a real loss)
    bool lastLossSimulated = false;   // the last loss was LoseDevice's
    bool removable = false;   // harness build: the device is "removable" (RemoveDevice can remove it)
    int removals = 0;         // RemoveDevice calls
};
WindowPresentInfo WindowPresents();
// Acts as if the Direct3D 11 device were lost at the next present ("losedevice"): the swap chain's Present
// is not called but reported DXGI_ERROR_DEVICE_REMOVED. Then, as for a real loss, the device, sokol, the
// swap chain and every image are made again and textures uploaded again from what they were decoded from.
void LoseDevice();
// Harness build only (EI_RENDER "removable"): the device was made through D3D11On12 on a Direct3D 12
// device, and this removes that device for real (ID3D12Device5::RemoveDevice, this process only) right
// before the next present; everything is made again as for any lost device. False when the device is not
// removable.
bool RemoveDevice();
// Harness build only ("terrainlevels"): what the ground pages draw. Levels: the texture level the page's
// sampler reads at each pixel instead of their colours (red = level * 32, mips blended as the sampler
// blends them; green 0x5A and blue 0xA5 mark the pixel), from a texture of the page's size with one grey
// per level; PointMips: the same through the old sampler (bilinear, D3DTFP_POINT mips: the level steps
// along the tiles' edges that showed as seams). Off: the pages' colours.
enum class TerrainLevelMode { Off, Levels, PointMips };
void TerrainLevels(TerrainLevelMode mode);
// Harness build only ("mmplevels"): how far the levels of the .mmp textures held now, decoded as for
// drawing, lie from their level 0: both box-filtered down to 4 x 4, the mean absolute difference per
// channel (0..255), the largest over all levels. Levels decoded from the wrong bytes (bright green strips
// over the trees) lie far off; the file's own mips stay close.
struct MmpLevelCheck {
    int textures = 0, levels = 0;
    double worst = 0.0;
    int worstWidth = 0, worstHeight = 0, worstLevel = 0;   // that texture's first level's size, the level
    std::uint32_t worstFormat = 0;                         // its .mmp format id
};
MmpLevelCheck CheckMmpLevels();
// Where the frame shows in the game window's client area: the frame's size and its rectangle there
// (aspect kept, centred). False when the renderer does not present itself.
bool FrameInWindow(int* frameWidth, int* frameHeight, Rect* placed);
// Client-area point to frame pixel (outside the frame: beyond its edges) and back (the centre of the frame
// pixel's window pixels). Unchanged when the frame fills the client area or the renderer does not present.
void WindowToFrame(int* x, int* y);
void FrameToWindow(int* x, int* y);

} // namespace render
