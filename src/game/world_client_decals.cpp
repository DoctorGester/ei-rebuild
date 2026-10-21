// 0056C750: the client world's decal projector (object shadows 0051CE60, ground prints 006CEDE0).
// The decal builder (ground_decal.h) is a function-local static of the original (007AF488, guard
// bit 1 of 007AF504), as is the work array of transformed part points (007AF478, bit 2); both are
// made on first use and freed at exit by the original's handlers. The builder's members are called
// through their addresses (the verify wrappers' path until S21d; AddTriangle's rewrite was left out of
// game.exe by disabled.txt until S21e, the image runs it). See docs/notes/models.md ("Ground prints").
#include "world.h"

#include "figure_mesh.h"          // VertexPacket, MeshCorner
#include "ground_decal.h"
#include "link_figure.h"          // CLinkMesh, ITransformPipeline
#include "map_object_figures.h"   // CObject3D
#include "model.h"                // CTransformPipeline, ScenePipeline
#include "originals_library.h"    // GameAtExit
#include "terrain.h"

#include "ei/hook.h"
#include "render/render.h"
#include "lib/stlp.h"
#include "lib/vecmath.h"
#include "game_data.h"

#include <cstdint>
#include <cstring>

namespace {

DecalBuilder& Builder() { return ei::GameData<DecalBuilder>(0x007AF488); }
lib::AlignedArray<Vector4>& PartPoints() { return ei::GameData<lib::AlignedArray<Vector4>>(0x007AF478); }
std::uint8_t& Guards() { return ei::GameData<std::uint8_t>(0x007AF504); }
void(__cdecl* const kDestroyBuilder)() = reinterpret_cast<void(__cdecl*)()>(0x0056CE30);
void(__cdecl* const kFreePartPoints)() = reinterpret_cast<void(__cdecl*)()>(0x0056CE10);

// A scene node's matrix from its orientation and position (the bottom row 0, 0, 0, 1).
void BuildNodeMatrix(CObject3D* node) {
    node->orientation.ToMatrix(node->worldMatrix.m);
    float* m = node->worldMatrix.m;
    m[11] = node->position.z;
    m[3] = node->position.x;
    m[7] = node->position.y;
    m[14] = 0.0f;
    m[13] = 0.0f;
    m[12] = 0.0f;
    m[15] = 1.0f;
}

// Pushes `m` onto the pipeline's decal stack as the original inlines it: the product through
// 004D9880, whose terms sum in another order than PushMatrix's (004D9B80) (X29).
void PushDecalMatrix(CTransformPipeline* pipeline, const float* m) {
    MatrixStack& stack = pipeline->stacks_[CTransformPipeline::kDecalStack];
    if (stack.count == 0) {
        stack.count = 1;
        std::memcpy(stack.matrices, m, sizeof(Matrix4));
        return;
    }
    ++stack.count;
    MatrixMultiply(stack.matrices[stack.count - 1].m, nullptr, stack.matrices[stack.count - 2].m, m);
}

Matrix4 Transposed(const Matrix4& m) {
    Matrix4 t;
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column) t.m[column * 4 + row] = m.m[row * 4 + column];
    return t;
}

// The part's triangles (its figure's part of the same name), transformed by the pipeline's
// stack-14 top, added to the decal. As in the original, GetPartData's result is not checked: a
// missing part leaves the previous part's data in use (ours starts from none).
struct PartData {
    VertexPacket* vertices = nullptr;
    MeshCorner* corners = nullptr;
    std::uint16_t* indices = nullptr;
    int blocks = 0;
    int indexCount = 0;
};

