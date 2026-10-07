// The link figure loader's .lnk reading and mesh cache (A-S7): 004DB9C0, 004F7020-004F7D20. See
// link_figure.h and CLinkFigureLoader in model.h.
#include "link_figure.h"

#include "originals_render.h"  // SurfaceHolderReleaseOriginal
#include "originals_world.h"   // FigureModelConstructOriginal, FigureModelLoadOriginal, FigureModelDestructOriginal
#include "resource.h"          // CMappedResource

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"

#include <cstdint>
#include <cstring>
#include <new>

void __fastcall ReadLinkString(stlp::string* out, const char** cursor) {
    std::uint32_t length;
    std::memcpy(&length, *cursor, 4);
    *cursor += 4;
    if (length == 0) return;
    const char* text = *cursor;
    const void* zero = std::memchr(text, 0, length);
    out->Assign(text, zero ? static_cast<const char*>(zero) : text + length);
    *cursor += length;
}
EI_REPLACE(0x004DB9C0, ReadLinkString);

stlp::string* CLinkMesh::CopyName(stlp::string* out) const { return new (out) stlp::string(name_); }
EI_REPLACE_METHOD(0x004F7370, CLinkMesh::CopyName);

namespace {

using Cache = stlp::map<stlp::string, CLinkMesh*>;

constexpr std::uint32_t kFigTag = 0x474946;   // "FIG": a FIG / FIG8 record, else the older one
constexpr double kSameMorph = 1e-5;           // 0073B860: squared distance of the coefficients

// The cache's value_type as insert_equal takes it: the name and a counted reference.
struct CacheValue {
    stlp::string name;
    CLinkMesh* mesh;
};

// 004FAB00 (RET 4; CLinkFigureLoader::FindFigureSet), called through its address so its check sees these calls.
__declspec(naked) LinkFigureSet* __fastcall FindFigureSetAt(CLinkFigureLoader*, void*, const stlp::string* /*name*/)
    EI_ORIGINAL(0x004FAB00)
// 004F2C00 (RET 0x14, returns this): the mesh constructor (loader, name, coefficients; no reference);
// rewritten (CLinkMesh::Construct), called through its address so its check sees these calls.
__declspec(naked) CLinkMesh* __fastcall MeshConstruct(void* /*memory*/, void*, CLinkFigureLoader*, const stlp::string*,
                                                      float, float, float) EI_ORIGINAL(0x004F2C00)
// 004F9E50 (RET 0x18, AL; CLinkFigureLoader::RemorphPart) and the record morphs 004F9FD0 / 004FA670
// (RET 0x18), called through their addresses so their checks see these calls.
__declspec(naked) bool __fastcall RemorphPartAt(CLinkFigureLoader*, void*, CLinkMesh*, LinkFigureSet*,
                                                const stlp::string* /*name*/, float, float, float) EI_ORIGINAL(0x004F9E50)
__declspec(naked) void __fastcall RemorphFigPartAt(CLinkFigureLoader*, void*, CLinkMesh*, const stlp::string*,
                                                   const char**, float, float, float) EI_ORIGINAL(0x004F9FD0)
__declspec(naked) void __fastcall RemorphOldPartAt(CLinkFigureLoader*, void*, CLinkMesh*, const stlp::string*,
                                                   const char**, float, float, float) EI_ORIGINAL(0x004FA670)
// 004FBD80 (RET 8): multimap insert_equal; the node copies the value (one more reference).
__declspec(naked) void __fastcall CacheInsertEqual(Cache*, void*, Cache::Node** /*result*/, const CacheValue*)
    EI_ORIGINAL(0x004FBD80)

// Rewritten loader members called through their addresses so their checks see these calls too.
__declspec(naked) void __fastcall LoadFigPartAt(CLinkFigureLoader*, void*, CLinkMesh*, const stlp::string*, const char**,
                                                float, float, float) EI_ORIGINAL(0x004F7F00)
__declspec(naked) void __fastcall LoadOldPartAt(CLinkFigureLoader*, void*, CLinkMesh*, const stlp::string*, const char**,
                                                float, float, float) EI_ORIGINAL(0x004F87D0)
__declspec(naked) void __fastcall ReadLinkStringAt(stlp::string*, const char**) EI_ORIGINAL(0x004DB9C0)
__declspec(naked) int __fastcall LoadArchivePartAt(CLinkFigureLoader*, void*, CLinkMesh*, const stlp::string*, float,
                                                   float, float) EI_ORIGINAL(0x004F7D20)
__declspec(naked) CLinkMesh* __fastcall LoadLinkFileAt(CLinkFigureLoader*, void*, const stlp::string*, float, float,
                                                       float) EI_ORIGINAL(0x004F77C0)
__declspec(naked) CLinkMesh* __fastcall MakeFigureAt(CLinkFigureLoader*, void*, const stlp::string*,
                                                     const stlp::vector<stlp::string>*, float, float, float)
    EI_ORIGINAL(0x004F7B10)

// A part record at `data` into `mesh`: FIG / FIG8 or the older kind.
void LoadPartRecord(CLinkFigureLoader* loader, CLinkMesh* mesh, const stlp::string* name, const char* data, float s,
                    float t, float u) {
    std::uint32_t tag;
    std::memcpy(&tag, data, 4);
    const char* cursor = data;
    if ((tag & 0xFFFFFF) == kFigTag)
        LoadFigPartAt(loader, nullptr, mesh, name, &cursor, s, t, u);
    else
        LoadOldPartAt(loader, nullptr, mesh, name, &cursor, s, t, u);
}

// The data of `name` in the first open archive that has it, or null.
const char* FindInArchives(const LinkArchiveNode* archives, const char* name) {
    for (const LinkArchiveNode* n = archives->next; n != archives; n = n->next)
        if (void* data = n->resource->GetFile(name, nullptr)) return static_cast<const char*>(data);
    return nullptr;
}

// Whether `mesh`'s coefficients are close enough to (s, t, u) to share it.
bool SameMorph(const CLinkMesh* mesh, float s, float t, float u) {
    float ds = mesh->morph_[0] - s;
    float dt = mesh->morph_[1] - t;
    float du = mesh->morph_[2] - u;
    return !(ds * ds + dt * dt + du * du >= kSameMorph);
}

// The first cached mesh named `name` with close coefficients, or null.
CLinkMesh* FindCached(const Cache& cache, const stlp::string* name, float s, float t, float u) {
    Cache::Node* last = cache.upper_bound(*name);
    for (Cache::Node* n = cache.lower_bound(*name); n != last; n = Cache::Next(n))
        if (SameMorph(n->second, s, t, u)) return n->second;
    return nullptr;
}

// Adds `mesh` to the cache under `name` (the node's reference).
void AddToCache(Cache* cache, const stlp::string* name, CLinkMesh* mesh) {
    CacheValue value{stlp::string(*name), mesh};
    if (mesh) ++mesh->refCount_;
    Cache::Node* inserted;
    CacheInsertEqual(cache, nullptr, &inserted, &value);
    if (mesh) --mesh->refCount_;   // the value's reference; the node keeps one
}

// A reference dropped: the mesh deletes itself at zero.
void ReleaseMesh(CLinkMesh* mesh) {
    if (mesh && --mesh->refCount_ == 0) mesh->DeleteThis(1);
}

// The cache's nodes below and including `node`, right subtrees first (_M_erase): each value's
// reference dropped, its name freed, the node returned to the pool.
void EraseNodes(Cache::Node* node) {
    while (node) {
        EraseNodes(node->right);
        Cache::Node* left = node->left;
        ReleaseMesh(node->second);
        node->first.~string();
        stlp::deallocate(node, sizeof(Cache::Node));
        node = left;
    }
}

} // namespace

