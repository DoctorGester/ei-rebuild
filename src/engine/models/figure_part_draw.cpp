// The draw of one figure part: the mesh advanced when animated and handed to the renderer whole, from its part of the
// owner's figure or its own arrays, placed by its model matrix and lit on the GPU (the GPU clips it). See
// docs/notes/models.md ("Figure parts").
#include "engine/models/figure_mesh.h"

#include "engine/models/link_figure.h"
#include "game/objects/object3d.h"
#include "engine/models/material.h"
#include "engine/scene/device.h"
#include "engine/scene/pipeline.h"
#include "engine/core/engine_globals.h"
#include "engine/core/manager_context.h"
#include "engine/scene/scene_renderer.h"
#include "lib/containers.h"
#include "lib/rect.h"
#include "render/render.h"
#include "render/render_state.h"

#include <algorithm>
#include <cfloat>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

// The mesh types whose texture is a sheet of frames (FigureMesh::meshType).
constexpr std::uint32_t kTwoRows = 13;     // 8 frames, 4 per row, two rows (v 0 / 0.5)
constexpr std::uint32_t kSheet4x4 = 14;    // 16 frames
constexpr std::uint32_t kSheet8x4 = 16;    // 32 frames

constexpr float kByteScale = 255.0f;

// The figure a mesh is drawn through: its owner's (Object3D::figure), or null for the mesh's own buffers.
LinkMesh* OwnerFigure(const FigureMesh* mesh) {
    TransformObject* owner = mesh->object;
    return owner ? static_cast<Object3D*>(owner)->figure.Get() : nullptr;
}

// The original's _ftol: truncated through 64 bits, the low half (0 for a NaN or a value beyond the range).
std::uint32_t Truncated(float value) { return static_cast<std::uint32_t>(static_cast<std::int64_t>(value)); }

// An animated mesh steps to the world's time (whole ticks plus the fraction of the current one, rounded to a float
// once) before it is drawn.
void AnimateSheet(FigureMeshDrawer* drawer) {
    const std::uint32_t meshType = drawer->mesh->meshType;
    if (meshType < kTwoRows || meshType > kSheet8x4) return;
    drawer->Animate(g_sceneRenderer.world->SceneTime());
}

// The mesh's arrays: its part `r` of `figure`, else its own buffers; the positions moved by its offsets.
render::MeshDraw MeshOf(FigureMesh* mesh, LinkMesh* figure, const LinkPartRanges* r) {
    render::MeshDraw d{};
    int packets = 0;
    if (figure) {
        packets = r->vertices.count;
        d.positions = figure->vertices.data() + r->vertices.start;
        d.normals = figure->normals.data() + r->normals.start;
        d.normalCount = r->normals.count * 4;
        d.uvs = figure->texCoords.data() + r->texCoords.start;
        d.corners = figure->corners.data() + r->corners.start;
        d.cornerCount = r->corners.count;
        d.words = figure->words.data() + r->words.start;
        d.wordCount = r->words.count;
        d.indices = figure->indices.data() + r->indices.start;
        d.indexCount = r->indices.count;
    } else {
        const MeshBuffers& b = mesh->buffers;
        packets = b.vertexCount;
        d.positions = b.vertices;
        d.normals = b.normals;
        d.normalCount = b.normalCount * 4;
        d.uvs = b.texCoords;
        d.corners = b.corners;
        d.cornerCount = b.cornerCount;
        d.words = b.words;
        d.wordCount = b.wordCount;
        d.indices = b.indices;
        d.indexCount = b.indexCount;
    }
    d.moved = !mesh->offsets.empty();
    if (d.moved) d.positions = MovedPositions(d.positions, packets, mesh->offsets);
    d.positionCount = packets * 4;
    d.uOffset = mesh->textureOffsetU;
    d.vOffset = mesh->textureOffsetV;
    d.moveUv = d.uOffset != 0.0f || d.vOffset != 0.0f;
    d.owner = mesh;
    d.shared = figure;
    d.slots = &mesh->meshSlots;
    return d;
}

} // namespace

const VertexPacket* MovedPositions(const VertexPacket* source, int packets, const std::vector<Vector3>& offsets) {
    static std::vector<VertexPacket> moved;
    moved.resize(packets);
    std::memcpy(moved.data(), source, static_cast<std::size_t>(packets) * sizeof(VertexPacket));
    const int count = std::min(static_cast<int>(std::ssize(offsets)), packets * 4);
    for (int i = 0; i < count; ++i) {
        VertexPacket& packet = moved[i >> 2];
        packet.block[0][i & 3] = offsets[i].x + packet.block[0][i & 3];
        packet.block[1][i & 3] = offsets[i].y + packet.block[1][i & 3];
        packet.block[2][i & 3] = offsets[i].z + packet.block[2][i & 3];
    }
    return moved.data();
}

