// The device's mesh draws: a mesh in model space handed to the renderer (src/render) whole, which clips it, in one pass
// of the game's render state; the flat-coloured geometry (the ground prints, the selection marker) the same way. See
// docs/notes/models.md ("Renderer").
#include "engine/scene/device.h"
#include "engine/scene/pipeline.h"
#include "engine/scene/device_material.h"
#include "render/render.h"
#include "render/render_checks.h"
#include "engine/data/settings.h"

#include <cstdint>

namespace {

// The mesh with the device's fog, then the pass.
void HandOver(TextureDevice* device, render::MeshDraw& m) {
    m.fog = device->fog;
    m.eyeHeight = device->eyeHeight;
    m.zScale = 1.0f;
    render::BeginMesh(device, m);
    device->DrawIndexed();
    render::EndMesh();
}

} // namespace

// The harness build's renderstats time the draw ("device mesh draws").
void TextureDevice::DrawMesh(render::MeshDraw& m) {
    render::BeginDeviceMeshTime();
    HandOver(this, m);
    render::EndDeviceMeshTime();
}

// `positions` blocks of four vertices (`blocks` of them), `uvs` one per vertex, `indices` / `indexCount` the triangles,
// `vertexColour` and `vertexSpecular` for every vertex, placed by the screen stack's top; `groundPrint`: a print laid
// on the ground, the positions the landscape's (render::MeshDraw::groundDecal).
void TextureDevice::DrawFlatBlocks(const VertexPacket* positions, const render::TexCoord* uvs, int blocks,
                                   const std::uint16_t* indices, int indexCount, std::uint32_t vertexColour,
                                   std::uint32_t vertexSpecular, bool groundPrint) {
    render::BeginFlatDrawTime(blocks * 4);
    render::MeshDraw m{};
    m.positions = positions;
    m.positionCount = blocks * 4;
    m.colour = vertexColour;
    m.specular = vertexSpecular;
    m.uvs = uvs;
    m.cornerCount = blocks * 4;
    m.indices = indices;
    m.indexCount = indexCount;
    m.matrix = pipeline->Top(TransformPipeline::kWorldStack);
    m.screenMatrix = true;
    m.groundDecal = groundPrint;
    HandOver(this, m);
    render::EndFlatDrawTime();
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
