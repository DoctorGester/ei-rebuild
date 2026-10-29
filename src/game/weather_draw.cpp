// The weather's draw (on the weather object; called by the world view's effects pass with the renderer). Mode
// (0 none, 1 rain, 2 snow): the weather is stepped (with the world's tick), its texture bound on stage 0, and
// the rain or snow drawn. Those two build their particles' pre-transformed vertices (vertices, indices) and
// hand them to the new renderer (src/render; CRainSnow::DrawParticles). See docs/notes/effects.md and
// docs/notes/renderer.md.
#include "effects.h"   // CRainSnow
#include "texture.h"
#include "model.h"     // CSceneRenderer
#include "world.h"     // CWorld

// Returns what the rain / snow draw returned, 1 when there is nothing to draw, 0 without
// a world or with an unknown mode.
int CRainSnow::Draw(CSceneRenderer* renderer) {
    CWorld* currentWorld = world;
    if (!currentWorld) return 0;
    if (this->mode == 0) return 1;
    UpdateFade(static_cast<float>(currentWorld->tick));
    if (this->mode == 0) return 1;
    EnsureTextureLoaded(texture);
    renderer->SetTexture(0, texture);
    int currentMode = mode;
    int result;
    if (currentMode == 1) result = DrawRain(renderer);
    else if (currentMode == 2) result = DrawSnow(renderer);
    else return 0;
    return result;
}
