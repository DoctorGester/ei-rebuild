// Differential checks for the figure mesh loaders (src/game/figure_mesh.cpp).
#include "verify.h"

#include "../../src/game/figure_mesh.h"

#include <cstdint>
#include <cstring>

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

// The buffer descriptor points at the object's own arrays (or, for class 0x41, into the file
// data, where both must point at the same place).
bool SameBuffers(const CFigureMesh* theirs, const CFigureMesh* ours, bool shared) {
    const MeshBuffers& a = theirs->buffers_;
    const MeshBuffers& b = ours->buffers_;
    if (a.vertexCount != b.vertexCount || a.recordCount != b.recordCount || a.pairCount != b.pairCount ||
        a.triangleCount != b.triangleCount || a.wordCount != b.wordCount || a.shortCount != b.shortCount)
        return false;
    if (a.vertices != theirs->vertices_.data || b.vertices != ours->vertices_.data) return false;
    if (a.records != theirs->records_.data || b.records != ours->records_.data) return false;
    if (shared) return a.pairs == b.pairs && a.shorts == b.shorts && a.triangles == b.triangles && a.words == b.words;
    return a.pairs == theirs->pairs_.data && b.pairs == ours->pairs_.data && a.shorts == theirs->shorts_.data &&
           b.shorts == ours->shorts_.data && a.triangles == theirs->triangles_.data &&
           b.triangles == ours->triangles_.data && a.words == theirs->words_.data && b.words == ours->words_.data;
}

using LoadFn = void(__fastcall*)(CFigureMesh*, void*, char**, float, float, float);

// The original loads into a copy of the object whose arrays start empty, ours into the object;
// the loaded state is compared and the copy's arrays are freed.
void CompareLoad(const char* name, LoadFn original, void (CFigureMesh::*ours)(char**, float, float, float), bool shared,
                 CFigureMesh* self, char** cursor, float s, float t, float u) {
    if (self->flags_ & 0x20) {
        (self->*ours)(cursor, s, t, u);
        return;
    }
    const unsigned objectSize = shared ? 0x1E0 : sizeof(CFigureMesh);
    MeshScratch scratch;
    std::memset(scratch.bytes, 0, sizeof(scratch.bytes));
    std::memcpy(scratch.bytes, self, objectSize);
    CFigureMesh* copy = scratch.mesh();
    bool hadNone = !self->colorsA_.data && !self->colorsB_.data && !self->vertices_.data && !self->records_.data;
    ClearArray(copy->colorsA_);
    ClearArray(copy->colorsB_);
    ClearArray(copy->vertices_);
    ClearArray(copy->records_);
    if (!shared) {
        hadNone = hadNone && !self->triangles_.data && !self->words_.data && !self->shorts_.data && !self->pairs_.data;
        ClearArray(copy->triangles_);
        ClearArray(copy->words_);
        ClearArray(copy->shorts_);
        ClearArray(copy->pairs_);
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
    else if (!SameArray(copy->records_, self->records_, hadNone)) what = "records";
    else if (!SameArray(copy->colorsA_, self->colorsA_, hadNone) || !SameArray(copy->colorsB_, self->colorsB_, hadNone)) what = "colour arrays";
    else if (!shared && (!SameArray(copy->triangles_, self->triangles_, hadNone) || !SameArray(copy->words_, self->words_, hadNone) ||
                         !SameArray(copy->shorts_, self->shorts_, hadNone) || !SameArray(copy->pairs_, self->pairs_, hadNone)))
        what = "index arrays";
    else if (!SameBuffers(copy, self, shared)) what = "buffers";
    ei::verify::Check(name, true, what == nullptr, "magic %08X vertices %d s %g t %g u %g read %d/%d: %s differs", magic,
                      vertexCount, s, t, u, static_cast<int>(theirs - start), static_cast<int>(*cursor - start),
                      what ? what : "");

    FreeArray(copy->colorsA_);
    FreeArray(copy->colorsB_);
    FreeArray(copy->vertices_);
    FreeArray(copy->records_);
    if (!shared) {
        FreeArray(copy->triangles_);
        FreeArray(copy->words_);
        FreeArray(copy->shorts_);
        FreeArray(copy->pairs_);
    }
}

// SUB ESP,0x44; PUSH EBP; MOV EBP,ECX (6 bytes) for both.
LoadFn s_loadFig;
void __fastcall LoadFigVerify(CFigureMesh* self, void*, char** cursor, float s, float t, float u) {
    CompareLoad("CFigureMesh::LoadFig", s_loadFig, &CFigureMesh::LoadFig, false, self, cursor, s, t, u);
}
EI_VERIFY(0x005A2500, LoadFigVerify, 6, s_loadFig);

LoadFn s_loadFigShared;
void __fastcall LoadFigSharedVerify(CFigureMesh* self, void*, char** cursor, float s, float t, float u) {
    CompareLoad("CFigureMesh::LoadFigShared", s_loadFigShared, &CFigureMesh::LoadFigShared, true, self, cursor, s, t, u);
}
EI_VERIFY(0x005A5C70, LoadFigSharedVerify, 6, s_loadFigShared);

} // namespace
