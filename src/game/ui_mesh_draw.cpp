// An interface sprite (CUiMesh, ui.h) draws itself: 005E6D90, called by the window base's draw
// (005FEB90) and the panels that draw single sprites. docs/notes/ui.md, section 9.
#include "ui.h"

#include "model.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "originals_engine.h"

#include <cstdint>
#include <cstring>

namespace {

constexpr float kColourScale = 255.0f;   // 0073C370
constexpr std::uint32_t kOne = 0x3F800000;
constexpr int kStackWorld = 1;           // the transform pipeline's stacks the sprite is pushed on
constexpr int kStackView = 2;

// The data the sprite draw hands the renderer's DrawFigure as its second argument (written by
// 005E6C30, not read on that path).
const void* SpriteDrawArgument() { return reinterpret_cast<const void*>(0x007C7CD8); }
const char* SetTextureFailed() { return reinterpret_cast<const char*>(0x00792E38); }   // "CI3DFigure::Draw: SetTexture fai..."
const char* DrawFailed() { return reinterpret_cast<const char*>(0x00792E0C); }         // "CI3DFigure::Draw: CFigure::Draw f..."

// One component of the tint as a colour byte: the product stored as a float, then FISTP.
std::uint32_t ColourByte(float factor) { return static_cast<std::uint32_t>(x87::fistp(factor * kColourScale)); }

// A stack's top dropped, unless it is empty.
void Pop(MatrixStack& stack) {
    if (stack.count != 0) --stack.count;
}

} // namespace

// 005E6D90 (RET 4). Nothing unless visible. Binds the texture to stage 0 (failure is fatal), builds
// the placement from the orientation and position, pushes it on the view and world stacks, then
// the scale matrix on both (the view stack's push inlined), draws the sprite as a figure in its
// tint (slot 7; the renderer's colour put back after) with its material, and drops both pushes
// from both stacks. Returns 1 (or the fatal error's result).
int CUiMesh::Draw(CSceneRenderer* renderer) {
    if (!visible_) return 1;
    CTransformPipeline* pipeline = renderer->pipeline_;
    if (!renderer->SetTexture(0, texture_)) return FatalErrorOriginal(SetTextureFailed());

    orientation_.ToMatrix(placement_);
    placement_[3] = position_.x;
    placement_[7] = position_.y;
    placement_[11] = position_.z;
    placement_[12] = 0.0f;
    placement_[13] = 0.0f;
    placement_[14] = 0.0f;
    std::memcpy(&placement_[15], &kOne, sizeof(kOne));
    float placement[16];
    std::memcpy(placement, placement_, sizeof(placement));
    pipeline->PushMatrix(placement, kStackView);
    pipeline->PushMatrix(placement, kStackWorld);
    pipeline->PushMatrix(scaleMatrix_, kStackView);
    pipeline->PushMatrix(scaleMatrix_, kStackWorld);

    const std::uint32_t saved = renderer->colour_;
    const std::uint32_t red = ColourByte(tint_[0]);
    const std::uint32_t green = ColourByte(tint_[1]);
    const std::uint32_t blue = ColourByte(tint_[2]);
    renderer->SetColour((((red | 0xFFFFFF00u) << 8 | green) << 8) | blue);
    const float origin[3] = {0.0f, 0.0f, 0.0f};
    const int drawn = renderer->DrawFigure(this, SpriteDrawArgument(), origin, material_, 0xFF, nullptr);
    renderer->SetColour(saved);
    if (!drawn) return FatalErrorOriginal(DrawFailed());

    MatrixStack& view = pipeline->stacks_[kStackView];
    MatrixStack& world = pipeline->stacks_[kStackWorld];
    Pop(view);
    Pop(world);
    Pop(view);
    Pop(world);
    return 1;
}
EI_REPLACE_METHOD(0x005E6D90, CUiMesh::Draw);
