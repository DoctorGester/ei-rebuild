#pragma once

// Internals shared by the renderer's files.

#include "platform/log.h"
#include "render.h"
#include "render_checks.h"
#include "render/gpu.h"
#include "render_state.h"

#include "lib/printf_args.h"
#include "lib/vecmath.h"

// The programs' uniform blocks, slots and vertex inputs (CMakeLists.txt, tools/gen_gpu_shaders.py).
#include "shaders/gpu_shaders.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

class TextureDevice;
class Texture;

namespace render::text {
class GlyphAtlas;
} // namespace render::text

namespace render {

using gpu_shaders::BlitParams;
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

// Logs a line the first most times it is asked for; count counts the asks.
template <class Count, class... Args>
void LogFirst(Count& count, int most, const char* format, Args... args) {
    if (count++ >= static_cast<Count>(most)) return;
    if constexpr (sizeof...(Args) == 0) ei::Log("%s", format);
    else ei::Log(format, args...);
}

// A D3DCOLOR's red, green and blue as 0..1 (the alpha is left to the caller).
inline void UnpackColour(std::uint32_t colour, float* rgb) {
    rgb[0] = ((colour >> 16) & 0xFF) / 255.0f;
    rgb[1] = ((colour >> 8) & 0xFF) / 255.0f;
    rgb[2] = (colour & 0xFF) / 255.0f;
}

// A vertex: world (terrain) or model (meshes) position.
struct Vertex {
    Vector3 position;
    std::uint32_t colour = 0;  // D3DCOLOR (bytes B, G, R, A)
    std::uint32_t specular = 0;  // D3DCOLOR; its alpha is the fog factor (255 no fog)
    float u = 0.0f, v = 0.0f;
    Vector3 normal;           // model space (a figure mesh lit on the GPU), else zero
};
static_assert(sizeof(Vertex) == 40);

// One texture stage of a pass (render_state.h's values). A stage the pass leaves off keeps these.
struct StageState {
    StageCombiner combiner = {TextureOp::Disable, TextureArg::Texture, TextureArg::Current, TextureOp::Disable, TextureArg::Texture, TextureArg::Current};
    Filter magFilter = Filter::Linear, minFilter = Filter::Linear;
    MipFilter mipFilter = MipFilter::None;
    Address addressU = Address::Wrap, addressV = Address::Wrap;
    gpu::Texture texture;   // the bound surface, read at the pass
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
StagePatch PatchOf(const Surface* holder);

// The render state of one pass of a mesh draw. Blend, depth and culling make the pipeline; the rest
// goes to the shader.
struct PassState {
    bool blend = false;
    Blend sourceBlend = Blend::One, destBlend = Blend::Zero;
    bool depthTest = true, depthWrite = true;
    Compare depthFunc = Compare::LessEqual;
    Cull cull = Cull::None;
    bool alphaTest = false;
    Compare alphaFunc = Compare::Always;
    std::uint8_t alphaReference = 0;
    bool specular = false;
    bool fog = false;
    std::uint32_t fogColour = 0;                   // D3DCOLOR
    std::uint32_t textureFactor = 0xFFFFFFFF;
    StageState stages[2];
    StagePatch patch;

