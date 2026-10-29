// Ground decals: see ground_decal.h. The game clips with five hand-unrolled functions (plane -> u >= 0 -> v
// >= 0 -> u <= 1 -> v <= 1, with entry points part way), each stage writing its second triangle into its own
// function-static buffer. ClipStage is that pipeline with one buffer per stage, so the in-place clipping and
// the order triangles are emitted in are the same.
#include "ground_decal.h"

#include "lib/float_select.h"
#include "lib/nan_audit.h"

#include <bit>
#include <cmath>
#include <cstring>
#include <vector>

namespace {

constexpr int kEmitStage = 5;

DecalVertex s_stageBuffers[kEmitStage][3];

// Sign bit clear: the clipper's "inside" test (NOT; SHR 31 on the distance's bits).
int Inside(float d) { return SignBit(d) ? 0 : 1; }

void CopyVertex(DecalVertex* to, const DecalVertex* from) { std::memcpy(to, from, sizeof(DecalVertex)); }

// ((a.z * p.z + a.y * p.y) + p.x * a.x) + offset, the order of the corner and plane sums.
float Project(const Vector3& axis, const Vector3& p, float offset) {
    return DotUnrounded<Sum3::ZYX>(axis, p) + offset;
}

} // namespace

double Vector3::LengthSquared() const { return LengthSquaredUnrounded<Sum3::XYZ>(*this); }

namespace decal {

void Interpolate(const DecalVertex* p, const DecalVertex* q, DecalVertex* out, float dp, float dq) {
    float scale = 1.0f / (dq - dp);
    out->position.x = (dq * p->position.x - dp * q->position.x) * scale;
    out->position.y = (dq * p->position.y - dp * q->position.y) * scale;
    out->position.z = (dq * p->position.z - dp * q->position.z) * scale;
    out->u = (dq * p->u - dp * q->u) * scale;
    out->v = (dq * p->v - dp * q->v) * scale;
}

int ClipTriangle(DecalVertex* tri, DecalVertex* out, float a, float b, float c) {
    DecalVertex* v0 = &tri[0];
    DecalVertex* v1 = &tri[1];
    DecalVertex* v2 = &tri[2];
    DecalVertex first, second;
    switch (Inside(a) + Inside(b) * 2 + Inside(c) * 4) {
    case 1:   // only v0 inside
        Interpolate(v0, v1, &first, a, b);
        Interpolate(v0, v2, &second, a, c);
        CopyVertex(v1, &first);
        CopyVertex(v2, &second);
        return 1;
    case 2:   // only v1
        Interpolate(v0, v1, &first, a, b);
        Interpolate(v1, v2, &second, b, c);
        CopyVertex(v0, &first);
        CopyVertex(v2, &second);
        return 1;
    case 3:   // v0 and v1
        Interpolate(v1, v2, &first, b, c);
        Interpolate(v0, v2, &second, a, c);
        CopyVertex(v2, &second);
        CopyVertex(&out[0], v1);
        CopyVertex(&out[1], &first);
        CopyVertex(&out[2], &second);
        return 2;
    case 4:   // only v2
        Interpolate(v1, v2, &first, b, c);
        Interpolate(v0, v2, &second, a, c);
        CopyVertex(v1, &first);
        CopyVertex(v0, &second);
        return 1;
    case 5:   // v0 and v2
        Interpolate(v0, v1, &first, a, b);
        Interpolate(v1, v2, &second, b, c);
        CopyVertex(v1, &second);
        CopyVertex(&out[0], v0);
        CopyVertex(&out[1], &first);
        CopyVertex(&out[2], &second);
        return 2;
    case 6:   // v1 and v2
        Interpolate(v0, v1, &first, a, b);
        Interpolate(v0, v2, &second, a, c);
        CopyVertex(v0, &second);
        CopyVertex(&out[0], v1);
        CopyVertex(&out[1], &second);
        CopyVertex(&out[2], &first);
        return 2;
    case 7:
        return 1;
    default:
        return 0;
    }
}

} // namespace decal

void DecalBuilder::Begin(std::vector<VertexPacket>* newPoints, std::vector<DecalTexCoord>* newTexCoords,
                         std::vector<std::uint16_t>* newIndices, const VertexPacket& quad, const Vector3& newDirection,
                         const Vector3& newPoint, float depth) {
    direction = newDirection;
    point = newPoint;
    points = newPoints;
    indices = newIndices;
    texCoords = newTexCoords;
    vertexCount = 0;
    indexCount = 0;
    newTexCoords->clear();
    points->clear();
    indices->clear();

    Vector3 newOrigin = {quad.x()[3], quad.y()[3], quad.z()[3]};
    Vector3 vCorner = {quad.x()[0], quad.y()[0], quad.z()[0]};

    Vector3& dir = direction;
    dir = Normalised<Sum3::XYZ>(dir);

    // The u edge without its part along the direction, over its squared length.
    uAxis = {quad.x()[2] - newOrigin.x, quad.y()[2] - newOrigin.y, quad.z()[2] - newOrigin.z};
    float along = DotZYX(uAxis, dir);
    Vector3 part = {along * dir.x, along * dir.y,
                    along * dir.z};
    uAxis = {uAxis.x - part.x, uAxis.y - part.y, uAxis.z - part.z};
    uAxis.DivideInPlace(static_cast<float>(uAxis.LengthSquared()));

    vAxis = {vCorner.x - newOrigin.x, vCorner.y - newOrigin.y, vCorner.z - newOrigin.z};
    float vAlong = Dot(vAxis, dir);
    part = {dir.x * vAlong, dir.y * vAlong, dir.z * vAlong};
    vAxis = {vAxis.x - part.x, vAxis.y - part.y, vAxis.z - part.z};
    vAxis.DivideInPlace(static_cast<float>(vAxis.LengthSquared()));

    float slopeDivisor = std::fabs(dir.z);
    slope = {dir.x / slopeDivisor, dir.y / slopeDivisor, dir.z / slopeDivisor};
    origin = newOrigin;
    Vector3 negated = {-origin.x, -origin.y, -origin.z};
    uOffset = Dot(negated, uAxis);
    vOffset = Dot(negated, vAxis);
    uLength = uAxis.Length();
    vLength = vAxis.Length();

    // The cut-off plane faces along the direction through point - direction * depth * 0.33.
    float back = depth * 0.33f;
    normal = dir;
    Vector3 scaled = {dir.x * back, dir.y * back, dir.z * back};
    Vector3 planePoint = {point.x - scaled.x, point.y - scaled.y, point.z - scaled.z};
    planeOffset = -Dot(planePoint, normal);
}

