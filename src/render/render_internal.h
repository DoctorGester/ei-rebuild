#pragma once

// Internals shared by the renderer's files.

#include "render.h"
#include "sokol_gfx.h"

#include "lib/vecmath.h"

#include <cstdint>
#include <memory>
#include <utility>
#include <span>
#include <tuple>
#include <vector>

class TextureDevice;
struct ID3D11Buffer;
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;
struct ID3D11Texture2D;
struct IDXGISwapChain1;
struct IDXGIFactory2;

namespace render::detail {

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
    sg_view texture{};                                            // the bound surface, read at the pass
    bool hasTexture = false;
};

// render::TexturePatch in stage 0's texture coordinates (wrapped into 0..1): inside `to` the shader samples
// `texture` at uv * map.xy + map.zw, kept inside `inside` (the source rectangle less half a texel). All
// zero: none.
struct StagePatch {
    sg_view texture{};
    float to[4] = {};       // u0, v0, u1, v1
    float map[4] = {};
    float inside[4] = {};   // u0, v0, u1, v1
};
StagePatch PatchOf(const void* holder);

// Compiled from src/render/shaders/*.hlsl at build time (CMakeLists.txt's ei_shader).
struct ShaderBytecode {
    sg_range vs, ps;
};
extern const ShaderBytecode kBlitShader, kSolidShader;
extern const ShaderBytecode kMeshShader;
extern const ShaderBytecode kTerrainShader;
extern const ShaderBytecode kPresentShader;
extern const ShaderBytecode kIntoMultisampledShader;
extern const ShaderBytecode kCasterShader, kTerrainCasterShader, kReduceShader, kDilateShader;
extern const ShaderBytecode kPerfOverlayShader;
extern const ShaderBytecode kTextureComposeShader, kCloudFrameShader, kCloudsShader;
extern const ShaderBytecode kUiSolidShader, kUiGlyphShader, kUiComposeShader, kUiGreyShader, kUiCopyShader,
    kUiRgb565Shader, kUiBlitShader;
// `samples` 2, 4 or 8; null ranges otherwise.
ShaderBytecode DepthFromMultisampledShader(int samples);
void SetBytecode(sg_shader_desc& s, const ShaderBytecode& code);

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
PassState ReadPassState(char* device);
void ApplyPassState(sg_pipeline_desc& p, const PassState& s);

// A terrain page kept on the GPU and one quarter of it drawn in a frame.
struct CachedPage;
struct TerrainPiece {
    CachedPage* page;
    int quarter;
};

struct TerrainBatch {
    const void* texture;      // the game's CTexture
    bool water;               // the water pass (alpha from the vertex colour)
    int matrix;               // index into Frame::matrices
    int firstPiece;           // into Frame::terrainPieces
    int pieceCount;
    PassState state;          // its textures not read
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
};

