// The battle HUD's party portraits, per frame (HudPortraits::Update): each portrait's
// health and mana bars, the red flash after a hit, the face picture (normal, hit, low mana) and
// model (by the unit's build), the face model's easing towards its rest orientation with an
// occasional random nod, shake or turn, and the marker under the selected unit's portrait.
// See docs/notes/ui.md.
#include "ui.h"
#include "ui_float.h"

#include "maths.h"
#include "random.h"
#include "unit.h"

#include "lib/crt.h"
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

#include <concepts>
#include "lib/nan_audit.h"
#include "camera.h"
#include "world.h"
#include "lib/vecmath.h"

namespace {

bool GamePaused() { return g_clientWorld.paused != 0; }

// Bar texture coordinate for a fill fraction: ((f * 64 + 9) / 256 + 1/512), stored.
constexpr float kBarTexels = 64.0f;
constexpr float kBarStart = 9.0f;
constexpr float kTexel = 0.00390625f;
constexpr float kHalfTexel = 0.001953125f;
constexpr std::uint32_t kHealthV0 = 0x3F768000;   // the bars' v coordinates
constexpr std::uint32_t kHealthV1 = 0x3F698000;
constexpr std::uint32_t kManaV0 = 0x3F5C8000;
constexpr std::uint32_t kManaV1 = 0x3F4F8000;
constexpr float kFlashAlpha = 200.0f;
constexpr float kHitPictureTime = 2.0f;       // face mode 2
constexpr float kMode1PictureTime = 4.0f;     // face mode 1
constexpr float kLowMana = 0.25f;
constexpr float kThin = 0.2f;
constexpr float kFat = 0.8f;
// The scales the original multiplies random words with (about 2^-32, -0.3 * 2^-32 and 2^-31).
constexpr float kRandomScale32 = 2.3283064e-10f;
constexpr float kRandomDelayScale = -6.9849196e-11f;
constexpr float kRandomScale31 = 4.656613e-10f;
constexpr float kWobbleRate = 0.33333334f;    // wobbles per second
constexpr float kTwelfth = 0.083333336f;
constexpr float kWobbleSpan = 0.2f;
constexpr float kWobbleFrequency = 5.0f;
constexpr float kWobbleAmplitude = 0.1f;
constexpr float kMarkerRise = 60.0f;
constexpr float kMarkerDepth = 10.0f;

// A bar: its right edge (x of vertices 2 and 3) moves with the fraction, its texture follows.
void SetBar(EngineObject* object, float fraction, std::uint32_t v0, std::uint32_t v1) {
    UiMesh* bar = AsUiMesh(object);
    float u = ((fraction * kBarTexels + kBarStart) * kTexel) + kHalfTexel;
    float right = fraction - 0.5f;
    bar->vertices[0].x()[2] = right;
    bar->vertices[0].x()[3] = right;
    bar->texCoords[2].u = u;
    bar->texCoords[2].v = FloatFromBits(v0);
    bar->texCoords[3].u = u;
    bar->texCoords[3].v = FloatFromBits(v1);
}

// The original inlines three quaternion products, each with its own operand order; every
// component is rounded once. The wobble target: q1 (about y) times q2 (about x).
Quaternion TargetProduct(const Quaternion& q1, const Quaternion& q2) {
    Quaternion r;
    r.w = q1.w * q2.w - DotUnrounded<Sum3::ZYX>(q1.x, q1.y, q1.z, q2.x, q2.y, q2.z);
    r.x = ((q2.y * q1.z - q1.y * q2.z) + q1.w * q2.x) + q1.x * q2.w;
    r.y = ((q1.x * q2.z - q2.x * q1.z) + q1.w * q2.y) + q1.y * q2.w;
    r.z = ((q1.y * q2.x - q1.x * q2.y) + q2.w * q1.z) + q2.z * q1.w;
    return r;
}

// The face's rest orientation from the slot's two parts (a, b).
Quaternion RestProduct(const Quaternion& a, const Quaternion& b) {
    Quaternion r;
    r.w = a.w * b.w - DotUnrounded<Sum3::ZYX>(a.x, a.y, a.z, b.x, b.y, b.z);
    r.x = ((a.y * b.z - a.z * b.y) + a.x * b.w) + b.x * a.w;
    r.y = ((a.z * b.x - a.x * b.z) + a.y * b.w) + a.w * b.y;
    r.z = ((a.x * b.y - b.x * a.y) + a.z * b.w) + b.z * a.w;
    return r;
}

// The eased face orientation f with the wobble w applied.
Quaternion WobbleProduct(const Quaternion& f, const Quaternion& w) {
    Quaternion r;
    r.w = f.w * w.w - DotUnrounded<Sum3::ZYX>(f.x, f.y, f.z, w.x, w.y, w.z);
    r.x = ((w.z * f.y - f.z * w.y) + w.x * f.w) + f.x * w.w;
    r.y = ((f.z * w.x - w.z * f.x) + w.y * f.w) + f.y * w.w;
    r.z = ((w.y * f.x - w.x * f.y) + w.z * f.w) + f.z * w.w;
    return r;
}

float RandomTilt(std::uint32_t r) {
    return (r * kRandomScale31 - 1.0f) * kHudQuarterTurn * kTwelfth;
}

Quaternion AxisAngle(float x, float y, float z, float angle) {
    Vector3 axis = {x, y, z};
    Quaternion q;
    q.SetAxisAngle(axis, angle);
    return q;
}

} // namespace

