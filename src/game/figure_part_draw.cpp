// The draw of one figure part: the mesh is clip-tested, advanced when animated, its colours filled (flat meshes)
// or lit once, then drawn through the device or through the owner's figure. See docs/notes/models.md ("Figure
// parts").
#include "figure_mesh.h"

#include "engine_object.h"
#include "game_singletons.h"
#include "link_figure.h"
#include "map_object_figures.h"
#include "material.h"
#include "model.h"
#include "options.h"
#include "renderer_lines.h"   // LineBatch
#include "spell.h"
#include "texture.h"
#include "world.h"
#include "world_data.h"
#include "lib/containers.h"
#include "lib/nan_audit.h"
#include "lib/pc24.h"
#include "lib/rect.h"
#include "lib/text_case.h"
#include "render/render.h"

#include <bit>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

namespace {

// The positions with the offsets added.
std::vector<VertexPacket> g_movedPositions;

// --- Helpers -------------------------------------------------------------------------------------

// --- x87 sequences --------------------------------------------------------------------------------

void AddOffset(const float* offset, float* target) { *target = *offset + *target; }

// The world time: whole ticks plus the fraction of the current one, rounded to a float once (the tick exact).
void TickTime(float* out) {
    const WorldClient* world = &g_clientWorld;
    *out = pc24::Add(static_cast<double>(world->tick), world->tickFraction);
}

// --- Arrays --------------------------------------------------------------------------------------

void Fill(std::vector<std::uint32_t>& values, std::uint32_t value) {
    for (int i = 0; i < std::ssize(values); ++i) values[i] = value;
}

// --- The part --------------------------------------------------------------------------------------

struct Part {
    FigureMeshDrawer* node;
    TextureDevice* device;
    const std::vector<Light*>* lights;
    Material* material;
    int alpha;
    Rect* rect;
    Texture* textureA;
    Texture* textureB;
    LinkMesh* figure;        // the mesh owner's figure (Object3D::figure), or null
    int drawFlags;            // 1 rectangle wanted, 2 crossing the view, 8 MaterialFlag::KeepSpecular
    bool coloursWritten = false;   // this draw filled or lit the colour arrays (render::MeshSource)
    const render::MeshLighting* lighting = nullptr;   // lit on the GPU (not a flat mesh)

