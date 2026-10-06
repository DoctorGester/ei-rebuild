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
        mesh = MeshConstructOriginal(bytes, nullptr, like->loader_, &like->name_, like->morph_[0], like->morph_[1],
                                     like->morph_[2]);
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
    void* found;
    LinkRangeMapFindOriginal(&mesh->ranges_, nullptr, &found, name);
    return found != mesh->ranges_.header;
}

bool Near(float expected, float actual) { return ei::verify::NearlyEqual<float>(expected, actual, 8, 1e-5f); }

template <typename T>
bool SameBytes(const T* expected, const T* actual, int count) {
    return count == 0 || std::memcmp(expected, actual, static_cast<std::size_t>(count) * sizeof(T)) == 0;
}

// The scratch's whole arrays against the real mesh's elements from `before` on; vertices nearly.
bool SameAppended(const CLinkMesh* expected, const CLinkMesh* actual, const Sizes& before, int* badVertex) {
    *badVertex = -1;
    Sizes added(expected);
    Sizes after(actual);
    if (after.vertices - before.vertices != added.vertices || after.normals - before.normals != added.normals ||
        after.texCoords - before.texCoords != added.texCoords || after.corners - before.corners != added.corners ||
        after.words - before.words != added.words || after.indices - before.indices != added.indices)
        return false;
    for (int i = 0; i < added.vertices; ++i) {
        const float* e = &expected->vertices_.data[i].block[0][0];
        const float* a = &actual->vertices_.data[before.vertices + i].block[0][0];
        for (int k = 0; k < 16; ++k)
            if (!Near(e[k], a[k])) {
                *badVertex = i;
                return false;
            }
    }
    return SameBytes(expected->normals_.data, actual->normals_.data + before.normals, added.normals) &&
           SameBytes(expected->texCoords_.data, actual->texCoords_.data + before.texCoords, added.texCoords) &&
           SameBytes(expected->corners_.data, actual->corners_.data + before.corners * 3, added.corners * 3) &&
           SameBytes(expected->words_.data, actual->words_.data + before.words, added.words) &&
           SameBytes(expected->indices_.data, actual->indices_.data + before.indices, added.indices);
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
    const float* ef = &e.center[0];
    const float* af = &a.center[0];
    for (int k = 0; k < 10; ++k)
        if (!Near(ef[k], af[k])) return false;
    return true;
}

// 004F30F0: SUB ESP,0x3C (3), MOV EAX,[ESP+0x40] (4).
int(__fastcall* s_addPart)(CLinkMesh*, void*, const stlp::string*, const VertexPacket*, int, const NormalPacket*, int,
                           const TexCoord*, int, const MeshCorner*, int, const std::uint32_t*, int, const std::uint16_t*, int);
int __fastcall AddPartVerify(CLinkMesh* self, void*, const stlp::string* name, const VertexPacket* vertices, int vertexCount,
                             const NormalPacket* normals, int normalCount, const TexCoord* texCoords, int texCoordCount,
                             const MeshCorner* corners, int cornerCount, const std::uint32_t* words, int wordCount,
                             const std::uint16_t* indices, int indexCount) {
    bool present = HasPart(self, name);
    Sizes before(self);
    int actual = self->AddPart(name, vertices, vertexCount, normals, normalCount, texCoords, texCoordCount, corners,
                               cornerCount, words, wordCount, indices, indexCount);
    bool same;
    int bad = -1;
    if (present) {
        same = actual == 0 && Sizes(self) == before;
    } else {
        ScratchMesh scratch(self);
        int expected = s_addPart(scratch.mesh, nullptr, name, vertices, vertexCount, normals, normalCount, texCoords,
                                 texCoordCount, corners, cornerCount, words, wordCount, indices, indexCount);
        same = expected == actual && actual == 1 && SameAppended(scratch.mesh, self, before, &bad) &&
               SameRanges(self, name, before, Sizes(scratch.mesh));
    }
    ei::verify::Check("CLinkMesh::AddPart", true, same, "\"%.*s\" present %d result %d counts %d %d %d %d %d %d vertex %d",
                      static_cast<int>(name->size()), name->data(), present, actual, vertexCount, normalCount, texCoordCount, cornerCount, wordCount,
                      indexCount, bad);
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
    bool same = *cursor == expectedCursor && e->unknown_00 == a->unknown_00 && e->unknown_04 == a->unknown_04;
    int bad = -1;
    if (!present) {
        same = same && (vertexCount <= 0 || SameBounds(e->bounds, a->bounds)) &&
               SameAppended(scratch.mesh, mesh, before, &bad);
    }
    ei::verify::Check(what, true, same, "\"%.*s\" present %d vertices %d read %d / %d bad vertex %d",
                      static_cast<int>(name->size()), name->data(),
                      present, vertexCount, static_cast<int>(expectedCursor - start), static_cast<int>(*cursor - start), bad);
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

} // namespace

EI_VERIFY(0x004F30F0, AddPartVerify, 7, s_addPart);
EI_VERIFY(0x004F7F00, LoadFigVerify, 7, s_loadFig);
EI_VERIFY(0x004F87D0, LoadOldVerify, 7, s_loadOld);
