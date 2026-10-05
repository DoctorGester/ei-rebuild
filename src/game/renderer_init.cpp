// The renderer's creation (00500B70, virtual slot 2 of the renderer at 007AF060, vtable 0073D93C):
// picks the Direct3D driver from the settings, creates the device object, fills the transform
// pipeline's matrix stacks (projection, viewport, the fixed axis swap and their products), sets the
// pipeline's viewport and depth range, the render and texture stage states, a default light and the
// ambient colour, and creates the counted helper object at +0x24. Runs once at start-up. See
// docs/notes/models.md ("Renderer creation").
#include "ei/hook.h"
#include "render/render_state.h"
#include "ei/x87.h"
#include "originals_engine.h"
#include "originals_library.h"

#include <bit>
#include <cstdint>
#include <cstring>

namespace {

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(object) + offset);
}

// --- Forwarders ----------------------------------------------------------------------------------

// 00500540: the window part of the creation (thiscall: window, settings).
__declspec(naked) void __fastcall CreateWindowPart(void*, void*, void* /*window*/, void* /*settings*/) EI_ORIGINAL(0x00500540)
// The Direct3D driver list (007AEE64): 004EE4A0 enumerates, 004EE5D0 the first hardware driver,
// 004EE6D0 the driver with an index (ECX), null when missing (both FatalErrorOriginal on failure).
__declspec(naked) int __cdecl EnumerateDrivers() EI_ORIGINAL(0x004EE4A0)
__declspec(naked) char* __cdecl FirstHardwareDriver() EI_ORIGINAL(0x004EE5D0)
__declspec(naked) char* __fastcall DriverAt(int /*index*/) EI_ORIGINAL(0x004EE6D0)
// 004E5E50: the device object's constructor (0x17C bytes, ten arguments).
__declspec(naked) void* __fastcall DeviceConstruct(void*, void*, void* /*window*/, std::uint32_t /*driver guid*/,
                                                   std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, int,
                                                   int, std::uint32_t, int) EI_ORIGINAL(0x004E5E50)
// 00504E20: the renderer's counted device holder (+0x20) takes the device.
__declspec(naked) void __fastcall HolderAssign(void*, void*, void* /*device*/) EI_ORIGINAL(0x00504E20)
// 004EBE80: the device's +0xAC takes the pipeline.
__declspec(naked) void __fastcall DeviceSetPipeline(void*, void*, void* /*pipeline*/) EI_ORIGINAL(0x004EBE80)
// 0040D380: the camera settings (0079B2A8) give the near and far planes and the field of view.
__declspec(naked) void __fastcall CameraPlanes(void*, void*, float* /*near*/, float* /*far*/, float* /*fov*/) EI_ORIGINAL(0x0040D380)
// Matrices (model.cpp): 004F0E80 projection, 004F1370 viewport, 004F1330 the axis swap (x, -y, z),
// 004D9880 product (ECX = out).
__declspec(naked) float* __stdcall ProjectionMatrix(float*, float /*fov*/, float /*aspect*/, float /*near*/, float /*far*/)
    EI_ORIGINAL(0x004F0E80)
__declspec(naked) float* __stdcall ViewportMatrix(float*, const int* /*rect*/) EI_ORIGINAL(0x004F1370)
__declspec(naked) float* __stdcall AxisSwapMatrix(float*) EI_ORIGINAL(0x004F1330)
__declspec(naked) void __fastcall MultiplyMatrix(float* /*out*/, void*, const float*, const float*) EI_ORIGINAL(0x004D9880)
// FatalErrorOriginal (patched by Rebirth; through its address), FatalErrorOriginal with an HRESULT, exit.
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
// 0068D0A0: a light's colour and position; 005007C0: the renderer adds a light.
__declspec(naked) void __fastcall LightSet(void*, void*, const float* /*colour*/, const float* /*position*/) EI_ORIGINAL(0x0068D0A0)
__declspec(naked) void __fastcall RendererAddLight(void*, void*, void* /*light*/) EI_ORIGINAL(0x005007C0)
// 004FCA60: the 0x118-byte helper object (device, flag).
__declspec(naked) void* __fastcall HelperConstruct(void*, void*, void* /*device*/, std::uint32_t /*flag dword*/) EI_ORIGINAL(0x004FCA60)

