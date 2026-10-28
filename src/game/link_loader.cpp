// The link figure loader's .lnk reading and mesh cache. See link_figure.h and CLinkFigureLoader in model.h.
#include "core_refcount.h"
#include "link_figure.h"

#include "model.h"
#include "resource.h"          // CMappedResource
#include "texture.h"           // CTextureDevice

#include "lib/crt.h"
#include "lib/nan_audit.h"

#include <cstdint>
#include <cstring>
#include <list>
#include <map>
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

namespace {

using Cache = std::multimap<std::string, CLinkMesh*>;

constexpr std::uint32_t kFigTag = 0x474946;   // "FIG": a FIG / FIG8 record, else the older one
constexpr double kSameMorph = 1e-5;           // squared distance of the coefficients

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
const char* FindInArchives(const std::list<LinkArchive>& archives, const char* name) {
    for (const LinkArchive& archive : archives)
        if (const char* data = FileData(archive.resource, name)) return data;
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
    auto [first, last] = cache.equal_range(*name);
    for (auto n = first; n != last; ++n)
        if (SameMorph(n->second, s, t, u)) return n->second;
    return nullptr;
}

// Adds `mesh` to the cache under `name`, after the entries of the same name; the entry holds a reference.
void AddToCache(Cache* cache, const std::string* name, CLinkMesh* mesh) {
    if (mesh) ++mesh->refCount_;
    cache->emplace(*name, mesh);
}

// A reference dropped: the mesh deletes itself at zero.
void ReleaseMesh(CLinkMesh* mesh) {
    if (mesh && --mesh->refCount_ == 0) delete mesh;
}


} // namespace

int CLinkFigureLoader::CacheSize() { return static_cast<int>(cache_.size()); }

int CLinkFigureLoader::CacheBytes() {
    int bytes = 0;
    for (const auto& entry : cache_) bytes += entry.second->MemoryBytes();
    return bytes;
}

CLinkMesh* CLinkFigureLoader::LoadFigure(const std::string* name, const std::vector<std::string>* parts, float s,
                                     float t, float u, bool fresh) {
    CLinkMesh* mesh = fresh ? nullptr : FindCached(cache_, name, s, t, u);
    if (!mesh) return MakeFigure(name, parts, s, t, u);
    FigureModel* set;
    {
        std::string meshName = mesh->name_;
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

// Each entry's reference dropped, last key first (the order the tree's erase freed its nodes in), then the
// entries erased.
void CLinkFigureLoader::ClearCache() {
    for (auto entry = cache_.rbegin(); entry != cache_.rend(); ++entry) ReleaseMesh(entry->second);
    cache_.clear();
}

bool CLinkFigureLoader::LoadPartInto(CLinkMesh* mesh, const std::string* name) {
    if (mesh->HasPart(name)) return true;
    FigureModel* set;
    {
        std::string meshName = mesh->name_;
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
        std::string meshName = mesh->name_;
        return LoadLink(&meshName, s, t, u, false);
    }
    if (!mesh) return nullptr;
    std::vector<std::string> names;
    mesh->CollectPartNames(&names);
    mesh->SetMorph(s, t, u);
    FigureModel* set;
    {
        std::string meshName = mesh->name_;
        set = FindFigureSet(&meshName);
    }
    for (std::string* part = names.data(); part != names.data() + names.size(); ++part)
        RemorphPart(mesh, set, part, s, t, u);
    return mesh;
}

CLinkMesh* CLinkFigureLoader::LoadLinkFile(const std::string* name, float s, float t, float u) {
    auto* mesh = new CLinkMesh(this, name, s, t, u);
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
    auto* mesh = new CLinkMesh(this, name, s, t, u);
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
        std::string file = mesh->name_;
        file.append(name->data(), name->data() + name->size());
        file.append(".fig", ".fig" + 4);
        data = FindInArchives(archives_, file.data());
    }
    if (!data) return 0;
    LoadPartRecord(this, mesh, name, data, s, t, u);
    return 1;
}

FigureModel* CLinkFigureLoader::FindFigureSet(const std::string* name) {
    if (auto found = figureSets_.find(*name); found != figureSets_.end()) return found->second;
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
    auto* set = new FigureModel();
    figureSets_[*name] = set;
    // The map is indexed a second time (resize for one more element included).
    figureSets_[*name]->Open(files[0], files[1], files[2]);
    auto found = figureSets_.find(*name);
    return found != figureSets_.end() ? found->second : nullptr;
}

CLinkFigureLoader::~CLinkFigureLoader() {
    for (LinkArchive& archive : archives_) {
        archive.resource->CloseResource();
        if (archive.resource) delete archive.resource;
    }
    for (auto& [name, set] : figureSets_) {
        delete set;
    }
    if (CTextureDevice* device = renderer_) {
        renderer_ = nullptr;
        device->Release();
    }
    CLinkFigureLoader::ClearCache();
}

bool CLinkFigureLoader::RemorphPart(CLinkMesh* mesh, FigureModel* set, const std::string* name, float s, float t,
                                    float u) {
    const char* data;
    if (set) {
        data = FileData(set->mesh, name->data());
    } else {
        std::string file = mesh->name_;
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
