// The draw of one figure part: the mesh is clip-tested, advanced when animated, its colours filled (flat meshes)
// or lit once, then drawn through the device or through the owner's figure. See docs/notes/models.md ("Figure
// parts").
#include "figure_mesh.h"
#include "game_singletons.h"
#include "link_figure.h"
#include "map_object_figures.h"
#include "material.h"
#include "model.h"
#include "options.h"
#include "render/render.h"
#include "spell.h"
#include "texture.h"
#include "world.h"
#include "world_data.h"

#include "lib/containers.h"
#include "lib/gameapi.h"
#include "lib/nan_audit.h"
#include "lib/pc24.h"
#include "lib/text_case.h"

#include <bit>
#include <cstdint>
#include <cstdlib>
#include <cstring>
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

void Fill(std::vector<std::uint32_t>& a, std::uint32_t value) {
    for (int i = 0; i < std::ssize(a); ++i) a[i] = value;
}

// --- The part --------------------------------------------------------------------------------------

struct Part {
    FigureMeshDrawer* node;
    TextureDevice* device;
    const std::vector<CLight*>* lights;
    Material* material;
    int alpha;
    RECT* rect;
    CTexture* textureA;
    CTexture* textureB;
    LinkMesh* figure;        // the mesh owner's figure (CObject3D::figure), or null
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
    if (const CLight* sun = g_sceneRenderer.sun) {
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
        const CLight* light = (*part.lights)[i];
        g_partLights.push_back(render::MeshLightSource{light->localPosition, light->inverseRangeSquared,
                                                       {light->color[0], light->color[1], light->color[2]}});
    }
    l.lights = g_partLights.data();
    l.lightCount = static_cast<int>(g_partLights.size());
    return &l;
}

// Lighting (once, FigureMeshFlag::Lit). The new renderer lights the part on the GPU; the colours keep only the material's
// diffuse alpha (white otherwise), the speculars become the alpha unless the material keeps them.
void Light(Part& part) {
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
    RECT* own = &part.node->screen;
    if (result == 2) {
        gameapi::SetRectEmpty(own);
        gameapi::SetRectEmpty(part.rect);
        return;
    }
    gameapi::CopyRect(own, &part.device->drawnRect);
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
        RECT* own = &part.node->screen;
        gameapi::CopyRect(own, &node->second.screen);
        *part.rect = *own;
    }
    if (result != 2) part.node->mesh->flags |= FigureMeshFlag::Drawn;
}

} // namespace

// `lights` the light objects, `material` the drawing context (flags, light vector), `alpha` the low byte for
// the speculars, `rect` (optional) receives the screen rectangle, the textures (their surfaces) select the
// two-texture draw. Returns 1.
int FigureMeshDrawer::DrawLit(TextureDevice* device, const std::vector<CLight*>* lights,
                              Material* material, int alpha, RECT* rect, CTexture* textureA, CTexture* textureB) {
    FigureMesh* currentMesh = mesh;
    currentMesh->flags &= ~FigureMeshFlag::Drawn;
    if (mesh->flags & FigureMeshFlag::Hidden) return 1;
    TransformObject* ownerObject = mesh->object;
    Part part{this, device, lights, material, alpha, rect, textureA, textureB,
              ownerObject ? static_cast<LinkMesh*>(static_cast<CObject3D*>(ownerObject)->figure) : nullptr, 0};

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
        if (!(meshFlags & FigureMeshFlag::Lit)) Light(part);
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
