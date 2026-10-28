// Per-frame updates of the battle HUD panels: the clock (CHudClock::Update) turns its
// face with the time of day, moves the hand and the speed dial and makes the pause button blink
// while the game is paused; the top panel (CHudTopPanel::Update) slides in and out.
// See docs/notes/ui.md.
// Floating point follows the original's x87 code: `double` for values the original keeps on the FPU
// stack, static_cast<float> where it stores to a float.
#include "ui.h"
#include "ui_widescreen.h"

#include "maths.h"

#include "ei/x87.h"
#include "lib/nan_audit.h"

#include <cmath>
#include <concepts>
#include <cstdint>
#include <cstring>
#include "lib/crt.h"
#include "world.h"
#include "game_singletons.h"

namespace {

inline double D(float f) { return f; }

float FloatFromBits(std::uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}
// A float argument would convert to an integer first: callers pass raw bits only.
template <std::floating_point T> float FloatFromBits(T) = delete;

constexpr float kPi = 3.1415927f;
// The constants are read from memory at run time; Rebirth rewrites one of them (the clock's x,
// widescreen::g_layoutWidth, for its widescreen layout).
constexpr float kPausedHand = 0.9166667f;   // hand angle while paused, in pi
constexpr float kFastHand = 0.5833333f;   // at fast speed
constexpr float kNormalHand = 0.75f;   // at normal speed
constexpr float kDialStep = 0.16666667f;   // dial notches, in pi
constexpr float kFaceStep = 0.083333336f;   // one hour of the face, in pi
constexpr float kFaceOffset = 15.0f;   // 15 hours
constexpr float kHandRadius = 73.0f;
constexpr float kDialRadius = 30.0f;
constexpr float kCentreY = 600.0f;
constexpr float kBlinkBase = 1.4f;
constexpr float kPulseAmplitude = 0.3f;
constexpr float kPulseMiddle = 0.7f;
constexpr float kSlideTime = 0.3f;   // 0.3 seconds for a full slide
bool GamePaused() { return g_clientWorld->paused_ != 0; }

constexpr std::uint32_t kHandDepth = 0x41000000;   // 8.0f
constexpr std::uint32_t kSpriteDepth = 0x4101B287;   // 8.106: the dial marks, the top panel sling

// The time of day in hours: (client ticks * scale + offset) mod 24 (_CIfmod), stored.
float TimeOfDay() {
    const CWorldClient* world = &g_clientWorld.Get();
    return static_cast<float>(std::fmod(static_cast<double>(world->tick_) * world->secondsPerTick_ + world->timeBase_, 24.0));
}

CUiMesh* Sprite(CEngineObject* object) { return AsUiMesh(object); }

void SetTint(CEngineObject* object, float value) {
    float* tint = Sprite(object)->tint_;
    std::memcpy(&tint[0], &value, 4);
    std::memcpy(&tint[1], &value, 4);
    std::memcpy(&tint[2], &value, 4);
}

// Turns a sprite about `axis` (z or -z) by `angle`.
void Turn(CEngineObject* object, float axisZ, float angle) {
    Vector3 axis = {0.0f, 0.0f, axisZ};
    Quaternion q;
    q.SetAxisAngle(axis, angle);
    std::memcpy(&Sprite(object)->localRotation_, &q, sizeof(q));
}

std::uint32_t Bits(float f) {
    std::uint32_t u;
    std::memcpy(&u, &f, sizeof(u));
    return u;
}

constexpr float kSlingX = 400.0f;               // the sling button's place: x
constexpr float kSlingInY = -52.0f;             // y while the panel slides out (turned over)
constexpr float kSlingOutY = 83.0f;             // y otherwise

} // namespace

void CUiMesh::Place(float a0, float a1, float a2) { PlaceBody(a0, a1, a2); }

