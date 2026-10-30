// The Direct3D device's indexed mesh draws, with one texture and with two: the mesh's positions are projected by
// the pipeline for the visibility verdict and the screen rectangle picking reads. The new
// renderer gets the mesh in model space once it is visible and clips it. See docs/notes/models.md ("Renderer").
#include "device_buffers.h"

#include "device_material.h"
#include "figure_mesh.h"
#include "model.h"
#include "settings.h"
#include "texture.h"
#include "lib/nan_audit.h"
#include "render/render.h"

#include <cstdint>
#include <float.h>

namespace {

// A corner's position indexes the projected vertices, its normal the colour and specular arrays (one
// colour per normal) and its texCoord the texture coordinates.
// The arguments both draws share (the two-texture draw has the two textures before them).
struct MeshArgs {
    const VertexPacket* positions;   // a block of four vertices each
    int blocks;
    const NormalPacket* normals;     // blocks of four as the positions
    int colourCount;              // the colour arrays hold four times this (and so many normal blocks)
    std::uint32_t* colours;
    std::uint32_t* speculars;
    const TexCoord* uvs;
    int unused10;
    const MeshCorner* corners;
    int cornerCount;
    const std::uint32_t* words;   // the words' address: (position, normal) index pairs, by colour index
    int wordCount;
    std::uint16_t* indices;       // into the corners
    int indexCount;
    const float* uOffset;         // the arguments themselves (their bits are compared and added)
    const float* vOffset;
    int alpha;                    // the low byte, with flag 4
    int flags;                    // 1 screen bounds, 2 clip, 4 colours from the device (colour) and alpha
};

struct Textures {
    Surface* first;
    Surface* second;
};

// --- The buffers ---------------------------------------------------------------------------------

using devicebuffers::g_projected;
using devicebuffers::g_outcodes;

// The pipeline's screen stack (1) and clip-space stack (2), whose top matrices project the mesh.
constexpr int kScreenStack = 1;
constexpr int kClipStack = 2;

// --- The steps -----------------------------------------------------------------------------------

// Flag 4: every colour becomes the device's (colour), every specular the alpha byte alone.
void OverrideColours(TextureDevice* device, const MeshArgs& args) {
    std::uint32_t colour = device->colour;
    std::uint32_t specular = (static_cast<std::uint32_t>(args.alpha) & 0xFF) << 24;
    int count = args.colourCount * 4;
    for (int i = 0; i < count; ++i) args.colours[i] = colour;
    for (int i = 0; i < count; ++i) args.speculars[i] = specular;
}

// One counted draw per pass (the device's vertex-buffer batch never gave a buffer: nothing is batched).
void Submit(TextureDevice* device, const Textures* textures) {
    if (textures) {
        device->DrawTwoTextures(textures->first, textures->second);
    } else {
        device->DrawIndexed();
    }
}

// On screen or crossing the view's edges: draw flag 1's rectangle from the projected positions (screen positions,
// or clip-space ones when the mesh crosses the edges), and the passes go to the device; the GPU clips the hand-over.
void Draw(TextureDevice* device, const MeshArgs& args, const Textures* textures, bool crossing) {
    if (args.flags & 1) device->BoundsOfProjected(&g_projected, crossing);
    Submit(device, textures);
}

void HandOver(TextureDevice* device, const MeshArgs& args) {
    render::MeshDraw m{};
    m.positions = &args.positions->block[0][0];
    m.positionCount = args.blocks * 4;
    m.colours = args.colours;
    m.speculars = args.speculars;
    m.uvs = &args.uvs->u;
    m.corners = &args.corners->position;
    m.cornerCount = args.cornerCount;
    m.indices = args.indices;
    m.indexCount = args.indexCount;
    EI_NAN_AUDIT(*args.uOffset, *args.vOffset);
    m.moveUv = *args.uOffset != 0.0f || *args.vOffset != 0.0f;
    m.uOffset = *args.uOffset;
    m.vOffset = *args.vOffset;
    // The screen stack (1): the clip-space stack (2, which the clipping draws use) times the
    // viewport mapping, so it also places meshes drawn into a part of the screen (the interface's
    // previews). Both stacks get the same pushes.
    m.screenMatrix = true;
    m.matrix = device->pipeline->Top(kScreenStack);
    m.clipped = (args.flags & 2) != 0;
    m.distanceFog = device->distanceFog != 0;
    m.zScale = 1.0f;
    m.coloursWritten = (args.flags & 4) != 0;   // OverrideColours
    // A figure part lit on the GPU needs its normals.
    m.normals = &args.normals->block[0][0];
    m.normalCount = args.colourCount * 4;
    m.words = reinterpret_cast<const std::uint16_t*>(args.words);   // (position, normal) index pairs
    m.wordCount = args.wordCount;
    render::BeginMesh(device, m);
}

// Returns the projection's verdict: 2 nothing on screen, 1 crossing the edges, 0 on screen (drawn: 1). Without flag
// 2 the mesh is projected with the screen stack and counts as on screen.
int ProjectAndDraw(TextureDevice* device, const MeshArgs& args, const Textures* textures) {
    const TransformPipeline* pipeline = device->pipeline;
    g_projected.resize(args.blocks * 4);
    int visibility = 0;
    if (args.flags & 2) {
        g_outcodes.resize(args.blocks * 4);
        visibility = pipeline->ProjectClipped(*pipeline->Top(kClipStack), args.positions, g_projected.data(),
                                              g_outcodes.data(), args.blocks);
    } else {
        TransformPipeline::Project(*pipeline->Top(kScreenStack), args.positions, g_projected.data(), args.blocks);
    }
    if (visibility == 2) return 2;
    if (args.flags & 4) OverrideColours(device, args);
    // The new renderer (src/render) takes the mesh in model space with the matrix it was projected
    // with; each pass the old path draws below hands over the device's state.
    HandOver(device, args);
    int result = visibility;
    if (visibility == 0 || visibility == 1) {
        Draw(device, args, textures, visibility == 1);
        result = 1;
    }
    render::EndMesh();
    return result;
}

// The harness build's renderstats time the draw ("device mesh draws").
int DrawMesh(TextureDevice* device, const MeshArgs& args, const Textures* textures) {
    render::BeginDeviceMeshTime();
    int result = ProjectAndDraw(device, args, textures);
    render::EndDeviceMeshTime();
    return result;
}

} // namespace

