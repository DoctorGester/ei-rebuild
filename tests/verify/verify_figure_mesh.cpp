// Differential checks for the figure mesh loaders (src/game/figure_mesh.cpp).
#include "verify.h"

#include "../../src/game/figure_mesh.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

// A raw copy of a mesh object for the original to load into. Only the bytes up to `size` are
// copied (class 0x41 objects are 0x1E0 bytes).
struct alignas(16) MeshScratch {
    unsigned char bytes[sizeof(CFigureMesh)];
    CFigureMesh* mesh() { return reinterpret_cast<CFigureMesh*>(bytes); }
};

// ---- 005A18E0 LoadBounds ------------------------------------------------------------------------

// MOV EAX,[ESP+0x14]; SUB ESP,0x3C (7 bytes). It writes only +0x160..+0x1B4 and the cursor: the
// original runs on a copy of those bytes with a copy of the cursor, ours on the object; the
// written range and the cursor are compared.
void(__fastcall* s_loadBounds)(CFigureMesh*, void*, char**, float, float, float, std::uint32_t);
void __fastcall LoadBoundsVerify(CFigureMesh* self, void*, char** cursor, float s, float t, float u, std::uint32_t magic) {
    MeshScratch scratch;
    std::memcpy(scratch.bytes, self, 0x1B4);
    char* start = *cursor;
    char* theirs = *cursor;
    s_loadBounds(scratch.mesh(), nullptr, &theirs, s, t, u, magic);
    self->LoadBounds(cursor, s, t, u, magic);
    bool same = theirs == *cursor && std::memcmp(scratch.bytes + 0x160, reinterpret_cast<unsigned char*>(self) + 0x160, 0x1B4 - 0x160) == 0;
    int first = -1;
    for (int i = 0x160; i < 0x1B4 && first < 0; ++i)
        if (scratch.bytes[i] != reinterpret_cast<unsigned char*>(self)[i]) first = i;
    ei::verify::Check("CFigureMesh::LoadBounds", true, same, "magic %08X s %g t %g u %g: read %d/%d, first difference +0x%X",
                      magic, s, t, u, static_cast<int>(theirs - start), static_cast<int>(*cursor - start), first);
}
EI_VERIFY(0x005A18E0, LoadBoundsVerify, 7, s_loadBounds);

// ---- 005A2500 / 005A5C70 LoadFig ----------------------------------------------------------------

template <typename T>
void ClearArray(AlignedArray<T>& a) {
    a.data = nullptr;
    a.size = 0;
    a.capacity = 0;
}

template <typename T>
void FreeArray(AlignedArray<T>& a) {
    if (a.data) AlignedArray<T>::Free(a.data);
}

// Equal sizes, growth steps and contents; capacities too when ours started without a buffer (the
// original always starts without one).
template <typename T>
bool SameArray(const AlignedArray<T>& theirs, const AlignedArray<T>& ours, bool compareCapacity) {
    if (theirs.size != ours.size || theirs.growBy != ours.growBy) return false;
    if (compareCapacity && theirs.capacity != ours.capacity) return false;
    if (!theirs.data != !ours.data) return false;
    return theirs.size <= 0 || std::memcmp(theirs.data, ours.data, static_cast<std::size_t>(theirs.size) * sizeof(T)) == 0;
}

// Size, step and capacity only: for arrays the loaders size but never write (the colours of FIG
// meshes, colorsA_ of old-format ones) the contents are whatever the fresh heap block held, which
// differs between the two runs.
template <typename T>
bool SameShape(const AlignedArray<T>& theirs, const AlignedArray<T>& ours, bool compareCapacity) {
    return theirs.size == ours.size && theirs.growBy == ours.growBy && !theirs.data == !ours.data &&
           (!compareCapacity || theirs.capacity == ours.capacity);
}

// The first differing dword of two arrays of equal size (-1 if none), for the mismatch message.
template <typename T>
int FirstDifference(const AlignedArray<T>& theirs, const AlignedArray<T>& ours, std::uint32_t* a, std::uint32_t* b) {
    if (!theirs.data || !ours.data) return -1;
    int words = static_cast<int>(theirs.size * sizeof(T) / 4);
    for (int i = 0; i < words; ++i) {
        std::memcpy(a, reinterpret_cast<const char*>(theirs.data) + 4 * i, 4);
        std::memcpy(b, reinterpret_cast<const char*>(ours.data) + 4 * i, 4);
        if (*a != *b) return i;
    }
    return -1;
}

