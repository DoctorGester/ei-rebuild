// The model named `name`, loaded on first use. The models are cached in FigureManager::models (nodes {next,
// key, value}; the key hashed h = h * 5 + c). A missing model opens "<name>.mod", ".bon" and ".anm" through
// the manager's GetFile; without the .mod nothing is created and null is returned. Otherwise a FigureModel is
// stored under the name, its mesh, bones and animation archives load from the files (OpenFromMemory; the .bon
// and .anm only when they opened), and the cached entry is looked up again. See docs/notes/models.md.
#include "figure_manager.h"

#include <cstdint>
#include <cstring>
#include <memory>

namespace {

void* OpenPart(FigureManager* manager, const std::string* name, const char* suffix) {
    char path[0x200];
    std::size_t length = std::strlen(name->data());
    std::memcpy(path, name->data(), length);
    const char* tail = suffix;
    std::memcpy(path + length, tail, std::strlen(tail) + 1);
    return manager->GetFile(path, nullptr);
}

FigureModel** ModelSlot(std::unordered_map<std::string, FigureModel*>* map, const std::string* name) { return &(*map)[*name]; }
MappedResource** AnimationSlot(std::unordered_map<std::string, MappedResource*>* map, const std::string* name) { return &(*map)[*name]; }

} // namespace

FigureModel* FigureManager::FindModel(const std::string* name) {
    std::unordered_map<std::string, FigureModel*>* map = &models;
    if (auto found = map->find(*name); found != map->end()) return found->second;

    void* mesh = OpenPart(this, name, ".mod");
    void* bones = OpenPart(this, name, ".bon");
    void* animation = OpenPart(this, name, ".anm");
    if (!mesh) return nullptr;

    auto* model = new FigureModel();
    *ModelSlot(map, name) = model;
    FigureModel* parts = *ModelSlot(map, name);
    parts->mesh->OpenFromMemory(mesh);
    if (bones) parts->bones->OpenFromMemory(bones);
    if (animation) parts->animations->OpenFromMemory(animation);
    auto found = map->find(*name);
    return found != map->end() ? found->second : nullptr;
}

// The model's animation of a name, loaded on first use from its animation archive (GetFile with the name) into
// a new archive over that data (OpenFromMemory); without data nothing is created. The cached entry is looked
// up again either way.
MappedResource* FigureModel::FindAnimation(const std::string* name) {
    std::unordered_map<std::string, MappedResource*>* map = &animationCache;
    if (auto found = map->find(*name); found != map->end()) return found->second;
    void* data = animations->GetFile(name->data(), nullptr);
    if (data) {
        MappedResource* animation = new MMIOResource();
        *AnimationSlot(map, name) = animation;
        (*AnimationSlot(map, name))->OpenFromMemory(data);
    }
    auto found = map->find(*name);
    return found != map->end() ? found->second : nullptr;
}

FigureModel::FigureModel() {
    mesh = new MMIOResource();
    bones = new MMIOResource();
    animations = new MMIOResource();
}

void FigureModel::Open(const void* mod, const void* bon, const void* anm) {
    if (mod) mesh->OpenFromMemory(mod);
    if (bon) bones->OpenFromMemory(bon);
    if (anm) animations->OpenFromMemory(anm);
}
