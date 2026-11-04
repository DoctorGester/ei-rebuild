// The renderer's creation. It
// creates the device object, fills the transform pipeline's
// matrix stacks (projection, viewport, the fixed axis swap and their products), sets the pipeline's viewport
// and depth range, the render state (render/render_state.h), a default light and the ambient colour, and creates
// the counted helper object. Runs once at start-up. See docs/notes/models.md ("Renderer creation").
#include "app.h"
#include "camera.h"
#include "game_singletons.h"
#include "model.h"   // SceneRenderer, TransformPipeline, RenderHelper
#include "options.h"
#include "render/render_state.h"
#include "spell.h"     // Light, LightFactory
#include "texture.h"   // TextureDevice
#include "world.h"

#include "lib/crt.h"
#include "platform/platform.h"

#include <bit>
#include <cstdint>
#include <cstring>

using options::GameOptions;

Matrix4* AxisSwapMatrix(Matrix4*);
namespace {


// The device's pipeline holder takes the pipeline: counted first, then the old one released.
void DeviceSetPipeline(TransformPipeline** holder, TransformPipeline* pipeline) {
    TransformPipeline* previous = *holder;
    *holder = pipeline;
    if (pipeline) pipeline->AddRef();
    if (previous) previous->Release();
}

// --- Matrix stacks -------------------------------------------------------------------------------

// A push onto one of the pipeline's matrix stacks: the first entry is
// `m` itself, later ones are the previous top times `m` (with the new top as out).
void StackPush(MatrixStack& stack, const Matrix4* m) {
    if (stack.count == 0) {
        stack.count = 1;
        std::memcpy(stack.matrices.data(), m, sizeof(Matrix4));
        return;
    }
    int n = ++stack.count;
    stack.matrices[n - 1].Multiply(stack.matrices[n - 2].m, m->m);
}
const Matrix4* StackTop(const MatrixStack& stack) { return stack.matrices.data() + (stack.count - 1); }

// The pipeline's stacks (TransformPipeline::stacks) the creation fills.
constexpr int kProjection = 6;
constexpr int kViewport = 8;
constexpr int kAxisSwap = 7;
constexpr int kStackA = 1;   // viewport * axis swap * projection
constexpr int kStackB = 0;   // the same
constexpr int kStackC = 2;   // axis swap * projection
constexpr int kStackD = 5;   // the same
constexpr int kProductStacks[2] = {kStackA, kStackB};
constexpr int kSwapStacks[2] = {kStackC, kStackD};

// The stacks from the projection on: the viewport (the screen's size), the axis swap and their products. Each stack
// is filled from empty.
void FillScreenStacks(SceneRenderer* renderer) {
    MatrixStack* stacks = renderer->pipeline->stacks;
    Matrix4 m;
    ViewportMatrix(&m, &renderer->device->screen);
    StackPush(stacks[kViewport], &m);
    AxisSwapMatrix(&m);
    StackPush(stacks[kAxisSwap], &m);
    for (int stack : kProductStacks) {
        StackPush(stacks[stack], StackTop(stacks[kViewport]));
        StackPush(stacks[stack], StackTop(stacks[kAxisSwap]));
        StackPush(stacks[stack], StackTop(stacks[kProjection]));
    }
    for (int stack : kSwapStacks) {
        StackPush(stacks[stack], StackTop(stacks[kAxisSwap]));
        StackPush(stacks[stack], StackTop(stacks[kProjection]));
    }
}

void FillMatrixStacks(SceneRenderer* renderer) {
    TransformPipeline* pipeline = renderer->pipeline;
    MatrixStack* stacks = pipeline->stacks;
    // The camera's near and far distances and field of view.
    const Camera* cam = &g_camera;
    float zNear, zFar, fov;
    zNear = cam->nearDistance;
    zFar = cam->farDistance;
    fov = cam->fieldOfView;
    Matrix4 m;
    ProjectionMatrix(&m, fov, 1.3333334f, zNear, zFar);   // aspect 4/3
    StackPush(stacks[kProjection], &m);
    FillScreenStacks(renderer);
    // The projection, for the fog's depth.
    pipeline->projection = *StackTop(stacks[kProjection]);
}

// The pipeline's viewport size and depth range (the options' fog start and far clip distances).
void SetViewport(SceneRenderer* renderer, const GameOptions* settings) {
    const Rect& screen = renderer->device->screen;
    float width = static_cast<float>(screen.Width());
    float height = static_cast<float>(screen.Height());
    renderer->pipeline->SetViewportSize(width, height);
    renderer->pipeline->SetDepthRange(options::kFogDayStartDistance, settings->farClipDistance);
}

// The texture filters of the stages: linear min / mag filters with the mip levels (anisotropy: [renderer]).
void SetTextureFilters() {
    for (render::TextureStage& stage : render::g_renderState.stages) {
        stage.minFilter = stage.magFilter = render::Filter::Linear;
        stage.mipFilter = render::MipFilter::Point;
    }
}

// Blending (one, zero) and the alpha test (greater than 200) on, clockwise triangles dropped, specular off, fog on
// in black. (The original's dithering, perspective correction and antialiasing options are gone: the renderer
// always draws with perspective correction, [renderer] Msaa smooths the edges.)
void SetRenderState() {
    render::RenderState& state = render::g_renderState;
    state.blend = true;
    state.alphaTest = true;
    state.alphaFunc = render::Compare::Greater;
    state.alphaReference = 200;
    state.sourceBlend = render::Blend::One;
    state.destBlend = render::Blend::Zero;
    state.cull = render::Cull::Clockwise;
    state.specular = false;
    state.fog = true;
    state.fogColour = 0;
}

// The default light: class 0x2E of the light factory, white, at (5, 5, -8), range 1e6,
// +0x100 = 1e12, +0x104 = 1e-12.
void AddDefaultLight(SceneRenderer* renderer) {
    Light* light = g_clientWorld.lightFactory->CreateLight(kLightTypeDefault);
    Vector3 colour = {1.0f, 1.0f, 1.0f};
    Vector3 position = {5.0f, 5.0f, -8.0f};
    light->SetDirectional(colour, position);
    const std::uint32_t range = 0x49742400, rangeSquared = 0x5368D4A5, inverseRangeSquared = 0x2B8CBCCC;
    light->range = std::bit_cast<float>(range);
    light->rangeSquared = std::bit_cast<float>(rangeSquared);
    light->inverseRangeSquared = std::bit_cast<float>(inverseRangeSquared);
    renderer->SetSun(light);
}

// The counted helper object.
void CreateHelper(SceneRenderer* renderer) {
    renderer->helper.Assign(new RenderHelper());
}

} // namespace

