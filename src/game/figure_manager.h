#pragma once

// The figure manager: makes the figure objects and loads their data from its archives or a cached model
// (docs/notes/models.md).

#include "figure_mesh.h"
#include "resource_manager.h"
#include "world.h"

#include "lib/stlp.h"

#include <cstddef>
#include <cstdint>

#undef GetClassName


void* __stdcall CreateFigureObject(int classId);

// The menus' button rectangles (menus.reg): the main menu's six (new game, load, exit, options, credits,
// multiplayer), then the Esc menu's five (save, load, resume, exit to the main menu, options).
constexpr int kMainMenuRects = 6;
constexpr int kMenuRects = 11;
extern RECT g_menuRects[kMenuRects];

class CFigureManager : public CResourceManager {
public:
    CFigureManager();
    ~CFigureManager() override;

    int Create() override { return CreateWithArchives(); }
    const char* GetClassName() override;
    int GetClassId() override;
    CEngineObject* NewObject(int type) override;
    BOOL AddArchive(const char* path) override { return AddMmioArchive(path); }
    CEngineObject* CreateObject(int type, int id) override { return CreateFigureObject(type, id); }
    const char* GetFolderName() override;

    int LoadSkeletonLinks(CFigureAnimationState* state, const char* name);
    int LoadFigureBones(FigureModel* model, FigureBone* bone);
    int LoadFigureBounds(ClipBounds* out, FigureModel* model, const char* directory, const char* name, std::uint32_t s, std::uint32_t t, std::uint32_t u);
    FigureModel* FindModel(const stlp::string* name);
    // Sets the animation found by name in model `modelName` on `figure`; 0 when it cannot.
    int SetFigureAnimation(CFigureAnimationState* figure, const char* modelName, const FigureAnimation* animation);
    // The menus' button rectangles from "menus.reg" into `rects`; 1, or FatalError's result when the file or an
    // entry cannot be read.
    int LoadMenuRects(RECT* rects);

    // CBaseManager::CreateObject; a new figure (classes 0x41-0x44) also gets a drawer: a
    // class 0x45 object (id -1) of the context's specific manager.
    CEngineObject* CreateFigureObject(int type, int id);
    // Create (Rebirth patches it; vanilla only): the base's Create, then the archives "figures.res" and
    // "menus.res" and the menus' rectangles. Returns 1.
    int CreateWithArchives();

    // (The interface's model manager too.) A figure object, loaded from "<name>.fig" when named (released and null
    // when the file is missing); the empty object without a name.
    CEngineObject* NewFigure(const char* name);
    // `part`'s data from the model's mesh archive or "<directory><name>.fig" at the morph (s, t, u); returns 1.
    // Missing data: FatalError's result. The data is not closed.
    int LoadPart(CFigureMesh* part, FigureModel* model, float s, float t, float u);
    // CObject3D::LoadPart. A new figure part (class 0x42 when `attached`, else 0x41) named `partName`. The model,
    // the scale and `arg` are not read; a null part faults, as in the original.
    CFigureMesh* NewPart(const char* modelName, const char* partName, FigureModel* model, std::uint32_t scaleX,
                         std::uint32_t scaleY, std::uint32_t scaleZ, std::uint32_t arg, std::uint32_t attached);
    // The bone's animation from "<model><bone><animation>.ast" into `animator`; 0 without an animation or the
    // file. The path is built in a 0x200-byte buffer without a bounds check; the data is not closed.
    int LoadBoneAnimation(FigureBone* bone, AnimationTrack* animator, const char* animation);

    stlp::hash_map<stlp::string, FigureModel*, stlp::StringHash> models_;   // the cached models by name
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
