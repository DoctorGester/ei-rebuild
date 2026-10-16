// The visibility fog's manager (CVisFogManager: ClientArrayManager in world.h, vtable 0073F554,
// the client world's +0x1A4; 004E2290-004E2A15, A-S11). It owns the "VisionFog" texture and a list
// of the fog objects (CVisibilityFog, effects.h; one a player whose sight is drawn). Rewritten: its
// factory (slot 14), Create / Destroy (slots 2 / 3), the tick (slot 6), the new fog (004E28D0) and
// the draw (004E2700), which binds the texture and has every fog draw itself (004E1EB0, the fog's
// own draw through the scene renderer: effects_weather_draw.cpp). Main thread. See
// docs/notes/script.md (A-S11).
#include "effects.h"
#include "model.h"     // CSceneRenderer
#include "texture.h"   // TextureHolderEnsureLoaded, TextureHolderLoad
#include "world.h"     // ClientArrayManager

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "lib/mfc.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#pragma warning(disable : 4740) // naked forwarders

namespace {

using FogNode = mfc::CList<CEngineObject*>::Node;


constexpr int kFogTextureKind = 6;
const char* FogTextureName() { return reinterpret_cast<const char*>(0x0078ADAC); }   // "VisionFog"

// The fog texture's 4 x 4 tiles (FogTiles, effects.h): filled here, read by the fog's draw.
constexpr int kFogTiles = 16;
constexpr float kFogTileSize = 0.25f;

constexpr std::size_t kFogBuckets = 100;   // the hint the cell map is created with

CTexture** TextureHolder(ClientArrayManager* self) { return reinterpret_cast<CTexture**>(&self->current); }

} // namespace

// 004E2290 (slot 14, RET 4): a new CVisibilityFog, whatever the type asked: the engine object's
// fields, everything up to the cell map zero, an empty cell map; null when the allocation fails.
CEngineObject* ClientArrayManager::NewObjectImpl(int) {
    void* memory = gamecrt::operator_new(sizeof(CVisibilityFog));
    if (!memory) return nullptr;
    auto* fog = static_cast<CVisibilityFog*>(memory);
    auto* object = reinterpret_cast<CEngineObject*>(fog);
    ei::SetVtable(fog, CEngineObject::kVtableAddress);
    object->ClearFields();
    std::memset(&fog->boundsKind, 0, offsetof(CVisibilityFog, cells) - offsetof(CVisibilityFog, boundsKind));
    fog->cells.InitBuckets(kFogBuckets);
    ei::SetVtable(fog, 0x0073D6B4);
    fog->boundsValid = 0;
    return object;
}
EI_REPLACE_METHOD(0x004E2290, ClientArrayManager::NewObjectImpl);

// 004E2990 (slot 2, Create): the texture, the tile table, then the manager's own Create (its
// result).
int __fastcall VisFogManagerCreate(ClientArrayManager* self) {
    TextureHolderLoad(TextureHolder(self), nullptr, kFogTextureKind, FogTextureName());
    FogTile* tiles = FogTiles();
    for (int i = 0; i < kFogTiles; ++i) {
        FogTile& tile = tiles[i];
        tile.u0 = static_cast<float>(i & 3) * kFogTileSize;
        tile.v0 = 1.0f - static_cast<float>(i / 4 + 1) * kFogTileSize;
        tile.u1 = tile.u0 + kFogTileSize;
        tile.v1 = tile.v0 + kFogTileSize;
    }
    return self->CBaseManager::Create();
}
EI_REPLACE(0x004E2990, VisFogManagerCreate);

// 004E2850 (slot 3, Destroy; the destructor 00562DF0 calls it too): the texture dropped (null,
// then released), every fog released, the list emptied.
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
EI_REPLACE(0x004E2850, VisFogManagerDestroy);

// 004E2760 (slot 6, the tick): every fog's tick; one that returns 0 (no cells left) leaves the list
// and is released. Returns 0.
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
EI_REPLACE(0x004E2760, VisFogManagerTick);

// 004E28D0: a new fog (CreateObject(0, -1): registered and created), counted, at the list's end.
CEngineObject* __fastcall VisFogManagerAdd(ClientArrayManager* self) {
    CEngineObject* fog = self->CreateObject(0, -1);
    ++fog->refCount_;
    self->list.AddTail(fog);
    return fog;
}
EI_REPLACE(0x004E28D0, VisFogManagerAdd);

// 004E2700 (RET 4; the world's draw 00569E40). Nothing listed: 1. Otherwise the texture is made
// sure of and bound to stage 0 (a failed bind: 0), then every fog draws; 1.
int ClientArrayManager::DrawImpl(CSceneRenderer* renderer) {
    if (list.count == 0) return 1;
    TextureHolderEnsureLoaded(TextureHolder(this));
    if (!renderer->SetTexture(0, current)) return 0;
    for (FogNode* node = list.head; node;) {
        CEngineObject* fog = node->data;
        node = node->next;
        reinterpret_cast<CVisibilityFog*>(fog)->Draw(renderer);   // 004E1EB0 (its result not read)
    }
    return 1;
}
EI_REPLACE_METHOD(0x004E2700, ClientArrayManager::DrawImpl);
