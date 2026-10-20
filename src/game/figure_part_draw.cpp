// The draw of one figure part (005A7A70, thiscall on the part's drawer, RET 0x1C): the drawer's
// mesh (FigureMeshDrawer::mesh_, a CFigureMesh) is clip-tested against the view; animated meshes
// (type 13..16) are advanced to the current time; the mesh's colours are filled (flat meshes, flag
// 0x100) or lit once (flag 0x10 marks them lit): the lights are moved into the mesh's space through
// the inverse of the model matrix, the transform object (or the owner's figure) computes the
// colours, the "crown" mesh is pulled 0.6 of the way to its average colour when 0079BD28 is set; then the
// mesh is drawn through the device (004E9010 / 004E9A50, with the per-vertex offsets added into a
// copy when there are any) or, when the mesh's owner has a figure (CObject3D::figure), through
// that figure's own slots; the screen rectangle goes to the drawer and to `rect`. See
// docs/notes/models.md ("Figure parts").
#include "ei/hook.h"
#include "ei/x87.h"
#include "figure_mesh.h"          // CFigureMesh, FigureMeshDrawer, FigureLights
#include "material.h"             // CMaterial
#include "link_figure.h"          // CLinkMesh, ITransformPipeline
#include "map_object_figures.h"   // CObject3D
#include "model.h"                // CTransformPipeline, CSceneRenderer
#include "spell.h"                // CLight
#include "texture.h"              // CTextureDevice, CTexture
#include "world_data.h"           // SceneSun
#include "render/render.h"         // MeshSource
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "originals_ai.h"
#include "originals_library.h"
#include "originals_render.h"
#include "world.h"

#include <bit>
#include <cstdint>
#include <cstring>

