#pragma once

// The new renderer: a sidecar module that draws the game's frame with sokol_gfx (Direct3D 11) from
// data pulled out of the game (docs/notes/renderer.md) and presents it on the game window. The game
// code that used to talk to the old Direct3D 7 path hands its data here instead; the Direct3D 7 calls
// left in game code go to stand-ins that draw nothing (device.h).
//
// The standalone image always draws with it: when the device object is built (StartPresenting) the
// game gets no DirectDraw or Direct3D 7 device at all (ticket R9a), the new renderer presents through
// its own swap chain on the game window, the holders of 004EF7B0 / 004F0060 and the texture
// manager's textures have no DirectDraw surface (the CPU-written, shadow and .bmp ones only memory
// the new renderer owns, SurfaceMemory; the .mmp ones only its decoded copy, TextureFromMmp), and
// captures write the new frame. There is no DirectDraw fallback (R11a): when the new renderer cannot
// start the game ends with an error. In game.exe (the harness and the vector recorders run in the
// original's process) it is off. EI_RENDER (or the harness step "render <spec>"), a comma-separated
// list, carries the smooth option ("smooth", SetSmooth), the classic shadows ("classicshadows",
// SetClassicShadows) and the harness build's "removable"; "new",
// "present" and "cpuwindows" are accepted and change nothing.

#include <cstdint>
#include <vector>

struct Matrix4;   // lib/vecmath.h

namespace terrain {
struct SectorData;
}

