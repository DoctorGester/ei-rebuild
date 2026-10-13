// Checks for the small calls of the two worlds (src/game/world_small_calls.cpp, A-W14).
// - NewTexture / NewMaterial (005655D0 / 005655F0), ours only (they create objects): null without
//   the manager. Reached through the scene loader's texture lookups (the originals 0051B2F0 /
//   0051B470) when a unit's figure gets its textures.
// - SetCamera (00565670; MOV EAX,[ECX+0x210]: 6 bytes), differential: the original first, its
//   results noted and the camera and both counts put back, then ours. Reached by the game start
//   (00456440, patched by Rebirth, so the original runs it) in every scenario that enters a zone.
// - SetWorldValue (00566C10; MOV EAX,[ESP+4]; MOV [ECX+0x7C0],EAX: 10 bytes), differential the same
//   way. Message 0x12 (CPlayerClient::OnWorldValue); no scenario is known to send it.
// - ReadWaterLevels (005683B0), ours only (it consumes the stream): returns 1. Message 0x73
//   (CPlayerClient::OnWorldStream): a multiplayer client only, no scenario.
// - CWorldServer::SetWaterLevel (00593FA0), ours only (a new key grows the array): the key's entry
//   is there with the steps asked for. The script function SetWaterLevel; no scenario's zone
//   script is known to call it.
#include "verify.h"

#include "../../src/game/camera.h"
#include "../../src/game/model.h"
#include "../../src/game/world.h"

namespace {
void* s_unused;

CTexture* __fastcall NewTextureVerify(CWorldClient* self, void*, int type) {
    CTexture* result = self->NewTexture(type);
    ei::verify::Check("CWorldClient::NewTexture", true, self->textureManager_ != nullptr || result == nullptr, "type %X: %p",
                      type, static_cast<void*>(result));
    return result;
}

CMaterial* __fastcall NewMaterialVerify(CWorldClient* self, void*, int type) {
    CMaterial* result = self->NewMaterial(type);
    ei::verify::Check("CWorldClient::NewMaterial", true, self->manager188_ != nullptr || result == nullptr, "type %X: %p",
                      type, static_cast<void*>(result));
    return result;
}

int Count(const CCamera* camera) { return camera ? camera->refCount_ : 0; }

void(__fastcall* s_setCamera)(CWorldClient*, void*, CCamera*);
void __fastcall SetCameraVerify(CWorldClient* self, void*, CCamera* camera) {
    CCamera* old = self->camera_;
    int oldCount = Count(old);
    int newCount = Count(camera);
    s_setCamera(self, nullptr, camera);
    CCamera* expected = self->camera_;
    int expectedOld = Count(old);
    int expectedNew = Count(camera);
    self->camera_ = old;
    if (old) old->refCount_ = oldCount;
    if (camera) camera->refCount_ = newCount;
    self->SetCamera(camera);
    ei::verify::Check("CWorldClient::SetCamera", true,
                      expected == self->camera_ && expectedOld == Count(old) && expectedNew == Count(camera),
                      "%p -> %p: counts %d/%d, expected %d/%d", static_cast<void*>(old), static_cast<void*>(camera),
                      Count(old), Count(camera), expectedOld, expectedNew);
}

void(__fastcall* s_setWorldValue)(CWorldClient*, void*, int);
void __fastcall SetWorldValueVerify(CWorldClient* self, void*, int value) {
    int before = self->worldValue_;
    s_setWorldValue(self, nullptr, value);
    int expected = self->worldValue_;
    self->worldValue_ = before;
    self->SetWorldValue(value);
    ei::verify::Check("CWorldClient::SetWorldValue", expected, self->worldValue_, "value %d", value);
}

int __fastcall ReadWaterLevelsVerify(CWorldClient* self, void*, CStream* stream) {
    int result = self->ReadWaterLevels(stream);
    ei::verify::Check("CWorldClient::ReadWaterLevels", 1, result, "world %p", static_cast<void*>(self));
    return result;
}

void __fastcall SetWaterLevelVerify(CWorldServer* self, void*, int key, float target, int steps) {
    self->SetWaterLevel(key, target, steps);
    const auto* levels = reinterpret_cast<const TweenArray*>(self->countdowns_);
    bool ok = false;
    for (int i = 0; i < levels->size; ++i) {
        if (levels->data[i].key != key) continue;
        ok = levels->data[i].steps == (steps < 1 ? 1 : steps);
        break;
    }
    ei::verify::Check("CWorldServer::SetWaterLevel", true, ok, "key %d target %g steps %d", key, target, steps);
}

} // namespace

EI_VERIFY(0x005655D0, NewTextureVerify, 0, s_unused);
EI_VERIFY(0x005655F0, NewMaterialVerify, 0, s_unused);
EI_VERIFY(0x00565670, SetCameraVerify, 6, s_setCamera);
EI_VERIFY(0x00566C10, SetWorldValueVerify, 10, s_setWorldValue);
EI_VERIFY(0x005683B0, ReadWaterLevelsVerify, 0, s_unused);
EI_VERIFY(0x00593FA0, SetWaterLevelVerify, 0, s_unused);
