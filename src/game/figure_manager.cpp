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

void FigureManager::Create() {
    BaseManager::Create();
    for (const std::string& path : modfolders::FigureArchivePaths(g_textFolder)) AddArchive(path);
    LoadMenuRects(g_menuRects);
}

FigureMesh* FigureManager::NewPart(const std::string& modelName, std::string_view partName, std::uint32_t attached) {
    auto* part = CreateObjectAs<FigureMesh>(attached ? FigureObjectType::AttachedPart : FigureObjectType::LoosePart, -1);
    part->name = partName;
    part->directory = modelName;
    return part;
}

UiMesh* FigureManager::NewBlankFigure() { return CreateObjectAs<UiMesh>(FigureObjectType::UiMesh, -1); }

UiMesh* FigureManager::NewFigure(std::string_view name) {
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
    const std::string path = bone->modelName + bone->GetName() + animation + game::AstExtensionText();
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

// Sets a figure's animation by name. Without an animation the figure's animation is cleared; otherwise the
// model is looked up and its animation of that name found; each failure logs "Can't open model" / "Can't open
// animation ... for model" and returns 0. See docs/notes/models.md.
bool FigureManager::SetFigureAnimation(FigureAnimationState* figure, const char* modelName,
                                       const FigureAnimation* animation) {
    if (!animation) {
        figure->ClearBoneTracks();
        return true;
    }
    std::string key(modelName);
    FigureModel* model = FindModel(&key);
    if (!model) {
        game::DebugLog("Can't open model \"%s\"\n", modelName);
        return false;
    }
    std::string name(animation->name);
    MappedResource* found = model->FindAnimation(&name);
    if (!found) {
        game::DebugLog("Can't open animation \"%s\" for model \"%s\"\n", animation->name, modelName);
        return false;
    }
    figure->LoadBoneTracks(found, animation);
    return true;
}

// --- Figure links ------------------------------------------------------------------------------------

// Links a figure's pieces from the skeleton file of the model `name`. With the model cached, its mesh archive
// reads the links (GetFile, freed by CloseFile); without it the manager opens "<name>.lnk" (its GetFile /
// CloseFile); with neither the fatal error "Can't load skeleton file" is returned. The data is a count and
// that many (child, parent) length-prefixed names (read into strings), each passed to the animation state's
// LinkPiece with the model; FindRoot finishes it. Returns 1. See docs/notes/models.md.

int FigureManager::LoadSkeletonLinks(FigureAnimationState* state, std::string_view name) {
    FigureModel* model;
    {
        std::string key(name);
        model = FindModel(&key);
    }

    void* data;
    if (model) {
        data = model->mesh->GetFile(name, nullptr);
    } else {
        std::string base(name);
        std::string path = base + ".lnk";
        data = GetFile(path, nullptr);
    }
    if (!data) return game::FatalError("Can't load skeleton file for \"%s\"\n", std::string(name));

    // A count, then that many (child, parent) names.
    std::uint32_t links;
    lib::LoadBytes(links, data);
    const std::uint8_t* cursor = static_cast<const std::uint8_t*>(data) + sizeof(links);
    {
        std::string child, parent;
        for (std::uint32_t i = links; i != 0; --i) {
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
        std::string directory = bone->modelName;
        std::string path = (directory + name) + ".bon";
        bones.data = GetFile(path, &bones.size);
    }
    if (!bones.data) return 0;
    // The bone's cage: `offset` becomes the point at (morphS, morphT, morphU) in the prism of six points of 0x48-byte
    // data, or in the cube of eight points of any other size.
    if (bones.size == sizeof(float) * 3 * fig::kOldVersions) {
        float prism[3 * fig::kOldVersions];
        lib::LoadBytes(prism, bones.data);
        bone->offset = fig::MorphPrism<Vector3>(prism, bone->morphS, bone->morphT, bone->morphU);
    } else {
        float cube[3 * fig::kFig8Versions];
        lib::LoadBytes(cube, bones.data);
        bone->offset = fig::MorphCube<Vector3>(cube, bone->morphS, bone->morphT, bone->morphU);
    }
    return 1;
}

// --- Figure load bounds ------------------------------------------------------------------------------

// A figure part's bounding volume. The part data comes from the cached model's mesh archive (GetFile with
// `name`, freed by CloseFile) or from the file "<directory><name>.fig" (the manager's GetFile / CloseFile);
// missing data returns 0. A "FIG" header (the low three bytes of the first dword) sends the data past the
// 0x28-byte header to the FIG bounds morph with the full magic, any other past 8 bytes to the older-format
// one, both with the morph weights. Returns 1. See figure_mesh.h and docs/notes/models.md.
int FigureManager::LoadFigureBounds(ClipBounds* out, FigureModel* model, std::string_view directory, std::string_view name, float weightS, float weightT, float weightU) {
    std::uint8_t* data;
    if (model) {
        data = static_cast<std::uint8_t*>(model->mesh->GetFile(name, nullptr));
    } else {
        const std::string path = std::string(directory) + std::string(name) + ".fig";
        data = static_cast<std::uint8_t*>(GetFile(path, nullptr));
    }
    if (!data) return 0;
    std::uint32_t magic;
    lib::LoadBytes(magic, data);
    // The bounds' floats (centre, box minimum, box maximum, radius): 8 versions of each in FIG8, one in another FIG,
    // 6 corner versions without a FIG header.
    if (fig::IsFig(magic)) {
        float versions[fig::kBoundsFloats * fig::kFig8Versions];
        const int count = fig::kBoundsFloats * fig::VersionsOf(magic);
        std::memcpy(versions, data + fig::kHeaderBytes, count * sizeof(float));
        MorphFigureBounds(out, versions, weightS, weightT, weightU, magic);
    } else {
        float corners[fig::kBoundsFloats * fig::kOldVersions];
        lib::LoadBytes(corners, data + fig::kMeshWordsBytes);
        MorphFigureBounds6(out, corners, weightS, weightT, weightU);
    }
    if (model) model->mesh->CloseFile(data);
    else CloseFile(data);
    return 1;
}

// --- Figure model destroy ----------------------------------------------------------------------------

// The figure model's destructor (FigureModel, model.h): every cached animation deleted, each archive closed
// (CloseResource) and deleted. See docs/notes/models.md.

FigureModel::~FigureModel() {
    for (auto& [name, animation] : animationCache) delete animation;

    MappedResource* const archives[3] = {mesh, bones, animations};
    for (MappedResource* archive : archives) archive->CloseResource();
    for (MappedResource* archive : archives) delete archive;
}

// The figure manager's destructor: every cached model deleted, then the base's clean-up.
FigureManager::~FigureManager() {
    for (auto& [name, model] : models) delete model;
    ResourceManager::Destroy();
}

// --- Figure models -----------------------------------------------------------------------------------

// The model named `name`, loaded on first use from "<name>.mod", ".bon" and ".anm" through the manager's GetFile
// and cached in FigureManager::models. Without the .mod nothing is created (the others are closed) and null is
// returned. See docs/notes/models.md.
FigureModel* FigureManager::FindModel(const std::string* name) {
    if (auto found = models.find(*name); found != models.end()) return found->second;
    FigureModel* model = OpenFigureModel(
        *name, [this](const std::string& path) { return GetFile(path, nullptr); },
        [this](void* data) {
            if (data) CloseFile(data);
        });
    if (model) models[*name] = model;
    return model;
}

// The model's animation of a name, loaded on first use from its animation archive (GetFile with the name) into
// a new archive over that data (OpenFromMemory); without data nothing is created and null is returned.
MappedResource* FigureModel::FindAnimation(const std::string* name) {
    if (auto found = animationCache.find(*name); found != animationCache.end()) return found->second;
    void* data = animations->GetFile(name->data(), nullptr);
    if (!data) return nullptr;
    MappedResource* animation = new MMIOResource();
    animationCache[*name] = animation;
    animation->OpenFromMemory(data);
    return animation;
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

// --- Figure factory ----------------------------------------------------------------------------------

// The figure object factory: creates a scene object of a class id. 0x41 and 0x42 are FigureMeshes, 0x43 a
// UiMesh (ui.h), 0x44 a SkyObject and 0x300B-0x300D an AnimationTrack, a FigureBone and a
// FigureAnimationState. The new object's type gets the class id; an unknown id returns null. See
// docs/notes/models.md.

namespace {

EngineObject* Create(int classId) {
    switch (static_cast<FigureObjectType>(classId)) {
    case FigureObjectType::LoosePart:
    case FigureObjectType::AttachedPart:
        return new FigureMesh();
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

EngineObject* CreateFigureObject(int classId) {
    EngineObject* object = Create(classId);
    if (!object) return nullptr;
    object->type = classId;
    return object;
}
