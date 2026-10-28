// The Direct3D device's indexed mesh draws, with one texture and with two: the mesh's positions are projected by
// the pipeline's transform object, every corner becomes a TL vertex, and meshes crossing the screen edges are
// clipped. The new renderer gets the mesh in model space once it is visible. See docs/notes/models.md ("Renderer").
#include "clip.h"

#include "device_buffers.h"
#include "figure_mesh.h"
#include "model.h"
#include "render/render.h"
#include "texture.h"
#include "lib/nan_audit.h"

#include <cstdint>
#include <cstring>

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
    int flags;                    // 1 screen bounds, 2 clip, 4 colours from the device (colour_) and alpha
};

struct Textures {
    CDXSurface* first;
    CDXSurface* second;
};

// --- The buffers ---------------------------------------------------------------------------------

using devicebuffers::ProjectedVertex;
using devicebuffers::Vertices;
using devicebuffers::ClipIndices;
using devicebuffers::Projected;
using devicebuffers::CornerOutcodes;
using devicebuffers::Outcodes;

TLVertex* VertexData() { return Vertices().data; }

// The pipeline's screen stack (1) and clip-space stack (2), whose top matrices project the mesh.
constexpr int kScreenStack = 1;
constexpr int kClipStack = 2;

void AddOffset(const float* offset, const float* value, float* out) { *out = *offset + *value; }

// --- The steps -----------------------------------------------------------------------------------

// Flag 4: every colour becomes the device's (colour_), every specular the alpha byte alone.
void OverrideColours(CTextureDevice* device, const MeshArgs& a) {
    std::uint32_t colour = device->colour_;
    std::uint32_t specular = (static_cast<std::uint32_t>(a.alpha) & 0xFF) << 24;
    int count = a.colourCount * 4;
    for (int i = 0; i < count; ++i) a.colours[i] = colour;
    for (int i = 0; i < count; ++i) a.speculars[i] = specular;
}

// The corners as TL vertices: the projected position (copied as two qwords), colour and specular,
// and the texture coordinate, moved by the offsets unless both compare equal to 0.
void FillCorners(const MeshArgs& a) {
    TLVertex* v = VertexData();
    const ProjectedVertex* projected = Projected().data;
    EI_NAN_AUDIT(*a.uOffset, *a.vOffset);
    bool moved = *a.uOffset != 0.0f || *a.vOffset != 0.0f;
    for (int i = 0; i < a.cornerCount; ++i, ++v) {
        const MeshCorner& c = a.corners[i];
        std::memcpy(&v->x, &projected[c.position], 16);
        v->color = a.colours[c.normal];
        v->specular = a.speculars[c.normal];
        if (moved) {
            AddOffset(a.uOffset, &a.uvs[c.texCoord].u, &v->u);
            AddOffset(a.vOffset, &a.uvs[c.texCoord].v, &v->v);
        } else {
            std::memcpy(&v->u, &a.uvs[c.texCoord], 8);
        }
    }
}

// Draws the vertices (the device's vertex-buffer batch never gave a buffer: nothing is batched).
void Submit(CTextureDevice* device, const Textures* textures, std::uint16_t* indices, int indexCount) {
    int count = Vertices().size;
    if (textures) {
        device->DrawTwoTextures(textures->first, textures->second, 4, 0x1C4, Vertices().data, count, indices, indexCount, 0);
    } else {
        device->DrawIndexed(4, 0x1C4, Vertices().data, count, indices, indexCount, 0);
    }
}

// The TL vertices are read by nothing when the new renderer draws the mesh: the passes still go to the device.
void DrawOnScreen(CTextureDevice* device, ITransformPipeline* transform, const MeshArgs& a, const Textures* textures) {
    if (a.flags & 1) device->BoundsOfProjected(&Projected(), 0);
    Vertices().SetSizeDiscard(a.cornerCount);
    if (!render::MeshCornersSkipped()) {
        FillCorners(a);
        if (device->distanceFog_) transform->FogVertices(Vertices().data, a.cornerCount);
    }
    Submit(device, textures, a.indices, a.indexCount);
}