// The buffer descriptor points at the object's own arrays (or, for class 0x41, into the file
// data, where both must point at the same place).
bool SameBuffers(const CFigureMesh* theirs, const CFigureMesh* ours, bool shared) {
    const MeshBuffers& a = theirs->buffers_;
    const MeshBuffers& b = ours->buffers_;
    if (a.vertexCount != b.vertexCount || a.normalCount != b.normalCount || a.texCoordCount != b.texCoordCount ||
        a.cornerCount != b.cornerCount || a.wordCount != b.wordCount || a.indexCount != b.indexCount)
        return false;
    if (a.vertices != theirs->vertices_.data || b.vertices != ours->vertices_.data) return false;
    if (a.normals != theirs->normals_.data || b.normals != ours->normals_.data) return false;
    if (shared) return a.texCoords == b.texCoords && a.indices == b.indices && a.corners == b.corners && a.words == b.words;
    return a.texCoords == theirs->texCoords_.data && b.texCoords == ours->texCoords_.data && a.indices == theirs->indices_.data &&
           b.indices == ours->indices_.data && a.corners == theirs->corners_.data &&
           b.corners == ours->corners_.data && a.words == theirs->words_.data && b.words == ours->words_.data;
}

using LoadFn = void(__fastcall*)(CFigureMesh*, void*, char**, float, float, float);

// The original loads into a copy of the object whose arrays start empty, ours into the object;
// the loaded state is compared and the copy's arrays are freed.
void CompareLoad(const char* name, LoadFn original, void (CFigureMesh::*ours)(char**, float, float, float), bool shared,
                 bool fillsColorsB, CFigureMesh* self, char** cursor, float s, float t, float u) {
    if (self->flags_ & 0x20) {
        (self->*ours)(cursor, s, t, u);
        return;
    }
    const unsigned objectSize = shared ? 0x1E0 : sizeof(CFigureMesh);
    MeshScratch scratch;
    std::memset(scratch.bytes, 0, sizeof(scratch.bytes));
    std::memcpy(scratch.bytes, self, objectSize);
    CFigureMesh* copy = scratch.mesh();
    bool hadNone = !self->colorsA_.data && !self->colorsB_.data && !self->vertices_.data && !self->normals_.data;
    ClearArray(copy->colorsA_);
    ClearArray(copy->colorsB_);
    ClearArray(copy->vertices_);
    ClearArray(copy->normals_);
    if (!shared) {
        hadNone = hadNone && !self->corners_.data && !self->words_.data && !self->indices_.data && !self->texCoords_.data;
        ClearArray(copy->corners_);
        ClearArray(copy->words_);
        ClearArray(copy->indices_);
        ClearArray(copy->texCoords_);
    }
    const FigHeader* header = reinterpret_cast<const FigHeader*>(*cursor);
    std::uint32_t magic = header->magic;
    int vertexCount = header->vertexCount;
    char* start = *cursor;
    char* theirs = *cursor;

    original(copy, nullptr, &theirs, s, t, u);
    (self->*ours)(cursor, s, t, u);

    const char* what = nullptr;
    if (theirs != *cursor) what = "cursor";
    else if (copy->flags_ != self->flags_) what = "flags";
    else if (copy->unknown_158 != self->unknown_158 || copy->unknown_15C != self->unknown_15C) what = "header words";
    else if (std::memcmp(scratch.bytes + 0x160, reinterpret_cast<unsigned char*>(self) + 0x160, 0x1B4 - 0x160) != 0) what = "bounds";
    else if (!SameArray(copy->vertices_, self->vertices_, hadNone)) what = "vertices";
    else if (!SameArray(copy->normals_, self->normals_, hadNone)) what = "records";
    else if (!SameShape(copy->colorsA_, self->colorsA_, hadNone) || !SameShape(copy->colorsB_, self->colorsB_, hadNone)) what = "colour array sizes";
    else if (fillsColorsB && !SameArray(copy->colorsB_, self->colorsB_, hadNone)) what = "colour B contents";
    else if (!shared && (!SameArray(copy->corners_, self->corners_, hadNone) || !SameArray(copy->words_, self->words_, hadNone) ||
                         !SameArray(copy->indices_, self->indices_, hadNone) || !SameArray(copy->texCoords_, self->texCoords_, hadNone)))
        what = "index arrays";
    else if (!SameBuffers(copy, self, shared)) what = "buffers";
    std::uint32_t valueA = 0, valueB = 0;
    int at = -1;
    if (what && std::strcmp(what, "vertices") == 0) at = FirstDifference(copy->vertices_, self->vertices_, &valueA, &valueB);
    else if (what && std::strcmp(what, "colour B contents") == 0) at = FirstDifference(copy->colorsB_, self->colorsB_, &valueA, &valueB);
    ei::verify::Check(name, true, what == nullptr, "magic %08X vertices %d s %g t %g u %g read %d/%d: %s differs (dword %d: %08X / %08X)",
                      magic, vertexCount, s, t, u, static_cast<int>(theirs - start), static_cast<int>(*cursor - start),
                      what ? what : "", at, valueA, valueB);

    FreeArray(copy->colorsA_);
    FreeArray(copy->colorsB_);
    FreeArray(copy->vertices_);
    FreeArray(copy->normals_);
    if (!shared) {
        FreeArray(copy->corners_);
        FreeArray(copy->words_);
        FreeArray(copy->indices_);
        FreeArray(copy->texCoords_);
    }
}

