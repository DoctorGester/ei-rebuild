// The figure manager's loaders: NewFigure, LoadPart, LoadBoneAnimation. See figure_manager.h.
#include "figure_manager.h"

#include "engine_object.h"   // EngineObject
#include "figure_mesh.h"
#include "game_strings.h"
#include "map_object_animation.h"
#include "map_object_figures.h"
#include "mod_folders.h"
#include "model.h"
#include "registry.h"
#include "sky_object.h"
#include "ui.h"   // UiMesh
#include "world_data.h"
#include "lib/bytes.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/rect.h"
#include "lib/str.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <string>

namespace {

constexpr const char* kFiguresArchive = "figures.res";
constexpr const char* kMenusArchive = "menus.res";

// Copies `text` with its terminator to `end`; returns the new end (at the terminator).
char* Append(char* end, const char* text) {
    std::size_t length = std::strlen(text);
    std::memcpy(end, text, length + 1);
    return end + length;
}

} // namespace

void FigureManager::Create() {
    BaseManager::Create();
    // Rebirth's ResOverride: the mod's figure archives come first.
    if (ei::rebirth::Enabled(ei::rebirth::Patch::ResOverride)) {
        for (const std::string& path : modfolders::FigureArchives()) AddArchive(path);
    }
    std::string folder = g_textFolder;
    AddArchive((folder + kFiguresArchive));
    AddArchive((folder + kMenusArchive));
    LoadMenuRects(g_menuRects);
}

FigureMesh* FigureManager::NewPart(const std::string& modelName, std::string_view partName, std::uint32_t attached) {
    auto* part = CreateObjectAs<FigureMesh>(attached ? FigureObjectType::PlainFigureMesh : FigureObjectType::SharedFigureMesh, -1);
    part->name = partName;
    part->directory = modelName;
    return part;
}

FigureMesh* FigureManager::NewBlankFigure() { return CreateObjectAs<FigureMesh>(FigureObjectType::UiMesh, -1); }

FigureMesh* FigureManager::NewFigure(std::string_view name) {
    auto* figure = NewBlankFigure();
    figure->name = name;
    figure->directory = game::kEmptyText;
    const std::string path = std::string(name) + ".fig";
    void* data = GetFile(path, nullptr);
    if (!data) {
        ReleaseObject(figure);
        return nullptr;
    }
    char* cursor = static_cast<char*>(data);
    figure->LoadFromMemory(&cursor);
    figure->box.bounds.flags = 0;
    CloseFile(data);
    return figure;
}

int FigureManager::LoadBoneAnimation(FigureBone* bone, AnimationTrack* animator, const char* animation) {
    if (!animation) return 0;
    char path[0x200];
    char* end = Append(path, bone->name3.c_str());
    end = Append(end, bone->GetName());
    end = Append(end, animation);
    Append(end, game::AstExtensionText());
    void* data = GetFile(path, nullptr);
    if (!data) return 0;
    auto* cursor = static_cast<std::uint8_t*>(data);
    animator->LoadKeyFrames(&cursor);
    return 1;
}

// --- Class name and id ------------------------------------------------------------------------------

namespace {
constexpr const char* kCFigureManagerClassName = "CFigureManager";
} // namespace

const char* FigureManager::ClassName() { return kCFigureManagerClassName; }
ClassId FigureManager::GetClassId() { return ClassId::FigureManager; }
const char* FigureManager::GetFolderName() { return "figures"; }

// --- Figure animation --------------------------------------------------------------------------------

// Sets a figure's animation by name (figure manager). Without an animation name the figure's animation is
// cleared (and the clear's result returned). Otherwise the model is looked up and its animation part asked
// for the name; each failure logs "Can't open model" / "Can't open animation ... for model" and returns 0; a
// found animation goes to the figure (its result returned). See docs/notes/models.md.

namespace {

} // namespace

int FigureManager::SetFigureAnimation(FigureAnimationState* figure, const char* modelName,
                                       const FigureAnimation* animation) {
    if (!animation) return figure->ClearBoneTracks();
    std::string key(modelName);
    FigureModel* model = FindModel(&key);
    if (!model) {
        game::DebugLog("Can't open model \"%s\"\n", modelName);
        return 0;
    }
    std::string name(animation->name);
    MappedResource* found = model->FindAnimation(&name);
    if (!found) {
        game::DebugLog("Can't open animation \"%s\" for model \"%s\"\n", animation->name, modelName);
        return 0;
    }
    return figure->LoadBoneTracks(found, animation);
}

// --- Figure links ------------------------------------------------------------------------------------

// Links a figure's pieces from the skeleton file of the model `name`. With the model cached, its mesh archive
// reads the links (GetFile, freed by CloseFile); without it the manager opens "<name>.lnk" (its GetFile /
// CloseFile); with neither the fatal error "Can't load skeleton file" is returned. The data is a count and
// that many (child, parent) length-prefixed names (read into strings), each passed to the animation state's
// LinkPiece with the model; FindRoot finishes it. Returns 1. See docs/notes/models.md.