int CLinkFigureLoader::CacheSize() { return static_cast<int>(cache_.size()); }

int CLinkFigureLoader::CacheBytes() {
    int bytes = 0;
    for (Cache::Node* n = cache_.begin(); n != cache_.end(); n = Cache::Next(n)) bytes += n->second->MemoryBytes();
    return bytes;
}

CLinkMesh* CLinkFigureLoader::LoadFigure(const stlp::string* name, const stlp::vector<stlp::string>* parts, float s,
                                         float t, float u, bool fresh) {
    CLinkMesh* mesh = fresh ? nullptr : FindCached(cache_, name, s, t, u);
    if (!mesh) return MakeFigureAt(this, nullptr, name, parts, s, t, u);
    LinkFigureSet* set;
    {
        stlp::string meshName;
        mesh->CopyName(&meshName);
        set = FindFigureSetAt(this, nullptr, &meshName);
    }
    for (const stlp::string* part = parts->begin(); part != parts->end(); ++part) {
        if (mesh->HasPart(part)) continue;
        const float ms = mesh->morph_[0], mt = mesh->morph_[1], mu = mesh->morph_[2];
        if (!set) {
            LoadArchivePartAt(this, nullptr, mesh, part, ms, mt, mu);
        } else if (const char* data = static_cast<const char*>(set->model->GetFile(part->data(), nullptr))) {
            LoadPartRecord(this, mesh, part, data, ms, mt, mu);
        }
    }
    return mesh;
}

