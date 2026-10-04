#pragma once

// Internals shared by the new renderer's files (render.cpp: device, frame, targets, window, the
// switches; render_terrain.cpp: terrain; render_mesh.cpp: meshes (figures, objects, the sky, the
// interface's sprites and 3D previews); render_blit.cpp: the interface windows' surfaces;
// render_textures.cpp: game textures and surfaces as sokol images).

#include "render.h"
#include "sokol_gfx.h"

#include <cstdint>
#include <vector>

namespace render::detail {

// A 4x4 matrix as the game's transform pipeline keeps it: row-major, out = M * (x, y, z, 1), the
// rows giving clip x, y, z and w (Direct3D clip space: -w <= x, y <= w, 0 <= z <= w).
struct Matrix {
    float m[16];
};

// A vertex of the new renderer: world (terrain) or model (meshes) position, the game's lit colours,
// texture coordinates.
struct Vertex {
    float x, y, z;
    std::uint32_t colour;     // D3DCOLOR (bytes B, G, R, A)
    std::uint32_t specular;   // D3DCOLOR; its alpha is the fog factor (255 no fog)
    float u, v;
};
static_assert(sizeof(Vertex) == 28);
using TerrainVertex = Vertex;

struct TerrainBatch {
    const void* texture;      // the game's CTexture
    bool water;               // the water pass (blended over what is drawn)
    int matrix;               // index into Frame::matrices
    int firstIndex;
    int indexCount;
};

// One texture stage of the old device for a pass (D3DTEXTURESTAGESTATETYPE values).
struct StageState {
    std::uint8_t colourOp = 1, colourArg1 = 2, colourArg2 = 1;   // D3DTOP_DISABLE, TEXTURE, CURRENT
    std::uint8_t alphaOp = 1, alphaArg1 = 2, alphaArg2 = 1;
    std::uint8_t magFilter = 2, minFilter = 2, mipFilter = 1;     // D3DTFG / D3DTFN / D3DTFP
    std::uint8_t addressU = 1, addressV = 1;                      // D3DTADDRESS_WRAP
    sg_view texture{};                                            // the bound surface, read at the pass
    bool hasTexture = false;
};

// The old device's state for one pass of a mesh draw: what the new renderer needs to draw it the
// same way. Blend, depth and culling make the pipeline; the rest goes to the shader.
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

    std::uint32_t PipelineKey() const {
        return (blend ? 1u : 0u) | (static_cast<std::uint32_t>(sourceBlend & 15) << 1) |
               (static_cast<std::uint32_t>(destBlend & 15) << 5) | (depthTest ? 1u << 9 : 0u) |
               (depthWrite ? 1u << 10 : 0u) | (static_cast<std::uint32_t>(depthFunc & 15) << 11) |
               (static_cast<std::uint32_t>(cull & 3) << 15);
    }
};

