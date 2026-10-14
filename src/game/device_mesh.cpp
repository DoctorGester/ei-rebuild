// The Direct3D device's indexed mesh draws: 004E9010 (thiscall, RET 0x48) and 004E9A50 (the same
// with two texture arguments, RET 0x50), on the device object at the renderer's +0x20. The mesh's
// positions (blocks of four vertices) are projected by the pipeline's transform object (+0xAC ->
// +0xC; slot 14, or slot 9 with an outcode per vertex when clipping is asked for), then every
// corner (position, colour and texture coordinate indices) becomes a TL vertex; meshes crossing the
// screen edges go through the transform object's clipper (slot 37) and are projected again (slot
// 34). Figures (005A7A70, 005A7640) and the 3D library's meshes (004F3C60 ...) draw through them.
// Buffers are globals at 007AEDE0..007AEE3C. See docs/notes/models.md ("Renderer"). The new
// renderer (src/render) gets the mesh in model space once it is known to be visible
// (docs/notes/renderer.md).
#include "clip.h"

#include "ei/hook.h"
#include "device_buffers.h"
#include "model.h"   // CTransformPipeline, ITransformPipeline
#include "render/render.h"
#include "originals_core.h"
#include "originals_library.h"
#include "originals_render.h"
#include "texture.h"

#include <cstdint>
#include <cstring>

namespace {

struct MeshCorner {           // 6 bytes
    std::uint16_t position;   // into the projected vertices
    std::uint16_t colour;     // into the colour and specular arrays
    std::uint16_t uv;         // into the texture coordinates
};
static_assert(sizeof(MeshCorner) == 6);
struct TexCoord {
    float u, v;
};

// The arguments both draws share (004E9A50 has the two textures before them).
struct MeshArgs {
    const void* positions;        // 64 bytes per block of four vertices
    int blocks;
    int unused5;
    int colourCount;              // the colour arrays hold four times this
    std::uint32_t* colours;
    std::uint32_t* speculars;
    const TexCoord* uvs;
    int unused10;
    const MeshCorner* corners;
    int cornerCount;
    int unused13;
    int unused14;
    std::uint16_t* indices;       // into the corners
    int indexCount;
    const float* uOffset;         // the arguments themselves (their bits are compared and added)
    const float* vOffset;
    int alpha;                    // the low byte, with flag 4
    int flags;                    // 1 screen bounds, 2 clip, 4 colours from the device (+0x150) and alpha
};

struct Textures {
    void* first;
    void* second;
};

// --- The buffers ---------------------------------------------------------------------------------

using devicebuffers::ProjectedVertex;
using devicebuffers::Vertices;
using devicebuffers::ClipIndices;
using devicebuffers::Projected;
using devicebuffers::CornerOutcodes;
using devicebuffers::Outcodes;

// The device (thiscall): the screen bounds of projected vertices (004E8900, the array and 0) or of
// clipped TL vertices (004E86B0, the array and their outcodes); the draws with one (004EB550) or
// two textures (004EB370): primitive, FVF, vertices, count, indices, count, flags.
__declspec(naked) void __fastcall BoundsOfProjected(CTextureDevice*, void*, lib::AlignedArray<ProjectedVertex>*, int)
    EI_ORIGINAL(0x004E8900)
__declspec(naked) void __fastcall BoundsOfVertices(CTextureDevice*, void*, lib::AlignedArray<TLVertex>*, std::uint8_t*)
    EI_ORIGINAL(0x004E86B0)
__declspec(naked) int __fastcall DrawPrimitive2(CTextureDevice*, void*, void*, void*, int, int, void*, int,
                                                std::uint16_t*, int, int) EI_ORIGINAL(0x004EB370)

TLVertex* VertexData() { return Vertices().data; }

// The pipeline's screen stack (1) and clip-space stack (2), whose top matrices project the mesh.
constexpr int kScreenStack = 1;
constexpr int kClipStack = 2;

// FCOMP with 0.0 (0073B858) then C3: equal, or unordered.
bool ZeroOrNaN(const float* value) {
    std::uint32_t bits;
    std::memcpy(&bits, value, 4);
    return (bits & 0x7FFFFFFFu) == 0 || ((bits & 0x7F800000u) == 0x7F800000u && (bits & 0x007FFFFFu) != 0);
}

// *out = *offset + *value: FLD the offset, FADD the coordinate, FSTP (the operand order decides
// which NaN comes out when both are NaNs).
__declspec(naked) void __fastcall AddOffset(const float* /*offset*/, const float* /*value*/, float* /*out*/) {
    __asm {
        fld dword ptr [ecx]
        fadd dword ptr [edx]
        mov eax, dword ptr [esp + 4]
        fstp dword ptr [eax]
        ret 4
    }
}

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
    bool moved = !(ZeroOrNaN(a.uOffset) && ZeroOrNaN(a.vOffset));
    for (int i = 0; i < a.cornerCount; ++i, ++v) {
        const MeshCorner& c = a.corners[i];
        std::memcpy(&v->x, &projected[c.position], 16);
        v->color = a.colours[c.colour];
        v->specular = a.speculars[c.colour];
        if (moved) {
            AddOffset(a.uOffset, &a.uvs[c.uv].u, &v->u);
            AddOffset(a.vOffset, &a.uvs[c.uv].v, &v->v);
        } else {
            std::memcpy(&v->u, &a.uvs[c.uv], 8);
        }
    }
}