    // The new renderer keeps the mesh on the GPU: what the device's draw below draws.
    render::MeshSource Source(bool throughFigure) const {
        return render::MeshSource{node->mesh, throughFigure ? figure : nullptr, !node->mesh->offsets.empty(), coloursWritten,
                                  lighting};
    }
    std::vector<std::uint32_t>& Colours() const { return node->mesh->colorsA; }
    std::vector<std::uint32_t>& Speculars() const { return node->mesh->colorsB; }
    bool Textured() const { return textureA && textureB; }
};

std::uint32_t OffsetBits(float offset) { return std::bit_cast<std::uint32_t>(offset); }

// Flat meshes (FigureMeshFlag::Flat): the figure fills the colours (its FlatColours), else every colour is
// the mesh's flat colour and every specular its flat specular (both arrays sized 4 * normals first).
void FlatColours(Part& part) {
    part.coloursWritten = true;
    if (part.figure) {
        const char* name = (part.node->mesh)->GetName();
        std::string s(name);
        FigureMesh* mesh = part.node->mesh;
        part.figure->FlatColours(&s, mesh->flatColour, mesh->flatSpecular, &part.Colours(), &part.Speculars());
        return;
    }
    part.Colours().resize(part.node->mesh->buffers.normalCount * 4);
    part.Speculars().resize(part.node->mesh->buffers.normalCount * 4);
    Fill(part.Colours(), part.node->mesh->flatColour);
    Fill(part.Speculars(), part.node->mesh->flatSpecular);
}

// The part's light for the GPU, every draw of a mesh that is not flat: the model matrix, the scene's sun, the
// ambient colour (AmbientColour), the material's colours, the options' coefficient, the crown and the lights
// within the object's reach.
std::vector<render::MeshLightSource> g_partLights;
render::MeshLighting g_partLighting;

const render::MeshLighting* PartLighting(const Part& part) {
    render::MeshLighting& l = g_partLighting;
    l = render::MeshLighting{};
    // The world stack's top is the scene's camera times the part's placement for every part; the object
    // stack (3) has the placement alone only for a part about to be lit on the CPU (a lit static part is
    // not pushed there). Outside the world's draw (the interface's figures) only stack 3.
    if (const Matrix4* camera = worlddata::SceneCamera()) {
        l.model = *part.device->pipeline->Top(1);
        l.camera = camera;
    } else {
        l.model = *part.device->pipeline->Top(3);
    }
    if (const Light* sun = g_sceneRenderer.sun) {
        l.sun = true;
        l.sunDirection = sun->direction;
        for (int k = 0; k < 3; ++k) l.sunColour[k] = sun->color[k];
    }
    const float* ambient = AmbientColour();
    for (int k = 0; k < 3; ++k) l.ambientColour[k] = ambient[k];
    const MaterialColors& c = part.material->colors;
    l.diffuseColour[0] = c.diffuse.x;
    l.diffuseColour[1] = c.diffuse.y;
    l.diffuseColour[2] = c.diffuse.z;
    l.diffuseColour[3] = c.diffuse.w;
    l.emissiveColour[0] = c.emissive.x;
    l.emissiveColour[1] = c.emissive.y;
    l.emissiveColour[2] = c.emissive.z;
    l.coefficient = options::g_options.objectsLightingCoeff;
    l.crown = options::g_options.floraSpecialLight != 0 && text::CompareIgnoringCase((part.node->mesh)->GetName(), "crown") == 0;
    g_partLights.clear();
    for (int i = 0; part.lights && i < std::ssize(*part.lights); ++i) {
        const Light* light = (*part.lights)[i];
        g_partLights.push_back(render::MeshLightSource{light->localPosition, light->inverseRangeSquared,
                                                       {light->color[0], light->color[1], light->color[2]}});
    }
    l.lights = g_partLights.data();
    l.lightCount = static_cast<int>(g_partLights.size());
    return &l;
}

// Lighting (once, FigureMeshFlag::Lit). The new renderer lights the part on the GPU; the colours keep only the material's
// diffuse alpha (white otherwise), the speculars become the alpha unless the material keeps them.
void LightFigurePart(Part& part) {
    part.coloursWritten = true;
    if (part.figure) {
        const char* name = (part.node->mesh)->GetName();
        std::string s(name);
        part.figure->LightPart(&s, part.lights, part.material, part.alpha, &part.Colours(), &part.Speculars());
    } else {
        Fill(part.Colours(), part.material->GpuLitColour());
    }
    if (!(part.material->flags & MaterialFlag::KeepSpecular)) {
        Fill(part.Speculars(), (static_cast<std::uint32_t>(part.alpha) & 0xFF) << 24);
    }
    part.node->mesh->flags |= FigureMeshFlag::Lit;
}

// The positions with the per-vertex offsets (FigureMesh::offsets) added.
const VertexPacket* MovedPositionsOf(FigureMesh* mesh) {
    std::vector<VertexPacket>& moved = g_movedPositions;
    moved.resize(mesh->buffers.vertexCount);
    std::memcpy(moved.data(), mesh->buffers.vertices, static_cast<std::size_t>(mesh->buffers.vertexCount) * 64);
    for (int i = 0; i < std::ssize(mesh->offsets); ++i) {
        const float* offset = &mesh->offsets[i].x;
        VertexPacket& packet = moved[i >> 2];
        AddOffset(&offset[0], &packet.block[0][i & 3]);
        AddOffset(&offset[1], &packet.block[1][i & 3]);
        AddOffset(&offset[2], &packet.block[2][i & 3]);
    }
    return moved.data();
}

// The mesh through the device. FigureMeshFlag::Drawn is only set when a rectangle was asked for.
void DrawThroughDevice(Part& part) {
    FigureMesh* mesh = part.node->mesh;
    const VertexPacket* positions = mesh->buffers.vertices;
    int blocks = mesh->buffers.vertexCount;
    if (!mesh->offsets.empty()) positions = MovedPositionsOf(mesh);
    mesh = part.node->mesh;
    const MeshBuffers& b = mesh->buffers;
    const render::MeshSource source = part.Source(false);
    render::SetMeshSource(&source);
    int result;
    if (part.Textured()) {
        result = part.device->DrawMeshBlocks2(part.textureA->surface, part.textureB->surface, positions, blocks,
                                              b.normals, b.normalCount, mesh->colorsA.data(), mesh->colorsB.data(),
                                              b.texCoords, b.texCoordCount, b.corners, b.cornerCount, b.words,
                                              b.wordCount, b.indices, b.indexCount,
                                              mesh->textureOffsetU, mesh->textureOffsetV, part.alpha, part.drawFlags);
    } else {
        result = part.device->DrawMeshBlocks(positions, blocks, b.normals, b.normalCount, mesh->colorsA.data(),
                                             mesh->colorsB.data(), b.texCoords, b.texCoordCount, b.corners, b.cornerCount,
                                             b.words, b.wordCount, b.indices, b.indexCount,
                                             mesh->textureOffsetU, mesh->textureOffsetV, part.alpha, part.drawFlags);
    }
    render::SetMeshSource(nullptr);
    if (!part.rect) return;
    Rect* own = &part.node->screen;
    if (result == 2) {
        rects::SetEmpty(own);
        rects::SetEmpty(part.rect);
        return;
    }
    rects::Copy(own, &part.device->drawnRect);
    *part.rect = *own;
    part.node->mesh->flags |= FigureMeshFlag::Drawn;
}

// The mesh through the owner's figure: DrawPartMoved(Textured) with the offsets, DrawPart(Textured)
// without; the rectangle from the figure's part header of that name.
void DrawThroughFigure(Part& part) {
    FigureMesh* mesh = part.node->mesh;
    bool moved = !mesh->offsets.empty();
    const char* name = mesh->GetName();
    std::string s(name);
    mesh = part.node->mesh;
    std::uint32_t u = OffsetBits(mesh->textureOffsetU);
    std::uint32_t v = OffsetBits(mesh->textureOffsetV);
    std::vector<std::uint32_t>* colours = &part.Colours();
    std::vector<std::uint32_t>* speculars = &part.Speculars();
    std::vector<Vector3>* offsets = &mesh->offsets;
    const render::MeshSource source = part.Source(true);
    render::SetMeshSource(&source);
    int result;
    if (moved && part.Textured()) {
        result = part.figure->DrawPartMovedTextured(&s, part.drawFlags, part.textureA->surface,
                                                    part.textureB->surface, colours, speculars, offsets, u, v);
    } else if (moved) {
        result = part.figure->DrawPartMoved(&s, part.drawFlags, colours, speculars, offsets, u, v);
    } else if (part.Textured()) {
        result = part.figure->DrawPartTextured(&s, part.drawFlags, part.textureA->surface,
                                               part.textureB->surface, colours, speculars, u, v);
    } else {
        result = part.figure->DrawPart(&s, part.drawFlags, colours, speculars, u, v);
    }
    render::SetMeshSource(nullptr);

    if (part.rect) {
        std::string key((part.node->mesh)->GetName());
        auto node = part.figure->headers.find(key);
        Rect* own = &part.node->screen;
        rects::Copy(own, &node->second.screen);
        *part.rect = *own;
    }
    if (result != 2) part.node->mesh->flags |= FigureMeshFlag::Drawn;
}

} // namespace

