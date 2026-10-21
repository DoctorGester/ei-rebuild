// The battle HUD's party portraits, per frame (CHudPortraits::Update, 00641080): each portrait's
// health and mana bars, the red flash after a hit, the face picture (normal, hit, low mana) and
// model (by the unit's build), the face model's easing towards its rest orientation with an
// occasional random nod, shake or turn, and the marker under the selected unit's portrait.
// See docs/notes/ui.md.
//
// Floating point follows the original's x87 code: `double` for values the original keeps on the FPU
// stack, static_cast<float> where it stores to a float.
#include "ui.h"

#include "maths.h"
#include "random.h"
#include "unit.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/mfc.h"

#include <concepts>
#include <cstdint>
#include <cstring>
#include "originals_ui.h"
#include "lib/crt.h"

namespace {

inline double D(float f) { return f; }

float FloatFromBits(std::uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}
// A float argument would convert to an integer first: callers pass raw bits only.
template <std::floating_point T> float FloatFromBits(T) = delete;

float GameFloat(std::uintptr_t address) { return *reinterpret_cast<const volatile float*>(address); }
float Pi() { return GameFloat(0x007423F8); }
float TurnPeriod() { return GameFloat(0x007C8140); }      // a global the clock also uses
float WobbleAngle() { return GameFloat(0x007C8144); }     // a global angle
const Quaternion* Identity() { return reinterpret_cast<const Quaternion*>(0x007C8150); }
bool GamePaused() { return *reinterpret_cast<const volatile bool*>(0x007AF624); }   // 00578D50
int SelectedUnitId() { return *reinterpret_cast<const volatile int*>(0x0079B3C4); }
IsaacRandom* Random() { return reinterpret_cast<IsaacRandom*>(0x007A9D40); }

// 00600270 (not rewritten): time since the last frame in seconds, unrounded in ST0.
inline double __fastcall WindowFrameTime(CUiWindow* w) { return WindowFrameTimeOriginal(w); }

// Bar texture coordinate for a fill fraction: ((f * 64 + 9) / 256 + 1/512), stored.
constexpr float kBarTexels = 64.0f;           // 0073D734
constexpr float kBarStart = 9.0f;             // 0073CF10
constexpr float kTexel = 0.00390625f;         // 0073EEB8
constexpr float kHalfTexel = 0.001953125f;    // 0073CC90
constexpr float kHalf = 0.5f;                 // 0073B86C
constexpr std::uint32_t kHealthV0 = 0x3F768000;   // the bars' v coordinates
constexpr std::uint32_t kHealthV1 = 0x3F698000;
constexpr std::uint32_t kManaV0 = 0x3F5C8000;
constexpr std::uint32_t kManaV1 = 0x3F4F8000;
constexpr float kFlashAlpha = 200.0f;         // 0073CF58
constexpr float kHitPictureTime = 2.0f;       // 0073CDB8 face mode 2
constexpr float kMode1PictureTime = 4.0f;     // 0073BAA0 face mode 1
constexpr float kLowMana = 0.25f;             // 0073C3B0
constexpr float kThin = 0.2f;                 // 0073BAD4
constexpr float kFat = 0.8f;                  // 0073CCA8
// Doubles the original multiplies random numbers with (about 2^-32, -0.3 * 2^-32 and 2^-31; not
// exact powers of two), read from the exe.
double GameDouble(std::uintptr_t address) { return *reinterpret_cast<const volatile double*>(address); }
double RandomScale32() { return GameDouble(0x0073C468); }
double RandomDelayScale() { return GameDouble(0x00742760); }
double RandomScale31() { return GameDouble(0x0073C508); }
constexpr float kWobbleRate = 0.33333334f;    // 0073D9A4 wobbles per second
constexpr float kTwelfth = 0.083333336f;      // 00741438
constexpr float kWobbleSpan = 0.2f;           // 0073CCA0
constexpr float kWobbleFrequency = 5.0f;      // 0073BF34
constexpr float kWobbleAmplitude = 0.1f;      // 0073BA80
constexpr float kMarkerRise = 60.0f;          // 0073F090
constexpr std::uint32_t kMarkerDepth = 0x41200000;   // 10.0f

// The interface sprite helpers on scene objects (CUiMesh, ui.h).
CUiMesh* Sprite(CEngineObject* object) { return reinterpret_cast<CUiMesh*>(object); }

Quaternion* RotationOf(CEngineObject* object) {
    return &Sprite(object)->localRotation_;
}

// A bar: its right edge (x of vertices 2 and 3) moves with the fraction, its texture follows.
void SetBar(CEngineObject* object, double fraction, std::uint32_t v0, std::uint32_t v1) {
    CUiMesh* bar = Sprite(object);
    float u = static_cast<float>(((fraction * kBarTexels + kBarStart) * kTexel) + kHalfTexel);
    double right = fraction - kHalf;
    bar->Quads()[0].x[2] = static_cast<float>(right);
    bar->Quads()[0].x[3] = static_cast<float>(right);
    bar->Texels()[2].u = u;
    bar->Texels()[2].v = FloatFromBits(v0);
    bar->Texels()[3].u = u;
    bar->Texels()[3].v = FloatFromBits(v1);
}

// The original inlines three quaternion products, each with its own operand order; every
// component is rounded once. The wobble target: q1 (about y) times q2 (about x).
Quaternion TargetProduct(const Quaternion& q1, const Quaternion& q2) {
    Quaternion r;
    r.w = static_cast<float>(D(q1.w) * D(q2.w) - DotUnrounded<Sum3::ZYX, double>(q1.x, q1.y, q1.z, q2.x, q2.y, q2.z));
    r.x = static_cast<float>(((D(q2.y) * D(q1.z) - D(q1.y) * D(q2.z)) + D(q1.w) * D(q2.x)) + D(q1.x) * D(q2.w));
    r.y = static_cast<float>(((D(q1.x) * D(q2.z) - D(q2.x) * D(q1.z)) + D(q1.w) * D(q2.y)) + D(q1.y) * D(q2.w));
    r.z = static_cast<float>(((D(q1.y) * D(q2.x) - D(q1.x) * D(q2.y)) + D(q2.w) * D(q1.z)) + D(q2.z) * D(q1.w));
    return r;
}

// The face's rest orientation from the slot's two parts (a, b).
Quaternion RestProduct(const Quaternion& a, const Quaternion& b) {
    Quaternion r;
    r.w = static_cast<float>(D(a.w) * D(b.w) - DotUnrounded<Sum3::ZYX, double>(a.x, a.y, a.z, b.x, b.y, b.z));
    r.x = static_cast<float>(((D(a.y) * D(b.z) - D(a.z) * D(b.y)) + D(a.x) * D(b.w)) + D(b.x) * D(a.w));
    r.y = static_cast<float>(((D(a.z) * D(b.x) - D(a.x) * D(b.z)) + D(a.y) * D(b.w)) + D(a.w) * D(b.y));
    r.z = static_cast<float>(((D(a.x) * D(b.y) - D(b.x) * D(a.y)) + D(a.z) * D(b.w)) + D(b.z) * D(a.w));
    return r;
}

// The eased face orientation f with the wobble w applied.
Quaternion WobbleProduct(const Quaternion& f, const Quaternion& w) {
    Quaternion r;
    r.w = static_cast<float>(D(f.w) * D(w.w) - DotUnrounded<Sum3::ZYX, double>(f.x, f.y, f.z, w.x, w.y, w.z));
    r.x = static_cast<float>(((D(w.z) * D(f.y) - D(f.z) * D(w.y)) + D(w.x) * D(f.w)) + D(f.x) * D(w.w));
    r.y = static_cast<float>(((D(f.z) * D(w.x) - D(w.z) * D(f.x)) + D(w.y) * D(f.w)) + D(f.y) * D(w.w));
    r.z = static_cast<float>(((D(w.y) * D(f.x) - D(w.x) * D(f.y)) + D(w.z) * D(f.w)) + D(f.z) * D(w.w));
    return r;
}

float RandomTilt(std::uint32_t r) {
    return static_cast<float>((x87::FromUnsigned(r) * RandomScale31() - 1.0f) * D(WobbleAngle()) * kTwelfth);
}

Quaternion AxisAngle(float x, float y, float z, float angle) {
    Vector3 axis = {x, y, z};
    Quaternion q;
    q.SetAxisAngle(axis, angle);
    return q;
}

} // namespace


