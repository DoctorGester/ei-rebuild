// The client world's decal projector (object shadows, ground prints). The decal builder (ground_decal.h) is a
// function-local static of the original game, as is the work array of transformed part points; both are made
// on first use and freed at exit. See docs/notes/models.md ("Ground prints").
#include "world.h"
#include "at_exit_functions.h"

#include "figure_mesh.h"          // VertexPacket, MeshCorner
#include "ground_decal.h"
#include "link_figure.h"          // CLinkMesh, ITransformPipeline
#include "map_object.h"           // CMapObject (ObjectWithId)
#include "map_object_figures.h"   // CObject3D
#include "model.h"                // CTransformPipeline, ScenePipeline
#include "terrain.h"

#include "ei/hook.h"
#include "render/render.h"
#include "lib/stlp.h"
#include "lib/vecmath.h"
#include "game_data.h"

#include <cstdint>
#include <cstring>

namespace {

DecalBuilder g_builder = /*007AF488*/ {};   // the function-local static of the draw (guard bit 0 of g_guards)
DecalBuilder& Builder() { return g_builder; }
constinit lib::AlignedArray<Vector4> g_partPoints = /*007AF478*/ {};
lib::AlignedArray<Vector4>& PartPoints() { return g_partPoints; }
void __cdecl FreePartPoints() { g_partPoints.FreeData(); }
std::uint8_t g_guards = /*007AF504*/ {};
std::uint8_t& Guards() { return g_guards; }

// A scene node's matrix (an object's or a figure part's) from its rotation and position (the
// bottom row 0, 0, 0, 1).
void BuildNodeMatrix(const Quaternion& rotation, const Vector3& position, Matrix4& matrix) {
    rotation.ToMatrix(matrix.m);
    float* m = matrix.m;
    m[11] = position.z;
    m[3] = position.x;
    m[7] = position.y;
    m[14] = 0.0f;
    m[13] = 0.0f;
    m[12] = 0.0f;
    m[15] = 1.0f;
}

// Pushes `m` onto the pipeline's decal stack: the product goes through a library call whose terms sum
// in another order than PushMatrix's.
void PushDecalMatrix(CTransformPipeline* pipeline, const float* m) {
    MatrixStack& stack = pipeline->stacks_[CTransformPipeline::kDecalStack];
    if (stack.count == 0) {
        stack.count = 1;
        std::memcpy(stack.matrices, m, sizeof(Matrix4));
        return;
    }
    ++stack.count;
    stack.matrices[stack.count - 1].Multiply(stack.matrices[stack.count - 2].m, m);
}

Matrix4 Transposed(const Matrix4& m) {
    Matrix4 t;
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column) t.m[column * 4 + row] = m.m[row * 4 + column];
    return t;
}

// The part's triangles (its figure's part of the same name), transformed by the pipeline's
// stack-14 top, added to the decal. As in the original, GetPartData's result is not checked: a missing
// part leaves the previous part's data in use (ours starts from none).
struct PartData {
    VertexPacket* vertices = nullptr;
    MeshCorner* corners = nullptr;
    std::uint16_t* indices = nullptr;
    int blocks = 0;
    int indexCount = 0;
};

void AddPartTriangles(CTransformPipeline* pipeline, CLinkMesh* figure, CFigureMesh* part, PartData& data) {
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
        libcrt::AtExit(ei::short_functions::Ret);   // the builder: nothing to destroy
    }
    if (!(Guards() & 2)) {
        Guards() |= 2;
        PartPoints() = lib::AlignedArray<Vector4>{};
        libcrt::AtExit(FreePartPoints);
    }
    auto* quad = reinterpret_cast<float* const*>(corners);
    builder.Begin(points, texCoords, indices, quad, direction, point, depth);
    if (!skipOnPlatform || !platforms_.Contains(point->x, point->y)) terrain_->AddDecalTriangles(&builder, quad);

    CTransformPipeline* pipeline = ScenePipeline();
    if (objects) {
        PartData data;
        for (PlatformNode* face = platforms_.list.head; face;) {
            PlatformNode* next = face->next;
            CObject3D* object = ObjectWithId(face->data.id);
            face = next;
            if (!object) continue;
            // The object's bounding sphere about its origin, then about its centre.
            if (!builder.Touches(&object->localPosition_, Length<Sum3::XYZ>(object->center) + object->radius))
                continue;
            BuildNodeMatrix(object->localRotation_, object->localPosition_, object->worldMatrix);
            Vector3 centre = TransformColumnNormal<Sum3::ZYX, Sum3::ZXY, Sum3::ZXY>(object->worldMatrix, object->center) +
                             object->localPosition_;
            if (!builder.Touches(&centre, object->radius)) continue;
            PushDecalMatrix(pipeline, object->worldMatrix.m);
            CLinkMesh* figure = object->figure;
            if (figure) {
                for (int i = 0; i < object->parts.size; ++i) {
                    CFigureMesh* part = object->parts[i];
                    BuildNodeMatrix(part->localRotation_, part->localPosition_, part->worldMatrix_);
                    PushDecalMatrix(pipeline, part->worldMatrix_.m);
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
EI_REWRITTEN(0x0056C750, CWorldClient::ProjectDecal);
EI_REPLACE(0x0056CE10, FreePartPoints);
