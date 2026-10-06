// Ground decals: see ground_decal.h. The original clips with five hand-unrolled functions
// (004D7BD0 plane -> u >= 0 -> v >= 0 -> 004D71D0; 004D74B0 v >= 0 -> u <= 1 -> v <= 1), each
// stage writing its second triangle into its own function-static buffer (007AA710, 007AA750,
// 007AA790, 007AA7D0, 007AA810). ClipStage is that pipeline with one buffer per stage, so the
// in-place clipping and the order triangles are emitted in are the same.
#include "ground_decal.h"

#include "ei/hook.h"

#include <cmath>
#include <cstring>

namespace {

constexpr int kEmitStage = 5;

DecalVertex s_stageBuffers[kEmitStage][3];

// Sign bit clear: the clipper's "inside" test (NOT; SHR 31 on the distance's bits).
int Inside(float d) {
    std::uint32_t bits;
    std::memcpy(&bits, &d, 4);
    return static_cast<int>((bits >> 31) ^ 1);
}

void CopyVertex(DecalVertex* to, const DecalVertex* from) { std::memcpy(to, from, sizeof(DecalVertex)); }

// ((a.z * p.z + a.y * p.y) + p.x * a.x) + offset, the order of the corner and plane sums.
float Project(const Vector3& axis, const float* p, float offset) {
    return axis.z * p[2] + axis.y * p[1] + p[0] * axis.x + offset;
}

// Keeps the buffer and sets the size to 0; a missing buffer is allocated for 0 elements (the
// growth branch for a negative capacity is the template's, never taken).
template <typename T>
void EmptyKeepingBuffer(lib::AlignedArray<T>* a) {
    if (!a->data) {
        a->data = lib::AlignedArray<T>::Allocate(0);
        a->capacity = 0;
        a->size = 0;
        return;
    }
    if (a->capacity >= 0) {
        a->size = 0;
        return;
    }
    int step = a->growBy;
    if (step == 0) {
        step = a->size / 8;
        if (step < 4) step = 4;
        else if (step > 0x400) step = 0x400;
    }
    int capacity = step + a->capacity;
    if (capacity <= 0) capacity = 0;
    T* fresh = lib::AlignedArray<T>::Allocate(capacity);
    lib::AlignedArray<T>::Free(a->data);
    a->data = fresh;
    a->size = 0;
    a->capacity = capacity;
}

} // namespace

double Vector3::LengthSquared() const { return x * x + y * y + z * z; }
EI_REPLACE_METHOD(0x004D8470, Vector3::LengthSquared);

