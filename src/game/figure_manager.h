#pragma once

// The figure manager's loaders (A-W12, figure_manager.cpp): the resource manager that makes the
// game-side figure objects (factory 005AA8E0, figure_factory.cpp) and loads their data from its
// archives or a cached model (figure_models.cpp; docs/notes/models.md). Name provisional.

#include "figure_mesh.h"   // CFigureMesh
#include "resource_manager.h"

#include "lib/stlp.h"

#include <cstddef>
#include <cstdint>

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

struct AnimationTrack;
class CFigureAnimationState;
struct FigureBone;

// 005ACCF0 (figure_animation.cpp; RET 0xC). The figure manager sets `animation` (an entry, read as
// its name) of the model `modelName` on `figure` (a CFigureAnimationState); 0 when it cannot.
int __fastcall SetFigureAnimation(void* manager, void*, void* figure, const char* modelName, const char* animation);

// 005AA8E0 (figure_factory.cpp): the figure object factory, also the figure manager's slot 14.
void* __fastcall CreateFigureObject(void* self, void* edx, int classId);

// FigureModel, the cached model, is in model.h.

// A real virtual class: the compiler makes its vtable (C19; game.exe's 0073FB88 is retired).
class CFigureManager : public CResourceManager {
public:
    // 005AA070 (figure_mesh_life.cpp). CResourceManager's constructor, an empty model map with
    // NextPrime(100) buckets.
    CFigureManager();
    // 005AA1C0 (figure_model_destroy.cpp). Every cached model destroyed and freed, the base's
    // clean-up (00674F40), the model map cleared and its buckets freed, then CResourceManager's
    // destructor.
    ~CFigureManager() override;

    int Create() override { return CreateWithArchives(); }                   // 2: 005AA510
    const char* GetClassName() override;                                     // 11: 005AA180 "CFigureManager"
    int GetClassId() override;                                               // 12: 005AA190 (0x28)
    CEngineObject* NewObject(int type) override;                             // 14: 005AA8E0 the factory
    BOOL AddArchive(const char* path) override { return AddMmioArchive(path); }   // 16: 005AA360
    CEngineObject* CreateObject(int type, int id) override { return CreateFigureObject(type, id); }   // 23
    const char* GetFolderName() override;                                    // 25: 005AA170 "figures"

    // 005AA1A0 (slot 0; world_deleting_destructors.cpp): the destructor, then operator delete when
    // bit 0 of `flags` is set (returns this). A registration (C20 retires it), as Construct.
    CFigureManager* DeleteScalar(unsigned flags);
    int LoadSkeletonLinks(CFigureAnimationState* state, const char* name);
    int LoadFigureBones(FigureModel* model, FigureBone* bone);
    int LoadFigureBounds(ClipBounds* out, FigureModel* model, const char* directory, const char* name, std::uint32_t s, std::uint32_t t, std::uint32_t u);
    FigureModel* FindModel(const stlp::string* name);

    // 005AA070 (figure_mesh_life.cpp, A-W13). The constructor in place (a registration); returns
    // this.
    CFigureManager* Construct();
    // 005AAE00 (slot 23, RET 8; figure_mesh_life.cpp). CBaseManager::CreateObject; a new figure
    // (classes 0x41-0x44) also gets a drawer: a class 0x45 object (id -1) of the context's
    // specific manager (its slot 21).
    CEngineObject* CreateFigureObject(int type, int id);
    // 005AA510 (slot 2, Create; patched by Rebirth, so vanilla only; A-W14). The base manager's
    // Create (0 when it fails), then the archives "figures.res" and "menus.res" of the text
    // resources' folder (007AF45C) added (slot 16) and the menus' button rectangles read
    // (005ACE80 into 007C8048). Returns 1.
    int CreateWithArchives();

    // 005AC1A0 (RET 4; the interface's model manager too). A figure object (class 0x43, slot 23 with
    // id -1); with a name it is named (+0x120; +0x124 emptied) and loaded from "<name>.fig" (slot
    // 19, then +0x188 cleared and the data closed), or released (slot 24) and null when the file is
    // missing. Without a name the empty object is returned.
    CEngineObject* NewFigure(const char* name);
    // 005AC2F0 (RET 0x14). `part`'s data from the model's mesh archive (slot 5 by the part's name)
    // or from "<directory (+0x124)><name>.fig" (slot 19), loaded by the part's slot 19 at the morph
    // (s, t, u); returns 1. Missing data: FatalError("Can't open file ...") and what it returns.
    // The data is not closed.
    int LoadPart(CFigureMesh* part, FigureModel* model, float s, float t, float u);
    // 005AC2B0 (RET 0x20; CObject3D::LoadPart, 00517240; A-W14). A new figure part object
    // (CreateObject with id -1: class 0x42 when `attached`, else 0x41) named `partName` (+0x120)
    // of the model `modelName` (+0x124). The cached model, the scale (three float bit patterns)
    // and `arg` are not read. Returns the part (a null part faults, as in the original).
    CEngineObject* NewPart(const char* modelName, const char* partName, FigureModel* model, std::uint32_t scaleX,
                           std::uint32_t scaleY, std::uint32_t scaleZ, std::uint32_t arg, std::uint32_t attached);
    // 005ACA60 (RET 0xC). The bone's animation `animation` from "<bone's model name (+0x24)><bone's
    // name (slot 7)><animation>.ast" (slot 19) into `animator` (LoadKeyFrames); 0 without an
    // animation or the file. The path is built in a 0x200-byte buffer without a bounds check; the
    // data is not closed.
    int LoadBoneAnimation(FigureBone* bone, AnimationTrack* animator, const char* animation);

    stlp::hash_map<stlp::string, FigureModel*, stlp::StringHash> models_;   // +0xBC the cached models by name
};
static_assert(offsetof(CFigureManager, models_) == 0xBC && sizeof(CFigureManager) == 0xD0);
