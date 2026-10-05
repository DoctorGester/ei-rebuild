// CWorldClient's draw path: Render (00569E40) and DrawPass (0056AFE0), with Rebirth's
// LandscapeDrawRadius and ObjectsDrawRadius behind their flags. See world.h and
// docs/notes/world.md ("Map of the client frame").
//
// Both talk to Direct3D 7 through the renderer (007AF080: +0x54 the IDirect3DDevice7, +0x08 the
// last state block result) and to the render device's matrix stacks (007AF068), exactly as the
// original: the same calls in the same order. Callees are reached through their original
// addresses (rewritten ones included, so the verify build's wrappers see the calls).
#include "world.h"

#include "ei/hook.h"
#include "render/render_state.h"
#include "ei/rebirth.h"
#include "ei/vcall.h"
#include "lib/gameapi.h"
#include "render/render.h"
#include "originals_core.h"
#include "originals_library.h"
#include "originals_world.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include "originals_render.h"
#include "originals_units.h"

#pragma warning(disable : 4740) // naked forwarders, inline assembly

namespace {

template <typename T>
__forceinline T& Field(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(object) + offset);
}

void* const kContext = reinterpret_cast<void*>(0x007AF060);   // the scene context (00500960 returns it)
char* Renderer() { return *reinterpret_cast<char**>(0x007AF080); }
char* Device() { return *reinterpret_cast<char**>(0x007AF068); }

// IDirect3DDevice7 (renderer +0x54): SetRenderState (slot 20), ApplyStateBlock (slot 39; its
// HRESULT goes to the renderer's +0x08).
void SetRenderState(char* renderer, int state, std::uint32_t value) {
    void* d3d = Field<void*>(renderer, 0x54);
    using Fn = long(__stdcall*)(void*, int, std::uint32_t);
    reinterpret_cast<Fn>((*static_cast<void***>(d3d))[0x50 / 4])(d3d, state, value);
    render::state::Render(static_cast<std::uint32_t>(state), value);
}
void ApplyStateBlock(char* renderer, std::uintptr_t handleAddress) {
    void* d3d = Field<void*>(renderer, 0x54);
    using Fn = long(__stdcall*)(void*, std::uint32_t);
    Field<long>(renderer, 8) = reinterpret_cast<Fn>((*static_cast<void***>(d3d))[0x9C / 4])(
        d3d, *reinterpret_cast<const std::uint32_t*>(handleAddress));
    render::state::Block(*reinterpret_cast<const std::uint32_t*>(handleAddress));
}
constexpr std::uintptr_t kStateBlockScene = 0x007AEDD0;
constexpr std::uintptr_t kStateBlockAlpha = 0x007AEDD8;
constexpr std::uintptr_t kStateBlockTerrain = 0x007AEDDC;

// 004E7840 / 004E7A40: the renderer flushes its two batched vertex buffers.
void __fastcall RendererFlushA(char* a0) { DeviceFlushBatchAOriginal((void*)a0); }
void __fastcall RendererFlushB(char* a0) { DeviceFlushBatchBOriginal((void*)a0); }
void Flush(char* renderer) {
    RendererFlushA(renderer);
    RendererFlushB(renderer);
}

// The render device's matrix stacks (pointer, depth): +0x1C/+0x20 and +0x24/+0x28 pushed by
// 004D9B80 (types 1 and 2), +0x3C/+0x40, and the view stack +0x5C/+0x60.
void __fastcall PushSceneMatrix(char* a0, void*, const float* a2, int a3) { PushSceneMatrixOriginal((void*)a0, nullptr, a2, a3); }
inline void __fastcall MultiplyMatrix(float* out, void*, const float* a, const float* b) { MatrixMultiplyOriginal(out, nullptr, a, b); }
// 004F0EF0: the view matrix of the camera 0079B2A8 into `out`.
__declspec(naked) void __fastcall DeviceViewMatrix(char*, void*, float*, const void*) EI_ORIGINAL(0x004F0EF0)

// Pushes `matrix` on the stack at `stackOffset` (its depth follows), as the original inlines it.
void PushMatrix(char* device, int stackOffset, const float* matrix) {
    int& depth = Field<int>(device, stackOffset + 4);
    float* stack = Field<float*>(device, stackOffset);
    if (depth == 0) {
        depth = 1;
        std::memcpy(stack, matrix, 0x40);
    } else {
        int d = ++depth;
        MultiplyMatrix(stack + (d - 1) * 16, nullptr, stack + (d - 2) * 16, matrix);
    }
}
void PopMatrix(char* device, int depthOffset) {
    int& depth = Field<int>(device, depthOffset);
    if (depth) --depth;
}

