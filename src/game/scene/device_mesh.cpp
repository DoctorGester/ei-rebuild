// The Direct3D device's indexed mesh draws, with one texture and with two: the mesh's positions are projected by
// the pipeline for the visibility verdict of a mesh crossing the view's edges and for the screen rectangle picking
// reads. The renderer (src/render) gets the mesh in model space once it is visible and clips it. See
// docs/notes/models.md ("Renderer").
#include "game/scene/device.h"
#include "game/scene/pipeline.h"
#include "game/scene/device_material.h"
#include "game/models/figure_mesh.h"
#include "render/render.h"
#include "render/render_checks.h"
#include "game/app/settings.h"

#include <cstdint>

namespace {

struct Textures {
    Surface* first = nullptr;
    Surface* second = nullptr;
};

// --- The buffers ---------------------------------------------------------------------------------

using devicebuffers::g_projected;
using devicebuffers::g_outcodes;

// The pipeline's screen stack (the world's) and clip-space stack, whose top matrices project the mesh.
constexpr int kScreenStack = TransformPipeline::kWorldStack;
constexpr int kClipStack = TransformPipeline::kClipStack;

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
    if (lib::Has(args.flags, DrawFlag::ScreenRect)) device->BoundsOfProjected(g_projected, crossing);
    Submit(device, textures);
}

void HandOver(TextureDevice* device, const MeshArgs& args) {
    render::MeshDraw m{};
    m.positions = args.positions;
    m.positionCount = args.blocks * 4;
    m.colours = args.colours;
    m.speculars = args.speculars;
    m.uvs = args.uvs;
    m.corners = args.corners;
    m.cornerCount = args.cornerCount;
    m.indices = args.indices;
    m.indexCount = args.indexCount;
    m.moveUv = args.uOffset != 0.0f || args.vOffset != 0.0f;
    m.uOffset = args.uOffset;
    m.vOffset = args.vOffset;
    // The screen stack (1): the clip-space stack (2, which the clipping draws use) times the
    // viewport mapping, so it also places meshes drawn into a part of the screen (the interface's
    // previews). Both stacks get the same pushes.
    m.screenMatrix = true;
    m.matrix = device->pipeline->Top(kScreenStack);
    m.clipped = lib::Has(args.flags, DrawFlag::Clipped);
    m.fog = device->fog;
    m.eyeHeight = device->eyeHeight;
    m.zScale = 1.0f;
    m.coloursWritten = lib::Has(args.flags, DrawFlag::Colours);   // OverrideColours
    // A figure part lit on the GPU needs its normals.
    m.normals = args.normals;
    m.normalCount = args.colourCount * 4;
    m.words = args.words;
    m.wordCount = args.wordCount;
    render::BeginMesh(device, m);
}

// Whether the mesh was drawn: not outside the view's edges. Without DrawFlag::Clipped the mesh counts as on screen and
// is projected with the screen stack only for its rectangle (DrawFlag::ScreenRect).
bool ProjectAndDraw(TextureDevice* device, const MeshArgs& args, const Textures* textures) {
    const TransformPipeline* pipeline = device->pipeline;
    TransformPipeline::ClipResult visibility = TransformPipeline::ClipResult::Inside;
    if (lib::Has(args.flags, DrawFlag::Clipped)) {
        g_projected.resize(args.blocks * 4);
        g_outcodes.resize(args.blocks * 4);
        visibility = pipeline->ProjectClipped(*pipeline->Top(kClipStack), args.positions, g_projected.data(),
                                              g_outcodes.data(), args.blocks);
    } else if (lib::Has(args.flags, DrawFlag::ScreenRect)) {
        g_projected.resize(args.blocks * 4);
        TransformPipeline::Project(*pipeline->Top(kScreenStack), args.positions, g_projected.data(), args.blocks);
    }
    if (visibility == TransformPipeline::ClipResult::Outside) return false;
    if (lib::Has(args.flags, DrawFlag::Colours)) OverrideColours(device, args);
    // The new renderer (src/render) takes the mesh in model space with the matrix it was projected
    // with; each pass the old path draws below hands over the device's state.
    HandOver(device, args);
    Draw(device, args, textures, visibility == TransformPipeline::ClipResult::Crossing);
    render::EndMesh();
    return true;
}

// The harness build's renderstats time the draw ("device mesh draws").
bool DrawMesh(TextureDevice* device, const MeshArgs& args, const Textures* textures) {
    render::BeginDeviceMeshTime();
    const bool result = ProjectAndDraw(device, args, textures);
    render::EndDeviceMeshTime();
    return result;
}

} // namespace

bool TextureDevice::DrawMeshBlocks(const MeshArgs& args) { return DrawMesh(this, args, nullptr); }

bool TextureDevice::DrawMeshBlocks2(Surface* firstTexture, Surface* secondTexture, const MeshArgs& args) {
    Textures textures{firstTexture, secondTexture};
    return DrawMesh(this, args, &textures);
}

// --- Device object -----------------------------------------------------------------------------------

// The device object (TextureDevice, texture.h): its constructor and destructor. There is no DirectDraw or Direct3D
// object: the constructor starts the new renderer (src/render), which draws and presents. See docs/notes/renderer.md (section 9).

