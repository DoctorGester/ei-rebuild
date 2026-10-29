// The visibility fog's manager (CVisFogManager: ClientArrayManager in world.h). It owns the "VisionFog"
// texture and a list of the fog objects (VisibilityFog, effects.h; one a player whose sight is drawn). Its
// factory, Create / Destroy, the tick, the new fog and the draw, which binds the texture and has every fog
// draw itself (the fog's own draw through the scene renderer: effects_weather_draw.cpp). Main thread. See
// docs/notes/script.md.
#include "effects.h"
#include "model.h"     // SceneRenderer
#include "texture.h"
#include "world.h"     // ClientArrayManager

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {


constexpr int kFogTextureKind = static_cast<int>(TextureKind::Effects);
const char* FogTextureName() { return "VisionFog"; }

// The fog texture's 4 x 4 tiles (FogTiles, effects.h): filled here, read by the fog's draw.
constexpr int kFogTiles = 16;
constexpr float kFogTileSize = 0.25f;


} // namespace

// A new VisibilityFog, whatever the type asked: the engine object's fields, everything up to the cell map
// zero, an empty cell map; null when the allocation fails.
EngineObject* ClientArrayManager::NewObject(int) { return new VisibilityFog(); }

// The texture, the tile table, then the manager's own Create.
void ClientArrayManager::Create() {
    LoadNamedTexture(texture, kFogTextureKind, FogTextureName());
    FogTile* tiles = g_fogTiles;
    for (int i = 0; i < kFogTiles; ++i) {
        FogTile& tile = tiles[i];
        tile.u0 = static_cast<float>(i & 3) * kFogTileSize;
        tile.v0 = 1.0f - static_cast<float>(i / 4 + 1) * kFogTileSize;
        tile.u1 = tile.u0 + kFogTileSize;
        tile.v1 = tile.v0 + kFogTileSize;
    }
    BaseManager::Create();
}

// The texture dropped (null, then released), every fog released, the list emptied.
void VisFogManagerDestroy(ClientArrayManager* self) {
    self->texture.Reset();
    for (EngineObject* fog : self->list) fog->Release();
    self->list.clear();
}

// Every fog's tick; one that returns false (no cells left) leaves the list and is released.
bool ClientArrayManager::RunTick() {
    std::erase_if(list, [](EngineObject* fog) {
        if (fog->RunTick()) return false;
        fog->Release();
        return true;
    });
    return false;
}

void ClientArrayManager::Destroy() { VisFogManagerDestroy(this); }

// A new fog (CreateObject(0, -1): registered and created), counted, at the list's end.
EngineObject* VisFogManagerAdd(ClientArrayManager* self) {
    EngineObject* fog = self->CreateObject(0, -1);
    fog->AddRef();
    self->list.push_back(fog);
    return fog;
}

// Draw (called by the world's draw). Nothing listed: 1. Otherwise the texture is made sure of and bound to
// stage 0 (a failed bind: 0), then every fog draws; 1.
int ClientArrayManager::Draw(SceneRenderer* renderer) {
    if (list.empty()) return 1;
    EnsureTextureLoaded(texture);
    if (!renderer->SetTexture(0, texture)) return 0;
    for (EngineObject* fog : list) static_cast<VisibilityFog*>(fog)->Draw(renderer);   // its result not read
    return 1;
}

// --- Class name and id ------------------------------------------------------------------------------

namespace {
constexpr const char* kClientArrayManagerClassName = "CVisFogManager";
} // namespace

const char* ClientArrayManager::ClassName() { return kClientArrayManagerClassName; }
ClassId ClientArrayManager::GetClassId() { return ClassId::ClientArrayManager; }
