// Checks for the renderer's creation (renderer_init.cpp).
#include "verify.h"

#include "../../src/game/model.h"
#include "../../src/game/options.h"

#include <cstdint>

void __fastcall RendererCreate(CSceneRenderer* renderer, void*, void* window, options::GameOptions* settings);

// 00500B70 creates the Direct3D device and sets its state once at start-up: ours only. Checked: the
// device holder (+0x20) and the helper (+0x24) are set, the projection stack (pipeline stack 6) has
// an entry, and flag 2 of +4 is set.
static void* s_rendererCreate;
static void __fastcall RendererCreateVerify(CSceneRenderer* renderer, void*, void* window, options::GameOptions* settings) {
    RendererCreate(renderer, nullptr, window, settings);
    CTransformPipeline* pipeline = renderer->pipeline_;
    bool ok = renderer->device_ && renderer->helper_ && pipeline && pipeline->stacks_[6].count >= 1 &&
              (renderer->flags_ & 2) != 0;
    ei::verify::Check("RendererCreate", true, ok, "device %p helper %p", static_cast<void*>(renderer->device_),
                      static_cast<void*>(renderer->helper_));
}
EI_VERIFY(0x00500B70, RendererCreateVerify, 0, s_rendererCreate);