namespace decal {

void __fastcall Interpolate(const DecalVertex* p, const DecalVertex* q, DecalVertex* out, float dp, float dq) {
    double scale = 1.0f / (dq - dp);
    out->x = static_cast<float>((dq * p->x - dp * q->x) * scale);
    out->y = static_cast<float>((dq * p->y - dp * q->y) * scale);
    out->z = static_cast<float>((dq * p->z - dp * q->z) * scale);
    out->u = static_cast<float>((dq * p->u - dp * q->u) * scale);
    out->v = static_cast<float>((dq * p->v - dp * q->v) * scale);
}

int __fastcall ClipTriangle(DecalVertex* tri, DecalVertex* out, float a, float b, float c) {
    DecalVertex* v0 = &tri[0];
    DecalVertex* v1 = &tri[1];
    DecalVertex* v2 = &tri[2];
    DecalVertex first, second;   // 007AA868 / 007AA850
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

DecalBuilder* DecalBuilder::Construct() {
    points_ = nullptr;
    texCoords_ = nullptr;
    indices_ = nullptr;
    return this;
}

void DecalBuilder::Begin(lib::AlignedArray<VertexPacket>* points, lib::AlignedArray<DecalTexCoord>* texCoords,
                         lib::AlignedArray<std::uint16_t>* indices, float* const* corners, const Vector3* direction,
                         const Vector3* point, float depth) {
    std::memcpy(&direction_, direction, sizeof(Vector3));
    std::memcpy(&point_, point, sizeof(Vector3));
    points_ = points;
    indices_ = indices;
    texCoords_ = texCoords;
    vertexCount_ = 0;
    indexCount_ = 0;
    EmptyKeepingBuffer(texCoords);
    EmptyKeepingBuffer(points_);
    EmptyKeepingBuffer(indices_);

    const float* m = *corners;
    Vector3 origin = {m[3], m[7], m[11]};
    Vector3 vCorner = {m[0], m[4], m[8]};

    Vector3& dir = direction_;
    double length = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    dir.x = static_cast<float>(dir.x / length);
    dir.y = static_cast<float>(dir.y / length);
    dir.z = static_cast<float>(dir.z / length);

    // The u edge without its part along the direction, over its squared length.
    uAxis_ = {m[2] - origin.x, m[6] - origin.y, m[10] - origin.z};
    double along = uAxis_.z * dir.z + uAxis_.y * dir.y + uAxis_.x * dir.x;
    Vector3 part = {static_cast<float>(along * dir.x), static_cast<float>(along * dir.y),
                    static_cast<float>(along * dir.z)};
    uAxis_ = {uAxis_.x - part.x, uAxis_.y - part.y, uAxis_.z - part.z};
    uAxis_.DivideInPlace(static_cast<float>(uAxis_.LengthSquared()));

    // The same for v.
    vAxis_ = {vCorner.x - origin.x, vCorner.y - origin.y, vCorner.z - origin.z};
    float vAlong = Dot(vAxis_, dir);
    part = {dir.x * vAlong, dir.y * vAlong, dir.z * vAlong};
    vAxis_ = {vAxis_.x - part.x, vAxis_.y - part.y, vAxis_.z - part.z};
    vAxis_.DivideInPlace(static_cast<float>(vAxis_.LengthSquared()));

    float slopeDivisor = std::fabs(dir.z);
    slope_ = {dir.x / slopeDivisor, dir.y / slopeDivisor, dir.z / slopeDivisor};
    origin_ = origin;
    Vector3 negated = {-origin_.x, -origin_.y, -origin_.z};
    uOffset_ = Dot(negated, uAxis_);
    vOffset_ = Dot(negated, vAxis_);
    uLength_ = uAxis_.Length();
    vLength_ = vAxis_.Length();

    // The cut-off plane faces along the direction through point - direction * depth * 0.33.
    float back = depth * 0.33f;
    normal_ = dir;
    Vector3 scaled = {dir.x * back, dir.y * back, dir.z * back};
    Vector3 planePoint = {point_.x - scaled.x, point_.y - scaled.y, point_.z - scaled.z};
    planeOffset_ = -Dot(planePoint, normal_);
}

bool DecalBuilder::Touches(const Vector3* center, float radius) const {
    double v = vAxis_.y * center->y + vAxis_.z * center->z + center->x * vAxis_.x + vOffset_;
    double vReach = radius * vLength_;
    if (v - vReach > 1.0f) return false;
    if (!(v + vReach >= 0.0f)) return false;
    double u = uAxis_.y * center->y + uAxis_.z * center->z + center->x * uAxis_.x + uOffset_;
    double uReach = radius * uLength_;
    if (u - uReach > 1.0f) return false;
    if (!(u + uReach >= 0.0f)) return false;
    return true;
}

void DecalBuilder::Reserve(int triangles) {
    int corners = triangles * 3;
    texCoords_->SetSize(corners + vertexCount_);
    points_->SetSize(corners / 4 + 1 + vertexCount_);
    indices_->SetSize(indexCount_ + corners, -1);
}

std::uint16_t DecalBuilder::AddVertex(const DecalVertex* v) {
    int i = vertexCount_;
    VertexPacket& packet = points_->data[i / 4];
    std::memcpy(&packet.block[0][i & 3], &v->x, 4);
    std::memcpy(&packet.block[1][i & 3], &v->y, 4);
    std::memcpy(&packet.block[2][i & 3], &v->z, 4);
    std::memcpy(&texCoords_->data[i].u, &v->u, 4);
    std::memcpy(&texCoords_->data[i].v, &v->v, 4);
    vertexCount_ = i + 1;
    return static_cast<std::uint16_t>(i);
}

void DecalBuilder::Emit(const DecalVertex* tri) {
    for (int k = 0; k < 3; ++k) {
        std::uint16_t* slot = &indices_->data[indexCount_++];
        *slot = AddVertex(&tri[k]);
    }
}

void DecalBuilder::ClipStage(DecalVertex* tri, int stage) {
    float d[3];
    for (int k = 0; k < 3; ++k) {
        const DecalVertex& c = tri[k];
        switch (stage) {
        case 0: d[k] = Project(normal_, &c.x, planeOffset_); break;
        case 1: d[k] = c.u; break;
        case 2: d[k] = c.v; break;
        case 3: d[k] = 1.0f - c.u; break;
        default: d[k] = 1.0f - c.v; break;
        }
    }
    DecalVertex* second = s_stageBuffers[stage];
    int count = decal::ClipTriangle(tri, second, d[0], d[1], d[2]);
    if (stage + 1 == kEmitStage) {
        if (count > 0) Emit(tri);
        if (count > 1) Emit(second);
        return;
    }
    if (count > 0) ClipStage(tri, stage + 1);
    if (count > 1) ClipStage(second, stage + 1);
}

void DecalBuilder::ClipPlane(DecalVertex* tri) { ClipStage(tri, 0); }
void DecalBuilder::ClipFromV(DecalVertex* tri) { ClipStage(tri, 2); }
void DecalBuilder::ClipFromUMax(DecalVertex* tri) { ClipStage(tri, 3); }

void DecalBuilder::AddCorners(DecalVertex* tri, const Vector3* a, const Vector3* b, const Vector3* c) {
    const Vector3* corners[3] = {a, b, c};
    for (int k = 0; k < 3; ++k) {
        std::memcpy(&tri[k].x, corners[k], sizeof(Vector3));
        tri[k].u = Project(uAxis_, &tri[k].x, uOffset_);
        tri[k].v = Project(vAxis_, &tri[k].x, vOffset_);
    }
}

void DecalBuilder::AddTriangle(const Vector3* a, const Vector3* b, const Vector3* c) {
    DecalVertex tri[3];   // 007AA690
    AddCorners(tri, a, b, c);
    ClipPlane(tri);
}

void DecalBuilder::AddTerrainTriangle(const Vector3* a, const Vector3* b, const Vector3* c) {
    DecalVertex tri[3];   // 007AA6D0
    AddCorners(tri, a, b, c);
    ClipPlane(tri);
}

void DecalBuilder::Finish() {
    lib::AlignedArray<VertexPacket>* points = points_;
    lib::AlignedArray<std::uint16_t>* indices = indices_;
    points->SetSize((vertexCount_ + 3) / 4);
    indices->SetSize(indexCount_);
    for (int i = vertexCount_; i < points->size * 4; ++i) {
        VertexPacket& packet = points->data[i / 4];
        const VertexPacket& first = points->data[0];
        std::memcpy(&packet.block[0][i & 3], &first.block[0][0], 4);
        std::memcpy(&packet.block[1][i & 3], &first.block[1][0], 4);
        std::memcpy(&packet.block[2][i & 3], &first.block[2][0], 4);
    }
    points_ = nullptr;
    texCoords_ = nullptr;
    indices_ = nullptr;
}

EI_REPLACE(0x004D7140, decal::Interpolate);
EI_REPLACE(0x004D6E90, decal::ClipTriangle);
EI_REPLACE_METHOD(0x004D7F50, DecalBuilder::Construct);
EI_REPLACE_METHOD(0x004D8000, DecalBuilder::Begin);
EI_REPLACE_METHOD(0x004D7F60, DecalBuilder::Touches);
EI_REPLACE_METHOD(0x004D8780, DecalBuilder::Reserve);
EI_REPLACE_METHOD(0x004D6C50, DecalBuilder::AddVertex);
EI_REPLACE_METHOD(0x004D6CF0, DecalBuilder::Emit);
EI_REPLACE_METHOD(0x004D71D0, DecalBuilder::ClipFromUMax);
EI_REPLACE_METHOD(0x004D74B0, DecalBuilder::ClipFromV);
EI_REPLACE_METHOD(0x004D7BD0, DecalBuilder::ClipPlane);
EI_REPLACE_METHOD(0x004D85F0, DecalBuilder::AddTriangle);
EI_REPLACE_METHOD(0x004D8490, DecalBuilder::AddTerrainTriangle);
EI_REPLACE_METHOD(0x004D89A0, DecalBuilder::Finish);