CLinkMesh* CLinkFigureLoader::LoadLink(const stlp::string* name, float s, float t, float u, bool fresh) {
    CLinkMesh* mesh = fresh ? nullptr : FindCached(cache_, name, s, t, u);
    return mesh ? mesh : LoadLinkFileAt(this, nullptr, name, s, t, u);
}

void CLinkFigureLoader::ClearCache() {
    if (cache_.size() == 0) return;
    EraseNodes(cache_.end()->parent);
    cache_.ResetToEmpty();
}

bool CLinkFigureLoader::LoadPartInto(CLinkMesh* mesh, const stlp::string* name) {
    if (mesh->HasPart(name)) return true;
    LinkFigureSet* set;
    {
        stlp::string meshName;
        mesh->CopyName(&meshName);
        set = FindFigureSetAt(this, nullptr, &meshName);
    }
    const float s = mesh->morph_[0], t = mesh->morph_[1], u = mesh->morph_[2];
    if (!set) return LoadArchivePartAt(this, nullptr, mesh, name, s, t, u) != 0;
    const char* data = static_cast<const char*>(set->model->GetFile(name->data(), nullptr));
    if (!data) return false;
    LoadPartRecord(this, mesh, name, data, s, t, u);
    return true;
}

CLinkMesh* CLinkFigureLoader::Remorph(CLinkMesh* mesh, float s, float t, float u) {
    if (mesh && mesh->refCount_ > 2) {
        stlp::string meshName;
        mesh->CopyName(&meshName);
        return LoadLink(&meshName, s, t, u, false);
    }
    if (!mesh) return nullptr;
    stlp::vector<stlp::string> names;
    mesh->CollectPartNames(&names);
    mesh->SetMorph(s, t, u);
    LinkFigureSet* set;
    {
        stlp::string meshName;
        mesh->CopyName(&meshName);
        set = FindFigureSetAt(this, nullptr, &meshName);
    }
    for (stlp::string* part = names.begin(); part != names.end(); ++part)
        RemorphPartAt(this, nullptr, mesh, set, part, s, t, u);
    names.clear();
    names.FreeStorage();
    return mesh;
}

CLinkMesh* CLinkFigureLoader::LoadLinkFile(const stlp::string* name, float s, float t, float u) {
    void* memory = gamecrt::operator_new(sizeof(CLinkMesh));
    CLinkMesh* mesh = memory ? MeshConstruct(memory, nullptr, this, name, s, t, u) : nullptr;
    LinkFigureSet* set = FindFigureSetAt(this, nullptr, name);
    const char* data;
    if (set) {
        data = static_cast<const char*>(set->model->GetFile(name->data(), nullptr));
    } else {
        stlp::string file(*name);
        file.Append(".lnk", ".lnk" + 4);
        data = FindInArchives(archives_, file.data());
    }
    if (!data) {
        game::FatalError("Can't find link file for \"%s\"\n", name->data());
        return nullptr;
    }
    std::uint32_t count;
    std::memcpy(&count, data, 4);
    const char* cursor = data + 4;
    stlp::string part;
    stlp::string::Reserve(part, 8);
    stlp::string unused;
    stlp::string::Reserve(unused, 8);
    for (; count != 0; --count) {
        ReadLinkStringAt(&part, &cursor);
        ReadLinkStringAt(&unused, &cursor);
        if (!set) {
            LoadArchivePartAt(this, nullptr, mesh, &part, s, t, u);
        } else if (const char* record = static_cast<const char*>(set->model->GetFile(part.data(), nullptr))) {
            LoadPartRecord(this, mesh, &part, record, s, t, u);
        }
    }
    AddToCache(&cache_, name, mesh);
    return mesh;
}

