// Small calls of the two worlds that other code reaches through them (A-W14): the client's new
// texture / material, its camera, the value of message 0x12 and the water levels of message 0x73;
// the server's SetWaterLevel. See world.h and docs/notes/world.md.
#include "world.h"

#include "camera.h"
#include "material.h"
#include "model.h"
#include "texture.h"

CTexture* WorldClient::NewTexture(int textureType) {
    if (!textureManager) return nullptr;
    return textureManager->NewTextureOfType(textureType);
}

Material* WorldClient::NewMaterial(int materialType) {
    if (!materialManager) return nullptr;
    return materialManager->NewMaterial(materialType);
}

void WorldClient::SetCamera(Camera* newCamera) {
    if (camera) --camera->refCount;
    camera = newCamera;
    if (newCamera) newCamera->AddRef();
}

void WorldClient::SetWorldValue(int value) { worldValue = value; }

int WorldClient::ReadWaterLevels(ByteStream* stream) {
    countdowns.Load(stream, terrain);
    return 1;
}

void WorldServer::SetWaterLevel(int key, float target, int steps) {
    countdowns.SetTarget(key, target, steps);
}
