// An interface sprite (CUiMesh, ui.h): its Create, its draw (called by the window base's draw and the panels
// that draw single sprites), its constructor, destructor and virtuals. docs/notes/ui.md, section 9.
#include "ui.h"

#include "material.h"
#include "model.h"

#include "ei/x87.h"
#include "lib/crt.h"
#include "render/render.h"   // SetInterfaceDraw
#include "ui_widescreen.h"
#include "world_class_slots.h"   // I3DFigureClassName / Id

#include <cstdint>
#include <cstring>

#undef GetClassName

namespace {

constexpr float kColourScale = 255.0f;
constexpr float kOne = 1.0f;
constexpr int kStackWorld = 1;           // the transform pipeline's stacks the sprite is pushed on
constexpr int kStackView = 2;

// The identity orientation (w 1, x y z 0): a sprite's first orientation, and what the draw hands DrawFigure
// as its second argument (not read on that path).
constexpr Quaternion kIdentityOrientation = /*007C7CD8*/ {1.0f, 0.0f, 0.0f, 0.0f};   // a static initialiser
constexpr float kSpriteDepth = 10.0f;
constexpr std::uint32_t kOpaqueBlack = 0xFF000000;
const char* SetTextureFailed() { return "CI3DFigure::Draw.\n SetTexture failed.\n"; }
const char* DrawFailed() { return "CI3DFigure::Draw.\n CFigure::Draw failed.\n"; }

// One component of the tint as a colour byte: the product stored as a float, then rounded to the nearest
// integer.
std::uint32_t ColourByte(float factor) { return static_cast<std::uint32_t>(x87::fistp(factor * kColourScale)); }

// A stack's top dropped, unless it is empty.
void Pop(MatrixStack& stack) {
    if (stack.count != 0) --stack.count;
}

} // namespace

void CUiMesh::GetPlacement(float* x, float* y, float* depth) const {
    *x = (localPosition_.x / localPosition_.z / 0.48157462f + 1.0f) * 400.0f;
    *y = localPosition_.y / localPosition_.z / 0.48157462f * 400.0f + 300.0f;
    *depth = localPosition_.z;
}

// The layout's 1 / 400 and 0.75 with the constants above. Rebirth's WideScreen replaces it (addon's
// 10023151): x moved by the panel offset first (an integer add; 0 outside its panel builders,
// ui_widescreen.h).
void CUiMesh::PlaceBody(float x, float y, float depth) {
    if (widescreen::Active()) x += static_cast<float>(widescreen::PanelOffset());
    float placedX = (x * 0.0025f - 1.0f) * 0.48157462f * depth;
    float placedY = (y * 0.0025f - 0.75f) * 0.48157462f * depth;
    localPosition_.x = placedX;
    localPosition_.y = placedY;
    localPosition_.z = depth;
}

// Nothing unless visible. Binds the texture to stage 0 (failure is fatal), builds the placement from the
// orientation and position, pushes it on the view and world stacks, then the scale matrix on both (the view
// stack's push inlined), draws the sprite as a figure in its tint (the renderer's colour put back after) with
// its material, and drops both pushes from both stacks. Returns 1 (or the fatal error's result).
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
    worldMatrix_.m[15] = kOne;
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
    render::SetInterfaceDraw(true);   // the new renderer streams interface sprites
    const int drawn = renderer->DrawFigure(this, &kIdentityOrientation, origin, static_cast<CMaterial*>(material_), 0xFF, nullptr);
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
    localRotation_ = kIdentityOrientation;
    localPosition_.x = 0.0f;
    localPosition_.y = 0.0f;
    localPosition_.z = kSpriteDepth;
    flatColour_ = kOpaqueBlack;
    flatSpecular_ = kOpaqueBlack;
    visible_ = 1;
    return 1;
}

// --- The class 0x43's lifetime and slots ---------------------------------------------------

CUiMesh::CUiMesh() {}
CUiMesh::~CUiMesh() {}

int CUiMesh::Create() { return CreateSprite(); }
void CUiMesh::Destroy() { DestroySprite(); }
const char* CUiMesh::GetClassName() { return I3DFigureClassName(this); }
int CUiMesh::GetClassId() { return I3DFigureClassId(this); }

// --- The arrays' sizes ---------------------------------

void CUiMesh::SetQuadCount(int count) {
    vertices_.resize(count);
    for (int i = 0; i < count; ++i)
        for (int k = 0; k < 4; ++k) {
            vertices_[i].z()[k] = 0.0f;
            vertices_[i].w()[k] = 1.0f;
        }
}

void CUiMesh::SetVertexCount(int count) {
    corners_.resize(count);
    for (int i = 0; i < count; ++i) corners_[i].normal = 0;
}

void CUiMesh::SetIndexCount(int count) { indices_.resize(count); }

void CUiMesh::SetUvCount(int count) { texCoords_.resize(count); }
