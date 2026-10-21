// The hero preview of the camp (0060F050) and the multiplayer hero screen (006159E0): 005EC300.
// See ui.h (CUiWindow::DrawHeroPreview) and docs/notes/ui.md ("Camp, global map, hero screens").
//
// Floating point follows the original's x87 code: values it stores as float are rounded with
// static_cast<float>, values it keeps on the FPU stack stay unrounded (double), products and sums
// keep its operand order, constants are read from the exe's memory.
#include "ui.h"
#include "model.h"

#include "ei/hook.h"
#include "lib/vecmath.h"
#include "render/render_state.h"
#include "ei/x87.h"
#include "lib/gameapi.h"
#include "render/render.h"
#include "originals_render.h"
#include "game_data.h"
#include "game_singletons.h"

#include <cstdint>
#include <cstring>

namespace {

inline double D(float f) { return f; }
float GameFloat(std::uintptr_t address) { return ei::GameData<const volatile float>(address); }
template <typename T>
T Get(const void* p, int offset) {
    T v;
    std::memcpy(&v, static_cast<const std::uint8_t*>(p) + offset, sizeof(T));
    return v;
}
template <typename T>
void Set(void* p, int offset, T v) {
    std::memcpy(static_cast<std::uint8_t*>(p) + offset, &v, sizeof(T));
}

// A game object's virtual function (thiscall: the object in ECX).
template <typename R = void, typename... Args>
R VirtualCall(void* object, int slot, Args... args) {
    void* fn = (*static_cast<void***>(object))[slot];
    return reinterpret_cast<R(__fastcall*)(void*, void*, Args...)>(fn)(object, nullptr, args...);
}

// The wide preview's six rectangles cleared to the far plane (800 x 600 layout), one table per edge.
constexpr LONG kWideClearLeft[6] = /*007402F8*/ {290, 272, 272, 510, 510, 510};
constexpr LONG kWideClearTop[6] = /*00740310*/ {100, 190, 340, 190, 290, 390};
constexpr LONG kWideClearRight[6] = /*00740328*/ {510, 290, 290, 528, 528, 528};
constexpr LONG kWideClearBottom[6] = /*00740340*/ {500, 260, 410, 210, 310, 410};

// The game's vector helpers as the original calls them (00401E00 dot, unrounded; 00448A10 /
// 00410770 scale; 004489D0 cross; 0040CDC0 sum; results stored as float).
double DotCall(const Vector3& a, const Vector3& b) { return DotUnrounded<Sum3::ZYX, double>(a, b); }
Vector3 Scale(const Vector3& v, float s) { return {static_cast<float>(D(s) * D(v.x)), static_cast<float>(D(s) * D(v.y)), static_cast<float>(D(s) * D(v.z))}; }
Vector3 CrossStored(const Vector3& a, const Vector3& b) {
    float z = static_cast<float>(D(a.x) * D(b.y) - D(a.y) * D(b.x));
    float y = static_cast<float>(D(a.z) * D(b.x) - D(b.z) * D(a.x));
    float x = static_cast<float>(D(a.y) * D(b.z) - D(a.z) * D(b.y));
    return {x, y, z};
}
Vector3 Sum(const Vector3& a, const Vector3& b) {
    float z = static_cast<float>(D(a.z) + D(b.z));
    float y = static_cast<float>(D(a.y) + D(b.y));
    float x = static_cast<float>(D(a.x) + D(b.x));
    return {x, y, z};
}

// The products the original inlines (q * r), with the two orders of the w sum it uses.
Quaternion Product(const Quaternion& q, const Quaternion& r, bool second) {
    Quaternion out;
    out.z = static_cast<float>(((D(q.x) * D(r.y) - D(q.y) * D(r.x)) + D(q.w) * D(r.z)) + D(q.z) * D(r.w));
    out.y = static_cast<float>(((D(q.z) * D(r.x) - D(q.x) * D(r.z)) + D(q.y) * D(r.w)) + D(q.w) * D(r.y));
    out.x = static_cast<float>(((D(q.y) * D(r.z) - D(q.z) * D(r.y)) + D(q.x) * D(r.w)) + D(q.w) * D(r.x));
    if (!second)
        out.w = static_cast<float>(D(q.w) * D(r.w) - DotUnrounded<Sum3::ZYX, double>(q.x, q.y, q.z, r.x, r.y, r.z));
    else
        out.w = static_cast<float>(D(q.w) * D(r.w) - DotUnrounded<Sum3::XYZ, double>(q.x, q.y, q.z, r.x, r.y, r.z));
    return out;
}

constexpr unsigned long kStageState = 0x12;

// D3DCLEAR_ZBUFFER, with D3DCLEAR_STENCIL when the device has a stencil buffer.
unsigned long ClearFlags(const UiRenderer* renderer) {
    if ((renderer->caps_ & 0x4000) != 0 && renderer->stencil_ != 0) return 6;
    return 2;
}

void ClearDepth(UiRenderer* renderer, unsigned long count, RECT* rects, float z) {
    unsigned long flags = ClearFlags(renderer);
    gameapi::EnterCriticalSection(TheDeviceLock());
    renderer->result_ = renderer->device_->Clear(count, rects, flags, 0, z, 0);
    gameapi::LeaveCriticalSection(TheDeviceLock());
    render::Rect r[6];
    int n = count < 6 ? static_cast<int>(count) : 6;
    for (int i = 0; i < n; ++i) r[i] = render::Rect{rects[i].left, rects[i].top, rects[i].right, rects[i].bottom};
    render::ClearDepth(z, r, n);
}

} // namespace

