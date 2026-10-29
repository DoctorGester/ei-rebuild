// The renderer's scene bracket: BeginScene with the last frame's time, EndScene closing the frame. The main loop
// brackets each frame's drawing with them.
#include "model.h"     // SceneRenderer
#include "texture.h"   // TextureDevice, CTexture

#include "core_maths.h"
#include "game_singletons.h"

#include "lib/gameapi.h"
#include <cstdint>

int SceneRenderer::BeginScene() {
    if (!(flags & 1)) return 0;
    std::uint32_t now = gameapi::timeGetTime();
    frameStart = now;
    frameTime = now - lastFrameStart;
    return 1;
}

int SceneRenderer::EndScene() {
    lastFrameStart = frameStart;
    return 1;
}
