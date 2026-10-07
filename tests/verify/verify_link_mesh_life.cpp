// Checks for CLinkMesh's constructor and destructors (link_figure.cpp, A-S8). The constructor runs
// against the original on a scratch mesh (destroyed again by the original destructor); the
// destructors are checked ours only. Scenarios: `newgame`, `models` (the loader's MakeFigure /
// LoadLinkFile construct meshes; ClearCache and released references destroy them through slot 0).
// 004F2D40 / 004F2E00 belong to the base vtable 0073D7CC and are reached only from EH unwinding.
#include "verify.h"

#include "../../src/game/link_figure.h"

#include <cstring>

namespace {

std::uintptr_t VtableOf(const void* object) {
    std::uintptr_t vtable;
    std::memcpy(&vtable, object, sizeof(vtable));
    return vtable;
}

template <typename T>
bool EmptyArray(const lib::AlignedArray<T>& a) {
    return !a.data && a.size == 0 && a.capacity == 0 && a.growBy == 0;
}

template <typename M>
bool EmptyMap(const M& map) {
    const auto* header = map.end();
    return header && map.size() == 0 && !header->parent && header->left == header && header->right == header;
}

bool SameFloatBits(float a, float b) { return std::memcmp(&a, &b, sizeof(float)) == 0; }

// The first field of `actual` that differs from the original's fresh mesh `expected`, or null.
const char* ConstructedDiff(const CLinkMesh* expected, const CLinkMesh* actual) {
    if (VtableOf(expected) != VtableOf(actual)) return "vtable";
    if (expected->refCount_ != actual->refCount_) return "refCount";
    if (!(expected->name_ == actual->name_)) return "name";
    for (int k = 0; k < 3; ++k)
        if (!SameFloatBits(expected->morph_[k], actual->morph_[k])) return "morph";
    if (!EmptyArray(actual->vertices_) || !EmptyArray(actual->normals_) || !EmptyArray(actual->texCoords_) ||
        !EmptyArray(actual->corners_) || !EmptyArray(actual->words_) || !EmptyArray(actual->indices_))
        return "arrays";
    if (!EmptyMap(actual->headers_) || !EmptyMap(actual->ranges_)) return "maps";
    if (!EmptyMap(expected->headers_) || !EmptyMap(expected->ranges_)) return "original maps";
    if (expected->loader_ != actual->loader_) return "loader";
    if (expected->pipeline_ != actual->pipeline_) return "pipeline";
    return nullptr;
}

} // namespace

// 004F2E40: PUSH -1 (2), PUSH imm32 (5). Ours only: the root vtable afterwards (the memory is
// freed by the caller next, so nothing else is left to compare).
static void(__fastcall* s_destruct)(CLinkMesh*);
static void __fastcall DestructVerify(CLinkMesh* self) {
    self->Destruct();
    ei::verify::Check("CLinkMesh::Destruct", CLinkMesh::kRootVtableAddress, VtableOf(self), "%08X");
}
EI_VERIFY(0x004F2E40, DestructVerify, 7, s_destruct);

// 004F2C00: PUSH -1 (2), PUSH imm32 (5). The original into a scratch mesh, ours into the real one.
static CLinkMesh*(__fastcall* s_construct)(CLinkMesh*, void*, CLinkFigureLoader*, const stlp::string*, float, float, float);
static CLinkMesh* __fastcall ConstructVerify(CLinkMesh* self, void*, CLinkFigureLoader* loader, const stlp::string* name,
                                             float s, float t, float u) {
    alignas(16) unsigned char scratch[sizeof(CLinkMesh)];
    CLinkMesh* expected = s_construct(reinterpret_cast<CLinkMesh*>(scratch), nullptr, loader, name, s, t, u);
    CLinkMesh* actual = self->Construct(loader, name, s, t, u);
    const char* diff = actual != self ? "result" : ConstructedDiff(expected, actual);
    ei::verify::Check("CLinkMesh::Construct", true, diff == nullptr, "%s (%s)", diff ? diff : "", name->data());
    s_destruct(expected);
    return actual;
}
EI_VERIFY(0x004F2C00, ConstructVerify, 7, s_construct);

// 004F2D40 (ours only): the root vtable afterwards.
static void(__fastcall* s_destructBase)(CLinkMesh*);
static void __fastcall DestructBaseVerify(CLinkMesh* self) {
    self->DestructBase();
    ei::verify::Check("CLinkMesh::DestructBase", CLinkMesh::kRootVtableAddress, VtableOf(self), "%08X");
}
EI_VERIFY(0x004F2D40, DestructBaseVerify, 0, s_destructBase);

// 004F2E20 / 004F2E00 (ours only; PUSH ESI, MOV ESI,ECX, then a CALL: no 5 stolen bytes): this.
static void*(__fastcall* s_deleteThis)(CLinkMesh*, void*, unsigned);
static void* __fastcall DeleteThisVerify(CLinkMesh* self, void*, unsigned flags) {
    void* result = self->CLinkMesh::DeleteThis(flags);
    ei::verify::Check("CLinkMesh::DeleteThis", static_cast<void*>(self), result, "%p");
    return result;
}
EI_VERIFY(0x004F2E20, DeleteThisVerify, 0, s_deleteThis);

static CLinkMesh*(__fastcall* s_deletingBase)(CLinkMesh*, void*, unsigned);
static CLinkMesh* __fastcall DeletingDestructBaseVerify(CLinkMesh* self, void*, unsigned flags) {
    CLinkMesh* result = self->DeletingDestructBase(flags);
    ei::verify::Check("CLinkMesh::DeletingDestructBase", self, result, "%p");
    return result;
}
EI_VERIFY(0x004F2E00, DeletingDestructBaseVerify, 0, s_deletingBase);

// 004F4B50 (pure): MOV EDX,[ECX+0x50] (3), MOV EAX,[ECX+0x40] (3). Against the original. Its only
// caller is the loader's slot 2 (004FAAA0), which no scenario is known to reach.
static int(__fastcall* s_memoryBytes)(const CLinkMesh*);
static int __fastcall MemoryBytesVerify(const CLinkMesh* self) {
    const int expected = s_memoryBytes(self);
    const int actual = self->MemoryBytes();
    ei::verify::Check("CLinkMesh::MemoryBytes", expected, actual, "%d");
    return actual;
}
EI_VERIFY(0x004F4B50, MemoryBytesVerify, 6, s_memoryBytes);