// Hands the vertices to the device's vertex-buffer batch (never available: 004E7820 returns null)
// or draws them.
void Submit(CTextureDevice* device, const Textures* textures, std::uint16_t* indices, int indexCount) {
    int count = Vertices().size;
    if (void* buffer = device->LockVertexBuffer(count)) {
        std::memcpy(buffer, Vertices().data, static_cast<std::size_t>(count) * sizeof(TLVertex));
        device->AppendBatchIndices(indices, indexCount);
        device->batch_.vertexCount += count;
        return;
    }
    if (textures) {
        DrawPrimitive2(device, nullptr, textures->first, textures->second, 4, 0x1C4, Vertices().data, count, indices,
                       indexCount, 0);
    } else {
        DrawPrimitive(device, nullptr, 4, 0x1C4, Vertices().data, count, indices, indexCount, 0);
    }
}

void DrawOnScreen(CTextureDevice* device, ITransformPipeline* transform, const MeshArgs& a, const Textures* textures) {
    if (a.flags & 1) BoundsOfProjected(device, nullptr, &Projected(), 0);
    Vertices().SetSizeDiscard(a.cornerCount);
    FillCorners(a);
    if (device->distanceFog_) transform->FogVertices(Vertices().data, a.cornerCount);
    Submit(device, textures, a.indices, a.indexCount);
}

// Some vertices off screen: room for ten times the corners and indices, the corners' outcodes, the
// transform object's clipper (slot 37) writes the clipped triangles to ClipIndices, then the
// vertices are projected (slot 34).
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
    Vertices().SetSize(vertexCount);       // 004EC790
    ClipIndices().SetSize(indexCount);     // 0044FA90
    transform->ToScreenClipped(Vertices().data, Vertices().size);
    if (device->distanceFog_) transform->FogVertices(Vertices().data, Vertices().size);
    if (a.flags & 1) BoundsOfVertices(device, nullptr, &Vertices(), CornerOutcodes().data);
    Submit(device, textures, ClipIndices().data, ClipIndices().size);
}

void HandOver(CTextureDevice* device, const MeshArgs& a) {
    render::MeshDraw m{};
    m.positions = static_cast<const float*>(a.positions);
    m.positionCount = a.blocks * 4;
    m.colours = a.colours;
    m.speculars = a.speculars;
    m.uvs = &a.uvs->u;
    m.corners = &a.corners->position;
    m.cornerCount = a.cornerCount;
    m.indices = a.indices;
    m.indexCount = a.indexCount;
    m.moveUv = !(ZeroOrNaN(a.uOffset) && ZeroOrNaN(a.vOffset));
    m.uOffset = *a.uOffset;
    m.vOffset = *a.vOffset;
    // The screen stack (1): the clip-space stack (2, which the clipping draws use) times the
    // viewport mapping, so it also places meshes drawn into a part of the screen (the interface's
    // previews). Both stacks get the same pushes.
    m.screenMatrix = true;
    m.matrix = device->pipeline_->Top(kScreenStack)->m;
    m.clipped = (a.flags & 2) != 0;
    m.distanceFog = device->distanceFog_ != 0;
    m.zScale = 1.0f;
    render::BeginMesh(device, m);
}

// Returns the transform's verdict: 2 nothing on screen, 1 clipped, 0 on screen (drawn: 1).
int DrawMesh(CTextureDevice* device, const MeshArgs& a, const Textures* textures) {
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
    // with; each pass the old path draws below hands over the device's state (device_draw.cpp).
    if (render::Active()) HandOver(device, a);
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

} // namespace

// 004E9010 (thiscall on the device, RET 0x48).
int __fastcall DeviceDrawMesh(CTextureDevice* device, void*, const void* positions, int blocks, int unused5, int colourCount,
                              std::uint32_t* colours, std::uint32_t* speculars, const void* uvs, int unused10,
                              const void* corners, int cornerCount, int unused13, int unused14,
                              std::uint16_t* indices, int indexCount, float uOffset, float vOffset, int alpha,
                              int flags) {
    MeshArgs a{positions, blocks, unused5, colourCount, colours, speculars, static_cast<const TexCoord*>(uvs),
               unused10, static_cast<const MeshCorner*>(corners), cornerCount, unused13, unused14, indices,
               indexCount, &uOffset, &vOffset, alpha, flags};
    return DrawMesh(device, a, nullptr);
}
EI_REPLACE(0x004E9010, DeviceDrawMesh);

// 004E9A50 (thiscall on the device, RET 0x50): the same with two textures for the draw (004EB370).
int __fastcall DeviceDrawMesh2(CTextureDevice* device, void*, void* firstTexture, void* secondTexture, const void* positions,
                               int blocks, int unused5, int colourCount, std::uint32_t* colours,
                               std::uint32_t* speculars, const void* uvs, int unused10, const void* corners,
                               int cornerCount, int unused13, int unused14, std::uint16_t* indices, int indexCount,
                               float uOffset, float vOffset, int alpha, int flags) {
    MeshArgs a{positions, blocks, unused5, colourCount, colours, speculars, static_cast<const TexCoord*>(uvs),
               unused10, static_cast<const MeshCorner*>(corners), cornerCount, unused13, unused14, indices,
               indexCount, &uOffset, &vOffset, alpha, flags};
    Textures textures{firstTexture, secondTexture};
    return DrawMesh(device, a, &textures);
}
EI_REPLACE(0x004E9A50, DeviceDrawMesh2);