// SUB ESP,0x44; PUSH EBP; MOV EBP,ECX (6 bytes) for both.
LoadFn s_loadFig;
void __fastcall LoadFigVerify(CFigureMesh* self, void*, char** cursor, float s, float t, float u) {
    CompareLoad("CFigureMesh::LoadFig", s_loadFig, &CFigureMesh::LoadFig, false, false, self, cursor, s, t, u);
}
EI_VERIFY(0x005A2500, LoadFigVerify, 6, s_loadFig);

LoadFn s_loadFigShared;
void __fastcall LoadFigSharedVerify(CFigureMesh* self, void*, char** cursor, float s, float t, float u) {
    CompareLoad("CFigureMesh::LoadFigShared", s_loadFigShared, &CFigureMesh::LoadFigShared, true, false, self, cursor, s, t, u);
}
EI_VERIFY(0x005A5C70, LoadFigSharedVerify, 6, s_loadFigShared);

// PUSH EBP; MOV EBP,ESP; SUB ESP,0x60 (6 bytes) for both old-format loaders. (005A4D60 does not
// test the loaded flag; CompareLoad then runs ours alone, as for the others.)
LoadFn s_loadOld;
void __fastcall LoadOldVerify(CFigureMesh* self, void*, char** cursor, float s, float t, float u) {
    CompareLoad("CFigureMesh::LoadOld", s_loadOld, &CFigureMesh::LoadOld, false, true, self, cursor, s, t, u);
}
EI_VERIFY(0x005A3800, LoadOldVerify, 6, s_loadOld);

LoadFn s_loadOldShared;
void __fastcall LoadOldSharedVerify(CFigureMesh* self, void*, char** cursor, float s, float t, float u) {
    CompareLoad("CFigureMesh::LoadOldShared", s_loadOldShared, &CFigureMesh::LoadOldShared, true, true, self, cursor, s, t, u);
}
EI_VERIFY(0x005A4D60, LoadOldSharedVerify, 6, s_loadOldShared);

// ---- 005AAE60 / 005AB9A0: the figure loader's bounds --------------------------------------------

// Output only: the original fills a copy of the bounds, ours the real ones; all 0x2C bytes compared.
// 005AAE60: MOV EAX,[ESP+0x18]; SUB ESP,0x6C (7 bytes).
void(__stdcall* s_figureBounds)(ClipBounds*, const float*, float, float, float, std::uint32_t);
void __stdcall FigureBoundsVerify(ClipBounds* out, const float* data, float s, float t, float u, std::uint32_t magic) {
    ClipBounds theirs = *out;
    s_figureBounds(&theirs, data, s, t, u, magic);
    MorphFigureBounds(out, data, s, t, u, magic);
    ei::verify::Check("MorphFigureBounds", true, std::memcmp(&theirs, out, sizeof(ClipBounds)) == 0,
                      "magic %08X s %g t %g u %g", magic, s, t, u);
}
EI_VERIFY(0x005AAE60, FigureBoundsVerify, 7, s_figureBounds);

// 005AB9A0: SUB ESP,0x54; PUSH EBX; PUSH EBP; PUSH ESI (6 bytes).
void(__stdcall* s_figureBounds6)(ClipBounds*, const float*, float, float, float);
void __stdcall FigureBounds6Verify(ClipBounds* out, const float* data, float s, float t, float u) {
    ClipBounds theirs = *out;
    s_figureBounds6(&theirs, data, s, t, u);
    MorphFigureBounds6(out, data, s, t, u);
    ei::verify::Check("MorphFigureBounds6", true, std::memcmp(&theirs, out, sizeof(ClipBounds)) == 0,
                      "s %g t %g u %g", s, t, u);
}
EI_VERIFY(0x005AB9A0, FigureBounds6Verify, 6, s_figureBounds6);