std::uint32_t TimeGetTime() {
    auto fn = *reinterpret_cast<std::uint32_t(__stdcall* const*)()>(0x0073B6A0);   // winmm timeGetTime (IAT)
    return fn();
}

// --- COM calls on the Direct3D device (IDirect3DDevice7) and DirectDraw (IDirectDraw7) -----------

void* DeviceOf(char* renderer) { return At<void*>(At<char*>(renderer, 0x20), 0x54); }
long ComCall(void* object, int slot) {
    return reinterpret_cast<long(__stdcall*)(void*)>((*static_cast<void***>(object))[slot])(object);
}
template <typename... Args>
long ComCall(void* object, int slot, Args... args) {
    return reinterpret_cast<long(__stdcall*)(void*, Args...)>((*static_cast<void***>(object))[slot])(object, args...);
}
constexpr int kGetCaps = 3;
constexpr int kSetRenderState = 20;
constexpr int kSetTextureStageState = 37;
constexpr int kDirectDrawGetCaps = 11;

void SetRenderState(char* renderer, int state, std::uint32_t value) {
    ComCall(DeviceOf(renderer), kSetRenderState, state, value);
    render::state::Render(static_cast<std::uint32_t>(state), value);
}
// SetTextureStageState; the result goes to the device object's +8, as every call there does.
void SetStageState(char* renderer, int stage, int type, int value) {
    char* device = At<char*>(renderer, 0x20);
    At<long>(device, 8) = ComCall(At<void*>(device, 0x54), kSetTextureStageState, stage, type, value);
    render::state::Stage(static_cast<std::uint32_t>(stage), static_cast<std::uint32_t>(type), static_cast<std::uint32_t>(value));
}

// --- Matrix stacks -------------------------------------------------------------------------------

// A stack of 4x4 matrices in the pipeline (data at `offset`, count at `offset` + 4): the first
// entry is `m` itself, later ones are the previous top times `m` (004D9880 with the new top as
// out).
void StackPush(char* pipeline, int offset, const float* m) {
    char* base = At<char*>(pipeline, offset);
    int& count = At<int>(pipeline, offset + 4);
    if (count == 0) {
        count = 1;
        std::memcpy(base, m, 64);
        return;
    }
    int n = ++count;
    MultiplyMatrix(reinterpret_cast<float*>(base + (n - 1) * 64), nullptr, reinterpret_cast<float*>(base + (n - 2) * 64),
                   m);
}
const float* StackTop(char* pipeline, int offset) {
    return reinterpret_cast<const float*>(At<char*>(pipeline, offset) + (At<int>(pipeline, offset + 4) - 1) * 64);
}

constexpr int kProjection = 0x44;
constexpr int kViewport = 0x54;
constexpr int kAxisSwap = 0x4C;
constexpr int kStackA = 0x1C;   // viewport * axis swap * projection
constexpr int kStackB = 0x14;   // the same
constexpr int kStackC = 0x24;   // axis swap * projection
constexpr int kStackD = 0x3C;   // the same
constexpr int kProductStacks[2] = {kStackA, kStackB};
constexpr int kSwapStacks[2] = {kStackC, kStackD};

// Transposes a 4x4 float matrix the way the original does: the upper-triangle elements move
// through the FPU (FLD / FSTP), the lower ones as dwords.
void Transpose(float* m) {
    static constexpr int kPairs[6][2] = {{1, 4}, {2, 8}, {3, 12}, {6, 9}, {7, 13}, {11, 14}};
    for (const auto& p : kPairs) {
        std::uint32_t lower;
        std::memcpy(&lower, &m[p[1]], 4);
        float* from = &m[p[0]];
        float* to = &m[p[1]];
        __asm {
            mov eax, from
            mov ecx, to
            fld dword ptr [eax]
            fstp dword ptr [ecx]
        }
        std::memcpy(&m[p[0]], &lower, 4);
    }
}

