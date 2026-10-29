// An interface sprite (UiMesh, ui.h): its Create, its draw (called by the window base's draw and the panels
// that draw single sprites), its constructor, destructor and virtuals. docs/notes/ui.md, section 9.
#include "material.h"
#include "model.h"
#include "render/render.h"   // SetInterfaceDraw
#include "ui.h"
#include "ui_widescreen.h"

#include "lib/crt.h"

#include <cmath>
#include <cstdint>
#include <cstring>

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
constexpr const char* kClassName = "CI3DFigure";
constexpr const char* kSetTextureFailed = "CI3DFigure::Draw.\n SetTexture failed.\n";
constexpr const char* kDrawFailed = "CI3DFigure::Draw.\n CFigure::Draw failed.\n";

// One component of the tint as a colour byte: the product stored as a float, then rounded to the nearest
// integer.
std::uint32_t ColourByte(float factor) { return static_cast<std::uint32_t>(std::lround(factor * kColourScale)); }

// A stack's top dropped, unless it is empty.
void Pop(MatrixStack& stack) {
    if (stack.count != 0) --stack.count;
}

} // namespace

void UiMesh::GetPlacement(float* x, float* y, float* depth) const {
    *x = (localPosition.x / localPosition.z / 0.48157462f + 1.0f) * 400.0f;
    *y = localPosition.y / localPosition.z / 0.48157462f * 400.0f + 300.0f;
    *depth = localPosition.z;
}

// The layout's 1 / 400 and 0.75 with the constants above. Rebirth's WideScreen replaces it (addon's
// 10023151): x moved by the panel offset first (an integer add; 0 outside its panel builders,
// ui_widescreen.h).
void UiMesh::Place(float x, float y, float depth) {
    if (widescreen::Active()) x += static_cast<float>(widescreen::PanelOffset());
    float placedX = (x * 0.0025f - 1.0f) * 0.48157462f * depth;
    float placedY = (y * 0.0025f - 0.75f) * 0.48157462f * depth;
    localPosition.x = placedX;
    localPosition.y = placedY;
    localPosition.z = depth;
}

// Nothing unless visible. Binds the texture to stage 0 (failure is fatal), builds the placement from the
// orientation and position, pushes it on the view and world stacks, then the scale matrix on both (the view
// stack's push inlined), draws the sprite as a figure in its tint (the renderer's colour put back after) with
// its material, and drops both pushes from both stacks.
void UiMesh::Draw(SceneRenderer* renderer) {
    if (!visible) return;
    TransformPipeline* pipeline = renderer->pipeline;
    if (!renderer->SetTexture(0, texture)) {
        game::FatalError(kSetTextureFailed);
        return;
    }

    localRotation.ToMatrix(worldMatrix.m);
    worldMatrix.m[3] = localPosition.x;
    worldMatrix.m[7] = localPosition.y;
    worldMatrix.m[11] = localPosition.z;
    worldMatrix.m[12] = 0.0f;
    worldMatrix.m[13] = 0.0f;
    worldMatrix.m[14] = 0.0f;
    worldMatrix.m[15] = kOne;
    Matrix4 placement;
    std::memcpy(&placement, &worldMatrix, sizeof(placement));
    pipeline->PushMatrix(placement.m, kStackView);
    pipeline->PushMatrix(placement.m, kStackWorld);
    pipeline->PushMatrix(scaleMatrix.m, kStackView);
    pipeline->PushMatrix(scaleMatrix.m, kStackWorld);

    const std::uint32_t saved = renderer->colour;
    const std::uint32_t red = ColourByte(tint[0]);
    const std::uint32_t green = ColourByte(tint[1]);
    const std::uint32_t blue = ColourByte(tint[2]);
    renderer->SetColour((((red | 0xFFFFFF00u) << 8 | green) << 8) | blue);
    render::SetInterfaceDraw(true);   // the new renderer streams interface sprites
    const int drawn = renderer->DrawFigure(this, static_cast<Material*>(material), 0xFF, nullptr);
    render::SetInterfaceDraw(false);
    renderer->SetColour(saved);
    if (!drawn) {
        game::FatalError(kDrawFailed);
        return;
    }

    MatrixStack& view = pipeline->stacks[kStackView];
    MatrixStack& world = pipeline->stacks[kStackWorld];
    Pop(view);
    Pop(world);
    Pop(view);
    Pop(world);
}

// --- The class 0x43's lifetime and slots ---------------------------------------------------

UiMesh::UiMesh() {}
UiMesh::~UiMesh() {}

void UiMesh::Create() {
    CreateMesh();
    material = nullptr;
    texture = nullptr;
    std::memset(&scaleMatrix, 0, sizeof(scaleMatrix));
    scaleMatrix.m[15] = 1.0f;
    scaleMatrix.m[10] = 1.0f;
    scaleMatrix.m[5] = 1.0f;
    scaleMatrix.m[0] = 1.0f;
    tint[0] = 1.0f;
    tint[1] = 1.0f;
    tint[2] = 1.0f;
    tintSpare[0] = 0.0f;
    tintSpare[1] = 0.0f;
    tintSpare[2] = 0.0f;
    localRotation = kIdentityOrientation;
    localPosition.x = 0.0f;
    localPosition.y = 0.0f;
    localPosition.z = kSpriteDepth;
    flatColour = kOpaqueBlack;
    flatSpecular = kOpaqueBlack;
    visible = true;
}

void UiMesh::Destroy() {
    material = nullptr;
    texture = nullptr;
    PlainFigureMesh::Destroy();
}

const char* UiMesh::ClassName() { return kClassName; }
ClassId UiMesh::GetClassId() { return ClassId::UiMesh; }

// --- The arrays' sizes ---------------------------------

void UiMesh::SetQuadCount(int count) {
    vertices.resize(count);
    for (int i = 0; i < count; ++i)
        for (int k = 0; k < 4; ++k) {
            vertices[i].z()[k] = 0.0f;
            vertices[i].w()[k] = 1.0f;
        }
}

void UiMesh::SetVertexCount(int count) {
    corners.resize(count);
    for (int i = 0; i < count; ++i) corners[i].normal = 0;
}

void UiMesh::SetIndexCount(int count) { indices.resize(count); }

void UiMesh::SetUvCount(int count) { texCoords.resize(count); }
