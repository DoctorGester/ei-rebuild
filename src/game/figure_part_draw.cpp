// The draw of one figure part: the mesh is clip-tested, advanced when animated, its colours filled (flat meshes)
// or lit once, then drawn through the device or through the owner's figure. See docs/notes/models.md ("Figure
// parts").
#include "ei/x87.h"
#include "figure_mesh.h"
#include "material.h"
#include "link_figure.h"
#include "map_object_figures.h"
#include "model.h"
#include "spell.h"
#include "texture.h"
#include "world_data.h"
#include "render/render.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "world.h"
#include "library_trees.h"
#include "game_data.h"
#include "game_singletons.h"
#include "lib/nan_audit.h"
#include "options.h"

#include <bit>
#include <cstdint>
#include <cstring>

namespace {

// The positions with the offsets added (a lib::AlignedArray; a function-local static, freed at exit).
constinit lib::AlignedArray<VertexPacket> g_movedPositions = /*007C2E48*/ {};
lib::AlignedArray<VertexPacket>& MovedPositions() { return g_movedPositions; }
void FreeMovedPositions() { g_movedPositions.FreeData(); }
std::uint8_t g_movedPositionsGuard = /*007C2E58*/ {};
// The scale the lights' colours are set with and the "crown" option.
float LightingCoefficient() { return options::TheOptions()->objectsLightingCoeff_; }
int CrownOption() { return options::TheOptions()->floraSpecialLight_; }

// --- Helpers -------------------------------------------------------------------------------------


const char* MeshName(CFigureMesh* mesh) { return mesh->GetName(); }

// --- x87 sequences --------------------------------------------------------------------------------

void AddOffset(const float* offset, float* target) { *target = *offset + *target; }

// The world time: whole ticks plus the fraction of the current one, rounded to a float.
void TickTime(float* out) {
    const CWorldClient* world = TheClientWorld();
    *out = static_cast<float>(static_cast<double>(world->tick_) + world->tickFraction_);
}

// --- Arrays --------------------------------------------------------------------------------------

// The colour arrays (CFigureMesh::colorsA_ / colorsB_) are sized with the inlined SetSize that drops the
// contents (SetSizeDiscard), the moved positions with the one that keeps them (SetSize).
using ColourArray = lib::AlignedArray<std::uint32_t>;

void Fill(ColourArray& a, std::uint32_t value) {
    for (int i = 0; i < a.size; ++i) a.data[i] = value;
}

// --- The part --------------------------------------------------------------------------------------

struct Part {
    FigureMeshDrawer* node;
    CTextureDevice* device;
    const FigureLights* lights;
    CMaterial* material;
    int alpha;
    RECT* rect;
    CTexture* textureA;
    CTexture* textureB;
    CLinkMesh* figure;        // the mesh owner's figure (CObject3D::figure), or null
    int drawFlags;            // 1 rectangle wanted, 2 crossing the view, 8 material flag 0x10
    bool coloursWritten = false;   // this draw filled or lit the colour arrays (render::MeshSource)
    const render::MeshLighting* lighting = nullptr;   // lit on the GPU (not a flat mesh)

