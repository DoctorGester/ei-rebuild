// The renderer's creation (00500B70, virtual slot 2 of the renderer at 007AF060, vtable 0073D93C):
// picks the Direct3D driver from the settings, creates the device object, fills the transform
// pipeline's matrix stacks (projection, viewport, the fixed axis swap and their products), sets the
// pipeline's viewport and depth range, the render and texture stage states, a default light and the
// ambient colour, and creates the counted helper object at +0x24. Runs once at start-up. See
// docs/notes/models.md ("Renderer creation").
#include "app.h"
#include "device.h"   // the old device's calls
#include "game_strings.h"
#include "ei/hook.h"
#include "render/render_state.h"
#include "ei/x87.h"
#include "camera.h"
#include "model.h"         // CSceneRenderer, CTransformPipeline, ITransformPipeline, CRenderHelper
#include "options.h"
#include "spell.h"         // CLight, CLightFactory
#include "texture.h"       // CTextureDevice, DirectDrawDriver
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "originals_engine.h"
#include "originals_library.h"
#include "originals_render.h"

#include <d3d.h>

#include <bit>
#include <cstdint>
#include <cstring>

using options::GameOptions;

namespace {

// --- Forwarders ----------------------------------------------------------------------------------

// 004EBE80: the device's pipeline holder (+0xAC) takes the pipeline: counted first, then the old
// one released.
void DeviceSetPipeline(CTransformPipeline** holder, CTransformPipeline* pipeline) {
    CTransformPipeline* previous = *holder;
    *holder = pipeline;
    if (pipeline) ++pipeline->refs_;
    if (previous) previous->Release();
}
// Matrices (model.cpp, called through their addresses): 004F1370 viewport,
// 004F1330 the axis swap (x, -y, z; patched by Rebirth); 004D9880 the product (MatrixMultiplyOriginal).
__declspec(naked) Matrix4* __stdcall AxisSwapMatrix(Matrix4*) EI_ORIGINAL(0x004F1330)
// The game's operator new, keeping EDX as the allocator leaves it (see CreateHelper).
__declspec(naked) void* __fastcall NewKeepingEdx(std::uint32_t /*size*/, std::uint32_t* /*edx out*/) {
    __asm {
        push esi
        mov esi, edx
        push ecx
        mov eax, 007005AFh
        call eax
        add esp, 4
        mov dword ptr [esi], edx
        pop esi
        ret
    }
}

// The capability records as the original reads them.
static_assert(offsetof(D3DDEVICEDESC7, wMaxSimultaneousTextures) == 0xBA);
static_assert(sizeof(DDCAPS) == 0x17C && offsetof(DDCAPS, ddsCaps) == 0x16C);

// --- Direct3D device (IDirect3DDevice7) ----------------------------------------------------------

IDirect3DDevice7* D3DDevice(CSceneRenderer* renderer) {
    return static_cast<IDirect3DDevice7*>(renderer->device_->d3dDevice_);
}

void SetRenderState(CSceneRenderer* renderer, D3DRENDERSTATETYPE state, std::uint32_t value) {
    device::SetRenderState(D3DDevice(renderer), state, value);
    render::state::Render(static_cast<std::uint32_t>(state), value);
}
// SetTextureStageState; the result goes to the device object's +8, as every call there does.
void SetStageState(CSceneRenderer* renderer, int stage, D3DTEXTURESTAGESTATETYPE type, int value) {
    CTextureDevice* device = renderer->device_;
    device->lastLoadResult_ = device::SetTextureStageState(static_cast<IDirect3DDevice7*>(device->d3dDevice_), stage, type, value);
    render::state::Stage(static_cast<std::uint32_t>(stage), static_cast<std::uint32_t>(type), static_cast<std::uint32_t>(value));
}

// --- Matrix stacks -------------------------------------------------------------------------------

// A push onto one of the pipeline's matrix stacks, as the original inlines it: the first entry is
// `m` itself, later ones are the previous top times `m` (004D9880 with the new top as out).
void StackPush(MatrixStack& stack, const Matrix4* m) {
    if (stack.count == 0) {
        stack.count = 1;
        std::memcpy(stack.matrices, m, sizeof(Matrix4));
        return;
    }
    int n = ++stack.count;
    MatrixMultiplyOriginal(stack.matrices[n - 1].m, nullptr, stack.matrices[n - 2].m, m->m);
}
const Matrix4* StackTop(const MatrixStack& stack) { return stack.matrices + (stack.count - 1); }

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

// Transposes a 4x4 float matrix the way the original does: the upper-triangle elements move
// through the FPU (FLD / FSTP), the lower ones as dwords.
void Transpose(float* m) {
    static constexpr int kPairs[6][2] = {{1, 4}, {2, 8}, {3, 12}, {6, 9}, {7, 13}, {11, 14}};
    for (const auto& p : kPairs) {
        std::uint32_t lower;
        std::memcpy(&lower, &m[p[1]], 4);
        std::memcpy(&m[p[1]], &m[p[0]], sizeof(float));
        std::memcpy(&m[p[0]], &lower, 4);
    }
}

void FillMatrixStacks(CSceneRenderer* renderer) {
    CTransformPipeline* pipeline = renderer->pipeline_;
    MatrixStack* stacks = pipeline->stacks_;
    // 0040D380 (CameraGetRange): the camera's near and far distances and field of view, as dwords.
    const CCamera* cam = camera::TheCamera();
    float zNear, zFar, fov;
    std::memcpy(&zNear, &cam->nearDistance_, 4);
    std::memcpy(&zFar, &cam->farDistance_, 4);
    std::memcpy(&fov, &cam->fieldOfView_, 4);
    Matrix4 m;
    ProjectionMatrix(&m, fov, std::bit_cast<float>(0x3FAAAAABu), zNear, zFar);   // aspect 4/3
    StackPush(stacks[kProjection], &m);
    ViewportMatrix(&m, reinterpret_cast<const int*>(&renderer->device_->screen_));
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

// The pipeline's viewport size (floats) and depth range (the options' fog start and far clip
// distances, moved as dwords; the first version passed only one argument, and the callee's RET 8
// then unbalanced the stack).
void SetViewport(CSceneRenderer* renderer, const GameOptions* settings) {
    const RECT& screen = renderer->device_->screen_;
    float width = static_cast<float>(screen.right - screen.left);
    float height = static_cast<float>(screen.bottom - screen.top);
    renderer->pipeline_->transform_->SetViewportSize(width, height);
    float first, second;
    std::memcpy(&first, &settings->fogDayStartDistance_, 4);
    std::memcpy(&second, &settings->farClipDistance_, 4);
    renderer->pipeline_->transform_->SetDepthRange(first, second);
}

// The texture filters of every stage the device can use at once: Filtering picks point (0) or
// linear (1, 2) min / mag filters; the mip filter is point with Mipmapping, else none.
void SetTextureStages(CSceneRenderer* renderer, const GameOptions* settings, const D3DDEVICEDESC7& deviceDesc) {
    for (int stage = 0;; ++stage) {
        if (stage >= static_cast<int>(deviceDesc.wMaxSimultaneousTextures)) break;
        int filter = settings->filtering_;
        if (filter == 0 || filter == 1 || filter == 2) {
            int value = filter == 0 ? D3DTFN_POINT : D3DTFN_LINEAR;   // the D3DTFG_ values are the same
            SetStageState(renderer, stage, D3DTSS_MINFILTER, value);
            SetStageState(renderer, stage, D3DTSS_MAGFILTER, value);
            if (settings->mipmapping_) SetStageState(renderer, stage, D3DTSS_MIPFILTER, D3DTFP_POINT);
        }
        if (settings->mipmapping_ == 0) SetStageState(renderer, stage, D3DTSS_MIPFILTER, D3DTFP_NONE);
    }
}

void SetRenderStates(CSceneRenderer* renderer, const GameOptions* settings) {
    SetRenderState(renderer, D3DRENDERSTATE_ALPHABLENDENABLE, 1);
    SetRenderState(renderer, D3DRENDERSTATE_ALPHATESTENABLE, 1);
    SetRenderState(renderer, D3DRENDERSTATE_ALPHAFUNC, D3DCMP_GREATER);
    SetRenderState(renderer, D3DRENDERSTATE_ALPHAREF, 200);
    SetRenderState(renderer, D3DRENDERSTATE_SRCBLEND, D3DBLEND_ONE);
    SetRenderState(renderer, D3DRENDERSTATE_DESTBLEND, D3DBLEND_ZERO);
    SetRenderState(renderer, D3DRENDERSTATE_CULLMODE, D3DCULL_CW);
    SetRenderState(renderer, D3DRENDERSTATE_SPECULARENABLE, static_cast<std::uint32_t>(settings->specular_));
    SetRenderState(renderer, D3DRENDERSTATE_DITHERENABLE, static_cast<std::uint32_t>(settings->dithering_));
    SetRenderState(renderer, D3DRENDERSTATE_TEXTUREPERSPECTIVE,
                   static_cast<std::uint32_t>(settings->perspectiveCorrection_));
    SetRenderState(renderer, D3DRENDERSTATE_ANTIALIAS, settings->antialiasing_ ? D3DANTIALIAS_SORTINDEPENDENT : 0);
    SetRenderState(renderer, D3DRENDERSTATE_FOGENABLE, 1);
    SetRenderState(renderer, D3DRENDERSTATE_FOGVERTEXMODE, D3DFOG_LINEAR);
    SetRenderState(renderer, D3DRENDERSTATE_FOGCOLOR, 0);
    SetRenderState(renderer, D3DRENDERSTATE_EXTENTS, 0);
    SetRenderState(renderer, D3DRENDERSTATE_CLIPPING, 0);
    SetRenderState(renderer, D3DRENDERSTATE_LIGHTING, 0);
    SetRenderState(renderer, D3DRENDERSTATE_AMBIENT, 0);
}

// The default light: class 0x2E of the light factory, white, at (5, 5, -8), range 1e6,
// +0x100 = 1e12, +0x104 = 1e-12 (stored as dwords).
void AddDefaultLight(CSceneRenderer* renderer) {
    auto* light = static_cast<CLight*>(TheLightFactory()->CreateObject(0x2E, -1));
    float colour[3] = {1.0f, 1.0f, 1.0f};
    Vector3 position = {5.0f, 5.0f, -8.0f};
    light->SetDirectional(reinterpret_cast<const Vector3*>(colour), &position);   // 0068D0A0
    const std::uint32_t range = 0x49742400, rangeSquared = 0x5368D4A5, inverseRangeSquared = 0x2B8CBCCC;
    std::memcpy(&light->range_, &range, 4);
    std::memcpy(&light->rangeSquared_, &rangeSquared, 4);
    std::memcpy(&light->inverseRangeSquared_, &inverseRangeSquared, 4);
    renderer->SetSun(light);
}

// The counted helper object (+0x24). The flag argument is pushed as EDX with only DL set, the rest
// of EDX being what operator new left there; the same value is passed.
void CreateHelper(CSceneRenderer* renderer, const GameOptions* settings) {
    std::uint32_t edx = 0;
    void* memory = NewKeepingEdx(sizeof(CRenderHelper), &edx);
    CRenderHelper* helper = nullptr;
    if (memory) {
        std::uint32_t flag = (edx & 0xFFFFFF00u) | (settings->driverIndex_ != 0 ? 1u : 0u);
        helper = static_cast<CRenderHelper*>(memory)->RenderHelperConstruct(D3DDevice(renderer), flag);
    }
    CRenderHelper* old = renderer->helper_;
    renderer->helper_ = helper;
    if (helper) ++helper->refs_;
    if (old && --old->refs_ == 0) old->DeleteThis(1);
}

} // namespace

// 00500B70 (thiscall, RET 8).
void CSceneRenderer::CreateRendererImpl(void* window, GameOptions* settings) {
    render::state::Reset();   // a new device: nothing it has is known yet
    CreateBase(window, settings);
    // The switch-over (the new renderer presents, device.h): no DirectDraw driver is listed or
    // tried; the device object is built without one.
    GUID* driverId = nullptr;
    void* memory = gamecrt::operator_new(0x17C);
    CTextureDevice* device = nullptr;
    if (memory) {
        device = static_cast<CTextureDevice*>(memory)->Construct(static_cast<HWND__*>(window), driverId, settings->fullScreen_, settings->resolutionX_,
                                 settings->resolutionY_, settings->colourDepth_, 0, 1, settings->zBufferDepth_, 0);
    }
    DeviceHolderAssign(&device_, nullptr, device);
    DeviceSetPipeline(&device_->pipeline_, pipeline_);

    FillMatrixStacks(this);
    SetViewport(this, settings);
    SetRenderState(this, D3DRENDERSTATE_ZENABLE, device_->zEnable_);
    SetRenderState(this, D3DRENDERSTATE_ZWRITEENABLE, 1);
    SetRenderState(this, D3DRENDERSTATE_FILLMODE, D3DFILL_SOLID);
    SetRenderState(this, D3DRENDERSTATE_SHADEMODE, D3DSHADE_GOURAUD);

    // The device's capabilities, then DirectDraw's (driver and emulation, 0x17C bytes each).
    D3DDEVICEDESC7 deviceDesc;   // not cleared, as in the original
    CTextureDevice* holder = device_;
    long result = device::GetCaps(static_cast<IDirect3DDevice7*>(holder->d3dDevice_), &deviceDesc);
    holder->lastLoadResult_ = result;
    if (result != 0) app::FatalErrorCode(result, "Can't get device description.\n");   // "Can't get device description"
    DDCAPS driverCaps = {};
    DDCAPS emulationCaps = {};
    driverCaps.dwSize = sizeof(DDCAPS);
    emulationCaps.dwSize = sizeof(DDCAPS);
    device::GetCaps(static_cast<IDirectDraw7*>(device_->ddraw_), &driverCaps, &emulationCaps);
    if (!(driverCaps.ddsCaps.dwCaps & DDSCAPS_MIPMAP)) settings->mipmapping_ = 0;

    SetTextureStages(this, settings, deviceDesc);
    SetRenderStates(this, settings);

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

    // The ambient colour (slot 7): grey 50 from three FISTPs of 50.0.
    std::uint32_t r = static_cast<std::uint32_t>(x87::fistp(50.0f));
    std::uint32_t g = static_cast<std::uint32_t>(x87::fistp(50.0f));
    std::uint32_t b = static_cast<std::uint32_t>(x87::fistp(50.0f));
    std::uint32_t ambient = ((((r | 0xFFFFFF00u) << 8) | g) << 8) | b;
    SetColour(ambient);

    CreateHelper(this, settings);
}
EI_REPLACE_METHOD(0x00500B70, CSceneRenderer::CreateRendererImpl);

// 00501980 (ECX = the matrix; only SetView calls it). Transposes a 4x4 matrix in place.
void __fastcall MatrixTransposeInPlace(float* m) { Transpose(m); }
EI_REPLACE(0x00501980, MatrixTransposeInPlace);

EI_REPLACE_METHOD(0x005019D0, CTransformPipeline::Top);

void CSceneRenderer::SetView(const tagRECT* screen, float fieldOfView) {
    CTransformPipeline* pipeline = pipeline_;
    MatrixStack* stacks = pipeline->stacks_;
    for (int i = 15; i >= 0; --i) stacks[i].count = 0;
    const CCamera* cam = camera::TheCamera();
    float zNear, zFar, fov;
    std::memcpy(&zNear, &cam->nearDistance_, 4);
    std::memcpy(&zFar, &cam->farDistance_, 4);
    std::memcpy(&fov, &cam->fieldOfView_, 4);
    // Any field of view but 0 (and NaN) replaces the camera's.
    if (fieldOfView < 0.0f || fieldOfView > 0.0f) std::memcpy(&fov, &fieldOfView, 4);
    const int width = screen->right - screen->left;
    const int height = screen->bottom - screen->top;
    Matrix4 m;
    ProjectionMatrix(&m, fov, static_cast<float>(width) / static_cast<float>(height), zNear, zFar);
    StackPush(stacks[kProjection], &m);
    ViewportMatrix(&m, reinterpret_cast<const int*>(screen));
    StackPush(stacks[kViewport], &m);
    AxisSwapMatrix(&m);
    StackPush(stacks[kAxisSwap], &m);
    // Unlike the creation, only the viewport and axis swap products are inlined; the rest go
    // through the pipeline's own push (004D9B80).
    StackPush(stacks[kStackA], StackTop(stacks[kViewport]));
    StackPush(stacks[kStackA], StackTop(stacks[kAxisSwap]));
    StackPush(stacks[kStackA], StackTop(stacks[kProjection]));
    StackPush(stacks[kStackB], StackTop(stacks[kViewport]));
    StackPush(stacks[kStackB], StackTop(stacks[kAxisSwap]));
    pipeline_->PushMatrix(StackTop(pipeline_->stacks_[kProjection])->m, kStackB);
    for (int stack : kSwapStacks) {
        pipeline = pipeline_;
        pipeline->PushMatrix(pipeline->Top(kAxisSwap)->m, stack);
        pipeline = pipeline_;
        pipeline->PushMatrix(pipeline->Top(kProjection)->m, stack);
    }
    Matrix4 transposed;
    std::memcpy(&transposed, pipeline_->Top(kProjection), sizeof(Matrix4));
    Transpose(transposed.m);
    Matrix4 copy;
    std::memcpy(&copy, &transposed, sizeof(Matrix4));
    pipeline_->transform_->SetProjection(copy.m);
    pipeline_->transform_->SetViewportSize(static_cast<float>(screen->right - screen->left),
                                           static_cast<float>(screen->bottom - screen->top));
}
EI_REPLACE_METHOD(0x005015A0, CSceneRenderer::SetView);
