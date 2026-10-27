// The visibility fog's manager (CVisFogManager: ClientArrayManager in world.h). It owns the "VisionFog"
// texture and a list of the fog objects (CVisibilityFog, effects.h; one a player whose sight is drawn). Its
// factory, Create / Destroy, the tick, the new fog and the draw, which binds the texture and has every fog
// draw itself (the fog's own draw through the scene renderer: effects_weather_draw.cpp). Main thread. See
// docs/notes/script.md.
#include "effects.h"
#include "model.h"     // CSceneRenderer
#include "texture.h"   // TextureHolderEnsureLoaded, HeldTexture
#include "world.h"     // ClientArrayManager

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/mfc.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {

using FogNode = mfc::CList<CEngineObject*>::Node;


constexpr int kFogTextureKind = 6;
const char* FogTextureName() { return "VisionFog"; }

// The fog texture's 4 x 4 tiles (FogTiles, effects.h): filled here, read by the fog's draw.
constexpr int kFogTiles = 16;
constexpr float kFogTileSize = 0.25f;


// The manager's current-object slot holds the fog texture.
HeldTexture* TextureHolder(ClientArrayManager* self) { return reinterpret_cast<HeldTexture*>(&self->current); }

} // namespace

// A new CVisibilityFog, whatever the type asked: the engine object's fields, everything up to the cell map
// zero, an empty cell map; null when the allocation fails.
CEngineObject* ClientArrayManager::NewObject(int) { return new CVisibilityFog(); }
EI_REWRITTEN(0x004E2290, ClientArrayManager::NewObject);

// The texture, the tile table, then the manager's own Create (its result).
int __fastcall VisFogManagerCreate(ClientArrayManager* self) {
    TextureHolder(self)->Load(kFogTextureKind, FogTextureName());
    FogTile* tiles = g_fogTiles;
    for (int i = 0; i < kFogTiles; ++i) {
        FogTile& tile = tiles[i];
        tile.u0 = static_cast<float>(i & 3) * kFogTileSize;
        tile.v0 = 1.0f - static_cast<float>(i / 4 + 1) * kFogTileSize;
        tile.u1 = tile.u0 + kFogTileSize;
        tile.v1 = tile.v0 + kFogTileSize;
    }
    return self->CBaseManager::Create();
}
EI_REWRITTEN(0x004E2990, VisFogManagerCreate);

// The texture dropped (null, then released), every fog released, the list emptied.
void __fastcall VisFogManagerDestroy(ClientArrayManager* self) {
    CEngineObject* texture = self->current;
    self->current = nullptr;
    if (texture) texture->Release();
    for (FogNode* node = self->list.head; node;) {
        CEngineObject* fog = node->data;
        node = node->next;
        fog->Release();
    }
    self->list.RemoveAll();
}
EI_REWRITTEN(0x004E2850, VisFogManagerDestroy);

// Every fog's tick; one that returns 0 (no cells left) leaves the list and is released. Returns 0.
int __fastcall VisFogManagerTick(ClientArrayManager* self) {
    for (FogNode* node = self->list.head; node;) {
        FogNode* current = node;
        node = node->next;
        CEngineObject* fog = current->data;
        if (fog->RunTick() != 0) continue;
        self->list.RemoveAt(current);
        fog->Release();
    }
    return 0;
}
EI_REWRITTEN(0x004E2760, VisFogManagerTick);

int ClientArrayManager::Create() { return VisFogManagerCreate(this); }
void ClientArrayManager::Destroy() { VisFogManagerDestroy(this); }
int ClientArrayManager::RunTick() { return VisFogManagerTick(this); }

// A new fog (CreateObject(0, -1): registered and created), counted, at the list's end.
CEngineObject* __fastcall VisFogManagerAdd(ClientArrayManager* self) {
    CEngineObject* fog = self->CreateObject(0, -1);
    ++fog->refCount_;
    self->list.AddTail(fog);
    return fog;
}
EI_REWRITTEN(0x004E28D0, VisFogManagerAdd);

// Draw (called by the world's draw). Nothing listed: 1. Otherwise the texture is made sure of and bound to
// stage 0 (a failed bind: 0), then every fog draws; 1.
int ClientArrayManager::Draw(CSceneRenderer* renderer) {
    if (list.count == 0) return 1;
    TextureHolderEnsureLoaded(TextureHolder(this));
    if (!renderer->SetTexture(0, current)) return 0;
    for (FogNode* node = list.head; node;) {
        CEngineObject* fog = node->data;
        node = node->next;
        static_cast<CVisibilityFog*>(fog)->Draw(renderer);   // its result not read
    }
    return 1;
}
EI_REWRITTEN(0x004E2700, ClientArrayManager::Draw);
