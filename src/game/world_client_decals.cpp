// The client world's decal projector (ground prints). The decal builder (ground_decal.h) is a
// function-local static of the original game, as is the work array of transformed part points; both are made
// on first use and freed at exit. See docs/notes/models.md ("Ground prints").
#include "figure_mesh.h"   // VertexPacket, MeshCorner
#include "ground_decal.h"
#include "link_figure.h"          // LinkMesh
#include "map_object.h"           // CMapObject (ObjectWithId)
#include "map_object_figures.h"   // CObject3D
#include "model.h"                // TransformPipeline, ScenePipeline
#include "render/render.h"
#include "terrain.h"
#include "world.h"

#include "lib/vecmath.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

DecalBuilder g_builder;
std::vector<Vector4> g_partPoints;   // a figure part's points, reused by every decal

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
void PushDecalMatrix(TransformPipeline* pipeline, const float* m) {
    MatrixStack& stack = pipeline->stacks[TransformPipeline::kDecalStack];
    if (stack.count == 0) {
        stack.count = 1;
        std::memcpy(stack.matrices.data(), m, sizeof(Matrix4));
        return;
    }
    ++stack.count;
    stack.matrices[stack.count - 1].Multiply(stack.matrices[stack.count - 2].m, m);
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

void AddPartTriangles(TransformPipeline* pipeline, LinkMesh* figure, FigureMesh* part, PartData& data) {
    const Matrix4* top = pipeline->Top(TransformPipeline::kDecalStack);
    {
        std::string name(part->GetName());
        figure->GetPartData(&name, &data.vertices, &data.corners, &data.indices, &data.blocks, &data.indexCount);
    }
    std::vector<Vector4>& points = g_partPoints;
    points.resize(data.blocks * 4);
    for (int i = 0; i < data.blocks * 4; ++i) {
        const VertexPacket& packet = data.vertices[i >> 2];
        points[i] = ToClip(*top, packet.block[0][i & 3], packet.block[1][i & 3], packet.block[2][i & 3]);
    }
    DecalBuilder& builder = g_builder;
    builder.Reserve(data.indexCount * 6);
    for (unsigned i = 0; i < static_cast<unsigned>(data.indexCount); i += 3) {
        const Vector4& a = points[data.corners[data.indices[i]].position];
        const Vector4& b = points[data.corners[data.indices[i + 1]].position];
        const Vector4& c = points[data.corners[data.indices[i + 2]].position];
        const Vector3 va{a.x, a.y, a.z};
        const Vector3 vb{b.x, b.y, b.z};
        const Vector3 vc{c.x, c.y, c.z};
        builder.AddTriangle(va, vb, vc);
    }
}

} // namespace

void WorldClient::ProjectDecal(const Vector3& direction, const Vector3& point, float depth,
                                const VertexPacket& quad, std::vector<VertexPacket>* points,
                                std::vector<DecalTexCoord>* texCoords, std::vector<std::uint16_t>* indices) {
    render::BeginDecalTime(false);   // a ground print's (object shadows project none)
    DecalBuilder& builder = g_builder;
    builder.Begin(points, texCoords, indices, quad, direction, point, depth);
    if (!platforms.Contains(point.x, point.y)) terrain->AddDecalTriangles(&builder, quad);

    TransformPipeline* pipeline = g_sceneRenderer.pipeline;
    PartData data;
    for (auto face = platforms.list.begin(); face != platforms.list.end();) {
        auto next = std::next(face);
        CObject3D* object = ObjectWithId(face->id);
        face = next;
        if (!object) continue;
        // The object's bounding sphere about its origin, then about its centre.
        if (!builder.Touches(object->localPosition, Length<Sum3::XYZ>(object->bounds.center) + object->bounds.radius))
            continue;
        BuildNodeMatrix(object->localRotation, object->localPosition, object->worldMatrix);
        Vector3 centre = TransformColumnNormal<Sum3::ZYX, Sum3::ZXY, Sum3::ZXY>(object->worldMatrix, object->bounds.center) +
                         object->localPosition;
        if (!builder.Touches(centre, object->bounds.radius)) continue;
        PushDecalMatrix(pipeline, object->worldMatrix.m);
        LinkMesh* figure = object->figure;
        if (figure) {
            for (int i = 0; i < std::ssize(object->parts); ++i) {
                FigureMesh* part = object->parts[i];
                BuildNodeMatrix(part->localRotation, part->localPosition, part->worldMatrix);
                PushDecalMatrix(pipeline, part->worldMatrix.m);
                AddPartTriangles(pipeline, figure, part, data);
                pipeline->PopMatrix(TransformPipeline::kDecalStack);
            }
        }
        pipeline->PopMatrix(TransformPipeline::kDecalStack);
    }
    builder.Finish();
    render::EndDecalTime(std::ssize(*points) * 4);
}
