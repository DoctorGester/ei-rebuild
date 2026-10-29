#pragma once

// The renderer: draws the game's frame with sokol_gfx (Direct3D 11) from the data the game hands over and
// presents it through its own swap chain on the game window. No fallback of any kind: when it cannot start, or
// cannot go on (a sokol panic, a lost device not made again, no swap chain), the game ends with the reason in the
// game's error box (renderer.md, "When the renderer fails"). Nothing calls it in game.exe's process (harness,
// vector recorders). See docs/notes/renderer.md.
// EI_RENDER (or the harness step "render <spec>") is a comma-separated list: "smooth", "smoothcamera", the
// harness build's "removable" and "checkall"; "new", "present" and "cpuwindows" are accepted and change nothing.

#include "lib/vecmath.h"

#include <cstdint>
#include <vector>

namespace terrain {
struct SectorData;
}

namespace render {

// Sets the options from `spec` (EI_RENDER's format) and EI_RENDER; without a call the environment is read
// at the first use.
void Configure(const char* spec);
// Starts making the device on its own thread at image start-up (before WinMain); the first use takes it
// (saves 0.15 s of each start).
void PrepareDevice();

// The smooth option (off by default): 16x anisotropic sampling of the world view's models and 8x
// multisampling of the world view, as the user's dgVoodoo setup draws it (renderer.md, "The smooth
// option"). EI_RENDER's "smooth" at the start, or SetSmooth from the next frame.
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
// Harness build only ("smoothorder"): the smooth passes in an order the game never draws. Before: an empty
// pass on the frame target first. Split: the world view's first run ends at its middle command and an
// empty pass on the frame target follows.
enum class SmoothOrder : std::uint8_t { Game, Before, Split };
void SetSmoothOrder(SmoothOrder order);

// The smooth camera (renderer.md section 14; off by default): the battle camera moves once a frame by the
// frame's time and follows its target blended between the target's last two ticks. Read by the main loop
// and CCamera::Update, not by the renderer. EI_RENDER's "smoothcamera" or SetSmoothCamera.
void SetSmoothCamera(bool on);
bool SmoothCamera();
// The performance overlay (render_perf.h). Switched by the settings file; the harness build has none.
bool PerfOverlaySupported();
void SetPerfOverlay(bool on);
// The sun for this frame's world view: the direction its light travels (z negative by day). Without it no
// shadow map is made.
void ShadowSun(const float direction[3]);
// The GPU lighting (renderer.md section 13). The world's draw hands over the frame's light, kept
// until the next hand-over: the sun, the ambient colour, the point lights the landscape lights its ground
// with (the nearest kMostSceneLights) and the camera. The terrain shader lights each pixel and SectorData::Light
// writes only the edge fade.
struct ScenePointLight {
    Vector3 position;   // the landscape's space
    float inverseRangeSquared;
    float colour[3];
    bool specular;      // light flag 0x40
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
// Object shadows (renderer.md section 13): figures and map objects cast from the sun into a shadow map made
// every frame; the game's 64 x 64 shadow decals are neither made nor drawn.
// The ground casts too (landscape flag 0x10): ShadowTerrainArea is ShadowCasterArea for a caster no higher
// than `highestTop` (false: no map this frame); a loaded sector in it casts its ground layer's 33 x 33
// vertices when ShadowCasterReaches its box.
struct ShadowCasterRect;
bool ShadowTerrainArea(float highestTop, ShadowCasterRect* area);
void ShadowTerrainSector(const terrain::SectorData* sector);
// The meshes handed over between BeginCaster and EndCaster belong to `object`; CastsShadow (from its shadow
// draw) lets them cast this frame, at least 0.5 strong. A `figure`'s meshes take only the shadows others
// cast, on every face; other objects' meshes are shadowed on the faces turned to the sun.
void BeginCaster(const void* object, bool figure);
void EndCaster();
void CastsShadow(const void* object, float strength);
// Casters the game does not draw this frame (or parts it left out) whose shadow can reach the visible ground
// cast through ShadowCasterMesh, read by the world's draw from the objects' parts. ShadowCasterArea: whether
// this frame gets a map, and the ground rectangle (landscape's space) a caster's centre must stand in: the
// visible ground's box swept back towards the sun as far as a caster no higher than `highestTop` reaches,
// widened by `widest` (the largest radius). ShadowCasterReaches is exact up to the bounds. CasterPartDrawn:
// `part` (MeshSource::owner) was drawn this frame as a caster's mesh. CasterStrength: -1 when CastsShadow was
// not called for `object` this frame. SetOffscreenCasters (harness "shadowcasters view|all|walked"): View,
// only what the game draws casts; Walked, every caster comes from the walk.
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
// The walk's time counts as the shadow map's CPU time.
void BeginShadowWalk();
void EndShadowWalk();
enum class OffscreenCasters { View, All, Walked };
void SetOffscreenCasters(OffscreenCasters mode);
// Harness build shadow checks. SetShadowView ("shadowview on|off"): the receivers show only how much sun the
// map hides. SetShadowShift ("shadowshift <texels>"): the map's origin moved by whole texels (half as many
// along), to show a filter that depends on the range cells.
void SetShadowView(bool on);
void SetShadowShift(int texels);
// The last presented frame's shadow map. Of the objects whose shadow the game draws: `offScreen` cast
// nothing and handed over no mesh, `lost` handed over meshes of which none went into the map (none should);
// `outside*`: casters through ShadowCasterMesh.
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

// What the renderer did for the presented frames (renderer.md section 13), summed over `frames`.
// TakeFrameStats returns the sums since its last call (the harness's "renderstats"). The CPU times and the
// reuse counts are the harness build's only.
struct FrameStats {
    enum Upload { Mmp, Movie, Small, UploadKinds };
    std::uint32_t frames = 0;
    int width = 0, height = 0;   // the last frame's size
// Hand-over: meshes are BeginMesh with corners built; stateReads are the passes' reads of the render state (ReadPassState).
    std::uint64_t terrainPages = 0, terrainBatches = 0, meshes = 0, meshPasses = 0, transformedDraws = 0;
    std::uint64_t blits = 0, depthClears = 0, stateReads = 0, matrices = 0;
// Of the meshes: the flat geometry (object shadows, ground prints, selection markers, light decals).
    std::uint64_t flatMeshes = 0, flatVertexBytes = 0;
// Vertices against the last frame's of the same terrain page or mesh: all the same, only colours changed,
// positions or texture coordinates changed, none last frame; shared: equal to another mesh of the same frame
// (instancing candidates).
    enum Reuse { Same, Relit, Moved, New, ReuseKinds };
    std::uint64_t terrainReuse[ReuseKinds] = {}, meshReuse[ReuseKinds] = {}, sharedMeshes = 0;
    std::uint64_t terrainVertexBytes = 0, terrainIndexBytes = 0, meshVertexBytes = 0, meshIndexBytes = 0;
    std::uint64_t uniformBytes = 0;
    std::uint64_t passes = 0, draws = 0, pipelineApplies = 0, pipelineChanges = 0, bindings = 0, textureChanges = 0;
// Images made (or updated in place) per kind.
    std::uint64_t uploads[UploadKinds] = {}, uploadBytes[UploadKinds] = {};
// CPU times (ms). statsMs (these counts' own hashing and checks) and terrainMs (the terrain pages'
// hand-over and uploads) are left out of the others.
    double terrainMs = 0.0;
    double handOverMs = 0.0, textureMs = 0.0, endFrameMs = 0.0, statsMs = 0.0;
// The game device's mesh draws (device_mesh.cpp's DrawMesh, the BeginDeviceMeshTime timer), the hand-over in them left out.
    double deviceMeshMs = 0.0;
// ProjectDecal for object shadows and ground prints; the device's flat draws (hand-over left out);
// RenderShadow.
    double decalShadowMs = 0.0, decalPrintMs = 0.0, flatMs = 0.0, shadowBuildMs = 0.0;
    std::uint64_t decalShadows = 0, decalPrints = 0, decalVertices = 0;
    std::uint64_t flatDraws = 0, flatDrawVertices = 0, shadowBuilds = 0;
// The shadow map's draws and CPU time (left out of the times around it); harness build, GPU time of the
// shadow pass and all passes (timestamp queries read a few frames later, summed over gpuFrames).
    std::uint64_t shadowDraws = 0;
    double shadowMs = 0.0;
    std::uint32_t gpuFrames = 0;
    double gpuShadowMs = 0.0, gpuFrameMs = 0.0;
// Meshes handed over with per-vertex offsets (an animation's morph).
    std::uint64_t morphMeshes = 0, morphCorners = 0;
};
FrameStats TakeFrameStats();

// Harness build: timers into FrameStats (deviceMeshMs, the decal / flat / shadow build times; `shadow`: an
// object shadow's decal, else a ground print's).
void BeginDeviceMeshTime();
void EndDeviceMeshTime();
void BeginDecalTime(bool shadow);
void EndDecalTime(int vertices);
void BeginFlatDrawTime(int vertices);
void EndFlatDrawTime();
void BeginShadowBuildTime();
void EndShadowBuildTime();

// The holder is destroyed: what the renderer kept for it forgotten.
void HolderGone(const void* holder);
// A holder loaded from an 8888 .mmp picture (the software cursor's images): the renderer keeps the
// picture's pixels from the file data.
void PictureFromMmp(const void* holder, const void* data);

// The device object is being built: the renderer starts (it presents through its own swap chain). When it cannot
// start the game ends here.
void StartPresenting();
// Default on; the harness turns it off under its virtual clock.
void SetWaitForVsync(bool on);
// The settings file's "VSync" (after StartPresenting, before the first present): off presents with sync
// interval 0 and, where supported, tearing. The harness build has no such switch.
bool VsyncSwitchSupported();
void SetVsync(bool on);

// An old-path mesh draw the game decided is visible, colours set: the mesh in model space and the matrix the
// old path projects it with. Every pass the old path draws between BeginMesh and EndMesh calls MeshPass,
// which takes the old device's state. A mesh the renderer refuses is lost (logged).
struct MeshDraw {
    const float* positions;            // blocks of four vertices: x[4], y[4], z[4], 4 unused
    int positionCount;
    const std::uint32_t* colours;      // D3DCOLOR, by the corners' colour index
    const std::uint32_t* speculars;
    const float* uvs;                  // (u, v) pairs, by the corners' texture index
    const std::uint16_t* corners;      // (position, colour, texture) index triples; null (the flat geometry
                                       // draw): vertex i is position i with uv i and colours[0] / speculars[0]
    int cornerCount;                   // without corners: the vertex count
    const std::uint16_t* indices;      // a triangle list over the corners
    int indexCount;
    float uOffset, vOffset;            // added to the texture coordinates when `moveUv`
    bool moveUv;
    const Matrix4* matrix;             // row-major, out = M * (x, y, z, 1)
    bool screenMatrix;                 // `matrix` gives screen pixels after the divide, else clip space
    bool clipped;                      // else only projected: the depth is not cut at the near / far planes
    bool distanceFog;
    float zScale;                      // 0 taken as 1
    bool coloursWritten;               // the draw set every colour to the device's (flag 4)
// A figure mesh lit on the GPU (MeshSource::lighting), else null: the normals (blocks of four as the
// positions) and the words ((position, normal) index pairs, by the corners' colour index).
    const float* normals;
    int normalCount;
    const std::uint16_t* words;
    int wordCount;
};

// A texture's .mmp file in memory: the levels from `mip` on that the game loads for `holder`, decoded and
// kept until the holder goes. False when not decoded (a format it does not read).
bool TextureFromMmp(const void* holder, const void* data, unsigned mip, unsigned levels);

// From now on mesh draws sampling `target` in their first stage take the rectangle `to` from `source`'s
// rectangle `from` (left, top, right, bottom in first-level texels); the target's texture is not changed.
// One per target; forgotten when either holder goes; nothing recorded for an empty or outside rectangle.
// The unit's face blink.
void TexturePatch(const void* target, const int to[4], const void* source, const int from[4]);

// How an image's pixels are stored (TextureImage).
enum class ImageLayout : std::uint8_t {
    Argb,     // 0xAARRGGBB with premultiplied alpha
    Pnt3,     // "PNT3": Argb in groups of four pixels, where a group's first pixel has alpha 0 and is not 0, that word
              // instead counts target bytes left as they are
    Rgb565,   // 16 bits 5:6:5, opaque (shot.sav's pictures)
};
// An image the game has drawn into a texture on the GPU (ComposeTexture, DrawIntoTexture): an .mmp file's pixels after
// its header and extra block, or another file's picture, rows from the top. What the renderer decodes of a file is
// shared by `name` while a texture holds it; an image without a name is decoded for its draw only.
struct TextureImage {
    const char* name;
    const void* pixels;
    std::uint32_t bytes;   // the pixels' bytes in the file
    int width, height;
    ImageLayout layout;
};
// The texture of `holder` composed on the GPU: `size` x `size` texels of transparent black, each image (`size`
// square) drawn over the last (premultiplied: what is there x (1 - the image's alpha) + the image), the levels
// below made from it; the texture shows the levels from `mip` on, `levels` of them (its size `size` >> `mip`).
// Kept until the holder goes, made again on a new device. False when not made (nothing shown).
bool ComposeTexture(const void* holder, int size, unsigned mip, unsigned levels, const TextureImage* images, int count);
// What a texture the game draws into (BlankTexture) keeps of the alpha drawn into it, and what it starts as.
enum class BlankAlpha : std::uint8_t {
    None,     // no alpha (a 5:6:5 texture's): opaque black, everything drawn into it opaque
    OneBit,   // only the alpha's top bit (a 1:5:5:5 texture's): transparent black
    Full,     // transparent black
};
// A texture of `holder` the game draws into on the GPU (DrawIntoTexture, DrawClouds): `width` x `height` texels; with
// `mipmapped` the levels below the first are made from it after each draw, else it has one level.
bool BlankTexture(const void* holder, int width, int height, BlankAlpha alpha, bool mipmapped);
// The rectangle `width` x `height` at `image`'s top-left put into the texture at (x, y), replacing what is there
// (clipped to the texture); without an image that rectangle is filled with opaque white.
void DrawIntoTexture(const void* holder, const TextureImage* image, int x, int y, int width, int height);
// One frame of the global map's clouds (DrawClouds): 256 x 256 grey levels of value noise summed from four 32 x 32
// layers, each made from three random words (CGlobalMapClouds, global_map_clouds.cpp).
struct CloudFrame {
    std::uint32_t words[4][3];   // each layer's a, b, c
};
// The whole 256 x 256 texture of `holder` (BlankTexture) made the clouds: each texel white with the alpha `ramp`[g],
// g = (frame `to`'s grey level x `blend` + `from`'s x (256 - `blend`)) / 256.
void DrawClouds(const void* holder, const CloudFrame& from, const CloudFrame& to, int blend, const std::uint8_t ramp[256]);

void BeginMesh(void* device, const MeshDraw& mesh);
void MeshPass(void* device);
void EndMesh();

// Pre-transformed vertices (FVF 0xC4, rhw taken as 1): the rectangle fill and outline. A triangle list over
// `indices`, or a line strip when `indices` is null. A draw it refuses is lost (logged).
struct ScreenVertex {
    float x, y, z, rhw;
    std::uint32_t colour, specular;
};
void ScreenDraw(void* device, const ScreenVertex* vertices, int count, const std::uint16_t* indices, int indexCount);

// The game's two layouts of pre-transformed vertices (the vertex format codes the game's draws pass): x, y, z, rhw, colour,
// specular, and in the textured one u, v.
inline constexpr int kFvfTextured = 0x1C4;
inline constexpr int kFvfUntextured = 0xC4;
// What TlDraw's vertices make.
enum class Primitive { LineStrip, TriangleList };

// A draw of pre-transformed vertices, fog already applied: `fvf` kFvfTextured or kFvfUntextured, indexed when
// `indices`. A draw it refuses is lost (logged).
void TlDraw(void* device, const void* vertices, int fvf, int vertexCount, const std::uint16_t* indices, int indexCount,
            Primitive primitive);

// A rectangle of the frame or of a surface, in pixels.
struct Rect {
    int left, top, right, bottom;
};

// A movie's decoded frame (R, G, B, X bytes), kept for `key` (the video object) until the next one.
void MovieFrame(const void* key, const std::uint8_t* rgbx, int width, int height);
void MovieBlit(const void* key, const Rect& to);

// The movie player's fill before its first frame (`colour`'s alpha ignored).
void Fill(const Rect& to, std::uint32_t colour);

// The software cursor: the part `from` of the cursor image with its mask (the image's full width, the
// surface's pixel format) at `to`; texels whose mask is not zero are left out.
void Cursor(const void* imageHolder, const void* mask, const Rect& from, const Rect& to);

// The software cursor between frames (renderer.md section 9): from the cursor thread, the cursor's lock
// held, the last presented frame is presented again with the cursor at `to` (Cursor must have drawn the
// image before). Only when the renderer presents itself and nothing was presented for 40 ms; false when
// nothing was presented. Thread-safe: recorded on a deferred context.
bool CursorBetweenFrames(const void* imageHolder, const void* mask, const Rect& from, const Rect& to);
// The cursor thread's presents and, in the verify build, the checks of the frame it presented.
struct CursorPresentInfo {
    std::uint32_t presents = 0;
    std::uint32_t calls = 0;
    std::uint32_t refused[4] = {};   // not presented: picture not drawn yet, no frame, a frame within 40 ms, outside
    Rect lastTo{0, 0, 0, 0};
    int checks = 0, mismatches = 0;
};
CursorPresentInfo CursorPresents();
// Verify build: the frame the cursor thread presented last (after the gamma ramp), 24-bit BGR rows from the
// top. False when there is none.
bool ReadCursorPresent(int* width, int* height, std::vector<std::uint8_t>* bgr);

// The game clears the depth buffer mid-frame (before the interface's panels and 3D previews); `rects`
// null: all of it.
void ClearDepth(float z, const Rect* rects, int count);

// Every sector page the game draws; `renderer`'s transform pipeline holds the current clip matrix; `page`:
// ground pages 0..pages+1, water pages after them.
void TerrainPage(const terrain::SectorData* sector, void* renderer, int page);
// What the sector's pages are built from changed: Rebuilt, its tile lists and records; Moved, vertices or
// texture coordinates (animated tiles); WaterMoved, only the water layer's positions.
enum class TerrainChange { Rebuilt, Moved, WaterMoved };
void TerrainChanged(const terrain::SectorData* sector, TerrainChange change);
// Harness build only ("terraincache"): pages kept on the GPU (on, the default) or built every frame (off,
// for frame comparisons). Every 16th frame the kept pages are checked against a fresh build; `stale` counts
// a change TerrainChanged was not told of.
void SetTerrainCache(bool on);
struct TerrainCacheInfo {
    bool on = true;
    std::size_t pages = 0;
    std::uint64_t checks = 0, stale = 0;
};
TerrainCacheInfo TerrainCacheState();

// A figure part lit on the GPU as 3DfpFPU's LightMesh lit its words (renderer.md section 13). Lights
// and sun are taken into the part's model space; `crown`: the colours pulled 0.6 of the way to the part's
// average (the flora's "crown" meshes).
struct MeshLightSource {
    Vector3 position;              // the landscape's space
    float inverseRangeSquared;
    float colour[3];
};
struct MeshLighting {
    Matrix4 model;                 // the part's model space to the landscape's (row-major); with `camera`, the
                                   // camera times that
    const Matrix4* camera;         // the scene's camera (worlddata::SceneCamera), else null
    bool sun;
    Vector3 sunDirection;          // the direction its light travels
    float sunColour[3];
    float ambientColour[3];        // CSceneRenderer::SetColour's last
    float diffuseColour[4];        // the material's
    float emissiveColour[3];
    float coefficient;             // the options' objects lighting coefficient (CLight::SetLitColors)
    bool crown;
    const MeshLightSource* lights;   // the lights within the object's reach (its draw's list)
    int lightCount;
};

// The figure mesh the next mesh draws draw, set by the figure part draws around the device's draw. The
// renderer keeps its corners on the GPU under `owner` and rebuilds only what changed: the colours when the
// draw wrote them, the positions on MeshChanged. A mesh draw without one is built every frame.
struct MeshSource {
    const void* owner;     // the figure mesh (CFigureMesh) drawn
    const void* shared;    // the link figure (CLinkMesh) whose part it draws, else null
    bool moved;            // the positions handed over are a copy with the per-vertex offsets added
    bool coloursWritten;   // this draw wrote the mesh's colour arrays (lit, flat colours)
    const MeshLighting* lighting = nullptr;   // lit on the GPU (the draw's colours: only their alpha)
};
void SetMeshSource(const MeshSource* source);
// A figure mesh or link figure changed in place: Positions (per-vertex offsets, a part morphed again),
// Colours outside its draw (the sky's lighting; a link figure's part lit or filled), Rebuilt (loaded,
// cleared, mirrored, a part added or removed, destroyed).
enum class MeshChange { Positions, Colours, Rebuilt };
void MeshChanged(const void* object, MeshChange change);
// Mesh draws between SetInterfaceDraw(true) and (false) (CUiMesh::Draw) are built every frame: the interface
// rewrites its sprites' corners in place in many places.
void SetInterfaceDraw(bool on);
// Harness build only ("meshcache"): as SetTerrainCache, for figure meshes.
void SetMeshCache(bool on);
struct MeshCacheInfo {
    bool on = true;
    std::size_t meshes = 0;
    std::uint64_t checks = 0, stale = 0;
};
MeshCacheInfo MeshCacheState();

// Draws what was handed over since the last present and presents it. Once per frame; captures call it
// before reading.
void EndFrame();

// EndFrame if not done yet, then a new frame begins.
void Present();

// The last frame EndFrame drew, 24-bit BGR rows from the top. False without a frame.
bool ReadFrame(int* width, int* height, std::vector<std::uint8_t>* bgr);

// The display's gamma ramp (three 256-entry ramps, full scale 65280; null channels identity), applied as
// the frame goes to the window; captures read the frame before it.
void SetGammaRamp(const std::uint16_t* red, const std::uint16_t* green, const std::uint16_t* blue);
// Frames presented on the game window; the verify build's checks of the presented frame against the gamma
// ramp (after each SetGammaRamp, at a grid of points, when the frame shows one to one).
struct WindowPresentInfo {
    std::uint32_t presents = 0;
    int rampChecks = 0, rampPoints = 0, rampDiffering = 0;
    int cursorChecks = 0, cursorMismatches = 0;   // verify build: the cursor the present draws, checked in the back buffer
    bool rampCheckPending = false;
    bool rampIdentity = true;
    int deviceLosses = 0;       // devices lost (or LoseDevice) ...
    int deviceRecoveries = 0;   // ... and made again with everything on them
    std::uint32_t presentsAtRecovery = 0;   // `presents` when the last new device was made
    long lastLossResult = 0;   // the last loss: the DXGI error that reported it ...
    long lastLossReason = 0;   // ... and GetDeviceRemovedReason then (S_OK: LoseDevice's, not a real loss)
    bool lastLossSimulated = false;
    bool removable = false;   // harness build: RemoveDevice can remove the device
    int removals = 0;
};
WindowPresentInfo WindowPresents();
// Acts as if the device were lost at the next present ("losedevice"): Present is not called but reported
// DXGI_ERROR_DEVICE_REMOVED; then, as for a real loss, everything is made again and textures uploaded again
// from what they were decoded from.
void LoseDevice();
// Harness build only (EI_RENDER "removable"): the device was made through D3D11On12, and this removes the
// Direct3D 12 device for real (ID3D12Device5::RemoveDevice, this process only) before the next present.
// False when the device is not removable.
bool RemoveDevice();
// Harness build only ("terrainlevels"): what the ground pages draw. Levels: the texture level the page's
// sampler reads at each pixel (red = level * 32, mips blended as the sampler blends them; green 0x5A and blue
// 0xA5 mark the pixel). PointMips: the same through the old sampler (bilinear, point mips: the level steps
// along the tiles' edges that showed as seams). Off: the pages' colours.
enum class TerrainLevelMode { Off, Levels, PointMips };
void TerrainLevels(TerrainLevelMode mode);
// Harness build only ("mmplevels"): how far the held .mmp textures' levels lie from their level 0, both
// box-filtered to 4 x 4: the mean absolute difference per channel (0..255), the largest over all levels.
// Levels decoded from the wrong bytes (bright green strips over the trees) lie far off.
struct MmpLevelCheck {
    int textures = 0, levels = 0;
    double worst = 0.0;
    int worstWidth = 0, worstHeight = 0, worstLevel = 0;   // that texture's first level's size, the level
    std::uint32_t worstFormat = 0;                         // its .mmp format id
};
MmpLevelCheck CheckMmpLevels();
// Where the frame shows in the window's client area (aspect kept, centred). False when the renderer does
// not present itself.
bool FrameInWindow(int* frameWidth, int* frameHeight, Rect* placed);
// Client-area point to frame pixel (outside the frame: beyond its edges) and back (the centre of the frame
// pixel's window pixels).
void WindowToFrame(int* x, int* y);
void FrameToWindow(int* x, int* y);

} // namespace render