namespace render {

// Sets the options from `spec` (the format of EI_RENDER) and EI_RENDER's. Without a call the
// environment is read at the first use.
void Configure(const char* spec);

// The smooth option (R12a; user 2026-10-20: off by default): the image as the user's Rebirth dgVoodoo
// setup draws it (starter\Engine\dgVoodoo.conf: Filtering 16, Antialiasing 8x). On, the world view's
// models (figures, map objects, the sky, shadows) are sampled anisotropically (16x) with linear mips
// where their stage asks for linear filters (the terrain pages always are, R12), and the world view is
// drawn with 8x multisampling (fewer when the device has no 8x), resolved into the frame before the
// interface draws over it, before every present and every screen copy or capture. The interface's
// sprites, previews and window surfaces draw as without it. Its one switch: EI_RENDER's word "smooth"
// (read at the start) or SetSmooth (takes effect at the next frame). Off: what the game asks for.
void SetSmooth(bool on);
bool Smooth();
// The world view's draws (CWorldClient::Render, 00569E40) come between BeginWorld and EndWorld.
void BeginWorld();
void EndWorld();
// The smooth option's state: on, the samples the device gives (up to 8) and the world's target's,
// and in the last presented frame the multisampled passes and the models drawn anisotropically
// (world) or as their stage says (the interface's).
struct SmoothInfo {
    bool on = false;
    int deviceSamples = 1, targetSamples = 1;
    int multisampledPasses = 0;
    int anisotropicModels = 0, plainModels = 0;
};
SmoothInfo SmoothState();

// Object shadows (R16j, docs/notes/renderer.md section 13). By default the world's figures and map
// objects cast shadows from the sun through a shadow map made every frame, sampled with filtering
// (PCF) where the terrain, the objects and the figures draw; the game's shadow decals (0051CE60's
// 64 x 64 textures laid on the ground) are not drawn. The classic option draws the decals as the
// game makes them and no shadow map: EI_RENDER's word "classicshadows" or SetClassicShadows (from
// the next frame; ST2's options screen).
void SetClassicShadows(bool on);
bool ClassicShadows();
// The sun for this frame's world view: the direction its light travels (CLight::direction_, z
// negative by day), from CWorldClient::Render. Without it no shadow map is made.
void ShadowSun(const float direction[3]);
// The meshes handed over between BeginCaster and EndCaster belong to `object` (the world's object
// draws, 0056AFE0); CastsShadow (from the object's shadow draw 0051CE60, when the game would draw
// its shadow: the options' switches, visible) lets them cast into this frame's shadow map, weaker
// than `strength` 0.5 not at all (a unit fading in). ClassicShadowDecal: the next flat mesh (until
// EndMesh) is that object's shadow decal, dropped unless the classic option is on.
void BeginCaster(const void* object);
void EndCaster();
void CastsShadow(const void* object, float strength);
void ClassicShadowDecal();
// The last presented frame's shadow map: made or not (classic option, no sun or terrain, no
// caster), its size, the world extent it covers (the larger side), the casters' objects and draws,
// the meshes and terrain batches that sampled it. Of the objects whose shadow the game draws
// (CastsShadow, strong enough): `offScreen` handed over no mesh (the game did not draw them), `lost`
// handed over meshes of which none went into the map (none should).
struct ShadowInfo {
    bool classic = false;
    bool made = false;
    int size = 0;
    float extent = 0.0f;
    int casters = 0, casterDraws = 0, receivingMeshes = 0, receivingTerrain = 0;
    int offScreen = 0, lost = 0;
};
ShadowInfo ShadowState();

// What the new renderer did for the presented frames (R16, docs/notes/renderer.md section 13): the
// hand-over's commands, the bytes written to the GPU, the draws and state changes, the textures made
// and the CPU time spent, summed over `frames`. TakeFrameStats returns the sums since its last call
// and starts new ones (the harness's "renderstats"). The CPU times and the reuse counts are the
// harness build's only (zero in the product).
struct FrameStats {
    enum Upload { Mmp, CpuPixels, MemoryTexture, WindowSurface, Movie, Small, UploadKinds };
    std::uint32_t frames = 0;
    int width = 0, height = 0;   // the last frame's size
    // Hand-over: terrain pages and the batches they became, meshes handed over (BeginMesh with
    // corners built) and their passes, pre-transformed draws (TlDraw, ScreenDraw), window blits
    // (SurfaceBlit, movies, cursor, fills), depth clears, the old device's state read per pass.
    std::uint64_t terrainPages = 0, terrainBatches = 0, meshes = 0, meshPasses = 0, transformedDraws = 0;
    std::uint64_t blits = 0, depthClears = 0, stateReads = 0, matrices = 0;
    // Of the meshes: the flat geometry (004EA4A0: object shadows, ground prints, selection markers,
    // light decals) and its vertex bytes.
    std::uint64_t flatMeshes = 0, flatVertexBytes = 0;
    // The vertices handed over against the last frame's of the same terrain page (sector, page) or
    // mesh (its positions' address): all the same, only the colours changed (relit), positions or
    // texture coordinates changed (moved), or none last frame (new); meshes whose positions and
    // texture coordinates equal another's of the same frame (shared: instancing candidates).
    enum Reuse { Same, Relit, Moved, New, ReuseKinds };
    std::uint64_t terrainReuse[ReuseKinds] = {}, meshReuse[ReuseKinds] = {}, sharedMeshes = 0;
    // Bytes appended to the per-frame vertex and index buffers, uniform bytes.
    std::uint64_t terrainVertexBytes = 0, terrainIndexBytes = 0, meshVertexBytes = 0, meshIndexBytes = 0;
    std::uint64_t uniformBytes = 0;
    // Submission: passes, draws, pipeline applies and changes, bindings applies, changes of the
    // first texture bound.
    std::uint64_t passes = 0, draws = 0, pipelineApplies = 0, pipelineChanges = 0, bindings = 0, textureChanges = 0;
    // Images made (or updated in place) per kind and their bytes (every level); bytes compared with
    // the snapshots of the renderer's memory (window surfaces where marked written, memory textures
    // once a frame) and converted from it.
    std::uint64_t uploads[UploadKinds] = {}, uploadBytes[UploadKinds] = {}, largestUpload[UploadKinds] = {};
    std::uint64_t comparedBytes = 0;
    // CPU time (ms): the hand-over (terrain pages, meshes, pre-transformed draws), texture work
    // (views checked, converted, made; windowMs: of it, the window surfaces'), EndFrame (uploads,
    // draws, the present on the window). (statsMs: these counts' own hashing and checks, left out of
    // the others; terrainMs: the terrain pages' hand-over and uploads, left out of the times around them.)
    double terrainMs = 0.0;
    double handOverMs = 0.0, textureMs = 0.0, windowMs = 0.0, endFrameMs = 0.0, statsMs = 0.0;
    // R16j: the shadow map's draws, its CPU time (fitting, matrices, the pass; left out of the times
    // around it) and, harness build, GPU time (timestamp queries, read a few frames later: summed over
    // gpuFrames) of the shadow pass and of the whole frame's passes.
    std::uint64_t shadowDraws = 0;
    double shadowMs = 0.0;
    std::uint32_t gpuFrames = 0;
    double gpuShadowMs = 0.0, gpuFrameMs = 0.0;
};
FrameStats TakeFrameStats();

// The new renderer draws: in the standalone image, until it failed (no Direct3D 11 device at the
// start, a sokol panic, a lost device that could not be made again). Every call below does nothing
// when it is off.
bool Active();

// The off-screen surfaces the game makes through 004EF7B0 (interface windows, the software cursor,
// movies) and 004F0060 (system-memory pictures) keep their pixels in memory the new renderer owns
// (NewSurfaceMemory) and have no DirectDraw surface. The new renderer reads a holder's (CDXSurface)
// pixels there wherever it draws it. Texture holders have such memory too (the save / load screen's
// pictures, the textures of 00691D80, the shadow and the .bmp textures): a mesh pass that binds one
// reads the memory, again whenever it changed.
// A surface holder's pixels: a zeroed top-down DIB section (GDI can draw into it through a memory
// DC) of `width` x `height` pixels, 16 or 32 bits each, the channels at the given masks (`alphaMask`
// 0 without alpha). The game code writes it (GDI, pixel code) and the new renderer reads it.
struct SurfaceMemory {
    void* bits = nullptr;
    void* bitmap = nullptr;   // the DIB section (HBITMAP)
    int pitch = 0, width = 0, height = 0, bitsPerPixel = 0;
    std::uint32_t redMask = 0, greenMask = 0, blueMask = 0, alphaMask = 0;
};
// Makes the memory of `holder` (replacing any it had); null when it cannot be made. The pointer
// stays valid until the memory is freed.
const SurfaceMemory* NewSurfaceMemory(const void* holder, int width, int height, int bitsPerPixel, std::uint32_t redMask,
                                      std::uint32_t greenMask, std::uint32_t blueMask, std::uint32_t alphaMask);
// The memory of `holder`, or null.
const SurfaceMemory* SurfaceMemoryOf(const void* holder);
void FreeSurfaceMemory(const void* holder);
// The holder is destroyed (004EFD20): its memory freed, what the renderer kept for it forgotten.
void HolderGone(const void* holder);
// A holder whose surface was loaded from an 8888 .mmp picture (004EFC60: the software cursor's
// images): the new renderer keeps the picture's pixels from the file data.
void PictureFromMmp(const void* holder, const void* data);

// The device object is being built (004EDD70): the new renderer starts now and from here on
// presents through its own swap chain on the game window; the game then builds its device without
// DirectDraw (device.h). False when the new renderer cannot start (the game then ends with an error).
bool StartPresenting();
// StartPresenting succeeded: the new renderer shows the frames itself.
bool PresentsItself();
// Whether presenting onto the game window waits for the vertical blank (default on). The harness
// turns it off under its virtual clock.
void SetWaitForVsync(bool on);

// A mesh draw of the old path (004E9010 / 004E9A50 on the device object), after the game has
// decided it is visible and set its colours: the mesh in model space and the matrix the old path
// projects it with. Handed over between BeginMesh and EndMesh; every pass the old path draws in
// between (004EB550 / 004EB370) calls MeshPass, which takes the old device's state for that pass
// (blend, alpha test, depth, culling, fog, texture stages, the bound textures) and returns whether
// the old path still draws it (only when the new renderer did not take it).
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
    const Matrix4* matrix;             // row-major, out = M * (x, y, z, 1)
    bool screenMatrix;                 // `matrix` gives screen pixels after the divide (the pipeline's
                                       // +0x1C stack), else Direct3D clip space (+0x24)
    bool clipped;                      // the old path clips it (else only projects it: the depth is
                                       // not cut at the near and far planes)
    bool distanceFog;                  // the old path fogs the vertices by distance (device +0x158)
    float zScale;                      // the depth is multiplied by it (0 taken as 1)
    bool coloursWritten;               // the draw set every colour to the device's (flag 4)
};

