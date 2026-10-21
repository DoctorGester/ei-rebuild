// The device object's texture binds (004EB300 a surface holder, 004EBAF0 a texture, 004EB1D0 a
// texture kept by name) and its two vertex-buffer batch flushes (004E7840, 004E7A40: nothing to
// draw). See docs/notes/renderer.md (section 12).
#include "app.h"
#include "device.h"
#include "game_strings.h"

#include "ground_print.h"       // NamedTexture
#include "originals_engine.h"
#include "game_singletons.h"

#include "ei/hook.h"
#include "lib/gameapi.h"

// 004E7840 / 004E7A40: the device's two vertex-buffer batches drawn. 004E7820 never gave a batch a
// buffer, so nothing was ever gathered and the flushes do nothing (their callers go in R9b4).
void CTextureDevice::FlushBatch() {}
EI_REPLACE_METHOD(0x004E7840, CTextureDevice::FlushBatch);

void CTextureDevice::FlushBatch2() {}
EI_REPLACE_METHOD(0x004E7A40, CTextureDevice::FlushBatch2);

int CTextureDevice::BindSurface(int stage, CDXSurface* holder) {
    if (stageSurfaces_[stage] == holder) return 1;
    stageSurfaces_[stage] = holder;
    FlushBatch();
    // (No holder has a DirectDraw surface: the stage's texture is null on the switch-over; the new
    // renderer draws with the holder in stageSurfaces_.)
    lastLoadResult_ = device::SetTexture(device::D3D(this), static_cast<DWORD>(stage), nullptr);
    ++textureChanges_;
    return 1;
}
EI_REPLACE_METHOD(0x004EB300, CTextureDevice::BindSurface);

// The original repeats BindSurface's body for a texture and for none.
int CTextureDevice::BindTexture(int stage, CTexture* texture) {
    return BindSurface(stage, texture ? texture->surface_ : nullptr);
}
EI_REPLACE_METHOD(0x004EBAF0, CTextureDevice::BindTexture);

int CTextureDevice::BindNamedTexture(int stage, NamedTexture* named) {
    if (!named->texture && named->name.size() != 0) {
        CTexture* found = TheTextureManager()->FindTexture(named->name.data());
        CTexture* previous = named->texture;
        named->texture = found;
        if (found) ++found->refCount_;
        if (previous) previous->Release();
    }
    return BindSurface(stage, named->texture ? named->texture->surface_ : nullptr);
}
EI_REPLACE_METHOD(0x004EB1D0, CTextureDevice::BindNamedTexture);