    CFigureMesh* Mesh() const { return node->mesh_; }
    // The new renderer keeps the mesh on the GPU: what the device's draw below draws.
    render::MeshSource Source(bool throughFigure) const {
        return render::MeshSource{Mesh(), throughFigure ? figure : nullptr, Mesh()->offsets_.size != 0, coloursWritten,
                                  lighting};
    }
    ColourArray& Colours() const { return Mesh()->colorsA_; }
    ColourArray& Speculars() const { return Mesh()->colorsB_; }
    bool Textured() const { return textureA && textureB; }
};

std::uint32_t OffsetBits(float offset) { return std::bit_cast<std::uint32_t>(offset); }

// Flat meshes (flag 0x100): the figure fills the colours (its FlatColours), else every colour is
// the mesh's flat colour and every specular its flat specular (both arrays sized 4 * normals first).
void FlatColours(Part& part) {
    part.coloursWritten = true;
    if (part.figure) {
        const char* name = MeshName(part.Mesh());
        std::string s(name);
        CFigureMesh* mesh = part.Mesh();
        part.figure->FlatColours(&s, mesh->flatColour_, mesh->flatSpecular_, &part.Colours(), &part.Speculars());
        return;
    }
    part.Colours().SetSizeDiscard(part.Mesh()->buffers_.normalCount * 4);
    part.Speculars().SetSizeDiscard(part.Mesh()->buffers_.normalCount * 4);
    Fill(part.Colours(), part.Mesh()->flatColour_);
    Fill(part.Speculars(), part.Mesh()->flatSpecular_);
}

// The part's light for the GPU, every draw of a mesh that is not flat: the model matrix, the scene's sun, the
// transform object's ambient colour, the material's colours, the options' coefficient, the crown and the lights
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
        l.model = *part.device->pipeline_->Top(1);
        l.camera = camera;
    } else {
        l.model = *part.device->pipeline_->Top(3);
    }
    if (const CLight* sun = worlddata::SceneSun()) {
        l.sun = true;
        l.sunDirection = sun->direction_;
        for (int k = 0; k < 3; ++k) l.sunColour[k] = sun->color_[k];
    }
    const float* ambient = TransformAmbientColour();
    for (int k = 0; k < 3; ++k) l.ambientColour[k] = ambient[k];
    const MaterialColors& c = part.material->colors;
    l.diffuseColour[0] = c.diffuse.x;
    l.diffuseColour[1] = c.diffuse.y;
    l.diffuseColour[2] = c.diffuse.z;
    l.diffuseColour[3] = c.diffuse.w;
    l.emissiveColour[0] = c.emissive.x;
    l.emissiveColour[1] = c.emissive.y;
    l.emissiveColour[2] = c.emissive.z;
    l.coefficient = LightingCoefficient();
    l.crown = CrownOption() != 0 && libcrt::Stricmp(MeshName(part.Mesh()), "crown") == 0;
    g_partLights.clear();
    for (int i = 0; part.lights && i < part.lights->size; ++i) {
        const CLight* light = part.lights->data[i];
        g_partLights.push_back(render::MeshLightSource{light->localPosition_, light->inverseRangeSquared_,
                                                       {light->color_[0], light->color_[1], light->color_[2]}});
    }
    l.lights = g_partLights.data();
    l.lightCount = static_cast<int>(g_partLights.size());
    return &l;
}

// Lighting (once, flag 0x10). The new renderer lights the part on the GPU; the colours keep only the material's
// diffuse alpha (white otherwise), the speculars become the alpha unless the material keeps them.
void Light(Part& part) {
    part.coloursWritten = true;
    if (CLight* sun = worlddata::SceneSun()) part.device->pipeline_->transform_->AddSun(sun);
    if (part.figure) {
        const char* name = MeshName(part.Mesh());
        std::string s(name);
        part.figure->LightPart(&s, part.lights, part.material, part.alpha, &part.Colours(), &part.Speculars());
    } else {
        Fill(part.Colours(), part.material->GpuLitColour());
    }
    if (!(part.material->flags_ & 0x10)) {
        Fill(part.Speculars(), (static_cast<std::uint32_t>(part.alpha) & 0xFF) << 24);
    }
    part.Mesh()->flags_ |= 0x10;
}

// The positions with the per-vertex offsets (CFigureMesh::offsets_) added.
const VertexPacket* MovedPositionsOf(CFigureMesh* mesh) {
    lib::AlignedArray<VertexPacket>& moved = MovedPositions();
    moved.SetSize(mesh->buffers_.vertexCount);
    std::memcpy(moved.data, mesh->buffers_.vertices, static_cast<std::size_t>(mesh->buffers_.vertexCount) * 64);
    for (int i = 0; i < mesh->offsets_.size; ++i) {
        const float* offset = &mesh->offsets_.data[i].x;
        VertexPacket& packet = moved.data[i >> 2];
        AddOffset(&offset[0], &packet.block[0][i & 3]);
        AddOffset(&offset[1], &packet.block[1][i & 3]);
        AddOffset(&offset[2], &packet.block[2][i & 3]);
    }
    return moved.data;
}

// The mesh through the device. The drawn flag (0x40) is only set when a rectangle was asked for.
void DrawThroughDevice(Part& part) {
    CFigureMesh* mesh = part.Mesh();
    const VertexPacket* positions = mesh->buffers_.vertices;
    int blocks = mesh->buffers_.vertexCount;
    if (mesh->offsets_.size != 0) positions = MovedPositionsOf(mesh);
    mesh = part.Mesh();
    const MeshBuffers& b = mesh->buffers_;
    const render::MeshSource source = part.Source(false);
    render::SetMeshSource(&source);
    int result;
    if (part.Textured()) {
        result = part.device->DrawMeshBlocks2(part.textureA->surface_, part.textureB->surface_, positions, blocks,
                                              b.normals, b.normalCount, mesh->colorsA_.data, mesh->colorsB_.data,
                                              b.texCoords, b.texCoordCount, b.corners, b.cornerCount, b.words,
                                              b.wordCount, b.indices, b.indexCount,
                                              mesh->textureOffsetU_, mesh->textureOffsetV_, part.alpha, part.drawFlags);
    } else {
        result = part.device->DrawMeshBlocks(positions, blocks, b.normals, b.normalCount, mesh->colorsA_.data,
                                             mesh->colorsB_.data, b.texCoords, b.texCoordCount, b.corners, b.cornerCount,
                                             b.words, b.wordCount, b.indices, b.indexCount,
                                             mesh->textureOffsetU_, mesh->textureOffsetV_, part.alpha, part.drawFlags);
    }
    render::SetMeshSource(nullptr);
    if (!part.rect) return;
    RECT* own = &part.node->screen_;
    if (result == 2) {
        gameapi::SetRectEmpty(own);
        gameapi::SetRectEmpty(part.rect);
        return;
    }
    gameapi::CopyRect(own, &part.device->drawnRect_);
    *part.rect = *own;
    part.Mesh()->flags_ |= 0x40;
}