// A texture loaded from its .mmp file (00690430): `data` the file in memory, the levels from
// `mip` on (`levels` of them) that the game loads for `holder` (a CDXSurface without a DirectDraw
// surface): decoded from the file, keyed by the holder, kept until the holder goes (HolderGone).
// False when it was not decoded (the new renderer is off, or the format is not one it reads).
bool TextureFromMmp(const void* holder, const void* data, unsigned mip, unsigned levels);

// From now on the rectangle `to` of the texture of `target` shows the rectangle `from` of the
// texture of `source` (both surface holders; rectangles left, top, right, bottom in texels of the
// textures' first level): every mesh draw sampling `target` in its first stage takes those texels
// from `source` in the shader (on every level); the target's texture is not changed. One per
// target: a new one replaces it; forgotten when either holder goes (HolderGone). Nothing is
// recorded when a rectangle is empty or outside its texture (the old path's blit failed then).
// The unit's face (00533980, the blink).
void TexturePatch(const void* target, const int to[4], const void* source, const int from[4]);

// The game made new pixels for a texture on the CPU (the shadow textures, 0051CE60) and copies
// them into its surface (CDXSurface): the new renderer takes them from the buffer (`pitch` bytes
// per row, `bitsPerPixel` 16 or 32, the channel masks of the surface's format). Nothing for a holder
// with SurfaceMemory (the switch-over's shadow textures): it is read from that memory.
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
// The game wrote `written` (pixels; null: all of it) of the SurfaceMemory of `holder` (R16a): a
// window surface is read again only where it was marked (and there only the rows and columns that
// differ from what its texture holds), and that part uploaded. Writers: the DC release 005FDC60 (GDI's
// bounds), DirectPixels for writing (the pixel code), CopyFrameTo.
void SurfaceWritten(const void* holder, const Rect* written);
// Harness build only: the window surfaces whose memory changed where no SurfaceWritten mark said (a
// writer without a mark: stale pixels in the product; the run fails), and the first one's description.
int MissedSurfaceWrites(const char** first);

