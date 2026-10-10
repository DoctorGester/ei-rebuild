// The figure manager's loaders (A-W12): 005AC1A0 NewFigure, 005AC2F0 LoadPart, 005ACA60
// LoadBoneAnimation. See figure_manager.h.
#include "figure_manager.h"

#include "figure_mesh.h"
#include "game_strings.h"
#include "map_object_animation.h"
#include "map_object_figures.h"

#include "ei/hook.h"
#include "lib/mfc.h"
#include "originals_engine.h"

#include <cstring>

namespace {

constexpr int kFigureClass = 0x43;

// Copies `text` with its terminator to `end`; returns the new end (at the terminator).
char* Append(char* end, const char* text) {
    std::size_t length = std::strlen(text);
    std::memcpy(end, text, length + 1);
    return end + length;
}

} // namespace

CEngineObject* CFigureManager::NewFigure(const char* name) {
    CEngineObject* object = CreateObject(kFigureClass, -1);
    if (!name) return object;
    auto* figure = reinterpret_cast<CFigureMesh*>(object);
    figure->name_ = name;
    figure->directory_ = game::EmptyText();
    mfc::CString path;
    path.Format(game::FigFileFormat(), name);
    void* data = GetFile(path, nullptr);
    if (!data) {
        ReleaseObject(object);
        return nullptr;
    }
    char* cursor = static_cast<char*>(data);
    FigureSlots(figure)->LoadFromMemory(&cursor, 0.0f, 0.0f, 0.0f);
    figure->bounds_.flags = 0;
    CloseFile(data);
    return object;
}

int CFigureManager::LoadPart(CFigureMesh* part, FigureModel* model, float s, float t, float u) {
    void* data;
    if (model) {
        CMappedResource* mesh = model->mesh;
        data = mesh->GetFile(FigureSlots(part)->GetName(), nullptr);
    } else {
        const char* directory = part->directory_;
        mfc::CString path;
        path.Format(game::FigPathFormat(), directory, FigureSlots(part)->GetName());
        data = GetFile(path, nullptr);
    }
    if (!data) {
        const char* directory = part->directory_;
        return FatalErrorOriginal(game::CantOpenFigText(), directory, FigureSlots(part)->GetName());
    }
    char* cursor = static_cast<char*>(data);
    FigureSlots(part)->LoadFromMemory(&cursor, s, t, u);
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

EI_REPLACE_METHOD(0x005AC1A0, CFigureManager::NewFigure);
EI_REPLACE_METHOD(0x005AC2F0, CFigureManager::LoadPart);
EI_REPLACE_METHOD(0x005ACA60, CFigureManager::LoadBoneAnimation);
