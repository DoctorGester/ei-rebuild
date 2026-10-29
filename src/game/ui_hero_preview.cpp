// The hero preview of the camp and the multiplayer hero screen.
// See ui.h (CUiWindow::DrawHeroPreview) and docs/notes/ui.md ("Camp, global map, hero screens").
// Floating point follows the original's x87 code: values it stores as float are rounded with
// static_cast<float>, values it keeps on the FPU stack stay unrounded (double), products and sums
// keep its operand order, constants are read from the exe's memory.
#include "ui.h"
#include "texture.h"
#include "map_object_shadow.h"
#include "model.h"
#include "unit.h"

#include "lib/vecmath.h"
#include "render/render_state.h"
#include "ei/x87.h"
#include "lib/gameapi.h"
#include "render/render.h"
#include "game_singletons.h"

#include <cstdint>
#include <cstring>

namespace {

inline double D(float f) { return f; }
constexpr float kTilt = /*007C7D24*/ 1.5707964f;   // a static initialiser: pi * 0.5
// The wide preview's six rectangles cleared to the far plane (800 x 600 layout), one table per edge.
constexpr LONG kWideClearLeft[6] = /*007402F8*/ {290, 272, 272, 510, 510, 510};
constexpr LONG kWideClearTop[6] = /*00740310*/ {100, 190, 340, 190, 290, 390};
constexpr LONG kWideClearRight[6] = /*00740328*/ {510, 290, 290, 528, 528, 528};
constexpr LONG kWideClearBottom[6] = /*00740340*/ {500, 260, 410, 210, 310, 410};

// The game's vector helpers as the original calls them (dot, unrounded; scale; cross; sum; results stored as float).
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

} // namespace

void CUiWindow::DrawHeroPreview(CSceneRenderer* pass, CUnit* hero, float turn, float depth, bool wide) {
    CObject3D* model = hero;
    if (!model) return;

    // The whole depth buffer to the near plane, then the preview's rectangles to the far plane.
    ClearUiDepth(0.0f);
    RECT rects[6];
    if (wide) {
        for (int i = 0; i < 6; ++i) {
            RECT r{kWideClearLeft[i], kWideClearTop[i], kWideClearRight[i], kWideClearBottom[i]};
            interface_->ScaleRectToScreenEntry(&r);
            rects[i] = r;
        }
        ClearUiDepth(1.0f, rects, 6);
    } else {
        RECT r;
        if (D(depth) > D(400.0f))
            r = {410, 100, 700, 500};
        else
            r = {200, 100, 600, 500};
        interface_->ScaleRectToScreenEntry(&r);
        rects[0] = r;
        ClearUiDepth(1.0f, rects, 1);
    }

    // Kept to put back afterwards: the position, the orientation and the pass's colour.
    Vector3 savedPosition;
    Quaternion savedOrientation;
    savedPosition = model->localPosition_;
    savedOrientation = model->localRotation_;
    CSceneRenderer* renderer = pass;
    std::uint32_t savedColour = renderer->colour_;
    // White: 255.0 converted three times, packed as 0xFF rr gg bb.
    renderer->SetColour(0xFFFFFFFFu);
    model->flags_ |= MapObjectFlag::NoEdgeFade;
    {
        std::uint32_t now = gameapi::timeGetTime();
        float time = static_cast<float>(x87::FromUnsigned(now) * D(0.0009765625f) * D(15.0f));
        model->AdvanceTime(time);
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
    double dx = ((D(depth) * D(0.0025f) - 1.0) * D(0.48157462f)) *
                D(6.0f);
    double dy = static_cast<float>(D(0.48157462f) * D(2.1f));   // rounded to 24 bits as the FPU does (the compiler folds it at 53)
    float px = static_cast<float>(D(rotated.x) + dx);
    float py = static_cast<float>(D(rotated.y) + dy);
    float pz = static_cast<float>(D(rotated.z) + D(6.0f));
    model->localPosition_.x = px;
    model->localPosition_.y = py;
    model->localPosition_.z = pz;
    // The orientation: the turn, then a fixed tilt about x, and in the wide layout a turn about -z.
    Vector3 xAxis = {1.0f, 0.0f, 0.0f};
    Quaternion tilt;
    q = Product(q, *tilt.SetAxisAngle(xAxis, kTilt), false);
    if (D(depth) > D(400.0f)) {
        Vector3 zAxis = {0.0f, 0.0f, -1.0f};
        Quaternion side;
        q = Product(q,
                    *side.SetAxisAngle(zAxis,
                                                    static_cast<float>(D(kTilt) * D(0.15f))),
                    true);
    }
    model->localRotation_ = q;

    // Drawn brighter (1.2) with texture stage 0's mip filter off, then everything put back.
    CObject3DClientSpecific* drawn = model->ClientSpecific();
    drawn->figureScale.x = 1.2f;
    drawn->figureScale.y = 1.2f;
    drawn->figureScale.z = 1.2f;
    render::TextureStage& stage0 = render::g_renderState.stages[0];
    const render::MipFilter savedMipFilter = stage0.mipFilter;
    stage0.mipFilter = render::MipFilter::None;
    model->DrawFigure(pass);
    stage0.mipFilter = savedMipFilter;
    drawn = model->ClientSpecific();
    drawn->figureScale.x = 1.0f;
    drawn->figureScale.y = 1.0f;
    drawn->figureScale.z = 1.0f;
    model->localPosition_ = savedPosition;
    model->localRotation_ = savedOrientation;
    model->flags_ &= ~MapObjectFlag::NoEdgeFade;
    renderer->SetColour(savedColour);
}