// A movie's decoded frame (CBinkVideo 005DBD80): R, G, B, X bytes per pixel (BinkCopyToBuffer's
// 32-bit "R" type), kept for `key` (the video object) until the next one; MovieBlit draws the last
// frame into the screen rectangle `to`.
void MovieFrame(const void* key, const std::uint8_t* rgbx, int width, int height);
void MovieBlit(const void* key, const Rect& to);

// A solid rectangle `to` in `colour` (D3DCOLOR; alpha ignored): the movie player's fill of the screen
// (005DB9E0) before its first frame.
void Fill(const Rect& to, std::uint32_t colour);

// The software cursor (005E54A0): the part `from` of the cursor image's surface (holder) with its
// mask (`mask`: the image's full width, the surface's pixel format; the old path draws
// (background & mask) | image) at the screen rectangle `to`. Texels whose mask is not zero are left
// out.
void Cursor(const void* imageHolder, const void* mask, const Rect& from, const Rect& to);

// The software cursor between frames (the cursor thread 005E5690 in the switch-over without
// DirectDraw; render_cursor.cpp, docs/notes/renderer.md section 9, R9c2). Called from the cursor
// thread with the cursor's lock held: the last frame presented on the game window (as drawn before
// its cursor) is presented again with the part `from` of the cursor image (holder and mask as for
// Cursor; the image must have been drawn by Cursor before) at `to`. Only when the new renderer
// presents itself and nothing was presented on the game window for 40 ms (the main thread's frames
// show the cursor themselves); false when nothing was presented. Thread-safe: no sokol call, the
// Direct3D 11 work recorded on a deferred context and run as one command list.
bool CursorBetweenFrames(const void* imageHolder, const void* mask, const Rect& from, const Rect& to);
// The cursor thread's presents so far, the last one's rectangle and, in the verify build, the
// checks of the frame it composed (the last frame without its cursor, the cursor's texels where its
// mask is clear) and how many differed.
struct CursorPresentInfo {
    std::uint32_t presents = 0;
    std::uint32_t calls = 0;       // moves the cursor thread handed over
    std::uint32_t refused[4] = {};   // not presented: picture not drawn yet, no frame, a frame within 40 ms, outside
    Rect lastTo{0, 0, 0, 0};
    int checks = 0, mismatches = 0;
};
CursorPresentInfo CursorPresents();
// Verify build: the frame the cursor thread presented last (before the gamma ramp), as 24-bit BGR
// rows from the top. False when there is none.
bool ReadCursorPresent(int* width, int* height, std::vector<std::uint8_t>* bgr);