// Some vertices off screen: room for ten times the corners and indices, the corners' outcodes, the transform
// object's clipper writes the clipped triangles to ClipIndices, then the vertices are projected. When the new
// renderer draws the mesh the clip still runs (its counts go to the device's counters, an empty result draws no
// pass), the projection only for draw flag 1's screen rectangle, the fog not at all.
void DrawClipped(CTextureDevice* device, ITransformPipeline* transform, const MeshArgs& a, const Textures* textures) {
    int capacity = a.cornerCount * 10;
    Vertices().SetSizeDiscard(capacity);
    ClipIndices().SetSize(a.indexCount * 10);
    FillCorners(a);
    CornerOutcodes().SetSize(capacity);
    for (int i = 0; i < a.cornerCount; ++i) CornerOutcodes().data[i] = Outcodes().data[a.corners[i].position];

    int vertexCount = a.cornerCount;
    int indexCount = a.indexCount;
    transform->ClipTriangles(Vertices().data, CornerOutcodes().data, a.indices, ClipIndices().data, &vertexCount,
                             &indexCount);
    if (indexCount == 0 || vertexCount == 0) return;
    Vertices().SetSize(vertexCount);
    ClipIndices().SetSize(indexCount);
    bool skipped = render::MeshCornersSkipped();
    if (!skipped || (a.flags & 1)) transform->ToScreenClipped(Vertices().data, Vertices().size);
    if (!skipped && device->distanceFog_) transform->FogVertices(Vertices().data, Vertices().size);
    if (a.flags & 1) device->BoundsOfVertices(&Vertices(), CornerOutcodes().data);
    Submit(device, textures, ClipIndices().data, ClipIndices().size);
}

void HandOver(CTextureDevice* device, const MeshArgs& a) {
    render::MeshDraw m{};
    m.positions = &a.positions->block[0][0];
    m.positionCount = a.blocks * 4;
    m.colours = a.colours;
    m.speculars = a.speculars;
    m.uvs = &a.uvs->u;
    m.corners = &a.corners->position;
    m.cornerCount = a.cornerCount;
    m.indices = a.indices;
    m.indexCount = a.indexCount;
    EI_NAN_AUDIT(*a.uOffset, *a.vOffset);
    m.moveUv = *a.uOffset != 0.0f || *a.vOffset != 0.0f;
    m.uOffset = *a.uOffset;
    m.vOffset = *a.vOffset;
    // The screen stack (1): the clip-space stack (2, which the clipping draws use) times the
    // viewport mapping, so it also places meshes drawn into a part of the screen (the interface's
    // previews). Both stacks get the same pushes.
    m.screenMatrix = true;
    m.matrix = device->pipeline_->Top(kScreenStack);
    m.clipped = (a.flags & 2) != 0;
    m.distanceFog = device->distanceFog_ != 0;
    m.zScale = 1.0f;
    m.coloursWritten = (a.flags & 4) != 0;   // OverrideColours
    // A figure part lit on the GPU needs its normals.
    m.normals = &a.normals->block[0][0];
    m.normalCount = a.colourCount * 4;
    m.words = reinterpret_cast<const std::uint16_t*>(a.words);   // (position, normal) index pairs
    m.wordCount = a.wordCount;
    render::BeginMesh(device, m);
}

// Returns the transform's verdict: 2 nothing on screen, 1 clipped, 0 on screen (drawn: 1).
int ProjectAndDraw(CTextureDevice* device, const MeshArgs& a, const Textures* textures) {
    ITransformPipeline* transform = device->pipeline_->transform_;
    Projected().SetSizeDiscard(a.blocks * 4);
    int visibility;
    if (a.flags & 2) {
        Outcodes().SetSizeDiscard(a.blocks * 4);
        visibility = transform->ProjectClipped(device->pipeline_->Top(kClipStack), a.positions, Projected().data,
                                               Outcodes().data, a.blocks);
    } else {
        visibility = transform->Project(device->pipeline_->Top(kScreenStack), a.positions, Projected().data, a.blocks);
    }
    if (visibility == 2) return 2;
    if (a.flags & 4) OverrideColours(device, a);
    // The new renderer (src/render) takes the mesh in model space with the matrix it was projected
    // with; each pass the old path draws below hands over the device's state.
    HandOver(device, a);
    int result = visibility;
    if (visibility == 0) {
        DrawOnScreen(device, transform, a, textures);
        result = 1;
    } else if (visibility == 1) {
        DrawClipped(device, transform, a, textures);
    }
    render::EndMesh();
    return result;
}

// The harness build's renderstats time the draw ("device mesh draws").
int DrawMesh(CTextureDevice* device, const MeshArgs& a, const Textures* textures) {
    render::BeginDeviceMeshTime();
    int result = ProjectAndDraw(device, a, textures);
    render::EndDeviceMeshTime();
    return result;
}

} // namespace

int CTextureDevice::DrawMeshBlocks(const VertexPacket* positions, int blocks, const NormalPacket* normals,
                              int colourCount, std::uint32_t* colours, std::uint32_t* speculars, const TexCoord* uvs,
                              int unused10, const MeshCorner* corners, int cornerCount, const std::uint32_t* words,
                              int wordCount, std::uint16_t* indices, int indexCount, float uOffset, float vOffset,
                              int alpha, int flags) {
    MeshArgs a{positions, blocks, normals, colourCount, colours, speculars, uvs, unused10, corners, cornerCount,
               words, wordCount, indices, indexCount, &uOffset, &vOffset, alpha, flags};
    return DrawMesh(this, a, nullptr);
}

int CTextureDevice::DrawMeshBlocks2(CDXSurface* firstTexture, CDXSurface* secondTexture,
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
