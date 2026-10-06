// Checks for ground_decal.cpp. Reached by every drawn frame with shadows: the shadow projector
// 0056C750 (from DrawShadow 0051CE60 and 006CEDE0) and its terrain part 006CCF20 (`newgame`,
// `world_load`).
//
// - Interpolate, ClipTriangle, Touches, LengthSquared, Construct: the original into scratch
//   outputs (copies of the inputs it changes), ours on the real ones; floats with NearlyEqual.
// - AddVertex, Emit and the clipping stages (AddTriangle, AddTerrainTriangle, ClipPlane,
//   ClipFromV, ClipFromUMax) only write vertices and indices past the counts (the buffers are
//   reserved beforehand) and the triangle they are given: the original runs first, the counts,
//   the new vertices and indices are recorded and the counts and triangle put back, then ours
//   runs; counts must match, positions and texture coordinates nearly (NearClipped: the
//   projections are summed in another order), indices exactly. A triangle on a clipping edge can
//   come out differently (a sign test of a float sum, rule 7).
// - Begin, Reserve, Finish reallocate: the original runs on a deep copy of the builder and its
//   arrays (buffers from the game's heap, freed afterwards), ours on the real ones; array sizes,
//   capacities and the used elements compared, Begin's fields nearly.
#include "verify.h"

#include "../../src/game/ground_decal.h"
#include "../../src/hook/log.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>

namespace {

bool Near(float a, float b) { return ei::verify::NearlyEqual<float>(a, b, 4, 1e-6f); }

bool NearVertex(const DecalVertex& a, const DecalVertex& b) {
    return Near(a.x, b.x) && Near(a.y, b.y) && Near(a.z, b.z) && Near(a.u, b.u) && Near(a.v, b.v);
}

// The appended vertices: u, v and the plane distance are sums of products of world coordinates
// that cancel to small values, and /fp:fast adds them in another order than the original
// ((z + y) + (x + offset) instead of ((z + y) + x) + offset, rule 7). They differ by a few ULPs of
// the largest product, not of the result, and the clipped corners inherit that; so a few ULPs, or
// 1e-3 plus 1e-5 relative (a thousandth of the decal in texture space).
bool NearClipped(float a, float b) {
    return Near(a, b) || std::fabs(a - b) <= 1e-3f + 1e-5f * std::fabs(a);
}
bool NearClippedVertex(const DecalVertex& a, const DecalVertex& b) {
    return NearClipped(a.x, b.x) && NearClipped(a.y, b.y) && NearClipped(a.z, b.z) && NearClipped(a.u, b.u) &&
           NearClipped(a.v, b.v);
}

// A deep copy of an aligned array: the same header with its own buffer of the same capacity.
template <typename T>
void CloneArray(lib::AlignedArray<T>* to, const lib::AlignedArray<T>* from) {
    *to = *from;
    if (from->data) {
        to->data = lib::AlignedArray<T>::Allocate(from->capacity);
        std::memcpy(to->data, from->data, static_cast<std::size_t>(from->capacity) * sizeof(T));
    }
}
template <typename T>
void FreeArray(lib::AlignedArray<T>* a) {
    if (a->data) lib::AlignedArray<T>::Free(a->data);
}

struct BuilderCopy {
    DecalBuilder builder;
    lib::AlignedArray<VertexPacket> points{};
    lib::AlignedArray<DecalTexCoord> texCoords{};
    lib::AlignedArray<std::uint16_t> indices{};