// The old path clears the depth buffer to `z` in the middle of the frame (before the interface's
// panels and 3D previews draw): `count` screen rectangles, or all of it when `rects` is null.
void ClearDepth(float z, const Rect* rects, int count);

// 006BFF40 (DrawSectorPage) hands every sector page the game draws: the sector, the renderer object
// (007AF060, whose transform pipeline holds the current clip matrix) and the page (ground pages
// 0..pages+1, water pages after them).
void TerrainPage(const terrain::SectorData* sector, void* renderer, int page);
// The game changed what the sector's pages are built from (R16c: the renderer keeps them on the GPU):
// `rebuilt` its tile lists and records (SectorData::Build, Clear), else it moved vertices or texture
// coordinates (Animate's waves and animated tiles, ApplyWaterLevels).
void TerrainChanged(const terrain::SectorData* sector, bool rebuilt);
// Harness build only (the harness's "terraincache", R16c): the terrain pages kept on the GPU (on, the
// default) or built and uploaded again every frame (off: as before R16c, for frame comparisons); the
// pages kept, and the checks of kept pages against a fresh build (every 16th frame) with how many
// found one stale (a change TerrainChanged was not told of).
void SetTerrainCache(bool on);
struct TerrainCacheInfo {
    bool on = true;
    std::size_t pages = 0;
    std::uint64_t checks = 0, stale = 0;
};
TerrainCacheInfo TerrainCacheState();

// R16d: the figure mesh the next mesh draws (004E9010 / 004E9A50) draw. The new renderer keeps the
// mesh's corners (positions and texture coordinates in model space, colours) on the GPU under `owner`
// and builds and uploads again only what the game says changed: the colours when the draw wrote them,
// the positions on MeshChanged. Set by the figure part draws (005A7A70, 005A7640) right before the
// device's draw and cleared (null) right after it; a mesh draw without one is built and uploaded again
// every frame.
struct MeshSource {
    const void* owner;     // the figure mesh (CFigureMesh) drawn
    const void* shared;    // the link figure (CLinkMesh) whose part it draws, else null
    bool moved;            // the positions handed over are a copy with the per-vertex offsets added
    bool coloursWritten;   // this draw wrote the mesh's colour arrays (lit, flat colours)
};
void SetMeshSource(const MeshSource* source);
// The game changed a figure mesh (CFigureMesh) or link figure (CLinkMesh) in place: its vertices'
// positions (an animation's per-vertex offsets, a part morphed again), its colours outside its draw
// (Colours: the sky's lighting on the sky object; a link figure's part lit or filled, on the colour
// array's data, which the original code also does outside the figure part draws), or its buffers
// (Rebuilt: loaded, cleared, mirrored, a part added or removed, destroyed).
enum class MeshChange { Positions, Colours, Rebuilt };
void MeshChanged(const void* object, MeshChange change);
// Mesh draws between SetInterfaceDraw(true) and SetInterfaceDraw(false) (an interface sprite's draw,
// CUiMesh::Draw 005E6D90) are built and uploaded every frame: the interface rewrites its sprites'
// corners in place in many places (bars, map markers, the original's and Rebirth's code), and they are
// a few corners each.
void SetInterfaceDraw(bool on);
// Harness build only (the harness's "meshcache", R16d): the figure meshes kept on the GPU (on, the
// default) or built and uploaded every frame (off: as before R16d, for frame comparisons); the meshes
// kept, and the checks of kept meshes against a fresh build (every 16th frame) with how many found
// one stale (a change the game did not tell of).
void SetMeshCache(bool on);
struct MeshCacheInfo {
    bool on = true;
    std::size_t meshes = 0;
    std::uint64_t checks = 0, stale = 0;
};
MeshCacheInfo MeshCacheState();

