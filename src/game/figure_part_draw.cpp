// The draw of one figure part: the mesh is clip-tested, advanced when animated, its colours filled (flat meshes)
// or lit once, then drawn through the device or through the owner's figure. See docs/notes/models.md ("Figure
// parts").
#include "figure_mesh.h"

#include "engine_object.h"
#include "manager_context.h"
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
#include "drawn_motion.h"
#include "lib/containers.h"
#include "lib/rect.h"
#include "lib/text_case.h"
#include "render/render.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

namespace {

// The mesh types whose texture is a sheet of frames (FigureMesh::meshType).
constexpr std::uint32_t kTwoRows = 13;     // 8 frames, 4 per row, two rows (v 0 / 0.5)
constexpr std::uint32_t kSheet4x4 = 14;    // 16 frames
constexpr std::uint32_t kSheet8x4 = 16;    // 32 frames

constexpr std::uint32_t kShowBounds = 0x4000;   // the manager context's debug flag: draw the clip boxes

bool IsAnimatedSheet(std::uint32_t meshType) { return meshType >= kTwoRows && meshType <= kSheet8x4; }

// The world time: whole ticks plus the fraction of the current one, rounded to a float once (the tick exact).
void TickTime(float* out) {
    const WorldClient* world = &g_clientWorld;
    *out = static_cast<float>(world->tick) + world->tickFraction;
}

// --- Arrays --------------------------------------------------------------------------------------

void Fill(std::vector<std::uint32_t>& values, std::uint32_t value) {
    for (int i = 0; i < std::ssize(values); ++i) values[i] = value;
}

// --- The part --------------------------------------------------------------------------------------

struct Part {
    FigureMeshDrawer* node = nullptr;
    TextureDevice* device = nullptr;
    const std::vector<Light*>* lights = nullptr;
    Material* material = nullptr;
    int alpha = 0;
    Rect* rect = nullptr;
    Texture* textureA = nullptr;
    Texture* textureB = nullptr;
    LinkMesh* figure = nullptr;  // the mesh owner's figure (Object3D::figure), or null
    DrawFlag drawFlags = {};
    bool coloursWritten = false;   // this draw filled or lit the colour arrays (render::MeshSource)
    const render::MeshLighting* lighting = nullptr;   // lit on the GPU (not a flat mesh)

