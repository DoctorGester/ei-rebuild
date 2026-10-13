#pragma once

// The figure manager's loaders (A-W12, figure_manager.cpp): the resource manager that makes the
// game-side figure objects (factory 005AA8E0, figure_factory.cpp) and loads their data from its
// archives or a cached model (figure_models.cpp; docs/notes/models.md). Name provisional.

#include "core_transform.h"
#include "resource_manager.h"

#include "lib/stlp.h"

#include <cstddef>
#include <cstdint>

struct AnimationTrack;
class CFigureMesh;
struct FigureBone;

// A cached figure model (0x20 bytes, constructor 005A9A50): the mesh, bones and animation archives
// of "<name>.mod / .bon / .anm" (0x6C-byte CMMIOResource objects; null when missing), then the
// animation map.
struct FigureModel {
    CMappedResource* mesh;                // +0x00
    CMappedResource* bones;               // +0x04
    CMappedResource* animations;          // +0x08
    std::uint8_t unknown_0C[0x20 - 0x0C]; // +0x0C hash_map<string, entry> (FindModelAnimation)

    // 005A9EB0 (RET 0xC; the link figure loader's sets, 004FAB00; A-W14). Opens the three archives
    // from the images of the figure's .mod / .bon / .anm files (OpenFromMemory); a null image
    // leaves its archive alone.
    void Open(const void* mod, const void* bon, const void* anm);
};
static_assert(sizeof(FigureModel) == 0x20);

// A figure object (classes 0x41-0x44, CFigureMesh's layout) as callers reach its slots 18 / 19
// through the vtable (as CSkyObject declares them). The slot bodies stay members of CFigureMesh.
class CFigureObjectVirtuals : public CTransformObject {
public:
    virtual void CopyFigure() = 0;                                              // 18: 0059F0F0
    virtual void LoadFromMemory(char** cursor, float s, float t, float u) = 0;  // 19: 005A2490 / 005A4CF0
    virtual void LoadFig(char** cursor, float s, float t, float u) = 0;         // 20: "FIG" data
    virtual void LoadOld(char** cursor, float s, float t, float u) = 0;         // 21: the older format
};
inline CFigureObjectVirtuals* FigureSlots(CFigureMesh* figure) { return reinterpret_cast<CFigureObjectVirtuals*>(figure); }

class CFigureManager : public CResourceManager {
public:
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x0073FB88);

    // 005AA070 (figure_mesh_life.cpp, A-W13). The constructor body: CResourceManager's, an empty
    // model map with NextPrime(100) buckets, the vtable. Returns this.
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
