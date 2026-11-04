#pragma once

// Internals shared by the renderer's files.

#include "render.h"
#include "render/gpu.h"

#include "lib/vecmath.h"

// The programs' uniform blocks, slots and vertex inputs (CMakeLists.txt, tools/gen_gpu_shaders.py).
#include "shaders/gpu_shaders.h"

#include <cstdint>
#include <memory>
#include <utility>
#include <span>
#include <tuple>
#include <vector>

class TextureDevice;

namespace render::text {
class GlyphAtlas;
} // namespace render::text

namespace render::detail {

using gpu_shaders::BlitParams;
using gpu_shaders::CloudParams;
using gpu_shaders::MeshFsParams;
using gpu_shaders::MeshLightParams;
using gpu_shaders::MeshVsParams;
using gpu_shaders::OcclusionBlurParams;
using gpu_shaders::OcclusionParams;
using gpu_shaders::PerfOverlayParams;
using gpu_shaders::PointShadowParams;
using gpu_shaders::PresentParams;
using gpu_shaders::RectParams;
using gpu_shaders::SceneLightParams;
using gpu_shaders::ShadowDilateParams;
using gpu_shaders::ShadowParams;
using gpu_shaders::ShadowTerrainFsParams;
using gpu_shaders::ShadowTerrainVsParams;
using gpu_shaders::SolidParams;
using gpu_shaders::TerrainFsParams;
using gpu_shaders::TerrainVsParams;
using gpu_shaders::TextureComposeParams;
using gpu_shaders::UiParams;

// The game's transform pipeline matrices are Matrix4: row-major, out = M * (x, y, z, 1), Direct3D clip
// space (-w <= x, y <= w, 0 <= z <= w).

// A vertex: world (terrain) or model (meshes) position.
struct Vertex {
    Vector3 position;
    std::uint32_t colour;     // D3DCOLOR (bytes B, G, R, A)
    std::uint32_t specular;   // D3DCOLOR; its alpha is the fog factor (255 no fog)
    float u, v;
    Vector3 normal;           // model space (a figure mesh lit on the GPU), else zero
};
static_assert(sizeof(Vertex) == 40);

// One texture stage of a pass (render_state.h's values as numbers).
struct StageState {
    std::uint8_t colourOp = 1, colourArg1 = 2, colourArg2 = 1;   // D3DTOP_DISABLE, TEXTURE, CURRENT
    std::uint8_t alphaOp = 1, alphaArg1 = 2, alphaArg2 = 1;
    std::uint8_t magFilter = 2, minFilter = 2, mipFilter = 1;     // D3DTFG / D3DTFN / D3DTFP
    std::uint8_t addressU = 1, addressV = 1;                      // D3DTADDRESS_WRAP
    gpu::Texture texture;                                         // the bound surface, read at the pass
    bool hasTexture = false;
};

// render::TexturePatch in stage 0's texture coordinates (wrapped into 0..1): inside `to` the shader samples
// `texture` at uv * map.xy + map.zw, kept inside `inside` (the source rectangle less half a texel). All
// zero: none.
struct StagePatch {
    gpu::Texture texture;
    float to[4] = {};       // u0, v0, u1, v1
    float map[4] = {};
    float inside[4] = {};   // u0, v0, u1, v1
};
StagePatch PatchOf(const void* holder);

// The render state of one pass of a mesh draw. Blend, depth and culling make the pipeline; the rest
// goes to the shader.
struct PassState {
    bool blend = false;
    std::uint8_t sourceBlend = 2, destBlend = 1;   // D3DBLEND
    bool depthTest = true, depthWrite = true;
    std::uint8_t depthFunc = 4;                    // D3DCMP_LESSEQUAL
    std::uint8_t cull = 1;                         // D3DCULL: 1 none, 2 clockwise, 3 counter-clockwise
    bool alphaTest = false;
    std::uint8_t alphaFunc = 8, alphaReference = 0;
    bool specular = false;
    bool fog = false;
    std::uint32_t fogColour = 0;                   // D3DCOLOR
    std::uint32_t textureFactor = 0xFFFFFFFF;
    StageState stages[2];
    StagePatch patch;

