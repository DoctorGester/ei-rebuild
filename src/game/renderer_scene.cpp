// The renderer's scene bracket: BeginScene with the last frame's time, EndScene closing the frame. The main loop
// brackets each frame's drawing with them.
#include "model.h"     // CSceneRenderer
#include "texture.h"   // CTextureDevice, CTexture

#include "core_maths.h"
#include "game_singletons.h"

#include "lib/gameapi.h"
#include <cstdint>

int CSceneRenderer::BeginScene() {
    if (!(flags_ & 1)) return 0;
    std::uint32_t now = gameapi::timeGetTime();
    frameStart_ = now;
    frameTime_ = now - lastFrameStart_;
    return 1;
}

int CSceneRenderer::EndScene() {
    lastFrameStart_ = frameStart_;
    return 1;
}