CLinkMesh* CLinkFigureLoader::MakeFigure(const stlp::string* name, const stlp::vector<stlp::string>* parts, float s,
                                         float t, float u) {
    void* memory = gamecrt::operator_new(sizeof(CLinkMesh));
    CLinkMesh* mesh = memory ? MeshConstruct(memory, nullptr, this, name, s, t, u) : nullptr;
    LinkFigureSet* set = FindFigureSetAt(this, nullptr, name);
    for (const stlp::string* part = parts->begin(); part != parts->end(); ++part) {
        if (!set) {
            LoadArchivePartAt(this, nullptr, mesh, part, s, t, u);
        } else if (const char* data = static_cast<const char*>(set->model->GetFile(part->data(), nullptr))) {
            LoadPartRecord(this, mesh, part, data, s, t, u);
        }
    }
    AddToCache(&cache_, name, mesh);
    return mesh;
}

int CLinkFigureLoader::LoadArchivePart(CLinkMesh* mesh, const stlp::string* name, float s, float t, float u) {
    const char* data;
    {
        stlp::string file;
        mesh->CopyName(&file);
        file.Append(name->data(), name->data() + name->size());
        file.Append(".fig", ".fig" + 4);
        data = FindInArchives(archives_, file.data());
    }
    if (!data) return 0;
    LoadPartRecord(this, mesh, name, data, s, t, u);
    return 1;
}

LinkFigureSet* CLinkFigureLoader::FindFigureSet(const stlp::string* name) {
    if (auto* found = figureSets_.Find(*name)) return found->value;
    // "<name>" plus each suffix in a 512-byte buffer, as the original builds them (strcpy / strcat,
    // no length check).
    char path[512];
    const char* files[3];
    const char* const suffixes[3] = {".mod", ".bon", ".anm"};
    const std::size_t length = std::strlen(name->data());
    for (int i = 0; i < 3; ++i) {
        std::memcpy(path, name->data(), length);
        std::memcpy(path + length, suffixes[i], 5);
        files[i] = FindInArchives(archives_, path);
    }
    if (!files[0]) return nullptr;
    void* memory = gamecrt::operator_new(sizeof(LinkFigureSet));
    auto* set = static_cast<LinkFigureSet*>(memory ? FigureModelConstructOriginal(memory) : nullptr);
    figureSets_[*name] = set;
    // The original indexes the map a second time (resize for one more element included).
    FigureModelLoadOriginal(figureSets_[*name], nullptr, files[0], files[1], files[2]);
    auto* found = figureSets_.Find(*name);
    return found ? found->value : nullptr;
}

namespace {
// The archive list's clear (004FBCF0): each node's name freed and the node returned to the pool,
// the sentinel linked to itself.
void ClearArchives(LinkArchiveNode* head) {
    for (LinkArchiveNode* n = head->next; n != head;) {
        LinkArchiveNode* next = n->next;
        n->name.~string();
        stlp::deallocate(n, sizeof(LinkArchiveNode));
        n = next;
    }
    head->next = head;
    head->prev = head;
}
} // namespace