namespace {

// The positions with the offsets added (007C2E48, a lib::AlignedArray; a function-local static,
// guard 007C2E58 bit 1, freed at exit by 005A8AE0).
lib::AlignedArray<VertexPacket>& MovedPositions() { return *reinterpret_cast<lib::AlignedArray<VertexPacket>*>(0x007C2E48); }
std::uint8_t& Guard() { return *reinterpret_cast<std::uint8_t*>(0x007C2E58); }
// The ambient level the lights' colours are set with (0079BE9C, float bits), the "crown" option
// (0079BD28), the flags an animated mesh's drawer gets (00790C0C) and the renderer (007AF060).
std::uint32_t AmbientBits() { return *reinterpret_cast<const std::uint32_t*>(0x0079BE9C); }
int CrownOption() { return *reinterpret_cast<const int*>(0x0079BD28); }
std::uint32_t AnimatedDrawerFlags() { return *reinterpret_cast<const std::uint32_t*>(0x00790C0C); }

// --- Forwarders ----------------------------------------------------------------------------------

// 004F16A0 CTransformPipeline::ClipTest (model.cpp): 2 = outside the view, 1 = crossing it.
// 005A7160: the drawer's animation at a time (float bits). 005A7290: after the draw (the renderer).
// A light's colours for a material (its two colours at +0x20 / +0x40) at the ambient level (float bits;
// CLight::SetLitColors, 0068D280).
void LightForMaterial(CLight* light, CMaterial* material, std::uint32_t ambient) {
    light->SetLitColors(reinterpret_cast<const float*>(material), std::bit_cast<float>(ambient));
}
// 005197B0: map<string, LinkPartHeader>::find on a figure's part headers: the node to *out.
void __fastcall FindPiece(LinkHeaderMap* a0, void*, LinkHeaderMap::Node** a2, const stlp::string* a3) {
    StringMapFindOriginal(a0, nullptr, reinterpret_cast<void**>(a2), a3);
}

// The mesh's name: its slot 7, CEngineObject::GetName (the mesh is an engine object; CFigureMesh
// keeps its vtable as a plain field).
const char* MeshName(CFigureMesh* mesh) { return reinterpret_cast<CEngineObject*>(mesh)->GetName(); }

// --- x87 sequences --------------------------------------------------------------------------------

// The inverse of the model matrix's upper 3x4 part (`m` row-major 4x4, the translation in column
// 3): out[0..11] three rows of four. Column 0's cofactors make the determinant and the inverse's
// column 0; the doubles are what the original keeps on the FPU stack (the cofactor of m4, 1 / det,
// out[2]); the others and the outputs are rounded to floats, and the translations reload the
// rounded outputs. Returns 0 (out untouched) when the determinant compares equal to 0 or is NaN
// (FCOM 0.0, TEST AH,40h).
int InvertModelMatrix(const float* m, float* out) {
    const double cofactor4 = static_cast<double>(m[2]) * m[9] - static_cast<double>(m[10]) * m[1];
    const float cofactor8 = static_cast<float>(static_cast<double>(m[6]) * m[1] - static_cast<double>(m[2]) * m[5]);
    const float cofactor0 = static_cast<float>(static_cast<double>(m[10]) * m[5] - static_cast<double>(m[9]) * m[6]);
    const double det = DotUnrounded<Sum3::ZYX, double>(cofactor0, cofactor4, cofactor8, m[0], m[4], m[8]);
    if (!(det < 0.0) && !(det > 0.0)) return 0;
    const double inverse = 1.0 / det;
    out[0] = static_cast<float>(cofactor0 * inverse);
    out[1] = static_cast<float>(cofactor4 * inverse);
    const double out2 = cofactor8 * inverse;
    out[2] = static_cast<float>(out2);
    out[3] = static_cast<float>(-DotUnrounded<Sum3::ZXY, double>(out[0], out[1], out2, m[3], m[7], m[11]));
    out[4] = static_cast<float>((static_cast<double>(m[8]) * m[6] - static_cast<double>(m[10]) * m[4]) * inverse);
    out[5] = static_cast<float>((static_cast<double>(m[10]) * m[0] - static_cast<double>(m[8]) * m[2]) * inverse);
    out[6] = static_cast<float>((static_cast<double>(m[2]) * m[4] - static_cast<double>(m[0]) * m[6]) * inverse);
    // (y + z) + x and (y + x) + z in the original: the same sums.
    out[7] = static_cast<float>(-DotUnrounded<Sum3::ZYX, double>(out[4], out[5], out[6], m[3], m[7], m[11]));
    out[8] = static_cast<float>((static_cast<double>(m[9]) * m[4] - static_cast<double>(m[8]) * m[5]) * inverse);
    out[9] = static_cast<float>((static_cast<double>(m[8]) * m[1] - static_cast<double>(m[0]) * m[9]) * inverse);
    out[10] = static_cast<float>((static_cast<double>(m[0]) * m[5] - static_cast<double>(m[4]) * m[1]) * inverse);
    out[11] = static_cast<float>(-DotUnrounded<Sum3::XYZ, double>(out[8], out[9], out[10], m[3], m[7], m[11]));
    return 1;
}

// A light's position into the mesh's space: each row ((r2*z + r1*y) + r0*x) + r3, rounded once
// (rows of four floats; the original writes z, y, x in that order).
void TransformPoint(const float* rows, const float* point, float* out) {
    for (int row = 2; row >= 0; --row) {
        const float* r = rows + 4 * row;
        out[row] = static_cast<float>(DotUnrounded<Sum3::ZYX, double>(r[0], r[1], r[2], point[0], point[1], point[2]) + r[3]);
    }
}

// The sun's direction into the mesh's space: the direction negated, no translation, with the
// original's order per row: (z + y) + x for the z and y rows, (x + z) + y for the x row.
void TransformSun(const float* rows, const float* direction, float* out) {
    const double x = -static_cast<double>(direction[0]);
    const double y = -static_cast<double>(direction[1]);
    const double z = -static_cast<double>(direction[2]);
    out[2] = static_cast<float>(DotUnrounded<Sum3::ZYX, double>(x, y, z, rows[8], rows[9], rows[10]));
    out[1] = static_cast<float>(DotUnrounded<Sum3::ZYX, double>(x, y, z, rows[4], rows[5], rows[6]));
    out[0] = static_cast<float>(DotUnrounded<Sum3::ZXY, double>(x, y, z, rows[0], rows[1], rows[2]));
}

// The "crown" mesh: every colour's channels pulled 0.6 of the way (0073CDE0) to the average colour.
// The sums accumulate channel * (1 / count) unrounded (1 / count rounded to a float); the averages
// are rounded, except that the first colour takes the red one unrounded (the original keeps it on
// the FPU stack into the first iteration); the new channels are rounded to floats, then to integers
// by FISTP; alpha is kept.
void CrownBlend(std::uint32_t* colours, int count) {
    constexpr float kPull = 0.6f;   // 0073CDE0
    const float share = static_cast<float>(1.0 / static_cast<double>(count));
    if (count <= 0) return;
    const auto channel = [](std::uint32_t colour, int shift) { return static_cast<int>((colour >> shift) & 0xFF); };
    double red = 0.0, green = 0.0, blue = 0.0;
    for (int i = 0; i < count; ++i) {
        const std::uint32_t colour = colours[i];
        red += static_cast<double>(channel(colour, 16)) * share;
        green += static_cast<double>(channel(colour, 8)) * share;
        blue += static_cast<double>(channel(colour, 0)) * share;
    }
    const float redAverage = static_cast<float>(red);
    const float greenAverage = static_cast<float>(green);
    const float blueAverage = static_cast<float>(blue);
    double redTarget = red;
    for (int i = 0; i < count; ++i) {
        const std::uint32_t colour = colours[i];
        const int r = channel(colour, 16), g = channel(colour, 8), b = channel(colour, 0);
        const float newRed = static_cast<float>((redTarget - r) * kPull + r);
        const float newGreen = static_cast<float>((static_cast<double>(greenAverage) - g) * kPull + g);
        const float newBlue = static_cast<float>((static_cast<double>(blueAverage) - b) * kPull + b);
        const auto ri = static_cast<std::uint32_t>(x87::fistp(newRed));
        const auto gi = static_cast<std::uint32_t>(x87::fistp(newGreen));
        const auto bi = static_cast<std::uint32_t>(x87::fistp(newBlue));
        colours[i] = ((((ri << 8) | gi) << 8) | (colour & 0xFF000000u)) | bi;
        redTarget = redAverage;
    }
}

// *target = *offset + *target (FLD offset, FADD, FSTP).
void AddOffset(const float* offset, float* target) { *target = *offset + *target; }

// The world time: whole ticks (FILD) plus the fraction of the current one, rounded to a float.
void TickTime(float* out) {
    const CWorldClient* world = GameClientWorld();
    *out = static_cast<float>(static_cast<double>(world->tick_) + world->tickFraction_);
}

// --- Arrays --------------------------------------------------------------------------------------

// The colour arrays (CFigureMesh::colorsA_ / colorsB_) are sized with the inlined SetSize that
// drops the contents (SetSizeDiscard; allocator 00406B50), the moved positions with the one that
// keeps them (SetSize; allocator 00452E70).
using ColourArray = lib::AlignedArray<std::uint32_t>;

void Fill(ColourArray& a, std::uint32_t value) {
    for (int i = 0; i < a.size; ++i) a.data[i] = value;
}

// --- The part --------------------------------------------------------------------------------------

struct Part {
    FigureMeshDrawer* node;   // this
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

