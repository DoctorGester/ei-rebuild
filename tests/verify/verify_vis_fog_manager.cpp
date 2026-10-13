// Checks for vis_fog_manager.cpp (A-S11). Main thread. Ours only, every one: each function
// allocates, loads a texture, releases objects or draws, so a second run by the original would not
// leave the state it found. What each wrapper checks is the state the function must leave.
//  - Create 004E2990 and Destroy 004E2850: the client world's set-up and shutdown (every start and
//    exit: `newgame`, `world_load`).
//  - The factory 004E2290 and the new fog 004E28D0: the "show visibility" effect's start
//    (0067C660); no scenario is known to cast it.
//  - The tick 004E2760 and the draw 004E2700: every frame of a loaded map (with no fog listed both
//    return at once).
#include "verify.h"

#include "../../src/game/effects.h"
#include "../../src/game/model.h"
#include "../../src/game/world.h"

#include <cstdint>

// vis_fog_manager.cpp (no header declares the shims).
CEngineObject* __fastcall VisFogManagerNewObject(ClientArrayManager* self, void*, int type);
int __fastcall VisFogManagerCreate(ClientArrayManager* self);
void __fastcall VisFogManagerDestroy(ClientArrayManager* self);
int __fastcall VisFogManagerTick(ClientArrayManager* self);
CEngineObject* __fastcall VisFogManagerAdd(ClientArrayManager* self);
int __fastcall VisFogManagerDraw(ClientArrayManager* self, void*, CSceneRenderer* renderer);

namespace {
void* s_unused;

constexpr std::size_t kFogBucketCount = 193;   // the first prime not below 100

CEngineObject* __fastcall NewObjectVerify(ClientArrayManager* self, void*, int type) {
    CEngineObject* result = VisFogManagerNewObject(self, nullptr, type);
    bool good = true;
    if (result) {
        auto* fog = reinterpret_cast<CVisibilityFog*>(result);
        good = fog->vtable_ == reinterpret_cast<void*>(0x0073D6B4) && fog->refCount_ == 0 && fog->boundsKind == 0 &&
               fog->cells.BucketCount() == kFogBucketCount && fog->cells.Size() == 0 && fog->boundsValid == 0;
    }
    ei::verify::Check("CVisFogManager factory (004E2290)", true, good, "type %d: %p", type, static_cast<void*>(result));
    return result;
}

// The table's tile 5 (column 1, row 1): u 0.25 .. 0.5, v 0.5 .. 0.75, all exact in a float.
int __fastcall CreateVerify(ClientArrayManager* self) {
    int result = VisFogManagerCreate(self);
    const float* tile = reinterpret_cast<const float*>(0x007ABA58) + 5 * 4;
    bool good = tile[0] == 0.25f && tile[1] == 0.75f && tile[2] == 0.5f && tile[3] == 0.5f;
    ei::verify::Check("CVisFogManager::Create (004E2990)", true, good, "result %d, tile 5: %g %g %g %g", result,
                      tile[0], tile[1], tile[2], tile[3]);
    return result;
}

void __fastcall DestroyVerify(ClientArrayManager* self) {
    VisFogManagerDestroy(self);
    bool good = self->current == nullptr && self->list.count == 0 && self->list.head == nullptr &&
                self->list.tail == nullptr && self->list.blocks == nullptr;
    ei::verify::Check("CVisFogManager::Destroy (004E2850)", true, good, "");
}

// The tick only takes fogs out of the list.
int __fastcall TickVerify(ClientArrayManager* self) {
    int before = self->list.count;
    int result = VisFogManagerTick(self);
    ei::verify::Check("CVisFogManager tick (004E2760)", true, result == 0 && self->list.count <= before, "%d -> %d fogs",
                      before, self->list.count);
    return result;
}

CEngineObject* __fastcall AddVerify(ClientArrayManager* self) {
    int before = self->list.count;
    CEngineObject* result = VisFogManagerAdd(self);
    bool good = self->list.count == before + 1 && self->list.tail && self->list.tail->data == result;
    ei::verify::Check("CVisFogManager new fog (004E28D0)", true, good, "%d fogs", self->list.count);
    return result;
}

// The draw leaves the list alone; with no fog it is 1.
int __fastcall DrawVerify(ClientArrayManager* self, void*, CSceneRenderer* renderer) {
    int before = self->list.count;
    int result = VisFogManagerDraw(self, nullptr, renderer);
    ei::verify::Check("CVisFogManager draw (004E2700)", true, self->list.count == before && (before != 0 || result == 1),
                      "%d fogs, result %d", before, result);
    return result;
}

} // namespace

EI_VERIFY(0x004E2290, NewObjectVerify, 0, s_unused);
EI_VERIFY(0x004E2990, CreateVerify, 0, s_unused);
EI_VERIFY(0x004E2850, DestroyVerify, 0, s_unused);
EI_VERIFY(0x004E2760, TickVerify, 0, s_unused);
EI_VERIFY(0x004E28D0, AddVerify, 0, s_unused);
EI_VERIFY(0x004E2700, DrawVerify, 0, s_unused);