// ---- 0059FDF0 ExtendWorldBox --------------------------------------------------------------------

// PUSH EBP; MOV EBP,ESP; SUB ESP,0x84 (9 bytes). Output only: the original grows copies of the
// six values, ours the caller's; compared bitwise.
void(__fastcall* s_extendWorldBox)(const CFigureMesh*, void*, float*, float*, float*, float*, float*, float*);
void __fastcall ExtendWorldBoxVerify(const CFigureMesh* self, void*, float* maxX, float* maxY, float* maxZ, float* minX,
                                     float* minY, float* minZ) {
    float theirs[6] = {*maxX, *maxY, *maxZ, *minX, *minY, *minZ};
    s_extendWorldBox(self, nullptr, &theirs[0], &theirs[1], &theirs[2], &theirs[3], &theirs[4], &theirs[5]);
    self->ExtendWorldBox(maxX, maxY, maxZ, minX, minY, minZ);
    float ours[6] = {*maxX, *maxY, *maxZ, *minX, *minY, *minZ};
    ei::verify::Check("CFigureMesh::ExtendWorldBox", true, std::memcmp(theirs, ours, sizeof(ours)) == 0,
                      "max %g %g %g / %g %g %g, min %g %g %g / %g %g %g", theirs[0], theirs[1], theirs[2], ours[0], ours[1],
                      ours[2], theirs[3], theirs[4], theirs[5], ours[3], ours[4], ours[5]);
}
EI_VERIFY(0x0059FDF0, ExtendWorldBoxVerify, 9, s_extendWorldBox);

// ---- Mirroring, buffers, offsets ----------------------------------------------------------------

// Snapshot of the arrays a mirror changes.
struct MirrorState {
    std::vector<unsigned char> vertices, normals, indices;
    explicit MirrorState(const CFigureMesh* m) {
        auto take = [](std::vector<unsigned char>& v, const void* p, int bytes) {
            if (p && bytes > 0) v.assign(static_cast<const unsigned char*>(p), static_cast<const unsigned char*>(p) + bytes);
        };
        take(vertices, m->vertices_.data, m->vertices_.size * 0x40);
        take(normals, m->normals_.data, m->normals_.size * 0x40);
        take(indices, m->indices_.data, m->indices_.size * 2);
    }
    bool operator==(const MirrorState& o) const { return vertices == o.vertices && normals == o.normals && indices == o.indices; }
};

// A mirror is its own inverse: ours runs, then the original must restore the snapshot exactly;
// then ours runs again to leave the mirrored state. (A signalling NaN would come back quieted
// from either version's FLD.)
using MirrorFn = void(__fastcall*)(CFigureMesh*, void*);
void CompareMirror(const char* name, MirrorFn original, void (CFigureMesh::*ours)(), CFigureMesh* self) {
    MirrorState before(self);
    (self->*ours)();
    original(self, nullptr);
    MirrorState back(self);
    (self->*ours)();
    ei::verify::Check(name, true, back == before, "vertices %d normals %d indices %d", self->vertices_.size,
                      self->normals_.size, self->indices_.size);
}

// 005A4B30: MOV EAX,[ECX+0x1C4] (6 bytes); 005A4BF0 the same.
MirrorFn s_mirrorX;
void __fastcall MirrorXVerify(CFigureMesh* self, void*) { CompareMirror("CFigureMesh::MirrorX", s_mirrorX, &CFigureMesh::MirrorX, self); }
EI_VERIFY(0x005A4B30, MirrorXVerify, 6, s_mirrorX);
MirrorFn s_mirrorY;
void __fastcall MirrorYVerify(CFigureMesh* self, void*) { CompareMirror("CFigureMesh::MirrorY", s_mirrorY, &CFigureMesh::MirrorY, self); }
EI_VERIFY(0x005A4BF0, MirrorYVerify, 6, s_mirrorY);

