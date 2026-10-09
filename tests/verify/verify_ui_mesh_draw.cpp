// Check for the sprite draw (ui_mesh_draw.cpp): 005E6D90 CUiMesh::Draw. Ours only (it draws):
// the result is 1, the view and world stacks are back at their depths, the renderer's colour is
// the one it had, and a visible sprite's placement is its orientation's rotation (the original
// 004D9AA0 into a scratch matrix) with the position in the last column and 0, 0, 0, 1 below.
// Every scenario (every window's sprites, every frame).
#include "verify.h"

#include "../../src/game/model.h"
#include "../../src/game/originals_render.h"
#include "../../src/game/ui.h"

#include <cstring>

namespace {
void* s_unused;

bool PlacementOk(const CUiMesh* self) {
    float expected[16];
    std::memcpy(expected, self->placement_, sizeof(expected));
    QuaternionToMatrixOriginal(&self->orientation_, nullptr, expected);
    expected[3] = self->position_.x;
    expected[7] = self->position_.y;
    expected[11] = self->position_.z;
    expected[12] = 0.0f;
    expected[13] = 0.0f;
    expected[14] = 0.0f;
    expected[15] = 1.0f;
    return std::memcmp(expected, self->placement_, sizeof(expected)) == 0;
}
} // namespace

static int __fastcall SpriteDrawVerify(CUiMesh* self, void*, CSceneRenderer* renderer) {
    CTransformPipeline* pipeline = renderer->pipeline_;
    const int view = pipeline->stacks_[2].count;
    const int world = pipeline->stacks_[1].count;
    const std::uint32_t colour = renderer->colour_;
    const int result = self->Draw(renderer);
    const bool ok = result == 1 && pipeline->stacks_[2].count == view && pipeline->stacks_[1].count == world &&
                    renderer->colour_ == colour && (!self->visible_ || PlacementOk(self));
    ei::verify::Check("CUiMesh::Draw", true, ok, "result %d stacks %d %d -> %d %d colour %08X -> %08X", result, view,
                      world, pipeline->stacks_[2].count, pipeline->stacks_[1].count, colour, renderer->colour_);
    return result;
}
EI_VERIFY(0x005E6D90, SpriteDrawVerify, 0, s_unused);