// A flat mesh (FigureMeshFlag::Flat) is drawn in its flat colour and specular; any other is lit on the GPU, its colour
// white with the material's diffuse alpha (Material::GpuLitColour), its specular the alpha byte over the material's
// specular colour when the material keeps its speculars (components times 255 truncated by _ftol and OR-ed in
// unmasked; the device's specular on around the draw), else the alpha byte alone.
bool FigureMeshDrawer::DrawLit(TextureDevice* device, const Matrix4& view, const Matrix4& model,
                               render::MeshLighting& lighting, Material* material, int alpha) {
    LinkMesh* figure = OwnerFigure(mesh);
    const LinkPartRanges* ranges = figure ? mesh->PartIn(figure) : nullptr;
    if (figure && !ranges) return false;
    AnimateSheet(this);
    render::MeshDraw d = MeshOf(mesh, figure, ranges);
    d.matrix = &view;
    d.model = &model;
    d.screenMatrix = true;
    const std::uint32_t alphaByte = static_cast<std::uint32_t>(alpha) & 0xFF;
    const bool keepSpecular = lib::Has(material->flags, MaterialFlag::KeepSpecular);
    if (mesh->flags & FigureMeshFlag::Flat) {
        d.colour = mesh->flatColour;
        d.specular = mesh->flatSpecular;
    } else {
        d.colour = material->GpuLitColour();
        d.specular = alphaByte << 24;
        if (keepSpecular) {
            const Vector4& s = material->colors.specular;
            d.specular = ((Truncated(s.x * kByteScale) | alphaByte << 8) << 16) | Truncated(s.y * kByteScale) << 8 |
                         Truncated(s.z * kByteScale);
        }
        const MaterialColors& c = material->colors;
        lighting.model = model;
        lighting.diffuseColour[0] = c.diffuse.x;
        lighting.diffuseColour[1] = c.diffuse.y;
        lighting.diffuseColour[2] = c.diffuse.z;
        lighting.diffuseColour[3] = c.diffuse.w;
        lighting.emissiveColour[0] = c.emissive.x;
        lighting.emissiveColour[1] = c.emissive.y;
        lighting.emissiveColour[2] = c.emissive.z;
        d.lighting = &lighting;
    }
    render::g_renderState.specular = keepSpecular;
    device->DrawMesh(d);
    render::g_renderState.specular = false;
    return true;
}

void FigureMeshDrawer::HandOverCaster(const Matrix4& model, const Surface* texture) {
    LinkMesh* figure = OwnerFigure(mesh);
    const LinkPartRanges* ranges = figure ? mesh->PartIn(figure) : nullptr;
    if (figure && !ranges) return;
    render::MeshDraw d = MeshOf(mesh, figure, ranges);
    d.model = &model;
    render::CasterMesh(d, texture);
}

const LinkPartRanges* FigureMesh::PartIn(const LinkMesh* figure) {
    if (figureSerial != figure->serial || !figureRanges) {
        figureSerial = figure->serial;
        figureRanges = figure->PartRanges(GetName());
    }
    return figureRanges;
}

// The sky keeps the colours its lighting wrote; an interface sprite (built every frame: the interface rewrites its
// corners in place) is its flat colours or the device's colour with the alpha 255.
void FigureMeshDrawer::DrawPlain(TextureDevice* device, const Matrix4* sky) {
    if (mesh->flags & FigureMeshFlag::Hidden) return;
    AnimateSheet(this);
    render::MeshDraw d = MeshOf(mesh, nullptr, nullptr);
    if (sky) {
        d.colours = mesh->colorsA.data();
        d.speculars = mesh->colorsB.data();
    } else {
        d.owner = nullptr;
        const bool flat = lib::Has(mesh->flags, FigureMeshFlag::Flat);
        d.colour = flat ? mesh->flatColour : device->colour;
        d.specular = flat ? mesh->flatSpecular : 0xFF000000u;
    }
    d.matrix = device->pipeline->Top(TransformPipeline::kWorldStack);
    d.model = sky;
    d.screenMatrix = true;
    device->DrawMesh(d);
}

// --- Picking -----------------------------------------------------------------------------------------

// The depth of what the last draw drew under a pixel: a ray from the eye through the pixel's centre against the
// mesh's triangles, as the draw had them (its figure's part or its own buffers, moved by its offsets) and placed
// them (screenMatrix). In the screen stack's homogeneous coordinates a point is on the ray when x - px w and
// y - py w are both 0; both are linear over a triangle, so its barycentric weights there are the cross product of
// the corners' values, and the triangle is under the pixel when they share one sign. Its w there, positive in
// front of the eye, is the view depth. CPU-side and renderer-independent, so picks stay deterministic. See
// docs/notes/renderer.md (section 3, "Picking").