struct MeshBatch {
    int matrix;
    int firstIndex;
    int indexCount;
    PassState state;
    bool distanceFog;         // the old path fogged the vertices by distance (device +0x158)
    float fogNear;            // the transform object's fog range: start
    float fogScale;           // 255 / (end - start)
    bool unclipped;           // projected to the screen without clipping by the old path
    float zScale;             // the depth is multiplied by it (1: unchanged)
    bool lines;               // a line list (else triangles)
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

// The draws of a frame in the order the game made them.
struct Command {
    enum Kind : std::uint8_t { Terrain, Mesh, Blit, ClearDepth };
    Kind kind;
    int index;                // into terrainBatches / meshBatches / blitBatches / clears
};

struct Frame {
    std::vector<Matrix> matrices;
    std::vector<Vertex> terrainVertices;
    std::vector<std::uint32_t> terrainIndices;
    std::vector<TerrainBatch> terrainBatches;
    std::vector<Vertex> meshVertices;
    std::vector<std::uint32_t> meshIndices;
    std::vector<MeshBatch> meshBatches;
    std::vector<BlitBatch> blitBatches;
    std::vector<ClearBatch> clears;
    std::vector<Command> commands;
    bool haveFog = false;
    bool fogEnabled = false;
    std::uint32_t fogColour = 0;   // D3DCOLOR
    // Direct3D 7 samples a pixel at its integer screen coordinates, Direct3D 11 half a pixel
    // further right and down: the 3D draws move their clip positions by half a pixel (x by
    // +1 / width, y by -1 / height, times w) so that the same screen coordinates get the same
    // pixels and the same interpolated texture coordinates and colours. Set by EndFrame.
    float pixelShift[2] = {0.0f, 0.0f};
};
Frame& CurrentFrame();
// Counts presented frames (texture caches age their entries by it).
std::uint32_t FrameNumber();
// Counts sg_commit calls (sokol's frames).
std::uint32_t CommitNumber();

// Index of `m` in the frame's matrices (added when it differs from the last one).
int AddMatrix(const float* m);
// The frame's fog (FOGENABLE / FOGCOLOR of the old device), read once per frame.
void ReadFog(Frame& f);
// Render states / stage 0..1 texture stage states of the old device (GetRenderState /
// GetTextureStageState), several under one hold of the device lock.
void OldRenderStates(const std::uint32_t* states, std::uint32_t* values, int count);
void OldStageStates(int stage, const std::uint32_t* states, std::uint32_t* values, int count);
// The values of render states / of a stage's states for a pass, from the state the game code fed
// (render_state.h), the old device for the rest (render_state.cpp; logged); PassDone after each
// pass counts the passes that still needed the old device.
void RenderValues(const std::uint32_t* states, std::uint32_t* values, int count);
void StageValues(int stage, const std::uint32_t* states, std::uint32_t* values, int count);
void PassDone();
// Inside the world view's draw (render::BeginWorld .. EndWorld).
bool InWorld();
// The device object (007AF080) and its render target's size.
char* DeviceObject();
bool GameTargetSize(int* width, int* height);
// sokol is set up (creates the device on first use); false when the sidecar is off or failed.
bool Started();

// A vertex or index buffer filled with sg_append_buffer, so that a frame can be drawn in several
// passes (a copy of the frame taken in its middle, CopyFrameTo); replaced by a larger one when an
// append does not fit (render_terrain.cpp).
class StreamBuffer {
public:
    StreamBuffer(bool index, const char* label) : index_(index), label_(label) {}
    // Appends `bytes` of `data`: the data's offset in Buffer(), or -1.
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
// Destroys the buffers replaced during the frame (after its commit).
void RetireBuffers();

// A frame's vertices and indices (absolute indices) on the GPU: appended again when they grew since
// the last pass of the frame.
class GeometryStream {
public:
    explicit GeometryStream(const char* label) : vertices_(false, label), indices_(true, label) {}
    bool Upload(const std::vector<Vertex>& vertices, const std::vector<std::uint32_t>& indices);
    // The vertex and index buffers with the last upload's offsets; false when there is none.
    bool Bind(sg_bindings& b) const;
    void Destroy();

private:
    StreamBuffer vertices_, indices_;
    std::uint32_t frame_ = 0xFFFFFFFFu;
    std::size_t vertexCount_ = 0, indexCount_ = 0;
    int vertexOffset_ = -1, indexOffset_ = -1;
};

// The terrain pipelines and draws, inside EndFrame's off-screen pass (render_terrain.cpp).
bool CreateTerrainPipelines();
void UploadTerrain(const Frame& frame);
void DrawTerrainBatch(const Frame& frame, const TerrainBatch& batch);
void DestroyTerrain();

// The mesh pipelines and draws (render_mesh.cpp).
bool CreateMeshShader();
void UploadMeshes(const Frame& frame);
void DrawMeshBatch(const Frame& frame, const MeshBatch& batch);
void DestroyMeshes();

// The window surfaces' blits and the depth clear in the middle of the pass (render_blit.cpp).
bool CreateBlitPipeline();
void DrawBlitBatch(const Frame& frame, const BlitBatch& batch, int width, int height);
void DrawClearDepth(const ClearBatch& clear, int width, int height);
// Destroys the images made for one frame (the cursor), after its commit.
void RetireFrameImages();
void DestroyBlits();

// The game texture `texture` (a CTexture) or a surface holder (CDXSurface, what CTexture +0x5C
// points at) as a texture view for sampling, with its mip levels; re-read when the surface's
// contents changed (IDirectDrawSurface7 uniqueness value). A white texture when it cannot be read
// (render_textures.cpp). Valid until the next Present.
sg_view TextureView(const void* texture);
sg_view SurfaceView(const void* holder);
// The texture of `holder` gets the pixels of `source` (another surface holder) now; it is not read
// from its own surface again until that changes (render::TextureChanged).
void ReadTextureFrom(const void* holder, const void* source);
// The texture of `holder` decoded from .mmp file data (render::TextureFromMmp).
bool TextureFromMmpData(const void* holder, const void* data, unsigned mip, unsigned levels);
// A window surface (CUiSurface's holder) as one level, read now when its contents changed; texels
// equal to `key` (the surface's pixel format) transparent when `keyed`.
sg_view WindowSurfaceView(const void* holder, bool keyed, std::uint32_t key, int* width, int* height);
sg_sampler LinearSampler();
sg_sampler StageSampler(const StageState& stage);
// After a frame is presented: images replaced during the frame are destroyed, entries unused for a
// long time dropped.
void RetireTextures();
void DestroyTextures();

// The game's device lock (007AEE78), held around DirectDraw calls on the game's surfaces.
void LockGameDevice();
void UnlockGameDevice();

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

// Pixel formats of the off-screen pass (render.cpp).
constexpr sg_pixel_format kColourFormat = SG_PIXELFORMAT_RGBA8;
constexpr sg_pixel_format kDepthFormat = SG_PIXELFORMAT_DEPTH;

} // namespace render::detail