// The mesh through the owner's figure: DrawPartMoved(Textured) with the offsets, DrawPart(Textured)
// without; the rectangle from the figure's part header of that name.
void DrawThroughFigure(Part& part) {
    CFigureMesh* mesh = part.Mesh();
    bool moved = mesh->offsets_.size != 0;
    const char* name = MeshName(mesh);
    std::string s(name);
    mesh = part.Mesh();
    std::uint32_t u = OffsetBits(mesh->textureOffsetU_);
    std::uint32_t v = OffsetBits(mesh->textureOffsetV_);
    ColourArray* colours = &part.Colours();
    ColourArray* speculars = &part.Speculars();
    lib::AlignedArray<Vector3>* offsets = &mesh->offsets_;
    const render::MeshSource source = part.Source(true);
    render::SetMeshSource(&source);
    int result;
    if (moved && part.Textured()) {
        result = part.figure->DrawPartMovedTextured(&s, part.drawFlags, part.textureA->surface_,
                                                    part.textureB->surface_, colours, speculars, offsets, u, v);
    } else if (moved) {
        result = part.figure->DrawPartMoved(&s, part.drawFlags, colours, speculars, offsets, u, v);
    } else if (part.Textured()) {
        result = part.figure->DrawPartTextured(&s, part.drawFlags, part.textureA->surface_,
                                               part.textureB->surface_, colours, speculars, u, v);
    } else {
        result = part.figure->DrawPart(&s, part.drawFlags, colours, speculars, u, v);
    }
    render::SetMeshSource(nullptr);

    if (part.rect) {
        std::string key(MeshName(part.Mesh()));
        LinkHeaderMap::Node* node = nullptr;
        node = lib_trees::FindLinkHeader(&part.figure->headers_, &key);
        RECT* own = &part.node->screen_;
        gameapi::CopyRect(own, &node->second.screen);
        *part.rect = *own;
    }
    if (result != 2) part.Mesh()->flags_ |= 0x40;
}

} // namespace

// `lights` the light objects, `material` the drawing context (flags, light vector), `alpha` the low byte for
// the speculars, `rect` (optional) receives the screen rectangle, the textures (their surfaces) select the
// two-texture draw. Returns 1.
int FigureMeshDrawer::DrawLit(CTextureDevice* device, const FigureLights* lights,
                              CMaterial* material, int alpha, RECT* rect, CTexture* textureA, CTexture* textureB) {
    CFigureMesh* mesh = mesh_;
    mesh->flags_ &= ~0x40u;
    if (mesh_->flags_ & 0x80) return 1;
    CTransformObject* owner = mesh_->object_;
    Part part{this, device, lights, material, alpha, rect, textureA, textureB,
              owner ? static_cast<CLinkMesh*>(static_cast<CObject3D*>(owner)->figure) : nullptr, 0};
    if (!(g_movedPositionsGuard & 1)) {
        g_movedPositionsGuard |= 1;
        MovedPositions() = lib::AlignedArray<VertexPacket>{};
        libcrt::AtExit(FreeMovedPositions);
    }

    int visibility = device->pipeline_->ClipTest(&part.Mesh()->box_.bounds);
    if (visibility == 2) return 1;
    int type = static_cast<int>(part.Mesh()->meshType_);
    if (type >= 13 && type <= 16) {
        float time;
        TickTime(&time);
        flags_ |= kAnimatedDrawerFlags;
        Animate(time);
    }

    std::uint32_t meshFlags = part.Mesh()->flags_;
    if (meshFlags & 0x100) {
        FlatColours(part);
    } else {
        part.lighting = PartLighting(part);
        if (!(meshFlags & 0x10)) Light(part);
    }

    if (visibility == 1) part.drawFlags = 2;
    if (rect) part.drawFlags |= 1;
    if (material->flags_ & 0x10) part.drawFlags |= 8;
    if (part.figure) {
        DrawThroughFigure(part);
    } else {
        DrawThroughDevice(part);
    }
    DrawBounds(TheSceneRenderer());
    return 1;
}
