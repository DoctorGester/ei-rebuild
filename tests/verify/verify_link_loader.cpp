// Checks for link_loader.cpp. Scenarios: newgame (the world client's objects get their figures
// through slot 3 004F7150 -> 004F7B10, units through slot 4 004F7080 -> 004F77C0, which reads each
// part's names with 004DB9C0), models / ui_hero_screen (figures and parts loaded and morphed again).
#include "verify.h"

#include "../../src/game/link_figure.h"

#include <cstring>

namespace {

using Cache = stlp::map<stlp::string, CLinkMesh*>;

bool SameName(const CLinkMesh* mesh, const stlp::string* name) { return mesh && mesh->name_ == *name; }

// Whether the cache holds `mesh` under `name`.
bool Cached(const CLinkFigureLoader* loader, const stlp::string* name, const CLinkMesh* mesh) {
    const Cache& cache = loader->cache_;
    Cache::Node* last = cache.upper_bound(*name);
    for (Cache::Node* n = cache.lower_bound(*name); n != last; n = Cache::Next(n))
        if (n->second == mesh) return true;
    return false;
}

bool MorphIs(const CLinkMesh* mesh, float s, float t, float u) {
    const float want[3] = {s, t, u};
    return std::memcmp(mesh->morphWeights_, want, sizeof(want)) == 0;
}

} // namespace

// 004DB9C0: PUSH ECX; MOV EAX,[EDX]; PUSH EBX; PUSH EBP (5 bytes). The original into a copy of the
// string with its own cursor, then ours; text and cursor compared.
static void (__fastcall* s_readLinkString)(stlp::string*, const char**);
static void __fastcall ReadLinkStringVerify(stlp::string* out, const char** cursor) {
    stlp::string expected(*out);
    const char* expectedCursor = *cursor;
    s_readLinkString(&expected, &expectedCursor);
    ReadLinkString(out, cursor);
    ei::verify::Check("ReadLinkString cursor", expectedCursor, *cursor, "%p");
    ei::verify::Check("ReadLinkString", true, expected == *out, "length %u", static_cast<unsigned>(out->size()));
}
EI_VERIFY(0x004DB9C0, ReadLinkStringVerify, 5, s_readLinkString);

// 004F7370: PUSH -1; PUSH imm32 (7 bytes). The original into a scratch string, then ours.
static stlp::string* (__fastcall* s_copyName)(const CLinkMesh*, void*, stlp::string*);
static stlp::string* __fastcall CopyNameVerify(const CLinkMesh* mesh, void*, stlp::string* out) {
    alignas(stlp::string) unsigned char scratch[sizeof(stlp::string)];
    auto* expected = s_copyName(mesh, nullptr, reinterpret_cast<stlp::string*>(scratch));
    stlp::string* actual = mesh->CopyName(out);
    ei::verify::Check("CLinkMesh::CopyName", true, actual == out && *expected == *out, "length %u",
                      static_cast<unsigned>(out->size()));
    expected->~string();
    return actual;
}
EI_VERIFY(0x004F7370, CopyNameVerify, 7, s_copyName);

// The loader's members and slots (ours only): the meshes they hand out and what the cache holds.

// 004F7150 slot 3: a mesh of that name in the cache; a fresh request adds one.
static CLinkMesh* (__fastcall* s_loadFigure)(CLinkFigureLoader*, void*, const stlp::string*,
                                             const stlp::vector<stlp::string>*, float, float, float, bool);
static CLinkMesh* __fastcall LoadFigureVerify(CLinkFigureLoader* self, void*, const stlp::string* name,
                                              const stlp::vector<stlp::string>* parts, float s, float t, float u,
                                              bool fresh) {
    const std::size_t before = self->cache_.size();
    CLinkMesh* mesh = self->CLinkFigureLoader::LoadFigure(name, parts, s, t, u, fresh);
    const std::size_t added = self->cache_.size() - before;
    const bool ok = SameName(mesh, name) && Cached(self, name, mesh) && added <= 1 && (!fresh || added == 1);
    ei::verify::Check("CLinkFigureLoader::LoadFigure", true, ok, "%s", name->data());
    return mesh;
}
EI_VERIFY(0x004F7150, LoadFigureVerify, 0, s_loadFigure);

