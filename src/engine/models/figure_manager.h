#pragma once

// The figure manager: makes the figure objects and loads their data from its archives or a cached model
// (docs/notes/models.md).

#include "game/world/world.h"

#include "lib/str.h"   // ViewHash

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>
#include <unordered_map>

// The figure manager's object types (CreateObject / type), named after their classes.
class UiMesh;

enum class FigureObjectType : int {
    LoosePart = 0x41,             // FigureMesh: a part that is not attached
    AttachedPart = 0x42,          // FigureMesh: an attached part
    UiMesh = 0x43,                // UiMesh: a whole figure (NewBlankFigure)
    SkyObject = 0x44,             // SkyObject
    AnimationTrack = 0x300B,      // AnimationTrack: a bone's animator
    FigureBone = 0x300C,          // FigureBone
    AnimationState = 0x300D,      // FigureAnimationState: a map object's skeleton
};

// The figure factory: a new object of figure type `classId` with type set, or null for another type.
EngineObject* CreateFigureObject(int classId);

// The menus' button rectangles (menus.reg): the main menu's six (new game, load, exit, options, credits,
// multiplayer), then the Esc menu's five (save, load, resume, exit to the main menu, options).
constexpr int kMainMenuRects = 6;
constexpr int kMenuRects = 11;
extern Rect g_menuRects[kMenuRects];

class FigureManager : public ResourceManager {
public:
    FigureManager();
    ~FigureManager() override;

    // Create (Rebirth patches it; vanilla only): the base's Create, then the archives "figures.res" and
    // "menus.res" and the menus' rectangles.
    void Create() override;
    const char* ClassName() override;
    ClassId GetClassId() override;
    EngineObject* NewObject(int type) override;
    bool AddArchive(const std::string& path) override { return AddMmioArchive(path); }
    // BaseManager::CreateObject; a new figure (classes 0x41-0x44) also gets a drawer: a class 0x45 object (id -1)
    // of the context's specific manager.
    EngineObject* CreateObject(int newType, int newId) override;

    int LoadSkeletonLinks(FigureAnimationState* state, std::string_view name);
    int LoadFigureBones(FigureModel* model, FigureBone* bone);
    int LoadFigureBounds(ClipBounds* out, FigureModel* model, std::string_view directory, std::string_view name, float s, float t, float u);
    FigureModel* FindModel(std::string_view name);
    // Sets the animation found by name in model `modelName` on `figure`; false when it cannot.
    bool SetFigureAnimation(FigureAnimationState* figure, std::string_view modelName, const FigureAnimation* animation);
    // The menus' button rectangles from "menus.reg" into `rects`; 1 (a file or an entry that cannot be read ends
    // the game).
    int LoadMenuRects(Rect* rects);

    // (The interface's model manager too.) A figure object, loaded from "<name>.fig" when named (released and null
    // when the file is missing); the empty object without a name.
    UiMesh* NewFigure(std::string_view name);
    // The empty figure object (no file read).
    UiMesh* NewBlankFigure();
    // Object3D::LoadPart. A new figure part (class 0x42 when `attached`, else 0x41) named
    // `partName`; a null part faults, as in the original.
    FigureMesh* NewPart(const std::string& modelName, std::string_view partName, std::uint32_t attached);
    // The bone's animation from "<model><bone><animation>.ast" into `animator`; 0 without an animation or the
    // file. The data is not closed.
    int LoadBoneAnimation(FigureBone* bone, AnimationTrack* animator, const char* animation);

    std::unordered_map<std::string, FigureModel*, str::ViewHash, std::equal_to<>> models;   // the cached models by name
};

// The figure manager of `object`'s world: the one that made it (a world has one).
inline FigureManager* FigureManagerOf(const EngineObject* object) {
    return object->GetManagerContext()->GetFigureManager();
}