// The image is read at the blit.
struct BlitBatch {
    sg_view texture;
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

// render::ui::DrawCanvas; the canvas is looked up by its holder when drawn.
struct CanvasBatch {
    const void* holder;
    Rect destination;
    Rect source;
    bool keyed;
    std::uint32_t key;        // 0x00RRGGBB as the canvas holds it
};

struct Command {
    enum Kind : std::uint8_t { Terrain, Mesh, Blit, ClearDepth, Fill, Canvas };
    Kind kind;
    int index;                // into terrainBatches / meshBatches / blitBatches / clears / fills / canvasBatches
};

// The receivers' shader parameters of the shadow map (shaders/shadow_receiver.hlsli).
struct ShadowParams {
    float on[4];       // x: on, y: strength, z: 1 the shadow alone (SetShadowView), w: constant depth bias
    float scale[4];    // xyz: shadow clip units to world units (normals), w: largest depth slope
    float camera[4];   // xyz: the camera in shadow clip space
    float soft[4];     // x: filter radius (uv) per unit of blocker distance (clip z), y: the blocker
                       // search's radius (uv, the widest filter), z: the narrowest filter (uv), w: texels a side
};
static_assert(sizeof(ShadowParams) == 64);

// The scene's light (shaders/scene_lighting.hlsli) as kept by render::SceneLighting, a fragment uniform block.
struct LightParams {
    float sun[4];          // xyz: the direction the sun's light travels (unit)
    float sunColour[4];
    float ambient[4];
    float counts[4];       // x: the point lights
    float mapSun[4];       // xyz: the direction the shadow map's light travels (MapLightDirection), unit
    float points[kMostSceneLights][2][4];   // position xyz, 1 / range squared; colour rgb, w: 1 specular
    float shadowSlots[kMostSceneLights];    // per point light its shadow map + 1, 0: none
};
static_assert(sizeof(LightParams) == 80 + kMostSceneLights * 36);
void LightUniforms(LightParams* params);
// The kept point lights, laid out as LightParams::points. SceneLightIndex: the kept light at `position`
// exactly (the same CLight's), -1 none.
const float (*SceneLightPoints(int* count))[2][4];
// The kept light `k` casts point shadows (ScenePointLight::shadows: not an object's own light).
bool SceneLightCastsShadows(int k);
int SceneLightIndex(const Vector3& position);
// The shadow map's light for the sun `sun`: at least 20 degrees above the horizon, unit. False without a
// direction. Receivers lit on the GPU weigh the map's darkening by their facing to it.
bool MapLightDirection(const Vector3& sun, Vector3* travel);
// `placed` (camera x model) without the camera. False when the camera has no inverse.
bool UnderCamera(const Matrix4& camera, const Matrix4& placed, Matrix4* model);
// a x b, both row-major.
Matrix4 Product(const Matrix4& a, const Matrix4& b);
// A figure part lit on the GPU (shaders/mesh.hlsl's fragment uniforms): the sun and lights in the part's
// model space, their colours times the material's diffuse colour and the coefficient; the
// kMostMeshLights nearest the part's origin.
constexpr int kMostMeshLights = 8;
struct MeshLightParams {
    float on[4];           // x: 1 lit, y: the crown's pull (0 none), z: the lights
    float sun[4];          // xyz: towards the sun in model space (as the game's: the direction's inverse)
    float sunColour[4];    // rgb: the sun's lit colour (none: 0)
    float ambient[4];      // rgb
    float emissive[4];     // rgb
    float points[kMostMeshLights][2][4];   // model-space position xyz, 1 / range squared; lit colour rgb, w: the
                                           // scene's light (SceneLightIndex) + 1, 0 none; drawn: its point
                                           // shadow + 1, 0 none
};
static_assert(sizeof(MeshLightParams) == 80 + kMostMeshLights * 32);
void MeshLightUniforms(const MeshLighting& lighting, MeshLightParams* params);

// Point-light shadows (shaders/point_shadow.hlsli): the receivers' fragment uniforms, the same for the
// frame. Each light with a map has six faces of kPointFaceTexels in one row of the atlas.
constexpr int kMostPointShadows = 4;
constexpr int kPointFaceTexels = 512;
struct PointShadowParams {
    float on[4];       // x: the lights with a map, y: texels a face side
    float eye[4];      // xyz: the camera in the landscape's space
    float lights[kMostPointShadows][4];   // xyz: the light in the landscape's space, w: the faces' near distance
    float depth[kMostPointShadows][4];    // xy: a face's depth for a distance d along its axis is x + y / d
};
static_assert(sizeof(PointShadowParams) == 32 + kMostPointShadows * 32);
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
    std::vector<Matrix4> lights;   // each receiving mesh's model space to the map's clip space; [0]: the terrain's
    ShadowParams params{};
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
    std::vector<CanvasBatch> canvasBatches;
    std::vector<Command> commands;
// The world view's commands (render::BeginWorld .. EndWorld), the last range open (end SIZE_MAX) while the
// world view draws. The render quality draws them multisampled and their models anisotropically.
    struct Range {
        std::size_t first, end;
    };
    std::vector<Range> worldRanges;
// Drawn last, after the frame without it is kept for the cursor thread.
    BlitBatch cursor{};
    bool haveCursor = false;
    bool haveFog = false;
    bool fogEnabled = false;
    std::uint32_t fogColour = 0;   // D3DCOLOR
// Direct3D 7 samples a pixel at its integer screen coordinates, Direct3D 11 half a pixel further right and
// down: the 3D draws move their clip positions by half a pixel (x by +1 / width, y by -1 / height, times w)
// so the same screen coordinates get the same pixels, texture coordinates and colours. Set by EndFrame.
    float pixelShift[2] = {0.0f, 0.0f};
    ShadowFrame shadow;
};
extern Frame g_frame;   // the frame being built
std::uint32_t FrameNumber();
std::uint32_t CommitNumber();

