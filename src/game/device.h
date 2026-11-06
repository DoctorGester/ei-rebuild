#pragma once

// The device object (TextureDevice): the screen, the texture stages' holders and the draws' state over the new
// renderer (src/render), the mesh draws' arguments (MeshArgs), texture stage 0's setups, and the device's mesh work
// buffers, shared by its mesh draws and its flat-coloured draw. See docs/notes/models.md ("Mesh draws") and
// docs/notes/renderer.md.

#include "game/renderer_base.h"
#include "render/mesh_records.h"
#include "render/render_state.h"

#include "lib/bit_flags.h"
#include "lib/rect.h"
#include "lib/vecmath.h"
#include "platform/platform.h"

#include <cstdint>
#include <vector>

class DeviceMaterial;
struct NamedTexture;
struct Surface;
class Texture;
struct TLVertex;
class TransformPipeline;
using VertexPacket = render::VertexPacket;
using NormalPacket = render::NormalPacket;

// The flags of the device's mesh draws (DrawMeshBlocks, DrawMeshBlocks2).
enum class DrawFlag : int {
    ScreenRect = 1,   // keep the drawn screen rectangle (TextureDevice::drawnRect)
    Clipped = 2,      // the mesh crosses the view's edges: project it with the clip stack
    Colours = 4,      // the device colours the mesh (the alpha with it)
    Specular = 8,     // the material keeps its speculars
};
constexpr bool EnableBitFlags(DrawFlag) { return true; }

// The arguments of a mesh draw. A corner's position indexes the projected vertices, its normal the colour and specular
// arrays (one colour per normal) and its texCoord the texture coordinates.
struct MeshArgs {
    const VertexPacket* positions = nullptr;  // a block of four vertices each
    int blocks = 0;
    const NormalPacket* normals = nullptr;  // blocks of four as the positions
    int colourCount = 0;          // the colour arrays hold four times this (and so many normal blocks)
    std::uint32_t* colours = nullptr;
    std::uint32_t* speculars = nullptr;
    const render::TexCoord* uvs = nullptr;
    const render::MeshCorner* corners = nullptr;
    int cornerCount = 0;
    const std::uint32_t* words = nullptr;  // (position, normal) index pairs, by colour index
    int wordCount = 0;
    std::uint16_t* indices = nullptr;  // into the corners
    int indexCount = 0;
    float uOffset = 0.0f;         // compared with 0 and added to the texture coordinates
    float vOffset = 0.0f;
    int alpha = 0;                // the low byte, with flag 4
    DrawFlag flags = {};
};

// The device object: the screen (its window, size and colour depth), the transform pipeline, the texture stages' holders and the
// draws' state. Made once by the scene renderer's creation (renderer_init.cpp); the new renderer (src/render) draws
// and presents. Held by the scene renderer and the texture manager.
class TextureDevice final : public RendererBase {
public:
    void SetMaterial(DeviceMaterial* material);
    int DrawFlatBlocks(const VertexPacket* positions, const render::TexCoord* uvs, int blocks, const std::uint16_t* indices,
                       int indexCount, std::uint32_t colour, std::uint32_t specular, bool groundPrint);
    // The mesh drawn (with two textures for DrawMeshBlocks2). Returns whether it was drawn: false for a mesh outside the view.
    // Without DrawFlag::Clipped the mesh is projected with the screen stack and counts as on screen.
    bool DrawMeshBlocks2(Surface* firstTexture, Surface* secondTexture, const MeshArgs& args);
    bool DrawMeshBlocks(const MeshArgs& args);
    void DrawTwoTextures(Surface* first, Surface* second);
    void DrawIndexed();
    // Draw flag 1's rectangle: the bounds of the projected `positions` (screen positions, or with `clipSpace` the
    // clip-space ones inside the depth range, mapped to the screen) clamped to the screen.
    void BoundsOfProjected(const std::vector<Vector4>& positions, bool clipSpace);
    int DrawTlTriangles(TLVertex* vertices, int vertexCount, const std::uint16_t* indices, int indexCount);
    // The holder the renderer draws the stage with.
    void BindSurface(int stage, Surface* holder);
    // A null texture binds none.
    void BindTexture(int stage, Texture* texture);
    // A texture not looked up yet is found by its name among the texture manager's registered and cached
    // textures (an empty name is not looked up) and kept, counted.
    void BindNamedTexture(int stage, NamedTexture* named);

    // Starts the new renderer (its failure ends the game). `width` x `height` pixels on `window`.
    TextureDevice(platform::NativeWindow window, bool fullScreen, int width, int height);
    ~TextureDevice() override;

    bool fullScreen = false;    // the window covers the monitor
    Rect screen = {};           // (0, 0, width, height): the screen's size
    platform::NativeWindow window = nullptr;
    TransformPipeline* pipeline = nullptr;
    Surface* stageSurfaces[8] = {};    // the holder bound to each texture stage
    DeviceMaterial* material = nullptr;
    std::uint32_t colour = 0xFF000000;    // the renderer's draw colour (mesh draw flag 4)
    // The mesh draws' fog (render::MeshFog; the triangle lists' FogVertices with Distance): the world's draws after its
    // sky fog by distance, its sky towards the horizon (eyeHeight the eye above the dome's origin), its hit points and
    // the interface not (WorldClient::Render, Sky::Draw).
    render::MeshFog fog = render::MeshFog::None;
    float eyeHeight = 0.0f;
    Rect drawnRect = {};        // the screen rectangle of the last mesh draw
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

// Once a frame: the new renderer's frame shown.
void DevicePresent(TextureDevice* device);

namespace devicebuffers {

// The projected positions (TransformPipeline::Project / ProjectClipped).
extern std::vector<Vector4> g_projected;
// An outcode per projected vertex.
extern std::vector<std::uint8_t> g_outcodes;

} // namespace devicebuffers