void CLinkFigureLoader::Destruct() {
    ei::SetVtable(this, kVtableAddress);
    for (LinkArchiveNode* n = archives_->next; n != archives_; n = n->next) {
        n->resource->CloseResource();
        if (n->resource) delete n->resource;
    }
    ClearArchives(archives_);
    for (auto* node = figureSets_.First(); node; node = figureSets_.After(node)) {
        if (LinkFigureSet* set = node->value) {
            FigureModelDestructOriginal(set);
            gamecrt::operator_delete(set);
        }
    }
    if (CTextureDevice* device = renderer_) {
        renderer_ = nullptr;
        SurfaceHolderReleaseOriginal(device);
    }
    CLinkFigureLoader::ClearCache();
    stlp::deallocate(cache_.end(), sizeof(Cache::Node));
    // The original releases renderer_ here once more; it is null by now.
    figureSets_.Destroy();
    ClearArchives(archives_);
    stlp::deallocate(archives_, sizeof(LinkArchiveNode));
    ei::SetVtable(this, CLinkMesh::kRootVtableAddress);
}

void* CLinkFigureLoader::DeleteThis(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}

EI_REPLACE_METHOD(0x004F6BD0, CLinkFigureLoader::Destruct);
bool CLinkFigureLoader::RemorphPart(CLinkMesh* mesh, LinkFigureSet* set, const stlp::string* name, float s, float t,
                                    float u) {
    const char* data;
    if (set) {
        data = static_cast<const char*>(set->model->GetFile(name->data(), nullptr));
    } else {
        stlp::string file;
        mesh->CopyName(&file);
        file.Append(name->data(), name->data() + name->size());
        file.Append(".fig", ".fig" + 4);
        data = FindInArchives(archives_, file.data());
    }
    if (!data) return false;
    std::uint32_t tag;
    std::memcpy(&tag, data, 4);
    const char* cursor = data;
    if ((tag & 0xFFFFFF) == kFigTag)
        RemorphFigPartAt(this, nullptr, mesh, name, &cursor, s, t, u);
    else
        RemorphOldPartAt(this, nullptr, mesh, name, &cursor, s, t, u);
    return true;
}

EI_REPLACE_METHOD(0x004F9E50, CLinkFigureLoader::RemorphPart);
EI_REPLACE_METHOD(0x004F77C0, CLinkFigureLoader::LoadLinkFile);
EI_REPLACE_METHOD(0x004FAB00, CLinkFigureLoader::FindFigureSet);
EI_REPLACE_METHOD(0x004F7B10, CLinkFigureLoader::MakeFigure);
EI_REPLACE_METHOD(0x004F7480, CLinkFigureLoader::LoadPartInto);
EI_REPLACE_METHOD(0x004F7D20, CLinkFigureLoader::LoadArchivePart);

// The virtual slots, registered through shims (rewrite guide rule 4).
namespace {
CLinkMesh* __fastcall LoadFigureHook(CLinkFigureLoader* self, void*, const stlp::string* name,
                                     const stlp::vector<stlp::string>* parts, float s, float t, float u, bool fresh) {
    return self->CLinkFigureLoader::LoadFigure(name, parts, s, t, u, fresh);
}
CLinkMesh* __fastcall LoadLinkHook(CLinkFigureLoader* self, void*, const stlp::string* name, float s, float t, float u,
                                   bool fresh) {
    return self->CLinkFigureLoader::LoadLink(name, s, t, u, fresh);
}
void __fastcall ClearCacheHook(CLinkFigureLoader* self) { self->CLinkFigureLoader::ClearCache(); }
int __fastcall CacheBytesHook(CLinkFigureLoader* self) { return self->CLinkFigureLoader::CacheBytes(); }
void* __fastcall LoaderDeleteThisHook(CLinkFigureLoader* self, void*, unsigned flags) {
    return self->CLinkFigureLoader::DeleteThis(flags);
}
CLinkMesh* __fastcall RemorphHook(CLinkFigureLoader* self, void*, CLinkMesh* mesh, float s, float t, float u) {
    return self->CLinkFigureLoader::Remorph(mesh, s, t, u);
}
} // namespace
EI_REPLACE(0x004F7150, LoadFigureHook);
EI_REPLACE(0x004F7080, LoadLinkHook);
EI_REPLACE(0x004F7020, ClearCacheHook);
EI_REPLACE(0x004FAAA0, CacheBytesHook);
EI_REPLACE(0x004F6A30, LoaderDeleteThisHook);
EI_REPLACE(0x004F75E0, RemorphHook);
