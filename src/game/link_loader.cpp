// The link figure loader's .lnk reading and mesh cache. See link_figure.h and LinkFigureLoader in model.h.
#include "core_refcount.h"
#include "link_figure.h"

#include "model.h"
#include "resource.h"          // MappedResource
#include "texture.h"           // TextureDevice

#include "lib/bytes.h"
#include "lib/crt.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
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
    *cursor += length;
    return std::string(text, std::find(text, text + length, '\0'));
}

namespace {

constexpr float kSameMorph = 1e-5f;           // squared distance of the coefficients

// A part record at `data` into `mesh`: FIG / FIG8 or the older kind.
void LoadPartRecord(LinkFigureLoader* loader, LinkMesh* mesh, const std::string& name, const char* data, float s,
                    float t, float u) {
    std::uint32_t tag;
    lib::LoadBytes(tag, data);
    const char* cursor = data;
    if (fig::IsFig(tag))
        loader->LoadFigPart(mesh, name, &cursor, s, t, u);
    else
        loader->LoadOldPart(mesh, name, &cursor, s, t, u);
}

// The bytes of file `name` in `archive`, or null.
const char* FileData(MappedResource* archive, std::string_view name) {
    return static_cast<const char*>(archive->GetFile(name, nullptr));
}

// The data of `name` in the first open archive that has it, or null.
const char* FindInArchives(const std::vector<LinkArchive>& archives, std::string_view name) {
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
    return distance < kSameMorph;
}

// The first cached mesh named `name` with close coefficients, or null.
LinkMesh* FindCached(const std::multimap<std::string, HeldRef<LinkMesh>, std::less<>>& cache, std::string_view name,
                     float s, float t, float u) {
    auto [first, last] = cache.equal_range(name);
    for (auto n = first; n != last; ++n)
        if (SameMorph(n->second, s, t, u)) return n->second;
    return nullptr;
}


} // namespace

LinkMesh* LinkFigureLoader::LoadFigure(std::string_view name, const std::vector<std::string>& parts, float s, float t,
                                       float u) {
    LinkMesh* mesh = FindCached(cache, name, s, t, u);
    if (!mesh) return MakeFigure(name, parts, s, t, u);
    FigureModel* set = FindFigureSet(mesh->name);
    for (const std::string& part : parts) {
        if (mesh->HasPart(part)) continue;
        const float ms = mesh->morphWeights[0], mt = mesh->morphWeights[1], mu = mesh->morphWeights[2];
        if (!set) {
            LoadArchivePart(mesh, part, ms, mt, mu);
        } else if (const char* data = FileData(set->mesh, part)) {
            LoadPartRecord(this, mesh, part, data, ms, mt, mu);
        }
    }
    return mesh;
}

LinkMesh* LinkFigureLoader::LoadLink(std::string_view name, float s, float t, float u) {
    LinkMesh* mesh = FindCached(cache, name, s, t, u);
    return mesh ? mesh : LoadLinkFile(name, s, t, u);
}

void LinkFigureLoader::ClearCache() { cache.clear(); }

bool LinkFigureLoader::LoadPartInto(LinkMesh* mesh, const std::string& name) {
    if (mesh->HasPart(name)) return true;
    FigureModel* set = FindFigureSet(mesh->name);
    const float s = mesh->morphWeights[0], t = mesh->morphWeights[1], u = mesh->morphWeights[2];
    if (!set) return LoadArchivePart(mesh, name, s, t, u) != 0;
    const char* data = FileData(set->mesh, name);
    if (!data) return false;
    LoadPartRecord(this, mesh, name, data, s, t, u);
    return true;
}

LinkMesh* LinkFigureLoader::Remorph(LinkMesh* mesh, float s, float t, float u) {
    if (mesh && mesh->refCount > 2) return LoadLink(mesh->name, s, t, u);
    if (!mesh) return nullptr;
    std::vector<std::string> names = mesh->PartNames();
    mesh->SetMorph(s, t, u);
    FigureModel* set = FindFigureSet(mesh->name);
    for (const std::string& part : names) RemorphPart(mesh, set, part, s, t, u);
    return mesh;
}

LinkMesh* LinkFigureLoader::LoadLinkFile(std::string_view name, float s, float t, float u) {
    FigureModel* set = FindFigureSet(name);
    const char* data;
    if (set) {
        data = FileData(set->mesh, name);
    } else {
        data = FindInArchives(archives, std::string(name) + ".lnk");
    }
    if (!data) {
        game::FatalError("Can't find link file for \"%s\"\n", std::string(name));
        return nullptr;
    }
    auto* mesh = new LinkMesh(this, name, s, t, u);
    std::uint32_t count;
    lib::LoadBytes(count, data);
    const char* cursor = data + 4;
    std::string part;
    for (; count != 0; --count) {
        if (std::optional<std::string> read = ReadLinkString(&cursor)) part = std::move(*read);
        ReadLinkString(&cursor);   // unused
        if (!set) {
            LoadArchivePart(mesh, part, s, t, u);
        } else if (const char* record = FileData(set->mesh, part)) {
            LoadPartRecord(this, mesh, part, record, s, t, u);
        }
    }
    cache.emplace(name, mesh);
    return mesh;
}

LinkMesh* LinkFigureLoader::MakeFigure(std::string_view name, const std::vector<std::string>& parts, float s, float t,
                                       float u) {
    auto* mesh = new LinkMesh(this, name, s, t, u);
    FigureModel* set = FindFigureSet(name);
    for (const std::string& part : parts) {
        if (!set) {
            LoadArchivePart(mesh, part, s, t, u);
        } else if (const char* data = FileData(set->mesh, part)) {
            LoadPartRecord(this, mesh, part, data, s, t, u);
        }
    }
    cache.emplace(name, mesh);
    return mesh;
}

int LinkFigureLoader::LoadArchivePart(LinkMesh* mesh, const std::string& name, float s, float t, float u) {
    const char* data = FindInArchives(archives, mesh->name + name + ".fig");
    if (!data) return 0;
    LoadPartRecord(this, mesh, name, data, s, t, u);
    return 1;
}

FigureModel* LinkFigureLoader::FindFigureSet(std::string_view name) {
    if (auto found = figureSets.find(name); found != figureSets.end()) return found->second;
    FigureModel* set = OpenFigureModel(
        name, [this](const std::string& path) { return FindInArchives(archives, path); }, [](const void*) {});
    if (set) figureSets.emplace(name, set);
    return set;
}

LinkFigureLoader::~LinkFigureLoader() {
    for (LinkArchive& archive : archives) {
        if (!archive.resource) continue;
        archive.resource->CloseResource();
        delete archive.resource;
    }
    for (auto& [name, set] : figureSets) {
        delete set;
    }
    renderer.Reset();
    LinkFigureLoader::ClearCache();
}

bool LinkFigureLoader::RemorphPart(LinkMesh* mesh, FigureModel* set, const std::string& name, float s, float t,
                                    float u) {
    const char* data;
    if (set) {
        data = FileData(set->mesh, name);
    } else {
        data = FindInArchives(archives, mesh->name + name + ".fig");
    }
    if (!data) return false;
    std::uint32_t tag;
    lib::LoadBytes(tag, data);
    const char* cursor = data;
    if (fig::IsFig(tag))
        RemorphFigPart(mesh, name, &cursor, s, t, u);
    else
        RemorphOldPart(mesh, name, &cursor, s, t, u);
    return true;
}
