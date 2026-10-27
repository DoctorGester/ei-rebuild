#pragma once

// Internals shared by the new renderer's files: render.cpp (device, frame, targets, window, switches),
// render_terrain.cpp, render_mesh.cpp, render_blit.cpp, render_textures.cpp.

#include "render.h"
#include "sokol_gfx.h"

#include "lib/vecmath.h"

#include <cstdint>
#include <utility>
#include <vector>

class CTextureDevice;
struct ID3D11Buffer;
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;
struct IDXGISwapChain1;
struct IDXGIFactory2;

namespace render::detail {

// The game's transform pipeline matrices are Matrix4: row-major, out = M * (x, y, z, 1), Direct3D clip
// space (-w <= x, y <= w, 0 <= z <= w).

// A vertex: world (terrain) or model (meshes) position, the game's lit colours, texture coordinates.
struct Vertex {
    Vector3 position;
    std::uint32_t colour;     // D3DCOLOR (bytes B, G, R, A)
    std::uint32_t specular;   // D3DCOLOR; its alpha is the fog factor (255 no fog)
    float u, v;
    Vector3 normal;           // model space (a figure mesh lit on the GPU), else zero
};
static_assert(sizeof(Vertex) == 40);

// One texture stage of the old device for a pass (D3DTEXTURESTAGESTATETYPE values).
struct StageState {
    std::uint8_t colourOp = 1, colourArg1 = 2, colourArg2 = 1;   // D3DTOP_DISABLE, TEXTURE, CURRENT
    std::uint8_t alphaOp = 1, alphaArg1 = 2, alphaArg2 = 1;
    std::uint8_t magFilter = 2, minFilter = 2, mipFilter = 1;     // D3DTFG / D3DTFN / D3DTFP
    std::uint8_t addressU = 1, addressV = 1;                      // D3DTADDRESS_WRAP
    sg_view texture{};                                            // the bound surface, read at the pass
    bool hasTexture = false;
};

// A rectangle of stage 0's texture drawn from another texture (render::TexturePatch), in stage 0's texture
// coordinates (wrapped into 0..1): inside `to` the shader samples `texture` at uv * map.xy + map.zw, kept
// inside `inside` (the source rectangle less half a texel). All zero: none.
struct StagePatch {
    sg_view texture{};
    float to[4] = {};       // u0, v0, u1, v1
    float map[4] = {};
    float inside[4] = {};   // u0, v0, u1, v1
};
StagePatch PatchOf(const void* holder);

// A shader's bytecode, compiled from src/render/shaders/*.hlsl at build time (CMakeLists.txt's ei_shader).
struct ShaderBytecode {
    sg_range vs, ps;
};
extern const ShaderBytecode kBlitShader, kClearDepthShader;
extern const ShaderBytecode kMeshShader;
extern const ShaderBytecode kTerrainShader;
extern const ShaderBytecode kPresentShader;
extern const ShaderBytecode kIntoMultisampledShader;
extern const ShaderBytecode kCasterShader, kTerrainCasterShader, kReduceShader, kDilateShader;
extern const ShaderBytecode kPerfOverlayShader;
extern const ShaderBytecode kUiSolidShader, kUiGlyphShader, kUiComposeShader, kUiGreyShader, kUiCopyShader,
    kUiBlitShader;
// The smooth option's depth copy for a multisampled target of `samples` (2, 4 or 8); null ranges otherwise.
ShaderBytecode DepthFromMultisampledShader(int samples);
void SetBytecode(sg_shader_desc& s, const ShaderBytecode& code);

// The old device's state for one pass of a mesh draw. Blend, depth and culling make the pipeline; the rest
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
    StagePatch patch;                              // stage 0's texture's patch, if any

    std::uint32_t PipelineKey() const {
        return (blend ? 1u : 0u) | (static_cast<std::uint32_t>(sourceBlend & 15) << 1) |
               (static_cast<std::uint32_t>(destBlend & 15) << 5) | (depthTest ? 1u << 9 : 0u) |
               (depthWrite ? 1u << 10 : 0u) | (static_cast<std::uint32_t>(depthFunc & 15) << 11) |
               (static_cast<std::uint32_t>(cull & 3) << 15);
    }
};