// 004F7080 slot 4: the same for .lnk figures.
static CLinkMesh* (__fastcall* s_loadLink)(CLinkFigureLoader*, void*, const stlp::string*, float, float, float, bool);
static CLinkMesh* __fastcall LoadLinkVerify(CLinkFigureLoader* self, void*, const stlp::string* name, float s, float t,
                                            float u, bool fresh) {
    const std::size_t before = self->cache_.size();
    CLinkMesh* mesh = self->CLinkFigureLoader::LoadLink(name, s, t, u, fresh);
    const std::size_t added = self->cache_.size() - before;
    const bool ok = SameName(mesh, name) && Cached(self, name, mesh) && added <= 1 && (!fresh || added == 1);
    ei::verify::Check("CLinkFigureLoader::LoadLink", true, ok, "%s", name->data());
    return mesh;
}
EI_VERIFY(0x004F7080, LoadLinkVerify, 0, s_loadLink);

// 004F75E0 slot 6: null stays null; a shared mesh gives one of the same name; else the mesh itself
// with the new coefficients.
static CLinkMesh* (__fastcall* s_remorph)(CLinkFigureLoader*, void*, CLinkMesh*, float, float, float);
static CLinkMesh* __fastcall RemorphVerify(CLinkFigureLoader* self, void*, CLinkMesh* mesh, float s, float t, float u) {
    const bool shared = mesh && mesh->refCount_ > 2;
    stlp::string name;
    if (mesh) mesh->CopyName(&name);
    CLinkMesh* result = self->CLinkFigureLoader::Remorph(mesh, s, t, u);
    bool ok;
    if (!mesh)
        ok = result == nullptr;
    else if (shared)
        ok = SameName(result, &name);
    else
        ok = result == mesh && MorphIs(mesh, s, t, u);
    ei::verify::Check("CLinkFigureLoader::Remorph", true, ok, "shared %d", shared ? 1 : 0);
    return result;
}
EI_VERIFY(0x004F75E0, RemorphVerify, 0, s_remorph);

// 004F7020 slot 5: the cache is empty.
static void (__fastcall* s_clearCache)(CLinkFigureLoader*);
static void __fastcall ClearCacheVerify(CLinkFigureLoader* self) {
    self->CLinkFigureLoader::ClearCache();
    const Cache& cache = self->cache_;
    ei::verify::Check("CLinkFigureLoader::ClearCache", true,
                      cache.size() == 0 && cache.begin() == cache.end() && cache.end()->parent == nullptr, "%u",
                      static_cast<unsigned>(cache.size()));
}
EI_VERIFY(0x004F7020, ClearCacheVerify, 0, s_clearCache);

// 004F77C0 / 004F7B10: a new mesh with the coefficients, cached once (its only reference).
static void CheckNewMesh(const char* what, CLinkFigureLoader* self, std::size_t before, const stlp::string* name,
                         const CLinkMesh* mesh, float s, float t, float u) {
    const bool ok = SameName(mesh, name) && MorphIs(mesh, s, t, u) && mesh->refCount_ == 1 &&
                    self->cache_.size() == before + 1 && Cached(self, name, mesh);
    ei::verify::Check(what, true, ok, "%s", name->data());
}

static CLinkMesh* (__fastcall* s_loadLinkFile)(CLinkFigureLoader*, void*, const stlp::string*, float, float, float);
static CLinkMesh* __fastcall LoadLinkFileVerify(CLinkFigureLoader* self, void*, const stlp::string* name, float s,
                                                float t, float u) {
    const std::size_t before = self->cache_.size();
    CLinkMesh* mesh = self->LoadLinkFile(name, s, t, u);
    if (mesh) CheckNewMesh("CLinkFigureLoader::LoadLinkFile", self, before, name, mesh, s, t, u);
    return mesh;
}
EI_VERIFY(0x004F77C0, LoadLinkFileVerify, 0, s_loadLinkFile);

static CLinkMesh* (__fastcall* s_makeFigure)(CLinkFigureLoader*, void*, const stlp::string*,
                                             const stlp::vector<stlp::string>*, float, float, float);
static CLinkMesh* __fastcall MakeFigureVerify(CLinkFigureLoader* self, void*, const stlp::string* name,
                                              const stlp::vector<stlp::string>* parts, float s, float t, float u) {
    const std::size_t before = self->cache_.size();
    CLinkMesh* mesh = self->MakeFigure(name, parts, s, t, u);
    CheckNewMesh("CLinkFigureLoader::MakeFigure", self, before, name, mesh, s, t, u);
    return mesh;
}
EI_VERIFY(0x004F7B10, MakeFigureVerify, 0, s_makeFigure);