// Added when it differs from the last one.
int AddMatrix(const Matrix4& m);
// The game's fog switch and colour (render_state.h), read once per frame.
void ReadFog(Frame& f);
TextureDevice* DeviceObject();
bool GameTargetSize(int* width, int* height);
// sokol is set up: the device and the pipelines made at the first use (a failure ends the game: Fatal).
void Start();
// The renderer cannot go on (no device, a sokol panic, a lost device not made again, no swap chain): the reason is
// logged and shown in the game's error box, and the game ends (exit -1), Rebirth's DisableExitOnCrashReport or not.
// There is no other way to draw: no fallback.
[[noreturn]] void Fatal(const char* format, ...);
ID3D11Device* D3DDevice();
ID3D11DeviceContext* D3DContext();
// The Direct3D 11 texture of a sokol image (no reference taken); null for none.
inline ID3D11Texture2D* D3DTexture(sg_image image) {
    return static_cast<ID3D11Texture2D*>(const_cast<void*>(sg_d3d11_query_image_info(image).tex2d));
}

// A texture's first level, or a region of it, read back: copied into a CPU-readable staging texture of its format
// and mapped (render_staging.cpp). The staging texture is kept for the next read (made again for a larger region or
// another format) until Release, which every holder calls (no destructor: some are globals that outlive the device).
// Touches no sokol state; a read on the main thread resets sokol's state cache after it.
class StagingCopy {
public:
    // The whole level / `width` x `height` texels from (left, top); mapped until Unmap. False, nothing mapped, for no
    // source or when the copy cannot be made or mapped.
    bool Map(ID3D11DeviceContext* context, ID3D11Texture2D* source);
    bool Map(ID3D11DeviceContext* context, ID3D11Texture2D* source, unsigned left, unsigned top, unsigned width,
             unsigned height);
    // The mapped region's size and row y (the source's texel format).
    unsigned Width() const { return mappedWidth; }
    unsigned Height() const { return mappedHeight; }
    const std::uint8_t* Row(unsigned y) const { return data + static_cast<std::size_t>(y) * pitch; }
    void Unmap();
    void Release();

private:
    ID3D11Texture2D* texture = nullptr;
    unsigned width = 0, height = 0, format = 0;
    ID3D11DeviceContext* context = nullptr;    // while mapped
    const std::uint8_t* data = nullptr;
    std::size_t pitch = 0;
    unsigned mappedWidth = 0, mappedHeight = 0;
};