    BuilderCopy(const DecalBuilder& real, lib::AlignedArray<VertexPacket>* p, lib::AlignedArray<DecalTexCoord>* t,
                lib::AlignedArray<std::uint16_t>* i) {
        std::memcpy(&builder, &real, sizeof(DecalBuilder));
        if (p) CloneArray(&points, p);
        if (t) CloneArray(&texCoords, t);
        if (i) CloneArray(&indices, i);
        builder.points_ = p ? &points : nullptr;
        builder.texCoords_ = t ? &texCoords : nullptr;
        builder.indices_ = i ? &indices : nullptr;
    }
    ~BuilderCopy() {
        FreeArray(&points);
        FreeArray(&texCoords);
        FreeArray(&indices);
    }
};

template <typename T>
bool SameShape(const lib::AlignedArray<T>& a, const lib::AlignedArray<T>& b) {
    return a.size == b.size && a.capacity == b.capacity && a.growBy == b.growBy && (a.data == nullptr) == (b.data == nullptr);
}

DecalVertex VertexAt(const lib::AlignedArray<VertexPacket>& points, const lib::AlignedArray<DecalTexCoord>* texCoords, int i) {
    const VertexPacket& p = points.data[i / 4];
    DecalVertex v = {p.block[0][i & 3], p.block[1][i & 3], p.block[2][i & 3], 0.0f, 0.0f};
    if (texCoords) {
        v.u = texCoords->data[i].u;
        v.v = texCoords->data[i].v;
    }
    return v;
}

// ---- Appending functions: original first, recorded, counts and triangle put back, then ours ----

struct Appended {
    int vertexCount, indexCount;
    std::vector<DecalVertex> vertices;
    std::vector<std::uint16_t> indices;
};

Appended Record(const DecalBuilder& b, int vertexStart, int indexStart) {
    Appended r{b.vertexCount_, b.indexCount_, {}, {}};
    for (int i = vertexStart; i < b.vertexCount_; ++i) r.vertices.push_back(VertexAt(*b.points_, b.texCoords_, i));
    for (int i = indexStart; i < b.indexCount_; ++i) r.indices.push_back(b.indices_->data[i]);
    return r;
}

// Returns false for a counted mismatch; `expectedOut` / `actualOut` (optional) receive both results.
template <typename Original, typename Ours>
bool CompareAppend(const char* name, DecalBuilder* self, DecalVertex* tri, Original original, Ours ours,
                   Appended* expectedOut = nullptr, Appended* actualOut = nullptr) {
    int vertexCount = self->vertexCount_, indexCount = self->indexCount_;
    DecalVertex saved[3];
    if (tri) std::memcpy(saved, tri, sizeof(saved));
    original();
    Appended expected = Record(*self, vertexCount, indexCount);
    self->vertexCount_ = vertexCount;
    self->indexCount_ = indexCount;
    if (tri) std::memcpy(tri, saved, sizeof(saved));
    ours();
    Appended actual = Record(*self, vertexCount, indexCount);
    bool ok = expected.vertexCount == actual.vertexCount && expected.indexCount == actual.indexCount;
    for (std::size_t i = 0; ok && i < expected.vertices.size(); ++i) ok = NearClippedVertex(expected.vertices[i], actual.vertices[i]);
    ok = ok && expected.indices == actual.indices;
    // Decals are drawn only (shadows on the ground), so close results are enough (renderer direction).
    // A corner on a clip plane can fall on either side under rule 7 and add or drop one triangle, and
    // a nearly degenerate corner can move a little more than NearClipped allows: logged, not counted.
    if (!ok) {
        int dv = std::abs(expected.vertexCount - actual.vertexCount), di = std::abs(expected.indexCount - actual.indexCount);
        bool sameCounts = dv == 0 && di == 0;
        bool edge = dv <= 3 && di <= 3;
        bool close = true;
        for (std::size_t i = 0; sameCounts && i < expected.vertices.size(); ++i) {
            const DecalVertex& a = expected.vertices[i];
            const DecalVertex& b = actual.vertices[i];
            for (float d : {a.x - b.x, a.y - b.y, a.z - b.z, a.u - b.u, a.v - b.v}) close = close && std::fabs(d) <= 0.05f;
        }
        if ((sameCounts && close) || (!sameCounts && edge)) {
            ei::Log("verify %s: tolerated clip-edge difference (vertices %d -> %d / %d)", name, vertexCount,
                    expected.vertexCount, actual.vertexCount);
            ok = true;
        }
    }
    ei::verify::Check(name, true, ok, "vertices %d -> %d / %d, indices %d -> %d / %d", vertexCount, expected.vertexCount,
                      actual.vertexCount, indexCount, expected.indexCount, actual.indexCount);
    if (expectedOut) *expectedOut = std::move(expected);
    if (actualOut) *actualOut = std::move(actual);
    return ok;
}

// 004D6C50: PUSH ESI (1), MOV ESI,[ECX+0x74] (3), MOV EAX,ESI (2).
std::uint16_t(__fastcall* s_addVertex)(DecalBuilder*, void*, const DecalVertex*);
std::uint16_t __fastcall AddVertexVerify(DecalBuilder* self, void*, const DecalVertex* v) {
    std::uint16_t expected = 0, actual = 0;
    CompareAppend("DecalBuilder::AddVertex", self, nullptr, [&] { expected = s_addVertex(self, nullptr, v); },
                  [&] { actual = self->AddVertex(v); });
    ei::verify::Check("DecalBuilder::AddVertex result", expected, actual, "count %d", self->vertexCount_);
    return actual;
}

// 004D6CF0: PUSH EBX, PUSH ESI, MOV ESI,ECX (2), PUSH EDI.
void(__fastcall* s_emit)(DecalBuilder*, void*, const DecalVertex*);
void __fastcall EmitVerify(DecalBuilder* self, void*, const DecalVertex* tri) {
    CompareAppend("DecalBuilder::Emit", self, nullptr, [&] { s_emit(self, nullptr, tri); }, [&] { self->Emit(tri); });
}

// 004D71D0 / 004D7BD0: MOV AL,[abs] (5). 004D74B0: PUSH ECX, MOV AL,[abs] (5).
void(__fastcall* s_clipFromUMax)(DecalBuilder*, void*, DecalVertex*);
void __fastcall ClipFromUMaxVerify(DecalBuilder* self, void*, DecalVertex* tri) {
    CompareAppend("DecalBuilder::ClipFromUMax", self, tri, [&] { s_clipFromUMax(self, nullptr, tri); },
                  [&] { self->ClipFromUMax(tri); });
}
void(__fastcall* s_clipFromV)(DecalBuilder*, void*, DecalVertex*);
void __fastcall ClipFromVVerify(DecalBuilder* self, void*, DecalVertex* tri) {
    CompareAppend("DecalBuilder::ClipFromV", self, tri, [&] { s_clipFromV(self, nullptr, tri); },
                  [&] { self->ClipFromV(tri); });
}
void(__fastcall* s_clipPlane)(DecalBuilder*, void*, DecalVertex*);
void __fastcall ClipPlaneVerify(DecalBuilder* self, void*, DecalVertex* tri) {
    CompareAppend("DecalBuilder::ClipPlane", self, tri, [&] { s_clipPlane(self, nullptr, tri); },
                  [&] { self->ClipPlane(tri); });
}

// 004D85F0 / 004D8490: PUSH ESI, MOV ESI,ECX (2), MOV CL,[abs] (6).
void(__fastcall* s_addTriangle)(DecalBuilder*, void*, const Vector3*, const Vector3*, const Vector3*);
void __fastcall AddTriangleVerify(DecalBuilder* self, void*, const Vector3* a, const Vector3* b, const Vector3* c) {
    CompareAppend("DecalBuilder::AddTriangle", self, nullptr, [&] { s_addTriangle(self, nullptr, a, b, c); },
                  [&] { self->AddTriangle(a, b, c); });
}

// X18: the input, the builder's clip state and both outputs of the first few counted
// AddTerrainTriangle mismatches. Per corner: the plane distance, u and v in double (exact
// products, one rounding per sum) to show which corner sits on a clip edge; both sides' rounded
// corners are clipped in place, so they cannot be read back (a corner left inside appears as is
// in the output).
double ProjectExact(const Vector3& axis, const Vector3& p, float offset) {
    return ((static_cast<double>(axis.z) * p.z + static_cast<double>(axis.y) * p.y) + static_cast<double>(p.x) * axis.x) +
           offset;
}

void LogVertices(const char* which, const Appended& r, int vertexStart) {
    ei::Log("  %s: %d vertices, %d indices", which, static_cast<int>(r.vertices.size()), static_cast<int>(r.indices.size()));
    for (std::size_t i = 0; i < r.vertices.size(); ++i) {
        const DecalVertex& v = r.vertices[i];
        ei::Log("    v%d: %.9g %.9g %.9g  uv %.9g %.9g", vertexStart + static_cast<int>(i), v.x, v.y, v.z, v.u, v.v);
    }
    for (std::size_t i = 0; i + 2 < r.indices.size(); i += 3)
        ei::Log("    tri %d %d %d", r.indices[i], r.indices[i + 1], r.indices[i + 2]);
}

void LogTerrainMismatch(const DecalBuilder& b, const Vector3* const corners[3], int vertexStart, const Appended& expected,
                        const Appended& actual) {
    static int s_logged = 0;
    if (s_logged >= 4) return;
    ++s_logged;
    ei::Log("verify DecalBuilder::AddTerrainTriangle mismatch %d:", s_logged);
    ei::Log("  uAxis %.9g %.9g %.9g + %.9g, vAxis %.9g %.9g %.9g + %.9g", b.uAxis_.x, b.uAxis_.y, b.uAxis_.z, b.uOffset_,
            b.vAxis_.x, b.vAxis_.y, b.vAxis_.z, b.vOffset_);
    ei::Log("  normal %.9g %.9g %.9g + %.9g", b.normal_.x, b.normal_.y, b.normal_.z, b.planeOffset_);
    for (int k = 0; k < 3; ++k) {
        const Vector3& p = *corners[k];
        ei::Log("  corner %d: %.9g %.9g %.9g  exact d %.9g uv %.9g %.9g", k, p.x, p.y, p.z,
                ProjectExact(b.normal_, p, b.planeOffset_), ProjectExact(b.uAxis_, p, b.uOffset_),
                ProjectExact(b.vAxis_, p, b.vOffset_));
    }
    LogVertices("original", expected, vertexStart);
    LogVertices("ours", actual, vertexStart);
}

void(__fastcall* s_addTerrainTriangle)(DecalBuilder*, void*, const Vector3*, const Vector3*, const Vector3*);
void __fastcall AddTerrainTriangleVerify(DecalBuilder* self, void*, const Vector3* a, const Vector3* b,
                                         const Vector3* c) {
    int vertexStart = self->vertexCount_;
    Appended expected, actual;
    bool ok = CompareAppend("DecalBuilder::AddTerrainTriangle", self, nullptr,
                            [&] { s_addTerrainTriangle(self, nullptr, a, b, c); },
                            [&] { self->AddTerrainTriangle(a, b, c); }, &expected, &actual);
    if (!ok) {
        const Vector3* corners[3] = {a, b, c};
        LogTerrainMismatch(*self, corners, vertexStart, expected, actual);
    }
}

// ---- Pure and scratch-output functions --------------------------------------------------------

// 004D7140: FLD [ESP+0xC] (4), FSUB [ESP+8] (4).
void(__fastcall* s_interpolate)(const DecalVertex*, const DecalVertex*, DecalVertex*, float, float);
void __fastcall InterpolateVerify(const DecalVertex* p, const DecalVertex* q, DecalVertex* out, float dp, float dq) {
    DecalVertex expected;
    std::memcpy(&expected, out, sizeof(expected));
    s_interpolate(p, q, &expected, dp, dq);
    decal::Interpolate(p, q, out, dp, dq);
    ei::verify::Check("decal::Interpolate", true, NearVertex(expected, *out), "dp %g dq %g", dp, dq);
}

// 004D6E90: SUB ESP,0xC (3), MOV EAX,[ESP+0x10] (4).
int(__fastcall* s_clipTriangle)(DecalVertex*, DecalVertex*, float, float, float);
int __fastcall ClipTriangleVerify(DecalVertex* tri, DecalVertex* out, float a, float b, float c) {
    DecalVertex expectedTri[3], expectedOut[3];
    std::memcpy(expectedTri, tri, sizeof(expectedTri));
    std::memcpy(expectedOut, out, sizeof(expectedOut));
    int expected = s_clipTriangle(expectedTri, expectedOut, a, b, c);
    int actual = decal::ClipTriangle(tri, out, a, b, c);
    bool ok = expected == actual;
    for (int k = 0; ok && k < 3; ++k) ok = NearVertex(expectedTri[k], tri[k]) && (actual < 2 || NearVertex(expectedOut[k], out[k]));
    ei::verify::Check("decal::ClipTriangle", true, ok, "%d / %d, d %g %g %g", expected, actual, a, b, c);
    return actual;
}

// 004D7F60: MOV EDX,[ESP+4] (4), FLD [ECX+0x1C] (3).
bool(__fastcall* s_touches)(const DecalBuilder*, void*, const Vector3*, float);
bool __fastcall TouchesVerify(const DecalBuilder* self, void*, const Vector3* center, float radius) {
    bool expected = s_touches(self, nullptr, center, radius);
    bool actual = self->Touches(center, radius);
    ei::verify::Check("DecalBuilder::Touches", expected, actual, "center %g %g %g r %g", center->x, center->y,
                      center->z, radius);
    return actual;
}

// 004D8470: FLD [ECX+8] (3), FLD [ECX+4] (3).
double(__fastcall* s_lengthSquared)(const Vector3*);
double __fastcall LengthSquaredVerify(const Vector3* self) {
    double expected = s_lengthSquared(self);
    double actual = self->LengthSquared();
    ei::verify::CheckNear("Vector3::LengthSquared", static_cast<float>(expected), static_cast<float>(actual), "%g %g %g",
                          self->x, self->y, self->z);
    return actual;
}

// 004D7F50: MOV EAX,ECX (2), XOR ECX,ECX (2), MOV [EAX],ECX (2).
DecalBuilder*(__fastcall* s_construct)(DecalBuilder*);
DecalBuilder* __fastcall ConstructVerify(DecalBuilder* self) {
    DecalBuilder scratch;
    std::memcpy(&scratch, self, sizeof(scratch));
    DecalBuilder* expected = s_construct(&scratch);
    DecalBuilder* actual = self->Construct();
    bool ok = expected == &scratch && actual == self && std::memcmp(&scratch, self, sizeof(scratch)) == 0;
    ei::verify::Check("DecalBuilder::Construct", true, ok, "");
    return actual;
}

// ---- Reallocating functions: the original on a deep copy ---------------------------------------

template <typename T>
bool SameElements(const lib::AlignedArray<T>& a, const lib::AlignedArray<T>& b, int count) {
    return count <= 0 || std::memcmp(a.data, b.data, static_cast<std::size_t>(count) * sizeof(T)) == 0;
}

bool SameVertices(const lib::AlignedArray<VertexPacket>& a, const lib::AlignedArray<VertexPacket>& b, int count) {
    for (int i = 0; i < count; ++i) {
        DecalVertex x = VertexAt(a, nullptr, i), y = VertexAt(b, nullptr, i);
        if (std::memcmp(&x, &y, sizeof(x)) != 0) return false;
    }
    return true;
}

// 004D8780: SUB ESP,0xC (3), MOV EAX,[ESP+0x10] (4).
void(__fastcall* s_reserve)(DecalBuilder*, void*, int);
void __fastcall ReserveVerify(DecalBuilder* self, void*, int triangles) {
    BuilderCopy copy(*self, self->points_, self->texCoords_, self->indices_);
    s_reserve(&copy.builder, nullptr, triangles);
    self->Reserve(triangles);
    int n = self->vertexCount_;
    bool ok = SameShape(copy.points, *self->points_) && SameShape(copy.texCoords, *self->texCoords_) &&
              SameShape(copy.indices, *self->indices_) && SameVertices(copy.points, *self->points_, n) &&
              SameElements(copy.texCoords, *self->texCoords_, n) &&
              SameElements(copy.indices, *self->indices_, self->indexCount_);
    ei::verify::Check("DecalBuilder::Reserve", true, ok, "triangles %d vertices %d indices %d", triangles, n,
                      self->indexCount_);
}

// 004D89A0: SUB ESP,0x10 (3), PUSH EBX, PUSH EBP.
void(__fastcall* s_finish)(DecalBuilder*);
void __fastcall FinishVerify(DecalBuilder* self) {
    lib::AlignedArray<VertexPacket>* points = self->points_;
    lib::AlignedArray<std::uint16_t>* indices = self->indices_;
    BuilderCopy copy(*self, points, self->texCoords_, indices);
    s_finish(&copy.builder);
    self->Finish();
    bool ok = copy.builder.points_ == nullptr && copy.builder.texCoords_ == nullptr && copy.builder.indices_ == nullptr &&
              self->points_ == nullptr && self->texCoords_ == nullptr && self->indices_ == nullptr &&
              SameShape(copy.points, *points) && SameShape(copy.indices, *indices) &&
              SameVertices(copy.points, *points, points->size * 4) &&
              SameElements(copy.indices, *indices, self->indexCount_);
    ei::verify::Check("DecalBuilder::Finish", true, ok, "vertices %d indices %d", self->vertexCount_, self->indexCount_);
}

// 004D8000: PUSH EBP, MOV EBP,ESP (2), SUB ESP,0x30 (3): 6.
void(__fastcall* s_begin)(DecalBuilder*, void*, lib::AlignedArray<VertexPacket>*, lib::AlignedArray<DecalTexCoord>*,
                          lib::AlignedArray<std::uint16_t>*, float* const*, const Vector3*, const Vector3*, float);
void __fastcall BeginVerify(DecalBuilder* self, void*, lib::AlignedArray<VertexPacket>* points,
                            lib::AlignedArray<DecalTexCoord>* texCoords, lib::AlignedArray<std::uint16_t>* indices,
                            float* const* corners, const Vector3* direction, const Vector3* point, float depth) {
    BuilderCopy copy(*self, points, texCoords, indices);
    s_begin(&copy.builder, nullptr, &copy.points, &copy.texCoords, &copy.indices, corners, direction, point, depth);
    self->Begin(points, texCoords, indices, corners, direction, point, depth);
    const DecalBuilder& e = copy.builder;
    const float* ef = &e.uAxis_.x;
    const float* af = &self->uAxis_.x;
    bool ok = e.points_ == &copy.points && e.texCoords_ == &copy.texCoords && e.indices_ == &copy.indices &&
              self->points_ == points && self->texCoords_ == texCoords && self->indices_ == indices &&
              e.vertexCount_ == self->vertexCount_ && e.indexCount_ == self->indexCount_ &&
              SameShape(copy.points, *points) && SameShape(copy.texCoords, *texCoords) && SameShape(copy.indices, *indices);
    int bad = -1;
    for (int k = 0; k < (0x74 - 0x0C) / 4; ++k) {
        if (!Near(ef[k], af[k])) {
            bad = k;
            break;
        }
    }
    ei::verify::Check("DecalBuilder::Begin", true, ok && bad < 0, "field +0x%X: %g / %g", 0x0C + bad * 4,
                      bad < 0 ? 0.0 : ef[bad], bad < 0 ? 0.0 : af[bad]);
}

} // namespace

