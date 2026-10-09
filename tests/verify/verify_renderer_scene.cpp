// Checks for renderer_scene.cpp (A-U10). All three call the Direct3D device: ours only.
// - BeginScene (00503F40): 0 unless flags_ bit 0; else 1, the frame's start remembered, its time
//   the start minus the previous one.
// - EndScene (005040A0): 1 and the previous start becomes this frame's.
// - DrawTexture (00504860): 0 without a texture; else 1 and four vertices, a primitive and a draw
//   counted.
// Reached by every frame (every scenario); slot 21 has no known caller in the scenarios.
#include "verify.h"

#include "../../src/game/model.h"
#include "../../src/game/texture.h"

namespace {
void* s_unusedBegin;
void* s_unusedEnd;
void* s_unusedTexture;
} // namespace

static int __fastcall BeginSceneVerify(CSceneRenderer* renderer, void*) {
    std::uint32_t previous = renderer->lastFrameStart_;
    bool active = (renderer->flags_ & 1) != 0;
    int result = renderer->BeginScene();
    bool ok = active ? result == 1 && renderer->frameTime_ == renderer->frameStart_ - previous : result == 0;
    ei::verify::Check("CSceneRenderer::BeginScene", true, ok, "result %d, frame %u ms", result, renderer->frameTime_);
    return result;
}
EI_VERIFY(0x00503F40, BeginSceneVerify, 0, s_unusedBegin);

static int __fastcall EndSceneVerify(CSceneRenderer* renderer, void*) {
    int result = renderer->EndScene();
    bool ok = result == 1 && renderer->lastFrameStart_ == renderer->frameStart_;
    ei::verify::Check("CSceneRenderer::EndScene", true, ok, "result %d", result);
    return result;
}
EI_VERIFY(0x005040A0, EndSceneVerify, 0, s_unusedEnd);

static int __fastcall DrawTextureVerify(CSceneRenderer* renderer, void*, CTexture* texture, float x, float y,
                                        float scale) {
    CTextureDevice* device = renderer->device_;
    int vertices = device->vertexCount_;
    int primitives = device->primitiveCount_;
    int draws = device->drawCount_;
    int result = renderer->DrawTexture(texture, x, y, scale);
    bool ok = texture ? result == 1 && device->vertexCount_ == vertices + 4 && device->primitiveCount_ == primitives + 1 &&
                            device->drawCount_ == draws + 1
                      : result == 0;
    ei::verify::Check("CSceneRenderer::DrawTexture", true, ok, "result %d at (%g, %g) x %g", result, x, y, scale);
    return result;
}
EI_VERIFY(0x00504860, DrawTextureVerify, 0, s_unusedTexture);
