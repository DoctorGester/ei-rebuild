// A-W12: the figure manager's loaders and the figure meshes' slot 19 (figure_manager.cpp). All ours
// only: they create objects and open archive files. Reached whenever a figure loads: NewFigure by the interface's 3D models (the
// model manager, ui_window.cpp) at the menus, LoadPart for every part of a unit or object figure
// (BuildPart, map_object_figures.cpp; every new game), LoadBoneAnimation when a bone starts an
// animation (map_object_animation_state.cpp).
#include "verify.h"

#include "../../src/game/figure_manager.h"
#include "../../src/game/figure_mesh.h"
#include "../../src/game/map_object_animation.h"
#include "../../src/game/map_object_figures.h"

#include <cstring>

namespace {
void* s_unused;

constexpr std::uint32_t kLoaded = 0x20;

// A figure that came back is named `name` (when given), loaded and has no valid bounds.
CEngineObject* __fastcall NewFigureVerify(CFigureManager* self, void*, const char* name) {
    CEngineObject* object = self->NewFigure(name);
    auto* figure = reinterpret_cast<CFigureMesh*>(object);
    bool ok = !object || !name ||
              (std::strcmp(figure->name_, name) == 0 && (figure->flags_ & kLoaded) && figure->bounds_.flags == 0);
    ei::verify::Check("CFigureManager::NewFigure", true, ok, "%s", name ? name : "(null)");
    return object;
}

// 1 leaves the part loaded (its slot 19 sets the flag).
int __fastcall LoadPartVerify(CFigureManager* self, void*, CFigureMesh* part, FigureModel* model, float s, float t, float u) {
    int result = self->LoadPart(part, model, s, t, u);
    ei::verify::Check("CFigureManager::LoadPart", true, result != 1 || (part->flags_ & kLoaded), "part %p model %p",
                      static_cast<void*>(part), static_cast<void*>(model));
    return result;
}

// 0 without an animation; 1 leaves the animator's key frame arrays consistent with their counts.
int __fastcall LoadBoneAnimationVerify(CFigureManager* self, void*, FigureBone* bone, AnimationTrack* animator,
                                       const char* animation) {
    int result = self->LoadBoneAnimation(bone, animator, animation);
    bool ok = result == 0 || result == 1;
    if (!animation) ok = result == 0;
    if (result == 1)
        ok = (animator->rotationCount == 0) == (animator->rotations == nullptr) &&
             (animator->positionCount == 0) == (animator->positions == nullptr);
    ei::verify::Check("CFigureManager::LoadBoneAnimation", true, ok, "%s", animation ? animation : "(null)");
    return result;
}

// 005A2490 / 005A4CF0 (slot 19): the mesh is marked loaded afterwards (its loaders are verified
// on their own).
void __fastcall LoadFromMemoryVerify(CFigureMesh* self, void*, char** cursor, float s, float t, float u) {
    self->LoadFromMemory(cursor, s, t, u);
    ei::verify::Check("CFigureMesh::LoadFromMemory", true, (self->flags_ & kLoaded) != 0, "mesh %p", static_cast<void*>(self));
}

// 005AC2B0 (A-W14), ours only (it creates an object): the part has the class asked for and the
// two names. Reached by CObject3D::LoadPart (00517240) for every part of every unit and object
// figure: each new game.
CEngineObject* __fastcall NewPartVerify(CFigureManager* self, void*, const char* modelName, const char* partName,
                                       FigureModel* model, std::uint32_t x, std::uint32_t y, std::uint32_t z,
                                       std::uint32_t arg, std::uint32_t attached) {
    CEngineObject* object = self->NewPart(modelName, partName, model, x, y, z, arg, attached);
    auto* part = reinterpret_cast<CFigureMesh*>(object);
    bool ok = part->type_ == (attached ? 0x42 : 0x41) && std::strcmp(part->name_, partName ? partName : "") == 0 &&
              std::strcmp(part->directory_, modelName ? modelName : "") == 0;
    ei::verify::Check("CFigureManager::NewPart", true, ok, "%s / %s attached %u", modelName ? modelName : "(null)",
                      partName ? partName : "(null)", attached);
    return object;
}

// 005A9EB0 (A-W14), ours only (an archive opened twice would close its files in between): each
// archive given an image has that image's header afterwards. Reached by the link figure loader
// (FindFigureSet, 004FAB00) the first time a figure with its own .mod archive is asked for: the
// menus' and the game's unit figures.
void __fastcall ModelOpenVerify(FigureModel* self, void*, const void* mod, const void* bon, const void* anm) {
    self->Open(mod, bon, anm);
    auto same = [](const CMappedResource* archive, const void* image) {
        return !image || std::memcmp(&archive->header_, image, sizeof(archive->header_)) == 0;
    };
    ei::verify::Check("FigureModel::Open", true, same(self->mesh, mod) && same(self->bones, bon) && same(self->animations, anm),
                      "model %p: %p %p %p", static_cast<void*>(self), mod, bon, anm);
}

} // namespace

EI_VERIFY(0x005A2490, LoadFromMemoryVerify, 0, s_unused);
EI_VERIFY(0x005A4CF0, LoadFromMemoryVerify, 0, s_unused);
EI_VERIFY(0x005AC1A0, NewFigureVerify, 0, s_unused);
EI_VERIFY(0x005AC2B0, NewPartVerify, 0, s_unused);
EI_VERIFY(0x005A9EB0, ModelOpenVerify, 0, s_unused);
EI_VERIFY(0x005AC2F0, LoadPartVerify, 0, s_unused);
EI_VERIFY(0x005ACA60, LoadBoneAnimationVerify, 0, s_unused);