// Render's callees.
__declspec(naked) void __fastcall ClientBeginFrame(CWorldClient*) EI_ORIGINAL(0x00566360)
__declspec(naked) void __fastcall SkyDraw(void*, void*, void*, const void*) EI_ORIGINAL(0x004D96E0)
__declspec(naked) void __fastcall CameraFrame(void*, void*, void*) EI_ORIGINAL(0x0040E810)
__declspec(naked) void __fastcall TerrainSetView(void*, void*, float, float, float) EI_ORIGINAL(0x006CA260)
__declspec(naked) void __fastcall TerrainDrawFirst(void*, void*, void*) EI_ORIGINAL(0x006C6230)
__declspec(naked) void __fastcall TerrainDrawSecond(void*, void*, void*) EI_ORIGINAL(0x006C6330)
__declspec(naked) int __fastcall DrawPassAt(CWorldClient*) EI_ORIGINAL(0x0056AFE0)
__declspec(naked) void __fastcall EffectsDraw(void*, void*, void*) EI_ORIGINAL(0x004E2700)
__declspec(naked) void __fastcall ParticlesDraw(void*, void*, void*) EI_ORIGINAL(0x00498940)
__declspec(naked) void __fastcall LightningDraw(void*, void*, void*) EI_ORIGINAL(0x00468940)
__declspec(naked) void __fastcall RibbonsDraw(void*, void*, void*) EI_ORIGINAL(0x004E5680)
__declspec(naked) void __fastcall ClientDrawOverlays(CWorldClient*) EI_ORIGINAL(0x00569D60)
__declspec(naked) void __fastcall HitPointsDraw(void*, void*, void*) EI_ORIGINAL(0x005E18E0)
constexpr int kSlotParticles = 0x90 / 4;   // CWorldClient: the particle manager

// A starter setting (HKCU\Software\Gipat.Ru\EI_Starter\Settings, a DWORD, 100 when absent) as
// addon turns it into the float it patches in: (float)(unsigned)value, read once (addon reads its
// settings when it loads).
float StarterDrawRadius(const char* name) {
    DWORD value = 100;
    HKEY key;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Gipat.Ru\\EI_Starter\\Settings", 0, KEY_READ, &key) == 0) {
        DWORD data = 0, size = sizeof(data), type = 0;
        if (RegQueryValueExA(key, name, nullptr, &type, reinterpret_cast<BYTE*>(&data), &size) == 0 && type == REG_DWORD)
            value = data;
        RegCloseKey(key);
    }
    volatile float radius = static_cast<float>(static_cast<double>(value));
    return radius;
}

// The terrain's draw distance (the PUSH 100.0 of 00569E40).
float LandscapeDrawRadius() {
    static const float setting = StarterDrawRadius("LandscapeDrawRadius");
    return ei::rebirth::Enabled(ei::rebirth::Patch::LandscapeDrawRadius) ? setting : 100.0f;
}

// DrawPass's callees.
__declspec(naked) void __fastcall DeferredClear(void*) EI_ORIGINAL(0x005703C0)
__declspec(naked) void __fastcall VectorClear(void*) EI_ORIGINAL(0x0056E870)
__declspec(naked) float* __fastcall CameraPosition(void*) EI_ORIGINAL(0x0040FB20)
__declspec(naked) void __fastcall SoundListener(void*, void*, float) EI_ORIGINAL(0x0067CEF0)
__declspec(naked) int __fastcall CollectVisibleAt(CWorldClient*, void*, void*, const float*, float, std::uint32_t, int, int)
    EI_ORIGINAL(0x0056A990)
__declspec(naked) void __fastcall NodeFree12(void*, void*, void*, int) EI_ORIGINAL(0x0056F700)
__declspec(naked) void __fastcall NodeFree32(void*, void*, void*, int) EI_ORIGINAL(0x0056F840)
__declspec(naked) void __fastcall IntroSortByModelAt(CEngineObject**, CEngineObject**, void*, int, void*)
    EI_ORIGINAL(0x005740D0)