    // The mesh the renderer keeps on the GPU: what the device's draw draws.
    render::MeshSource Source(bool throughFigure) const {
        return render::MeshSource{node->mesh, throughFigure ? figure : nullptr, !node->mesh->offsets.empty(), coloursWritten,
                                  lighting};
    }
    std::vector<std::uint32_t>& Colours() const { return node->mesh->colorsA; }
    std::vector<std::uint32_t>& Speculars() const { return node->mesh->colorsB; }
    bool Textured() const { return textureA && textureB; }
};

// Flat meshes (FigureMeshFlag::Flat): the figure fills the colours (its FlatColours), else every colour is
// the mesh's flat colour and every specular its flat specular (both arrays sized 4 * normals first).
void FlatColours(Part& part) {
    part.coloursWritten = true;
    if (part.figure) {
        FigureMesh* mesh = part.node->mesh;
        part.figure->FlatColours(mesh->GetName(), mesh->flatColour, mesh->flatSpecular, &part.Colours(),
                                 &part.Speculars());
        return;
    }
    part.Colours().resize(part.node->mesh->buffers.normalCount * 4);
    part.Speculars().resize(part.node->mesh->buffers.normalCount * 4);
    Fill(part.Colours(), part.node->mesh->flatColour);
    Fill(part.Speculars(), part.node->mesh->flatSpecular);
}

// The part's light for the GPU, every draw of a mesh that is not flat: the model matrix, the scene's sun, the
// ambient colour (AmbientColour), the material's colours, the options' coefficient, the crown and the lights
// within the object's reach, where the draw shows them (drawn::LightPosition, as the scene's).
std::vector<render::MeshLightSource> g_partLights;
render::MeshLighting g_partLighting;

const render::MeshLighting* PartLighting(const Part& part) {
    render::MeshLighting& l = g_partLighting;
    l = render::MeshLighting{};
    // The world stack's top is the scene's camera times the part's placement for every part; the object
    // stack has the placement alone only for a part about to be lit on the CPU (a lit static part is
    // not pushed there). Outside the world's draw (the interface's figures) only the object stack.
    if (const Matrix4* camera = SceneCamera()) {
        l.model = *part.device->pipeline->Top(TransformPipeline::kWorldStack);
        l.camera = camera;
    } else {
        l.model = *part.device->pipeline->Top(TransformPipeline::kObjectStack);
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
    l.coefficient = options::kObjectsLightingCoeff;
    g_partLights.clear();
    for (int i = 0; part.lights && i < std::ssize(*part.lights); ++i) {
        Light* light = (*part.lights)[i];
        g_partLights.push_back(render::MeshLightSource{drawn::LightPosition(light), light->inverseRangeSquared,
                                                       {light->color[0], light->color[1], light->color[2]}});
    }
    l.lights = g_partLights.data();
    l.lightCount = static_cast<int>(g_partLights.size());
    return &l;
}

// Lighting (once, FigureMeshFlag::Lit). The part is lit on the GPU; the colours keep only the material's
// diffuse alpha (white otherwise), the speculars become the alpha unless the material keeps them.
void LightFigurePart(Part& part) {
    part.coloursWritten = true;
    if (part.figure) {
        part.figure->LightPart(part.node->mesh->GetName(), *part.lights, part.material, part.alpha, &part.Colours(), &part.Speculars());
    } else {
        Fill(part.Colours(), part.material->GpuLitColour());
    }
    if (!(part.material->flags & MaterialFlag::KeepSpecular)) {
        Fill(part.Speculars(), (static_cast<std::uint32_t>(part.alpha) & 0xFF) << 24);
    }
    part.node->mesh->flags |= FigureMeshFlag::Lit;
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

namespace {

// The mesh through the device, its positions moved by its offsets; textured when both textures are given. Returns
// whether it was drawn.
bool DeviceDraw(TextureDevice* device, FigureMesh* mesh, const render::MeshSource& source, const Texture* textureA,
                const Texture* textureB, int alpha, DrawFlag drawFlags) {
    const MeshBuffers& b = mesh->buffers;
    const VertexPacket* positions = b.vertices;
    if (!mesh->offsets.empty()) positions = MovedPositions(positions, b.vertexCount, mesh->offsets);
    render::SetMeshSource(&source);
    const MeshArgs args{positions, b.vertexCount, b.normals, b.normalCount, mesh->colorsA.data(), mesh->colorsB.data(),
                        b.texCoords, b.corners, b.cornerCount, b.words, b.wordCount, b.indices, b.indexCount,
                        mesh->textureOffsetU, mesh->textureOffsetV, alpha, drawFlags};
    const bool drawn = textureA && textureB ? device->DrawMeshBlocks2(textureA->surface, textureB->surface, args)
                                            : device->DrawMeshBlocks(args);
    render::SetMeshSource(nullptr);
    return drawn;
}

// An animated mesh steps to the world's time before it is drawn.
void AnimateSheet(FigureMeshDrawer* drawer) {
    if (!IsAnimatedSheet(drawer->mesh->meshType)) return;
    float time;
    TickTime(&time);
    drawer->Animate(time);
}

// The mesh through the device. FigureMeshFlag::Drawn is only set when a rectangle was asked for.
void DrawThroughDevice(Part& part) {
    FigureMesh* mesh = part.node->mesh;
    const render::MeshSource source = part.Source(false);
    const bool drawn = DeviceDraw(part.device, mesh, source, part.textureA, part.textureB, part.alpha, part.drawFlags);
    if (!part.rect) return;
    Rect* own = &part.node->screen;
    if (!drawn) {
        *own = {};
        *part.rect = {};
        return;
    }
    *own = part.device->drawnRect;
    *part.rect = *own;
    mesh->flags |= FigureMeshFlag::Drawn;
}

// The mesh through the owner's figure: DrawPartMoved(Textured) with the offsets, DrawPart(Textured)
// without; the rectangle from the figure's part header of that name.
void DrawThroughFigure(Part& part) {
    FigureMesh* mesh = part.node->mesh;
    bool moved = !mesh->offsets.empty();
    const std::string_view name = mesh->GetName();
    const float u = mesh->textureOffsetU;
    const float v = mesh->textureOffsetV;
    std::vector<std::uint32_t>* colours = &part.Colours();
    std::vector<std::uint32_t>* speculars = &part.Speculars();
    std::vector<Vector3>* offsets = &mesh->offsets;
    const render::MeshSource source = part.Source(true);
    render::SetMeshSource(&source);
    bool drawn;
    if (moved && part.Textured()) {
        drawn = part.figure->DrawPartMovedTextured(name, part.drawFlags, part.textureA->surface,
                                                    part.textureB->surface, colours, speculars, offsets, u, v);
    } else if (moved) {
        drawn = part.figure->DrawPartMoved(name, part.drawFlags, colours, speculars, offsets, u, v);
    } else if (part.Textured()) {
        drawn = part.figure->DrawPartTextured(name, part.drawFlags, part.textureA->surface,
                                               part.textureB->surface, colours, speculars, u, v);
    } else {
        drawn = part.figure->DrawPart(name, part.drawFlags, colours, speculars, u, v);
    }
    render::SetMeshSource(nullptr);

    if (part.rect) {
        auto node = part.figure->headers.find(name);
        Rect* own = &part.node->screen;
        *own = node->second.screen;
        *part.rect = *own;
    }
    if (drawn) part.node->mesh->flags |= FigureMeshFlag::Drawn;
}

} // namespace

// `lights` the light objects, `material` the drawing context (flags, light vector), `alpha` the low byte for
// the speculars, `rect` (optional) receives the screen rectangle, the textures (their surfaces) select the
// two-texture draw. Returns 1.
int FigureMeshDrawer::DrawLit(TextureDevice* device, const std::vector<Light*>& lights,
                              Material* material, int alpha, Rect* rect, Texture* textureA, Texture* textureB) {
    FigureMesh* currentMesh = mesh;
    currentMesh->flags &= ~FigureMeshFlag::Drawn;
    if (mesh->flags & FigureMeshFlag::Hidden) return 1;
    // Only the scene's parts reach here (the owner is an Object3D: map_object_draw, also for the interface's previews); the other meshes draw plain.
    TransformObject* ownerObject = mesh->object;
    Part part{this, device, &lights, material, alpha, rect, textureA, textureB,
              ownerObject ? static_cast<Object3D*>(ownerObject)->figure.Get() : nullptr, DrawFlag{}};

    const TransformPipeline::ClipResult visibility = device->pipeline->ClipTest(&part.node->mesh->box.bounds);
    if (visibility == TransformPipeline::ClipResult::Outside) return 1;
    AnimateSheet(this);

    std::uint32_t meshFlags = part.node->mesh->flags;
    if (meshFlags & FigureMeshFlag::Flat) {
        FlatColours(part);
    } else {
        part.lighting = PartLighting(part);
        if (!(meshFlags & FigureMeshFlag::Lit)) LightFigurePart(part);
    }

    if (visibility == TransformPipeline::ClipResult::Crossing) part.drawFlags = DrawFlag::Clipped;
    if (rect) part.drawFlags |= DrawFlag::ScreenRect;
    if (material->flags & MaterialFlag::KeepSpecular) part.drawFlags |= DrawFlag::Specular;
    if (part.figure) {
        DrawThroughFigure(part);
    } else {
        DrawThroughDevice(part);
    }
    DrawBounds(&g_sceneRenderer);
    return 1;
}

// --- Figure part bounds ------------------------------------------------------------------------------

// The finishing call of the part draws. With the debug flag kShowBounds of the owning manager's context set, the
// mesh's clip box is drawn as lines through the renderer: the corners are the bounds' centre plus, per axis,
// either the box minimum or the maximum; eight edges, two points each, in the original's order. See
// docs/notes/models.md ("Figure parts").

void FigureMeshDrawer::DrawBounds(SceneRenderer* renderer) {
    if (!(GetManagerContext()->flags & kShowBounds)) return;
    LineBatch lines{{}, 0, 0xFFFF0000u};
    const ClipBounds& bounds = mesh->box.bounds;
    const Vector3& origin = bounds.center;
    const Vector3& nearExtent = bounds.boxMin;
    const Vector3& farExtent = bounds.boxMax;
    // corner[i]: bit 2 near x, bit 1 near y, bit 0 near z (else far).
    float corner[8][3];
    for (int i = 0; i < 8; ++i) {
        corner[i][0] = origin.x + ((i & 4) ? nearExtent.x : farExtent.x);
        corner[i][1] = origin.y + ((i & 2) ? nearExtent.y : farExtent.y);
        corner[i][2] = origin.z + ((i & 1) ? nearExtent.z : farExtent.z);
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

// The material is not read; `alpha` goes to the device draw, `rect` (optional) receives the screen rectangle
// (empty when off screen). Returns 1.
int FigureMeshDrawer::DrawPlain(TextureDevice* device, Material*, int alpha, Rect* rect) {
    if (mesh->flags & FigureMeshFlag::Hidden) return 1;
    DrawFlag drawFlags = {};
    mesh->flags &= ~FigureMeshFlag::Drawn;
    const TransformPipeline::ClipResult visibility = device->pipeline->ClipTest(&mesh->box.bounds);
    if (visibility == TransformPipeline::ClipResult::Outside) return 1;
    AnimateSheet(this);

    std::uint32_t meshFlags = mesh->flags;
    if (meshFlags & FigureMeshFlag::Flat) {
        Fill(mesh->colorsA, mesh->flatColour);
        Fill(mesh->colorsB, mesh->flatSpecular);
    } else if (!(meshFlags & FigureMeshFlag::Lit)) {
        drawFlags = DrawFlag::Colours;
    }
    if (visibility == TransformPipeline::ClipResult::Crossing) drawFlags |= DrawFlag::Clipped;
    if (rect) drawFlags |= DrawFlag::ScreenRect;

    const render::MeshSource source{mesh, nullptr, !mesh->offsets.empty(), lib::Has(meshFlags, FigureMeshFlag::Flat)};
    const bool drawn = DeviceDraw(device, mesh, source, nullptr, nullptr, alpha, drawFlags);
    if (rect) {
        Rect* own = &screen;
        if (!drawn) {
            *own = {};
            *rect = {};
        } else {
            *own = device->drawnRect;
            *rect = *own;
        }
    }
    if (drawn) mesh->flags |= FigureMeshFlag::Drawn;
    DrawBounds(&g_sceneRenderer);
    return 1;
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
