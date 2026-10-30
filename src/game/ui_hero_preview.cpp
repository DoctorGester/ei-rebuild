// The hero preview of the camp and the multiplayer hero screen.
// See ui.h (UiWindow::DrawHeroPreview) and docs/notes/ui.md ("Camp, global map, hero screens").
#include "ui.h"
#include "texture.h"
#include "map_object_shadow.h"
#include "model.h"
#include "unit.h"

#include "lib/vecmath.h"
#include "render/render_state.h"
#include "platform/clock.h"
#include "render/render.h"
#include "game_singletons.h"

#include <cstdint>
#include <cstring>

namespace {

constexpr float kTilt = /*007C7D24*/ 1.5707964f;   // a static initialiser: pi * 0.5
// The wide preview's six rectangles cleared to the far plane (800 x 600 layout), one table per edge.
constexpr std::int32_t kWideClearLeft[6] = /*007402F8*/ {290, 272, 272, 510, 510, 510};
constexpr std::int32_t kWideClearTop[6] = /*00740310*/ {100, 190, 340, 190, 290, 390};
constexpr std::int32_t kWideClearRight[6] = /*00740328*/ {510, 290, 290, 528, 528, 528};
constexpr std::int32_t kWideClearBottom[6] = /*00740340*/ {500, 260, 410, 210, 310, 410};

// The game's vector helpers as the original calls them (dot; scale; cross; sum).
float DotCall(const Vector3& a, const Vector3& b) { return DotZYX(a, b); }
Vector3 Scale(const Vector3& v, float s) { return {s * v.x, s * v.y, s * v.z}; }
Vector3 CrossStored(const Vector3& a, const Vector3& b) {
    float z = a.x * b.y - a.y * b.x;
    float y = a.z * b.x - b.z * a.x;
    float x = a.y * b.z - a.z * b.y;
    return {x, y, z};
}
Vector3 Sum(const Vector3& a, const Vector3& b) {
    float z = a.z + b.z;
    float y = a.y + b.y;
    float x = a.x + b.x;
    return {x, y, z};
}

// The products the original inlines (q * r), with the two orders of the w sum it uses.
Quaternion Product(const Quaternion& q, const Quaternion& r, bool second) {
    Quaternion out;
    out.z = ((q.x * r.y - q.y * r.x) + q.w * r.z) + q.z * r.w;
    out.y = ((q.z * r.x - q.x * r.z) + q.y * r.w) + q.w * r.y;
    out.x = ((q.y * r.z - q.z * r.y) + q.x * r.w) + q.w * r.x;
    if (!second)
        out.w = q.w * r.w - DotUnrounded<Sum3::ZYX>(q.x, q.y, q.z, r.x, r.y, r.z);
    else
        out.w = q.w * r.w - DotUnrounded<Sum3::XYZ>(q.x, q.y, q.z, r.x, r.y, r.z);
    return out;
}

} // namespace

void UiWindow::DrawHeroPreview(SceneRenderer* pass, CUnit* hero, float turn, float depth, bool wide) {
    CObject3D* model = hero;
    if (!model) return;

    // The whole depth buffer to the near plane, then the preview's rectangles to the far plane.
    ClearUiDepth(0.0f);
    Rect rects[6];
    if (wide) {
        for (int i = 0; i < 6; ++i) {
            Rect r{kWideClearLeft[i], kWideClearTop[i], kWideClearRight[i], kWideClearBottom[i]};
            ui->ScaleRectToScreenEntry(&r);
            rects[i] = r;
        }
        ClearUiDepth(1.0f, rects, 6);
    } else {
        Rect r;
        if (depth > 400.0f)
            r = {410, 100, 700, 500};
        else
            r = {200, 100, 600, 500};
        ui->ScaleRectToScreenEntry(&r);
        rects[0] = r;
        ClearUiDepth(1.0f, rects, 1);
    }

    // Kept to put back afterwards: the position, the orientation and the pass's colour.
    Vector3 savedPosition;
    Quaternion savedOrientation;
    savedPosition = model->localPosition;
    savedOrientation = model->localRotation;
    SceneRenderer* renderer = pass;
    std::uint32_t savedColour = renderer->colour;
    // White: 255.0 converted three times, packed as 0xFF rr gg bb.
    renderer->SetColour(0xFFFFFFFFu);
    model->flags |= MapObjectFlag::NoEdgeFade;
    {
        // In animation frames: milliseconds / 1024 * 15 (a double: the milliseconds since the system started).
        model->AdvanceTime(static_cast<float>(platform::Milliseconds() * (15.0 / 1024.0)));
    }

    // The turn about y, applied to the origin (the game's rotation of a vector by a quaternion:
    // 2 (v.u) v + (w^2 - v.v) u + 2 w (v x u), with u = 0).
    Vector3 up = {0.0f, 1.0f, 0.0f};
    Quaternion q;
    q.SetAxisAngle(up, turn);
    Vector3 v = {q.x, q.y, q.z};
    Vector3 origin = {0.0f, 0.0f, 0.0f};
    float ww = q.w * q.w;
    Vector3 along = Scale(v, DotCall(v, origin) + DotCall(v, origin));
    Vector3 across = Scale(CrossStored(v, origin), q.w + q.w);
    Vector3 rotated = Sum(Sum(Scale(origin, ww - DotCall(v, v)), across), along);
    // Placed by the depth: x from depth * c - 1, y and z fixed offsets.
    float dx = ((depth * 0.0025f - 1.0f) * 0.48157462f) *
                6.0f;
    float dy = 0.48157462f * 2.1f;
    float px = rotated.x + dx;
    float py = rotated.y + dy;
    float pz = rotated.z + 6.0f;
    model->localPosition.x = px;
    model->localPosition.y = py;
    model->localPosition.z = pz;
    // The orientation: the turn, then a fixed tilt about x, and in the wide layout a turn about -z.
    Vector3 xAxis = {1.0f, 0.0f, 0.0f};
    Quaternion tilt;
    q = Product(q, *tilt.SetAxisAngle(xAxis, kTilt), false);
    if (depth > 400.0f) {
        Vector3 zAxis = {0.0f, 0.0f, -1.0f};
        Quaternion side;
        q = Product(q,
                    *side.SetAxisAngle(zAxis,
                                                    kTilt * 0.15f),
                    true);
    }
    model->localRotation = q;

    // Drawn brighter (1.2) with texture stage 0's mip filter off, then everything put back.
    Object3DClientSpecific* drawn = model->ClientSpecific();
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
    model->localPosition = savedPosition;
    model->localRotation = savedOrientation;
    model->flags &= ~MapObjectFlag::NoEdgeFade;
    renderer->SetColour(savedColour);
}