    std::uint32_t PipelineKey() const {
        auto bits = [](auto value, std::uint32_t mask) { return static_cast<std::uint32_t>(value) & mask; };
        return (blend ? 1u : 0u) | (bits(sourceBlend, 15) << 1) | (bits(destBlend, 15) << 5) | (depthTest ? 1u << 9 : 0u) |
               (depthWrite ? 1u << 10 : 0u) | (bits(depthFunc, 15) << 11) | (bits(cull, 3) << 15);
    }
};

// The pass adds its light to the frame (blended, the destination's factor one): an effect that neither casts a
// shadow nor receives one.
inline bool AddsLight(const PassState& s) { return s.blend && s.destBlend == Blend::One; }

// A box in the landscape's space: empty (low above high) until points are added.
struct Box {
    Vector3 low{1e30f, 1e30f, 1e30f}, high{-1e30f, -1e30f, -1e30f};
    void Extend(const Vector3& p) {
        low = Min(low, p);
        high = Max(high, p);
    }
};

// A caster's cut-out for its stage 0 texture: alpha[0] 1 (cut out), where its alpha is below alpha[1]; none (0) for a
// pass that is neither alpha tested nor blended, or without a texture.
inline void CasterAlpha(const PassState& s, float* alpha) {
    alpha[0] = alpha[1] = 0.0f;
    if ((s.alphaTest || s.blend) && s.stages[0].hasTexture) {
        alpha[0] = 1.0f;
        alpha[1] = s.alphaTest ? (static_cast<float>(s.alphaReference) + 0.5f) / 255.0f : 0.5f;
    }
}

// The render state the game set for the pass about to be drawn (render_state.h). `device`'s bound surfaces
// become the stages' textures; null: no textures.
PassState ReadPassState(const TextureDevice* device);
void ApplyPassState(gpu::PipelineDesc& desc, const PassState& state);

// A sector a terrain pass drew (the game's clip test kept it): its place in the zone's mesh (render_terrain.cpp's
// slots) and its quarters the game did not cull (bit q).
struct TerrainDrawn {
    int slot = 0;
    std::uint8_t quarters = 0;
};

// A terrain pass: the zone's mesh of the ground or the water, one draw.
struct TerrainBatch {
    bool water = false;       // the water pass (alpha from the vertex colour)
    int matrix = 0;           // index into Frame::matrices
    int firstSector = 0;      // its drawn sectors in Frame::terrainSectors
    int sectorCount = 0;
    PassState state;          // its textures not read
    bool occluded = false;    // the ground of the world: it takes the ambient occlusion (DrawOcclusion)
};

struct CachedMesh;

constexpr float kNoFront = 1e30f;
struct MeshBatch {
    int matrix = 0;
    int firstIndex = 0;       // into the frame's mesh stream, or (cached) the mesh's own indices
    int indexCount = 0;
    PassState state;
    DistanceFog fog;          // the distance fog's (density 0: none; MeshFog::Distance)
    bool horizon = false;     // MeshFog::Horizon, from the eye eyeHeight above the mesh's origin
    float eyeHeight = 0.0f;
    bool unclipped = false;   // the old path projected it without clipping
    float zScale = 0.0f;      // the depth is multiplied by it (1: unchanged)
    bool lines = false;       // a line list (else triangles)
    bool model = false;       // a model's mesh (BeginMesh .. MeshPass), not pre-transformed vertices
    bool flat = false;        // the flat geometry (decals, markers), no corners
    bool decal = false;       // flat: a ground print (MeshDraw::groundDecal), shaded as the ground under it; `box` its
                              // positions' box in the landscape's space
    const Object3D* caster = nullptr;   // render::BeginCaster's object, if any
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
    float weather[2] = {};    // a unit's own wetness and snow (PrepareUnitWeather)
    const Object3D* interfaceUnit = nullptr;   // an interface figure of that world unit (BeginInterfaceUnit): its weather
};

// The image is read at the blit.
struct BlitBatch {
    gpu::Texture texture;
    int textureWidth = 0, textureHeight = 0;
    Rect destination;
    Rect source;
};

struct ClearBatch {
    float z = 0.0f;
    Rect rect;                // screen pixels; right 0: the whole frame
};

// render::Fill.
struct FillBatch {
    Rect rect;                // screen pixels
    std::uint32_t colour = 0;  // D3DCOLOR, alpha ignored
};

// The interface's quads (render::ui::Solid / Glyphs): a corner in frame pixels, its atlas texels (u < 0: full
// coverage) and its colour's bytes R, G, B, A.
struct UiVertex {
    float x = 0.0f, y = 0.0f, u = 0.0f, v = 0.0f;
    std::uint32_t colour = 0;
};
// A run of the frame's uiVertices drawn with one atlas (null: solids only), scissored to `scissor`.
struct UiBatch {
    std::uint32_t first = 0, count = 0;
    const text::GlyphAtlas* atlas = nullptr;
    Rect scissor;
};
// render::ui::DrawPicture.
struct PictureBatch {
    gpu::Texture texture;
    int width = 0, height = 0;
    Rect destination;
    bool grey = false;
};

struct Command {
    enum class Kind : std::uint8_t { Terrain, Mesh, Blit, ClearDepth, Fill, Ui, Picture };
    Kind kind = {};
    int index = 0;            // into terrainBatches / meshBatches / blitBatches / clears / fills / uiBatches / pictureBatches
};

struct Frame;

// The scene's light as render::SceneLighting keeps it: the sun (the direction its light travels, unit), its colours
// and the point lights nearest the camera. Until the first hand-over: a sun from above, half ambient.
struct KeptLight {
    Vector3 at;
    float inverseRangeSquared = 0.0f;
    float colour[3] = {};
    bool specular = false;
    bool shadows = false;   // casts point shadows (ScenePointLight::shadows: not an object's own light)
};
struct KeptScene {
    Vector3 sun{0.0f, 0.0f, -1.0f};
    float sunColour[3] = {1.0f, 1.0f, 1.0f};
    float ambientColour[3] = {0.5f, 0.5f, 0.5f};
    float daySunColour[3] = {1.0f, 1.0f, 1.0f};   // SceneLights::daySunColour, dayAmbientColour
    float dayAmbientColour[3] = {0.5f, 0.5f, 0.5f};
    Vector3 camera;
    double clientTicks = 0.0;   // the client's time it was handed over with (SceneLights::clientTicks)
    int count = 0;
    KeptLight lights[kMostSceneLights];
};
const KeptScene& KeptSceneLight();
// The terrain shader's (shaders/scene_lighting.hlsli): the kept light with the frame's point shadows and the shadow
// map's light direction.
static_assert(std::size(SceneLightParams{}.pointAt) == kMostSceneLights);
SceneLightParams SceneLightUniforms(const Frame& frame);
// The kept light at `position` exactly (the same Light's), -1 none.
int SceneLightIndex(const Vector3& position);
// The shadow map's light for the sun `sun`: at least 20 degrees above the horizon, unit. False without a
// direction. Receivers lit on the GPU weigh the map's darkening by their facing to it.
std::optional<Vector3> MapLightDirection(const Vector3& sun);
// A camera's clip matrix inverted, in double: a model matrix taken from under it moves by less than the camera's
// rounding (render_point_shadows.cpp's kKeptPlace). Invert works on the camera given unless it is the one inverted
// last; false when it has no inverse. Under: `placed` (camera x model) without the camera.
class CameraInverse {
public:
    bool Invert(const Matrix4& camera);
    Matrix4 Under(const Matrix4& placed) const;

private:
    Matrix4 camera{};
    double inverse[16] = {};
    bool known = false;   // `camera` was inverted (`ok`: it has an inverse)
    bool ok = false;
};
// `m`'s inverse (worked in double); none when it has none.
std::optional<Matrix4> InvertMatrix(const Matrix4& m);
// a x b, both row-major.
Matrix4 Product(const Matrix4& a, const Matrix4& b);
// A mesh lit on the GPU (shaders/mesh.hlsl's MeshLightParams). FigureLight: a figure part's, the sun and lights in
// the part's model space, their colours times the material's diffuse colour and the coefficient; the
// kMostMeshLights nearest the part's origin. GroundDecalLight (below): a ground print's. Each light
// names its kept scene light (SceneLightIndex, -1 none), whose point shadow it takes at the draw
// (MeshLightUniforms).
constexpr int kMostMeshLights = 8;
static_assert(std::size(MeshLightParams{}.pointAt) == kMostMeshLights);
struct MeshLight {
    bool groundPrint = false;
    Vector3 sun;   // towards the sun
    float sunColour[3] = {};
    float ambientColour[3] = {};
    float emissiveColour[3] = {};
    Vector3 mapSun;   // a ground print's: the direction the shadow map's light travels, and the daylight colours
    float daySunColour[3] = {};
    float dayAmbientColour[3] = {};
    int count = 0;
    struct Point {
        Vector3 at;
        float inverseRangeSquared = 0.0f;
        float colour[3] = {};
        int sceneLight = -1;
        bool specular = false;   // a ground print's: added after its colour
    } points[kMostMeshLights];
};
MeshLight FigureLight(const MeshLighting& lighting);
// Harness build: the ground prints drawn (SetGroundDecals).
bool GroundDecalsDrawn();

// Point-light shadows (shaders/point_shadow.hlsli): the receivers' fragment uniforms, the same for the frame. Each
// light with a map has six faces of kPointFaceTexels in one row of the atlas.
constexpr int kMostPointShadows = 4;
constexpr int kPointFaceTexels = 512;
static_assert(std::size(PointShadowParams{}.lights) == kMostPointShadows);
struct PointShadowFrame {
    std::vector<Matrix4> models;   // MeshBatch::landscape: model space to the landscape's
    int sceneSlots[kMostSceneLights] = {};   // per kept scene light its map + 1, 0 none
    int mapped = 0;                          // the lights with a map
    PointShadowParams params{};
    void Clear() {
        models.clear();
        for (int& slot : sceneSlots) slot = 0;
        mapped = 0;
        params = PointShadowParams{};
    }
};

// Casters weaker than this (units fading in) cast no shadow.
constexpr float kLeastStrength = 0.5f;

// A frame's object shadows: the sun and casters handed over, and what PrepareShadows made of them.
struct ShadowFrame {
    bool haveSun = false;
    Vector3 sun{};
    std::unordered_map<const Object3D*, float> casters;   // object -> strength (render::CastsShadow; the last wins)
    bool prepared = false;
    bool on = false;          // the map is made this frame: receivers sample it
    bool haveClouds = false;  // render::CloudShadows handed over
    CloudWeather weather{};
    bool clouds = false;      // the cloud shadows are shown this frame (`lights` made for them too)
    std::vector<Matrix4> lights;   // each receiving mesh's model space to the map's clip space; [0]: the terrain's
    ShadowParams params{};
    ShadowParams cloudParams{};    // the clouds alone (`on` 0): for the water, and when no map is drawn
    float normalOffset = 0.0f;     // ShadowNormalOffset's with the map
    int cellsAround = 1;           // the dilation of the map's depth range (shaders/shadow_dilate.hlsl)
    PointShadowFrame point;
    void Clear() {
        haveSun = false;
        sun = Vector3{};
        casters.clear();
        prepared = false;
        on = false;
        haveClouds = false;
        weather = CloudWeather{};
        clouds = false;
        lights.clear();
        params = ShadowParams{};
        cloudParams = ShadowParams{};
        normalOffset = 0.0f;
        cellsAround = 1;
        point.Clear();
    }
};

// A mesh batch the world draws after its first ground batch that has a model (the flat geometry only as a ground
// print: MeshBatch::decal), as PrepareShadows lists them for the walks of the sun's and the point lights' shadows,
// the ambient occlusion and the roofs.
struct WorldMesh {
    int batch = 0;           // into Frame::meshBatches
    bool additive = false;   // the light added (D3DBLEND_ONE): a receiver and a caster of nothing
    bool repeat =
        false;   // another pass of the mesh of the last batch that was not additive (the same kept mesh and first index)
};

struct Frame {
    std::vector<Matrix4> matrices;
    std::vector<TerrainDrawn> terrainSectors;
    std::vector<TerrainBatch> terrainBatches;
    std::vector<Vertex> meshVertices;
    std::vector<std::uint32_t> meshIndices;
    std::vector<MeshBatch> meshBatches;
    std::vector<MeshLight> meshLights;   // MeshBatch::lighting
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
        std::size_t first = 0, end = 0;
    };
    std::vector<Range> worldRanges;
    // PrepareShadows' lists for the walks that follow it: the non-water terrain batches of the world ranges (indices
    // into terrainBatches) and the world's models drawn after the first of them (WorldMesh).
    std::vector<int> worldGround;
    std::vector<WorldMesh> worldMeshes;
    bool haveFog = false;
    bool fogEnabled = false;
    std::uint32_t fogColour = 0;   // D3DCOLOR
    DistanceFog distanceFog;       // the terrain's
    MapEdge mapEdge;
// Direct3D 7 samples a pixel at its integer screen coordinates, today's GPUs half a pixel further right and
// down: the 3D draws move their clip positions by half a pixel (x by +1 / width, y by -1 / height, times w)
// so the same screen coordinates get the same pixels, texture coordinates and colours. Set by EndFrame.
    float pixelShift[2] = {0.0f, 0.0f};
    ShadowFrame shadow;
    // Empties the frame for the next one (at its present), the lists keeping their room.
    void Clear() {
        matrices.clear();
        terrainSectors.clear();
        terrainBatches.clear();
        meshVertices.clear();
        meshIndices.clear();
        meshBatches.clear();
        meshLights.clear();
        blitBatches.clear();
        clears.clear();
        fills.clear();
        uiVertices.clear();
        uiBatches.clear();
        pictureBatches.clear();
        commands.clear();
        worldRanges.clear();
        worldGround.clear();
        worldMeshes.clear();
        haveFog = false;
        fogEnabled = false;
        fogColour = 0;
        distanceFog = {};
        mapEdge = {};
        pixelShift[0] = pixelShift[1] = 0.0f;
        shadow.Clear();
    }
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
// The size of the game's screen (the device object's); none without a device or beyond the frame target's limit.
std::optional<Size> GameTargetSize();
// The device is started: the device and the pipelines made at the first use (a failure ends the game: Fatal).
void Start();
// The renderer cannot go on (no device, no swap chain): the reason is logged and shown in the game's error box, and
// the game ends (exit -1). There is no other way to draw: no fallback.
[[noreturn]] EI_PRINTF_FORMAT(1, 2) void Fatal(const char* format, ...);
// With a std::string or std::string_view among the arguments: its text is passed for the %s.
template <class... Args>
    requires libprintf::HasClass<Args...>
[[noreturn]] void Fatal(const char* format, Args&&... args) {
    Fatal(format, libprintf::Arg(libprintf::Owned(std::forward<Args>(args)))...);
}

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
    RectParams vs{{}, {static_cast<float>(width), static_cast<float>(height)}};
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
// The frame target's size; none without the target.
std::optional<Size> FrameSize();
// The frame into the swap chain's texture through the gamma ramp; PresentWindow, after the frame's submission (which
// presents it), counts it. False: nothing to present into / nothing presented.
bool DrawToWindow();
void PresentWindow();
// How often SetGammaRamp changed the gamma ramp the present applies (render.h's GammaRampTable).
std::uint32_t GammaRampChanges();
#if defined(EI_RENDER_VALIDATION)
// Whether the verify build's GPU read-backs (a stall each) run for the `count`th of their kind: the first
// kReadBackAlways, then every 16th, and every one with "checkall" in EI_RENDER.
constexpr long kReadBackAlways = 64;
bool ReadBackDue(long count);
// The movie image just uploaded read back from the GPU and compared with the pixels written (render_checks.cpp),
// sampled.
void CheckMovieUpload(const gpu::Texture& image, const std::uint8_t* rgba, int w, int h);
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
    bool index = false;
    const char* label = nullptr;
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
    bool terrain = false;
    std::uint32_t frame = 0xFFFFFFFFu;
    std::size_t vertexCount = 0, indexCount = 0;
    int vertexOffset = -1, indexOffset = -1;
};

struct PoolPlace {
    std::uint32_t generation = 0;   // the pool's when placed (0: never)
    std::uint32_t firstVertex = 0, vertexRoom = 0, firstIndex = 0, indexRoom = 0;
};

// An item kept in a GpuPool (a terrain page, a figure mesh, a caster): its place and what of it is still to be
// written there.
struct KeptRanges {
    PoolPlace place;
    bool dirty = true;                               // all of it to write
    bool pending = false;                            // in its cache's list of items to write (UploadKept)
    std::uint32_t vertexFrom = 0, vertexTo = 0;      // vertices whose geometry to write
    std::uint32_t colourFrom = 0, colourTo = 0;      // vertices whose colours to write
    std::uint32_t lastUsed = 0;                      // FrameNumber() of its last use