__declspec(naked) void __fastcall FinalSortByModelAt(CEngineObject**, CEngineObject**, void*) EI_ORIGINAL(0x00574390)
__declspec(naked) void __fastcall IntroSortByModelUseAt(CEngineObject**, CEngineObject**, void*, int, void*)
    EI_ORIGINAL(0x00573D50)
__declspec(naked) void __fastcall FinalSortByModelUseAt(CEngineObject**, CEngineObject**, void*) EI_ORIGINAL(0x00573F10)
__declspec(naked) void __fastcall QueueSelected(CEngineObject*, void*) EI_ORIGINAL(0x0056A370)
__declspec(naked) void __fastcall LightEffectDraw(void*) EI_ORIGINAL(0x006CEDE0)
__declspec(naked) void __fastcall QuaternionAxisZ(const void*, void*, float*) EI_ORIGINAL(0x0040D6B0)
__declspec(naked) void __fastcall QuaternionAxisX(const void*, void*, float*) EI_ORIGINAL(0x0040D940)
void __fastcall MarkerBegin(void* a0, void*, int a2, int a3) { DeviceSetTextureOriginal(a0, nullptr, a2, (void*)a3); }
void* __fastcall ObjectFindPart(CEngineObject* a0, void*, const char* a2) { return (void*)ObjectFindPartOriginal((void*)a0, nullptr, a2); }
__declspec(naked) float* __fastcall ObjectPartPosition(CEngineObject*, void*, float*, void*) EI_ORIGINAL(0x005188B0)
__declspec(naked) void __fastcall MarkerDraw(void*, void*, const float*, float, const float*, const float*)
    EI_ORIGINAL(0x00503A40)
__declspec(naked) void __fastcall ListErase(void*, void*, void**, void*) EI_ORIGINAL(0x00512030)
__declspec(naked) CEngineObject** __fastcall VectorCopyBack(CEngineObject**, CEngineObject**, CEngineObject**)
    EI_ORIGINAL(0x00571E30)
__declspec(naked) void __fastcall VectorErase(void*, void*, CEngineObject**, CEngineObject**) EI_ORIGINAL(0x0056F5E0)

// The frame's draw lists (STLport vectors of object pointers) and the deferred unit list.
struct ObjectVector {
    CEngineObject** begin;
    CEngineObject** end;
    CEngineObject** capacity;
};
ObjectVector& List(std::uintptr_t address) { return *reinterpret_cast<ObjectVector*>(address); }
constexpr std::uintptr_t kTouchedModels = 0x007AF508;
constexpr std::uintptr_t kShadow52 = 0x007AF518;
constexpr std::uintptr_t kShadowUnits = 0x007AF528;
constexpr std::uintptr_t kShadow5D = 0x007AF538;
constexpr std::uintptr_t kShadowOther = 0x007AF548;
constexpr std::uintptr_t kShadow54 = 0x007AF558;
constexpr std::uintptr_t kVisible52 = 0x007AF568;
constexpr std::uintptr_t kVisibleUnits = 0x007AF578;
constexpr std::uintptr_t kVisible5D = 0x007AF588;
constexpr std::uintptr_t kVisibleOther = 0x007AF598;
constexpr std::uintptr_t kVisible54 = 0x007AF5A8;
void* const kDeferred = reinterpret_cast<void*>(0x007AF5B4);

struct ListNode {
    ListNode* next;
    ListNode* prev;
};
ListNode* Head(const void* list) { return *static_cast<ListNode* const*>(list); }

// The object margin around the camera when collecting what to draw (0073BAB0, 100.0).
float ObjectsDrawRadius() {
    static const float setting = StarterDrawRadius("ObjectsDrawRadius");
    return ei::rebirth::Enabled(ei::rebirth::Patch::ObjectsDrawRadius) ? setting
                                                                         : *reinterpret_cast<const float*>(0x0073BAB0);
}

