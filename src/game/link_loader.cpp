// The link figure loader's .lnk reading and mesh cache. See link_figure.h and CLinkFigureLoader in model.h.
#include "core_refcount.h"
#include "link_figure.h"

#include "model.h"
#include "resource.h"          // CMappedResource
#include "texture.h"           // CTextureDevice

#include "lib/crt.h"
#include "library_trees.h"
#include "lib/nan_audit.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <vector>

void ReadLinkString(std::string* out, const char** cursor) {
    std::uint32_t length;
    std::memcpy(&length, *cursor, 4);
    *cursor += 4;
    if (length == 0) return;
    const char* text = *cursor;
    const void* zero = std::memchr(text, 0, length);
    out->assign(text, zero ? static_cast<const char*>(zero) : text + length);
    *cursor += length;
}

std::string CLinkMesh::CopyName() const { return name_; }

namespace {

using Cache = stlp::map<std::string, CLinkMesh*>;

constexpr std::uint32_t kFigTag = 0x474946;   // "FIG": a FIG / FIG8 record, else the older one
constexpr double kSameMorph = 1e-5;           // squared distance of the coefficients

// The cache's node values: the name, then one more reference on the mesh (as
// lib_trees::MeshCache's counted pointer copies it).
struct CachedMesh {
    static void Copy(CLinkMesh** to, CLinkMesh* const& from) {
        *to = from;
        if (from) ++from->refCount_;
    }
};
using CacheElement = stlp::PairElement<Cache::Value, CachedMesh>;

// A part record at `data` into `mesh`: FIG / FIG8 or the older kind.
void LoadPartRecord(CLinkFigureLoader* loader, CLinkMesh* mesh, const std::string* name, const char* data, float s,
                    float t, float u) {
    std::uint32_t tag;
    std::memcpy(&tag, data, 4);
    const char* cursor = data;
    if ((tag & 0xFFFFFF) == kFigTag)
        loader->LoadFigPart(mesh, name, &cursor, s, t, u);
    else
        loader->LoadOldPart(mesh, name, &cursor, s, t, u);
}

// The bytes of file `name` in `archive`, or null.
const char* FileData(CMappedResource* archive, const char* name) {
    return static_cast<const char*>(archive->GetFile(name, nullptr));
}

// The data of `name` in the first open archive that has it, or null.
const char* FindInArchives(const LinkArchiveNode* archives, const char* name) {
    for (const LinkArchiveNode* n = archives->next; n != archives; n = n->next)
        if (const char* data = FileData(n->resource, name)) return data;
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
CLinkMesh* FindCached(const Cache& cache, const std::string* name, float s, float t, float u) {
    Cache::Node* last = cache.upper_bound(*name);
    for (Cache::Node* n = cache.lower_bound(*name); n != last; n = Cache::Next(n))
        if (SameMorph(n->second, s, t, u)) return n->second;
    return nullptr;
}

// Adds `mesh` to the cache under `name` (the node's reference).
void AddToCache(Cache* cache, const std::string* name, CLinkMesh* mesh) {
    Cache::Value value{std::string(*name), mesh};
    if (mesh) ++mesh->refCount_;
    cache->InsertEqual<CacheElement>(value);   // multimap insert_equal
    if (mesh) --mesh->refCount_;   // the value's reference; the node keeps one
}

// A reference dropped: the mesh deletes itself at zero.
void ReleaseMesh(CLinkMesh* mesh) {
    if (mesh && --mesh->refCount_ == 0) delete mesh;
}

// The cache's nodes below and including `node`, right subtrees first (_M_erase): each value's
// reference dropped, its name freed, the node returned to the pool.
void EraseNodes(Cache::Node* node) {
    while (node) {
        EraseNodes(node->right);
        Cache::Node* left = node->left;
        ReleaseMesh(node->second);
        std::destroy_at(&node->first);
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

CLinkMesh* CLinkFigureLoader::LoadFigure(const std::string* name, const std::vector<std::string>* parts, float s,
                                     float t, float u, bool fresh) {
    CLinkMesh* mesh = fresh ? nullptr : FindCached(cache_, name, s, t, u);
    if (!mesh) return MakeFigure(name, parts, s, t, u);
    FigureModel* set;
    {
        std::string meshName = mesh->CopyName();
        set = FindFigureSet(&meshName);
    }
    for (const std::string* part = parts->data(); part != parts->data() + parts->size(); ++part) {
        if (mesh->HasPart(part)) continue;
        const float ms = mesh->morphWeights_[0], mt = mesh->morphWeights_[1], mu = mesh->morphWeights_[2];
        if (!set) {
            LoadArchivePart(mesh, part, ms, mt, mu);
        } else if (const char* data = FileData(set->mesh, part->data())) {
            LoadPartRecord(this, mesh, part, data, ms, mt, mu);
        }
    }
    return mesh;
}

CLinkMesh* CLinkFigureLoader::LoadLink(const std::string* name, float s, float t, float u, bool fresh) {
    CLinkMesh* mesh = fresh ? nullptr : FindCached(cache_, name, s, t, u);
    return mesh ? mesh : LoadLinkFile(name, s, t, u);
}

void CLinkFigureLoader::ClearCache() {
    if (cache_.size() == 0) return;
    EraseNodes(cache_.end()->parent);
    cache_.ResetToEmpty();
}

bool CLinkFigureLoader::LoadPartInto(CLinkMesh* mesh, const std::string* name) {
    if (mesh->HasPart(name)) return true;
    FigureModel* set;
    {
        std::string meshName = mesh->CopyName();
        set = FindFigureSet(&meshName);
    }
    const float s = mesh->morphWeights_[0], t = mesh->morphWeights_[1], u = mesh->morphWeights_[2];
    if (!set) return LoadArchivePart(mesh, name, s, t, u) != 0;
    const char* data = FileData(set->mesh, name->data());
    if (!data) return false;
    LoadPartRecord(this, mesh, name, data, s, t, u);
    return true;
}

CLinkMesh* CLinkFigureLoader::Remorph(CLinkMesh* mesh, float s, float t, float u) {
    if (mesh && mesh->refCount_ > 2) {
        std::string meshName = mesh->CopyName();
        return LoadLink(&meshName, s, t, u, false);
    }
    if (!mesh) return nullptr;
    std::vector<std::string> names;
    mesh->CollectPartNames(&names);
    mesh->SetMorph(s, t, u);
    FigureModel* set;
    {
        std::string meshName = mesh->CopyName();
        set = FindFigureSet(&meshName);
    }
    for (std::string* part = names.data(); part != names.data() + names.size(); ++part)
        RemorphPart(mesh, set, part, s, t, u);
    return mesh;
}

CLinkMesh* CLinkFigureLoader::LoadLinkFile(const std::string* name, float s, float t, float u) {
    CLinkMesh* memory = gamecrt::operator_new_as<CLinkMesh>();
    CLinkMesh* mesh = memory ? new (memory) CLinkMesh(this, name, s, t, u) : nullptr;
    FigureModel* set = FindFigureSet(name);
    const char* data;
    if (set) {
        data = FileData(set->mesh, name->data());
    } else {
        std::string file(*name);
        file.append(".lnk", ".lnk" + 4);
        data = FindInArchives(archives_, file.data());
    }
    if (!data) {
        game::FatalError("Can't find link file for \"%s\"\n", name->data());
        return nullptr;
    }
    std::uint32_t count;
    std::memcpy(&count, data, 4);
    const char* cursor = data + 4;
    std::string part;
    std::string unused;
    for (; count != 0; --count) {
        ReadLinkString(&part, &cursor);
        ReadLinkString(&unused, &cursor);
        if (!set) {
            LoadArchivePart(mesh, &part, s, t, u);
        } else if (const char* record = FileData(set->mesh, part.data())) {
            LoadPartRecord(this, mesh, &part, record, s, t, u);
        }
    }
    AddToCache(&cache_, name, mesh);
    return mesh;
}

CLinkMesh* CLinkFigureLoader::MakeFigure(const std::string* name, const std::vector<std::string>* parts, float s,
                                         float t, float u) {
    CLinkMesh* memory = gamecrt::operator_new_as<CLinkMesh>();
    CLinkMesh* mesh = memory ? new (memory) CLinkMesh(this, name, s, t, u) : nullptr;
    FigureModel* set = FindFigureSet(name);
    for (const std::string* part = parts->data(); part != parts->data() + parts->size(); ++part) {
        if (!set) {
            LoadArchivePart(mesh, part, s, t, u);
        } else if (const char* data = FileData(set->mesh, part->data())) {
            LoadPartRecord(this, mesh, part, data, s, t, u);
        }
    }
    AddToCache(&cache_, name, mesh);
    return mesh;
}

int CLinkFigureLoader::LoadArchivePart(CLinkMesh* mesh, const std::string* name, float s, float t, float u) {
    const char* data;
    {
        std::string file = mesh->CopyName();
        file.append(name->data(), name->data() + name->size());
        file.append(".fig", ".fig" + 4);
        data = FindInArchives(archives_, file.data());
    }
    if (!data) return 0;
    LoadPartRecord(this, mesh, name, data, s, t, u);
    return 1;
}

FigureModel* CLinkFigureLoader::FindFigureSet(const std::string* name) {
    if (auto* found = figureSets_.Find(*name)) return found->value;
    // "<name>" plus each suffix in a 512-byte buffer (strcpy / strcat, no length check).
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
    FigureModel* memory = gamecrt::operator_new_as<FigureModel>();
    auto* set = memory ? ConstructFigureModel(memory) : nullptr;
    figureSets_[*name] = set;
    // The map is indexed a second time (resize for one more element included).
    figureSets_[*name]->Open(files[0], files[1], files[2]);
    auto* found = figureSets_.Find(*name);
    return found ? found->value : nullptr;
}

namespace {
// The archive list's clear: each node's name freed and the node returned to the pool,
// the sentinel linked to itself.
void ClearArchives(LinkArchiveNode* head) {
    for (LinkArchiveNode* n = head->next; n != head;) {
        LinkArchiveNode* next = n->next;
        std::destroy_at(&n->name);
        stlp::deallocate(n, sizeof(LinkArchiveNode));
        n = next;
    }
    head->next = head;
    head->prev = head;
}
} // namespace

CLinkFigureLoader::~CLinkFigureLoader() {
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
        device->Release();
    }
    CLinkFigureLoader::ClearCache();
    stlp::deallocate(cache_.end(), sizeof(Cache::Node));
    // renderer_ is released here once more, like the original; it is null by now.
    figureSets_.Destroy();
    ClearArchives(archives_);
    stlp::deallocate(archives_, sizeof(LinkArchiveNode));
}

bool CLinkFigureLoader::RemorphPart(CLinkMesh* mesh, FigureModel* set, const std::string* name, float s, float t,
                                    float u) {
    const char* data;
    if (set) {
        data = FileData(set->mesh, name->data());
    } else {
        std::string file = mesh->CopyName();
        file.append(name->data(), name->data() + name->size());
        file.append(".fig", ".fig" + 4);
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
