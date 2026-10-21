// The link figure loader's .lnk reading and mesh cache (A-S7): 004DB9C0, 004F7020-004F7D20. See
// link_figure.h and CLinkFigureLoader in model.h.
#include "link_figure.h"

#include "model.h"
#include "originals_world.h"
#include "resource.h"          // CMappedResource
#include "texture.h"           // CTextureDevice

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "library_trees.h"
#include "lib/nan_audit.h"

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

stlp::string* CLinkMesh::CopyName(stlp::string* out) const { return new (out) stlp::string(name_); }

namespace {

using Cache = stlp::map<stlp::string, CLinkMesh*>;

constexpr std::uint32_t kFigTag = 0x474946;   // "FIG": a FIG / FIG8 record, else the older one
constexpr double kSameMorph = 1e-5;           // 0073B860: squared distance of the coefficients

// The cache's value_type as insert_equal takes it: the name and a counted reference.
struct CacheValue {
    stlp::string name;
    CLinkMesh* mesh;
};

// 004FBD80: multimap insert_equal; the node copies the value (one more reference): lib_trees::MeshCache's.
void CacheInsertEqual(Cache* cache, const CacheValue* value) {
    lib_trees::MeshCache::Node* inserted;
    lib_trees::MeshCache::InsertEqual(reinterpret_cast<lib_trees::MeshCache::Tree*>(cache), nullptr, &inserted,
                                      reinterpret_cast<const lib_trees::MeshCache::Value*>(value));
}

// A part record at `data` into `mesh`: FIG / FIG8 or the older kind.
void LoadPartRecord(CLinkFigureLoader* loader, CLinkMesh* mesh, const stlp::string* name, const char* data, float s,
                    float t, float u) {
    std::uint32_t tag;
    std::memcpy(&tag, data, 4);
    const char* cursor = data;
    if ((tag & 0xFFFFFF) == kFigTag)
        loader->LoadFigPart(mesh, name, &cursor, s, t, u);
    else
        loader->LoadOldPart(mesh, name, &cursor, s, t, u);
}

// The data of `name` in the first open archive that has it, or null.
const char* FindInArchives(const LinkArchiveNode* archives, const char* name) {
    for (const LinkArchiveNode* n = archives->next; n != archives; n = n->next)
        if (void* data = n->resource->GetFile(name, nullptr)) return static_cast<const char*>(data);
    return nullptr;
}

// Whether `mesh`'s coefficients are close enough to (s, t, u) to share it.
bool SameMorph(const CLinkMesh* mesh, float s, float t, float u) {
    float ds = mesh->morphWeights_[0] - s;
    float dt = mesh->morphWeights_[1] - t;
    float du = mesh->morphWeights_[2] - u;
    const float distance = ds * ds + dt * dt + du * du;
    EI_NAN_AUDIT(distance);
    return distance < kSameMorph;
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
    CacheInsertEqual(cache, &value);
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

CLinkMesh* CLinkFigureLoader::LoadFigureImpl(const stlp::string* name, const stlp::vector<stlp::string>* parts, float s,
                                         float t, float u, bool fresh) {
    CLinkMesh* mesh = fresh ? nullptr : FindCached(cache_, name, s, t, u);
    if (!mesh) return MakeFigure(name, parts, s, t, u);
    FigureModel* set;
    {
        stlp::string meshName;
        mesh->CopyName(&meshName);
        set = FindFigureSet(&meshName);
    }
    for (const stlp::string* part = parts->begin(); part != parts->end(); ++part) {
        if (mesh->HasPart(part)) continue;
        const float ms = mesh->morphWeights_[0], mt = mesh->morphWeights_[1], mu = mesh->morphWeights_[2];
        if (!set) {
            LoadArchivePart(mesh, part, ms, mt, mu);
        } else if (const char* data = static_cast<const char*>(set->mesh->GetFile(part->data(), nullptr))) {
            LoadPartRecord(this, mesh, part, data, ms, mt, mu);
        }
    }
    return mesh;
}

CLinkMesh* CLinkFigureLoader::LoadLinkImpl(const stlp::string* name, float s, float t, float u, bool fresh) {
    CLinkMesh* mesh = fresh ? nullptr : FindCached(cache_, name, s, t, u);
    return mesh ? mesh : LoadLinkFile(name, s, t, u);
}

void CLinkFigureLoader::ClearCache() {
    if (cache_.size() == 0) return;
    EraseNodes(cache_.end()->parent);
    cache_.ResetToEmpty();
}

bool CLinkFigureLoader::LoadPartInto(CLinkMesh* mesh, const stlp::string* name) {
    if (mesh->HasPart(name)) return true;
    FigureModel* set;
    {
        stlp::string meshName;
        mesh->CopyName(&meshName);
        set = FindFigureSet(&meshName);
    }
    const float s = mesh->morphWeights_[0], t = mesh->morphWeights_[1], u = mesh->morphWeights_[2];
    if (!set) return LoadArchivePart(mesh, name, s, t, u) != 0;
    const char* data = static_cast<const char*>(set->mesh->GetFile(name->data(), nullptr));
    if (!data) return false;
    LoadPartRecord(this, mesh, name, data, s, t, u);
    return true;
}

CLinkMesh* CLinkFigureLoader::RemorphImpl(CLinkMesh* mesh, float s, float t, float u) {
    if (mesh && mesh->refCount_ > 2) {
        stlp::string meshName;
        mesh->CopyName(&meshName);
        return LoadLink(&meshName, s, t, u, false);
    }
    if (!mesh) return nullptr;
    stlp::vector<stlp::string> names;
    mesh->CollectPartNames(&names);
    mesh->SetMorph(s, t, u);
    FigureModel* set;
    {
        stlp::string meshName;
        mesh->CopyName(&meshName);
        set = FindFigureSet(&meshName);
    }
    for (stlp::string* part = names.begin(); part != names.end(); ++part)
        RemorphPart(mesh, set, part, s, t, u);
    names.clear();
    names.FreeStorage();
    return mesh;
}

CLinkMesh* CLinkFigureLoader::LoadLinkFile(const stlp::string* name, float s, float t, float u) {
    void* memory = gamecrt::operator_new(sizeof(CLinkMesh));
    CLinkMesh* mesh = memory ? static_cast<CLinkMesh*>(memory)->Construct(this, name, s, t, u) : nullptr;
    FigureModel* set = FindFigureSet(name);
    const char* data;
    if (set) {
        data = static_cast<const char*>(set->mesh->GetFile(name->data(), nullptr));
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
        ReadLinkString(&part, &cursor);
        ReadLinkString(&unused, &cursor);
        if (!set) {
            LoadArchivePart(mesh, &part, s, t, u);
        } else if (const char* record = static_cast<const char*>(set->mesh->GetFile(part.data(), nullptr))) {
            LoadPartRecord(this, mesh, &part, record, s, t, u);
        }
    }
    AddToCache(&cache_, name, mesh);
    return mesh;
}

CLinkMesh* CLinkFigureLoader::MakeFigure(const stlp::string* name, const stlp::vector<stlp::string>* parts, float s,
                                         float t, float u) {
    void* memory = gamecrt::operator_new(sizeof(CLinkMesh));
    CLinkMesh* mesh = memory ? static_cast<CLinkMesh*>(memory)->Construct(this, name, s, t, u) : nullptr;
    FigureModel* set = FindFigureSet(name);
    for (const stlp::string* part = parts->begin(); part != parts->end(); ++part) {
        if (!set) {
            LoadArchivePart(mesh, part, s, t, u);
        } else if (const char* data = static_cast<const char*>(set->mesh->GetFile(part->data(), nullptr))) {
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

FigureModel* CLinkFigureLoader::FindFigureSet(const stlp::string* name) {
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
    void* memory = gamecrt::operator_new(sizeof(FigureModel));
    auto* set = static_cast<FigureModel*>(memory ? ConstructFigureModel(static_cast<FigureModel*>(memory)) : nullptr);
    figureSets_[*name] = set;
    // The original indexes the map a second time (resize for one more element included).
    static_cast<FigureModel*>(figureSets_[*name])->Open(files[0], files[1], files[2]);
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
        if (FigureModel* set = node->value) {
            DestroyFigureModel(set);
            gamecrt::operator_delete(set);
        }
    }
    if (CTextureDevice* device = renderer_) {
        renderer_ = nullptr;
        device->Release();   // 004E6BA0
    }
    CLinkFigureLoader::ClearCache();
    stlp::deallocate(cache_.end(), sizeof(Cache::Node));
    // The original releases renderer_ here once more; it is null by now.
    figureSets_.Destroy();
    ClearArchives(archives_);
    stlp::deallocate(archives_, sizeof(LinkArchiveNode));
    ei::SetVtable(this, CLinkMesh::kRootVtableAddress);
}

void* CLinkFigureLoader::DeleteThisImpl(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}

bool CLinkFigureLoader::RemorphPart(CLinkMesh* mesh, FigureModel* set, const stlp::string* name, float s, float t,
                                    float u) {
    const char* data;
    if (set) {
        data = static_cast<const char*>(set->mesh->GetFile(name->data(), nullptr));
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
        RemorphFigPart(mesh, name, &cursor, s, t, u);
    else
        RemorphOldPart(mesh, name, &cursor, s, t, u);
    return true;
}

// The virtual slots, registered through shims (rewrite guide rule 4).
namespace {
void __fastcall ClearCacheHook(CLinkFigureLoader* self) { self->CLinkFigureLoader::ClearCache(); }
int __fastcall CacheBytesHook(CLinkFigureLoader* self) { return self->CLinkFigureLoader::CacheBytes(); }
} // namespace
EI_REPLACE_METHOD(0x004F7150, CLinkFigureLoader::LoadFigureImpl);
EI_REPLACE_METHOD(0x004F7080, CLinkFigureLoader::LoadLinkImpl);
EI_REPLACE(0x004F7020, ClearCacheHook);
EI_REPLACE(0x004FAAA0, CacheBytesHook);
EI_REPLACE_METHOD(0x004F6A30, CLinkFigureLoader::DeleteThisImpl);
EI_REPLACE_METHOD(0x004F75E0, CLinkFigureLoader::RemorphImpl);