    CFigureMesh* Mesh() const { return node->mesh_; }
    // The new renderer keeps the mesh on the GPU (R16d): what the device's draw below draws.
    render::MeshSource Source(bool throughFigure) const {
        return render::MeshSource{Mesh(), throughFigure ? figure : nullptr, Mesh()->offsets_.size != 0, coloursWritten};
    }
    ColourArray& Colours() const { return Mesh()->colorsA_; }
    ColourArray& Speculars() const { return Mesh()->colorsB_; }
    bool Textured() const { return textureA && textureB; }
};

// A pointer passed to the device's draws as a dword.
// A texture offset as the draws take it (its bits).
std::uint32_t OffsetBits(float offset) { return std::bit_cast<std::uint32_t>(offset); }
std::uint32_t Word(const void* p) { return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p)); }

// Flat meshes (flag 0x100): the figure fills the colours (its FlatColours), else every colour is
// the mesh's flat colour and every specular its flat specular (both arrays sized 4 * normals first).
void FlatColours(Part& part) {
    part.coloursWritten = true;
    if (part.figure) {
        const char* name = MeshName(part.Mesh());
        stlp::string s(name);
        CFigureMesh* mesh = part.Mesh();
        part.figure->FlatColours(&s, mesh->flatColour_, mesh->flatSpecular_, &part.Colours(), &part.Speculars());
        return;
    }
    part.Colours().SetSizeDiscard(part.Mesh()->buffers_.normalCount * 4);
    part.Speculars().SetSizeDiscard(part.Mesh()->buffers_.normalCount * 4);
    Fill(part.Colours(), part.Mesh()->flatColour_);
    Fill(part.Speculars(), part.Mesh()->flatSpecular_);
}