// FLD a; FADD/FSUB b; FSTP: the result rounded to float.
float AddRounded(float a, float b) {
    float r;
    __asm {
        fld a
        fadd b
        fstp r
    }
    return r;
}
float SubRounded(float a, float b) {
    float r;
    __asm {
        fld a
        fsub b
        fstp r
    }
    return r;
}
// FLD a; FCOMP b: whether C0 is set (a < b, or unordered).
bool LessOrUnordered(float a, float b) {
    int status;
    __asm {
        fld a
        fcomp b
        fnstsw ax
        movzx eax, ah
        mov status, eax
    }
    return (status & 1) != 0;
}
// The original's mask selects: C0 picks a's bits, else b's (the smaller one) ...
float SelectLess(float a, float b) { return LessOrUnordered(a, b) ? a : b; }
// ... and, with FLD 0; FCOMP t, t when 0 < t (or unordered), else 0 (the larger one).
float ClampToZero(float t) { return LessOrUnordered(0.0f, t) ? t : 0.0f; }
// Copies a float through the FPU.
float Reload(float v) {
    float r;
    __asm {
        fld v
        fstp r
    }
    return r;
}
// The game's _ftol of a float.
int Ftol(float v) {
    int r;
    __asm {
        fld v
        mov eax, 0x006E853C
        call eax
        mov r, eax
    }
    return r;
}
// (float)(int)[007AF61C] + [007AFCA8], rounded once (FILD, FADD, FSTP).
float AnimationTime() {
    float r;
    __asm {
        fild dword ptr ds:[0x007AF61C]
        fadd dword ptr ds:[0x007AFCA8]
        fstp r
    }
    return r;
}

// floor(log2(n)) as the original counts it (n >= 1).
int Log2(int n) {
    int depth = 0;
    while (n != 1) {
        n >>= 1;
        ++depth;
    }
    return depth;
}

// Sorts one draw list with UnitOrderLess (the comparator gets the renderer).
void SortByModel(std::uintptr_t address, char* renderer) {
    ObjectVector& v = List(address);
    CEngineObject** first = v.begin;
    CEngineObject** last = v.end;
    if (first == last) return;
    IntroSortByModelAt(first, last, nullptr, Log2(static_cast<int>(last - first)) * 2, renderer);
    FinalSortByModelAt(first, last, renderer);
}

// Calls virtual slot `slot` (one argument, the scene context) of every object of a draw list.
void DrawEach(std::uintptr_t address, int slot) {
    CEngineObject** end = List(address).end;
    for (CEngineObject** p = List(address).begin; p != end; ++p) ei::VCall<void>(*p, slot, kContext);
}
constexpr int kSlotDraw = 0x58 / 4;         // 005783C0
constexpr int kSlotDrawUnit = 0x5C / 4;     // 005783A0
constexpr int kSlotDrawShadow = 0x60 / 4;   // 005783B0

// Empties the deferred unit list: each node's counted pointer released, the node freed with
// `fastFree` (0056FB40) or 0056F700.
void EmptyDeferred(bool fastFree) {
    ListNode* head = Head(kDeferred);
    ListNode* node = head->next;
    while (node != head) {
        ListNode* current = node;
        node = node->next;
        CountedRelease(reinterpret_cast<char*>(current) + 8, nullptr, 0);
        if (fastFree) NodeFree12Fast(current, 1);
        else NodeFree12(kDeferred, nullptr, current, 1);
        head = Head(kDeferred);
    }
    head->next = head;
    Head(kDeferred)->prev = Head(kDeferred);
}

// Calls 006CEDE0 on the value of every node of an effect list (this + 0x1B8 / 0x1BC / 0x1C0).
void DrawLightEffects(void* list) {
    ListNode* head = Head(list);
    for (ListNode* node = head->next; node != head; node = node->next) LightEffectDraw(reinterpret_cast<char*>(node) + 8);
}

} // namespace

// ---- Render (00569E40) --------------------------------------------------------------------------