int TextureDevice::DrawMeshBlocks(const VertexPacket* positions, int blocks, const NormalPacket* normals,
                              int colourCount, std::uint32_t* colours, std::uint32_t* speculars, const TexCoord* uvs,
                              int unused10, const MeshCorner* corners, int cornerCount, const std::uint32_t* words,
                              int wordCount, std::uint16_t* indices, int indexCount, float uOffset, float vOffset,
                              int alpha, int flags) {
    MeshArgs a{positions, blocks, normals, colourCount, colours, speculars, uvs, unused10, corners, cornerCount,
               words, wordCount, indices, indexCount, &uOffset, &vOffset, alpha, flags};
    return DrawMesh(this, a, nullptr);
}

int TextureDevice::DrawMeshBlocks2(Surface* firstTexture, Surface* secondTexture,
                               const VertexPacket* positions, int blocks, const NormalPacket* normals, int colourCount,
                               std::uint32_t* colours, std::uint32_t* speculars, const TexCoord* uvs, int unused10,
                               const MeshCorner* corners, int cornerCount, const std::uint32_t* words, int wordCount,
                               std::uint16_t* indices, int indexCount, float uOffset, float vOffset, int alpha,
                               int flags) {
    MeshArgs a{positions, blocks, normals, colourCount, colours, speculars, uvs, unused10, corners, cornerCount,
               words, wordCount, indices, indexCount, &uOffset, &vOffset, alpha, flags};
    Textures textures{firstTexture, secondTexture};
    return DrawMesh(this, a, &textures);
}

// --- Device object -----------------------------------------------------------------------------------

// The device object (TextureDevice, texture.h): its constructor and destructor. There is no DirectDraw or Direct3D
// object: the constructor starts the new renderer (src/render), which draws and presents. See docs/notes/renderer.md (section 9).

namespace {

// The performance overlay's switch (docs/notes/renderer.md section 15): [renderer] PerfOverlay of ei_settings.ini;
// off when absent or without the overlay.
bool PerfOverlaySetting() {
    return render::PerfOverlaySupported() && settings::GetBool(settings::kRenderer, "PerfOverlay", false);
}

// The vertical blank's switch (docs/notes/renderer.md section 15): [renderer] VSync, on when absent. Read only in the
// product, as above.
bool VsyncSetting() { return settings::GetBool(settings::kRenderer, "VSync", true); }

// The render quality (docs/notes/renderer.md section 11, "Render quality"): [renderer] Msaa (the world view's samples:
// 1 off, 2, 4 or 8) and Anisotropy (its models' anisotropic sampling: 1 off .. 16). A value missing (or not a number)
// is written with its default, the look the user's dgVoodoo setup gave, so the file shows both. Read only in the
// product, as above (render::QualitySettingsSupported).
constexpr int kDefaultMsaa = 8;
constexpr int kDefaultAnisotropy = 16;

int QualitySetting(const char* name, int fallback) {
    const int value = settings::GetInt(settings::kRenderer, name, fallback);
    settings::SetInt(settings::kRenderer, name, value);
    return value;
}

} // namespace

