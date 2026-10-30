// The Direct3D device's indexed mesh draws, with one texture and with two: the mesh's positions are projected by
// the pipeline for the visibility verdict and the screen rectangle picking reads. The new
// renderer gets the mesh in model space once it is visible and clips it. See docs/notes/models.md ("Renderer").
#include "device_buffers.h"
#include "figure_mesh.h"
#include "model.h"
#include "render/render.h"
#include "texture.h"

#include "lib/nan_audit.h"

#include <cstdint>

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
