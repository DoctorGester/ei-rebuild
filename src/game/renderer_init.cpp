// The renderer's creation. It
// creates the device object, fills the transform pipeline's
// matrix stacks (projection, viewport, the fixed axis swap and their products), sets the pipeline's viewport
// and depth range, the render state (render/render_state.h), a default light and the ambient colour, and creates
// the counted helper object. Runs once at start-up. See docs/notes/models.md ("Renderer creation").
#include "app.h"
#include "render/render_state.h"
#include "ei/x87.h"
#include "camera.h"
#include "model.h"         // CSceneRenderer, CTransformPipeline, ITransformPipeline, CRenderHelper
#include "options.h"
#include "spell.h"         // CLight, CLightFactory
#include "texture.h"       // CTextureDevice
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "game_singletons.h"
#include "world.h"
#include "lib/nan_audit.h"

#include <bit>
#include <cstdint>
#include <cstring>

using options::GameOptions;

Matrix4* AxisSwapMatrix(Matrix4*);
namespace {


// The device's pipeline holder takes the pipeline: counted first, then the old one released.
void DeviceSetPipeline(CTransformPipeline** holder, CTransformPipeline* pipeline) {
    CTransformPipeline* previous = *holder;
    *holder = pipeline;
    if (pipeline) ++pipeline->refs_;
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

// The pipeline's stacks (CTransformPipeline::stacks_) the creation fills.
constexpr int kProjection = 6;
constexpr int kViewport = 8;
constexpr int kAxisSwap = 7;
constexpr int kStackA = 1;   // viewport * axis swap * projection
constexpr int kStackB = 0;   // the same
constexpr int kStackC = 2;   // axis swap * projection
constexpr int kStackD = 5;   // the same
constexpr int kProductStacks[2] = {kStackA, kStackB};
constexpr int kSwapStacks[2] = {kStackC, kStackD};

// Transposes a 4x4 float matrix: the upper-triangle elements move
// through the FPU, the lower ones as plain bit copies.
void Transpose(float* m) {
    static constexpr int kPairs[6][2] = {{1, 4}, {2, 8}, {3, 12}, {6, 9}, {7, 13}, {11, 14}};
    for (const auto& p : kPairs) {
        std::uint32_t lower;
        lower = std::bit_cast<std::uint32_t>(m[p[1]]);
        m[p[1]] = m[p[0]];
        m[p[0]] = std::bit_cast<float>(lower);
    }
}

void FillMatrixStacks(CSceneRenderer* renderer) {
    CTransformPipeline* pipeline = renderer->pipeline_;
    MatrixStack* stacks = pipeline->stacks_;
    // The camera's near and far distances and field of view.
    const CCamera* cam = &g_camera.Get();
    float zNear, zFar, fov;
    zNear = cam->nearDistance_;
    zFar = cam->farDistance_;
    fov = cam->fieldOfView_;
    Matrix4 m;
    static_assert(std::bit_cast<std::uint32_t>(1.3333334f) == 0x3FAAAAABu);
    ProjectionMatrix(&m, fov, 1.3333334f, zNear, zFar);   // aspect 4/3
    StackPush(stacks[kProjection], &m);
    ViewportMatrix(&m, &renderer->device_->screen_);
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
    // The projection, transposed, to the pipeline's transform object.
    Matrix4 transposed;
    std::memcpy(&transposed, StackTop(stacks[kProjection]), sizeof(Matrix4));
    Transpose(transposed.m);
    Matrix4 copy;
    std::memcpy(&copy, &transposed, sizeof(Matrix4));
    pipeline->transform_->SetProjection(copy.m);
}

// The pipeline's viewport size (floats) and depth range (the options' fog start and far clip distances, moved
// bit for bit; both arguments must be passed, or the callee's stack cleanup unbalances the stack).
void SetViewport(CSceneRenderer* renderer, const GameOptions* settings) {
    const RECT& screen = renderer->device_->screen_;
    float width = static_cast<float>(screen.right - screen.left);
    float height = static_cast<float>(screen.bottom - screen.top);
    renderer->pipeline_->transform_->SetViewportSize(width, height);
    float first, second;
    first = settings->fogDayStartDistance_;
    second = settings->farClipDistance_;
    renderer->pipeline_->transform_->SetDepthRange(first, second);
}

// The texture filters of the stages: Filtering picks point (0) or linear (1, 2) min / mag filters; the mip filter
// is point with Mipmapping, else none.
void SetTextureFilters(const GameOptions* settings) {
    for (render::TextureStage& stage : render::g_renderState.stages) {
        int filter = settings->filtering_;
        if (filter == 0 || filter == 1 || filter == 2) {
            stage.minFilter = stage.magFilter = filter == 0 ? render::Filter::Point : render::Filter::Linear;
            if (settings->mipmapping_) stage.mipFilter = render::MipFilter::Point;
        }
        if (settings->mipmapping_ == 0) stage.mipFilter = render::MipFilter::None;
    }
}

// Blending (one, zero) and the alpha test (greater than 200) on, clockwise triangles dropped, specular from the
// options, fog on in black. (The options' dithering, perspective correction and antialiasing do nothing: the
// renderer always draws with perspective correction.)
void SetRenderState(const GameOptions* settings) {
    render::RenderState& state = render::g_renderState;
    state.blend = true;
    state.alphaTest = true;
    state.alphaFunc = render::Compare::Greater;
    state.alphaReference = 200;
    state.sourceBlend = render::Blend::One;
    state.destBlend = render::Blend::Zero;
    state.cull = render::Cull::Clockwise;
    state.specular = settings->specular_ != 0;
    state.fog = true;
    state.fogColour = 0;
}

// The default light: class 0x2E of the light factory, white, at (5, 5, -8), range 1e6,
// +0x100 = 1e12, +0x104 = 1e-12.
void AddDefaultLight(CSceneRenderer* renderer) {
    CLight* light = g_clientWorld->lightFactory_->CreateLight(0x2E);
    Vector3 colour = {1.0f, 1.0f, 1.0f};
    Vector3 position = {5.0f, 5.0f, -8.0f};
    light->SetDirectional(&colour, &position);
    const std::uint32_t range = 0x49742400, rangeSquared = 0x5368D4A5, inverseRangeSquared = 0x2B8CBCCC;
    light->range_ = std::bit_cast<float>(range);
    light->rangeSquared_ = std::bit_cast<float>(rangeSquared);
    light->inverseRangeSquared_ = std::bit_cast<float>(inverseRangeSquared);
    renderer->SetSun(light);
}

// The counted helper object. The flag is a byte argument: only its low byte is read (the caller
// pushes it with the rest of the register left over from operator new).
void CreateHelper(CSceneRenderer* renderer, const GameOptions* settings) {
    const std::uint32_t flag = settings->driverIndex_ != 0 ? 1u : 0u;
    renderer->helper_.Assign(new CRenderHelper(flag));
}

} // namespace

void CSceneRenderer::CreateRenderer(void* window, GameOptions* settings) {
    render::g_renderState = {};   // a new device's
    CreatePipeline(settings);
    // (The options' driver, refresh rate and depth buffer bits choose nothing: the new renderer draws.)
    CTextureDevice* device = new CTextureDevice(static_cast<HWND>(window), settings->fullScreen_ != 0,
                                                settings->resolutionX_, settings->resolutionY_, settings->colourDepth_);
    device_.Assign(device);
    DeviceSetPipeline(&device_->pipeline_, pipeline_);

    FillMatrixStacks(this);
    SetViewport(this, settings);
    render::g_renderState.depthTest = true;
    render::g_renderState.depthWrite = true;

    SetTextureFilters(settings);
    SetRenderState(settings);

    frameTime_ = 0;
    flags_ |= 2;
    shortestFrame_ = 0x80000000;
    longestFrame_ = 0;
    totalTime_ = 0;
    frameCount_ = 0;
    averageFrame_ = 0;
    lastFrameStart_ = gameapi::timeGetTime();
    windowTime_ = 0;
    windowFrames_ = 0;
    AddDefaultLight(this);

    // The ambient colour: grey 50 from three FISTPs of 50.0.
    std::uint32_t r = static_cast<std::uint32_t>(x87::fistp(50.0f));
    std::uint32_t g = static_cast<std::uint32_t>(x87::fistp(50.0f));
    std::uint32_t b = static_cast<std::uint32_t>(x87::fistp(50.0f));
    std::uint32_t ambient = ((((r | 0xFFFFFF00u) << 8) | g) << 8) | b;
    SetColour(ambient);

    CreateHelper(this, settings);
}