    std::uint32_t PipelineKey() const {
        return (blend ? 1u : 0u) | (static_cast<std::uint32_t>(sourceBlend & 15) << 1) |
               (static_cast<std::uint32_t>(destBlend & 15) << 5) | (depthTest ? 1u << 9 : 0u) |
               (depthWrite ? 1u << 10 : 0u) | (static_cast<std::uint32_t>(depthFunc & 15) << 11) |
               (static_cast<std::uint32_t>(cull & 3) << 15);
    }
};

// The render state the game set for the pass about to be drawn (render_state.h). `device`'s bound surfaces
// become the stages' textures; null: no textures.
PassState ReadPassState(const TextureDevice* device);
void ApplyPassState(gpu::PipelineDesc& p, const PassState& s);

// A terrain page kept on the GPU and one quarter of it drawn in a frame.
struct CachedPage;
struct TerrainPiece {
    CachedPage* page;
    int quarter;
};

struct TerrainBatch {
    const void* texture;      // the game's Texture
    bool water;               // the water pass (alpha from the vertex colour)
    int matrix;               // index into Frame::matrices
    int firstPiece;           // into Frame::terrainPieces
    int pieceCount;
    PassState state;          // its textures not read
    bool occluded = false;    // the ground of the world: it takes the ambient occlusion (DrawOcclusion)
};

struct CachedMesh;

constexpr float kNoFront = 1e30f;
struct MeshBatch {
    int matrix;
    int firstIndex;           // into the frame's mesh stream, or (cached) the mesh's own indices
    int indexCount;
    PassState state;
    bool distanceFog;
    float fogNear;            // the pipeline's fog start
    float fogScale;           // 255 / (end - start)
    bool unclipped;           // the old path projected it without clipping
    float zScale;             // the depth is multiplied by it (1: unchanged)
    bool lines;               // a line list (else triangles)
    bool model = false;       // a model's mesh (BeginMesh .. MeshPass), not pre-transformed vertices
    bool flat = false;        // the flat geometry (decals, markers), no corners
    const void* caster = nullptr;   // render::BeginCaster's object, if any
    int light = -1;           // index into Frame::shadow.lights: it samples the shadow map
// A unit's mesh: its corners' box in model space (low xyz, high xyz), and the nearest map depth of its
// object's boxes (PrepareShadows; above 1: none).
    bool figure = false;
    float box[6] = {};
    float front = 2.0f;
// A caster's mesh has its box too (`boxed`); a mesh lit on the GPU gets the index of its model matrix into
// the landscape's space (PointShadowFrame::models, -1: none) and, a unit's, its object's nearest distance
// to each point light with a map (kNoFront: not a unit).
    bool boxed = false;
    int landscape = -1;
    float pointFront[4] = {kNoFront, kNoFront, kNoFront, kNoFront};
    CachedMesh* cached = nullptr;   // the mesh kept on the GPU it draws, else the frame's stream
    float uOffset = 0.0f, vOffset = 0.0f;   // added to a cached mesh's texture coordinates (MeshDraw::moveUv)
    int lighting = -1;        // index into Frame::meshLights: lit on the GPU, else the vertex colours
    bool occluded = false;    // a world mesh lit on the GPU: it takes the ambient occlusion (DrawOcclusion)
};

// The image is read at the blit.
struct BlitBatch {
    gpu::Texture texture;
    int textureWidth, textureHeight;
    Rect destination;
    Rect source;
};

struct ClearBatch {
    float z;
    Rect rect;                // screen pixels; right 0: the whole frame
};

// render::Fill.
struct FillBatch {
    Rect rect;                // screen pixels
    std::uint32_t colour;     // D3DCOLOR, alpha ignored
};

// The interface's quads (render::ui::Solid / Glyphs): a corner in frame pixels, its atlas texels (u < 0: full
// coverage) and its colour's bytes R, G, B, A.
struct UiVertex {
    float x, y, u, v;
    std::uint32_t colour;
};
// A run of the frame's uiVertices drawn with one atlas (null: solids only), scissored to `scissor`.
struct UiBatch {
    std::uint32_t first, count;
    const text::GlyphAtlas* atlas;
    Rect scissor;
};
// render::ui::DrawPicture.
struct PictureBatch {
    gpu::Texture texture;
    int width, height;
    Rect destination;
    bool grey;
};

struct Command {
    enum Kind : std::uint8_t { Terrain, Mesh, Blit, ClearDepth, Fill, Ui, Picture };
    Kind kind;
    int index;                // into terrainBatches / meshBatches / blitBatches / clears / fills / uiBatches / pictureBatches
};

// The scene's light as kept by render::SceneLighting, for the terrain shader (shaders/scene_lighting.hlsli), with
// the frame's point shadows and the shadow map's light direction.
static_assert(std::size(SceneLightParams{}.pointAt) == kMostSceneLights);
void LightUniforms(SceneLightParams* params);
// The kept point lights (`counts[0]` of them in pointAt / pointColour). SceneLightIndex: the kept light at
// `position` exactly (the same Light's), -1 none.
const SceneLightParams& KeptSceneLight();
// The client's time the scene's light was handed over with (SceneLights::clientTicks).
double KeptClientTicks();
// The kept light `k` casts point shadows (ScenePointLight::shadows: not an object's own light).
bool SceneLightCastsShadows(int k);
int SceneLightIndex(const Vector3& position);
// The shadow map's light for the sun `sun`: at least 20 degrees above the horizon, unit. False without a
// direction. Receivers lit on the GPU weigh the map's darkening by their facing to it.
bool MapLightDirection(const Vector3& sun, Vector3* travel);
// `placed` (camera x model) without the camera. False when the camera has no inverse.
bool UnderCamera(const Matrix4& camera, const Matrix4& placed, Matrix4* model);
// `m`'s inverse (worked in double); false when it has none.
bool InvertMatrix(const Matrix4& m, Matrix4* inverse);
// a x b, both row-major.
Matrix4 Product(const Matrix4& a, const Matrix4& b);
// A figure part lit on the GPU (shaders/mesh.hlsl's MeshLightParams): the sun and lights in the part's model
// space, their colours times the material's diffuse colour and the coefficient; the kMostMeshLights nearest the
// part's origin. A light's pointColour w: the scene's light (SceneLightIndex) + 1, 0 none; drawn: its point shadow
// + 1, 0 none.
constexpr int kMostMeshLights = 8;
static_assert(std::size(MeshLightParams{}.pointAt) == kMostMeshLights);
void MeshLightUniforms(const MeshLighting& lighting, MeshLightParams* params);

// Point-light shadows (shaders/point_shadow.hlsli): the receivers' fragment uniforms, the same for the frame. Each
// light with a map has six faces of kPointFaceTexels in one row of the atlas.
constexpr int kMostPointShadows = 4;
constexpr int kPointFaceTexels = 512;
static_assert(std::size(PointShadowParams{}.lights) == kMostPointShadows);
struct PointShadowFrame {
    std::vector<Matrix4> models;   // MeshBatch::landscape: model space to the landscape's
    int sceneSlots[kMostSceneLights] = {};   // per kept scene light its map + 1, 0 none
    PointShadowParams params{};
};

// A frame's object shadows: the sun and casters handed over, and what PrepareShadows made of them.
struct ShadowFrame {
    bool haveSun = false;
    Vector3 sun{};
    std::vector<std::pair<const void*, float>> casters;   // object, strength (render::CastsShadow)
    bool prepared = false;
    bool on = false;          // the map is made this frame: receivers sample it
    bool haveClouds = false;  // render::CloudShadows handed over
    CloudWeather weather{};
    bool clouds = false;      // the cloud shadows are shown this frame (`lights` made for them too)
    std::vector<Matrix4> lights;   // each receiving mesh's model space to the map's clip space; [0]: the terrain's
    ShadowParams params{};
    ShadowParams cloudParams{};    // the clouds alone (on[0] 0): for the water, and when no map is drawn
    PointShadowFrame point;
};

struct Frame {
    std::vector<Matrix4> matrices;
    std::vector<TerrainPiece> terrainPieces;
    std::vector<TerrainBatch> terrainBatches;
    std::vector<Vertex> meshVertices;
    std::vector<std::uint32_t> meshIndices;
    std::vector<MeshBatch> meshBatches;
    std::vector<MeshLightParams> meshLights;   // MeshBatch::lighting
    std::vector<BlitBatch> blitBatches;
    std::vector<ClearBatch> clears;
    std::vector<FillBatch> fills;
    std::vector<UiVertex> uiVertices;
    std::vector<UiBatch> uiBatches;
    std::vector<PictureBatch> pictureBatches;
    std::vector<Command> commands;
// The world view's commands (render::BeginWorld .. EndWorld), the last range open (end SIZE_MAX) while the
// world view draws. The render quality draws them multisampled and their models anisotropically.
    struct Range {
        std::size_t first, end;
    };
    std::vector<Range> worldRanges;
    bool haveFog = false;
    bool fogEnabled = false;
    std::uint32_t fogColour = 0;   // D3DCOLOR
// Direct3D 7 samples a pixel at its integer screen coordinates, today's GPUs half a pixel further right and
// down: the 3D draws move their clip positions by half a pixel (x by +1 / width, y by -1 / height, times w)
// so the same screen coordinates get the same pixels, texture coordinates and colours. Set by EndFrame.
    float pixelShift[2] = {0.0f, 0.0f};
    ShadowFrame shadow;
};
extern Frame g_frame;   // the frame being built
std::uint32_t FrameNumber();
// Counts the frames' submissions (gpu::Submit at the frame's end).
std::uint32_t CommitNumber();

// Added when it differs from the last one.
int AddMatrix(const Matrix4& m);
// The game's fog switch and colour (render_state.h), read once per frame.
void ReadFog(Frame& f);
TextureDevice* DeviceObject();
bool GameTargetSize(int* width, int* height);
// The device is started: the device and the pipelines made at the first use (a failure ends the game: Fatal).
void Start();
// The renderer cannot go on (no device, no swap chain): the reason is logged and shown in the game's error box, and
// the game ends (exit -1), Rebirth's DisableExitOnCrashReport or not. There is no other way to draw: no fallback.
[[noreturn]] void Fatal(const char* format, ...);

// `r` as a shader's float4: left, top, right, bottom.
inline void StoreRect(const Rect& r, float (&out)[4]) {
    out[0] = static_cast<float>(r.left);
    out[1] = static_cast<float>(r.top);
    out[2] = static_cast<float>(r.right);
    out[3] = static_cast<float>(r.bottom);
}

// `r` on a `width` x `height` target as the rectangle programs' vertex uniforms (shaders/rect_vs.hlsli): block 0
// of every one of them.
static_assert(gpu_shaders::blit::kBlockRectParams == 0 && gpu_shaders::ui_picture::kBlockRectParams == 0 &&
              gpu_shaders::ui_quad::kBlockRectParams == 0 &&
              gpu_shaders::texture_compose::kBlockRectParams == 0 && gpu_shaders::texture_mip::kBlockRectParams == 0);
inline void ApplyRect(const Rect& r, int width, int height) {
    RectParams vs{{}, {static_cast<float>(width), static_cast<float>(height), 0.0f, 0.0f}};
    StoreRect(r, vs.rect);
    gpu::PushUniforms(gpu::Stage::Vertex, 0, vs);
}

// The top `bits` of a channel value widened to 8 by repeating them (more than 8: the top 8 of them).
inline std::uint32_t WidenTop(std::uint32_t top, int bits) {
    if (bits <= 0) return 0;
    if (bits >= 8) return (top >> (bits - 8)) & 0xFF;
    std::uint32_t out = 0;
    for (int filled = 0; filled < 8; filled += bits) out |= (top << (8 - bits)) >> filled;
    return out & 0xFF;
}

// A pixel format's channel: its mask, the shift of the mask's lowest set bit and the run of set bits from there.
struct Channel {
    std::uint32_t mask = 0;
    int shift = 0;
    int bits = 0;
    explicit Channel(std::uint32_t m) : mask(m) {
        if (!m) return;
        while (!((m >> shift) & 1)) ++shift;
        while (shift + bits < 32 && ((m >> (shift + bits)) & 1)) ++bits;
    }
    // The channel of `pixel` widened to 8 bits; 0 for an empty mask.
    std::uint32_t Widen(std::uint32_t pixel) const { return WidenTop((pixel & mask) >> shift, bits); }
};

const gpu::Texture& FrameTexture();
bool FrameSize(int* width, int* height);
// The frame into the swap chain's texture through the gamma ramp; PresentWindow, after the frame's submission (which
// presents it), counts it. False: nothing to present into / nothing presented.
bool DrawToWindow();
bool PresentWindow();
// The gamma ramp the present applies (256 RGBA8 entries) and how often SetGammaRamp changed it.
const std::uint8_t* GammaRamp();
std::uint32_t GammaRampChanges();
#if defined(EI_RENDER_VALIDATION)
// Whether the verify build's GPU read-backs (a stall each) run for the `count`th of their kind: the first
// kReadBackAlways, then every 16th, and every one with "checkall" in EI_RENDER.
constexpr long kReadBackAlways = 64;
bool ReadBackDue(long count);
#endif

// Appended to between passes (a frame drawn in several passes appends after what it drew from), written from its
// start again each frame; replaced by a larger one when an append does not fit. Append: the bytes' offset in Buffer()
// until the next Append (which may replace it), -1 when they could not be written.
class StreamBuffer {
public:
    StreamBuffer(bool newIndex, const char* newLabel) : index(newIndex), label(newLabel) {}
    int Append(const void* data, std::size_t bytes);
    const gpu::Buffer& Buffer() const { return buffer; }

private:
    bool index;
    const char* label;
    gpu::Buffer buffer;
    std::size_t used = 0;                 // appended since the last commit
    std::uint32_t commit = 0xFFFFFFFFu;   // CommitNumber() of the last append
};

// The buffers a draw reads its corners from: vertex buffers 0.. and the 32-bit indices.
struct Geometry {
    gpu::BufferBinding vertices[2];
    int vertexBuffers = 0;
    gpu::BufferBinding indices;
    void Bind() const;
};

// A frame's vertices and absolute indices on the GPU, appended again when they grew since the frame's last
// pass.
class GeometryStream {
public:
    // `terrain`: counted as the terrain's uploads (FrameStats), else the meshes'.
    GeometryStream(const char* label, bool newTerrain) : vertices(false, label), indices(true, label), terrain(newTerrain) {}
    bool Upload(const std::vector<Vertex>& vertices, const std::vector<std::uint32_t>& indices);
    bool Bind(Geometry& g) const;

private:
    StreamBuffer vertices, indices;
    bool terrain;
    std::uint32_t frame = 0xFFFFFFFFu;
    std::size_t vertexCount = 0, indexCount = 0;
    int vertexOffset = -1, indexOffset = -1;
};

struct PoolPlace {
    std::uint32_t generation = 0;   // the pool's when placed (0: never)
    std::uint32_t firstVertex = 0, vertexRoom = 0, firstIndex = 0, indexRoom = 0;
};

// Vertices and indices kept on the GPU across frames (terrain pages, meshes): a geometry stream, a colour
// stream (kColourPairBytes a vertex) and absolute 32-bit indices, in buffers written in place, only the changed
// ranges. Items are placed one after the other; when one does not fit, the pool starts again (Restart) with the
// items still needed, larger when they need more than half.
constexpr std::size_t kColourPairBytes = 2 * sizeof(std::uint32_t);
class GpuPool {
public:
    GpuPool(std::size_t newGeometryStride, std::uint32_t newLeastVertices, std::uint32_t newLeastIndices, const char* newLabel)
        : geometryStride(newGeometryStride), leastVertices(newLeastVertices), leastIndices(newLeastIndices), label(newLabel) {}
// Keeps the place when the item still fits there, else places it at the end (`anew`: all of it to write).
// False when the pool is full.
    bool Place(PoolPlace& p, std::uint32_t vertices, std::uint32_t indices, bool* anew);
    bool Placed(const PoolPlace& p) const { return p.generation == generation && geometry.buffer; }
    void Restart(std::uint64_t vertices, std::uint64_t indices);
    void WriteGeometry(const PoolPlace& p, std::uint32_t first, std::uint32_t count, const void* data) const;
    void WriteColours(const PoolPlace& p, std::uint32_t first, std::uint32_t count, const std::uint32_t* pairs) const;
    void WriteIndices(const PoolPlace& p, const std::uint32_t* relative, std::size_t count);
    void Bind(Geometry& g) const;
    void Drop();

private:
    struct Buffer {
        gpu::Buffer buffer;
        bool Make(std::size_t bytes, bool index, const char* label);
        void Write(std::size_t offset, const void* data, std::size_t bytes) const;
        void Drop();   // kept by the GPU until the commands using it are done
    };
    std::size_t geometryStride;
    std::uint32_t leastVertices, leastIndices;
    const char* label;
    Buffer geometry, colours, indices;
    std::uint32_t vertexCapacity = 0, indexCapacity = 0;
    std::uint32_t usedVertices = 0, usedIndices = 0;
    std::uint32_t generation = 1;
    std::vector<std::uint32_t> absolute;    // scratch: an item's indices made absolute
};

// The terrain, inside EndFrame's off-screen pass. For the shadow map's fit: TerrainPieceGroups, a piece's
// vertices in runs (TerrainGroup: kept with the page, made when its vertices are built or moved), and
// TerrainPieceWhole, all of them as one; TerrainGroupPositions, every `step`th position of a run. TerrainFrameDone, after a present, drops pages not
// drawn for a long time.
struct TerrainGroup {
    Vector3 low, high;              // the box of its positions in the landscape's space
    std::uint32_t first, count;     // its vertices in the page's
};
bool CreateTerrainShader();
// The water's ripple map (render_water.cpp), made at the start (false: it could not be), bound with every terrain
// batch; RippleOffsets, its two layers' offsets at the client time (TerrainFsParams::ripples, slowRipples).
bool MakeRippleMap();
gpu::TextureBinding RippleMap();
void RippleOffsets(float water[4], float swamp[4]);
void UploadTerrain(const Frame& frame);
void DrawTerrainBatch(const Frame& frame, const TerrainBatch& batch);
std::span<const TerrainGroup> TerrainPieceGroups(const TerrainPiece& piece);
const TerrainGroup& TerrainPieceWhole(const TerrainPiece& piece);
void TerrainGroupPositions(const TerrainPiece& piece, const TerrainGroup& group, std::uint32_t step,
                           std::vector<Vector3>* out);
void TerrainFrameDone();
// The ambient occlusion's depth (DrawOcclusion): the ground pages of the frame's batches marked `occluded` drawn,
// depth only, with TerrainDepthDesc's pipeline; the draws made. GroundOcclusionCounts: the sectors whose ground
// occlusion is kept, and how often one was worked out (render::OcclusionInfo).
gpu::PipelineDesc TerrainDepthDesc();
int DrawTerrainDepth(const Frame& frame, gpu::Pipeline pipeline);
void GroundOcclusionCounts(int* sectors, int* made);
// The ground's casters (render::ShadowTerrainSector). DrawTerrainCasters draws with `pipeline` (vertex buffer
// 0: TerrainCasterVertex, 32-bit indices) whose uniforms the caller applied.
struct TerrainCasterVertex {
    Vector3 position;
    Vector3 normal;
};
static_assert(sizeof(TerrainCasterVertex) == 24);
std::size_t TerrainCasterCount();
void UploadTerrainCasters();
int DrawTerrainCasters(gpu::Pipeline pipeline);
void TerrainCastersDone();

bool CreateMeshShader();
void UploadMeshes(const Frame& frame);
// `anisotropy` above 1: the stages sampled anisotropically at that level (AnisotropicSampler).
void DrawMeshBatch(const Frame& frame, const MeshBatch& batch, int anisotropy);

// The buffers a mesh batch draws from (the frame's stream, or the kept meshes' pool) and its first index;
// false when it has none. MeshLayout `positionsOnly`: position and texture coordinates alone (the casters).
bool BindMeshBatch(const MeshBatch& batch, Geometry& g, int* firstIndex);
void MeshLayout(gpu::PipelineDesc& p, bool cached, bool positionsOnly);

// Meshes kept on the GPU (docs/notes/renderer.md section 13). KeepMesh: the mesh being handed over brought up
// to date as kept for render::SetMeshSource's figure mesh, or null: it goes into the frame's stream (no source,
// no corners, the cache off, or the mesh already drawn this frame with other data). MeshCacheFrameDone drops
// meshes not drawn for a long time.
constexpr std::size_t kCachedVertexBytes = 32;   // a kept corner's geometry: position, texture coordinates, normal
void NoteMeshDraw(const MeshDraw& mesh);
CachedMesh* KeepMesh(const MeshDraw& mesh);
int CachedIndexCount(const CachedMesh& mesh);
int CachedVertexCount(const CachedMesh& mesh);
// The box of a mesh's positions in model space (low xyz, high xyz); false without positions. CachedMeshBox:
// the kept mesh's, taken again only when its positions changed (`mesh` is the draw KeepMesh kept it from).
bool PositionBox(const MeshDraw& mesh, float box[6]);
void CachedMeshBox(CachedMesh& kept, const MeshDraw& mesh, float box[6]);
// A caster outside the view (render::ShadowCasterMesh) kept by its part as KeepMesh keeps a drawn mesh (its
// positions and texture coordinates without the offsets, which the caster pass adds; no colours), read once
// a frame; null when a corner is out of range or nothing is left to draw.
CachedMesh* KeepCasterMesh(const ShadowMesh& mesh);
// Changes whenever the kept mesh's corners (positions, texture coordinates) or triangles change; no two kept
// meshes ever share one (render_point_shadows.cpp keeps faces drawn from them).
std::uint32_t CachedMeshVersion(const CachedMesh& mesh);
void UploadCachedMeshes(const Frame& frame);
bool BindCachedMesh(const CachedMesh& mesh, Geometry& g, int* firstIndex);
void MeshCacheFrameDone();
const void* MeshSourceOwner();
const MeshLighting* MeshSourceLighting();
// The model-space normal of the corners with colour index `index`, zero without normals or words.
Vector3 CornerNormal(const MeshDraw& mesh, int index);

// Object shadows (docs/notes/renderer.md section 13). PrepareShadows, once a frame before its first pass on
// the world: fits the map around the visible ground seen from the sun and gives every receiving batch its
// light matrix; DrawShadowMap draws the casters and the depth range per cell. Receivers include
// shaders/shadow_receiver.hlsli, take its ShadowParams (ShadowUniforms: all zero, no shadow, unless `receives`) and
// bind the map (or a cleared 1 x 1 one) at `mapSlot` and its depth range per cell after it with BindShadowMap.
void PrepareShadows(Frame& frame);
void DrawShadowMap(Frame& frame);
const ShadowParams& ShadowUniforms(const Frame& frame, bool receives, bool clouded);
void BindShadowMap(int mapSlot);
// The cloud shadows (render_cloud_shadows.cpp, shaders/cloud_shadow.hlsli), in the receivers' ShadowParams: a batch
// with a light matrix takes them (`clouded` in ShadowUniforms), the water too. PrepareClouds, from PrepareShadows
// once a frame outside a pass: the cloud map made at the first, the drift and the cover moved on by the world's time;
// returns how much of the sun a full cloud hides this frame (0: no clouds). CloudUniforms: their part of `params`
// for the map's fit (the landscape's x0, y0 at its clip space's -1, its extent). BindCloudMap: the map at `slot`.
float PrepareClouds(const ShadowFrame& shadow);
float CloudCover();
void CloudUniforms(float x0, float y0, float extent, float light, ShadowParams* params);
void BindCloudMap(int slot);
// How far along its normal a receiver's point is moved before it is looked up in the map: 1.5 of the map's texels,
// in world units (0 without the map).
inline float ShadowNormalOffset(const ShadowParams& shadow) {
    if (!(shadow.on[0] > 0.5f && shadow.soft[3] > 0.0f)) return 0.0f;
    return shadow.scale[0] * 2.0f / shadow.soft[3] * 1.5f;
}
// The clip matrix of the frame's first ground batch and the camera in the landscape's space; false without
// ground drawn.
bool WorldCamera(const Frame& frame, Matrix4* clip, Vector3* eye);
// NoShadowTexture: 1 x 1 at the far plane, for a receiver drawn without a map.
gpu::Texture MakeShadowDepth(int width, int height, const char* label);
gpu::Sampler ShadowCompareSampler();
const gpu::Texture& NoShadowTexture();
// The casters' draws into a depth map (shaders/shadow_caster.hlsl), the sun's and the point lights'. Each draw's
// own data (CasterInstance) goes into one instance buffer uploaded once a pass and is read at the draw's first
// instance; the draws are sorted by what they bind (a depth map does not depend on the order its casters are
// drawn in) and the pipeline and bindings are applied only when they change.
struct CasterInstance {
    Matrix4 light;       // model space -> the map's clip space
    float uvOffset[2];   // a kept mesh's texture offsets (MeshBatch::uOffset, vOffset)
    float alpha[2];      // cut out (1) where stage 0's alpha is below [1]
};
static_assert(sizeof(CasterInstance) == 80, "the instance layout in CasterDraws' pipelines");
class CasterDraws {
public:
    explicit CasterDraws(const char* label) : instances(false, label) {}
    // A draw of `indexCount` indices from `firstIndex` in the buffers of `geometry` (BindMeshBatch, BindCachedMesh;
    // `kept`: the kept meshes' pool, MeshLayout's `cached`).
    void Add(const Geometry& geometry, bool kept, const gpu::Texture& texture, gpu::Sampler sampler, int firstIndex,
             int indexCount, const CasterInstance& instance);
    std::size_t Count() const { return draws.size(); }
    // Sorts the draws in [from, to) by what they bind.
    void Sort(std::size_t from, std::size_t to);
    // Uploads the instances, before the pass (whose first Draw applies the pipeline and bindings anew); false when
    // they could not be (nothing is drawn).
    bool Upload();
    void Draw(std::size_t from, std::size_t to);
    void Clear();

private:
    struct Entry {
        gpu::BufferBinding vertices, indices;   // the positions (MeshLayout's buffer 0; the colours are not read)
        gpu::TextureBinding texture;
        int kept;
        int firstIndex, indexCount;
        int instance;   // in instanceData
    };
    // What a draw binds: the order Sort puts the draws in, the same key for draws with the same bindings.
    static auto BindingKey(const Entry& e) {
        return std::make_tuple(e.kept, reinterpret_cast<std::uintptr_t>(e.texture.texture.texture),
                               reinterpret_cast<std::uintptr_t>(e.texture.sampler.sampler),
                               reinterpret_cast<std::uintptr_t>(e.vertices.buffer.buffer), e.vertices.offset,
                               reinterpret_cast<std::uintptr_t>(e.indices.buffer.buffer), e.indices.offset);
    }
    std::vector<Entry> draws;
    std::vector<CasterInstance> instanceData;
    StreamBuffer instances;
    int instanceOffset = -1;   // Upload's, in `instances`
    const Entry* applied = nullptr;
    int appliedPipeline = -1;
};
// Point-light shadows (docs/notes/renderer.md section 13, item 9). PreparePointShadows (from PrepareShadows)
// picks the strongest point lights in view that something shadows and gives the meshes lit on the GPU their
// matrix into the landscape's space. PointShadowSlot: the map of a kept scene light, -1 none.
void PreparePointShadows(Frame& frame);
void DrawPointShadows(Frame& frame);
void BindPointShadows(int slot);
int PointShadowSlot(const Frame& frame, int sceneLight);
// The object whose meshes are being handed over. ShadowFrameDone: the frame is presented.
const void* CurrentCaster();
bool CurrentCasterFigure();
void NoteCasterPart(const void* part);
void ShadowFrameDone();

// The screen's ambient occlusion (docs/notes/renderer.md section 13, item 11). DrawOcclusion, once a frame after the
// shadow maps: marks the receivers (TerrainBatch / MeshBatch::occluded), draws the world's depth at half size, the
// occlusion in it and its blur. Receivers include shaders/ambient_occlusion.hlsli, take OcclusionUniforms (all zero,
// none, unless `receives` and made) and bind the occlusion and its depth at `slot` with BindOcclusion (white
// stand-ins without). OcclusionFrameDone: the frame is presented.
void DrawOcclusion(Frame& frame);
void OcclusionUniforms(bool receives, float on[4], float map[4]);
void BindOcclusion(int slot);
void OcclusionFrameDone();

// The movies' blits, the depth clear in the middle of the pass and the movie player's fill.
bool CreateBlitPipeline();
void DrawBlitBatch(const Frame& frame, const BlitBatch& batch, int width, int height);
void DrawClearDepth(const ClearBatch& clear, int width, int height);
void DrawFill(const FillBatch& fill, int width, int height);
// At the frame's present: the frame's cursor (render::Cursor) shown, or none (render_cursor.cpp).
void ShowFrameCursor();
// The holder is gone: the cursor shapes made from its picture let go.
void ForgetCursor(const void* holder);

// The counts of the frame being built (summed at Present). StatsTimer adds its scope's CPU time to a field; a
// timer inside another's scope takes its time out of the outer one's field.
extern FrameStats g_stats;
// The renderer's own timings (StatsTimer, the perf overlay, the texture counts): the steady clock's ticks, nanoseconds
// (render_perf.cpp: <chrono> would cost every includer a megabyte of header).
long long Ticks();
inline constexpr double kMsPerTick = 1e-6;
class StatsTimer {
public:
    explicit StatsTimer(double FrameStats::* field);
    ~StatsTimer();
    StatsTimer(const StatsTimer&) = delete;
    StatsTimer& operator=(const StatsTimer&) = delete;

private:
    double FrameStats::* field;
    long long start;
    StatsTimer* outer;
};
// CountPipeline: a change when it differs from the last one applied in the frame; CountBindings: a texture change
// when the first texture bound differs from the last.
void CountPipeline(gpu::Pipeline pipeline);
void CountBindings(const gpu::Texture& first);
void CountDraw(std::size_t uniformBytes);
void CountUpload(FrameStats::Upload kind, std::size_t bytes);
// Against the last frame's vertices for the same key (a terrain page: sector and page; a mesh: its positions,
// page 0).
void CountReuse(bool terrain, const void* key, int page, const Vertex* vertices, std::size_t count);

// The interface (render_ui.cpp). RunCanvasCommands takes the frame copies waiting for the pictures (at the start
// of DrawPending, outside any pass); UploadUi puts the frame's quads and the atlases they need on the GPU (before
// the passes); UiFrameDone after the present.
void RunCanvasCommands();
void UploadUi(const Frame& frame);
void UiFrameDone();
void DrawUiBatch(const UiBatch& batch, int width, int height);
void DrawPictureBatch(const PictureBatch& batch, int width, int height);
// SplitFrame draws the commands recorded so far (a pass split; not between frames); RunCanvasesNow runs
// RunCanvasCommands at once.
void SplitFrame();
void RunCanvasesNow();

// Images made and updated in place by purpose, logged in the verify build.
enum class ImageUse { Movie, Texture, Count };
void CountImage(ImageUse use, bool made);
void CountImageRequest(ImageUse use);
void LogImageCounts(const char* when);

// A Texture or a texture's holder (Surface) as a texture with its mip levels: what was decoded or drawn for it; white
// when there is none. Valid until the next Present.
const gpu::Texture& TextureView(const void* texture);
const gpu::Texture& SurfaceView(const void* holder);
// render::TextureFromMmp: kept until the holder goes.
bool TextureFromMmpData(const void* holder, const void* data, unsigned mip, unsigned levels);
void ForgetHolder(const void* holder);
// render::PictureFromMmp: level 0 as RGBA8, kept until the holder goes.
struct Picture {
    int width = 0, height = 0;
    std::vector<std::uint8_t> rgba;
};
bool PictureFromMmpData(const void* holder, const void* data);
const Picture* PictureOf(const void* holder);
gpu::Sampler LinearSampler();
// A 1x1 white texture.
const gpu::Texture& WhiteView();

// Textures the game builds from images on the GPU (render::ComposeTexture; render_compose.cpp): an image
// decoded to premultiplied RGBA8 rows from the top. False (nothing decoded) for an image without pixels or too large.
struct ComposeImage {
    int width = 0, height = 0;
    std::vector<std::uint8_t> rgba;
};
bool DecodeComposeImage(const TextureImage& image, ComposeImage* decoded);
// The global map's clouds drawn into a texture (render::DrawClouds).
struct CloudsDraw {
    CloudFrame from{}, to{};
    int blend = 0;
    std::uint8_t ramp[256] = {};
};
// One image drawn into the texture's first level, or the clouds.
struct ComposeStep {
    const ComposeImage* image = nullptr;   // null: `to` filled white (unless `clouds`)
    const CloudsDraw* clouds = nullptr;    // the clouds over all of `to`
    bool over = true;                      // over what is there (premultiplied alpha), else replacing it
    int fromX = 0, fromY = 0;              // the image texel drawn at `to`'s top-left texel
    Rect to{};                             // first-level texels
};
// A composed texture's image.
struct ComposeShape {
    int width = 0, height = 0;   // the first level
    int levels = 1;              // the image's levels (the ones below the first made by the GPU)
    int firstLevel = 0;          // the first level the texture shows
    BlankAlpha alpha = BlankAlpha::Full;   // what is drawn keeps of the alpha (None: cleared to opaque black)
};
// The texture of `shape` made with `steps` drawn into it: the levels it shows (from shape.firstLevel on, copied out
// of a full-size image when that is above 0); empty (nothing made) on failure.
gpu::Texture MakeComposed(const ComposeShape& shape, const ComposeStep* steps, std::size_t count);
// More steps drawn into a texture MakeComposed made for `shape` (of first level 0; its levels made again).
void DrawComposed(const ComposeShape& shape, const gpu::Texture& image, const ComposeStep* steps, std::size_t count);
#if defined(EI_RENDER_VALIDATION)
// Level `level` (`width` x `height`) of a texture as RGBA8 rows from the top, as the GPU reads it (drawn texel for
// texel into a target and read back): render::CheckMmpLevels's levels. No pass in progress.
bool ReadImageLevel(const gpu::Texture& image, int level, int width, int height, std::vector<std::uint8_t>* rgba);
#endif
// render::ComposeTexture's and BlankTexture's entry for `holder` (render_textures.cpp): `shape` made with `steps`.
bool StoreComposed(const void* holder, const ComposeShape& shape, const ComposeStep* steps, std::size_t count);
gpu::Sampler StageSampler(const StageState& stage);
// Linear filters asked for: anisotropic at `level` (2..kMostAnisotropy) with linear mips; point filters as the
// stage says. Always for the terrain pages (at kMostAnisotropy, whatever the render quality), at the render
// quality's anisotropy for the world's models when it is above 1.
constexpr std::uint32_t kMostAnisotropy = 16;
gpu::Sampler AnisotropicSampler(const StageState& stage, std::uint32_t level);
// After a present: textures replaced during the frame released, entries unused for a long time dropped.
void RetireTextures();

constexpr gpu::Format kColourFormat = gpu::Format::Rgba8;
constexpr gpu::Format kDepthFormat = gpu::Format::Depth32Float;

// The render quality's multisampling (docs/notes/renderer.md section 11): the world commands are drawn into a
// multisampled target resolved into the frame target. PassSampleCount: the sample count of the pass being
// drawn; every pipeline drawing into it is made for it.
int PassSampleCount();
constexpr int kSmoothSamples = 8;   // the most the render quality asks for
// The multisampled target's second colour target: each fragment's depth, written where the depth is (a
// multisampled depth cannot be resolved or sampled), resolved into a plain texture.
constexpr gpu::Format kDepthCopyFormat = gpu::Format::R32Float;
// A pipeline made for more than one sample: that target added, written when `p` writes the depth.
void AddDepthCopy(gpu::PipelineDesc& p);

// The pipelines every game makes, listed for gpu::PrepareInBackground (render.cpp's ListPipelines), which makes them on
// the device's thread before the first frame. Each is the description the file's draws make (the same function builds
// both), appended for a pass of `samples` samples where that matters. Pure: they run off the main thread.
void ListMeshPipelines(std::vector<gpu::PipelineDesc>& out, int samples);
void ListTerrainPipelines(std::vector<gpu::PipelineDesc>& out, int samples);
void ListBlitPipelines(std::vector<gpu::PipelineDesc>& out, int samples);
void ListUiPipelines(std::vector<gpu::PipelineDesc>& out, int samples);
void ListSmoothPipelines(std::vector<gpu::PipelineDesc>& out, int samples);
void ListShadowPipelines(std::vector<gpu::PipelineDesc>& out);
void ListComposePipelines(std::vector<gpu::PipelineDesc>& out);
void ListPresentPipelines(std::vector<gpu::PipelineDesc>& out);
void ListOcclusionPipelines(std::vector<gpu::PipelineDesc>& out);
gpu::PipelineDesc PointShadowClearDesc();
// False (logged) when the copies could not be made: the world is then drawn without multisampling.
bool PrepareSmoothCopies(int samples);
// Inside a pass on the multisampled target: the frame target's colour and depth copied into every sample (and the
// depth into the depth copy's).
void CopyIntoMultisampled(const gpu::Texture& colour, const gpu::Texture& depth);
// Inside a pass on the frame target: its depth set from the resolved depth copy; the colour comes from the
// multisampled pass's resolve.
void CopyDepthFromResolved(const gpu::Texture& depth);

} // namespace render::detail