// 004F7480 / 004F7D20: a part reported loaded is a part of the mesh.
static bool (__fastcall* s_loadPartInto)(CLinkFigureLoader*, void*, CLinkMesh*, const stlp::string*);
static bool __fastcall LoadPartIntoVerify(CLinkFigureLoader* self, void*, CLinkMesh* mesh, const stlp::string* name) {
    const bool loaded = self->LoadPartInto(mesh, name);
    ei::verify::Check("CLinkFigureLoader::LoadPartInto", true, !loaded || mesh->HasPart(name), "%s", name->data());
    return loaded;
}
EI_VERIFY(0x004F7480, LoadPartIntoVerify, 0, s_loadPartInto);

static int (__fastcall* s_loadArchivePart)(CLinkFigureLoader*, void*, CLinkMesh*, const stlp::string*, float, float,
                                           float);
static int __fastcall LoadArchivePartVerify(CLinkFigureLoader* self, void*, CLinkMesh* mesh, const stlp::string* name,
                                            float s, float t, float u) {
    const int loaded = self->LoadArchivePart(mesh, name, s, t, u);
    ei::verify::Check("CLinkFigureLoader::LoadArchivePart", true, loaded == 0 || (loaded == 1 && mesh->HasPart(name)),
                      "%s", name->data());
    return loaded;
}
EI_VERIFY(0x004F7D20, LoadArchivePartVerify, 0, s_loadArchivePart);

// 004FAB00 (A-S8): PUSH -1; PUSH imm32 (7 bytes). A cached set: the original's lookup and ours agree.
// A new one (ours only): a non-null set is stored under the name; null leaves no entry.
static FigureModel* (__fastcall* s_findFigureSet)(CLinkFigureLoader*, void*, const stlp::string*);
static FigureModel* __fastcall FindFigureSetVerify(CLinkFigureLoader* self, void*, const stlp::string* name) {
    if (self->figureSets_.Find(*name)) {
        FigureModel* expected = s_findFigureSet(self, nullptr, name);
        FigureModel* actual = self->FindFigureSet(name);
        ei::verify::Check("CLinkFigureLoader::FindFigureSet", expected, actual, "%p");
        return actual;
    }
    const std::size_t before = self->figureSets_.Size();
    FigureModel* actual = self->FindFigureSet(name);
    auto* node = self->figureSets_.Find(*name);
    const bool ok = actual ? node && node->value == actual && self->figureSets_.Size() == before + 1
                           : !node && self->figureSets_.Size() == before;
    ei::verify::Check("CLinkFigureLoader::FindFigureSet (new)", true, ok, "%s", name->data());
    return actual;
}
EI_VERIFY(0x004FAB00, FindFigureSetVerify, 7, s_findFigureSet);

// 004F6BD0 / 004F6A30 (A-S8, ours only): the counted root's vtable afterwards; DeleteThis returns
// this. Reached when the client world is destroyed (world_load's load, leaving to the menu, exit).
static void (__fastcall* s_loaderDestruct)(CLinkFigureLoader*);
static void __fastcall LoaderDestructVerify(CLinkFigureLoader* self) {
    self->Destruct();
    std::uintptr_t vtable;
    std::memcpy(&vtable, self, sizeof(vtable));
    ei::verify::Check("CLinkFigureLoader::Destruct", CLinkMesh::kRootVtableAddress, vtable, "%08X");
}
EI_VERIFY(0x004F6BD0, LoaderDestructVerify, 0, s_loaderDestruct);

static void* (__fastcall* s_loaderDeleteThis)(CLinkFigureLoader*, void*, unsigned);
static void* __fastcall LoaderDeleteThisVerify(CLinkFigureLoader* self, void*, unsigned flags) {
    void* result = self->CLinkFigureLoader::DeleteThis(flags);
    ei::verify::Check("CLinkFigureLoader::DeleteThis", static_cast<void*>(self), result, "%p");
    return result;
}
EI_VERIFY(0x004F6A30, LoaderDeleteThisVerify, 0, s_loaderDeleteThis);

// 004FAAA0 slot 2 (pure): PUSH EBX; MOV EBX,ECX; PUSH ESI; PUSH EDI (5 bytes). Against the original;
// no scenario is known to reach it.
static int (__fastcall* s_cacheBytes)(CLinkFigureLoader*);
static int __fastcall CacheBytesVerify(CLinkFigureLoader* self) {
    const int expected = s_cacheBytes(self);
    const int actual = self->CLinkFigureLoader::CacheBytes();
    ei::verify::Check("CLinkFigureLoader::CacheBytes", expected, actual, "%d");
    return actual;
}
EI_VERIFY(0x004FAAA0, CacheBytesVerify, 5, s_cacheBytes);