int CWorldClient::Render() {
    // The new renderer (src/render) tells the world's meshes from the interface's by this.
    render::BeginWorld();
    char* renderer = Renderer();
    char* device = Device();
    ClientBeginFrame(this);
    Flush(renderer);
    float view[16];
    DeviceViewMatrix(device, nullptr, view, reinterpret_cast<const void*>(0x0079B2A8));
    PopMatrix(device, 0x60);
    PushMatrix(device, 0x5C, view);
    const float* top = Field<float*>(device, 0x5C) + (Field<int>(device, 0x60) - 1) * 16;
    PushSceneMatrix(device, nullptr, top, 2);
    PushSceneMatrix(device, nullptr, top, 1);
    PushMatrix(device, 0x3C, Field<float*>(device, 0x5C) + (Field<int>(device, 0x60) - 1) * 16);

    std::uint32_t flags = flags_;
    void* sky = Field<void*>(this, 0x1AC);
    if ((flags & 0x1000) && sky) {
        SetRenderState(renderer, 15, 0);
        ApplyStateBlock(renderer, kStateBlockScene);
        SetRenderState(renderer, 14, 0);
        SetRenderState(renderer, 22, 1);
        SetRenderState(renderer, 29, 1);
        Field<int>(renderer, 0x158) = 0;
        SkyDraw(Field<void*>(this, 0x1AC), nullptr, kContext, Field<char*>(this, 0x210) + 0x1C);
        Flush(renderer);
        Field<int>(renderer, 0x158) = 1;
        SetRenderState(renderer, 22, 2);
        SetRenderState(renderer, 29, 0);
        SetRenderState(renderer, 14, 1);
    }
    NetworkTimers();
    if ((flags_ & 0x100) && Field<void*>(this, 0x180)) {
        char frame[12];
        char* camera = Field<char*>(this, 0x210);
        CameraFrame(camera, nullptr, frame);
        camera = Field<char*>(this, 0x210);
        TerrainSetView(Field<void*>(this, 0x180), nullptr, Field<float>(camera, 0x1C), Field<float>(camera, 0x20),
                       LandscapeDrawRadius());
        ApplyStateBlock(renderer, kStateBlockTerrain);
        TerrainDrawFirst(Field<void*>(this, 0x180), nullptr, kContext);
        Flush(renderer);
    }
    DrawPassAt(this);
    NetworkTimers();
    if ((flags_ & 0x100) && Field<void*>(this, 0x180)) {
        char frame[12];
        char* camera = Field<char*>(this, 0x210);
        CameraFrame(camera, nullptr, frame);
        camera = Field<char*>(this, 0x210);
        TerrainSetView(Field<void*>(this, 0x180), nullptr, Field<float>(camera, 0x1C), Field<float>(camera, 0x20),
                       LandscapeDrawRadius());
        Flush(renderer);
        SetRenderState(renderer, 15, 0);
        ApplyStateBlock(renderer, kStateBlockScene);
        TerrainDrawSecond(Field<void*>(this, 0x180), nullptr, kContext);
        Flush(renderer);
    }
    NetworkTimers();
    if (flags_ & 0x400) {
        Flush(renderer);
        ApplyStateBlock(renderer, kStateBlockAlpha);
        SetRenderState(Renderer(), 27, 1);
        SetRenderState(Renderer(), 15, 0);
        SetRenderState(Renderer(), 19, 5);
        SetRenderState(Renderer(), 20, 6);
        SetRenderState(renderer, 14, 0);
        EffectsDraw(Field<void*>(this, 0x1A4), nullptr, kContext);
        Flush(renderer);
        SetRenderState(Renderer(), 19, 5);
        SetRenderState(Renderer(), 20, 6);
        SetRenderState(renderer, 14, 0);
        void* particles = ei::VCall<void*>(this, kSlotParticles);
        ParticlesDraw(particles, nullptr, kContext);
        Flush(renderer);
        SetRenderState(Renderer(), 19, 5);
        SetRenderState(Renderer(), 20, 6);
        LightningDraw(Field<void*>(this, 0x1A0), nullptr, kContext);
        Flush(renderer);
        SetRenderState(Renderer(), 19, 5);
        SetRenderState(Renderer(), 20, 6);
        RibbonsDraw(reinterpret_cast<char*>(this) + 0x6F4, nullptr, kContext);
        Flush(renderer);
        SetRenderState(renderer, 14, 1);
    }
    ApplyStateBlock(renderer, kStateBlockScene);
    NetworkTimers();
    ClientDrawOverlays(this);
    PopMatrix(device, 0x40);
    PopMatrix(device, 0x28);
    PopMatrix(device, 0x20);
    if (flags_ & 0x2000) {
        SetRenderState(renderer, 7, 0);
        ApplyStateBlock(renderer, kStateBlockAlpha);
        Field<int>(renderer, 0x158) = 0;
        HitPointsDraw(Field<void*>(this, 0x1B4), nullptr, kContext);
        Field<int>(renderer, 0x158) = 1;
        Flush(renderer);
        SetRenderState(renderer, 7, Field<std::uint32_t>(renderer, 0x60));
    }
    render::EndWorld();
    return 1;
}
EI_REPLACE_METHOD(0x00569E40, CWorldClient::Render);
EI_REBIRTH_IMPLEMENTS(0x00569E40, ei::rebirth::Patch::LandscapeDrawRadius);