// `lights` the light objects, `material` the drawing context (flags, light vector), `alpha` the low byte for
// the speculars, `rect` (optional) receives the screen rectangle, the textures (their surfaces) select the
// two-texture draw. Returns 1.
int FigureMeshDrawer::DrawLit(TextureDevice* device, const std::vector<Light*>* lights,
                              Material* material, int alpha, Rect* rect, Texture* textureA, Texture* textureB) {
    FigureMesh* currentMesh = mesh;
    currentMesh->flags &= ~FigureMeshFlag::Drawn;
    if (mesh->flags & FigureMeshFlag::Hidden) return 1;
    TransformObject* ownerObject = mesh->object;
    Part part{this, device, lights, material, alpha, rect, textureA, textureB,
              ownerObject ? static_cast<LinkMesh*>(static_cast<Object3D*>(ownerObject)->figure) : nullptr, 0};

    int visibility = device->pipeline->ClipTest(&part.node->mesh->box.bounds);
    if (visibility == 2) return 1;
    int meshType = static_cast<int>(part.node->mesh->meshType);
    if (meshType >= 13 && meshType <= 16) {
        float time;
        TickTime(&time);
        flags |= kAnimatedDrawerFlags;
        Animate(time);
    }

    std::uint32_t meshFlags = part.node->mesh->flags;
    if (meshFlags & FigureMeshFlag::Flat) {
        FlatColours(part);
    } else {
        part.lighting = PartLighting(part);
        if (!(meshFlags & FigureMeshFlag::Lit)) LightFigurePart(part);
    }

    if (visibility == 1) part.drawFlags = 2;
    if (rect) part.drawFlags |= 1;
    if (material->flags & MaterialFlag::KeepSpecular) part.drawFlags |= 8;
    if (part.figure) {
        DrawThroughFigure(part);
    } else {
        DrawThroughDevice(part);
    }
    DrawBounds(&g_sceneRenderer);
    return 1;
}