// The old device's state for the pass about to be drawn (what the game code fed, render_state.h).
// `device`: the device object whose bound surfaces become the stages' textures; null: no textures.
PassState ReadPassState(char* device);
// Culling, depth test and write, blending and the colour target of a pipeline as the old device draws with `s`.
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
    PassState state;          // the old device's state for the page (its textures not read)
};

struct CachedMesh;

constexpr float kNoFront = 1e30f;
struct MeshBatch {
    int matrix;
    int firstIndex;           // into the frame's mesh stream, or (cached) the mesh's own indices
    int indexCount;
    PassState state;
    bool distanceFog;         // the old path fogged the vertices by distance
    float fogNear;            // the transform object's fog range: start
    float fogScale;           // 255 / (end - start)
    bool unclipped;           // projected to the screen without clipping by the old path
    float zScale;             // the depth is multiplied by it (1: unchanged)
    bool lines;               // a line list (else triangles)
    bool model = false;       // a model's mesh (BeginMesh .. MeshPass), not pre-transformed vertices
    bool flat = false;        // the flat geometry (decals, markers), no corners
    const void* caster = nullptr;   // the object drawing it (render::BeginCaster), if any
    int light = -1;           // index into Frame::shadow.lights: it samples the shadow map
// A unit's mesh (render::BeginCaster's `figure`): its corners' box in model space (low xyz, high xyz), and
// the nearest map depth of its object's boxes (PrepareShadows; above 1: none).
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

// A window surface's blit: the image read at the blit, the screen and source rectangles.
struct BlitBatch {
    sg_view texture;
    int textureWidth, textureHeight;
    Rect destination;
    Rect source;
};

// A depth clear: the rectangle (screen pixels; right 0: the whole frame) set to `z`.
struct ClearBatch {
    float z;
    Rect rect;
};

// An interface canvas drawn onto the frame (render::ui::DrawCanvas), looked up by its holder when drawn.
struct CanvasBatch {
    const void* holder;
    Rect destination;
    Rect source;
    bool keyed;
    std::uint32_t key;        // 0x00RRGGBB as the canvas holds it
};

struct Command {
    enum Kind : std::uint8_t { Terrain, Mesh, Blit, ClearDepth, Canvas };
    Kind kind;
    int index;                // into terrainBatches / meshBatches / blitBatches / clears / canvasBatches
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

// The scene's light for the GPU lighting (shaders/scene_lighting.hlsli): the sun, the ambient colour and
// the point lights render::SceneLighting kept, as a fragment uniform block.
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
// The scene's point lights as kept: `count` of them, position xyz, 1 / range squared; colour rgb, w: 1
// specular. SceneLightIndex: the kept light at `position` exactly (the same CLight's), -1 none.
const float (*SceneLightPoints(int* count))[2][4];
int SceneLightIndex(const Vector3& position);
// The sun's travel direction as kept (the terrain casters keep the faces turned away from it).
Vector3 SceneSunDirection();
// The direction the shadow map's light travels for the sun `sun`: the sun at least 20 degrees above the
// horizon, unit. False without a direction. Receivers lit on the GPU weigh the map's darkening by their
// facing to it (the map's sun is what casts).
bool MapLightDirection(const Vector3& sun, Vector3* travel);
// `placed` (camera x model, row-major) without the camera: the model matrix. False when the camera has no
// inverse.
bool UnderCamera(const Matrix4& camera, const Matrix4& placed, Matrix4* model);
// A figure part lit on the GPU (render::MeshLighting; shaders/mesh.hlsl), as part of the mesh shader's
// fragment uniforms: the sun and lights in the part's model space, their colours times the material's
// diffuse colour and the coefficient. The lights nearest the part's origin are kept, at most kMostMeshLights.
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

// A frame's object shadows: the sun handed over, the objects casting, and what PrepareShadows made of them:
// the matrices from each receiving mesh's model space (and the terrain's) to the shadow map's clip space,
// and the receivers' shader parameters.
struct ShadowFrame {
    bool haveSun = false;
    Vector3 sun{};
    std::vector<std::pair<const void*, float>> casters;   // object, strength (render::CastsShadow)
    bool prepared = false;    // PrepareShadows ran for this frame
    bool on = false;          // the map is made this frame: receivers sample it
    std::vector<Matrix4> lights;   // [0]: the terrain's
    ShadowParams params{};
    PointShadowFrame point;   // the point lights' shadows (PreparePointShadows)
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
    std::vector<CanvasBatch> canvasBatches;
    std::vector<Command> commands;
// The world view's commands (render::BeginWorld .. EndWorld): [first, end) into `commands`, the last range
// open (end SIZE_MAX) while the world view draws. With the smooth option they are drawn into the
// multisampled target with anisotropic models.
    struct Range {
        std::size_t first, end;
    };
    std::vector<Range> worldRanges;
// The software cursor (render::Cursor), drawn last, after the frame without it is kept for the cursor
// thread.
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
Frame& CurrentFrame();
std::uint32_t FrameNumber();
std::uint32_t CommitNumber();

// Index of `m` in the frame's matrices (added when it differs from the last one).
int AddMatrix(const Matrix4& m);
// The frame's fog (FOGENABLE / FOGCOLOR of the old device), read once per frame.
void ReadFog(Frame& f);
// Render states / stage states of the old device: the switch-over's, which keeps every state the game sets.
void OldRenderStates(const std::uint32_t* states, std::uint32_t* values, int count);
void OldStageStates(int stage, const std::uint32_t* states, std::uint32_t* values, int count);
// The values of render states / of a stage's states for a pass, from the state the game code fed
// (render_state.h), the old device for the rest (logged); PassDone counts the passes that needed the old device.
void RenderValues(const std::uint32_t* states, std::uint32_t* values, int count);
void StageValues(int stage, const std::uint32_t* states, std::uint32_t* values, int count);
void PassDone();
CTextureDevice* DeviceObject();
bool GameTargetSize(int* width, int* height);
// sokol is set up (creates the device on first use); false when the sidecar is off or failed.
bool Started();
ID3D11Device* D3DDevice();
ID3D11DeviceContext* D3DContext();
sg_image FrameImage();
sg_view FrameTexture();
bool FrameSize(int* width, int* height);
// The switch-over's present: the frame into the game window's swap chain through the gamma ramp, with the
// software cursor (`cursor`, null: none) drawn over it, then Present. False: nothing to present into.
bool DrawToWindow(const BlitBatch* cursor);
void PresentWindow();
// What the last DrawToWindow drew with, for the cursor thread's presents: the swap chain and its back
// buffer's view, the present pipeline, the frame's and the ramp's views and the ramp as uploaded (256 RGBA8
// entries), the client area, the frame's size and place, Present's sync interval and flags. Raw pointers, no
// references taken; false when there is no swap chain.
struct WindowParts {
    IDXGISwapChain1* chain;
    ID3D11RenderTargetView* view;
    sg_shader shader;
    sg_pipeline pipeline;
    sg_view frame;
    sg_view ramp;
    const std::uint8_t* rampTable;
    int clientWidth, clientHeight;
    int frameWidth, frameHeight;
    Rect placed;
    std::uint32_t syncInterval;
    std::uint32_t presentFlags;
};
bool CurrentWindowParts(WindowParts* parts);
// The vertical blank switch's rules: whether the system allows tearing presents, the flags of a swap chain
// made when tearing is wanted, Present's sync interval and flags (tools/perf_check checks them).
bool TearingSupported(IDXGIFactory2* factory);
std::uint32_t ChainFlagsFor(bool allowTearing, bool tearingSupported);
std::uint32_t SyncIntervalFor(bool vsync);
std::uint32_t PresentFlagsFor(bool vsync, std::uint32_t chainFlags);
// The game window's swap chain and everything the window module made on the device, released (before
// sg_shutdown); the next DrawToWindow makes them on the new device.
void DropWindow();
// A lost device (DXGI_ERROR_DEVICE_REMOVED / RESET / HUNG, a driver error) was seen: everything is made
// again at the end of the frame. `result`: the DXGI error, `reason`: GetDeviceRemovedReason then (S_OK for
// render::LoseDevice's). DeviceIsLost: seen and not made again yet.
void ReportDeviceLost(long result, long reason);
bool DeviceIsLost();
// render::LoseDevice was called and not taken yet: true once, then false.
bool TakeSimulatedLoss();
#if defined(EI_RENDER_VALIDATION)
// render::RemoveDevice was called: the removable device is removed now (once), right before the game
// window's Present, or at the end of the frame without one.
void RemoveIfAsked();
// Whether the verify build's GPU read-backs (a stall each) run for the `count`th of their kind: the first
// kReadBackAlways of each kind, then every 16th, and every one with "checkall" in EI_RENDER.
constexpr long kReadBackAlways = 64;
bool ReadBackDue(long count);
#endif
void DeviceLossCounts(WindowPresentInfo* info);

// A vertex or index buffer filled with sg_append_buffer, so a frame can be drawn in several passes; replaced
// by a larger one when an append does not fit.
class StreamBuffer {
public:
    StreamBuffer(bool index, const char* label) : index_(index), label_(label) {}
    int Append(const void* data, std::size_t bytes);
    sg_buffer Buffer() const { return buffer_; }
    void Destroy();

private:
    bool index_;
    const char* label_;
    sg_buffer buffer_{};
    std::size_t capacity_ = 0;
    std::size_t used_ = 0;                // appended since the last commit
    std::uint32_t commit_ = 0xFFFFFFFFu;  // CommitNumber() of the last append
};
void RetireBuffers();

// A frame's vertices and indices (absolute indices) on the GPU, appended again when they grew since the last
// pass of the frame.
class GeometryStream {
public:
    // `terrain`: counted as the terrain's uploads (FrameStats), else the meshes'.
    GeometryStream(const char* label, bool terrain) : vertices_(false, label), indices_(true, label), terrain_(terrain) {}
    bool Upload(const std::vector<Vertex>& vertices, const std::vector<std::uint32_t>& indices);
    bool Bind(sg_bindings& b) const;
    void Destroy();

private:
    StreamBuffer vertices_, indices_;
    bool terrain_;
    std::uint32_t frame_ = 0xFFFFFFFFu;
    std::size_t vertexCount_ = 0, indexCount_ = 0;
    int vertexOffset_ = -1, indexOffset_ = -1;
};

// Where an item sits in a GpuPool: the pool's generation when it was placed (0: never), its first vertex and
// index and the room it has there.
struct PoolPlace {
    std::uint32_t generation = 0;
    std::uint32_t firstVertex = 0, vertexRoom = 0, firstIndex = 0, indexRoom = 0;
};

// Vertices and indices kept on the GPU across frames (terrain pages, meshes): a geometry stream
// (`geometryStride` bytes a vertex), a colour stream (kColourPairBytes a vertex) and absolute 32-bit indices,
// in default-usage Direct3D 11 buffers written in place with UpdateSubresource (sokol has no partial
// update). Items are placed one after the other; when one does not fit, the pool starts again (Restart) with
// the items still needed, and is made larger when they need more than half of it.
constexpr std::size_t kColourPairBytes = 2 * sizeof(std::uint32_t);
class GpuPool {
public:
    GpuPool(std::size_t geometryStride, std::uint32_t leastVertices, std::uint32_t leastIndices, const char* label)
        : geometryStride_(geometryStride), leastVertices_(leastVertices), leastIndices_(leastIndices), label_(label) {}
// Room for an item of `vertices` and `indices`: where it is when it still fits there, else at the pool's
// end (`anew`: all of it to write). False when the pool is full.
    bool Place(PoolPlace& p, std::uint32_t vertices, std::uint32_t indices, bool* anew);
    bool Placed(const PoolPlace& p) const { return p.generation == generation_ && geometry_.buffer.id != 0; }
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
    std::size_t geometryStride_;
    std::uint32_t leastVertices_, leastIndices_;
    const char* label_;
    Buffer geometry_, colours_, indices_;
    std::uint32_t vertexCapacity_ = 0, indexCapacity_ = 0;
    std::uint32_t usedVertices_ = 0, usedIndices_ = 0;
    std::uint32_t generation_ = 1;
    std::vector<std::uint32_t> absolute_;   // scratch: an item's indices made absolute
};

// The terrain shader and draws, inside EndFrame's off-screen pass. UploadTerrain puts the pages handed over
// with changes into the pool; TerrainPositions gives every `step`th position of the frame's terrain vertices
// in draw order (the shadow map's fit); TerrainFrameDone, after a present, drops pages not drawn for a long
// time. DestroyTerrain keeps the pages (placed again at their next draw).
bool CreateTerrainShader();
void UploadTerrain(const Frame& frame);
void DrawTerrainBatch(const Frame& frame, const TerrainBatch& batch);
void TerrainPositions(const Frame& frame, std::size_t step, std::vector<Vector3>* out);
void TerrainFrameDone();
void DestroyTerrain();
// The ground's casters into the shadow map (render::ShadowTerrainSector): UploadTerrainCasters writes the
// sectors handed over this frame into their pool; DrawTerrainCasters draws them with `pipeline` (vertex
// buffer 0: TerrainCasterVertex, 32-bit indices) whose uniforms the caller applied.
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
void DrawMeshBatch(const Frame& frame, const MeshBatch& batch, bool anisotropic);
void DestroyMeshes();

// The vertex and index buffers a mesh batch draws from (the frame's stream, or the pool of the kept meshes)
// and its first index there; false when it has none. MeshLayout: a pipeline's vertex layout for such a
// batch; `positionsOnly`: the position and texture coordinates alone (the casters).
bool BindMeshBatch(const MeshBatch& batch, sg_bindings& b, int* firstIndex);
void MeshLayout(sg_pipeline_desc& p, bool cached, bool positionsOnly);

// Meshes kept on the GPU (docs/notes/renderer.md section 13). KeepMesh: the mesh being handed over brought up
// to date as kept for the figure mesh of render::SetMeshSource, or null: it is built into the frame's stream
// (no source, no corners, the cache off in the harness build, or the mesh already drawn this frame with other
// data). UploadCachedMeshes writes what changed into the pool; MeshCacheFrameDone drops meshes not drawn for a
// long time; DropMeshCache gives up the pool (the meshes are placed and written again at their next draw).
constexpr std::size_t kCachedVertexBytes = 32;   // a kept corner's geometry: position, texture coordinates, normal
void NoteMeshDraw(const MeshDraw& mesh);
CachedMesh* KeepMesh(const MeshDraw& mesh);
int CachedIndexCount(const CachedMesh& mesh);
void UploadCachedMeshes(const Frame& frame);
bool BindCachedMesh(const CachedMesh& mesh, sg_bindings& b, int* firstIndex);
void MeshCacheFrameDone();
void DropMeshCache();
const void* MeshSourceOwner();
const MeshLighting* MeshSourceLighting();
// The model-space normal of the corners with colour index `index`, zero without normals or words.
Vector3 CornerNormal(const MeshDraw& mesh, int index);

// Object shadows (docs/notes/renderer.md section 13). PrepareShadows, once a frame before its first pass on
// the world: fits the shadow map around the visible ground seen from the sun and gives every receiving batch
// its light matrix; DrawShadowMap draws the casters into the map and its depth range per cell. The receivers'
// shaders include shaders/shadow_receiver.hlsli, take ShadowParams in their uniforms and bind the map (or a
// cleared 1 x 1 one) with BindShadowMap.
void PrepareShadows(Frame& frame);
void DrawShadowMap(Frame& frame);
void ShadowUniforms(const Frame& frame, bool receives, ShadowParams* params);
void BindShadowMap(sg_bindings& b, int viewSlot, int samplerSlot);
// The shader descriptions of the map's texture (a depth view) and comparison sampler at the slots, and of its
// depth range per cell (RGBA32F, read filtered) with its sampler at the slots after them.
void DescribeShadowMap(sg_shader_desc& s, int viewSlot, int samplerSlot);
void DestroyShadows();
// The world's camera this frame: the clip matrix of its first ground batch and its position in the
// landscape's space; false without ground drawn.
bool WorldCamera(const Frame& frame, Matrix4* clip, Vector3* eye);
// A depth image with an attachment and a texture view; the shadow maps' comparison sampler; a 1 x 1 depth
// texture at the far plane, for a receiver drawn without a map.
bool MakeShadowDepth(int width, int height, const char* label, sg_image* image, sg_view* attachment, sg_view* texture);
sg_sampler ShadowCompareSampler();
sg_view NoShadowTexture();
// Point-light shadows (docs/notes/renderer.md section 13, item 9). PreparePointShadows (from PrepareShadows)
// picks the strongest point lights in view that something shadows, and gives the meshes lit on the GPU their
// matrix into the landscape's space; DrawPointShadows draws the casters into the atlas. The receivers take
// PointShadowUniforms and bind the atlas with BindPointShadows; PointShadowSlot: the map of a kept scene
// light, -1 none.
void PreparePointShadows(Frame& frame);
void DrawPointShadows(Frame& frame);
void PointShadowUniforms(const Frame& frame, PointShadowParams* params);
void BindPointShadows(sg_bindings& b, int viewSlot, int samplerSlot);
void DescribePointShadows(sg_shader_desc& s, int viewSlot, int samplerSlot);
int PointShadowSlot(const Frame& frame, int sceneLight);
void DestroyPointShadows();
// The game's hand-over: the object whose meshes are being handed over. ShadowFrameEnd: the frame's passes are
// recorded (the harness build's GPU time); ShadowFrameDone: the frame is presented.
const void* CurrentCaster();
bool CurrentCasterFigure();
void NoteCasterPart(const void* part);
void ShadowFrameEnd();
void ShadowFrameDone();

// The window surfaces' blits and the depth clear in the middle of the pass.
bool CreateBlitPipeline();
void DrawBlitBatch(const Frame& frame, const BlitBatch& batch, int width, int height);
void DrawClearDepth(const ClearBatch& clear, int width, int height);
void RetireFrameImages();
void DestroyBlits();
struct Picture;
// render::Cursor's image: the whole cursor picture of `holder` (all its frames side by side) as RGBA8, alpha
// 255 where the mask (`maskBytes` per pixel, the picture's width) is clear, else 0; kept for the cursor
// thread. The part `from` copied into `rgba`; false when the picture or the rectangle does not fit.
bool CursorPicturePart(const void* holder, const void* mask, int maskBytes, const Picture& picture, const Rect& from,
                 std::vector<std::uint8_t>* rgba);
// DestroyBlits, and every image kept across frames (movies, the cursor's and fills' cache): the device is lost.
void DropBlits();

// The counts of the frame being built (render::FrameStats; summed at Present). StatsTimer adds the CPU time
// of its scope to one of the stats' fields; a timer inside another's scope takes its time out of the outer
// one's field.
FrameStats& Stats();
class StatsTimer {
public:
    explicit StatsTimer(double FrameStats::* field);
    ~StatsTimer();
    StatsTimer(const StatsTimer&) = delete;
    StatsTimer& operator=(const StatsTimer&) = delete;

private:
    double FrameStats::* field_;
    long long start_;
    StatsTimer* outer_;
};
// Counts an applied pipeline (a change when it differs from the last one applied in the frame), the bindings
// with their first texture view, a draw with its uniform bytes.
void CountPipeline(sg_pipeline pipeline);
void CountBindings(const sg_bindings& b);
void CountDraw(std::size_t uniformBytes);
void CountUpload(FrameStats::Upload kind, std::size_t bytes);
// Compares `count` vertices handed over for `key` (a terrain page: sector and page; a mesh: its positions,
// page 0) with the last frame's for the same key.
void CountReuse(bool terrain, const void* key, int page, const Vertex* vertices, std::size_t count);

// The interface canvases (render_ui.cpp). RunCanvasCommands draws every queued canvas command outside any
// pass (at the start of DrawPending, under the present's lock); CanvasBlitsDrawn: the commands recorded so far
// are drawn; DropCanvases: the device is lost (the canvases come back black at their next command).
void RunCanvasCommands();
void CanvasBlitsDrawn();
void DrawCanvasBatch(const CanvasBatch& batch, int width, int height);
void DropCanvases();
// SplitFrame draws the frame's commands recorded so far (a pass split; not between frames); RunCanvasesNow
// runs RunCanvasCommands at once, under the present's lock.
void SplitFrame();
void RunCanvasesNow();

// Counts of the images the renderer made and updated in place by purpose, logged in the verify build.
enum class ImageUse { Cursor, Fill, Movie, Window, Texture, Count };
void CountImage(ImageUse use, bool made);
void CountImageRequest(ImageUse use);
void LogImageCounts(const char* when);

// The game texture `texture` (a CTexture) or a surface holder (CDXSurface) as a texture view for sampling,
// with its mip levels: for a holder with SurfaceMemory read from it (again when that memory changed), else
// what was decoded for the holder. A white texture when there is none. Valid until the next Present.
sg_view TextureView(const void* texture);
sg_view SurfaceView(const void* holder);
// The texture of `holder` made from CPU pixels (render::TexturePixels), kept until the holder goes; nothing
// for a holder with SurfaceMemory.
bool TextureFromPixels(const void* holder, const void* pixels, int width, int height, int pitch, int bitsPerPixel,
                       std::uint32_t redMask, std::uint32_t greenMask, std::uint32_t blueMask, std::uint32_t alphaMask);
// The texture of `holder` decoded from .mmp file data (render::TextureFromMmp), kept until the holder goes.
bool TextureFromMmpData(const void* holder, const void* data, unsigned mip, unsigned levels);
void ForgetHolder(const void* holder);
// A window surface (CUiSurface's holder) as one level, read from its SurfaceMemory when that changed (none
// without memory); texels equal to `key` (the surface's pixel format) transparent when `keyed`.
sg_view WindowSurfaceView(const void* holder, bool keyed, std::uint32_t key, int* width, int* height);
// A picture loaded into a surface holder (render::PictureFromMmp): level 0 as RGBA8, kept until the holder goes.
struct Picture {
    int width = 0, height = 0;
    std::vector<std::uint8_t> rgba;
};
bool PictureFromMmpData(const void* holder, const void* data);
const Picture* PictureOf(const void* holder);
sg_sampler LinearSampler();

// The cursor between frames. The swap chain's lock: the main thread's present on the game window
// (BeginWindowPresent .. EndWindowPresent) and the cursor thread's (render::CursorBetweenFrames) take turns.
// BeginWindowPresent comes before anything draws into the frame target (the cursor thread draws it itself,
// with its cursor, only while no pass is in progress); EndWindowPresent takes the references the cursor thread
// draws with. ProtectDevice turns on the device's multithread protection (the cursor thread's command list
// runs on the immediate context); DropCursorPresents releases everything before the device goes.
void ProtectDevice(ID3D11Device* device, ID3D11DeviceContext* context);
void BeginWindowPresent();
void EndWindowPresent(bool presented);
void DropCursorPresents();
sg_sampler StageSampler(const StageState& stage);
// The sampler for `stage` with linear filters asked for: anisotropic (kAnisotropy) with linear mips; point
// filters as the stage says. Always for the terrain pages, with the smooth option for the world's models too.
constexpr std::uint32_t kAnisotropy = 16;
sg_sampler AnisotropicSampler(const StageState& stage);
// After a frame is presented: images replaced during the frame are destroyed, entries unused for a long time
// dropped.
void RetireTextures();
void DestroyTextures();
// The device is lost: every texture image destroyed (before sg_shutdown). Textures decoded for a holder keep
// what they were decoded from and are made again at their next use; `kept` of them, with `keptBytes` of
// sources. The rest is read again from its memory.
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
    unsigned int saved_;
};

constexpr sg_pixel_format kColourFormat = SG_PIXELFORMAT_RGBA8;
constexpr sg_pixel_format kDepthFormat = SG_PIXELFORMAT_DEPTH;

// The smooth option's multisampling (render.cpp draws the frame's world commands into a multisampled target
// and resolves it into the frame target, docs/notes/renderer.md section 11). PassSampleCount: the sample count
// of the pass being drawn (1, or the multisampled target's): every pipeline drawing into it is made for it.
int PassSampleCount();
// The most samples up to kSmoothSamples the device renders kColourFormat and kDepthFormat with (1: none).
constexpr int kSmoothSamples = 8;
int SupportedSamples(ID3D11Device* device);
// The two copies below made for a multisampled target of `samples`; false (logged) when they could not be:
// the world is then drawn without multisampling.
bool PrepareSmoothCopies(int samples);
// Inside a pass on the multisampled target: the frame target's colour and depth (texture views of
// the single-sampled images) copied into every sample.
void CopyIntoMultisampled(sg_view colour, sg_view depth);
// Inside a pass on the frame target: its depth set to sample 0 of the multisampled depth (a texture
// view of it); the colour comes from the multisampled pass's resolve.
void CopyDepthFromMultisampled(sg_view depth);
void DestroySmooth();

} // namespace render::detail