void FillMatrixStacks(char* renderer) {
    char* pipeline = At<char*>(renderer, 8);
    float zNear, zFar, fov;
    CameraPlanes(reinterpret_cast<void*>(0x0079B2A8), nullptr, &zNear, &zFar, &fov);
    float m[16];
    ProjectionMatrix(m, fov, std::bit_cast<float>(0x3FAAAAABu), zNear, zFar);
    StackPush(pipeline, kProjection, m);
    ViewportMatrix(m, &At<int>(At<char*>(renderer, 0x20), 0x24));
    StackPush(pipeline, kViewport, m);
    AxisSwapMatrix(m);
    StackPush(pipeline, kAxisSwap, m);
    for (int stack : kProductStacks) {
        StackPush(pipeline, stack, StackTop(pipeline, kViewport));
        StackPush(pipeline, stack, StackTop(pipeline, kAxisSwap));
        StackPush(pipeline, stack, StackTop(pipeline, kProjection));
    }
    for (int stack : kSwapStacks) {
        StackPush(pipeline, stack, StackTop(pipeline, kAxisSwap));
        StackPush(pipeline, stack, StackTop(pipeline, kProjection));
    }
    // The projection, transposed, to the pipeline's transform object (slot 4).
    float transposed[16];
    std::memcpy(transposed, StackTop(pipeline, kProjection), 64);
    Transpose(transposed);
    void* transform = At<void*>(pipeline, 0xC);
    float copy[16];
    std::memcpy(copy, transposed, 64);
    reinterpret_cast<void(__fastcall*)(void*, void*, float*)>((*static_cast<void***>(transform))[4])(transform, nullptr, copy);
}

// The pipeline's viewport size (slot 1, floats) and depth range (slot 5: settings +0x264 and +0x1E4;
// the first version passed only one argument, and the callee's RET 8 then unbalanced the stack).
void SetViewport(char* renderer, char* settings) {
    char* device = At<char*>(renderer, 0x20);
    void* transform = At<void*>(At<char*>(renderer, 8), 0xC);
    float width = static_cast<float>(At<int>(device, 0x2C) - At<int>(device, 0x24));
    float height = static_cast<float>(At<int>(device, 0x30) - At<int>(device, 0x28));
    reinterpret_cast<void(__fastcall*)(void*, void*, float, float)>((*static_cast<void***>(transform))[1])(
        transform, nullptr, width, height);
    void* transform2 = At<void*>(At<char*>(renderer, 8), 0xC);
    reinterpret_cast<void(__fastcall*)(void*, void*, std::uint32_t, std::uint32_t)>((*static_cast<void***>(transform2))[5])(
        transform2, nullptr, At<std::uint32_t>(settings, 0x264), At<std::uint32_t>(settings, 0x1E4));
}

// The texture filters of every stage the device can use at once: settings +0x74 picks point (0)
// or linear (1, 2) min / mag filters; mipmaps (0x12) linear with settings +0x70, else none.
void SetTextureStages(char* renderer, char* settings, const std::uint8_t* deviceDesc) {
    for (int stage = 0;; ++stage) {
        std::uint32_t stages;
        std::memcpy(&stages, deviceDesc + 0xBA, 4);
        if (stage >= static_cast<int>(stages & 0xFFFF)) break;
        int filter = At<int>(settings, 0x74);
        if (filter == 0 || filter == 1 || filter == 2) {
            int value = filter == 0 ? 1 : 2;
            SetStageState(renderer, stage, 0x11, value);
            SetStageState(renderer, stage, 0x10, value);
            if (At<int>(settings, 0x70)) SetStageState(renderer, stage, 0x12, 2);
        }
        if (At<int>(settings, 0x70) == 0) SetStageState(renderer, stage, 0x12, 1);
    }
}