namespace {

} // namespace

int FigureManager::LoadSkeletonLinks(FigureAnimationState* state, std::string_view name) {
    FigureModel* model;
    {
        std::string key(name);
        model = FindModel(&key);
    }

    std::uint32_t* data;
    if (model) {
        data = static_cast<std::uint32_t*>(model->mesh->GetFile(name, nullptr));
    } else {
        std::string base(name);
        std::string path = base + ".lnk";
        data = static_cast<std::uint32_t*>(GetFile(path, nullptr));
    }
    if (!data) return game::FatalError("Can't load skeleton file for \"%s\"\n", std::string(name));

    const std::uint8_t* cursor = reinterpret_cast<const std::uint8_t*>(data + 1);
    {
        std::string child, parent;
        for (std::uint32_t i = data[0]; i != 0; --i) {
            ReadPrefixedName(&child, &cursor);
            ReadPrefixedName(&parent, &cursor);
            state->LinkPiece(model, child, parent);
        }
        state->FindRoot();
        if (model) model->mesh->CloseFile(data);
        else CloseFile(data);
    }
    return 1;
}

// A bone's cage. The data comes from the cached model's bones archive (GetFile with the bone's name and the
// size out) or from the file "<bone's model name><bone's name>.bon" (the manager's GetFile with the size
// out); none returns 0. 0x48 bytes go to one bone loader, other sizes to another, as {data, size}. The data
// is not released here. Returns 1.
int FigureManager::LoadFigureBones(FigureModel* model, FigureBone* bone) {
    struct {
        void* data;
        std::uint32_t size;
    } bones{nullptr, 0};
    if (model) {
        bones.data = model->bones->GetFile(bone->name, &bones.size);
    } else {
        std::string name = bone->GetName();
        std::string directory = bone->name3;
        std::string path = (directory + name) + ".bon";
        bones.data = GetFile(path, &bones.size);
    }
    if (!bones.data) return 0;
    // The bone's cage interpolation: 0x48-byte data (a prism of six points), or
    // any other size (a cube of eight); the stream is the data pointer, advanced as it is read.
    auto** stream = reinterpret_cast<const float**>(static_cast<void*>(&bones.data));
    if (bones.size == 0x48) bone->InterpolatePrism(stream);
    else bone->InterpolateCube(stream);
    return 1;
}

// --- Figure load bounds ------------------------------------------------------------------------------

// A figure part's bounding volume. The part data comes from the cached model's mesh archive (GetFile with
// `name`, freed by CloseFile) or from the file "<directory><name>.fig" (the manager's GetFile / CloseFile);
// missing data returns 0. A "FIG" header (the low three bytes of the first dword) sends the data past the
// 0x28-byte header to the FIG bounds morph with the full magic, any other past 8 bytes to the older-format
// one, both with the morph weights. Returns 1. See figure_mesh.h and docs/notes/models.md.

namespace {

constexpr std::uint32_t kFigMagic = 0x474946;   // "FIG"

} // namespace

int FigureManager::LoadFigureBounds(ClipBounds* out, FigureModel* model, std::string_view directory, std::string_view name, std::uint32_t s, std::uint32_t t, std::uint32_t u) {
    std::uint8_t* data;
    if (model) {
        data = static_cast<std::uint8_t*>(model->mesh->GetFile(name, nullptr));
    } else {
        // The strcpy / strcat into a 0x200-byte buffer (no bounds check, like the original).
        char path[0x200];
        std::size_t length = directory.size();
        std::memcpy(path, directory.data(), length);
        std::size_t nameLength = name.size();
        std::memcpy(path + length, name.data(), nameLength);
        std::memcpy(path + length + nameLength, ".fig", 5);
        data = static_cast<std::uint8_t*>(GetFile(std::string_view(path, length + nameLength + 4), nullptr));
    }
    if (!data) return 0;
    std::uint32_t magic;
    lib::LoadBytes(magic, data);
    if ((magic & 0xFFFFFF) == kFigMagic)
        MorphFigureBounds(out, reinterpret_cast<const float*>(data + 0x28), std::bit_cast<float>(s), std::bit_cast<float>(t),
                          std::bit_cast<float>(u), magic);
    else
        MorphFigureBounds6(out, reinterpret_cast<const float*>(data + 8), std::bit_cast<float>(s), std::bit_cast<float>(t),
                           std::bit_cast<float>(u));
    if (model) model->mesh->CloseFile(data);
    else CloseFile(data);
    return 1;
}

// --- Figure model destroy ----------------------------------------------------------------------------

// The figure model's destructor (FigureModel, model.h). Every cached animation is deleted, the map cleared,
// each archive closed (CloseResource) and deleted, then the map destroyed. See docs/notes/models.md.

FigureModel::~FigureModel() {
    std::unordered_map<std::string, MappedResource*>& table = animationCache;
    for (auto& [name, animation] : table)
        if (animation) delete animation;
    table.clear();

    static constexpr MappedResource* FigureModel::*kArchives[3] = {&FigureModel::mesh, &FigureModel::bones,
                                                                    &FigureModel::animations};
    for (MappedResource* FigureModel::*archive : kArchives) (this->*archive)->CloseResource();
    for (MappedResource* FigureModel::*archive : kArchives) delete (this->*archive);
}