// --- Figure part bounds ------------------------------------------------------------------------------

// The finishing call of the part draws. With the debug flag 0x4000 of the owning manager's context set, the
// mesh's clip box is drawn as lines through the renderer: the corners are the bounds' centre plus, per axis,
// either the box minimum or the maximum; eight edges, two points each, in the original's order. See
// docs/notes/models.md ("Figure parts").

namespace {

// The owning manager's context (hoover_loot.cpp; the world, an engine object), and appending a point.
EngineObject* ContextOf(FigureMeshDrawer* node) { return reinterpret_cast<EngineObject*>(node->GetManagerContext()); }

constexpr std::uint32_t kShowBounds = 0x4000;   // the context's flags

} // namespace

void FigureMeshDrawer::DrawBounds(SceneRenderer* renderer) {
    if (!(ContextOf(this)->flags & kShowBounds)) return;
    LineBatch lines{{}, 0, 0xFFFF0000u};
    const ClipBounds& bounds = mesh->box.bounds;
    const float* origin = &bounds.center.x;
    const float* nearExtent = &bounds.boxMin.x;
    const float* farExtent = &bounds.boxMax.x;
    // corner[i]: bit 2 near x, bit 1 near y, bit 0 near z (else far).
    float corner[8][3];
    for (int i = 0; i < 8; ++i) {
        corner[i][0] = origin[0] + ((i & 4) ? nearExtent[0] : farExtent[0]);
        corner[i][1] = origin[1] + ((i & 2) ? nearExtent[1] : farExtent[1]);
        corner[i][2] = origin[2] + ((i & 1) ? nearExtent[2] : farExtent[2]);
    }
    static const int kOrder[16] = {3, 1, 0, 2, 6, 4, 0, 1, 5, 4, 5, 7, 6, 7, 3, 2};
    for (int index : kOrder) lines.AddPoint(corner[index]);
    renderer->DrawLineList(&lines);
    lib::FreeStorage(lines.packets);
}

// --- Plain -------------------------------------------------------------------------------------------

// The plain draw of one figure part: the lit draw's simpler sibling, always through the device and without the
// lighting. Flat meshes (0x100) get their colours filled with one value and speculars with another, a mesh not
// yet lit asks the device to light it, and the per-vertex offsets are added into a copy of the positions. See
// docs/notes/models.md ("Figure parts").

