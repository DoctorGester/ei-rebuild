// The link figure loader's .lnk reading and mesh cache. See link_figure.h and LinkFigureLoader in model.h.
#include "core_refcount.h"
#include "link_figure.h"

#include "model.h"
#include "resource.h"          // MappedResource
#include "texture.h"           // TextureDevice

#include "lib/bytes.h"
#include "lib/crt.h"
#include "lib/nan_audit.h"

#include <cstdint>
#include <cstring>
#include <list>
#include <map>
#include <memory>
#include <new>
#include <vector>

std::optional<std::string> ReadLinkString(const char** cursor) {
    std::uint32_t length;
    lib::LoadBytes(length, *cursor);
    *cursor += 4;
    if (length == 0) return std::nullopt;
    const char* text = *cursor;
    const void* zero = std::memchr(text, 0, length);
    *cursor += length;
    return std::string(text, zero ? static_cast<const char*>(zero) : text + length);
}

namespace {

constexpr std::uint32_t kFigTag = 0x474946;   // "FIG": a FIG / FIG8 record, else the older one
constexpr double kSameMorph = 1e-5;           // squared distance of the coefficients

// A part record at `data` into `mesh`: FIG / FIG8 or the older kind.
void LoadPartRecord(LinkFigureLoader* loader, LinkMesh* mesh, const std::string* name, const char* data, float s,
                    float t, float u) {
    std::uint32_t tag;
    lib::LoadBytes(tag, data);
    const char* cursor = data;
    if ((tag & 0xFFFFFF) == kFigTag)
        loader->LoadFigPart(mesh, name, &cursor, s, t, u);
    else
        loader->LoadOldPart(mesh, name, &cursor, s, t, u);
}

// The bytes of file `name` in `archive`, or null.
const char* FileData(MappedResource* archive, const char* name) {
    return static_cast<const char*>(archive->GetFile(name, nullptr));
}

// The data of `name` in the first open archive that has it, or null.
const char* FindInArchives(const std::list<LinkArchive>& archives, const char* name) {
    for (const LinkArchive& archive : archives)
        if (const char* data = FileData(archive.resource, name)) return data;
    return nullptr;
}

// Whether `mesh`'s coefficients are close enough to (s, t, u) to share it.
bool SameMorph(const LinkMesh* mesh, float s, float t, float u) {
    float ds = mesh->morphWeights[0] - s;
    float dt = mesh->morphWeights[1] - t;
    float du = mesh->morphWeights[2] - u;
    const float distance = ds * ds + dt * dt + du * du;
    EI_NAN_AUDIT(distance);
    return distance < kSameMorph;
}

// The first cached mesh named `name` with close coefficients, or null.
LinkMesh* FindCached(const std::multimap<std::string, LinkMesh*>& cache, const std::string* name, float s, float t, float u) {
    auto [first, last] = cache.equal_range(*name);
    for (auto n = first; n != last; ++n)
        if (SameMorph(n->second, s, t, u)) return n->second;
    return nullptr;
}

// Adds `mesh` to the cache under `name`, after the entries of the same name; the entry holds a reference.
void AddToCache(std::multimap<std::string, LinkMesh*>* cache, const std::string* name, LinkMesh* mesh) {
    if (mesh) mesh->AddRef();
    cache->emplace(*name, mesh);
}

// A reference dropped: the mesh deletes itself at zero.
void ReleaseMesh(LinkMesh* mesh) {
    if (mesh) mesh->Release();
}


} // namespace

int LinkFigureLoader::CacheSize() { return static_cast<int>(cache.size()); }

int LinkFigureLoader::CacheBytes() {
    int bytes = 0;
    for (const auto& entry : cache) bytes += entry.second->MemoryBytes();
    return bytes;
}

LinkMesh* LinkFigureLoader::LoadFigure(const std::string* name, const std::vector<std::string>* parts, float s,
                                     float t, float u) {
    LinkMesh* mesh = FindCached(cache, name, s, t, u);
    if (!mesh) return MakeFigure(name, parts, s, t, u);
    FigureModel* set;
    {
        std::string meshName = mesh->name;
        set = FindFigureSet(&meshName);
    }
    for (const std::string* part = parts->data(); part != parts->data() + parts->size(); ++part) {
        if (mesh->HasPart(part)) continue;
        const float ms = mesh->morphWeights[0], mt = mesh->morphWeights[1], mu = mesh->morphWeights[2];
        if (!set) {
            LoadArchivePart(mesh, part, ms, mt, mu);
        } else if (const char* data = FileData(set->mesh, part->data())) {
            LoadPartRecord(this, mesh, part, data, ms, mt, mu);
        }
    }
    return mesh;
}

LinkMesh* LinkFigureLoader::LoadLink(const std::string* name, float s, float t, float u) {
    LinkMesh* mesh = FindCached(cache, name, s, t, u);
    return mesh ? mesh : LoadLinkFile(name, s, t, u);
}

// Each entry's reference dropped, last key first (the order the tree's erase freed its nodes in), then the
// entries erased.
void LinkFigureLoader::ClearCache() {
    for (auto entry = cache.rbegin(); entry != cache.rend(); ++entry) ReleaseMesh(entry->second);
    cache.clear();
}