void SetRenderStates(char* renderer, char* settings) {
    SetRenderState(renderer, 0x1B, 1);
    SetRenderState(renderer, 0x0F, 1);
    SetRenderState(renderer, 0x19, 5);
    SetRenderState(renderer, 0x18, 200);
    SetRenderState(renderer, 0x13, 2);
    SetRenderState(renderer, 0x14, 1);
    SetRenderState(renderer, 0x16, 2);
    SetRenderState(renderer, 0x1D, At<std::uint32_t>(settings, 0x64));
    SetRenderState(renderer, 0x1A, At<std::uint32_t>(settings, 0x60));
    SetRenderState(renderer, 0x04, At<std::uint32_t>(settings, 0x78));
    SetRenderState(renderer, 0x02, At<int>(settings, 0x68) ? 2 : 0);
    SetRenderState(renderer, 0x1C, 1);
    SetRenderState(renderer, 0x8C, 3);
    SetRenderState(renderer, 0x22, 0);
    SetRenderState(renderer, 0x8A, 0);
    SetRenderState(renderer, 0x88, 0);
    SetRenderState(renderer, 0x89, 0);
    SetRenderState(renderer, 0x8B, 0);
}

// The default light: class 0x2E of the light factory (*007AF748), white, at (5, 5, -8), range
// 1e6 (+0xE0), +0x100 = 1e12, +0x104 = 1e-12.
void AddDefaultLight(char* renderer) {
    void* factory = *reinterpret_cast<void**>(0x007AF748);
    void* light = reinterpret_cast<void*(__fastcall*)(void*, void*, int, int)>((*static_cast<void***>(factory))[23])(
        factory, nullptr, 0x2E, -1);
    float colour[3] = {1.0f, 1.0f, 1.0f};
    float position[3] = {5.0f, 5.0f, -8.0f};
    LightSet(light, nullptr, colour, position);
    At<std::uint32_t>(light, 0xE0) = 0x49742400;
    At<std::uint32_t>(light, 0x100) = 0x5368D4A5;
    At<std::uint32_t>(light, 0x104) = 0x2B8CBCCC;
    RendererAddLight(renderer, nullptr, light);
}

// The counted helper object (+0x24). The flag argument is pushed as EDX with only DL set, the rest
// of EDX being what operator new left there; the same value is passed.
void CreateHelper(char* renderer, char* settings) {
    std::uint32_t edx = 0;
    void* memory = NewKeepingEdx(0x118, &edx);
    void* helper = nullptr;
    if (memory) {
        void* device = DeviceOf(renderer);
        std::uint32_t flag = (edx & 0xFFFFFF00u) | (At<int>(settings, 0x2C) != 0 ? 1u : 0u);
        helper = HelperConstruct(memory, nullptr, device, flag);
    }
    void* old = At<void*>(renderer, 0x24);
    At<void*>(renderer, 0x24) = helper;
    if (helper) ++At<int>(helper, 4);
    if (old && --At<int>(old, 4) == 0) {
        reinterpret_cast<void(__fastcall*)(void*, void*, int)>((*static_cast<void***>(old))[0])(old, nullptr, 1);
    }
}

} // namespace