// The inverse model matrix of the last call, used when the matrix is singular (the original then
// reads stack slots it did not write, which usually hold the previous part's inverse).
float g_lastInverse[12];

// Lighting (once, flag 0x10): the lights and the sun (SceneSun) into the mesh's space, their
// colours for the material, then the figure (LightPart) or the transform object (LightMesh) lights
// the mesh; the crown blend; the speculars become the alpha unless the material keeps them.
void Light(Part& part) {
    part.coloursWritten = true;
    CTransformPipeline* pipeline = part.device->pipeline_;
    ITransformPipeline* transform = pipeline->transform_;
    InvertModelMatrix(pipeline->Top(3)->m, g_lastInverse);
    std::uint32_t ambient = AmbientBits();
    for (int i = 0; i < part.lights->count; ++i) {
        CLight* light = part.lights->data[i];
        Vector3 moved;
        TransformPoint(g_lastInverse, &light->position_.x, &moved.x);
        std::memcpy(&light->localPosition_, &moved, sizeof(moved));
        LightForMaterial(light, part.material, ambient);
    }
    if (CLight* sun = worlddata::SceneSun()) {
        Vector3 moved;
        TransformSun(g_lastInverse, &sun->direction_.x, &moved.x);
        std::memcpy(&sun->localDirection_, &moved, sizeof(moved));
        LightForMaterial(sun, part.material, ambient);
        transform->AddSun(sun);
    }

    if (part.figure) {
        const char* name = MeshName(part.Mesh());
        stlp::string s(name);
        part.figure->LightPart(&s, part.lights, part.material, part.alpha, &part.Colours(), &part.Speculars());
    } else {
        std::uint32_t vector[3];
        std::memcpy(vector, &part.material->colors.emissive, sizeof(vector));
        CFigureMesh* mesh = part.Mesh();
        transform->LightMesh(mesh->buffers_.vertices, mesh->buffers_.normals, part.Colours().data, mesh->buffers_.words,
                             mesh->buffers_.wordCount, part.lights->data, part.lights->count, vector);
    }

    if (CrownOption() != 0 && libcrt::Stricmp(MeshName(part.Mesh()), "crown") == 0) {
        CrownBlend(part.Colours().data, part.Colours().size);
    }
    if (!(part.material->flags_ & 0x10)) {
        Fill(part.Speculars(), (static_cast<std::uint32_t>(part.alpha) & 0xFF) << 24);
    }
    part.Mesh()->flags_ |= 0x10;
}

