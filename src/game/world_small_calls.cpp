// Small calls of the two worlds that other code reaches through them (A-W14): the client's new
// texture / material, its camera, the value of message 0x12 and the water levels of message 0x73;
// the server's SetWaterLevel. See world.h and docs/notes/world.md.
#include "world.h"

#include "camera.h"
#include "material.h"
#include "model.h"
#include "texture.h"

#include "ei/hook.h"

CTexture* CWorldClient::NewTexture(int type) {
    if (!textureManager_) return nullptr;
    return textureManager_->NewTextureOfType(type);
}

CMaterial* CWorldClient::NewMaterial(int type) {
    if (!manager188_) return nullptr;
    return static_cast<CMaterialManager*>(manager188_)->NewMaterial(type);
}

void CWorldClient::SetCamera(CCamera* camera) {
    if (camera_) --camera_->refCount_;
    camera_ = camera;
    if (camera) ++camera->refCount_;
}

void CWorldClient::SetWorldValue(int value) { worldValue_ = value; }

int CWorldClient::ReadWaterLevels(CStream* stream) {
    countdowns_.Load(stream, terrain_);
    return 1;
}

void CWorldServer::SetWaterLevel(int key, float target, int steps) {
    countdowns_.SetTarget(key, target, steps);
}

EI_REPLACE_METHOD(0x005655D0, CWorldClient::NewTexture);
EI_REPLACE_METHOD(0x005655F0, CWorldClient::NewMaterial);
EI_REPLACE_METHOD(0x00565670, CWorldClient::SetCamera);
EI_REPLACE_METHOD(0x00566C10, CWorldClient::SetWorldValue);
EI_REPLACE_METHOD(0x005683B0, CWorldClient::ReadWaterLevels);
EI_REPLACE_METHOD(0x00593FA0, CWorldServer::SetWaterLevel);
