// Checks for device_binds.cpp (A-S13). Main thread.
//  - Side by side: the texture binds 004EB300 (a surface holder), 004EBAF0 (a texture) and 004EB1D0
//    (a texture kept by name). The original runs first, the stage's holder, the device's last result
//    and its count of texture changes are put back, then ours runs; the three and the result are
//    compared (the same texture set twice on a stage changes nothing in Direct3D). For 004EB1D0 the
//    texture the original looked up is forgotten in between, and the reference ours takes for the
//    second look-up is dropped afterwards. Reached by every scene (004EB300: any draw with a
//    texture) and by the ground prints (004EB1D0). No scenario reaches 004EBAF0: only the device's
//    multi-pass material draw calls it, for a stage whose texture is still to be found by name.
//  - Ours only: the batch flushes 004E7840 / 004E7A40. No vertex buffer is ever locked (004E7820
//    returns none), so only their first test runs, in the original as well; the wrapper checks that
//    the batch is left without a buffer. Reached by every texture change and frame part.
//  - The first bind or flush also writes the device object's state to the log, once
//    (verify_device_state.h).
#include "verify.h"
#include "verify_device_state.h"

#include "../../src/game/ground_print.h"
#include "../../src/game/texture.h"

#include <cstdint>

namespace {
void* s_unused;

// What a bind changes besides Direct3D's own state.
struct StageState {
    CDXSurface* holder;
    long result;
    int changes;
    bool operator==(const StageState&) const = default;
};
StageState Capture(const CTextureDevice* device, int stage) {
    return {device->stageSurfaces_[stage], static_cast<long>(device->lastLoadResult_), device->textureChanges_};
}
void Restore(CTextureDevice* device, int stage, const StageState& state) {
    device->stageSurfaces_[stage] = state.holder;
    device->lastLoadResult_ = state.result;
    device->textureChanges_ = state.changes;
}

// 004EB300: PUSH EBX (1), MOV EBX,[ESP+0xC] (4).
int(__fastcall* s_bindSurface)(CTextureDevice*, void*, int, CDXSurface*);
int __fastcall BindSurfaceVerify(CTextureDevice* self, void*, int stage, CDXSurface* holder) {
    DumpDeviceStateOnce(self);
    const StageState before = Capture(self, stage);
    const int expected = s_bindSurface(self, nullptr, stage, holder);
    const StageState original = Capture(self, stage);
    Restore(self, stage, before);
    const int result = self->BindSurface(stage, holder);
    ei::verify::Check("CTextureDevice::BindSurface (004EB300)", true,
                      result == expected && Capture(self, stage) == original, "stage %d holder %p result %d / %d",
                      stage, static_cast<void*>(holder), expected, result);
    return result;
}

// 004EBAF0: MOV EAX,[ESP+8] (4), PUSH ESI (1).
int(__fastcall* s_bindTexture)(CTextureDevice*, void*, int, CTexture*);
int __fastcall BindTextureVerify(CTextureDevice* self, void*, int stage, CTexture* texture) {
    const StageState before = Capture(self, stage);
    const int expected = s_bindTexture(self, nullptr, stage, texture);
    const StageState original = Capture(self, stage);
    Restore(self, stage, before);
    const int result = self->BindTexture(stage, texture);
    ei::verify::Check("CTextureDevice::BindTexture (004EBAF0)", true,
                      result == expected && Capture(self, stage) == original, "stage %d texture %p result %d / %d",
                      stage, static_cast<void*>(texture), expected, result);
    return result;
}

// 004EB1D0: PUSH ESI (1), PUSH EDI (1), MOV EDI,[ESP+0x10] (4).
int(__fastcall* s_bindNamed)(CTextureDevice*, void*, int, NamedTexture*);
int __fastcall BindNamedTextureVerify(CTextureDevice* self, void*, int stage, NamedTexture* named) {
    CTexture* const heldBefore = named->texture;
    const StageState before = Capture(self, stage);
    const int expected = s_bindNamed(self, nullptr, stage, named);
    CTexture* const heldOriginal = named->texture;
    const StageState original = Capture(self, stage);
    // The original's look-up is forgotten (its reference stays the record's own).
    const bool lookedUp = !heldBefore && heldOriginal;
    if (lookedUp) named->texture = nullptr;
    Restore(self, stage, before);
    const int result = self->BindNamedTexture(stage, named);
    const bool sameTexture = named->texture == heldOriginal;
    ei::verify::Check("CTextureDevice::BindNamedTexture (004EB1D0)", true,
                      result == expected && sameTexture && Capture(self, stage) == original,
                      "stage %d name '%s' texture %p / %p", stage, named->name.data(),
                      static_cast<void*>(heldOriginal), static_cast<void*>(named->texture));
    if (lookedUp) {
        if (sameTexture) {
            --heldOriginal->refCount_;   // ours counted the same texture again
        } else {
            if (named->texture) named->texture->Release();
            named->texture = heldOriginal;
        }
    }
    return result;
}

bool Emptied(const DeviceVertexBatch& batch, bool locked) {
    return !batch.buffer && (!locked || (batch.vertexCount == 0 && batch.indices.size == 0));
}

void __fastcall FlushBatchVerify(CTextureDevice* self) {
    DumpDeviceStateOnce(self);
    const bool locked = self->batch_.buffer != nullptr;
    self->FlushBatch();
    ei::verify::Check("CTextureDevice::FlushBatch (004E7840)", true, Emptied(self->batch_, locked), "locked %d",
                      static_cast<int>(locked));
}

void __fastcall FlushBatch2Verify(CTextureDevice* self) {
    const bool locked = self->batch2_.buffer != nullptr;
    self->FlushBatch2();
    ei::verify::Check("CTextureDevice::FlushBatch2 (004E7A40)", true, Emptied(self->batch2_, locked), "locked %d",
                      static_cast<int>(locked));
}

} // namespace

EI_VERIFY(0x004EB300, BindSurfaceVerify, 5, s_bindSurface);
EI_VERIFY(0x004EBAF0, BindTextureVerify, 5, s_bindTexture);
EI_VERIFY(0x004EB1D0, BindNamedTextureVerify, 6, s_bindNamed);
EI_VERIFY(0x004E7840, FlushBatchVerify, 0, s_unused);
EI_VERIFY(0x004E7A40, FlushBatch2Verify, 0, s_unused);
