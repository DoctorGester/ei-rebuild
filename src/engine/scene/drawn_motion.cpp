// The drawn motion of map objects between client ticks (drawn_motion.h; docs/notes/renderer.md section 14).
#include "engine/scene/drawn_motion.h"

#include "engine/models/figure_instance.h"
#include "engine/scene/light.h"
#include "platform/platform.h"

#include <cstring>

namespace {

// A tick's step longer than this (map units) placed the object anew: it is shown at once (the camera's cut).
constexpr float kCut = 8.0f;
// How long a path change's jump takes to fade (milliseconds of real time).
constexpr std::uint32_t kCorrectionFade = 150;

std::uint32_t g_ticks = 0;      // client ticks run
std::uint32_t g_passes = 1;     // passes that set the progress (LightStep::carriedPass of none: 0)
float g_spread = 0.0f;          // the part of the last tick's step the frames spread, taken once after the tick
bool g_spreadTaken = true;
float g_back = 0.0f;            // drawn::Back

// The same pose to the bit.
bool Same(const drawn::Pose& a, const drawn::Pose& b) { return std::memcmp(&a, &b, sizeof(drawn::Pose)) == 0; }

} // namespace

void drawn::TickRan() {
    ++g_ticks;
    g_spreadTaken = false;
}

float drawn::PassSpread(float passTicks) {
    const float spread = 2.0f - passTicks;
    return spread > 0.0f ? (spread < 1.0f ? spread : 1.0f) : 0.0f;
}

void drawn::SetProgress(float reached, float passTicks) {
    ++g_passes;
    if (!g_spreadTaken) {
        g_spread = PassSpread(passTicks);
        g_spreadTaken = true;
    }
    reached = reached > 0.0f ? (reached < 1.0f ? reached : 1.0f) : 0.0f;
    g_back = (1.0f - reached) * g_spread;
}

float drawn::Back() { return g_back; }

drawn::Pose drawn::OwnPose(const FigureInstance* object) { return Pose{object->localPosition, object->localRotation}; }

void drawn::Observe(FigureInstance* object) {
    ObjectStep& step = object->drawnStep;
    if (step.seenTick == g_ticks) return;
    const bool oneTick = step.seenTick != 0 && step.seenTick + 1 == g_ticks;
    step.from = step.to;
    step.to = OwnPose(object);
    step.moving = oneTick && !step.placedByFrame && !Same(step.from, step.to) &&
                  DistanceSquared(step.from.position, step.to.position) <= kCut * kCut;
    step.placedByFrame = false;
    step.seenTick = g_ticks;
}

void drawn::NotePlaced(FigureInstance* object, const Pose& before) {
    ObjectStep& step = object->drawnStep;
    if (!Same(OwnPose(object), before)) step.placedByFrame = true;
    if (!step.pathChanged) return;
    step.pathChanged = false;
    const Vector3 jump = step.shown - object->localPosition;
    if (!step.hasShown || !(g_spread > 0.0f) || !(LengthSquared(jump) <= kCut * kCut)) {
        step.correctionShare = 0.0f;
        return;
    }
    step.correction = jump;
    step.correctionShare = g_spread;
    step.correctionStart = platform::FrameClock();
}

void drawn::NotePathChanged(FigureInstance* object) { object->drawnStep.pathChanged = true; }

drawn::Pose drawn::PoseOf(const FigureInstance* object) {
    const ObjectStep& step = object->drawnStep;
    const Pose own = OwnPose(object);
    // An interface figure (the HUD unit panel's unit, a preview) stands where the interface put it in its own view:
    // the world's tick step and path change jump would move it there.
    if (object->flags & FigureFlag::NoEdgeFade) return own;
    Pose out = own;
    if (step.moving && step.seenTick == g_ticks && g_back > 0.0f && Same(own, step.to)) {
        out.position = own.position - (step.to.position - step.from.position) * g_back;
        out.rotation.Slerp(g_back, step.to.rotation, step.from.rotation);
    }
    if (step.correctionShare > 0.0f) {
        const std::uint32_t elapsed = platform::FrameClock() - step.correctionStart;
        if (elapsed < kCorrectionFade) {
            const float t = static_cast<float>(elapsed) / static_cast<float>(kCorrectionFade);
            const float left = (1.0f - t * t * (3.0f - 2.0f * t)) * step.correctionShare;
            out.position = out.position + step.correction * left;
        }
    }
    return out;
}

drawn::Pose drawn::Show(FigureInstance* object) {
    const Pose shown = PoseOf(object);
    ObjectStep& step = object->drawnStep;
    step.shown = shown.position;
    step.hasShown = true;
    if (step.correctionShare > 0.0f && platform::FrameClock() - step.correctionStart >= kCorrectionFade)
        step.correctionShare = 0.0f;
    return shown;
}

void drawn::Carry(Light* light, const Vector3& at) {
    light->drawnStep.carried = at;
    light->drawnStep.carriedPass = g_passes;
}

Vector3 drawn::LightPosition(Light* light) {
    LightStep& step = light->drawnStep;
    if (step.carriedPass == g_passes) return step.carried;
    const Vector3 own = light->localPosition;
    if (step.seenTick != g_ticks) {
        const bool oneTick = step.seenTick != 0 && step.seenTick + 1 == g_ticks;
        step.from = step.to;
        step.to = own;
        step.moving = oneTick && std::memcmp(&step.from, &step.to, sizeof(Vector3)) != 0 &&
                      DistanceSquared(step.from, step.to) <= kCut * kCut;
        step.seenTick = g_ticks;
    }
    if (step.moving && g_back > 0.0f && std::memcmp(&own, &step.to, sizeof(Vector3)) == 0)
        return own - (step.to - step.from) * g_back;
    return own;
}