// The figure manager's destructor: every cached model destroyed and freed, the base's clean-up, the model map
// cleared and its buckets freed (the map's inline destructor), then the base destructor.
FigureManager::~FigureManager() {
    for (auto& [name, model] : models) {
        delete model;
    }
    ResourceManager::Destroy();   // the base's clean-up
}

// --- Figure models -----------------------------------------------------------------------------------

// The model named `name`, loaded on first use. The models are cached in FigureManager::models (nodes {next,
// key, value}; the key hashed h = h * 5 + c). A missing model opens "<name>.mod", ".bon" and ".anm" through
// the manager's GetFile; without the .mod nothing is created and null is returned. Otherwise a FigureModel is
// stored under the name, its mesh, bones and animation archives load from the files (OpenFromMemory; the .bon
// and .anm only when they opened), and the cached entry is looked up again. See docs/notes/models.md.

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

// --- Menu rects --------------------------------------------------------------------------------------

// The main menu's and the escape menu's button rectangles from "menus.reg" (each entry
// four integers; a missing or malformed one is a fatal error). See docs/notes/world.md.

Rect g_menuRects[kMenuRects] = /*007C8048*/ {};

namespace {

// The entries in the order the original reads them: section, name, error, the RECT's index.
struct MenuRect {
    const char* section;
    const char* name;
    const char* error;
    int index;
};
constexpr MenuRect kRects[] = {
    {"MainMenu", "NewGame", "Can't get rect for MM NewGame\n", 0},
    {"MainMenu", "LoadGame", "Can't get rect for MM LoadGame\n", 1},
    {"MainMenu", "ExitGame", "Can't get rect for MM ExitGame\n", 2},
    {"MainMenu", "Options", "Can't get rect for MM Options\n", 3},
    {"MainMenu", "Credits", "Can't get rect for MM Credits\n", 4},
    {"MainMenu", "Multiplayer", "Can't get rect for MM Multiplayer\n", 5},
    {"EscMenu", "SaveGame", "Can't get rect for EM SaveGame\n", 6},
    {"EscMenu", "LoadGame", "Can't get rect for EM LoadGame\n", 7},
    {"EscMenu", "Options", "Can't get rect for EM Options\n", 10},
    {"EscMenu", "ResumeGame", "Can't get rect for EM ResumeGame\n", 8},
    {"EscMenu", "ExitMM", "Can't get rect for EM ExitMM\n", 9},
};

} // namespace

int FigureManager::LoadMenuRects(Rect* rects) {
    FileRegistry reg;
    // The manager's GetFile: the resource file's data.
    void* file = GetFile(game::menutexts::MenusReg(), nullptr);
    if (!file || !reg.Attach(file)) {
        return game::FatalError(file ? game::menutexts::CantParse() : game::menutexts::CantOpen());
    }
    std::vector<std::int32_t> values;
    for (const MenuRect& entry : kRects) {
        values.clear();
        reg.GetIntArray(entry.section, entry.name, &values);
        if (std::ssize(values) != 4) {
            int result = game::FatalError(entry.error);
            lib::FreeStorage(values);
            return result;
        }
        rects::Set(&rects[entry.index], values[0], values[1], values[2],
                         values[3]);
    }
    lib::FreeStorage(values);
    return 1;
}

// --- Figure factory ----------------------------------------------------------------------------------

// The figure object factory (virtual, `this` unused): creates a scene object of a class id. 0x41-0x43 are the
// figure part meshes built on the common base: 0x41 with two empty arrays (SharedFigureMesh), 0x42 with four
// more (PlainFigureMesh), 0x43 the same (UiMesh, ui.h); the original inlines their constructors. 0x44 and
// 0x300B-0x300D (AnimationTrack, FigureBone, FigureAnimationState) have their own. All are C++ classes,
// allocated at the game's sizes. The new object's type gets the class id; an unknown id or a failed
// allocation returns null. See docs/notes/models.md.

namespace {

void* Create(int classId) {
    switch (static_cast<FigureObjectType>(classId)) {
    case FigureObjectType::SharedFigureMesh:
        return new SharedFigureMesh();
    case FigureObjectType::PlainFigureMesh:
        return new PlainFigureMesh();
    case FigureObjectType::UiMesh:
        return new UiMesh();
    case FigureObjectType::SkyObject:
        return new SkyObject();
    case FigureObjectType::AnimationTrack:
        return new AnimationTrack();
    case FigureObjectType::FigureBone:
        return new FigureBone();
    case FigureObjectType::AnimationState:
        return new FigureAnimationState();
    default:
        return nullptr;
    }
}

} // namespace

void* CreateFigureObject(int classId) {
    void* object = Create(classId);
    if (!object) return nullptr;
    static_cast<EngineObject*>(object)->type = classId;
    return object;
}