// ---- DrawPass (0056AFE0) ------------------------------------------------------------------------

int CWorldClient::DrawPass() {
    void* terrain = Field<void*>(this, 0x180);
    if (!terrain || !Field<void*>(terrain, 0x100)) return 0;
    char* renderer = Renderer();
    char* device = Device();
    std::uint32_t now = gameapi::timeGetTime();
    const float* view = Field<float*>(device, 0x5C) + (Field<int>(device, 0x60) - 1) * 16;
    DeferredClear(kDeferred);
    static const std::uintptr_t kCleared[] = {kVisible54, kVisibleOther, kVisible5D, kVisibleUnits, kVisible52, kShadow54,
                                              kShadowOther, kShadow5D, kShadowUnits, kShadow52, kTouchedModels};
    for (std::uintptr_t list : kCleared) VectorClear(reinterpret_cast<void*>(list));
    float animTime = AnimationTime();
    const float* position = CameraPosition(Field<void*>(this, 0x210));
    if (void* listener = Field<void*>(this, 0x1B0)) SoundListener(listener, nullptr, animTime);

    // The sectors around the camera: the position widened by the margin, clamped to the map
    // (size - 0073B84C) and to 0, in 16-unit sectors.
    const float margin = ObjectsDrawRadius();
    const float edge = *reinterpret_cast<const float*>(0x0073B84C);
    terrain = Field<void*>(this, 0x180);
    float yHi = SelectLess(AddRounded(position[1], margin), SubRounded(Field<float>(terrain, 0x110), edge));
    float xMax = SubRounded(Field<float>(Field<void*>(this, 0x180), 0x10C), edge);
    float xHi = SelectLess(AddRounded(position[0], margin), xMax);
    float yMax = SubRounded(Field<float>(Field<void*>(this, 0x180), 0x110), edge);
    float yLo = ClampToZero(Reload(SelectLess(SubRounded(position[1], margin), yMax)));
    float xMax2 = SubRounded(Field<float>(Field<void*>(this, 0x180), 0x10C), edge);
    float xLo = ClampToZero(Reload(SelectLess(SubRounded(position[0], margin), xMax2)));
    std::uint32_t sx0 = static_cast<std::uint32_t>(Ftol(xLo)) >> 4;
    Field<std::uint32_t>(this, 0x138) = sx0;
    Field<std::uint32_t>(this, 0x148) = sx0;
    std::uint32_t sy0 = static_cast<std::uint32_t>(Ftol(yLo)) >> 4;
    Field<std::uint32_t>(this, 0x13C) = sy0;
    Field<std::uint32_t>(this, 0x14C) = sy0;
    Field<std::uint32_t>(this, 0x140) = static_cast<std::uint32_t>(Ftol(xHi)) >> 4;
    Field<std::uint32_t>(this, 0x144) = static_cast<std::uint32_t>(Ftol(yHi)) >> 4;

    int touched = 0;
    char** rows = Field<char**>(this, 0x124);
    void* sector = rows[sy0] + sx0 * 4;
    while (sector) {
        touched += CollectVisibleAt(this, nullptr, sector, view, animTime, now, 0, 0);
        std::uint32_t x = ++Field<std::uint32_t>(this, 0x148);
        if (x > Field<std::uint32_t>(this, 0x140)) {
            Field<std::uint32_t>(this, 0x148) = Field<std::uint32_t>(this, 0x138);
            std::uint32_t y = ++Field<std::uint32_t>(this, 0x14C);
            if (y > Field<std::uint32_t>(this, 0x144)) break;
        }
        sector = Field<char**>(this, 0x124)[Field<std::uint32_t>(this, 0x14C)] + Field<std::uint32_t>(this, 0x148) * 4;
    }
    touched += CollectVisibleAt(this, nullptr, kDeferred, view, animTime, now, 1, 0);
    EmptyDeferred(false);
    touched += CollectVisibleAt(this, nullptr, reinterpret_cast<char*>(this) + 0xB4, view, animTime, now, 1, 1);

    // Models in view stay loaded: the most recently used max(10, n / 8) lose flag 0x400.
    ObjectVector& models = List(kTouchedModels);
    if (models.begin != models.end) {
        CEngineObject** first = models.begin;
        CEngineObject** last = models.end;
        IntroSortByModelUseAt(first, last, nullptr, Log2(static_cast<int>(last - first)) * 2, nullptr);
        FinalSortByModelUseAt(first, last, nullptr);
        int count = touched >> 3;
        if (count < 10) count = 10;
        for (CEngineObject** p = models.begin; p != models.end && count != 0; --count) {
            CEngineObject* object = *p++;
            object->flags_ &= ~0x400u;
        }
    }

    if (flags_ & 0xC0) {
        ApplyStateBlock(renderer, kStateBlockAlpha);
        void* queued = reinterpret_cast<char*>(this) + 0xF0;   // an STLport list of 32-byte nodes
        ListNode* node = Head(queued)->next;
        while (node != Head(queued)) {
            ListNode* current = node;
            node = node->next;
            NodeFree32(queued, nullptr, current, 1);
        }
        Head(queued)->next = Head(queued);
        Head(queued)->prev = Head(queued);
        Flush(renderer);
        SetRenderState(renderer, 19, 5);
        SetRenderState(renderer, 20, 6);
        SetRenderState(renderer, 27, 1);
        SetRenderState(renderer, 15, 1);
        Flush(renderer);
    }
    void* queued = reinterpret_cast<char*>(this) + 0xF0;
    if (flags_ & 0x40) {
        SortByModel(kVisible54, renderer);
        SortByModel(kVisibleOther, renderer);
        SortByModel(kVisible5D, renderer);
        SortByModel(kVisible52, renderer);
        SetRenderState(renderer, 24, 0xC8);
        DrawEach(kVisibleOther, kSlotDraw);
        DrawEach(kVisible5D, kSlotDrawUnit);
        CEngineObject** end = List(kVisible5D).end;
        for (CEngineObject** p = List(kVisible5D).begin; p != end; ++p) QueueSelected(*p, queued);
        Flush(renderer);
        DrawEach(kVisible54, kSlotDraw);
        DrawEach(kVisible52, kSlotDraw);
        Flush(renderer);
    }
    if ((flags_ & 0x80) && List(kVisibleUnits).begin != List(kVisibleUnits).end) {
        SortByModel(kVisibleUnits, renderer);
        SetRenderState(renderer, 24, 10);
        ApplyStateBlock(renderer, kStateBlockAlpha);
        DrawEach(kVisibleUnits, kSlotDrawUnit);
        CEngineObject** end = List(kVisibleUnits).end;
        for (CEngineObject** p = List(kVisibleUnits).begin; p != end; ++p) QueueSelected(*p, queued);
        Flush(renderer);
    }

    // Light effects (three lists of the client, each behind its setting).
    char* effects = reinterpret_cast<char*>(this) + 0x1B8;
    if (Head(effects)->next != Head(effects) || Head(effects + 4)->next != Head(effects + 4) ||
        Head(effects + 8)->next != Head(effects + 8)) {
        ApplyStateBlock(renderer, kStateBlockAlpha);
        SetRenderState(renderer, 15, 0);
        SetRenderState(renderer, 14, 0);
        if (*reinterpret_cast<const int*>(0x0079BD50)) DrawLightEffects(effects);
        if (*reinterpret_cast<const int*>(0x0079BD4C)) DrawLightEffects(effects + 4);
        if (*reinterpret_cast<const int*>(0x0079BD54)) DrawLightEffects(effects + 8);
        SetRenderState(renderer, 14, 1);
    }

    // Shadows: projected along the sun (0079B2DC's Z axis scaled by 0073F750).
    if (flags_ & 0x200) {
        SortByModel(kShadow54, renderer);
        SortByModel(kShadowOther, renderer);
        SortByModel(kShadow5D, renderer);
        SortByModel(kShadow52, renderer);
        float matrix[16] = {};
        matrix[15] = 1.0f;
        matrix[10] = 1.0f;
        matrix[5] = 1.0f;
        matrix[0] = 1.0f;
        float sun[3];
        QuaternionAxisZ(reinterpret_cast<const void*>(0x0079B2DC), nullptr, sun);
        const float scale = *reinterpret_cast<const float*>(0x0073F750);
        volatile float product = sun[0] * scale;
        matrix[3] = product;
        product = sun[1] * scale;
        matrix[7] = product;
        product = sun[2] * scale;
        matrix[11] = product;
        PushSceneMatrix(device, nullptr, matrix, 2);
        PushSceneMatrix(device, nullptr, matrix, 1);
        ApplyStateBlock(renderer, kStateBlockAlpha);
        SetRenderState(renderer, 15, 0);
        SetRenderState(renderer, 14, 0);
        DrawEach(kShadow5D, kSlotDrawShadow);
        DrawEach(kShadowOther, kSlotDrawShadow);
        DrawEach(kShadow54, kSlotDrawShadow);
        DrawEach(kShadow52, kSlotDrawShadow);
        DrawEach(kShadowUnits, kSlotDrawShadow);
        Flush(renderer);
        SetRenderState(renderer, 14, 1);
        PopMatrix(device, 0x28);
        PopMatrix(device, 0x20);
    }

    // Selection markers (list +0x7BC of object ids) in single-player mode 1 with units drawn: a
    // marker at each living object (at its named part when it has one); dead ones leave the list.
    void* selected = reinterpret_cast<char*>(this) + 0x7BC;
    if (Head(selected)->next != Head(selected) && *reinterpret_cast<const int*>(0x0079BE64) == 1 && (flags_ & 0x80)) {
        SetRenderState(renderer, 15, 0);
        MarkerBegin(kContext, nullptr, 0, 0);
        void* manager = Field<void*>(this, 0x40);
        ListNode* head = Head(selected);
        for (ListNode* node = head->next; node != head;) {
            ListNode* next = node->next;
            CEngineObject* object = ManagerFindObject(manager, nullptr, Field<int>(node, 8));
            if (object) {
                if (ei::VCall<int>(object, 0xDC / 4) == 0) {
                    float point[3];
                    std::memcpy(point, &Field<float>(object, 0x1C), sizeof(point));
                    if (void* part = ObjectFindPart(object, nullptr, reinterpret_cast<const char*>(0x007900E4))) {
                        float scratch[3];
                        const float* at = ObjectPartPosition(object, nullptr, scratch, part);
                        std::memcpy(point, at, sizeof(point));
                        point[2] = Reload(Field<float>(object, 0x24));
                    }
                    void* orientation = reinterpret_cast<char*>(object) + 0x34;
                    float axisY[3], axisX[3];
                    QuaternionAxisY(orientation, nullptr, axisY);
                    QuaternionAxisX(orientation, nullptr, axisX);
                    MarkerDraw(kContext, nullptr, point, 0.5f, axisX, axisY);
                } else {
                    if (object->flags_ & 0x10) {
                        object->flags_ &= ~0x110u;
                        ei::VCall<void>(Field<void*>(object, 0x94), 0x64 / 4, 0);
                        ei::VCall<void>(Field<void*>(object, 0x94), 0x68 / 4, 0);
                    }
                    void* unused;
                    ListErase(selected, nullptr, &unused, node);
                }
            }
            node = next;
        }
    }

    EmptyDeferred(true);
    static const std::uintptr_t kCopied[] = {kVisible54, kVisibleOther, kVisible5D, kVisibleUnits, kVisible52,
                                             kShadow54, kShadowOther};
    for (std::uintptr_t address : kCopied) {
        ObjectVector& v = List(address);
        v.end = VectorCopyBack(v.end, v.end, v.begin);
    }
    VectorErase(reinterpret_cast<void*>(kShadow5D), nullptr, List(kShadow5D).begin, List(kShadow5D).end);
    VectorErase(reinterpret_cast<void*>(kShadowUnits), nullptr, List(kShadowUnits).begin, List(kShadowUnits).end);
    List(kShadow52).end = VectorCopyBack(List(kShadow52).end, List(kShadow52).end, List(kShadow52).begin);
    VectorErase(reinterpret_cast<void*>(kTouchedModels), nullptr, List(kTouchedModels).begin, List(kTouchedModels).end);
    return 1;
}
EI_REPLACE_METHOD(0x0056AFE0, CWorldClient::DrawPass);
EI_REBIRTH_IMPLEMENTS(0x0056AFE0, ei::rebirth::Patch::ObjectsDrawRadius);
