// Checks for the renderer's rectangle (renderer_rect.cpp).
#include "verify.h"

#include "../../src/game/model.h"
#include "../../src/game/texture.h"

// 005041F0 draws with Direct3D: ours only. The result is 0 for an inverted rectangle (nothing
// counted), else 1 with one draw call (two with the outline) counted on the device object, stage
// 0 without a texture.
static void* s_rectangle;
static int __fastcall RendererRectangleVerify(CSceneRenderer* renderer, void*, int left, int top, int right, int bottom,
                                              char alpha, char outline) {
    CTextureDevice* device = renderer->device_;
    int calls = device->drawCount_;
    int result = renderer->DrawRectangleImpl(left, top, right, bottom, alpha, outline);
    int drawn = device->drawCount_ - calls;
    bool inverted = right < left || bottom < top;
    bool ok = inverted ? result == 0 && drawn == 0
                       : result == 1 && drawn == (outline ? 2 : 1) && device->stageSurfaces_[0] == nullptr;
    ei::verify::Check("RendererRectangle", true, ok, "result %d draws %d (%d,%d)-(%d,%d) outline %d", result, drawn, left, top,
                      right, bottom, outline);
    return result;
}
EI_VERIFY(0x005041F0, RendererRectangleVerify, 0, s_rectangle);
