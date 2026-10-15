#pragma once

// Internals shared by the new renderer's files (render.cpp: device, frame, targets, window, the
// switches; render_terrain.cpp: terrain; render_mesh.cpp: meshes (figures, objects, the sky, the
// interface's sprites and 3D previews); render_blit.cpp: the interface windows' surfaces;
// render_textures.cpp: game textures and surfaces as sokol images).

#include "render.h"
#include "sokol_gfx.h"

#include "lib/vecmath.h"

#include <cstdint>
#include <vector>

class CTextureDevice;   // game/texture.h
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;
struct IDXGISwapChain1;

namespace render::detail {

// The game's transform pipeline matrices are Matrix4 (src/lib/vecmath.h): row-major, out =
// M * (x, y, z, 1), the rows giving clip x, y, z and w (Direct3D clip space: -w <= x, y <= w,
// 0 <= z <= w).

// A vertex of the new renderer: world (terrain) or model (meshes) position, the game's lit colours,
// texture coordinates.
struct Vertex {
    Vector3 position;
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
    std::vector<Matrix4> matrices;
    std::vector<Vertex> terrainVertices;
    std::vector<std::uint32_t> terrainIndices;
    std::vector<TerrainBatch> terrainBatches;
    std::vector<Vertex> meshVertices;
    std::vector<std::uint32_t> meshIndices;
    std::vector<MeshBatch> meshBatches;
    std::vector<BlitBatch> blitBatches;
    std::vector<ClearBatch> clears;
    std::vector<Command> commands;
    // The software cursor (render::Cursor) when the new renderer presents itself: drawn last, after
    // the frame without it is kept for the cursor thread (render_cursor.cpp).
    BlitBatch cursor{};
    bool haveCursor = false;
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
int AddMatrix(const Matrix4& m);
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
CTextureDevice* DeviceObject();
bool GameTargetSize(int* width, int* height);
// sokol is set up (creates the device on first use); false when the sidecar is off or failed.
bool Started();
// The Direct3D 11 device and context behind sokol, the off-screen frame target (image, texture view,
// size: false before the first frame).
ID3D11Device* D3DDevice();
ID3D11DeviceContext* D3DContext();
sg_image FrameImage();
sg_view FrameTexture();
bool FrameSize(int* width, int* height);
// The switch-over's present (render_window.cpp): the frame into the game window's swap chain (false:
// nothing to present into: no window, minimised, the swap chain or device failed), then Present.
bool DrawToWindow();
void PresentWindow();
// What the last DrawToWindow drew with, for the cursor thread's presents (render_cursor.cpp): the
// swap chain and its back buffer's view, the present pipeline (the frame through the gamma ramp) and
// the ramp's view, the client area and the frame's place in it, whether Present waits for the
// vertical blank. Raw pointers, no references taken; false when there is no swap chain.
struct WindowParts {
    IDXGISwapChain1* chain;
    ID3D11RenderTargetView* view;
    sg_shader shader;
    sg_pipeline pipeline;
    sg_view ramp;
    int clientWidth, clientHeight;
    Rect placed;
    bool vsync;
};
bool CurrentWindowParts(WindowParts* parts);
// The game window's swap chain and everything the window module made on the device, released
// (before sg_shutdown: the device is lost); the next DrawToWindow makes them on the new device.
void DropWindow();
// A lost device (DXGI_ERROR_DEVICE_REMOVED / RESET / HUNG, a driver error) was seen: everything is
// made again at the end of the frame (render.cpp). DeviceIsLost: seen and not made again yet.
void ReportDeviceLost();
bool DeviceIsLost();
// render::LoseDevice was called and not taken yet: true once, then false.
bool TakeSimulatedLoss();
void DeviceLossCounts(int* losses, int* recoveries);

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
// The window blit's shader, pipeline and sampler (the cursor thread draws the cursor with them).
void BlitParts(sg_shader* shader, sg_pipeline* pipeline, sg_sampler* sampler);
struct Picture;
// render::Cursor's image: the whole cursor picture of `holder` (all its frames side by side) as RGBA8,
// alpha 255 where the mask (`mask`, `maskBytes` per pixel, the picture's width) is clear, else 0;
// kept for the cursor thread (render_cursor.cpp). The part `from` copied into `rgba` (w x h); false
// when the picture or the rectangle does not fit.
bool CursorPicturePart(const void* holder, const void* mask, int maskBytes, const Picture& picture, const Rect& from,
                 std::vector<std::uint8_t>* rgba);
// DestroyBlits, and every image kept across frames (movies, the cursor's and fills' cache): the
// device is lost.
void DropBlits();

// Counts of the images the renderer made (sg_make_image) and updated in place (sg_update_image)
// by purpose, logged in the verify build (render_blit.cpp).
enum class ImageUse { Cursor, Fill, Movie, Window, Texture, Count };
void CountImage(ImageUse use, bool made);
void CountImageRequest(ImageUse use);
void LogImageCounts(const char* when);

// The game texture `texture` (a CTexture) or a surface holder (CDXSurface, what CTexture +0x5C
// points at) as a texture view for sampling, with its mip levels; re-read when the surface's
// contents changed (IDirectDrawSurface7 uniqueness value; for a holder with SurfaceMemory, when that
// memory changed). A white texture when it cannot be read (render_textures.cpp). Valid until the
// next Present.
sg_view TextureView(const void* texture);
sg_view SurfaceView(const void* holder);
// The texture of `holder` made from CPU pixels (render::TexturePixels); it is not read from its
// surface again until that changes. Nothing for a holder with SurfaceMemory (read from there).
bool TextureFromPixels(const void* holder, const void* pixels, int width, int height, int pitch, int bitsPerPixel,
                       std::uint32_t redMask, std::uint32_t greenMask, std::uint32_t blueMask, std::uint32_t alphaMask);
// The texture of `holder` decoded from .mmp file data (render::TextureFromMmp), keyed by the holder:
// for a holder with a DirectDraw surface until that surface changes, for one without (the
// switch-over) until the holder goes.
bool TextureFromMmpData(const void* holder, const void* data, unsigned mip, unsigned levels);
// The holder is gone (render::HolderGone): its textures and window texture are retired.
void ForgetHolder(const void* holder);
// A window surface (CUiSurface's holder) as one level, read now when its contents changed (from its
// SurfaceMemory when it has some: a holder made in the switch-over has nothing else); texels equal
// to `key` (the surface's pixel format) transparent when `keyed`.
sg_view WindowSurfaceView(const void* holder, bool keyed, std::uint32_t key, int* width, int* height);
// A picture loaded into a surface holder (render::PictureFromMmp): level 0 as RGBA8, kept until
// the holder goes (render::HolderGone).
struct Picture {
    int width = 0, height = 0;
    std::vector<std::uint8_t> rgba;
};
bool PictureFromMmpData(const void* holder, const void* data);
const Picture* PictureOf(const void* holder);
sg_sampler LinearSampler();

// The cursor between frames (render_cursor.cpp). The swap chain's lock: the main thread's present on
// the game window (BeginWindowPresent .. EndWindowPresent) and the cursor thread's
// (render::CursorBetweenFrames) take turns. In between the main thread keeps its frame without the
// cursor (KeepFrameForCursor, after DrawPending); EndWindowPresent takes the references the cursor
// thread draws with (when the frame was presented). ProtectDevice turns on the device's multithread
// protection (the cursor thread's command list runs on the immediate context); DropCursorPresents
// releases everything before the device goes.
void ProtectDevice(ID3D11Device* device, ID3D11DeviceContext* context);
void BeginWindowPresent();
void KeepFrameForCursor();
void EndWindowPresent(bool presented);
void DropCursorPresents();
sg_sampler StageSampler(const StageState& stage);
// After a frame is presented: images replaced during the frame are destroyed, entries unused for a
// long time dropped.
void RetireTextures();
void DestroyTextures();
// The device is lost: every texture image destroyed (before sg_shutdown). Textures decoded for a
// holder with nothing to read again (the switch-over's .mmp textures) keep what they were decoded
// from and are made again at their next use; `kept` of them, with `keptBytes` of sources. The rest
// is read again from its surface or memory.
void DropTextures(std::size_t* kept, std::size_t* keptBytes);

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