    void MarkVertex(std::uint32_t i) { Widen(i, vertexFrom, vertexTo); }
    void MarkColour(std::uint32_t i) { Widen(i, colourFrom, colourTo); }
    // All of it to write (built again), no ranges.
    void WriteAll() {
        dirty = true;
        vertexFrom = vertexTo = colourFrom = colourTo = 0;
    }
    bool HasWrites() const { return dirty || vertexTo > vertexFrom || colourTo > colourFrom; }

private:
    static void Widen(std::uint32_t i, std::uint32_t& from, std::uint32_t& to) {
        if (to <= from) {
            from = i;
            to = i + 1;
        } else {
            if (i < from) from = i;
            if (i + 1 > to) to = i + 1;
        }
    }
};

// Vertices and indices kept on the GPU across frames (terrain pages, meshes): a geometry stream, a colour
// stream (a ColourPair a vertex) and absolute 32-bit indices, in buffers written in place, only the changed
// ranges. Items are placed one after the other; when one does not fit, the pool starts again (Restart) with the
// items still needed, larger when they need more than half.
struct ColourPair {
    std::uint32_t colour = 0, specular = 0;   // D3DCOLOR
};
class GpuPool {
public:
    GpuPool(std::size_t newGeometryStride, std::uint32_t newLeastVertices, std::uint32_t newLeastIndices, const char* newLabel)
        : geometryStride(newGeometryStride), leastVertices(newLeastVertices), leastIndices(newLeastIndices), label(newLabel) {}
// Keeps the item's place when it still fits there, else places it at the end (all of it to write: `dirty`).
// False when the pool is full.
    bool Place(KeptRanges& k, std::size_t vertices, std::size_t indices);
    bool Placed(const PoolPlace& p) const { return p.generation == generation && geometry.buffer; }
    void Restart(std::uint64_t vertices, std::uint64_t indices);
    void WriteGeometry(const PoolPlace& p, std::uint32_t first, std::uint32_t count, const void* data) const;
    void WriteIndices(const PoolPlace& p, const std::uint32_t* relative, std::size_t count);
    // Writes what `k` says of an item's arrays as kept on the CPU: all of them when dirty, else the vertices' and
    // colours' changed ranges; their bytes are counted under `vertexBytes` and `indexBytes`. `vertices` holds
    // `vertexCount` of the pool's geometry stride; `colours` two words a vertex, or none (a caster); `indices` are
    // relative to the item's first vertex.
    void WriteKept(KeptRanges& k, const void* vertices, std::size_t vertexCount, std::span<const std::uint32_t> colours,
                   std::span<const std::uint32_t> indices, FrameStats::Count vertexBytes, FrameStats::Count indexBytes);
    void Bind(Geometry& g) const;
    void Drop();

private:
    struct Buffer {
        gpu::Buffer buffer;
        bool Make(std::size_t bytes, bool index, const char* label);
        void Write(std::size_t offset, const void* data, std::size_t bytes) const;
        void Drop();   // kept by the GPU until the commands using it are done
    };
    std::size_t geometryStride = 0;
    std::uint32_t leastVertices = 0, leastIndices = 0;
    const char* label = nullptr;
    Buffer geometry, colours, indices;
    std::uint32_t vertexCapacity = 0, indexCapacity = 0;
    std::uint32_t usedVertices = 0, usedIndices = 0;
    std::uint32_t generation = 1;
    std::vector<std::uint32_t> absolute;    // scratch: an item's indices made absolute
};

// `item` (a KeptRanges) joins its cache's list of items to write, unless it is in it.
template <class Item>
void AddPending(std::vector<Item*>& pending, Item& item) {
    if (item.pending) return;
    item.pending = true;
    pending.push_back(&item);
}

// The pending items (Item: a KeptRanges with its `vertices`, `colours` and `indices`) placed in `pool` and written.
// When one does not fit, the frame's items (`frameItems(add)` calls add(item) for each) join the pending ones and the
// pool starts again with all of them, placed again from its start.
template <class Item, class FrameItems>
void UploadKept(GpuPool& pool, std::vector<Item*>& pending, FrameItems frameItems, FrameStats::Count vertexBytes,
                FrameStats::Count indexBytes) {
    if (pending.empty()) return;
    bool placed = true;
    for (Item* item : pending) {
        if (!pool.Place(*item, item->vertices.size(), item->indices.size())) {
            placed = false;
            break;
        }
    }
    if (!placed) {
        frameItems([&](Item& item) { AddPending(pending, item); });
        std::uint64_t vertices = 0, indices = 0;
        for (const Item* item : pending) {
            vertices += item->vertices.size();
            indices += item->indices.size();
        }
        pool.Restart(vertices, indices);
        // They fit: the pool holds twice what they need.
        for (Item* item : pending) pool.Place(*item, item->vertices.size(), item->indices.size());
    }
    for (Item* item : pending) {
        if (pool.Placed(item->place))
            pool.WriteKept(*item, item->vertices.data(), item->vertices.size(), item->colours, item->indices,
                           vertexBytes, indexBytes);
        item->pending = false;
    }
    pending.clear();
}

// The terrain (render_terrain.cpp): UploadTerrain writes what changed of the zone's meshes before the frame's passes,
// DrawTerrainBatch draws a pass's mesh. For the shadow map's fit and the water's mirror, the frame's terrain as the
// passes drew it in pieces (TerrainPieces: one page's tiles in one quarter of a drawn sector, the quarter not culled,
// page by page and each page's in the order the sectors were drawn): TerrainPieceGroups, a piece's vertices in runs
// (TerrainGroup: made when the sector is built, the water's boxes again when it moved), TerrainPieceWhole, all of them
// as one; TerrainGroupPositions, every `step`th position of a run. TerrainFrameDone, after a present, counts the
// frame's sectors (FrameStats::terrainReuse).
struct TerrainGroup {
    Box box;                             // of its positions in the landscape's space
    std::uint32_t first = 0, count = 0;  // its vertices in its sector's layer
};
struct TerrainPiece {
    int slot = 0, layer = 0, run = 0;
};
bool CreateTerrainShader();
// The water's ripple map (render_water.cpp), made at the start (false: it could not be), bound with every terrain
// batch; RippleOffsets, its two layers' offsets at the client time (TerrainFsParams::ripples, slowRipples).
bool MakeRippleMap();
gpu::TextureBinding RippleMap();
void RippleOffsets(float water[4], float swamp[4]);
// The water's reflections (render_water.cpp; shaders/terrain.hlsl's reflected_sky and screen_reflection).
// DrawSkyReflection, once a frame before its first pass on the world: the sky dome (the world's meshes before its
// first ground) drawn mirrored in the water's plane into the sky image. CopyFrameForWater, outside a pass at the end
// of a world view that drew water (or before the water's first draw when !LastWaterCopyKept: the last frame left
// none at this size): the frame's colour (resolved) at half size, the colour the water's rays take where they hit
// the frame's depth (the ambient occlusion's); WaterCopyTaken: this frame's was taken. BindWaterReflections: every
// terrain batch's part of `fs` for them (`view`: its clip matrix) and the two images bound. ListWaterPipelines: the
// copy's.
void DrawSkyReflection(Frame& frame);
void CopyFrameForWater(const gpu::Texture& colour);
bool WaterCopyTaken();
bool LastWaterCopyKept();
void BindWaterReflections(const Matrix4& view, TerrainFsParams* fs);
void ListWaterPipelines(std::vector<gpu::PipelineDesc>& out);
// A clip matrix's depth (z / w) to its clip w: w = out[1] / (depth - out[0]) (render_occlusion.cpp).
void DepthMap(const Matrix4& clip, float out[2]);
void UploadTerrain();
void DrawTerrainBatch(const Frame& frame, const TerrainBatch& batch);
// Appends `batch`'s pieces to `out`.
void TerrainPieces(const Frame& frame, const TerrainBatch& batch, std::vector<TerrainPiece>* out);
std::span<const TerrainGroup> TerrainPieceGroups(const TerrainPiece& piece);
const TerrainGroup& TerrainPieceWhole(const TerrainPiece& piece);
void TerrainGroupPositions(const TerrainPiece& piece, const TerrainGroup& group, std::uint32_t step,
                           std::vector<Vector3>* out);
void TerrainFrameDone();
// The ambient occlusion's depth (DrawOcclusion): the ground's mesh drawn for the frame's batches marked `occluded`
// (once a clip matrix), depth only, with TerrainDepthDesc's pipeline; the draws made. GroundOcclusionCounts: the sectors whose ground
// occlusion is kept, and how often one was worked out (render::OcclusionInfo).
gpu::PipelineDesc TerrainDepthDesc();
int DrawTerrainDepth(const Frame& frame, gpu::Pipeline pipeline);
struct GroundOcclusionCount {
    int sectors = 0, made = 0;
};
GroundOcclusionCount GroundOcclusionCounts();
// The ground's casters (render::ShadowTerrainSector): every sector's 33 x 33 ground vertices in one mesh, written by
// UploadTerrain. DrawTerrainCasters applies `pipeline` (vertex buffer 0: TerrainCasterVertex, 32-bit indices) and its
// vertex uniforms (`light`, the sectors that cast this frame) and draws the mesh once; the caller pushed the fragment
// uniforms. TerrainCasterCount: the sectors that cast this frame.
struct TerrainCasterVertex {
    Vector3 position;
    Vector3 normal;
};
static_assert(sizeof(TerrainCasterVertex) == 24);
std::size_t TerrainCasterCount();
int DrawTerrainCasters(gpu::Pipeline pipeline, const Matrix4& light);
void TerrainCastersDone();

bool CreateMeshShader();
void UploadMeshes(const Frame& frame);
// `anisotropy` above 1: the stages sampled anisotropically at that level (AnisotropicSampler).
void DrawMeshBatch(const Frame& frame, const MeshBatch& batch, int anisotropy);

// The buffers a mesh batch draws from (the frame's stream, or the kept meshes' pool) and its first index;
// false when it has none. MeshLayout `positionsOnly`: position and texture coordinates alone (the casters).
bool BindMeshBatch(const MeshBatch& batch, Geometry& g, int* firstIndex);
void MeshLayout(gpu::PipelineDesc& desc, bool cached, bool positionsOnly);

// Appends the triangles of `indices` (three a triangle) over `cornerCount` corners to `out`, each index plus `base`; a
// triangle with a corner out of range is left out.
void AppendTriangles(const std::uint16_t* indices, int indexCount, int cornerCount, std::uint32_t base,
                     std::vector<std::uint32_t>& out);

// Meshes kept on the GPU (docs/notes/renderer.md section 13). KeepMesh: the mesh being handed over brought up
// to date as kept for render::SetMeshSource's figure mesh, or null: it goes into the frame's stream (no source,
// no corners, the cache off, or the mesh already drawn this frame with other data). MeshCacheFrameDone drops
// meshes not drawn for a long time.
// A kept corner's geometry (its colours in the pool's colour stream).
struct MeshVertex {
    Vector3 position;
    float u = 0.0f, v = 0.0f;
    Vector3 normal;   // CornerNormal
};
static_assert(sizeof(MeshVertex) == 32);
void NoteMeshDraw(const MeshDraw& mesh);
CachedMesh* KeepMesh(const MeshDraw& mesh);
int CachedIndexCount(const CachedMesh& mesh);
int CachedVertexCount(const CachedMesh& mesh);
// The position (model space) of the kept mesh's corner its index `k` names.
Vector3 CachedCorner(const CachedMesh& mesh, int k);
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
const FigureMesh* MeshSourceOwner();
const MeshLighting* MeshSourceLighting();
// The model-space normal of the corners with colour index `index`, zero without normals or words.
Vector3 CornerNormal(const MeshDraw& mesh, int index);

// Object shadows (docs/notes/renderer.md section 13). PrepareShadows, once a frame before its first pass on
// the world: fits the map around the visible ground seen from the sun and gives every receiving batch its
// light matrix; DrawShadowMap draws the casters and the depth range per cell (nothing when the map is off), then the
// caller the roof map and the point lights' shadows. Receivers include
// shaders/shadow_receiver.hlsli, take its ShadowParams (ShadowUniforms: all zero, no shadow, unless `receives`) and
// bind the map (or a cleared 1 x 1 one) at `mapSlot` and its depth range per cell after it with BindShadowMap.
void PrepareShadows(Frame& frame);
void DrawShadowMap(Frame& frame);
const ShadowParams& ShadowUniforms(const Frame& frame, bool receives, bool clouded);
void BindShadowMap(const Frame& frame, int mapSlot);
// The cloud shadows (render_cloud_shadows.cpp, shaders/cloud_shadow.hlsli), in the receivers' ShadowParams: a batch
// with a light matrix takes them (`clouded` in ShadowUniforms), the water too. PrepareClouds, from PrepareShadows
// once a frame outside a pass: the cloud map made at the first, the drift and the cover moved on by the world's time;
// returns how much of the sun a full cloud hides this frame (0: no clouds). CloudUniforms: their part of `params`
// for the map's fit (the landscape's x0, y0 at its clip space's -1, its extent). BindCloudMap: the map at `slot`.
float PrepareClouds(const ShadowFrame& shadow);
float CloudCover();
// The clouds' part of the weather a save's side file keeps (render.h, KeptWeather), and its restore.
void CloudsToKeep(KeptWeather* weather);
void RestoreClouds(const KeptWeather& weather);
void CloudUniforms(float x0, float y0, float extent, float light, ShadowParams* params);
void BindCloudMap(int slot);
// The weather on the surfaces (render_weather.cpp; section 13, item 11b). PrepareSurfaces, from PrepareShadows once a
// frame: the wetness, the puddles and the snow cover moved on towards the frame's weather by the world's time.
// SurfaceUniforms: the terrain's weather in its TerrainFsParams. MeshWeatherUniforms: a world mesh's in its
// MeshFsParams (wet, snow, weatherSun, weatherSunColour). SurfaceState: the three levels for the harness.
// PrepareUnitWeather, after PrepareSurfaces: each unit drawn gets its own wetness and snow (MeshBatch::weather),
// following the weather out of shelter and drying under a roof (the casters straight above it).
void PrepareSurfaces(const ShadowFrame& shadow);
void PrepareUnitWeather(Frame& frame);
void SurfaceUniforms(TerrainFsParams* fs);
void MeshWeatherUniforms(MeshFsParams* fs);
struct SurfaceLevels {
    float wet = 0.0f, puddles = 0.0f, snow = 0.0f;
};
SurfaceLevels SurfaceState();
// The roofs over the ground and the world's meshes (render_weather.cpp, shaders/roof.hlsli): while there is weather on
// the surfaces, PrepareRoofs (after PrepareUnitWeather) fits a top-down depth map of the frame's roofs (the drawn
// objects' meshes, not the units') around the visible ground; RoofUniforms writes the receivers' part of `params` for
// the sun map's light matrix `light` (its clip space, where the receivers' points are, to the roof map's);
// DrawRoofMap draws it (outside a pass); BindRoofMap binds it (or the 1 x 1 stand-in) at `slot`.
void PrepareRoofs(Frame& frame);
void RoofUniforms(const Matrix4& light, ShadowParams* params);
void DrawRoofMap();
void BindRoofMap(int slot);
// The visible ground's box in the landscape's space, by the sun map's fit of this frame; none without one.
// AddOutsideRoofs: the sun map's casters outside the view that are not units (render::ShadowCasterMesh) into `draws`
// through `clip` (the landscape's space to the roof map's), after the sun map's draws were listed; the count added.
std::optional<Box> VisibleGround(Frame& frame);
class CasterDraws;
int AddOutsideRoofs(CasterDraws& draws, const Matrix4& clip);
// How far along its normal a receiver's point is moved before it is looked up in the map: 1.5 of the map's texels,
// in world units (0 without the map: not `receives` in ShadowUniforms, or none drawn).
inline float ShadowNormalOffset(const Frame& frame, bool receives) {
    return receives && frame.shadow.on ? frame.shadow.normalOffset : 0.0f;
}
// The world's camera this frame: the clip matrix of the frame's first ground batch, the eye in the landscape's space
// and the clip matrix's inverse.
struct WorldView {
    Matrix4 clip;
    Vector3 eye;
    CameraInverse inverse;
};
// None without ground drawn, or when its clip matrix has no inverse or puts the eye at infinity.
std::optional<WorldView> WorldCamera(const Frame& frame);
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
    float uvOffset[2] = {};  // a kept mesh's texture offsets (MeshBatch::uOffset, vOffset)
    float alpha[2] = {};  // cut out (1) where stage 0's alpha is below [1]
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
        int kept = 0;
        int firstIndex = 0, indexCount = 0;
        int instance = 0;  // in instanceData
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
void BindPointShadows(const Frame& frame, int slot);
int PointShadowSlot(const Frame& frame, int sceneLight);
// A ground print's light (MeshBatch::decal; mesh.hlsl's ground_shade): the scene's sun, ambient light and the
// kMostMeshLights point lights nearest its box (in the landscape's space, low xyz, high xyz) that reach it, the
// shadow map's light and the daylight colours. MeshLightUniforms: a mesh's light as the shader takes it, each light
// with its point shadow (-1 none).
MeshLight GroundDecalLight(const Frame& frame, const float box[6]);
MeshLightParams MeshLightUniforms(const Frame& frame, const MeshLight& light);
// The object whose meshes are being handed over. ShadowFrameDone: the frame is presented.
const Object3D* CurrentCaster();
bool CurrentCasterFigure();
// The world unit an interface figure being handed over shows (render::BeginInterfaceUnit), or null.
const Object3D* CurrentInterfaceUnit();
void NoteCasterPart(const FigureMesh* part);
void ShadowFrameDone();

// The screen's ambient occlusion (docs/notes/renderer.md section 13, item 11). DrawOcclusion, once a frame after the
// shadow maps: marks the receivers (TerrainBatch / MeshBatch::occluded), draws the world's depth at half size, the
// occlusion in it and its blur. Receivers include shaders/ambient_occlusion.hlsli, take OcclusionUniforms (all zero,
// none, unless `receives` and made) and bind the occlusion and its depth at `slot` with BindOcclusion (white
// stand-ins without). OcclusionFrameDone: the frame is presented.
void DrawOcclusion(Frame& frame);
// What the receivers take of the occlusion this frame (ambient_occlusion.hlsli's arguments; all zero: none).
struct OcclusionReceiver {
    bool on = false;
    bool view = false;                  // the occlusion alone (SetOcclusion's View)
    float depthOffset = 0.0f;           // a depth's clip w: depthScale / (depth - depthOffset)
    float depthScale = 0.0f;
    float scale[2] = {};                // the half-size texels per frame pixel
    float texel[2] = {};                // their inverse size
};
// The frame's, or none when the receiver does not take it.
const OcclusionReceiver& OcclusionOf(bool receives);
// Fills the occlusion members of a receiver's block (MeshFsParams, TerrainFsParams).
template <class Block>
void OcclusionUniforms(bool receives, Block& fs) {
    const OcclusionReceiver& o = OcclusionOf(receives);
    fs.occlusionOn = o.on ? 1 : 0;
    fs.occlusionView = o.view ? 1 : 0;
    fs.occlusionDepthOffset = o.depthOffset;
    fs.occlusionDepthScale = o.depthScale;
    for (int k = 0; k < 2; ++k) {
        fs.occlusionScale[k] = o.scale[k];
        fs.occlusionTexel[k] = o.texel[k];
    }
}
void BindOcclusion(int slot);
void OcclusionFrameDone();

// The movies' blits, the depth clear in the middle of the pass and the movie player's fill.
bool CreateBlitPipeline();
void DrawBlitBatch(const BlitBatch& batch, int width, int height);
void DrawClearDepth(const ClearBatch& clear, int width, int height);
void DrawFill(const FillBatch& fill, int width, int height);
// At the frame's present: the frame's cursor (render::Cursor) shown, or none (render_cursor.cpp).
void ShowFrameCursor();
// The holder is gone: the cursor shapes made from its picture let go.
void ForgetCursor(const Surface* holder);

// The harness build's renderer (EI_RENDER_VALIDATION): its checks and FrameStats' times.
#if defined(EI_RENDER_VALIDATION)
constexpr bool kRenderValidation = true;
#else
constexpr bool kRenderValidation = false;
#endif
// The counts of the frame being built (summed at Present).
extern FrameStats g_stats;
// The renderer's own timings (Timer, the perf overlay, the texture counts): the steady clock's ticks, nanoseconds
// (render_perf.cpp: <chrono> would cost every includer a megabyte of header).
long long Ticks();
inline constexpr double kMsPerTick = 1e-6;
// A span of the renderer's CPU time, from Begin to End, or a scope's when made with a kind. End gives its
// milliseconds (0 when it was not running). With a kind the harness build adds them to that FrameStats time (in the
// product such a timer never runs); a timer begun with a kind while another such runs takes its time out of that
// one's, so each counts its own work only (timers with a kind end in the reverse order of their Begin). Without a kind
// it runs in every build (the perf overlay's CPU parts).
class Timer {
public:
    Timer() = default;
    explicit Timer(FrameStats::Time kind) { Begin(kind); }
    ~Timer() { End(); }
    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;
    // Nothing when running.
    void Begin();
    void Begin(FrameStats::Time kind);
    double End();
    bool Running() const { return start != 0; }

private:
    int kind = -1;    // the FrameStats::Time, -1 none
    int outer = -1;   // the kind of the timer running when this one began, -1 none
    long long start = 0;
};
// CountPipeline: a change when it differs from the last one applied in the frame; CountBindings: a texture change
// when the first texture bound differs from the last.
void CountPipeline(gpu::Pipeline pipeline);
void CountBindings(const gpu::Texture& first);
void CountDraw(std::size_t uniformBytes);
void CountUpload(FrameStats::Upload kind, std::size_t bytes);
// At the present (render_checks.cpp): the frame's counts and times, with its `matrices` and size, added to the sums
// TakeFrameStats gives; the next frame's start from zero.
void AddFrameStats(std::size_t matrices, int width, int height);
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

// A Texture or a texture's holder (Surface) as a texture with its mip levels: what was decoded or drawn for it; white
// when there is none. Valid until the next Present.
const gpu::Texture& TextureView(const ::Texture* texture);
const gpu::Texture& SurfaceView(const Surface* holder);
// Which image TextureView gives: another number each time it is replaced (0: none kept).
std::uint32_t TextureSerial(const ::Texture* texture);
// render::TextureFromMmp: kept until the holder goes.
bool TextureFromMmpData(const Surface* holder, const MmpFile& file, unsigned mip, unsigned levels);
void ForgetHolder(const Surface* holder);
// render::PictureFromMmp: level 0, kept until the holder goes.
bool PictureFromMmpData(const Surface* holder, const MmpFile& file);
// The held .mmp textures (not the composed ones): the image, its first level's size, the levels uploaded and the
// file's format id (render::CheckMmpLevels reads them).
struct HeldMmp {
    gpu::Texture image;
    int width = 0, height = 0, levels = 0;
    std::uint32_t format = 0;
};
std::vector<HeldMmp> HeldMmpTextures();
const Picture* PictureOf(const Surface* holder);
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
// One image drawn into the texture's first level.
struct ComposeStep {
    const ComposeImage* image = nullptr;   // null: `to` filled white
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
// The global map's clouds (map_clouds.hlsl) drawn over all of a texture MakeComposed made for `shape` (first level 0;
// its levels made again).
void DrawCloudTexture(const ComposeShape& shape, const gpu::Texture& image, const MapClouds& clouds);
// Level `level` (`width` x `height`) of a texture as RGBA8 rows from the top, as the GPU reads it (drawn texel for
// texel into a target and read back): render::CheckMmpLevels's levels. No pass in progress.
bool ReadImageLevel(const gpu::Texture& image, int level, int width, int height, std::vector<std::uint8_t>* rgba);
// render::ComposeTexture's and BlankTexture's entry for `holder` (render_textures.cpp): `shape` made with `steps`.
bool StoreComposed(const Surface* holder, const ComposeShape& shape, const ComposeStep* steps, std::size_t count);
gpu::Sampler StageSampler(const StageState& stage);
// Linear filters asked for: anisotropic at `level` (2..kMostAnisotropy) with linear mips; point filters as the
// stage says. Always for the terrain pages (at kMostAnisotropy, whatever the render quality), at the render
// quality's anisotropy for the world's models when it is above 1.
constexpr std::uint32_t kMostAnisotropy = 16;
gpu::Sampler AnisotropicSampler(const StageState& stage, std::uint32_t level);
// An image the frame's recorded commands still draw: released by ReleaseRetired, after the present.
void ReleaseAfterPresent(const gpu::Texture& image);
void ReleaseRetired();

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
// The multisampled passes of a frame where no draw takes the depth copy have no such target (render.cpp's
// DepthCopyRead). PassDepthCopy: the pass being drawn has it. DropDepthCopy: `p` (made with AddDepthCopy) for a pass
// without it. MakePassPipeline: `desc`'s pipeline for the pass being drawn.
bool PassDepthCopy();
void DropDepthCopy(gpu::PipelineDesc& p);
gpu::Pipeline MakePassPipeline(gpu::PipelineDesc desc, const char* label);

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
// depth into the depth copy's, when the pass has it).
void CopyIntoMultisampled(const gpu::Texture& colour, const gpu::Texture& depth);
// Inside a pass on the frame target: its depth set from the resolved depth copy; the colour comes from the
// multisampled pass's resolve.
void CopyDepthFromResolved(const gpu::Texture& depth);

}   // namespace render