bool DecalBuilder::Touches(const Vector3& center, float radius) const {
    float v = DotZYX(vAxis, center) + vOffset;
    float vReach = radius * vLength;
    if (v - vReach > 1.0f) return false;
    EI_NAN_AUDIT(v, vReach);
    if (v + vReach < 0.0f) return false;
    float u = DotZYX(uAxis, center) + uOffset;
    float uReach = radius * uLength;
    if (u - uReach > 1.0f) return false;
    return u + uReach >= 0.0f;
}

void DecalBuilder::Reserve(int triangles) {
    int corners = triangles * 3;
    texCoords->resize(corners + vertexCount);
    points->resize(corners / 4 + 1 + vertexCount);
    indices->resize(indexCount + corners);
}

std::uint16_t DecalBuilder::AddVertex(const DecalVertex* v) {
    int i = vertexCount;
    VertexPacket& packet = (*points)[i / 4];
    packet.block[0][i & 3] = v->position.x;
    packet.block[1][i & 3] = v->position.y;
    packet.block[2][i & 3] = v->position.z;
    (*texCoords)[i].u = v->u;
    (*texCoords)[i].v = v->v;
    vertexCount = i + 1;
    return static_cast<std::uint16_t>(i);
}

void DecalBuilder::Emit(const DecalVertex* tri) {
    for (int k = 0; k < 3; ++k) {
        std::uint16_t* slot = &(*indices)[indexCount++];
        *slot = AddVertex(&tri[k]);
    }
}

void DecalBuilder::ClipStage(DecalVertex* tri, int stage) {
    float distances[3];   // per corner
    for (int k = 0; k < 3; ++k) {
        const DecalVertex& c = tri[k];
        switch (stage) {
        case 0: distances[k] = Project(normal, c.position, planeOffset); break;
        case 1: distances[k] = c.u; break;
        case 2: distances[k] = c.v; break;
        case 3: distances[k] = 1.0f - c.u; break;
        default: distances[k] = 1.0f - c.v; break;
        }
    }
    DecalVertex* second = s_stageBuffers[stage];
    int count = decal::ClipTriangle(tri, second, distances[0], distances[1], distances[2]);
    if (stage + 1 == kEmitStage) {
        if (count > 0) Emit(tri);
        if (count > 1) Emit(second);
        return;
    }
    if (count > 0) ClipStage(tri, stage + 1);
    if (count > 1) ClipStage(second, stage + 1);
}

void DecalBuilder::ClipPlane(DecalVertex* tri) { ClipStage(tri, 0); }

void DecalBuilder::AddCorners(DecalVertex* tri, const Vector3& a, const Vector3& b, const Vector3& c) {
    const Vector3* corners[3] = {&a, &b, &c};
    for (int k = 0; k < 3; ++k) {
        tri[k].position = *corners[k];
        tri[k].u = Project(uAxis, tri[k].position, uOffset);
        tri[k].v = Project(vAxis, tri[k].position, vOffset);
    }
}

void DecalBuilder::AddTriangle(const Vector3& a, const Vector3& b, const Vector3& c) {
    DecalVertex tri[3];
    AddCorners(tri, a, b, c);
    ClipPlane(tri);
}

void DecalBuilder::AddTerrainTriangle(const Vector3& a, const Vector3& b, const Vector3& c) {
    DecalVertex tri[3];
    AddCorners(tri, a, b, c);
    ClipPlane(tri);
}

void DecalBuilder::Finish() {
    std::vector<VertexPacket>* currentPoints = points;
    std::vector<std::uint16_t>* currentIndices = indices;
    currentPoints->resize((vertexCount + 3) / 4);
    currentIndices->resize(indexCount);
    for (int i = vertexCount; i < std::ssize(*currentPoints) * 4; ++i) {
        VertexPacket& packet = (*currentPoints)[i / 4];
        const VertexPacket& first = (*currentPoints)[0];
        packet.block[0][i & 3] = first.block[0][0];
        packet.block[1][i & 3] = first.block[1][0];
        packet.block[2][i & 3] = first.block[2][0];
    }
    points = nullptr;
    texCoords = nullptr;
    indices = nullptr;
}