namespace {

// The performance overlay's switch (docs/notes/renderer.md section 15): [renderer] PerfOverlay of ei_settings.ini;
// off without the overlay.
bool PerfOverlaySetting() {
    return render::PerfOverlaySupported() && settings::GetBool(settings::kRenderer, "PerfOverlay");
}

// The vertical blank's switch (docs/notes/renderer.md section 15): [renderer] VSync. Read only in the product, as
// above.
bool VsyncSetting() { return settings::GetBool(settings::kRenderer, "VSync"); }

// The render quality (docs/notes/renderer.md section 11, "Render quality"): [renderer] Msaa (the world view's samples:
// 1 off, 2, 4 or 8) and Anisotropy (its models' anisotropic sampling: 1 off .. 16). A value missing (or not a number)
// is written with its catalog default, the look the user's dgVoodoo setup gave, so the file shows both. Read only in
// the product, as above (render::QualitySettingsSupported).
int QualitySetting(const char* name) {
    const int value = settings::GetInt(settings::kRenderer, name);
    settings::SetInt(settings::kRenderer, name, value);
    return value;
}

} // namespace

TextureDevice::TextureDevice(platform::NativeWindow newWindow, bool newFullScreen, int width, int height)
    : fullScreen(newFullScreen), screen{0, 0, width, height}, window(newWindow) {
    // (The original required MMX and loaded 2DintMMX.dll, its pixel converter, here; nothing needs it now.)
    // The new renderer presents; when it cannot start, the game ends there (render::Fatal).
    render::StartPresenting();
    render::SetPerfOverlay(PerfOverlaySetting());
    if (render::VsyncSwitchSupported()) render::SetVsync(VsyncSetting());
    if (render::QualitySettingsSupported()) {
        render::SetMsaa(QualitySetting("Msaa"));
        render::SetAnisotropy(QualitySetting("Anisotropy"));
        settings::Save();
    }
}

TextureDevice::~TextureDevice() {
    if (pipeline) pipeline->Release();
    if (material) material->Release();
}

// --- Device shadow -----------------------------------------------------------------------------------

// The device's draw of flat-coloured textured geometry (the ground prints, the selection marker): the positions are
// projected by the pipeline (the visibility verdict); the renderer gets one colour / specular pair, each vertex's
// texture coordinate and a z scale, and clips what crosses the screen edges. See docs/notes/models.md ("Mesh draws").

namespace {

using devicebuffers::g_outcodes;
using devicebuffers::g_projected;

// The pipeline's screen stack (1; the projection uses it or the clip stack 2, which get the same pushes).

// The new renderer (src/render) gets the geometry in model space with the screen stack's matrix
// (kScreenStack); each pass the indexed draw then makes hands over the device's state.
void HandOver(TextureDevice* device, const VertexPacket* positions, const TexCoord* uvs, int count,
              const std::uint16_t* indices, int indexCount, const std::uint32_t* colours, bool clipped, bool groundPrint) {
    render::MeshDraw m{};
    m.positions = positions;
    m.positionCount = count;
    m.colours = &colours[0];
    m.speculars = &colours[1];
    m.uvs = uvs;
    m.corners = nullptr;
    m.cornerCount = count;
    m.indices = indices;
    m.indexCount = indexCount;
    m.matrix = device->pipeline->Top(kScreenStack);
    m.screenMatrix = true;
    m.clipped = clipped;
    m.fog = device->fog;
    m.eyeHeight = device->eyeHeight;
    m.zScale = 1.0f;   // multiplies z (1: none)
    m.groundDecal = groundPrint;
    render::BeginMesh(device, m);
}

int DrawFlat(TextureDevice* device, const VertexPacket* positions, const TexCoord* uvs, int blocks,
             const std::uint16_t* indices, int indexCount, std::uint32_t colour, std::uint32_t specular, bool groundPrint) {
    TransformPipeline* pipeline = device->pipeline;
    int count = blocks * 4;
    std::uint32_t colours[2] = {colour, specular};
    g_projected.resize(count);
    g_outcodes.resize(count);
    const TransformPipeline::ClipResult visibility =
        pipeline->ProjectBlocks(positions, g_projected.data(), g_outcodes.data(), blocks, nullptr);
    if (visibility == TransformPipeline::ClipResult::Outside) return 2;
    HandOver(device, positions, uvs, count, indices, indexCount, colours, visibility == TransformPipeline::ClipResult::Crossing, groundPrint);
    device->DrawIndexed();
    return 0;
}

} // namespace

// `positions` blocks of four vertices (`blocks` of them), `uvs` one per vertex,
// `indices` / `indexCount` the triangles, `colour` and `specular` for every vertex; `groundPrint`: a print laid on
// the ground, the positions the landscape's (render::MeshDraw::groundDecal). Returns 2 when nothing is visible, else 0.
int TextureDevice::DrawFlatBlocks(const VertexPacket* positions, const TexCoord* uvs, int blocks,
                                   const std::uint16_t* indices, int indexCount, std::uint32_t fillColour,
                                   std::uint32_t specular, bool groundPrint) {
    render::BeginFlatDrawTime(blocks * 4);
    int result = DrawFlat(this, positions, uvs, blocks, indices, indexCount, fillColour, specular, groundPrint);
    render::EndMesh();
    render::EndFlatDrawTime();
    return result;
}
