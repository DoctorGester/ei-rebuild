// An interface sprite (CUiMesh, ui.h): its Create (005E6C60, vtable slot 2) and its draw (005E6D90,
// called by the window base's draw 005FEB90 and the panels that draw single sprites).
// docs/notes/ui.md, section 9.
#include "ui.h"

#include "model.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "originals_engine.h"
#include "originals_ui.h"
#include "render/render.h"   // SetInterfaceDraw
#include "ui_widescreen.h"

#include <cstdint>
#include <cstring>

namespace {

constexpr float kColourScale = 255.0f;   // 0073C370
constexpr std::uint32_t kOne = 0x3F800000;
constexpr int kStackWorld = 1;           // the transform pipeline's stacks the sprite is pushed on
constexpr int kStackView = 2;

// The identity orientation (w 1, x y z 0; set by 005E6C30 at start): a sprite's first orientation,
// and what the draw hands DrawFigure as its second argument (not read on that path).
const Quaternion& IdentityOrientation() { return *reinterpret_cast<const Quaternion*>(0x007C7CD8); }
constexpr float kSpriteDepth = 10.0f;
constexpr std::uint32_t kOpaqueBlack = 0xFF000000;
const char* SetTextureFailed() { return "CI3DFigure::Draw.\n SetTexture failed.\n"; }
const char* DrawFailed() { return "CI3DFigure::Draw.\n CFigure::Draw failed.\n"; }

// One component of the tint as a colour byte: the product stored as a float, then FISTP.
std::uint32_t ColourByte(float factor) { return static_cast<std::uint32_t>(x87::fistp(factor * kColourScale)); }

// A stack's top dropped, unless it is empty.
void Pop(MatrixStack& stack) {
    if (stack.count != 0) --stack.count;
}

// Place's layout scale (00740158, 0.48157), the half width / height (00740160, 400; 0073FA98, 300)
// and 1 (0073B84C), read from the exe.
float GameFloat(std::uintptr_t address) { return *reinterpret_cast<const volatile float*>(address); }

} // namespace

void CUiMesh::GetPlacement(float* x, float* y, float* depth) const {
    *x = (localPosition_.x / localPosition_.z / GameFloat(0x00740158) + GameFloat(0x0073B84C)) * GameFloat(0x00740160);
    *y = localPosition_.y / localPosition_.z / GameFloat(0x00740158) * GameFloat(0x00740160) + GameFloat(0x0073FA98);
    std::memcpy(depth, &localPosition_.z, sizeof(float));
}
EI_REPLACE_METHOD(0x005E7010, CUiMesh::GetPlacement);

// 005E6FA0. The layout's 1 / 400 (0074015C) and 0.75 (0073C524) with the constants above. Rebirth's
// WideScreen replaces it (addon's 10023151): x moved by the panel offset first (FIADD; 0 outside its
// panel builders, ui_widescreen.h).
void CUiMesh::PlaceBody(float x, float y, float depth) {
    if (widescreen::Active()) x += static_cast<float>(widescreen::PanelOffset());
    float placedX = (x * GameFloat(0x0074015C) - GameFloat(0x0073B84C)) * GameFloat(0x00740158) * depth;
    float placedY = (y * GameFloat(0x0074015C) - GameFloat(0x0073C524)) * GameFloat(0x00740158) * depth;
    localPosition_.x = placedX;
    localPosition_.y = placedY;
    std::memcpy(&localPosition_.z, &depth, sizeof(float));
}
EI_REPLACE_METHOD(0x005E6FA0, CUiMesh::PlaceBody);

