#pragma once

// The figure manager's loaders (figure_manager.cpp): the resource manager that makes the game-side figure
// objects (figure_factory.cpp) and loads their data from its archives or a cached model (figure_models.cpp;
// docs/notes/models.md). Name provisional.

#include "figure_mesh.h"   // CFigureMesh
#include "resource_manager.h"
#include "world.h"   // CWorld::FigureManager

#include "lib/stlp.h"

#include <cstddef>
#include <cstdint>

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

struct AnimationTrack;
class CFigureAnimationState;
struct FigureAnimation;
struct FigureBone;


// figure_factory.cpp: the figure object factory, also the figure manager's creator virtual.
void* __stdcall CreateFigureObject(int classId);

// FigureModel, the cached model, is in model.h.

// A real virtual class.
// The menus' button rectangles (menus.reg; menu_rects.cpp): the main menu's six (new game, load, exit, options,
// credits, multiplayer), then the Esc menu's five (save, load, resume, exit to the main menu, options).
// credits, multiplayer), then the Esc menu's five (save, load, resume, exit to the main menu, options).
constexpr int kMainMenuRects = 6;
constexpr int kMenuRects = 11;
extern RECT g_menuRects[kMenuRects];

class CFigureManager : public CResourceManager {
public:
    // figure_mesh_life.cpp. CResourceManager's constructor, an empty model map with NextPrime(100) buckets.
    CFigureManager();
    // figure_model_destroy.cpp. Every cached model destroyed and freed, the base's clean-up, the model map cleared
    // and its buckets freed, then CResourceManager's destructor.
    ~CFigureManager() override;

    int Create() override { return CreateWithArchives(); }
    const char* GetClassName() override;                                     // "CFigureManager"
    int GetClassId() override;                                               // 0x28
    CEngineObject* NewObject(int type) override;                             // the factory
    BOOL AddArchive(const char* path) override { return AddMmioArchive(path); }
    CEngineObject* CreateObject(int type, int id) override { return CreateFigureObject(type, id); }   // 23
    const char* GetFolderName() override;                                    // "figures"

    int LoadSkeletonLinks(CFigureAnimationState* state, const char* name);
    int LoadFigureBones(FigureModel* model, FigureBone* bone);
    int LoadFigureBounds(ClipBounds* out, FigureModel* model, const char* directory, const char* name, std::uint32_t s, std::uint32_t t, std::uint32_t u);
    FigureModel* FindModel(const stlp::string* name);
    // figure_animation.cpp. Sets `animation` (found in the model by its name) of the model `modelName` on
    // `figure`; 0 when it cannot.
    int SetFigureAnimation(CFigureAnimationState* figure, const char* modelName, const FigureAnimation* animation);
    // menu_rects.cpp (CreateWithArchives): the main menu's and the escape menu's button rectangles from
    // "menus.reg" into `rects` (g_menuRects). 1; FatalError's result when the file or an entry cannot be read.
    int LoadMenuRects(RECT* rects);

    // figure_mesh_life.cpp. CBaseManager::CreateObject; a new figure (classes 0x41-0x44) also gets a drawer: a
    // class 0x45 object (id -1) of the context's specific manager.
    CEngineObject* CreateFigureObject(int type, int id);
    // Create (patched by Rebirth, so vanilla only). The base manager's Create (0 when it fails), then the
    // archives "figures.res" and "menus.res" of the text resources' folder added and the menus' button rectangles
    // read. Returns 1.
    int CreateWithArchives();

    // (The interface's model manager too.) A figure object (class 0x43, created with id -1); with a name it is
    // named (its directory emptied) and loaded from "<name>.fig" (then a member cleared and the data closed), or
    // released and null when the file is missing. Without a name the empty object is returned.
    CEngineObject* NewFigure(const char* name);
    // `part`'s data from the model's mesh archive (by the part's name) or from "<directory><name>.fig", loaded by
    // the part's load virtual at the morph (s, t, u); returns 1. Missing data: FatalError("Can't open file ...")
    // and what it returns. The data is not closed.
    int LoadPart(CFigureMesh* part, FigureModel* model, float s, float t, float u);
    // CObject3D::LoadPart. A new figure part object (CreateObject with id -1: class 0x42 when `attached`, else
    // 0x41) named `partName` of the model `modelName`. The cached model, the scale (three float bit patterns) and
    // `arg` are not read. Returns the part (a null part faults, as in the original).
    CFigureMesh* NewPart(const char* modelName, const char* partName, FigureModel* model, std::uint32_t scaleX,
                         std::uint32_t scaleY, std::uint32_t scaleZ, std::uint32_t arg, std::uint32_t attached);
    // The bone's animation `animation` from "<bone's model name><bone's name><animation>.ast" into `animator`
    // (LoadKeyFrames); 0 without an animation or the file. The path is built in a 0x200-byte buffer without a
    // bounds check; the data is not closed.
    int LoadBoneAnimation(FigureBone* bone, AnimationTrack* animator, const char* animation);

    stlp::hash_map<stlp::string, FigureModel*, stlp::StringHash> models_;   // +0xBC the cached models by name
};
static_assert(offsetof(CFigureManager, models_) == 0xBC && sizeof(CFigureManager) == 0xD0);

// The figure manager of `object`'s world (every world's is a CFigureManager).
inline CFigureManager* FigureManagerOf(const CEngineObject* object) {
    return static_cast<CFigureManager*>(object->WorldContext()->FigureManager());
}
// The figure manager that made `object` (a bone or an animation track, CEngineObject::owner_).
inline CFigureManager* OwningFigureManager(const CEngineObject* object) {
    return static_cast<CFigureManager*>(object->owner_);
}