// Arguments 2-4 are not read; `alpha` goes to the device draw, `rect` (optional) receives the screen rectangle
// (empty when off screen). Returns 1.
int FigureMeshDrawer::DrawPlain(TextureDevice* device, Material*, int alpha,
                                   Rect* rect) {
    if (mesh->flags & FigureMeshFlag::Hidden) return 1;
    int drawFlags = 0;
    mesh->flags &= ~FigureMeshFlag::Drawn;
    int visibility = device->pipeline->ClipTest(&mesh->box.bounds);
    if (visibility == 2) return 1;
    int meshType = static_cast<int>(mesh->meshType);
    if (meshType >= 13 && meshType <= 16) {
        float time;
        TickTime(&time);
        flags |= kAnimatedDrawerFlags;
        Animate(time);
    }

    std::uint32_t meshFlags = mesh->flags;
    if (meshFlags & FigureMeshFlag::Flat) {
        Fill(mesh->colorsA, mesh->flatColour);
        Fill(mesh->colorsB, mesh->flatSpecular);
    } else if (!(meshFlags & FigureMeshFlag::Lit)) {
        drawFlags = 4;
    }
    if (visibility == 1) drawFlags |= 2;
    if (rect) drawFlags |= 1;

    const VertexPacket* positions = mesh->buffers.vertices;
    int blocks = mesh->buffers.vertexCount;
    if (!mesh->offsets.empty()) positions = MovedPositionsOf(mesh);
    FigureMesh* m = mesh;
    const MeshBuffers& b = m->buffers;
    // The new renderer keeps the mesh on the GPU: what the device's draw below draws.
    const render::MeshSource source{m, nullptr, !m->offsets.empty(), lib::Has(meshFlags, FigureMeshFlag::Flat)};
    render::SetMeshSource(&source);
    int result = device->DrawMeshBlocks(positions, blocks, b.normals, b.normalCount, m->colorsA.data(), m->colorsB.data(),
                                        b.texCoords, b.texCoordCount, b.corners, b.cornerCount, b.words, b.wordCount,
                                        b.indices, b.indexCount, m->textureOffsetU,
                                        m->textureOffsetV, alpha, drawFlags);
    render::SetMeshSource(nullptr);
    if (rect) {
        Rect* own = &screen;
        if (result == 2) {
            rects::SetEmpty(own);
            rects::SetEmpty(rect);
        } else {
            rects::Copy(own, &device->drawnRect);
            *rect = *own;
        }
    }
    if (result != 2) mesh->flags |= FigureMeshFlag::Drawn;
    DrawBounds(&g_sceneRenderer);
    return 1;
}

// --- Figure drawer -----------------------------------------------------------------------------------

// FigureMeshDrawer::Animate: an animated figure mesh's texture offset per frame. The drawer's lifetime: its
// constructor, destructor bodies and the create / destroy virtuals. See link_figure.h.

namespace {

// The mesh types (FigureMesh +0x15C) whose texture is a sheet of frames.
constexpr std::uint32_t kTwoRows = 13;     // 8 frames, 4 per row, two rows (v 0 / 0.5)
constexpr std::uint32_t kSheet4x4 = 14;    // 16 frames
constexpr std::uint32_t kSheet8x4 = 16;    // 32 frames

} // namespace

void FigureMeshDrawer::Animate(float time) {
    FigureMesh* currentMesh = mesh;
    const std::uint32_t meshType = currentMesh->meshType;
    if (meshType == kTwoRows) {
        int frame = static_cast<int>(static_cast<std::int64_t>(time) & 7);
        if (frame < 4) {
            currentMesh->textureOffsetU = static_cast<float>(frame) * 0.25f;
            mesh->textureOffsetV = 0.0f;
        } else {
            currentMesh->textureOffsetU = static_cast<float>(frame - 4) * 0.25f;
            mesh->textureOffsetV = 0.5f;
        }
    } else if (meshType == kSheet4x4) {
        int frame = static_cast<int>(static_cast<std::int64_t>(time) & 15);
        currentMesh->textureOffsetU = static_cast<float>(frame % 4) * 0.25f;
        mesh->textureOffsetV = 1.0f - static_cast<float>(frame / 4) * 0.25f;
    } else if (meshType == kSheet8x4) {
        int frame = static_cast<int>(static_cast<std::int64_t>(time) & 31);
        currentMesh->textureOffsetU = static_cast<float>(frame % 8) * 0.125f;
        mesh->textureOffsetV = 1.0f - static_cast<float>(frame / 8) * 0.25f;
    }
}

// ---- Lifetime ---------------------------------------------------------------------------

FigureDrawerBase::FigureDrawerBase() {
    mesh = nullptr;
}

// The original's base clears the flags, then its inlined base part the mesh.
FigureDrawerBase::~FigureDrawerBase() { mesh = nullptr; }

FigureMeshDrawer::FigureMeshDrawer() = default;

FigureMeshDrawer::~FigureMeshDrawer() { flags = 0; }

void FigureMeshDrawer::DestroyDrawer() {
    mesh = nullptr;
    flags = 0;
}

void FigureMeshDrawer::Create() { flags = 0; }
void FigureMeshDrawer::Destroy() { DestroyDrawer(); }
const char* FigureMeshDrawer::ClassName() { return "CFigureClientSpecific"; }
ClassId FigureMeshDrawer::GetClassId() { return ClassId::FigureMeshDrawer; }