bool LinkFigureLoader::LoadPartInto(LinkMesh* mesh, const std::string* name) {
    if (mesh->HasPart(name)) return true;
    FigureModel* set;
    {
        std::string meshName = mesh->name;
        set = FindFigureSet(&meshName);
    }
    const float s = mesh->morphWeights[0], t = mesh->morphWeights[1], u = mesh->morphWeights[2];
    if (!set) return LoadArchivePart(mesh, name, s, t, u) != 0;
    const char* data = FileData(set->mesh, name->data());
    if (!data) return false;
    LoadPartRecord(this, mesh, name, data, s, t, u);
    return true;
}

LinkMesh* LinkFigureLoader::Remorph(LinkMesh* mesh, float s, float t, float u) {
    if (mesh && mesh->refCount > 2) {
        std::string meshName = mesh->name;
        return LoadLink(&meshName, s, t, u);
    }
    if (!mesh) return nullptr;
    std::vector<std::string> names = mesh->PartNames();
    mesh->SetMorph(s, t, u);
    FigureModel* set;
    {
        std::string meshName = mesh->name;
        set = FindFigureSet(&meshName);
    }
    for (std::string* part = names.data(); part != names.data() + names.size(); ++part)
        RemorphPart(mesh, set, part, s, t, u);
    return mesh;
}

LinkMesh* LinkFigureLoader::LoadLinkFile(const std::string* name, float s, float t, float u) {
    auto* mesh = new LinkMesh(this, name, s, t, u);
    FigureModel* set = FindFigureSet(name);
    const char* data;
    if (set) {
        data = FileData(set->mesh, name->data());
    } else {
        std::string file(*name);
        file.append(".lnk", ".lnk" + 4);
        data = FindInArchives(archives, file.data());
    }
    if (!data) {
        game::FatalError("Can't find link file for \"%s\"\n", name->data());
        return nullptr;
    }
    std::uint32_t count;
    lib::LoadBytes(count, data);
    const char* cursor = data + 4;
    std::string part;
    for (; count != 0; --count) {
        if (std::optional<std::string> read = ReadLinkString(&cursor)) part = std::move(*read);
        ReadLinkString(&cursor);   // unused
        if (!set) {
            LoadArchivePart(mesh, &part, s, t, u);
        } else if (const char* record = FileData(set->mesh, part.data())) {
            LoadPartRecord(this, mesh, &part, record, s, t, u);
        }
    }
    AddToCache(&cache, name, mesh);
    return mesh;
}

LinkMesh* LinkFigureLoader::MakeFigure(const std::string* name, const std::vector<std::string>* parts, float s,
                                         float t, float u) {
    auto* mesh = new LinkMesh(this, name, s, t, u);
    FigureModel* set = FindFigureSet(name);
    for (const std::string* part = parts->data(); part != parts->data() + parts->size(); ++part) {
        if (!set) {
            LoadArchivePart(mesh, part, s, t, u);
        } else if (const char* data = FileData(set->mesh, part->data())) {
            LoadPartRecord(this, mesh, part, data, s, t, u);
        }
    }
    AddToCache(&cache, name, mesh);
    return mesh;
}

int LinkFigureLoader::LoadArchivePart(LinkMesh* mesh, const std::string* name, float s, float t, float u) {
    const char* data;
    {
        std::string file = mesh->name;
        file.append(name->data(), name->data() + name->size());
        file.append(".fig", ".fig" + 4);
        data = FindInArchives(archives, file.data());
    }
    if (!data) return 0;
    LoadPartRecord(this, mesh, name, data, s, t, u);
    return 1;
}

FigureModel* LinkFigureLoader::FindFigureSet(const std::string* name) {
    if (auto found = figureSets.find(*name); found != figureSets.end()) return found->second;
    // "<name>" plus each suffix in a 512-byte buffer (strcpy / strcat, no length check).
    char path[512];
    const char* files[3];
    const char* const suffixes[3] = {".mod", ".bon", ".anm"};
    const std::size_t length = std::strlen(name->data());
    for (int i = 0; i < 3; ++i) {
        std::memcpy(path, name->data(), length);
        std::memcpy(path + length, suffixes[i], 5);
        files[i] = FindInArchives(archives, path);
    }
    if (!files[0]) return nullptr;
    auto* set = new FigureModel();
    figureSets[*name] = set;
    // The map is indexed a second time (resize for one more element included).
    figureSets[*name]->Open(files[0], files[1], files[2]);
    auto found = figureSets.find(*name);
    return found != figureSets.end() ? found->second : nullptr;
}

LinkFigureLoader::~LinkFigureLoader() {
    for (LinkArchive& archive : archives) {
        archive.resource->CloseResource();
        if (archive.resource) delete archive.resource;
    }
    for (auto& [name, set] : figureSets) {
        delete set;
    }
    renderer.Reset();
    LinkFigureLoader::ClearCache();
}

bool LinkFigureLoader::RemorphPart(LinkMesh* mesh, FigureModel* set, const std::string* name, float s, float t,
                                    float u) {
    const char* data;
    if (set) {
        data = FileData(set->mesh, name->data());
    } else {
        std::string file = mesh->name;
        file.append(name->data(), name->data() + name->size());
        file.append(".fig", ".fig" + 4);
        data = FindInArchives(archives, file.data());
    }
    if (!data) return false;
    std::uint32_t tag;
    lib::LoadBytes(tag, data);
    const char* cursor = data;
    if ((tag & 0xFFFFFF) == kFigTag)
        RemorphFigPart(mesh, name, &cursor, s, t, u);
    else
        RemorphOldPart(mesh, name, &cursor, s, t, u);
    return true;
}
