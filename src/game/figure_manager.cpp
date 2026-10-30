// The figure manager's loaders: NewFigure, LoadPart, LoadBoneAnimation. See figure_manager.h.
#include "figure_manager.h"

#include "figure_mesh.h"
#include "game_strings.h"
#include "map_object_animation.h"
#include "map_object_figures.h"
#include "mod_folders.h"
#include "world_data.h"

#include "lib/bytes.h"
#include "lib/crt.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include "lib/str.h"



namespace {

constexpr std::uint32_t kFigMagic = 0x474946;      // "FIG"
constexpr const char* kFiguresArchive = "figures.res";
constexpr const char* kMenusArchive = "menus.res";

// Copies `text` with its terminator to `end`; returns the new end (at the terminator).
char* Append(char* end, const char* text) {
    std::size_t length = std::strlen(text);
    std::memcpy(end, text, length + 1);
    return end + length;
}

} // namespace

void FigureModel::Open(const void* mod, const void* bon, const void* anm) {
    if (mod) mesh->OpenFromMemory(mod);
    if (bon) bones->OpenFromMemory(bon);
    if (anm) animations->OpenFromMemory(anm);
}

void CFigureManager::Create() {
    BaseManager::Create();
    // Rebirth's ResOverride: the mod's figure archives come first.
    if (modfolders::Active(ei::rebirth::Patch::ResOverride)) {
        for (const std::string& path : modfolders::FigureArchives()) AddArchive(path);
    }
    std::string folder = g_textFolder;
    AddArchive((folder + kFiguresArchive));
    AddArchive((folder + kMenusArchive));
    LoadMenuRects(g_menuRects);
}

FigureMesh* CFigureManager::NewPart(const std::string& modelName, std::string_view partName, std::uint32_t attached) {
    auto* part = CreateObjectAs<FigureMesh>(attached ? FigureObjectType::PlainFigureMesh : FigureObjectType::SharedFigureMesh, -1);
    part->name = partName;
    part->directory = modelName;
    return part;
}

FigureMesh* CFigureManager::NewBlankFigure() { return CreateObjectAs<FigureMesh>(FigureObjectType::UiMesh, -1); }

FigureMesh* CFigureManager::NewFigure(std::string_view name) {
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

int CFigureManager::LoadBoneAnimation(FigureBone* bone, AnimationTrack* animator, const char* animation) {
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

void FigureMesh::LoadFromMemory(char** cursor) {
    if (flags & FigureMeshFlag::Loaded) return;
    std::uint32_t magic;
    lib::LoadBytes(magic, *cursor);
    if ((magic & 0xFFFFFF) == kFigMagic) LoadFig(cursor);
    else LoadOld(cursor);
    flags |= FigureMeshFlag::Loaded;
}

// --- Class name and id ------------------------------------------------------------------------------

namespace {
constexpr const char* kCFigureManagerClassName = "CFigureManager";
} // namespace

const char* CFigureManager::ClassName() { return kCFigureManagerClassName; }
ClassId CFigureManager::GetClassId() { return ClassId::FigureManager; }
const char* CFigureManager::GetFolderName() { return "figures"; }