// Copies the new frame's `source` rectangle (screen pixels, as drawn so far this frame) into the
// SurfaceMemory of `holder` (a CDXSurface), stretched to its size: the screen copies of 005FD3D0
// (the escape menu's background, the save game's picture). False when the sidecar is off, the holder
// has no such memory or the copy failed.
bool CopyFrameTo(const void* holder, const Rect& source);

// The end of the game's frame: draws what was handed over since the last present into the
// off-screen target and presents it on the game window. Once per frame; captures call it before
// reading.
void EndFrame();

// The game presents (004EF610): EndFrame if not done yet, then a new frame begins.
void Present();

// The last frame EndFrame drew, as 24-bit BGR rows from the top. False when the sidecar is off or
// has no frame.
bool ReadFrame(int* width, int* height, std::vector<std::uint8_t>* bgr);

// The switch-over's own present on the game window (render_window.cpp; docs/notes/renderer.md,
// section 9). The display's gamma ramp (00504A60's three 256-entry ramps, full scale 65280; null
// channels identity) applied as the frame goes to the window; captures read the frame before it.
// GammaRamp copies the lookup in use (256 RGBA8 entries).
void SetGammaRamp(const std::uint16_t* red, const std::uint16_t* green, const std::uint16_t* blue);
void GammaRamp(std::uint8_t* rgba256);
// Frames presented on the game window so far; the verify build's checks of the presented frame
// against the gamma ramp (after each SetGammaRamp, at a grid of points, when the frame shows one
// to one): how many ran, the last one's points and differing points, whether one is still due.
struct WindowPresentInfo {
    std::uint32_t presents = 0;
    int rampChecks = 0, rampPoints = 0, rampDiffering = 0;
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
// Acts as if the Direct3D 11 device were lost at the next present (the harness's "losedevice"):
// the swap chain's Present is not called but reported DXGI_ERROR_DEVICE_REMOVED (without a swap
// chain of the game window's, the loss is taken at the end of the frame). Then, as for a real loss,
// the device, sokol, the swap chain and every image are made again and textures uploaded again
// from what they were decoded from.
void LoseDevice();
// Harness build only (EI_RENDER "removable", R9c5): the device was made through D3D11On12 on a
// Direct3D 12 device, and this removes that device for real (ID3D12Device5::RemoveDevice, this
// process only: no driver reset) right before the next present, which sees DXGI_ERROR_DEVICE_REMOVED;
// everything is made again as for any lost device (on a new removable device). False when the
// device is not removable.
bool RemoveDevice();
// Harness build only (the harness's "terrainlevels", R12): what the ground pages draw. Levels: the
// texture level the page's sampler reads at each pixel instead of their colours (red = level * 32,
// mip levels blended as the sampler blends them; green 0x5A and blue 0xA5 mark the pixel as
// such), from a texture of the page's size and levels with one grey per level; PointMips: the same
// through the old sampler (bilinear, D3DTFP_POINT mips: the level steps along the tiles' edges
// that showed as seams). Off: the pages' colours.
enum class TerrainLevelMode { Off, Levels, PointMips };
void TerrainLevels(TerrainLevelMode mode);
// Harness build only (the harness's "mmplevels", R12b): how far the levels of the .mmp textures held
// now, decoded as for drawing, lie from their level 0: both box-filtered down to 4 x 4, the mean
// absolute difference per channel (0..255), the largest over all levels down to 4 x 4.
// Levels decoded from the wrong bytes (the old offsets' overflow: bright green strips over the trees)
// lie far off; the file's own mips stay close.
struct MmpLevelCheck {
    int textures = 0, levels = 0;
    double worst = 0.0;
    int worstWidth = 0, worstHeight = 0, worstLevel = 0;   // that texture's first level's size, the level
    std::uint32_t worstFormat = 0;                         // its .mmp format id
};
MmpLevelCheck CheckMmpLevels();
// Where the frame shows in the game window's client area: the frame's size and its rectangle there
// (aspect kept, centred; the rest black). False when the new renderer does not present itself.
bool FrameInWindow(int* frameWidth, int* frameHeight, Rect* placed);
// Client-area point to frame pixel (outside the frame: beyond its edges) and back (the centre of the
// frame pixel's window pixels). Unchanged when the frame fills the client area one to one or the new
// renderer does not present itself.
void WindowToFrame(int* x, int* y);
void FrameToWindow(int* x, int* y);

} // namespace render