// 005A6E70: MOV EAX,[ESP+8]; PUSH EBX (5 bytes). The original on a copy with its own buffer, ours
// on the real array; sizes, capacity, step and the kept elements compared.
void(__fastcall* s_resize)(AlignedArray<std::uint32_t>*, void*, int, int);
void __fastcall ResizeVerify(AlignedArray<std::uint32_t>* self, void*, int count, int growBy) {
    AlignedArray<std::uint32_t> scratch = *self;
    if (self->data) {
        scratch.data = AlignedArray<std::uint32_t>::Allocate(self->capacity);
        std::memcpy(scratch.data, self->data, static_cast<std::size_t>(self->size) * 4);
    }
    int oldSize = self->size;
    s_resize(&scratch, nullptr, count, growBy);
    self->Resize(count, growBy);
    int kept = oldSize < self->size ? oldSize : self->size;
    bool same = scratch.size == self->size && scratch.capacity == self->capacity && scratch.growBy == self->growBy &&
                !scratch.data == !self->data &&
                (!self->data || kept <= 0 || std::memcmp(scratch.data, self->data, static_cast<std::size_t>(kept) * 4) == 0);
    ei::verify::Check("AlignedArray<uint32>::Resize", true, same, "%d -> %d: size %d/%d cap %d/%d", oldSize, count,
                      scratch.size, self->size, scratch.capacity, self->capacity);
    if (scratch.data) AlignedArray<std::uint32_t>::Free(scratch.data);
}
EI_VERIFY(0x005A6E70, ResizeVerify, 5, s_resize);

// 005A6D40 (stateful: resizes the colour arrays), ours only: the descriptor matches the arrays.
void(__fastcall* s_refreshBuffers)(CFigureMesh*, void*);
void __fastcall RefreshBuffersVerify(CFigureMesh* self, void*) {
    self->RefreshBuffers();
    const MeshBuffers& b = self->buffers_;
    bool ok = b.vertices == self->vertices_.data && b.vertexCount == self->vertices_.size && b.normals == self->normals_.data &&
              b.normalCount == self->normals_.size && b.texCoords == self->texCoords_.data &&
              b.texCoordCount == self->texCoords_.size && b.corners == self->corners_.data &&
              b.cornerCount == self->corners_.size && b.words == self->words_.data && b.wordCount == self->words_.size &&
              b.indices == self->indices_.data && b.indexCount == self->indices_.size &&
              self->colorsA_.size == self->normals_.size * 4 && self->colorsB_.size == self->normals_.size * 4;
    ei::verify::Check("CFigureMesh::RefreshBuffers", true, ok, "normals %d", self->normals_.size);
}
EI_VERIFY(0x005A6D40, RefreshBuffersVerify, 0, s_refreshBuffers);

// 005A0B10 (allocates), ours only: the offsets equal the source.
void(__fastcall* s_setOffsets)(CFigureMesh*, void*, const CFigureMesh::OffsetList*);
void __fastcall SetOffsetsVerify(CFigureMesh* self, void*, const CFigureMesh::OffsetList* offsets) {
    self->SetOffsets(offsets);
    bool ok = self->offsets_.size == offsets->count &&
              (offsets->count <= 0 || std::memcmp(self->offsets_.data, offsets->data, static_cast<std::size_t>(offsets->count) * 12) == 0);
    ei::verify::Check("CFigureMesh::SetOffsets", true, ok, "count %d", offsets->count);
}
EI_VERIFY(0x005A0B10, SetOffsetsVerify, 0, s_setOffsets);

// 005A0CD0 (draws), ours only: the result and, with offsets, the shared buffer's first vertex.
int(__fastcall* s_drawOffset)(CFigureMesh*, void*, void*, void*, void*, void*);
int __fastcall DrawOffsetVerify(CFigureMesh* self, void*, void* owner, void* a1, void* a2, void* a3) {
    int result = self->DrawOffset(owner, a1, a2, a3);
    bool ok = result == self->buffers_.vertexCount * 4;
    const auto* moved = reinterpret_cast<const AlignedArray<VertexPacket>*>(0x007C2E18);
    if (ok && self->offsets_.size > 0 && self->buffers_.vertexCount > 0) {
        const auto* src = static_cast<const VertexPacket*>(self->buffers_.vertices);
        float x = self->offsets_.data[0].x + src[0].block[0][0];
        ok = moved->size == self->buffers_.vertexCount && std::memcmp(&x, &moved->data[0].block[0][0], 4) == 0;
    }
    ei::verify::Check("CFigureMesh::DrawOffset", true, ok, "vertices %d offsets %d result %d", self->buffers_.vertexCount,
                      self->offsets_.size, result);
    return result;
}
EI_VERIFY(0x005A0CD0, DrawOffsetVerify, 0, s_drawOffset);

} // namespace