void CUiWindow::DrawHeroPreview(CSceneRenderer* pass, CUnit* hero, float turn, float depth, int wide) {
    auto* model = reinterpret_cast<std::uint8_t*>(hero);
    if (!model) return;
    auto deviceOf = [this] { return interface_->renderer_; };

    // The whole depth buffer (stencil too), then the preview's rectangles to the far plane.
    {
        UiRenderer* renderer = deviceOf();
        unsigned long flags = ClearFlags(renderer);
        gameapi::EnterCriticalSection(TheDeviceLock());
        renderer->result_ = renderer->device_->Clear(0, nullptr, flags, 0, 0.0f, 0);
        gameapi::LeaveCriticalSection(TheDeviceLock());
        render::ClearDepth(0.0f, nullptr, 0);
    }
    RECT rects[6];
    if (static_cast<std::uint8_t>(wide) != 0) {
        for (int i = 0; i < 6; ++i) {
            RECT r{kWideClearLeft[i], kWideClearTop[i], kWideClearRight[i], kWideClearBottom[i]};
            interface_->CallScaleRectToScreen(&r);
            rects[i] = r;
        }
        ClearDepth(deviceOf(), 6, rects, 1.0f);
    } else {
        RECT r;
        if (D(depth) > D(400.0f /*00740160*/))
            r = {410, 100, 700, 500};
        else
            r = {200, 100, 600, 500};
        interface_->CallScaleRectToScreen(&r);
        rects[0] = r;
        ClearDepth(deviceOf(), 1, rects, 1.0f);
    }

    // Kept to put back afterwards: the position, the orientation and the pass's colour.
    std::uint8_t savedPosition[12], savedOrientation[16];
    std::memcpy(savedPosition, model + 0x1C, 12);
    std::memcpy(savedOrientation, model + 0x34, 16);
    CSceneRenderer* renderer = pass;
    std::uint32_t savedColour = renderer->colour_;
    // White: three FISTPs of 255.0 packed as 0xFF rr gg bb.
    renderer->SetColour(0xFFFFFFFFu);
    Set(model, 8, Get<std::uint32_t>(model, 8) | 0x800);
    {
        std::uint32_t now = gameapi::timeGetTime();
        float time = static_cast<float>(x87::FromUnsigned(now) * D(0.0009765625f /*0073BB24*/) * D(15.0f /*0073C518*/));
        VirtualCall(model, 25, time);
    }

    // The turn about y, applied to the origin (the game's rotation of a vector by a quaternion:
    // 2 (v.u) v + (w^2 - v.v) u + 2 w (v x u), with u = 0).
    Vector3 up = {0.0f, 1.0f, 0.0f};
    Quaternion q;
    q.SetAxisAngle(up, turn);
    Vector3 v = {q.x, q.y, q.z};
    Vector3 origin = {0.0f, 0.0f, 0.0f};
    float ww = static_cast<float>(D(q.w) * D(q.w));
    Vector3 along = Scale(v, static_cast<float>(DotCall(v, origin) + DotCall(v, origin)));
    Vector3 across = Scale(CrossStored(v, origin), static_cast<float>(D(q.w) + D(q.w)));
    Vector3 rotated = Sum(Sum(Scale(origin, static_cast<float>(D(ww) - DotCall(v, v))), across), along);
    // Placed by the depth: x from depth * c - 1, y and z fixed offsets.
    double dx = ((D(depth) * D(0.0025f /*0074015C*/) - D(1.0f /*0073B84C*/)) * D(0.48157462f /*007402C4*/)) *
                D(6.0f /*0073BA98*/);
    double dy = static_cast<float>(D(0.48157462f /*007402C4*/) * D(2.1f /*007403D8*/));   // rounded to 24 bits as the FPU does (the compiler folds it at 53)
    float px = static_cast<float>(D(rotated.x) + dx);
    float py = static_cast<float>(D(rotated.y) + dy);
    float pz = static_cast<float>(D(rotated.z) + D(6.0f /*0073BA98*/));
    Set(model, 0x1C, px);
    Set(model, 0x20, py);
    Set(model, 0x24, pz);
    // The orientation: the turn, then a fixed tilt about x, and in the wide layout a turn about -z.
    Vector3 xAxis = {1.0f, 0.0f, 0.0f};
    Quaternion tilt;
    q = Product(q, *tilt.SetAxisAngle(xAxis, GameFloat(0x007C7D24)), false);
    if (D(depth) > D(400.0f /*00740160*/)) {
        Vector3 zAxis = {0.0f, 0.0f, -1.0f};
        Quaternion side;
        q = Product(q,
                    *side.SetAxisAngle(zAxis,
                                                    static_cast<float>(D(GameFloat(0x007C7D24)) * D(0.15f /*0073CD18*/))),
                    true);
    }
    std::memcpy(model + 0x34, &q, 16);

    // Drawn brighter (1.2) with texture stage 0's state 0x12 set to 1, then everything put back.
    std::uint8_t* light = Get<std::uint8_t*>(model, 0x94);
    Set(light, 0xF0, 1.2f);
    Set(light, 0xF4, 1.2f);
    Set(light, 0xF8, 1.2f);
    UiRenderer* device = deviceOf();
    std::uint32_t savedStage = 0;
    device->result_ = device->device_->GetTextureStageState(0, kStageState, &savedStage);
    device->result_ = device->device_->SetTextureStageState(0, kStageState, 1);
    render::state::Stage(0, kStageState, 1);
    VirtualCall(model, 22, pass);
    device->result_ = device->device_->SetTextureStageState(0, kStageState, savedStage);
    render::state::Stage(0, kStageState, savedStage);
    light = Get<std::uint8_t*>(model, 0x94);
    Set(light, 0xF0, 1.0f);
    Set(light, 0xF4, 1.0f);
    Set(light, 0xF8, 1.0f);
    std::memcpy(model + 0x1C, savedPosition, 12);
    std::memcpy(model + 0x34, savedOrientation, 16);
    Set(model, 8, Get<std::uint32_t>(model, 8) & ~0x800u);
    renderer->SetColour(savedColour);
}
EI_REPLACE_METHOD(0x005EC300, CUiWindow::DrawHeroPreview);