TextureDevice::TextureDevice(platform::NativeWindow newWindow, bool newFullScreen, int width, int height, int newBitsPerPixel)
    : fullScreen(newFullScreen), screen{0, 0, width, height}, window(newWindow), bitsPerPixel(newBitsPerPixel) {
    // (The original required MMX and loaded 2DintMMX.dll, its pixel converter, here; nothing needs it now.)
    // The new renderer presents; when it cannot start, the game ends there (render::detail::Fatal).
    render::StartPresenting();
    render::SetPerfOverlay(PerfOverlaySetting());
    if (render::VsyncSwitchSupported()) render::SetVsync(VsyncSetting());
    if (render::QualitySettingsSupported()) {
        render::SetMsaa(QualitySetting("Msaa", kDefaultMsaa));
        render::SetAnisotropy(QualitySetting("Anisotropy", kDefaultAnisotropy));
        settings::Save();
    }

    // The game's Direct3D 7 device set the calling thread's x87 precision to 24 bits (the game asked for
    // DDSCL_FPUSETUP). The game code is SSE2 code, but the precision still counts: MSVC's 32-bit code gets a float
    // result back on the x87 register stack and may do the next operations there (a product plus one, a dot's sums),
    // which give float maths' result only at 24 bits; and the DLLs with x87 code (Miles, Bink) compute as they did
    // in the original (docs/notes/cleanup-tooling.md, "The control word"). x64 code has no x87 maths and no
    // precision control: SSE float maths is 24-bit already.
#if defined(_M_IX86)
    unsigned int x87 = 0;
    __control87_2(_PC_24, _MCW_PC, &x87, nullptr);
#endif
}

TextureDevice::~TextureDevice() {
    if (pipeline) pipeline->Release();
    if (material) material->Release();
}

// --- Device shadow -----------------------------------------------------------------------------------

// The device's draw of flat-coloured textured geometry, used for object shadows: the positions are projected by
// the pipeline (the visibility verdict); the renderer gets one colour / specular pair, each vertex's texture
// coordinate and a z scale, and clips what crosses the screen edges. See docs/notes/models.md ("Mesh draws").

namespace {

using devicebuffers::g_outcodes;
using devicebuffers::g_projected;

// The pipeline's screen stack (1; the projection uses it or the clip stack 2, which get the same pushes).

// The new renderer (src/render) gets the geometry in model space with the screen stack's matrix
// (kScreenStack); each pass the indexed draw then makes hands over the device's state.
void HandOver(TextureDevice* device, const VertexPacket* positions, const void* uvs, int count,
              const std::uint16_t* indices, int indexCount, const std::uint32_t* colours, bool clipped) {
    render::MeshDraw m{};
    m.positions = &positions->block[0][0];
    m.positionCount = count;
    m.colours = &colours[0];
    m.speculars = &colours[1];
    m.uvs = static_cast<const float*>(uvs);
    m.corners = nullptr;
    m.cornerCount = count;
    m.indices = indices;
    m.indexCount = indexCount;
    m.matrix = device->pipeline->Top(kScreenStack);
    m.screenMatrix = true;
    m.clipped = clipped;
    m.distanceFog = false;   // no fog pass here
    m.zScale = 1.0f;   // multiplies z (1: none)
    render::BeginMesh(device, m);
}

int DrawFlat(TextureDevice* device, const VertexPacket* positions, const void* uvs, int blocks,
             const std::uint16_t* indices, int indexCount, std::uint32_t colour, std::uint32_t specular) {
    TransformPipeline* pipeline = device->pipeline;
    int count = blocks * 4;
    std::uint32_t colours[2] = {colour, specular};
    g_projected.resize(count);
    g_outcodes.resize(count);
    // The verdict: 0 on screen, 1 crossing, 2 outside.
    int visibility = pipeline->ProjectBlocks(positions, g_projected.data(), g_outcodes.data(), blocks, nullptr);
    if (visibility == 2) return 2;
    HandOver(device, positions, uvs, count, indices, indexCount, colours, visibility == 1);
    device->DrawIndexed();
    return 0;
}

} // namespace

// `positions` blocks of four vertices (`blocks` of them), `uvs` one (u, v) per vertex,
// `indices` / `indexCount` the triangles, `colour` and `specular` for every vertex. Returns 2 when
// nothing is visible, else 0.
int TextureDevice::DrawFlatBlocks(const VertexPacket* positions, const void* uvs, int blocks,
                                   const std::uint16_t* indices, int indexCount, std::uint32_t fillColour,
                                   std::uint32_t specular) {
    render::BeginFlatDrawTime(blocks * 4);
    int result = DrawFlat(this, positions, uvs, blocks, indices, indexCount, fillColour, specular);
    render::EndMesh();
    render::EndFlatDrawTime();
    return result;
}