void HudPortraits::Wobble(int index, int kind) {
    slots[index].wobbleTime = g_clientRandom.Next() * kRandomDelayScale;
    slots[index].wobbleKind = kind;
}

void HudPortraits::Update() {
    float dt = GamePaused() ? 0.0f : UiContainer::FrameTime();
    SceneMesh(0)->visible = false;    // the selection marker

    for (int i = 0; i < std::ssize(owner->party); ++i) {
        if (i >= count) continue;
        // Objects 5i+1 .. 5i+5, read afresh each time (replacing the face model may move the array).
        auto object = [this, i](int k) { return sceneObjects[i * 5 + k]; };
        HudPortraitSlot& slot = slots[i];
        CUnit* unit = owner->party[i];

        // Health bar (object 5i+2) and mana bar (5i+3).
        int hitPoints = unit->packedVitals.hitPoints < 0 ? 0 : unit->packedVitals.hitPoints;
        float health = unit->packedVitals.maxHitPoints > 0
                            ? static_cast<float>(hitPoints) / static_cast<float>(unit->packedVitals.maxHitPoints)
                            : 0.0f;
        SetBar(object(2), health, kHealthV0, kHealthV1);
        std::uint16_t maxMana = unit->packedVitals.maxMana;
        float mana = 0.0f;
        if (maxMana != 0) {
            float current = static_cast<float>(unit->packedVitals.mana);   // exact (16 bits)
            float positive = 0.0f < current ? current : 0.0f;
            mana = positive / static_cast<int>(maxMana);
        }
        SetBar(object(3), mana, kManaV0, kManaV1);

        // A drop in hit points flashes the portrait (object 5i+5) and shows the hit picture.
        if (slot.lastHitPoints > hitPoints) {
            slot.flash = 1.0f;
            slot.lastHitPoints = hitPoints;
            slot.faceTimer = 0.0f;
            slot.faceMode = 2;
        } else if (slot.flash > 0.0f) {
            slot.flash = slot.flash - dt;
        }
        slot.flash = slot.flash >= 0.0f ? slot.flash : 0.0f;
        EngineObject* flash = object(5);
        if (slot.flash > 0.0f) {
            AsUiMesh(flash)->visible = true;
            int alpha = static_cast<int>(slot.flash * kFlashAlpha);
            std::uint32_t colour = static_cast<std::uint32_t>(alpha) << 24 | 0xFF0000u;
            AsUiMesh(flash)->flatColour = colour;
        } else {
            AsUiMesh(flash)->visible = false;
        }

        // The face picture: a timed one (hit, mode 1) while it lasts, else low mana or normal.
        int state = 0;
        if (slot.faceMode != 0) {
            slot.faceTimer = dt + slot.faceTimer;
            if (slot.faceMode == 1) {
                if (slot.faceTimer > kMode1PictureTime) slot.faceMode = 0;
            } else if (slot.faceMode == 2) {
                if (slot.faceTimer > kHitPictureTime) slot.faceMode = 0;
            }
            state = slot.faceMode;
        }
        if (state == 0) {
            std::uint16_t maximum = unit->packedVitals.maxMana;
            if (maximum == 0 ||
                static_cast<float>(static_cast<int>(unit->packedVitals.mana)) / static_cast<float>(static_cast<int>(maximum)) <
                    kLowMana)   // integers: never NaN
                state = 3;
        }
        if (state != slot.faceState) {
            slot.faceState = state;
            ShowFace(i, state);
        }

        // The face model follows the unit's build (its scale's y): thin, medium or fat.
        float build = unit->scale.y;
        EI_NAN_AUDIT(slot.build, build);
        if (slot.build != build) {
            slot.build = build;
            std::string name("me");
            if (build < kThin)
                name = "th";
            else if (build > kFat)
                name = "fa";
            {
                const std::string& prefix = slot.modelPrefix;
                const std::string& suffix = slot.modelSuffix;
                std::string withPrefix = prefix + name;
                std::string full = withPrefix + suffix;
                name = full;
            }
            ReplaceSceneObject(object(1), name);
        }

        // Now and then (a third of the time per second) a wobble starts after a random delay:
        // a turn towards a random target.
        Quaternion wobble = {1.0f, 0.0f, 0.0f, 0.0f};
        if (slot.wobbleKind == 0 && dt > 0.0f) {
            std::uint32_t roll = g_clientRandom.Next();
            if (dt * kWobbleRate > roll * kRandomScale32) {
                slot.wobbleTime = g_clientRandom.Next() * kRandomDelayScale;
                slot.wobbleKind = 3;
                std::uint32_t r1 = g_clientRandom.Next();
                std::uint32_t r2 = g_clientRandom.Next();
                Quaternion q1 = AxisAngle(0.0f, 1.0f, 0.0f, RandomTilt(r1));
                Quaternion q2 = AxisAngle(1.0f, 0.0f, 0.0f, RandomTilt(r2));
                slot.wobbleTarget = TargetProduct(q1, q2);
                slot.wobbleSpeed = g_clientRandom.Next() * kRandomScale31 + 1.0f;
            }
        }
        if (slot.wobbleKind != 0) {
            slot.wobbleTime = dt + slot.wobbleTime;
            if (slot.wobbleTime >= 0.0f) {
                float t = slot.wobbleTime;
                switch (slot.wobbleKind) {
                case 1:   // nod
                    if ((kPi * kWobbleSpan) > t)
                        wobble = AxisAngle(1.0f, 0.0f, 0.0f,
                                           kHudQuarterTurn * std::sin(t * kWobbleFrequency) * kWobbleAmplitude);
                    else
                        slot.wobbleKind = 0;
                    break;
                case 2:   // shake
                    if ((kHudFullTurn * kWobbleSpan) > t)
                        wobble = AxisAngle(0.0f, 1.0f, 0.0f,
                                           kHudQuarterTurn * std::sin(t * kWobbleFrequency) * kWobbleAmplitude);
                    else
                        slot.wobbleKind = 0;
                    break;
                case 3:   // turn towards the target and back
                    if (kHudFullTurn / slot.wobbleSpeed > t) {
                        float w = 0.5f - std::cos(t * slot.wobbleSpeed) * 0.5f;
                        wobble.Slerp(w, kHudRestRotation, slot.wobbleTarget);
                    } else {
                        slot.wobbleKind = 0;
                    }
                    break;
                default:
                    break;
                }
            }
        }

        // The face eases towards its rest orientation (fully when the frame is long).
        Quaternion rest = RestProduct(slot.restA, slot.restB);
        Quaternion previous = slot.current;
        Quaternion face;
        EI_NAN_AUDIT(dt);
        if (dt < 0.5f)
            face.Slerp(dt + dt, previous, rest);
        else
            face = rest;
        slot.current = face;
        if (slot.wobbleKind != 0) face = WobbleProduct(face, wobble);
        AsUiMesh(object(1))->localRotation = face;

        // The selection marker (object 0) sits above the selected unit's portrait (object 5i+4).
        if (g_camera.targetId == unit->id) {
            SceneMesh(0)->visible = true;
            float x, y, depth;
            AsUiMesh(object(4))->GetPlacement(&x, &y, &depth);
            SceneMesh(0)->Place(x, y - kMarkerRise, kMarkerDepth);
        }
    }
}