void CHudPortraits::Update() {
    float dt = GamePaused() ? 0.0f : static_cast<float>(WindowFrameTime(this));
    Sprite(sceneObjects_.data[0])->visible_ = 0;   // the selection marker

    for (int i = 0; i < owner_->party_.size; ++i) {
        if (i >= count_) continue;
        // Objects 5i+1 .. 5i+5, read afresh each time (replacing the face model may move the array).
        auto object = [this, i](int k) { return sceneObjects_.data[i * 5 + k]; };
        HudPortraitSlot& slot = slots_[i];
        CUnit* unit = owner_->party_[i];

        // Health bar (object 5i+2) and mana bar (5i+3).
        int hitPoints = unit->packedVitals.hitPoints < 0 ? 0 : unit->packedVitals.hitPoints;
        double health = unit->packedVitals.maxHitPoints > 0
                            ? static_cast<double>(hitPoints) / static_cast<double>(unit->packedVitals.maxHitPoints)
                            : 0.0;
        SetBar(object(2), health, kHealthV0, kHealthV1);
        std::uint16_t maxMana = unit->packedVitals.maxMana;
        double mana = 0.0;
        if (maxMana != 0) {
            float current = static_cast<float>(unit->packedVitals.mana);   // exact (16 bits)
            float positive = 0.0f < current || x87::IsNaN(current) ? current : 0.0f;
            mana = D(positive) / static_cast<double>(static_cast<int>(maxMana));
        }
        SetBar(object(3), mana, kManaV0, kManaV1);

        // A drop in hit points flashes the portrait (object 5i+5) and shows the hit picture.
        if (slot.lastHitPoints_ > hitPoints) {
            slot.flash_ = 1.0f;
            slot.lastHitPoints_ = hitPoints;
            slot.faceTimer_ = 0.0f;
            slot.faceMode_ = 2;
        } else if (slot.flash_ > 0.0f) {
            slot.flash_ = static_cast<float>(D(slot.flash_) - D(dt));
        }
        if (!(slot.flash_ >= 0.0f)) slot.flash_ = 0.0f;
        CEngineObject* flash = object(5);
        if (slot.flash_ > 0.0f) {
            Sprite(flash)->visible_ = 1;
            int alpha = static_cast<int>(gamecrt::Ftol(D(slot.flash_) * kFlashAlpha));
            std::uint32_t colour = static_cast<std::uint32_t>(alpha) << 24 | 0xFF0000u;
            Sprite(flash)->flatColour_ = colour;
        } else {
            Sprite(flash)->visible_ = 0;
        }

        // The face picture: a timed one (hit, mode 1) while it lasts, else low mana or normal.
        int state = 0;
        if (slot.faceMode_ != 0) {
            slot.faceTimer_ = static_cast<float>(D(dt) + D(slot.faceTimer_));
            if (slot.faceMode_ == 1) {
                if (slot.faceTimer_ > kMode1PictureTime) slot.faceMode_ = 0;
            } else if (slot.faceMode_ == 2) {
                if (slot.faceTimer_ > kHitPictureTime) slot.faceMode_ = 0;
            }
            state = slot.faceMode_;
        }
        if (state == 0) {
            std::uint16_t maximum = unit->packedVitals.maxMana;
            if (maximum == 0 ||
                !(static_cast<double>(static_cast<int>(unit->packedVitals.mana)) / static_cast<double>(static_cast<int>(maximum)) >=
                  kLowMana))
                state = 3;
        }
        if (state != slot.faceState_) {
            slot.faceState_ = state;
            ShowFace(i, state);
        }

        // The face model follows the unit's build (its scale's y): thin, medium or fat.
        float build = unit->scale.y;
        if (slot.build_ < build || slot.build_ > build) {
            slot.build_ = build;
            mfc::CString name("me");
            if (!(build >= kThin))
                name = "th";
            else if (build > kFat)
                name = "fa";
            {
                const auto& prefix = *reinterpret_cast<const mfc::CString*>(&slot.modelPrefix_);
                const auto& suffix = *reinterpret_cast<const mfc::CString*>(&slot.modelSuffix_);
                mfc::CString withPrefix = prefix + name;
                mfc::CString full = withPrefix + suffix;
                name = full;
            }
            ReplaceSceneObject(object(1), name);   // CUiWindow's (005FF460)
        }

        // Now and then (a third of the time per second) a wobble starts after a random delay:
        // a turn towards a random target.
        Quaternion wobble = {1.0f, 0.0f, 0.0f, 0.0f};
        if (slot.wobbleKind_ == 0 && dt > 0.0f) {
            std::uint32_t roll = Random()->Next();
            if (D(dt) * kWobbleRate > x87::FromUnsigned(roll) * RandomScale32()) {
                slot.wobbleTime_ = static_cast<float>(x87::FromUnsigned(Random()->Next()) * RandomDelayScale());
                slot.wobbleKind_ = 3;
                std::uint32_t r1 = Random()->Next();
                std::uint32_t r2 = Random()->Next();
                Quaternion q1 = AxisAngle(0.0f, 1.0f, 0.0f, RandomTilt(r1));
                Quaternion q2 = AxisAngle(1.0f, 0.0f, 0.0f, RandomTilt(r2));
                slot.wobbleTarget_ = TargetProduct(q1, q2);
                slot.wobbleSpeed_ = static_cast<float>(x87::FromUnsigned(Random()->Next()) * RandomScale31() + 1.0f);
            }
        }
        if (slot.wobbleKind_ != 0) {
            slot.wobbleTime_ = static_cast<float>(D(dt) + D(slot.wobbleTime_));
            if (slot.wobbleTime_ >= 0.0f) {
                float t = slot.wobbleTime_;
                switch (slot.wobbleKind_) {
                case 1:   // nod
                    if (D(Pi()) * kWobbleSpan > D(t))
                        wobble = AxisAngle(1.0f, 0.0f, 0.0f,
                                           static_cast<float>(D(WobbleAngle()) * x87::fsin(D(t) * kWobbleFrequency) * kWobbleAmplitude));
                    else
                        slot.wobbleKind_ = 0;
                    break;
                case 2:   // shake
                    if (D(TurnPeriod()) * kWobbleSpan > D(t))
                        wobble = AxisAngle(0.0f, 1.0f, 0.0f,
                                           static_cast<float>(D(WobbleAngle()) * x87::fsin(D(t) * kWobbleFrequency) * kWobbleAmplitude));
                    else
                        slot.wobbleKind_ = 0;
                    break;
                case 3:   // turn towards the target and back
                    if (D(TurnPeriod()) / D(slot.wobbleSpeed_) > D(t)) {
                        float w = static_cast<float>(kHalf - x87::fcos(D(t) * D(slot.wobbleSpeed_)) * kHalf);
                        wobble.Slerp(w, *Identity(), slot.wobbleTarget_);
                    } else {
                        slot.wobbleKind_ = 0;
                    }
                    break;
                default:
                    break;
                }
            }
        }

        // The face eases towards its rest orientation (fully when the frame is long).
        Quaternion rest = RestProduct(slot.restA_, slot.restB_);
        Quaternion previous = slot.current_;
        Quaternion face;
        if (!(dt >= kHalf))
            face.Slerp(static_cast<float>(D(dt) + D(dt)), previous, rest);
        else
            face = rest;
        slot.current_ = face;
        if (slot.wobbleKind_ != 0) face = WobbleProduct(face, wobble);
        *RotationOf(object(1)) = face;

        // The selection marker (object 0) sits above the selected unit's portrait (object 5i+4).
        if (SelectedUnitId() == unit->id_) {
            Sprite(sceneObjects_.data[0])->visible_ = 1;
            float x, y, depth;
            Sprite(object(4))->GetPlacement(&x, &y, &depth);
            Sprite(sceneObjects_.data[0])->Place(x, static_cast<float>(D(y) - kMarkerRise), FloatFromBits(kMarkerDepth));
        }
    }
}
EI_REPLACE_METHOD(0x00641080, CHudPortraits::Update);
