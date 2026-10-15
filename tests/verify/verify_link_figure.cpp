// Differential checks for link_figure.cpp. The loaders and AddPart change a mesh's maps and arrays, so
// the original runs on a scratch mesh made by the original constructor (004F2C00, same loader, name and
// morph coefficients; destroyed by 004F2E40) with the same input, and ours on the real mesh: what the
// original put into the empty scratch must equal what ours appended (vertices with NearlyEqual, the
// other arrays bytewise), the part's ranges must be the old sizes and the counts, and the loaders must
// move the cursor alike. A part whose name the real mesh already has is not appended by either
// AddPart (0) — the scratch still gets it — so then only the cursor and the header dwords are compared.
//
// Scenario: `newgame` (the world client's object creation 00568800 asks the world view's loader for
// each object's figure: slot 3 004F7150 -> 004F7B10 / 004F77C0 -> 004F7F00 / 004F87D0 -> 004F30F0).
#include "verify.h"

#include "../../src/game/link_figure.h"
#include "../../src/game/originals_script.h"

#include <cstring>

namespace {

// The original constructor (loader, name, s, t, u; RET 0x14, EAX = this) and destructor body.
__declspec(naked) CLinkMesh* __fastcall MeshConstructOriginal(void* /*memory*/, void*, CLinkFigureLoader* /*loader*/,
                                                              const stlp::string* /*name*/, float, float, float)
    EI_ORIGINAL(0x004F2C00)
__declspec(naked) void __fastcall MeshDestructOriginal(CLinkMesh* /*mesh*/) EI_ORIGINAL(0x004F2E40)

struct ScratchMesh {
    alignas(16) unsigned char bytes[sizeof(CLinkMesh)];
    CLinkMesh* mesh;
    explicit ScratchMesh(const CLinkMesh* like) {
        mesh = MeshConstructOriginal(bytes, nullptr, like->loader_, &like->name_, like->morphWeights_[0], like->morphWeights_[1],
                                     like->morphWeights_[2]);
    }
    ~ScratchMesh() { MeshDestructOriginal(mesh); }
};

struct Sizes {
    int vertices, normals, texCoords, corners, words, indices;
    explicit Sizes(const CLinkMesh* m)
        : vertices(m->vertices_.size), normals(m->normals_.size), texCoords(m->texCoords_.size), corners(m->corners_.size),
          words(m->words_.size), indices(m->indices_.size) {}
    bool operator==(const Sizes& o) const {
        return vertices == o.vertices && normals == o.normals && texCoords == o.texCoords && corners == o.corners &&
               words == o.words && indices == o.indices;
    }
};

bool HasPart(CLinkMesh* mesh, const stlp::string* name) {
    return mesh->ranges_.find(*name) != mesh->ranges_.end();
}

bool Near(float expected, float actual) { return ei::verify::NearlyEqual<float>(expected, actual, 8, 1e-5f); }

template <typename T>
bool SameBytes(const T* expected, const T* actual, int count) {
    return count == 0 || std::memcmp(expected, actual, static_cast<std::size_t>(count) * sizeof(T)) == 0;
}

// The scratch's whole arrays against the real mesh's elements from `before` on; vertices nearly.
// Returns the first field that differs (nullptr when none); `badVertex` is the differing vertex.
const char* AppendedDiff(const CLinkMesh* expected, const CLinkMesh* actual, const Sizes& before, int* badVertex) {
    *badVertex = -1;
    Sizes added(expected);
    Sizes after(actual);
    if (after.vertices - before.vertices != added.vertices) return "vertex count";
    if (after.normals - before.normals != added.normals) return "normal count";
    if (after.texCoords - before.texCoords != added.texCoords) return "texcoord count";
    if (after.corners - before.corners != added.corners) return "corner count";
    if (after.words - before.words != added.words) return "word count";
    if (after.indices - before.indices != added.indices) return "index count";
    // The comparisons below read added.x elements from `before.x` on: both arrays must hold them.
    if (before.vertices < 0 || before.normals < 0 || before.texCoords < 0 || before.corners < 0 || before.words < 0 ||
        before.indices < 0 || after.vertices > actual->vertices_.capacity || after.normals > actual->normals_.capacity ||
        after.texCoords > actual->texCoords_.capacity || after.corners > actual->corners_.capacity ||
        after.words > actual->words_.capacity || after.indices > actual->indices_.capacity ||
        added.vertices > expected->vertices_.capacity || added.normals > expected->normals_.capacity ||
        added.texCoords > expected->texCoords_.capacity || added.corners > expected->corners_.capacity ||
        added.words > expected->words_.capacity || added.indices > expected->indices_.capacity)
        return "array bounds";
    for (int i = 0; i < added.vertices; ++i) {
        const float* e = &expected->vertices_.data[i].block[0][0];
        const float* a = &actual->vertices_.data[before.vertices + i].block[0][0];
        for (int k = 0; k < 16; ++k)
            if (!Near(e[k], a[k])) {
                *badVertex = i;
                return "vertex";
            }
    }
    if (!SameBytes(expected->normals_.data, actual->normals_.data + before.normals, added.normals)) return "normals";
    if (!SameBytes(expected->texCoords_.data, actual->texCoords_.data + before.texCoords, added.texCoords))
        return "texcoords";
    // corners_ holds whole 6-byte triangles (IndexTriangle), so its index is in triangles.
    if (!SameBytes(expected->corners_.data, actual->corners_.data + before.corners, added.corners)) return "corners";
    if (!SameBytes(expected->words_.data, actual->words_.data + before.words, added.words)) return "words";
    if (!SameBytes(expected->indices_.data, actual->indices_.data + before.indices, added.indices)) return "indices";
    return nullptr;
}

bool SameRanges(CLinkMesh* mesh, const stlp::string* name, const Sizes& before, const Sizes& added) {
    const LinkPartRanges* r = LinkRangeMapAtOriginal(&mesh->ranges_, nullptr, name);
    return r->vertices.start == before.vertices && r->vertices.count == added.vertices &&
           r->normals.start == before.normals && r->normals.count == added.normals &&
           r->texCoords.start == before.texCoords && r->texCoords.count == added.texCoords &&
           r->corners.start == before.corners && r->corners.count == added.corners && r->words.start == before.words &&
           r->words.count == added.words && r->indices.start == before.indices && r->indices.count == added.indices;
}

bool SameBounds(const ClipBounds& e, const ClipBounds& a) {
    if (e.flags != a.flags) return false;
    const float* ef = &e.center.x;
    const float* af = &a.center.x;
    for (int k = 0; k < 10; ++k)
        if (!Near(ef[k], af[k])) return false;
    return true;
}

// 004F30F0: SUB ESP,0x3C (3), MOV EAX,[ESP+0x40] (4). The result is AL only: on success the original
// leaves 004F4CE0's pointer in EAX's upper bytes (MOV EDI,EAX; MOV AL,1).
bool(__fastcall* s_addPart)(CLinkMesh*, void*, const stlp::string*, const VertexPacket*, int, const NormalPacket*, int,
                            const TexCoord*, int, const MeshCorner*, int, const std::uint32_t*, int, const std::uint16_t*, int);
bool __fastcall AddPartVerify(CLinkMesh* self, void*, const stlp::string* name, const VertexPacket* vertices,
                              int vertexCount, const NormalPacket* normals, int normalCount, const TexCoord* texCoords,
                              int texCoordCount, const MeshCorner* corners, int cornerCount, const std::uint32_t* words,
                              int wordCount, const std::uint16_t* indices, int indexCount) {
    bool present = HasPart(self, name);
    Sizes before(self);
    bool actual = self->AddPart(name, vertices, vertexCount, normals, normalCount, texCoords, texCoordCount, corners,
                                cornerCount, words, wordCount, indices, indexCount);
    const char* diff = nullptr;
    int bad = -1;
    if (present) {
        if (actual) diff = "result";
        else if (!(Sizes(self) == before)) diff = "sizes";
    } else {
        ScratchMesh scratch(self);
        bool expected = s_addPart(scratch.mesh, nullptr, name, vertices, vertexCount, normals, normalCount, texCoords,
                                  texCoordCount, corners, cornerCount, words, wordCount, indices, indexCount);
        if (expected != actual || !actual) diff = "result";
        if (!diff) diff = AppendedDiff(scratch.mesh, self, before, &bad);
        if (!diff && !SameRanges(self, name, before, Sizes(scratch.mesh))) diff = "ranges";
    }
    ei::verify::Check("CLinkMesh::AddPart", true, diff == nullptr,
                      "\"%.*s\" %s differs: present %d result %d counts %d %d %d %d %d %d vertex %d",
                      static_cast<int>(name->size()), name->data(), diff ? diff : "nothing", present, actual,
                      vertexCount, normalCount, texCoordCount, cornerCount, wordCount, indexCount, bad);
    return actual;
}

using LoadFn = void(__fastcall*)(CLinkFigureLoader*, void*, CLinkMesh*, const stlp::string*, const char**, float, float,
                                 float);
using OursFn = void (CLinkFigureLoader::*)(CLinkMesh*, const stlp::string*, const char**, float, float, float);

void LoadVerify(const char* what, LoadFn original, OursFn ours, int vertexCount, CLinkFigureLoader* self, CLinkMesh* mesh,
                const stlp::string* name, const char** cursor, float s, float t, float u) {
    bool present = HasPart(mesh, name);
    Sizes before(mesh);
    const char* start = *cursor;
    ScratchMesh scratch(mesh);
    const char* expectedCursor = start;
    original(self, nullptr, scratch.mesh, name, &expectedCursor, s, t, u);
    (self->*ours)(mesh, name, cursor, s, t, u);

    const LinkPartHeader* e = LinkHeaderMapAtOriginal(&scratch.mesh->headers_, nullptr, name);
    const LinkPartHeader* a = LinkHeaderMapAtOriginal(&mesh->headers_, nullptr, name);
    bool same = *cursor == expectedCursor && e->meshType == a->meshType && e->materialValue == a->materialValue;
    int bad = -1;
    const char* diff = same ? nullptr : "cursor or header";
    if (!diff && !present) {
        if (vertexCount > 0 && !SameBounds(e->bounds, a->bounds)) diff = "bounds";
        else diff = AppendedDiff(scratch.mesh, mesh, before, &bad);
    }
    ei::verify::Check(what, true, diff == nullptr, "\"%.*s\" %s differs: present %d vertices %d read %d / %d vertex %d",
                      static_cast<int>(name->size()), name->data(), diff ? diff : "nothing", present, vertexCount,
                      static_cast<int>(expectedCursor - start), static_cast<int>(*cursor - start), bad);
}

// 004F7F00 / 004F87D0: PUSH -1 (2), PUSH imm32 (5).
LoadFn s_loadFig;
void __fastcall LoadFigVerify(CLinkFigureLoader* self, void*, CLinkMesh* mesh, const stlp::string* name,
                              const char** cursor, float s, float t, float u) {
    FigHeader header;
    std::memcpy(&header, *cursor, sizeof(header));
    LoadVerify("CLinkFigureLoader::LoadFigPart", s_loadFig, &CLinkFigureLoader::LoadFigPart, header.vertexCount, self,
               mesh, name, cursor, s, t, u);
}

LoadFn s_loadOld;
void __fastcall LoadOldVerify(CLinkFigureLoader* self, void*, CLinkMesh* mesh, const stlp::string* name,
                              const char** cursor, float s, float t, float u) {
    int count;
    std::memcpy(&count, *cursor + 0xF8, 4);
    LoadVerify("CLinkFigureLoader::LoadOldPart", s_loadOld, &CLinkFigureLoader::LoadOldPart, count, self, mesh, name,
               cursor, s, t, u);
}

// ---- CLinkMesh's slots ------------------------------------------------------------------------------
// Reached with the meshes the loader slots hand out: HasPart from 004F7150 for every requested part of
// a cached figure; UpdateScreenRect and GetPartData from the part draws (slots 7-14) and their callers.

// 004F2F90: MOV EAX,[ESP+4] (4), MOV EDX,[ESP+8] (4). The original on a byte copy.
void(__fastcall* s_setMorph)(CLinkMesh*, void*, float, float, float);
void __fastcall SetMorphVerify(CLinkMesh* self, void*, float s, float t, float u) {
    alignas(16) unsigned char copy[sizeof(CLinkMesh)];
    std::memcpy(copy, self, sizeof(CLinkMesh));
    s_setMorph(reinterpret_cast<CLinkMesh*>(copy), nullptr, s, t, u);
    self->SetMorph(s, t, u);
    ei::verify::Check("CLinkMesh::SetMorph", true, std::memcmp(copy, self, sizeof(CLinkMesh)) == 0, "");
}

// 004F45E0: PUSH ECX, PUSH ESI, PUSH EDI (3), MOV ESI,ECX (2). The original on a byte copy.
void(__fastcall* s_updateRect)(CLinkMesh*);
void __fastcall UpdateScreenRectVerify(CLinkMesh* self) {
    alignas(16) unsigned char copy[sizeof(CLinkMesh)];
    std::memcpy(copy, self, sizeof(CLinkMesh));
    s_updateRect(reinterpret_cast<CLinkMesh*>(copy));
    self->CLinkMesh::UpdateScreenRect();
    const RECT& e = reinterpret_cast<CLinkMesh*>(copy)->screenRect_;
    const RECT& a = self->screenRect_;
    ei::verify::Check("CLinkMesh::UpdateScreenRect", true, std::memcmp(copy, self, sizeof(CLinkMesh)) == 0,
                      "%d %d %d %d / %d %d %d %d", e.left, e.top, e.right, e.bottom, a.left, a.top, a.right, a.bottom);
}

// 004F3040: SUB ESP,8 (3), PUSH EBX (1), MOV EBX,ECX (2). Pure.
bool(__fastcall* s_hasPart)(CLinkMesh*, void*, const stlp::string*);
bool __fastcall HasPartVerify(CLinkMesh* self, void*, const stlp::string* name) {
    bool expected = s_hasPart(self, nullptr, name);
    bool actual = self->CLinkMesh::HasPartImpl(name);
    ei::verify::Check("CLinkMesh::HasPart", expected, actual, "\"%.*s\"", static_cast<int>(name->size()), name->data());
    return actual;
}

// 004F3780: MOV EAX,[ESP+4] (4), PUSH ESI (1). The original into scratch outputs.
bool(__fastcall* s_getPartData)(CLinkMesh*, void*, const stlp::string*, VertexPacket**, MeshCorner**, std::uint16_t**,
                                int*, int*);
bool __fastcall GetPartDataVerify(CLinkMesh* self, void*, const stlp::string* name, VertexPacket** vertices,
                                  MeshCorner** corners, std::uint16_t** indices, int* vertexCount, int* indexCount) {
    VertexPacket* ev = nullptr;
    MeshCorner* ec = nullptr;
    std::uint16_t* eix = nullptr;
    int evc = -1;
    int eic = -1;
    bool expected = s_getPartData(self, nullptr, name, &ev, &ec, &eix, &evc, &eic);
    bool actual = self->CLinkMesh::GetPartDataImpl(name, vertices, corners, indices, vertexCount, indexCount);
    bool same = expected == actual && (!actual || (ev == *vertices && ec == *corners && eix == *indices &&
                                                   evc == *vertexCount && eic == *indexCount));
    ei::verify::Check("CLinkMesh::GetPartData", true, same, "\"%.*s\" result %d / %d", static_cast<int>(name->size()),
                      name->data(), expected, actual);
    return actual;
}

// 004F2FB0: PUSH EBX, MOV EBX,ECX, PUSH ESI (4), MOV EAX,[EBX+0x9C] (6). The original into a fresh vector.
void(__fastcall* s_collectNames)(CLinkMesh*, void*, stlp::vector<stlp::string>*);
void __fastcall CollectPartNamesVerify(CLinkMesh* self, void*, stlp::vector<stlp::string>* out) {
    std::size_t before = out->size();
    stlp::vector<stlp::string> expected;
    s_collectNames(self, nullptr, &expected);
    self->CLinkMesh::CollectPartNamesImpl(out);
    bool same = out->size() - before == expected.size();
    for (std::size_t i = 0; same && i < expected.size(); ++i) same = expected.begin()[i] == out->begin()[before + i];
    ei::verify::Check("CLinkMesh::CollectPartNames", true, same, "%u names, %u before",
                      static_cast<unsigned>(expected.size()), static_cast<unsigned>(before));
    expected.clear();
    expected.FreeStorage();
}

void* s_unused;   // registrations without a trampoline

// 004F30D0 (ours only: the loader reads files and changes the mesh). A loaded part is a part afterwards.
bool __fastcall LoadPartVerify(CLinkMesh* self, void*, const stlp::string* name) {
    bool loaded = self->CLinkMesh::LoadPartImpl(name);
    ei::verify::Check("CLinkMesh::LoadPart", true, !loaded || self->CLinkMesh::HasPartImpl(name), "\"%.*s\"",
                      static_cast<int>(name->size()), name->data());
    return loaded;
}

// 004F3530 (ours only: the original erases the map node). The result says whether the part was
// there; afterwards it is gone and each array is shorter by its count.
bool __fastcall RemovePartVerify(CLinkMesh* self, void*, const stlp::string* name) {
    bool present = HasPart(self, name);
    Sizes before(self);
    LinkPartRanges removed{};
    if (present) removed = *LinkRangeMapAtOriginal(&self->ranges_, nullptr, name);
    bool result = self->CLinkMesh::RemovePartImpl(name);
    Sizes after(self);
    bool same = result == present && !HasPart(self, name) &&
                (!present || (after.vertices == before.vertices - removed.vertices.count &&
                              after.normals == before.normals - removed.normals.count &&
                              after.texCoords == before.texCoords - removed.texCoords.count &&
                              after.corners == before.corners - removed.corners.count &&
                              after.words == before.words - removed.words.count &&
                              after.indices == before.indices - removed.indices.count));
    ei::verify::Check("CLinkMesh::RemovePart", true, same, "\"%.*s\" present %d", static_cast<int>(name->size()),
                      name->data(), present);
    return result;
}

} // namespace

EI_VERIFY(0x004F30F0, AddPartVerify, 7, s_addPart);
EI_VERIFY(0x004F7F00, LoadFigVerify, 7, s_loadFig);
EI_VERIFY(0x004F87D0, LoadOldVerify, 7, s_loadOld);
EI_VERIFY(0x004F2F90, SetMorphVerify, 8, s_setMorph);
EI_VERIFY(0x004F45E0, UpdateScreenRectVerify, 5, s_updateRect);
EI_VERIFY(0x004F3040, HasPartVerify, 6, s_hasPart);
EI_VERIFY(0x004F3780, GetPartDataVerify, 5, s_getPartData);
EI_VERIFY(0x004F2FB0, CollectPartNamesVerify, 10, s_collectNames);
EI_VERIFY(0x004F30D0, LoadPartVerify, 0, s_unused);
EI_VERIFY(0x004F3530, RemovePartVerify, 0, s_unused);