float FigureMeshDrawer::SurfaceDepth(const Point& point) const {
    const VertexPacket* positions = mesh->buffers.vertices;
    int packets = mesh->buffers.vertexCount;
    const MeshCorner* corners = mesh->buffers.corners;
    const std::uint16_t* indices = mesh->buffers.indices;
    int indexCount = mesh->buffers.indexCount;
    if (LinkMesh* figure = OwnerFigure(mesh)) {
        VertexPacket* partVertices = nullptr;
        MeshCorner* partCorners = nullptr;
        std::uint16_t* partIndices = nullptr;
        if (!figure->GetPartData(mesh->GetName(), &partVertices, &partCorners, &partIndices, &packets, &indexCount))
            return FLT_MAX;
        positions = partVertices;
        corners = partCorners;
        indices = partIndices;
    }
    const float px = static_cast<float>(point.x) + 0.5f;
    const float py = static_cast<float>(point.y) + 0.5f;
    // Per vertex: x - px w, y - py w and w.
    const int count = packets * 4;
    std::vector<Vector3> onRay(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        Vector3 p = render::PacketVertex(positions, i);
        if (i < std::ssize(mesh->offsets)) p += mesh->offsets[i];
        const Vector3 s = TransformColumn(screenMatrix, p);
        const float w = TransformColumnW(screenMatrix, p);
        onRay[i] = {s.x - px * w, s.y - py * w, w};
    }
    float nearest = FLT_MAX;
    for (int t = 0; t + 2 < indexCount; t += 3) {
        const int a = corners[indices[t]].position;
        const int b = corners[indices[t + 1]].position;
        const int c = corners[indices[t + 2]].position;
        if (a >= count || b >= count || c >= count) continue;
        const Vector3& A = onRay[a];
        const Vector3& B = onRay[b];
        const Vector3& C = onRay[c];
        const float wa = B.x * C.y - C.x * B.y;
        const float wb = C.x * A.y - A.x * C.y;
        const float wc = A.x * B.y - B.x * A.y;
        const float sum = wa + wb + wc;
        if (sum == 0.0f) continue;   // edge on to the ray
        const float ba = wa / sum;
        const float bb = wb / sum;
        const float bc = wc / sum;
        if (ba < 0.0f || bb < 0.0f || bc < 0.0f) continue;
        const float depth = ba * A.z + bb * B.z + bc * C.z;
        if (depth > 0.0f && depth < nearest) nearest = depth;
    }
    return nearest;
}

// --- Figure drawer -----------------------------------------------------------------------------------

// FigureMeshDrawer::Animate: an animated figure mesh's texture offset per frame. See link_figure.h.
void FigureMeshDrawer::Animate(float time) {
    const std::uint32_t meshType = mesh->meshType;
    if (meshType == kTwoRows) {
        int frame = static_cast<int>(static_cast<std::int64_t>(time) & 7);
        if (frame < 4) {
            mesh->textureOffsetU = static_cast<float>(frame) * 0.25f;
            mesh->textureOffsetV = 0.0f;
        } else {
            mesh->textureOffsetU = static_cast<float>(frame - 4) * 0.25f;
            mesh->textureOffsetV = 0.5f;
        }
    } else if (meshType == kSheet4x4) {
        int frame = static_cast<int>(static_cast<std::int64_t>(time) & 15);
        mesh->textureOffsetU = static_cast<float>(frame % 4) * 0.25f;
        mesh->textureOffsetV = 1.0f - static_cast<float>(frame / 4) * 0.25f;
    } else if (meshType == kSheet8x4) {
        int frame = static_cast<int>(static_cast<std::int64_t>(time) & 31);
        mesh->textureOffsetU = static_cast<float>(frame % 8) * 0.125f;
        mesh->textureOffsetV = 1.0f - static_cast<float>(frame / 8) * 0.25f;
    }
}

// ---- Lifetime ---------------------------------------------------------------------------

FigureMeshDrawer::FigureMeshDrawer() = default;

FigureMeshDrawer::~FigureMeshDrawer() = default;

void FigureMeshDrawer::Create() { flags = 0; }

void FigureMeshDrawer::Destroy() {
    mesh = nullptr;
    flags = 0;
}
const char* FigureMeshDrawer::ClassName() { return "CFigureClientSpecific"; }
ClassId FigureMeshDrawer::GetClassId() { return ClassId::FigureMeshDrawer; }