EI_VERIFY(0x004D6C50, AddVertexVerify, 6, s_addVertex);
EI_VERIFY(0x004D6CF0, EmitVerify, 5, s_emit);
EI_VERIFY(0x004D71D0, ClipFromUMaxVerify, 5, s_clipFromUMax);
EI_VERIFY(0x004D74B0, ClipFromVVerify, 6, s_clipFromV);
EI_VERIFY(0x004D7BD0, ClipPlaneVerify, 5, s_clipPlane);
EI_VERIFY(0x004D85F0, AddTriangleVerify, 9, s_addTriangle);
EI_VERIFY(0x004D8490, AddTerrainTriangleVerify, 9, s_addTerrainTriangle);
EI_VERIFY(0x004D7140, InterpolateVerify, 8, s_interpolate);
EI_VERIFY(0x004D6E90, ClipTriangleVerify, 7, s_clipTriangle);
EI_VERIFY(0x004D7F60, TouchesVerify, 7, s_touches);
EI_VERIFY(0x004D8470, LengthSquaredVerify, 6, s_lengthSquared);
EI_VERIFY(0x004D7F50, ConstructVerify, 6, s_construct);
EI_VERIFY(0x004D8780, ReserveVerify, 7, s_reserve);
EI_VERIFY(0x004D89A0, FinishVerify, 5, s_finish);
EI_VERIFY(0x004D8000, BeginVerify, 6, s_begin);
