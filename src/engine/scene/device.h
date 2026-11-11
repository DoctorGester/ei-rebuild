#pragma once

// The device object (TextureDevice): the screen, the texture stages' holders and the draws' state over the new
// renderer (src/render), its mesh draws and texture stage 0's setups. See docs/notes/models.md ("Mesh draws") and
// docs/notes/renderer.md.

#include "engine/scene/renderer_base.h"
#include "render/mesh_records.h"
#include "render/render_state.h"

#include "lib/rect.h"
#include "lib/vecmath.h"
#include "platform/platform.h"

#include <cstdint>

class DeviceMaterial;
struct Surface;
class Texture;
struct TLVertex;
class TransformPipeline;
using VertexPacket = render::VertexPacket;
using NormalPacket = render::NormalPacket;

namespace render {
struct MeshDraw;
}

// The device object: the screen (its window, size and colour depth), the transform pipeline, the texture stages' holders and the
// draws' state. Made once by the scene renderer's creation (renderer_init.cpp); the new renderer (src/render) draws
// and presents. Held by the scene renderer and the texture manager.
class TextureDevice final : public RendererBase {
public:
    void SetMaterial(DeviceMaterial* material);
    void DrawFlatBlocks(const VertexPacket* positions, const render::TexCoord* uvs, int blocks, const std::uint16_t* indices,
                        int indexCount, std::uint32_t colour, std::uint32_t specular, bool groundPrint);
    // The mesh handed to the renderer whole (the GPU clips it) with the device's fog, in one pass of the current
    // render state and material (DrawIndexed).
    void DrawMesh(render::MeshDraw& mesh);
    void DrawIndexed();
    int DrawTlTriangles(TLVertex* vertices, int vertexCount, const std::uint16_t* indices, int indexCount);
    // The holder the renderer draws the stage with.
    void BindSurface(int stage, Surface* holder);
    // A null texture binds none.
    void BindTexture(int stage, Texture* texture);

    // Starts the new renderer (its failure ends the game). `width` x `height` pixels on `window`.
    TextureDevice(platform::NativeWindow window, bool fullScreen, int width, int height);
    ~TextureDevice() override;

    bool fullScreen = false;    // the window covers the monitor
    Rect screen = {};           // (0, 0, width, height): the screen's size
    platform::NativeWindow window = nullptr;
    TransformPipeline* pipeline = nullptr;
    Surface* stageSurfaces[8] = {};    // the holder bound to each texture stage
    DeviceMaterial* material = nullptr;
    std::uint32_t colour = 0xFF000000;    // the renderer's draw colour (an interface sprite's, DrawPlain)
    // The mesh draws' fog (render::MeshFog; the triangle lists' FogVertices with Distance): the world's draws after its
    // sky fog by distance, its sky towards the horizon (eyeHeight the eye above the dome's origin), its hit points and
    // the interface not (WorldClient::Render, Sky::Draw).
    render::MeshFog fog = render::MeshFog::None;
    float eyeHeight = 0.0f;
};

// Texture stage 0's three setups the world's and the interface's draws switch between: the scene's (texture x
// diffuse, alpha diffuse), the alpha passes' (texture x diffuse for colour and alpha) and the terrain's (texture x
// current, alpha texture).
inline constexpr render::StageCombiner kSceneStage = {render::TextureOp::Modulate, render::TextureArg::Texture,
                                                      render::TextureArg::Diffuse, render::TextureOp::SelectArg1,
                                                      render::TextureArg::Diffuse, render::TextureArg::Current};
inline constexpr render::StageCombiner kAlphaPassStage = {render::TextureOp::Modulate, render::TextureArg::Texture,
                                                          render::TextureArg::Diffuse, render::TextureOp::Modulate,
                                                          render::TextureArg::Texture, render::TextureArg::Diffuse};
inline constexpr render::StageCombiner kTerrainStage = {render::TextureOp::Modulate, render::TextureArg::Texture,
                                                        render::TextureArg::Current, render::TextureOp::SelectArg1,
                                                        render::TextureArg::Texture, render::TextureArg::Current};