// The vertex parameters of the rectangle shaders (blit, ui_canvas, texture_compose, texture_clouds): the rectangle
// (left, top, right, bottom, target pixels) and the target's width and height.
struct RectParams {
    float rect[4];
    float target[4];
};
static_assert(sizeof(RectParams) == 32);
// `r` on a `width` x `height` target as vertex uniform block 0.
inline void ApplyRect(const Rect& r, int width, int height) {
    RectParams vs{{static_cast<float>(r.left), static_cast<float>(r.top), static_cast<float>(r.right), static_cast<float>(r.bottom)},
                  {static_cast<float>(width), static_cast<float>(height), 0.0f, 0.0f}};
    sg_range range{&vs, sizeof(vs)};
    sg_apply_uniforms(0, &range);
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

sg_image FrameImage();
sg_view FrameTexture();
bool FrameSize(int* width, int* height);
// The frame into the window's swap chain through the gamma ramp, `cursor` (null: none) over it, then Present.
// False: nothing to present into.
bool DrawToWindow(const BlitBatch* cursor);
void PresentWindow();
// What the last DrawToWindow drew with, for the cursor thread's presents. Raw pointers, no references taken;
// false when there is no swap chain.
struct WindowParts {
    IDXGISwapChain1* chain;
    ID3D11RenderTargetView* view;
    sg_shader shader;
    sg_pipeline pipeline;
    sg_view frame;
    sg_view ramp;
    const std::uint8_t* rampTable;   // 256 RGBA8 entries
    int clientWidth, clientHeight;
    int frameWidth, frameHeight;
    Rect placed;
    std::uint32_t syncInterval;
    std::uint32_t presentFlags;
};
bool CurrentWindowParts(WindowParts* parts);
// The vertical blank switch's rules (tools/perf_check checks them).
bool TearingSupported(IDXGIFactory2* factory);
std::uint32_t ChainFlagsFor(bool allowTearing, bool tearingSupported);
std::uint32_t SyncIntervalFor(bool vsync);
std::uint32_t PresentFlagsFor(bool vsync, std::uint32_t chainFlags);
// Releases the swap chain and everything the window module made on the device (before sg_shutdown); the next
// DrawToWindow makes them on the new device.
void DropWindow();
// A lost device (DXGI_ERROR_DEVICE_REMOVED / RESET / HUNG, a driver error) was seen: everything is made again
// at the end of the frame. `reason`: GetDeviceRemovedReason then (S_OK for render::LoseDevice's).
// DeviceIsLost: seen and not made again yet.
void ReportDeviceLost(long result, long reason);
bool DeviceIsLost();
// True once after render::LoseDevice.
bool TakeSimulatedLoss();
#if defined(EI_RENDER_VALIDATION)
// After render::RemoveDevice: removes the device now (once), right before the window's Present, or at the end
// of the frame without one.
void RemoveIfAsked();
// Whether the verify build's GPU read-backs (a stall each) run for the `count`th of their kind: the first
// kReadBackAlways, then every 16th, and every one with "checkall" in EI_RENDER.
constexpr long kReadBackAlways = 64;
bool ReadBackDue(long count);
#endif
void DeviceLossCounts(WindowPresentInfo* info);

// Filled with sg_append_buffer, so a frame can be drawn in several passes; replaced by a larger one when an
// append does not fit.
class StreamBuffer {
public:
    StreamBuffer(bool newIndex, const char* newLabel) : index(newIndex), label(newLabel) {}
    int Append(const void* data, std::size_t bytes);
    sg_buffer Buffer() const { return buffer; }
    void Destroy();

private:
    bool index;
    const char* label;
    sg_buffer buffer{};
    std::size_t capacity = 0;
    std::size_t used = 0;                 // appended since the last commit
    std::uint32_t commit = 0xFFFFFFFFu;   // CommitNumber() of the last append
};
void RetireBuffers();

// A frame's vertices and absolute indices on the GPU, appended again when they grew since the frame's last
// pass.
class GeometryStream {
public:
    // `terrain`: counted as the terrain's uploads (FrameStats), else the meshes'.
    GeometryStream(const char* label, bool newTerrain) : vertices(false, label), indices(true, label), terrain(newTerrain) {}
    bool Upload(const std::vector<Vertex>& vertices, const std::vector<std::uint32_t>& indices);
    bool Bind(sg_bindings& b) const;
    void Destroy();

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
// stream (kColourPairBytes a vertex) and absolute 32-bit indices, in default-usage buffers written in place
// with UpdateSubresource (sokol has no partial update). Items are placed one after the other; when one does
// not fit, the pool starts again (Restart) with the items still needed, larger when they need more than half.
constexpr std::size_t kColourPairBytes = 2 * sizeof(std::uint32_t);
class GpuPool {
public:
    GpuPool(std::size_t newGeometryStride, std::uint32_t newLeastVertices, std::uint32_t newLeastIndices, const char* newLabel)
        : geometryStride(newGeometryStride), leastVertices(newLeastVertices), leastIndices(newLeastIndices), label(newLabel) {}
// Keeps the place when the item still fits there, else places it at the end (`anew`: all of it to write).
// False when the pool is full.
    bool Place(PoolPlace& p, std::uint32_t vertices, std::uint32_t indices, bool* anew);
    bool Placed(const PoolPlace& p) const { return p.generation == generation && geometry.buffer.id != 0; }
    void Restart(std::uint64_t vertices, std::uint64_t indices);
    void WriteGeometry(const PoolPlace& p, std::uint32_t first, std::uint32_t count, const void* data) const;
    void WriteColours(const PoolPlace& p, std::uint32_t first, std::uint32_t count, const std::uint32_t* pairs) const;
    void WriteIndices(const PoolPlace& p, const std::uint32_t* relative, std::size_t count);
    void Bind(sg_bindings& b) const;
    void Drop();

private:
    struct Buffer {
        sg_buffer buffer{};
        ID3D11Buffer* d3d = nullptr;   // sokol holds the reference
        bool Make(std::size_t bytes, bool index, const char* label);
        void Write(std::size_t offset, const void* data, std::size_t bytes) const;
        void Drop();   // destroyed after the frame's commit
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
// drawn for a long time. DestroyTerrain keeps the pages (placed again at their next draw).
struct TerrainGroup {
    Vector3 low, high;              // the box of its positions in the landscape's space
    std::uint32_t first, count;     // its vertices in the page's
};
bool CreateTerrainShader();
void UploadTerrain(const Frame& frame);
void DrawTerrainBatch(const Frame& frame, const TerrainBatch& batch);
std::span<const TerrainGroup> TerrainPieceGroups(const TerrainPiece& piece);
const TerrainGroup& TerrainPieceWhole(const TerrainPiece& piece);
void TerrainGroupPositions(const TerrainPiece& piece, const TerrainGroup& group, std::uint32_t step,
                           std::vector<Vector3>* out);
void TerrainFrameDone();
void DestroyTerrain();
// The ground's casters (render::ShadowTerrainSector). DrawTerrainCasters draws with `pipeline` (vertex buffer
// 0: TerrainCasterVertex, 32-bit indices) whose uniforms the caller applied.
struct TerrainCasterVertex {
    Vector3 position;
    Vector3 normal;
};
static_assert(sizeof(TerrainCasterVertex) == 24);
std::size_t TerrainCasterCount();
void UploadTerrainCasters();
int DrawTerrainCasters(sg_pipeline pipeline);
void TerrainCastersDone();
void DropTerrainCasters();

bool CreateMeshShader();
void UploadMeshes(const Frame& frame);
// `anisotropy` above 1: the stages sampled anisotropically at that level (AnisotropicSampler).
void DrawMeshBatch(const Frame& frame, const MeshBatch& batch, int anisotropy);
void DestroyMeshes();

// The buffers a mesh batch draws from (the frame's stream, or the kept meshes' pool) and its first index;
// false when it has none. MeshLayout `positionsOnly`: position and texture coordinates alone (the casters).
bool BindMeshBatch(const MeshBatch& batch, sg_bindings& b, int* firstIndex);
void MeshLayout(sg_pipeline_desc& p, bool cached, bool positionsOnly);

// Meshes kept on the GPU (docs/notes/renderer.md section 13). KeepMesh: the mesh being handed over brought up
// to date as kept for render::SetMeshSource's figure mesh, or null: it goes into the frame's stream (no source,
// no corners, the cache off, or the mesh already drawn this frame with other data). MeshCacheFrameDone drops
// meshes not drawn for a long time; after DropMeshCache meshes are placed again at their next draw.
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
bool BindCachedMesh(const CachedMesh& mesh, sg_bindings& b, int* firstIndex);
void MeshCacheFrameDone();
void DropMeshCache();
const void* MeshSourceOwner();
const MeshLighting* MeshSourceLighting();
// The model-space normal of the corners with colour index `index`, zero without normals or words.
Vector3 CornerNormal(const MeshDraw& mesh, int index);

// Object shadows (docs/notes/renderer.md section 13). PrepareShadows, once a frame before its first pass on
// the world: fits the map around the visible ground seen from the sun and gives every receiving batch its
// light matrix; DrawShadowMap draws the casters and the depth range per cell. Receivers include
// shaders/shadow_receiver.hlsli, take ShadowParams and bind the map (or a cleared 1 x 1 one) with
// BindShadowMap.
void PrepareShadows(Frame& frame);
void DrawShadowMap(Frame& frame);
void ShadowUniforms(const Frame& frame, bool receives, ShadowParams* params);
void BindShadowMap(sg_bindings& b, int viewSlot, int samplerSlot);
// The map's depth view and comparison sampler at the slots, its depth range per cell (RGBA32F, read
// filtered) and sampler at the slots after them.
void DescribeShadowMap(sg_shader_desc& s, int viewSlot, int samplerSlot);
void DestroyShadows();
// The clip matrix of the frame's first ground batch and the camera in the landscape's space; false without
// ground drawn.
bool WorldCamera(const Frame& frame, Matrix4* clip, Vector3* eye);
// NoShadowTexture: 1 x 1 at the far plane, for a receiver drawn without a map.
bool MakeShadowDepth(int width, int height, const char* label, sg_image* image, sg_view* attachment, sg_view* texture);
sg_sampler ShadowCompareSampler();
sg_view NoShadowTexture();
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
    // A draw of `indexCount` indices from `firstIndex` in the buffers `geometry` binds (BindMeshBatch,
    // BindCachedMesh; `kept`: the kept meshes' pool, MeshLayout's `cached`).
    void Add(const sg_bindings& geometry, bool kept, sg_view texture, sg_sampler sampler, int firstIndex, int indexCount,
             const CasterInstance& instance);
    std::size_t Count() const { return draws.size(); }
    // Sorts the draws in [from, to) by what they bind.
    void Sort(std::size_t from, std::size_t to);
    // Uploads the instances, before the pass's first Draw (whose pipeline and bindings are then applied anew);
    // false when they could not be (nothing is drawn).
    bool Upload();
    void Draw(std::size_t from, std::size_t to);
    void Clear();
    void Destroy();

private:
    struct Entry {
        sg_buffer vertices[2], indices;
        int vertexOffsets[2], indexOffset;
        sg_view texture;
        sg_sampler sampler;
        int kept;
        int firstIndex, indexCount;
        int instance;   // in instanceData
    };
    // What a draw binds: the order Sort puts the draws in, the same key for draws with the same bindings.
    static auto BindingKey(const Entry& e) {
        return std::make_tuple(e.kept, e.texture.id, e.sampler.id, e.vertices[0].id, e.vertexOffsets[0], e.vertices[1].id,
                               e.vertexOffsets[1], e.indices.id, e.indexOffset);
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
void PointShadowUniforms(const Frame& frame, PointShadowParams* params);
void BindPointShadows(sg_bindings& b, int viewSlot, int samplerSlot);
void DescribePointShadows(sg_shader_desc& s, int viewSlot, int samplerSlot);
int PointShadowSlot(const Frame& frame, int sceneLight);
void DestroyPointShadows();
// The object whose meshes are being handed over. ShadowFrameEnd: the frame's passes are recorded (the harness
// build's GPU time); ShadowFrameDone: the frame is presented.
const void* CurrentCaster();
bool CurrentCasterFigure();
void NoteCasterPart(const void* part);
void ShadowFrameEnd();
void ShadowFrameDone();

// The movies' blits, the depth clear in the middle of the pass and the movie player's fill.
bool CreateBlitPipeline();
void DrawBlitBatch(const Frame& frame, const BlitBatch& batch, int width, int height);
void DrawClearDepth(const ClearBatch& clear, int width, int height);
void DrawFill(const FillBatch& fill, int width, int height);
void RetireFrameImages();
void DestroyBlits();
// The cursor picture the frames draw (render::Cursor): `view` the whole picture (width x height, all its frames side
// by side) with its mask as alpha, made once; the cursor thread draws the same texture. DropCursorSheet: the
// holder is gone (the cursor thread forgets its picture).
void SetCursorSheet(const void* holder, const void* mask, int width, int height, sg_view view);
void DropCursorSheet(const void* holder);
// The holder is gone: its cursor picture's image let go.
void ForgetCursor(const void* holder);
// The device is lost: DestroyBlits, and every image kept across frames (movies, the cursor's pictures).
void DropBlits();

// The counts of the frame being built (summed at Present). StatsTimer adds its scope's CPU time to a field; a
// timer inside another's scope takes its time out of the outer one's field.
extern FrameStats g_stats;
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
// CountPipeline: a change when it differs from the last one applied in the frame.
void CountPipeline(sg_pipeline pipeline);
void CountBindings(const sg_bindings& b);
void CountDraw(std::size_t uniformBytes);
void CountUpload(FrameStats::Upload kind, std::size_t bytes);
// Against the last frame's vertices for the same key (a terrain page: sector and page; a mesh: its positions,
// page 0).
void CountReuse(bool terrain, const void* key, int page, const Vertex* vertices, std::size_t count);

// The interface canvases (render_ui.cpp). RunCanvasCommands draws the queued canvas commands outside any pass
// (at the start of DrawPending, under the present's lock); CanvasBlitsDrawn: the commands recorded so far are
// drawn; DropCanvases: the device is lost (each canvas is made again on the new one and its history drawn).
void RunCanvasCommands();
void CanvasBlitsDrawn();
void DrawCanvasBatch(const CanvasBatch& batch, int width, int height);
void DropCanvases();
// SplitFrame draws the commands recorded so far (a pass split; not between frames); RunCanvasesNow runs
// RunCanvasCommands at once, under the present's lock.
void SplitFrame();
void RunCanvasesNow();

// Images made and updated in place by purpose, logged in the verify build.
enum class ImageUse { Cursor, Movie, Texture, Count };
void CountImage(ImageUse use, bool made);
void CountImageRequest(ImageUse use);
void LogImageCounts(const char* when);

// A CTexture or a texture's holder (Surface) as a texture view with its mip levels: what was decoded or drawn for it;
// white when there is none. Valid until the next Present.
sg_view TextureView(const void* texture);
sg_view SurfaceView(const void* holder);
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
sg_sampler LinearSampler();
// A 1x1 white texture.
sg_view WhiteView();

// Textures the game builds from images on the GPU (render::ComposeTexture; render_compose.cpp): an image
// decoded to premultiplied RGBA8 rows from the top, shared by the textures drawn from it.
struct ComposeImage {
    int width = 0, height = 0;
    std::vector<std::uint8_t> rgba;
};
std::shared_ptr<const ComposeImage> DecodeComposeImage(const TextureImage& image);
// The global map's clouds drawn into a texture (render::DrawClouds).
struct CloudsDraw {
    CloudFrame from{}, to{};
    int blend = 0;
    std::uint8_t ramp[256] = {};
};
// One image drawn into the texture's first level, or the clouds.
struct ComposeStep {
    std::shared_ptr<const ComposeImage> image;   // null: `to` filled white (unless `clouds`)
    std::shared_ptr<const CloudsDraw> clouds;    // the clouds over all of `to`
    bool over = true;                            // over what is there (premultiplied alpha), else replacing it
    int fromX = 0, fromY = 0;                    // the image texel drawn at `to`'s top-left texel
    Rect to{};                                   // first-level texels
};
// What a composed texture is made of, kept to make it again on a new device.
struct ComposeRecipe {
    int width = 0, height = 0;   // the first level
    int levels = 1;              // the image's levels (the ones below the first made by the GPU)
    int firstLevel = 0;          // the first level the texture shows
    BlankAlpha alpha = BlankAlpha::Full;   // what is drawn keeps of the alpha (None: cleared to opaque black)
    std::vector<ComposeStep> steps;
};
// The image of `recipe` made and drawn, with a view of the levels it shows; false (nothing made) on failure.
bool MakeComposed(const ComposeRecipe& recipe, sg_image* image, sg_view* view);
// More steps drawn into an image MakeComposed made for `recipe` (its levels made again).
void DrawComposed(const ComposeRecipe& recipe, sg_image image, const ComposeStep* steps, std::size_t count);
// The device is lost: the pipelines and the clouds' frames go (made again at the next use).
void DestroyCompose();
#if defined(EI_RENDER_VALIDATION)
// Level `level` (`width` x `height`) of a texture image as RGBA8 rows from the top, as the GPU reads it (drawn texel
// for texel into a target and read back): render::CheckMmpLevels's block-compressed levels. No pass in progress.
bool ReadImageLevel(sg_image image, int level, int width, int height, std::vector<std::uint8_t>* rgba);
#endif
// render::ComposeTexture's entry for `holder` (render_textures.cpp); RemakeComposed draws every composed texture
// again on a new device (no pass in progress).
bool StoreComposed(const void* holder, std::shared_ptr<ComposeRecipe> recipe);
void RemakeComposed();
// GPU work from game code outside the frame's drawing (a texture composed): the cursor thread keeps off the
// immediate context meanwhile (the present's lock), as for the canvases run at once.
class GpuWorkAlone {
public:
    GpuWorkAlone();
    ~GpuWorkAlone();
    GpuWorkAlone(const GpuWorkAlone&) = delete;
    GpuWorkAlone& operator=(const GpuWorkAlone&) = delete;
};

// The cursor between frames. The main thread's window present (BeginWindowPresent .. EndWindowPresent) and
// the cursor thread's take turns on the swap chain's lock. BeginWindowPresent comes before anything draws into
// the frame target (the cursor thread draws it only while no pass is in progress); EndWindowPresent takes the
// references the cursor thread draws with. ProtectDevice: the cursor thread's command list runs on the
// immediate context. DropCursorPresents releases everything before the device goes.
void ProtectDevice(ID3D11Device* device, ID3D11DeviceContext* context);
void BeginWindowPresent();
void EndWindowPresent(bool presented);
void DropCursorPresents();
sg_sampler StageSampler(const StageState& stage);
// Linear filters asked for: anisotropic at `level` (2..kMostAnisotropy) with linear mips; point filters as the
// stage says. Always for the terrain pages (at kMostAnisotropy, whatever the render quality), at the render
// quality's anisotropy for the world's models when it is above 1.
constexpr std::uint32_t kMostAnisotropy = 16;
sg_sampler AnisotropicSampler(const StageState& stage, std::uint32_t level);
// After a present: images replaced during the frame destroyed, entries unused for a long time dropped.
void RetireTextures();
void DestroyTextures();
// The device is lost: every texture image destroyed (before sg_shutdown). The textures keep their sources: an
// .mmp texture is made again at its next use (`kept`, with `keptBytes` of sources), a composed or drawn-into one
// by RemakeComposed.
void DropTextures(std::size_t* kept, std::size_t* keptBytes);

// Puts the x87 control word back after calls into the driver (the game's main thread runs at 24-bit
// precision, docs/notes/rebirth.md).
class FpuGuard {
public:
    FpuGuard();
    ~FpuGuard();
    FpuGuard(const FpuGuard&) = delete;
    FpuGuard& operator=(const FpuGuard&) = delete;

private:
    unsigned int saved;
};

constexpr sg_pixel_format kColourFormat = SG_PIXELFORMAT_RGBA8;
constexpr sg_pixel_format kDepthFormat = SG_PIXELFORMAT_DEPTH;

// The render quality's multisampling (docs/notes/renderer.md section 11): the world commands are drawn into a
// multisampled target resolved into the frame target. PassSampleCount: the sample count of the pass being
// drawn; every pipeline drawing into it is made for it.
int PassSampleCount();
// The most samples up to kSmoothSamples the device renders kColourFormat and kDepthFormat with (1: none).
constexpr int kSmoothSamples = 8;
int SupportedSamples(ID3D11Device* device);
// False (logged) when the copies could not be made: the world is then drawn without multisampling.
bool PrepareSmoothCopies(int samples);
// Inside a pass on the multisampled target: the frame target's colour and depth copied into every sample.
void CopyIntoMultisampled(sg_view colour, sg_view depth);
// Inside a pass on the frame target: its depth set to sample 0 of the multisampled depth; the colour comes
// from the multisampled pass's resolve.
void CopyDepthFromMultisampled(sg_view depth);
void DestroySmooth();

} // namespace render::detail