void AddPartTriangles(CTransformPipeline* pipeline, CLinkMesh* figure, CObject3D* part, PartData& data) {
    const Matrix4* top = pipeline->Top(CTransformPipeline::kDecalStack);
    {
        stlp::string name(part->GetName());
        figure->GetPartData(&name, &data.vertices, &data.corners, &data.indices, &data.blocks, &data.indexCount);
    }
    lib::AlignedArray<Vector4>& points = PartPoints();
    points.SetSizeDiscard(data.blocks * 4);
    Matrix4 transposed = Transposed(*top);
    pipeline->transform_->TransformPoints(&transposed, data.vertices, points.data, data.blocks);
    DecalBuilder& builder = Builder();
    builder.Reserve(data.indexCount * 6);
    for (unsigned i = 0; i < static_cast<unsigned>(data.indexCount); i += 3) {
        const Vector4* a = &points[data.corners[data.indices[i]].index[0]];
        const Vector4* b = &points[data.corners[data.indices[i + 1]].index[0]];
        const Vector4* c = &points[data.corners[data.indices[i + 2]].index[0]];
        builder.AddTriangle(reinterpret_cast<const Vector3*>(a), reinterpret_cast<const Vector3*>(b),
                           reinterpret_cast<const Vector3*>(c));
    }
}

} // namespace

void CWorldClient::ProjectDecal(const Vector3* direction, const Vector3* point, float depth,
                                VertexPacket* const* corners, lib::AlignedArray<VertexPacket>* points,
                                lib::AlignedArray<DecalTexCoord>* texCoords, lib::AlignedArray<std::uint16_t>* indices,
                                std::uint8_t skipOnPlatform, std::uint8_t objects) {
    render::BeginDecalTime(!skipOnPlatform);   // the ground prints skip the ground under a platform
    DecalBuilder& builder = Builder();
    if (!(Guards() & 1)) {
        Guards() |= 1;
        builder.Construct();
        GameAtExit(kDestroyBuilder);
    }
    if (!(Guards() & 2)) {
        Guards() |= 2;
        PartPoints() = lib::AlignedArray<Vector4>{};
        GameAtExit(kFreePartPoints);
    }
    auto* quad = reinterpret_cast<float* const*>(corners);
    builder.Begin(points, texCoords, indices, quad, direction, point, depth);
    if (!skipOnPlatform || !platforms_.Contains(point->x, point->y)) terrain_->AddDecalTriangles(&builder, quad);

    CTransformPipeline* pipeline = ScenePipeline();
    if (objects) {
        PartData data;
        for (PlatformNode* face = platforms_.list.head; face;) {
            PlatformNode* next = face->next;
            auto* object = static_cast<CObject3D*>(ObjectWithId(face->data.id));
            face = next;
            if (!object) continue;
            // The object's bounding sphere about its origin, then about its centre.
            if (!builder.Touches(&object->position, Length<Sum3::XYZ>(object->center) + object->radius))
                continue;
            BuildNodeMatrix(object);
            Vector3 centre = TransformColumnNormal<Sum3::ZYX, Sum3::ZXY, Sum3::ZXY>(object->worldMatrix, object->center) +
                             object->position;
            if (!builder.Touches(&centre, object->radius)) continue;
            PushDecalMatrix(pipeline, object->worldMatrix.m);
            auto* figure = reinterpret_cast<CLinkMesh*>(object->figure);
            if (figure) {
                // The parts are scene nodes laid out as CObject3D's first members (position,
                // orientation, matrix); CFigureMesh's declaration contradicts it (models.md, C9).
                for (int i = 0; i < object->parts.size; ++i) {
                    auto* part = static_cast<CObject3D*>(object->parts[i]);
                    BuildNodeMatrix(part);
                    PushDecalMatrix(pipeline, part->worldMatrix.m);
                    AddPartTriangles(pipeline, figure, part, data);
                    pipeline->PopMatrix(CTransformPipeline::kDecalStack);
                }
            }
            pipeline->PopMatrix(CTransformPipeline::kDecalStack);
        }
    }
    builder.Finish();
    render::EndDecalTime(points->size * 4);
}
EI_REPLACE_METHOD(0x0056C750, CWorldClient::ProjectDecal);