// The positions with the per-vertex offsets (CFigureMesh::offsets_) added, in 007C2E48.
const void* MovedPositionsOf(CFigureMesh* mesh) {
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
    const void* positions = mesh->buffers_.vertices;
    int blocks = mesh->buffers_.vertexCount;
    if (mesh->offsets_.size != 0) positions = MovedPositionsOf(mesh);
    mesh = part.Mesh();
    const MeshBuffers& b = mesh->buffers_;
    const render::MeshSource source = part.Source(false);
    render::SetMeshSource(&source);
    int result;
    if (part.Textured()) {
        result = DrawMesh2(part.device, nullptr, part.textureA->surface_, part.textureB->surface_, positions, blocks,
                           Word(b.normals), b.normalCount, Word(mesh->colorsA_.data), Word(mesh->colorsB_.data),
                           Word(b.texCoords), b.texCoordCount, Word(b.corners), b.cornerCount, Word(b.words),
                           b.wordCount, Word(b.indices), b.indexCount, OffsetBits(mesh->textureOffsetU_), OffsetBits(mesh->textureOffsetV_), part.alpha,
                           part.drawFlags);
    } else {
        result = DrawMesh(reinterpret_cast<char*>(part.device), nullptr, positions, blocks, Word(b.normals),
                          b.normalCount, Word(mesh->colorsA_.data), Word(mesh->colorsB_.data), Word(b.texCoords),
                          b.texCoordCount, Word(b.corners), b.cornerCount, Word(b.words), b.wordCount, Word(b.indices),
                          b.indexCount, OffsetBits(mesh->textureOffsetU_), OffsetBits(mesh->textureOffsetV_), part.alpha, part.drawFlags);
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
    stlp::string s(name);
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
        stlp::string key(MeshName(part.Mesh()));
        LinkHeaderMap::Node* node = nullptr;
        FindPiece(&part.figure->headers_, nullptr, &node, &key);
        RECT* own = &part.node->screen_;
        gameapi::CopyRect(own, &node->second.screen);
        *part.rect = *own;
    }
    if (result != 2) part.Mesh()->flags_ |= 0x40;
}

// 005A8AE0: frees the moved positions at exit (registered by its game address, as the original does).
constexpr std::uintptr_t kFreeMovedPositions = std::uintptr_t(0x005A8AE0);

} // namespace

// 005A7A70 (thiscall on the part's drawer, RET 0x1C). `lights` the light objects, `material` the
// drawing context (flags, light vector), `alpha` the low byte for the speculars, `rect` (optional)
// receives the screen rectangle, the textures (their surfaces) select the two-texture draw.
// Returns 1.
int FigureMeshDrawer::DrawLit(CTextureDevice* device, const FigureLights* lights,
                              CMaterial* material, int alpha, RECT* rect, CTexture* textureA, CTexture* textureB) {
    CFigureMesh* mesh = mesh_;
    mesh->flags_ &= ~0x40u;
    if (mesh_->flags_ & 0x80) return 1;
    CObject3D* owner = mesh_->object_;
    Part part{this, device, lights, material, alpha, rect, textureA, textureB,
              owner ? reinterpret_cast<CLinkMesh*>(owner->figure) : nullptr, 0};
    if (!(Guard() & 1)) {
        Guard() |= 1;
        MovedPositions() = lib::AlignedArray<VertexPacket>{};
        GameAtExit(reinterpret_cast<void(__cdecl*)()>(kFreeMovedPositions));
    }

    int visibility = device->pipeline_->ClipTest(&part.Mesh()->bounds_);
    if (visibility == 2) return 1;
    int type = static_cast<int>(part.Mesh()->meshType_);
    if (type >= 13 && type <= 16) {
        float time;
        TickTime(&time);
        flags_ |= AnimatedDrawerFlags();
        Animate(time);
    }

    std::uint32_t meshFlags = part.Mesh()->flags_;
    if (meshFlags & 0x100) {
        FlatColours(part);
    } else if (!(meshFlags & 0x10)) {
        Light(part);
    }

    if (visibility == 1) part.drawFlags = 2;
    if (rect) part.drawFlags |= 1;
    if (material->flags_ & 0x10) part.drawFlags |= 8;
    if (part.figure) {
        DrawThroughFigure(part);
    } else {
        DrawThroughDevice(part);
    }
    DrawBounds(static_cast<CSceneRenderer*>(SceneContext()));
    return 1;
}
EI_REPLACE_METHOD(0x005A7A70, FigureMeshDrawer::DrawLit);
