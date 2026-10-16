// The weather's draw (004E5680, thiscall on the weather object, RET 4; called by the world view's
// effects pass 00569E40 with the renderer 007AF060). Mode (0 none, 1 rain, 2 snow): the weather is
// stepped (004E40B0, with the world's tick), its texture bound on stage 0, and the rain (004E4240)
// or snow (004E4C80) drawn. Those two build their particles' pre-transformed vertices (vertices,
// indices) and draw them with the device directly; the new renderer (src/render) gets the same
// triangles after they return. See docs/notes/effects.md and docs/notes/renderer.md.
#include "effects.h"   // CRainSnow
#include "model.h"     // CSceneRenderer
#include "world.h"     // CWorld

#include "ei/hook.h"
#include "render/render.h"
#include "originals_render.h"

#include <cstdint>

namespace {

// The triangles the rain or snow just drew (if any) to the new renderer.
void HandOver(CRainSnow* weather, CSceneRenderer* renderer) {
    int count = weather->vertices.size;
    TLVertex* vertices = weather->vertices.data;
    std::uint16_t* indices = weather->indices.data;
    if (count <= 0 || !vertices || !indices) return;
    render::TlDraw(renderer->device_, vertices, 0x1C4, count, indices, count, 4);
}

} // namespace

// 004E5680. Returns what the rain / snow draw returned, 1 when there is nothing to draw, 0 without
// a world or with an unknown mode.
int CRainSnow::DrawImpl(CSceneRenderer* renderer) {
    CWorld* currentWorld = world;
    if (!currentWorld) return 0;
    if (this->mode == 0) return 1;
    UpdateFade(static_cast<float>(currentWorld->tick_));
    if (this->mode == 0) return 1;
    TextureEnsureLoadedOriginal(&texture);
    renderer->SetTexture(0, texture);
    int currentMode = mode;
    int result;
    if (currentMode == 1) result = DrawRain(renderer);
    else if (currentMode == 2) result = DrawSnow(renderer);
    else return 0;
    if (render::Active()) HandOver(this, renderer);
    return result;
}
EI_REPLACE_METHOD(0x004E5680, CRainSnow::DrawImpl);
