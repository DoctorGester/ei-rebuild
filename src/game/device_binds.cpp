// The device object's texture binds: a surface holder, a texture, a texture kept by name. See
// docs/notes/renderer.md (section 12).
#include "app.h"
#include "texture.h"

#include "ground_print.h"       // NamedTexture
#include "game_singletons.h"
#include "world.h"

#include "lib/gameapi.h"

int CTextureDevice::BindSurface(int stage, Surface* holder) {
    if (stageSurfaces_[stage] == holder) return 1;
    stageSurfaces_[stage] = holder;
    ++textureChanges_;
    return 1;
}

// The same body as BindSurface's, for a texture and for none.
int CTextureDevice::BindTexture(int stage, CTexture* texture) {
    return BindSurface(stage, texture ? texture->surface_ : nullptr);
}

int CTextureDevice::BindNamedTexture(int stage, NamedTexture* named) {
    if (!named->texture && named->name.size() != 0) {
        CTexture* found = g_clientWorld->textureManager_->FindTexture(named->name.data());
        CTexture* previous = named->texture;
        named->texture = found;
        if (found) ++found->refCount_;
        if (previous) previous->Release();
    }
    return BindSurface(stage, named->texture ? named->texture->surface_ : nullptr);
}
