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
#include "game_data.h"
#include "lib/str.h"



namespace {

constexpr int kFigureClass = 0x43;
constexpr std::uint32_t kLoaded = 0x20;            // CFigureMesh::flags_
constexpr std::uint32_t kFigMagic = 0x474946;      // "FIG"
constexpr const char* kFiguresArchive = "figures.res";
constexpr const char* kMenusArchive = "menus.res";
constexpr int kSharedPartClass = 0x41;             // a part whose buffers point into the file data; 0x42 owns them

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

void CFigureManager::CreateWithArchives() {
    CBaseManager::Create();
    // Rebirth's ResOverride: the mod's figure archives come first.
    if (modfolders::Active(ei::rebirth::Patch::ResOverride)) {
        for (const std::string& path : modfolders::FigureArchives()) AddArchive(path);
    }
    std::string folder = *g_textFolder;
    AddArchive((folder + kFiguresArchive));
    AddArchive((folder + kMenusArchive));
    LoadMenuRects(g_menuRects);
}

CFigureMesh* CFigureManager::NewPart(const std::string& modelName, std::string_view partName, std::uint32_t attached) {
    auto* part = CreateObjectAs<CFigureMesh>(kSharedPartClass + (attached != 0), -1);
    part->name_ = partName;
    part->directory_ = modelName;
    return part;
}

CFigureMesh* CFigureManager::NewBlankFigure() { return CreateObjectAs<CFigureMesh>(kFigureClass, -1); }

CFigureMesh* CFigureManager::NewFigure(std::string_view name) {
    auto* figure = NewBlankFigure();
    figure->name_ = name;
    figure->directory_ = game::kEmptyText;
    const std::string path = std::string(name) + ".fig";
    void* data = GetFile(path, nullptr);
    if (!data) {
        ReleaseObject(figure);
        return nullptr;
    }
    char* cursor = static_cast<char*>(data);
    figure->LoadFromMemory(&cursor, 0.0f, 0.0f, 0.0f);
    figure->box_.bounds.flags = 0;
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

void CFigureMesh::LoadFromMemory(char** cursor, float s, float t, float u) {
    if (flags_ & kLoaded) return;
    std::uint32_t magic;
    lib::LoadBytes(magic, *cursor);
    if ((magic & 0xFFFFFF) == kFigMagic) LoadFig(cursor, s, t, u);
    else LoadOld(cursor, s, t, u);
    flags_ |= kLoaded;
}
