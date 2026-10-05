// Checks for the .bmp textures (texture_bmp.cpp).
#include "verify.h"

#include "../../src/game/texture.h"
#include "../../src/render/render.h"

// 0068FBF0 reads files and makes DirectDraw textures (or the new renderer's memory): ours only. A
// texture loaded before stays as it was (1); otherwise 1 means loaded, with a holder of its size
// (width_ x height_, the .bmp's) in the texture's format: in the switch-over with "cpuwindows" only
// the new renderer's memory of that size, otherwise a DirectDraw texture. 0 (the .bmp not found)
// leaves it unloaded. No scenario has a loose textures\<name>.bmp (the installs' textures folders
// are empty): not reached; ChannelLayout::ToNative, its pixel conversion, is checked against
// 004EDC00 from 00690430's wrapper.
static void* s_loadBmpFiles;
static int __fastcall LoadBmpFilesVerify(CTexture* self, void*, CTextureDevice* device, const char* path,
                                         const char* alphaPath) {
    bool wasLoaded = (self->flags_ & kTextureLoaded) != 0;
    int result = self->LoadBmpFiles(device, path, alphaPath);
    const CDXSurface* holder = self->surface_;
    bool ok;
    if (wasLoaded) {
        ok = result == 1;
    } else if (result == 1) {
        ok = (self->flags_ & kTextureLoaded) && holder && holder->width == static_cast<int>(self->width_) &&
             holder->height == static_cast<int>(self->height_) &&
             holder->bitsPerPixel == static_cast<int>(self->format_.words[3]);
        const render::SurfaceMemory* memory = holder ? render::SurfaceMemoryOf(holder) : nullptr;
        if (ok && holder->surface)
            ok = !memory;
        else if (ok)
            ok = render::Presenting() && memory && memory->width == holder->width && memory->height == holder->height;
    } else {
        ok = result == 0 && !(self->flags_ & kTextureLoaded);
    }
    ei::verify::Check("CTexture::LoadBmpFiles", true, ok, "\"%s\" result %d holder %p", path, result,
                      static_cast<const void*>(holder));
    return result;
}
EI_VERIFY(0x0068FBF0, LoadBmpFilesVerify, 0, s_loadBmpFiles);