// 00500B70 (thiscall, RET 8).
void __fastcall RendererCreate(char* renderer, void*, void* window, char* settings) {
    render::state::Reset();   // a new device: nothing it has is known yet
    CreateWindowPart(renderer, nullptr, window, settings);
    EnumerateDrivers();
    char* driver = FirstHardwareDriver();
    if (!driver) {
        FatalErrorOriginal(reinterpret_cast<const char*>(0x0078E534));   // "Can't find any driver with HW 3D..."
        ExitGame(-1);
    }
    int index = At<int>(settings, 0x2C);
    if (index != -1) {
        driver = DriverAt(index);
        if (!driver) driver = FirstHardwareDriver();
    }
    void* memory = reinterpret_cast<void*(__cdecl*)(std::uint32_t)>(0x007005AF)(0x17C);
    void* device = nullptr;
    if (memory) {
        device = DeviceConstruct(memory, nullptr, window, At<std::uint32_t>(driver, 0x10), At<std::uint32_t>(settings, 0x44),
                                 At<std::uint32_t>(settings, 0x30), At<std::uint32_t>(settings, 0x34),
                                 At<std::uint32_t>(settings, 0x38), 0, 1, At<std::uint32_t>(settings, 0x3C), 0);
    }
    HolderAssign(&At<char>(renderer, 0x20), nullptr, device);
    DeviceSetPipeline(&At<char>(At<char*>(renderer, 0x20), 0xAC), nullptr, At<void*>(renderer, 8));

    FillMatrixStacks(renderer);
    SetViewport(renderer, settings);
    SetRenderState(renderer, 7, At<std::uint32_t>(At<char*>(renderer, 0x20), 0x60));
    SetRenderState(renderer, 0x0E, 1);
    SetRenderState(renderer, 8, 3);
    SetRenderState(renderer, 9, 2);

    // The device's capabilities, then DirectDraw's (driver and emulation, 0x17C bytes each).
    std::uint8_t deviceDesc[0x200];   // D3DDEVICEDESC7 (0xF0 bytes), not cleared, as in the original
    char* holder = At<char*>(renderer, 0x20);
    long result = ComCall(At<void*>(holder, 0x54), kGetCaps, deviceDesc);
    At<long>(holder, 8) = result;
    if (result != 0) FatalDirectX(result, reinterpret_cast<const char*>(0x0078EE20));   // "Can't get device description"
    std::uint32_t driverCaps[0x5F] = {};
    std::uint32_t emulationCaps[0x5F] = {};
    driverCaps[0] = 0x17C;
    emulationCaps[0] = 0x17C;
    ComCall(At<void*>(At<char*>(renderer, 0x20), 0xC), kDirectDrawGetCaps, driverCaps, emulationCaps);
    std::uint32_t ddsCaps;
    std::memcpy(&ddsCaps, reinterpret_cast<const char*>(driverCaps) + 0x16C, 4);
    if (!(ddsCaps & 0x400000)) At<int>(settings, 0x70) = 0;   // no mipmaps

    SetTextureStages(renderer, settings, deviceDesc);
    SetRenderStates(renderer, settings);

    At<int>(renderer, 0x34) = 0;
    At<std::uint32_t>(renderer, 4) |= 2;
    At<std::uint32_t>(renderer, 0x38) = 0x80000000;
    At<int>(renderer, 0x3C) = 0;
    At<int>(renderer, 0x44) = 0;
    At<int>(renderer, 0x48) = 0;
    At<int>(renderer, 0x40) = 0;
    At<std::uint32_t>(renderer, 0x50) = TimeGetTime();
    At<int>(renderer, 0x54) = 0;
    At<int>(renderer, 0x58) = 0;
    AddDefaultLight(renderer);

    // The ambient colour (slot 7): grey 50 from three FISTPs of 50.0.
    std::uint32_t r = static_cast<std::uint32_t>(x87::fistp(50.0f));
    std::uint32_t g = static_cast<std::uint32_t>(x87::fistp(50.0f));
    std::uint32_t b = static_cast<std::uint32_t>(x87::fistp(50.0f));
    std::uint32_t ambient = ((((r | 0xFFFFFF00u) << 8) | g) << 8) | b;
    reinterpret_cast<void(__fastcall*)(void*, void*, std::uint32_t)>((*reinterpret_cast<void***>(renderer))[7])(
        renderer, nullptr, ambient);

    CreateHelper(renderer, settings);
}
EI_REPLACE(0x00500B70, RendererCreate);
