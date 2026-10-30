// The device object's texture binds: a surface holder, a texture, a texture kept by name. See
// docs/notes/renderer.md (section 12).
#include "app.h"
#include "texture.h"

#include "ground_print.h"       // NamedTexture
#include "game_singletons.h"
#include "world.h"

int TextureDevice::BindSurface(int stage, Surface* holder) {
    stageSurfaces[stage] = holder;
    return 1;
}

// The same body as BindSurface's, for a texture and for none.
int TextureDevice::BindTexture(int stage, Texture* texture) {
    return BindSurface(stage, texture ? texture->surface.Get() : nullptr);
}

int TextureDevice::BindNamedTexture(int stage, NamedTexture* named) {
    if (!named->texture && named->name.size() != 0) {
        Texture* found = g_clientWorld.textureManager->FindTexture(named->name.data());
        named->texture.Assign(found);
    }
    return BindSurface(stage, named->texture ? named->texture->surface.Get() : nullptr);
}