void SceneRenderer::ResizeScreen(int width, int height) {
    device->screen = Rect{0, 0, width, height};
    MatrixStack* stacks = pipeline->stacks;
    // Between frames each stack holds only what the creation pushed (the frame's pushes are popped again).
    for (int stack : {kViewport, kAxisSwap, kProductStacks[0], kProductStacks[1], kSwapStacks[0], kSwapStacks[1]})
        stacks[stack].count = 0;
    FillScreenStacks(this);
    pipeline->SetViewportSize(static_cast<float>(width), static_cast<float>(height));
}

void SceneRenderer::CreateRenderer(void* window, GameOptions* settings) {
    render::g_renderState = {};   // a new device's
    CreatePipeline();
    TextureDevice* newDevice = new TextureDevice(window, settings->fullScreen != 0,
                                                settings->resolutionX, settings->resolutionY);
    device.Assign(newDevice);
    DeviceSetPipeline(&device->pipeline, pipeline);

    FillMatrixStacks(this);
    SetViewport(this, settings);
    render::g_renderState.depthTest = true;
    render::g_renderState.depthWrite = true;

    SetTextureFilters();
    SetRenderState();

    frameTime = 0;
    lastFrameStart = platform::FrameClock();
    AddDefaultLight(this);

    // The ambient colour: grey 50, opaque.
    SetColour(0xFF323232u);

    CreateHelper(this);
}
