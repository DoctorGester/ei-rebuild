// Per-frame updates of the battle HUD panels: the clock (HudClock::Update) turns its
// face with the time of day, moves the hand and the speed dial and makes the pause button blink
// while the game is paused; the top panel (HudTopPanel::Update) slides in and out.
// See docs/notes/ui.md.
#include "ui.h"
#include "ui_widescreen.h"

#include "maths.h"

#include "lib/nan_audit.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include "lib/crt.h"
#include "world.h"
#include "game_singletons.h"
#include "lib/vecmath.h"

namespace {


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
bool GamePaused() { return g_clientWorld.paused != 0; }

constexpr float kHandDepth = 8.0f;
constexpr float kSpriteDepth = 8.106086f;   // 8.106: the dial marks, the top panel sling

// The time of day in hours: (client ticks * scale + offset) mod 24 (_CIfmod), stored.
float TimeOfDay() {
    const WorldClient* world = &g_clientWorld;
    return std::fmod(world->tick * world->secondsPerTick + world->timeBase, 24.0f);
}

void SetTint(EngineObject* object, float value) {
    float* tint = AsUiMesh(object)->tint;
    tint[0] = value;
    tint[1] = value;
    tint[2] = value;
}

// Turns a sprite about `axis` (z or -z) by `angle`.
void Turn(EngineObject* object, float axisZ, float angle) {
    Vector3 axis = {0.0f, 0.0f, axisZ};
    Quaternion q;
    q.SetAxisAngle(axis, angle);
    AsUiMesh(object)->localRotation = q;
}

std::uint32_t Bits(float f) {
    std::uint32_t u;
    u = std::bit_cast<std::uint32_t>(f);
    return u;
}

constexpr float kSlingX = 400.0f;               // the sling button's place: x
constexpr float kSlingInY = -52.0f;             // y while the panel slides out (turned over)
constexpr float kSlingOutY = 83.0f;             // y otherwise

} // namespace

void HudClock::Update() {
    ui->fastGame = fast;
    float hours = TimeOfDay();
    EngineObject** scene = sceneObjects.data();
    EngineObject* face = scene[0];

    // The blink phase runs from 0 to 1 once a second.
    float blinkAt = UiContainer::FrameTime() + blink;
    blink = blinkAt;
    if (blinkAt >= 1.0f) {
        float next;
        do {
            next = blink - 1.0f;
            blink = next;
        } while (next >= 1.0f);
    }
    float pulse = std::cos(kHudFullTurn * blink) * kPulseAmplitude + kPulseMiddle;

    float handAngle;
    if (GamePaused()) {
        SetTint(face, kBlinkBase - pulse);
        handAngle = kPi * kPausedHand;
        AsUiMesh(scene[1])->visible = blink > 0.5f;
    } else {
        SetTint(face, 1.0f);
        float step;
        if (fast != 0) {
            handAngle = kPi * kFastHand;
            step = UiContainer::FrameTime();
        } else {
            handAngle = kPi * kNormalHand;
            step = UiContainer::FrameTime() * 0.5f;
        }
        dial = step + dial;
        AsUiMesh(scene[1])->visible = true;
    }

    // The dial turns by whole notches' worth and wraps.
    float notch = kPi * kDialStep;
    if (dial > notch) {
        float next;
        do {
            next = dial - notch;
            dial = next;
            notch = kPi * kDialStep;
        } while (next > notch);
    }

    // The hand (object 1) about -z, placed on its circle.
    Turn(scene[1], -1.0f, handAngle);
    float handSin = std::sin(handAngle);
    float handY = kCentreY - handSin * kHandRadius;
    float handX = std::cos(handAngle) * kHandRadius + widescreen::g_layoutWidth;
    AsUiMesh(scene[1])->Place(handX, handY, kHandDepth);

    // The face turns with the time of day.
    Turn(face, 1.0f, (hours + kFaceOffset) * kPi * kFaceStep);

    // The four dial marks (objects 2-5), a notch apart.
    float angle = dial - (kPi * kFaceStep);
    for (int k = 2; k < 6; ++k) {
        EngineObject* mark = scene[k];
        Turn(mark, 1.0f, angle);
        float c = std::cos(angle);
        float y = kCentreY - c * kDialRadius;
        float x = std::sin(angle) * kDialRadius + widescreen::g_layoutWidth;
        AsUiMesh(mark)->Place(x, y, kSpriteDepth);
        angle = angle - (kPi * kDialStep);
    }

    SetTint(scene[6], highlight ? pulse : 1.0f);
}

// --- Top panel ---------------------------------------------------------------------------------

void HudTopPanel::Update() {
    if (sliding) {
        float slideAt = UiContainer::FrameTime() * slideDirection / kSlideTime + slide;
        slide = slideAt;
        if (slideAt > 1.0f) slide = 1.0f;
        slide = slide >= 0.0f ? slide : 0.0f;      // NaN too
        // Ease in and out: 2s^2 for the first half, 1 - 2(1-s)^2 for the second.
        float eased;
        if (slide < 0.5f) {
            float s = slide;
            eased = s * s;
            eased = eased + eased;
        } else {
            float r = 1.0f - slide;
            float r2 = r * r;
            eased = 1.0f - (r2 + r2);
        }
        offset = static_cast<int>(travel * eased);
        EI_NAN_AUDIT(slide);
        if (Bits(slide) == 0x3F800000 || slide == 0.0f) sliding = false;
        Layout();
        EngineObject* sling = sceneObjects[2];
        float y;
        if (Bits(slideDirection) == 0xBF800000) {    // -1: sliding out
            Turn(sling, 1.0f, kPi);
            y = kSlingInY;
        } else {
            AsUiMesh(sling)->localRotation = kHudRestRotation;
            y = kSlingOutY;
        }
        AsUiMesh(sling)->Place(kSlingX, y, kSpriteDepth);
    }
    ui->hudPanelsIn[1] = slideDirection > 0.0f ? 1 : 0;             // the panel is (coming) in
    ui->hudTopPage = page;
}