void CHudClock::Update() {
    interface_->fastGame_ = fast_;
    float hours = TimeOfDay();
    CEngineObject** objects = sceneObjects_.data();
    CEngineObject* face = objects[0];

    // The blink phase runs from 0 to 1 once a second.
    double blink = CUiContainer::FrameTime() + D(blink_);
    blink_ = static_cast<float>(blink);
    if (blink >= 1.0f) {
        double next;
        do {
            next = D(blink_) - 1.0f;
            blink_ = static_cast<float>(next);
        } while (next >= 1.0f);
    }
    float pulse = static_cast<float>(x87::fcos(D(kHudFullTurn) * D(blink_)) * kPulseAmplitude + kPulseMiddle);

    float handAngle;
    if (GamePaused()) {
        SetTint(face, static_cast<float>(kBlinkBase - D(pulse)));
        handAngle = static_cast<float>(D(kPi) * kPausedHand);
        Sprite(objects[1])->visible_ = blink_ > 0.5f ? 1 : 0;
    } else {
        SetTint(face, 1.0f);
        double step;
        if (fast_ != 0) {
            handAngle = static_cast<float>(D(kPi) * kFastHand);
            step = CUiContainer::FrameTime();
        } else {
            handAngle = static_cast<float>(D(kPi) * kNormalHand);
            step = CUiContainer::FrameTime() * 0.5f;
        }
        dial_ = static_cast<float>(step + D(dial_));
        Sprite(objects[1])->visible_ = 1;
    }

    // The dial turns by whole notches' worth and wraps.
    float notch = static_cast<float>(D(kPi) * kDialStep);
    if (dial_ > notch) {
        double next;
        do {
            next = D(dial_) - D(notch);
            dial_ = static_cast<float>(next);
            notch = static_cast<float>(D(kPi) * kDialStep);
        } while (next > notch);
    }

    // The hand (object 1) about -z, placed on its circle.
    Turn(objects[1], -1.0f, handAngle);
    double handSin = x87::fsin(handAngle);
    float handY = static_cast<float>(kCentreY - handSin * kHandRadius);
    float handX = static_cast<float>(x87::fcos(handAngle) * kHandRadius + widescreen::g_layoutWidth);
    Sprite(objects[1])->Place(handX, handY, FloatFromBits(kHandDepth));

    // The face turns with the time of day.
    Turn(face, 1.0f, static_cast<float>((D(hours) + kFaceOffset) * D(kPi) * kFaceStep));

    // The four dial marks (objects 2-5), a notch apart.
    float angle = static_cast<float>(D(dial_) - D(static_cast<float>(D(kPi) * kFaceStep)));
    for (int k = 2; k < 6; ++k) {
        CEngineObject* mark = objects[k];
        Turn(mark, 1.0f, angle);
        double c = x87::fcos(angle);
        float y = static_cast<float>(kCentreY - c * kDialRadius);
        float x = static_cast<float>(x87::fsin(angle) * kDialRadius + widescreen::g_layoutWidth);
        Sprite(mark)->Place(x, y, FloatFromBits(kSpriteDepth));
        angle = static_cast<float>(D(angle) - D(static_cast<float>(D(kPi) * kDialStep)));
    }

    SetTint(objects[6], highlight_ ? pulse : 1.0f);
}

// --- Top panel ---------------------------------------------------------------------------------

void CHudTopPanel::Update() {
    if (sliding_) {
        double slide = CUiContainer::FrameTime() * D(slideDirection_) / kSlideTime + D(slide_);
        slide_ = static_cast<float>(slide);
        if (slide > 1.0f) slide_ = 1.0f;
        slide_ = slide_ >= 0.0f ? slide_ : 0.0f;   // NaN too
        // Ease in and out: 2s^2 for the first half, 1 - 2(1-s)^2 for the second.
        double eased;
        if (slide_ < 0.5f) {
            double s = D(slide_);
            eased = s * s;
            eased = eased + eased;
        } else {
            double r = 1.0f - D(slide_);
            double r2 = r * r;
            eased = 1.0f - (r2 + r2);
        }
        offset_ = static_cast<int>(libcrt::Ftol(static_cast<double>(travel_) * eased));
        EI_NAN_AUDIT(slide_);
        if (Bits(slide_) == 0x3F800000 || slide_ == 0.0f) sliding_ = 0;
        Layout();
        CEngineObject* sling = sceneObjects_[2];
        float y;
        if (Bits(slideDirection_) == 0xBF800000) {   // -1: sliding out
            Turn(sling, 1.0f, kPi);
            y = kSlingInY;
        } else {
            std::memcpy(&Sprite(sling)->localRotation_, &kHudRestRotation, 16);
            y = kSlingOutY;
        }
        Sprite(sling)->Place(kSlingX, y, FloatFromBits(kSpriteDepth));
    }
    interface_->hudPanelsIn_[1] = slideDirection_ > 0.0f ? 1 : 0;   // the panel is (coming) in
    interface_->hudTopPage_ = page_;
}
