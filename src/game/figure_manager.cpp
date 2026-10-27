// The figure manager's loaders (A-W12): 005AC1A0 NewFigure, 005AC2F0 LoadPart, 005ACA60
// LoadBoneAnimation. See figure_manager.h.
#include "figure_manager.h"

#include "figure_mesh.h"
#include "game_strings.h"
#include "map_object_animation.h"
#include "map_object_figures.h"
#include "mod_folders.h"
#include "world_data.h"

#include "ei/hook.h"
#include "lib/mfc.h"
#include "lib/crt.h"
#include "game_data.h"
#include "lib/str.h"

#include <cstring>
#include <string>


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

int CFigureManager::CreateWithArchives() {
    if (!CBaseManager::Create()) return 0;
    // Rebirth's ResOverride: the mod's figure archives come first.
    if (modfolders::Active(ei::rebirth::Patch::ResOverride)) {
        for (const std::string& path : modfolders::FigureArchives()) AddArchive(path.c_str());
    }
    std::string folder = TheTextFolder().GetString();
    AddArchive((folder + kFiguresArchive).c_str());
    AddArchive((folder + kMenusArchive).c_str());
    LoadMenuRects(g_menuRects);
    return 1;
}
EI_REWRITTEN(0x005AA510, CFigureManager::CreateWithArchives);

CFigureMesh* CFigureManager::NewPart(const char* modelName, const char* partName, FigureModel*, std::uint32_t,
                                     std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t attached) {
    auto* part = CreateObjectAs<CFigureMesh>(kSharedPartClass + (attached != 0), -1);
    part->name_ = partName;
    part->directory_ = modelName;
    return part;
}

CEngineObject* CFigureManager::NewFigure(const char* name) {
    auto* figure = CreateObjectAs<CFigureMesh>(kFigureClass, -1);
    if (!name) return figure;
    figure->name_ = name;
    figure->directory_ = game::EmptyText();
    std::string path;
    path = str::Format(game::FigFileFormat(), name);
    void* data = GetFile(path.c_str(), nullptr);
    if (!data) {
        ReleaseObject(figure);
        return nullptr;
    }
    char* cursor = static_cast<char*>(data);
    figure->LoadFromMemory(&cursor, 0.0f, 0.0f, 0.0f);
    figure->bounds_.flags = 0;
    CloseFile(data);
    return figure;
}

int CFigureManager::LoadPart(CFigureMesh* part, FigureModel* model, float s, float t, float u) {
    void* data;
    if (model) {
        CMappedResource* mesh = model->mesh;
        data = mesh->GetFile(part->GetName(), nullptr);
    } else {
        const char* directory = part->directory_;
        std::string path;
        path = str::Format(game::FigPathFormat(), directory, part->GetName());
        data = GetFile(path.c_str(), nullptr);
    }
    if (!data) {
        const char* directory = part->directory_;
        return game::FatalError(game::CantOpenFigText(), directory, part->GetName());
    }
    char* cursor = static_cast<char*>(data);
    part->LoadFromMemory(&cursor, s, t, u);
    return 1;
}

int CFigureManager::LoadBoneAnimation(FigureBone* bone, AnimationTrack* animator, const char* animation) {
    if (!animation) return 0;
    // The inlined strcpy / strcat chain (no bounds check, as the original).
    char path[0x200];
    char* end = Append(path, bone->name3);
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
    std::memcpy(&magic, *cursor, 4);
    if ((magic & 0xFFFFFF) == kFigMagic) LoadFig(cursor, s, t, u);
    else LoadOld(cursor, s, t, u);
    flags_ |= kLoaded;
}

EI_REWRITTEN(0x005A2490, CFigureMesh::LoadFromMemory);
EI_REWRITTEN(0x005A4CF0, CFigureMesh::LoadFromMemory);   // byte-identical (class 0x41's slot 19)
EI_REWRITTEN(0x005AC1A0, CFigureManager::NewFigure);
EI_REWRITTEN(0x005AC2B0, CFigureManager::NewPart);
EI_REWRITTEN(0x005A9EB0, FigureModel::Open);
EI_REWRITTEN(0x005AC2F0, CFigureManager::LoadPart);
EI_REWRITTEN(0x005ACA60, CFigureManager::LoadBoneAnimation);
