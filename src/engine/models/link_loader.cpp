// The link figure loader's archive list, .lnk reading and mesh cache, and its constructor (the client
// world's setup news it with the device object and the game's folder). See link_loader.h, link_figure.h and
// docs/notes/renderer.md (section 12).
#include "engine/models/link_loader.h"

#include "engine/core/core_refcount.h"
#include "engine/data/resource.h"   // MappedResource, MMIOResource
#include "engine/models/link_figure.h"   // LinkMesh: the cache's entries
#include "engine/mods/mod_folders.h"
#include "engine/scene/device.h"   // TextureDevice

#include "lib/bytes.h"
#include "lib/crt.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <new>
#include <string>
#include <vector>

std::optional<std::string> ReadLinkString(const char** cursor) {
    std::uint32_t length = lib::Load<std::uint32_t>(*cursor);
    *cursor += 4;
    if (length == 0) return std::nullopt;
    const char* text = *cursor;
    *cursor += length;
    return std::string(text, std::find(text, text + length, '\0'));
}

namespace {

constexpr float kSameMorph = 1e-5f;           // squared distance of the coefficients

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

// Whether `mesh`'s coefficients are close enough to `morph` to share it.
bool SameMorph(const LinkMesh* mesh, Morph morph) {
    float ds = mesh->morph.s - morph.s;
    float dt = mesh->morph.t - morph.t;
    float du = mesh->morph.u - morph.u;
    const float distance = ds * ds + dt * dt + du * du;
    return distance < kSameMorph;
}

// The first cached mesh named `name` with close coefficients, or null.
LinkMesh* FindCached(const std::multimap<std::string, HeldRef<LinkMesh>, std::less<>>& cache, std::string_view name,
                     Morph morph) {
    auto [first, last] = cache.equal_range(name);
    for (auto n = first; n != last; ++n)
        if (SameMorph(n->second, morph)) return n->second;
    return nullptr;
}

} // namespace

LinkMesh* LinkFigureLoader::LoadFigure(std::string_view name, const std::vector<std::string>& parts, Morph morph) {
    LinkMesh* mesh = FindCached(cache, name, morph);
    if (!mesh) return MakeFigure(name, parts, morph);
    FigureModel* set = FindFigureSet(mesh->name);
    for (const std::string& part : parts)
        if (!mesh->HasPart(part)) LoadPart(mesh, set, part, mesh->morph);
    return mesh;
}

LinkMesh* LinkFigureLoader::LoadLink(std::string_view name, Morph morph) {
    LinkMesh* mesh = FindCached(cache, name, morph);
    return mesh ? mesh : LoadLinkFile(name, morph);
}

void LinkFigureLoader::ClearCache() { cache.clear(); }

bool LinkFigureLoader::LoadPartInto(LinkMesh* mesh, const std::string& name) {
    if (mesh->HasPart(name)) return true;
    return LoadPart(mesh, FindFigureSet(mesh->name), name, mesh->morph);
}

LinkMesh* LinkFigureLoader::Remorph(LinkMesh* mesh, Morph morph) {
    if (mesh && mesh->refCount > 2) return LoadLink(mesh->name, morph);
    if (!mesh) return nullptr;
    std::vector<std::string> names = mesh->PartNames();
    mesh->morph = morph;
    FigureModel* set = FindFigureSet(mesh->name);
    for (const std::string& part : names) RemorphPart(mesh, set, part, morph);
    return mesh;
}

LinkMesh* LinkFigureLoader::LoadLinkFile(std::string_view name, Morph morph) {
    FigureModel* set = FindFigureSet(name);
    const char* data;
    if (set) {
        data = FileData(set->mesh, name);
    } else {
        data = FindInArchives(archives, std::string(name) + ".lnk");
    }
    if (!data) game::FatalExit("Can't find link file for \"%s\"\n", std::string(name));
    auto* mesh = new LinkMesh(this, name, morph);
    std::uint32_t count = lib::Load<std::uint32_t>(data);
    const char* cursor = data + 4;
    std::string part;
    for (; count != 0; --count) {
        if (std::optional<std::string> read = ReadLinkString(&cursor)) part = std::move(*read);
        ReadLinkString(&cursor);   // unused
        LoadPart(mesh, set, part, morph);
    }
    cache.emplace(name, mesh);
    return mesh;
}

LinkMesh* LinkFigureLoader::MakeFigure(std::string_view name, const std::vector<std::string>& parts, Morph morph) {
    auto* mesh = new LinkMesh(this, name, morph);
    FigureModel* set = FindFigureSet(name);
    for (const std::string& part : parts) LoadPart(mesh, set, part, morph);
    cache.emplace(name, mesh);
    return mesh;
}

const char* LinkFigureLoader::FindPartData(const LinkMesh* mesh, FigureModel* set, const std::string& name) {
    return set ? FileData(set->mesh, name) : FindInArchives(archives, mesh->name + name + ".fig");
}

bool LinkFigureLoader::LoadPart(LinkMesh* mesh, FigureModel* set, const std::string& name, Morph morph) {
    const char* data = FindPartData(mesh, set, name);
    if (!data) return false;
    const char* cursor = data;
    if (fig::IsFig(lib::Load<std::uint32_t>(data)))
        LoadFigPart(mesh, name, &cursor, morph);
    else
        LoadOldPart(mesh, name, &cursor, morph);
    return true;
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

bool LinkFigureLoader::RemorphPart(LinkMesh* mesh, FigureModel* set, const std::string& name, Morph morph) {
    const char* data = FindPartData(mesh, set, name);
    if (!data) return false;
    const char* cursor = data;
    if (fig::IsFig(lib::Load<std::uint32_t>(data)))
        RemorphFigPart(mesh, name, &cursor, morph);
    else
        RemorphOldPart(mesh, name, &cursor, morph);
    return true;
}

int LinkFigureLoader::AddArchive(const std::string& path) {
    for (const LinkArchive& archive : archives)
        if (archive.name == path) return 1;

    LinkArchive& archive = archives.emplace_back(path);
    MappedResource* resource = new MMIOResource();
    archive.resource = resource;
    if (resource->OpenResource(path)) return 1;

    // Failure: drop the archive again (the resource object is leaked) and log. (The original passed STLport's
    // string object by value to "%s": its first word, the text pointer, is what printed.)
    archives.pop_back();
    game::DebugLog("Can't open resource file \"%s\"\n", path);
    return 0;
}

// --- The constructor ---

// The device object counted, then the figure archives of `folder` opened (a failure is only logged).
LinkFigureLoader::LinkFigureLoader(TextureDevice* device, const std::string& folder) {
    renderer.Assign(device);
    for (const std::string& path : modfolders::FigureArchivePaths(folder)) AddArchive(path);
}