// 005E6D90 (RET 4). Nothing unless visible. Binds the texture to stage 0 (failure is fatal), builds
// the placement from the orientation and position, pushes it on the view and world stacks, then
// the scale matrix on both (the view stack's push inlined), draws the sprite as a figure in its
// tint (slot 7; the renderer's colour put back after) with its material, and drops both pushes
// from both stacks. Returns 1 (or the fatal error's result).
int CUiMesh::Draw(CSceneRenderer* renderer) {
    if (!visible_) return 1;
    CTransformPipeline* pipeline = renderer->pipeline_;
    if (!renderer->SetTexture(0, texture_)) return game::FatalError(SetTextureFailed());

    localRotation_.ToMatrix(worldMatrix_.m);
    worldMatrix_.m[3] = localPosition_.x;
    worldMatrix_.m[7] = localPosition_.y;
    worldMatrix_.m[11] = localPosition_.z;
    worldMatrix_.m[12] = 0.0f;
    worldMatrix_.m[13] = 0.0f;
    worldMatrix_.m[14] = 0.0f;
    std::memcpy(&worldMatrix_.m[15], &kOne, sizeof(kOne));
    Matrix4 placement;
    std::memcpy(&placement, &worldMatrix_, sizeof(placement));
    pipeline->PushMatrix(placement.m, kStackView);
    pipeline->PushMatrix(placement.m, kStackWorld);
    pipeline->PushMatrix(scaleMatrix_.m, kStackView);
    pipeline->PushMatrix(scaleMatrix_.m, kStackWorld);

    const std::uint32_t saved = renderer->colour_;
    const std::uint32_t red = ColourByte(tint_[0]);
    const std::uint32_t green = ColourByte(tint_[1]);
    const std::uint32_t blue = ColourByte(tint_[2]);
    renderer->SetColour((((red | 0xFFFFFF00u) << 8 | green) << 8) | blue);
    const float origin[3] = {0.0f, 0.0f, 0.0f};
    render::SetInterfaceDraw(true);   // the new renderer streams interface sprites (R16d)
    const int drawn = renderer->DrawFigure(this, &IdentityOrientation(), origin, material_, 0xFF, nullptr);
    render::SetInterfaceDraw(false);
    renderer->SetColour(saved);
    if (!drawn) return game::FatalError(DrawFailed());

    MatrixStack& view = pipeline->stacks_[kStackView];
    MatrixStack& world = pipeline->stacks_[kStackWorld];
    Pop(view);
    Pop(world);
    Pop(view);
    Pop(world);
    return 1;
}
EI_REPLACE_METHOD(0x005E6D90, CUiMesh::Draw);

int CUiMesh::CreateSprite() {
    CreateMesh();
    material_ = nullptr;
    texture_ = nullptr;
    std::memset(&scaleMatrix_, 0, sizeof(scaleMatrix_));
    scaleMatrix_.m[15] = 1.0f;
    scaleMatrix_.m[10] = 1.0f;
    scaleMatrix_.m[5] = 1.0f;
    scaleMatrix_.m[0] = 1.0f;
    tint_[0] = 1.0f;
    tint_[1] = 1.0f;
    tint_[2] = 1.0f;
    tintSpare_[0] = 0.0f;
    tintSpare_[1] = 0.0f;
    tintSpare_[2] = 0.0f;
    localRotation_ = IdentityOrientation();
    localPosition_.x = 0.0f;
    localPosition_.y = 0.0f;
    localPosition_.z = kSpriteDepth;
    flatColour_ = kOpaqueBlack;
    flatSpecular_ = kOpaqueBlack;
    visible_ = 1;
    return 1;
}
EI_REPLACE_METHOD(0x005E6C60, CUiMesh::CreateSprite);

// --- The arrays' sizes (005EA210, 005EA600, 005EA4C0, 005EA380) ---------------------------------

void CUiMesh::SetQuadCount(int count) {
    Quads().SetSize(count);
    for (int i = 0; i < count; ++i)
        for (int k = 0; k < 4; ++k) {
            Quads()[i].z[k] = 0.0f;
            Quads()[i].w[k] = 1.0f;
        }
}
EI_REPLACE_METHOD(0x005EA210, CUiMesh::SetQuadCount);

void CUiMesh::SetVertexCount(int count) {
    Vertices().SetSize(count);
    for (int i = 0; i < count; ++i) Vertices()[i].normal = 0;
}
EI_REPLACE_METHOD(0x005EA600, CUiMesh::SetVertexCount);

void CUiMesh::SetIndexCount(int count) { indices_.SetSize(count); }
EI_REPLACE_METHOD(0x005EA4C0, CUiMesh::SetIndexCount);

void CUiMesh::SetUvCount(int count) { Texels().SetSize(count); }
EI_REPLACE_METHOD(0x005EA380, CUiMesh::SetUvCount);
