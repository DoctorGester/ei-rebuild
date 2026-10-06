// The battle HUD's party portraits, per frame (CHudPortraits::Update, 00641080): each portrait's
// health and mana bars, the red flash after a hit, the face picture (normal, hit, low mana) and
// model (by the unit's build), the face model's easing towards its rest orientation with an
// occasional random nod, shake or turn, and the marker under the selected unit's portrait.
// See docs/notes/ui.md.
//
// Floating point follows the original's x87 code: `double` for values the original keeps on the
// FPU stack, Store where it stores to a float. Compiled with precise semantics so that the
// operation order is kept and NaN compares stay as written.
#include "ui.h"

#include "maths.h"
#include "random.h"
#include "unit.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/mfc.h"

#include <cstdint>
#include <cstring>
#include "originals_ui.h"

#pragma float_control(precise, on)

namespace {

inline double D(float f) { return f; }

float Store(double v) {
    volatile float r = static_cast<float>(v);
    return r;
}

float FloatFromBits(std::uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

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

// The game's _ftol (006E853C): truncation of the value in ST0.
__declspec(naked) int __cdecl GameFtol(double) {
    __asm {
        fld qword ptr [esp + 4]
        mov eax, 0x006E853C
        call eax
        ret
    }
}

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

struct SpriteQuery {
    // 005E7010 (not rewritten). The sprite's layout position (x, y) and depth.
    void Position(float* x, float* y, float* depth);
};
__declspec(naked) void SpriteQuery::Position(float*, float*, float*) EI_ORIGINAL(0x005E7010)

Quaternion* RotationOf(CEngineObject* object) {
    return reinterpret_cast<Quaternion*>(reinterpret_cast<std::uint8_t*>(object) + 0x34);
}

// A bar: its right edge (x of vertices 2 and 3) moves with the fraction, its texture follows.
void SetBar(CEngineObject* object, double fraction, std::uint32_t v0, std::uint32_t v1) {
    CUiMesh* bar = Sprite(object);
    float u = Store(((fraction * kBarTexels + kBarStart) * kTexel) + kHalfTexel);
    double right = fraction - kHalf;
    bar->quads_[0].x[2] = Store(right);
    bar->quads_[0].x[3] = Store(right);
    bar->uvs_[4] = u;
    bar->uvs_[5] = FloatFromBits(v0);
    bar->uvs_[6] = u;
    bar->uvs_[7] = FloatFromBits(v1);
}

// The original inlines three quaternion products, each with its own operand order; every
// component is rounded once. The wobble target: q1 (about y) times q2 (about x).
Quaternion TargetProduct(const Quaternion& q1, const Quaternion& q2) {
    Quaternion r;
    r.w = Store(D(q1.w) * D(q2.w) - ((D(q2.z) * D(q1.z) + D(q1.y) * D(q2.y)) + D(q1.x) * D(q2.x)));
    r.x = Store(((D(q2.y) * D(q1.z) - D(q1.y) * D(q2.z)) + D(q1.w) * D(q2.x)) + D(q1.x) * D(q2.w));
    r.y = Store(((D(q1.x) * D(q2.z) - D(q2.x) * D(q1.z)) + D(q1.w) * D(q2.y)) + D(q1.y) * D(q2.w));
    r.z = Store(((D(q1.y) * D(q2.x) - D(q1.x) * D(q2.y)) + D(q2.w) * D(q1.z)) + D(q2.z) * D(q1.w));
    return r;
}

// The face's rest orientation from the slot's two parts (a, b).
Quaternion RestProduct(const Quaternion& a, const Quaternion& b) {
    Quaternion r;
    r.w = Store(D(a.w) * D(b.w) - ((D(a.z) * D(b.z) + D(a.y) * D(b.y)) + D(a.x) * D(b.x)));
    r.x = Store(((D(a.y) * D(b.z) - D(a.z) * D(b.y)) + D(a.x) * D(b.w)) + D(b.x) * D(a.w));
    r.y = Store(((D(a.z) * D(b.x) - D(a.x) * D(b.z)) + D(a.y) * D(b.w)) + D(a.w) * D(b.y));
    r.z = Store(((D(a.x) * D(b.y) - D(b.x) * D(a.y)) + D(a.z) * D(b.w)) + D(b.z) * D(a.w));
    return r;
}

// The eased face orientation f with the wobble w applied.
Quaternion WobbleProduct(const Quaternion& f, const Quaternion& w) {
    Quaternion r;
    r.w = Store(D(f.w) * D(w.w) - ((D(f.z) * D(w.z) + D(f.y) * D(w.y)) + D(w.x) * D(f.x)));
    r.x = Store(((D(w.z) * D(f.y) - D(f.z) * D(w.y)) + D(w.x) * D(f.w)) + D(f.x) * D(w.w));
    r.y = Store(((D(f.z) * D(w.x) - D(w.z) * D(f.x)) + D(w.y) * D(f.w)) + D(f.y) * D(w.w));
    r.z = Store(((D(w.y) * D(f.x) - D(w.x) * D(f.y)) + D(w.z) * D(f.w)) + D(f.z) * D(w.w));
    return r;
}

// A random rotation angle in [-1, 1) * WobbleAngle() / 12 (two of them build a wobble target).
// A random word as the original loads it: FILD of the zero-extended qword, exact in ST0. (MSVC's
// own unsigned-to-double is FILD dword plus 2^32, an FADD the FPU rounds to its precision: under
// the game's 24-bit precision that changed words of 2^31 and up.)
__declspec(naked) double __cdecl Exact(std::uint32_t) {
    __asm {
        push 0
        push dword ptr [esp + 8]
        fild qword ptr [esp]
        add esp, 8
        ret
    }
}

float RandomTilt(std::uint32_t r) {
    return Store((Exact(r) * RandomScale31() - 1.0f) * D(WobbleAngle()) * kTwelfth);
}

Quaternion AxisAngle(float x, float y, float z, float angle) {
    Vector3 axis = {x, y, z};
    Quaternion q;
    q.SetAxisAngle(axis, angle);
    return q;
}

} // namespace

__declspec(naked) void CHudPortraits::ShowFaceAt(int, int) EI_ORIGINAL(0x00640670)
__declspec(naked) CEngineObject* CHudPortraits::ReplaceSceneObject(CEngineObject*, const char*) EI_ORIGINAL(0x005FF460)

void CHudPortraits::Update() {
    float dt = GamePaused() ? 0.0f : Store(WindowFrameTime(this));
    Sprite(sceneObjects_.data[0])->visible_ = 0;   // the selection marker

    for (int i = 0; i < owner_->partyCount_; ++i) {
        if (i >= count_) continue;
        // Objects 5i+1 .. 5i+5, read afresh each time (replacing the face model may move the array).
        auto object = [this, i](int k) { return sceneObjects_.data[i * 5 + k]; };
        HudPortraitSlot& slot = slots_[i];
        CUnit* unit = owner_->party_[i];

        // Health bar (object 5i+2) and mana bar (5i+3).
        int hitPoints = unit->hitPoints < 0 ? 0 : unit->hitPoints;
        double health = unit->maxHitPoints > 0
                            ? static_cast<double>(hitPoints) / static_cast<double>(unit->maxHitPoints)
                            : 0.0;
        SetBar(object(2), health, kHealthV0, kHealthV1);
        std::uint16_t maxMana;
        std::memcpy(&maxMana, reinterpret_cast<const std::uint8_t*>(unit) + 0x184, 2);
        double mana = 0.0;
        if (maxMana != 0) {
            float current = static_cast<float>(unit->mana);   // exact (16 bits)
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
            slot.flash_ = Store(D(slot.flash_) - D(dt));
        }
        if (!(slot.flash_ >= 0.0f)) slot.flash_ = 0.0f;
        CEngineObject* flash = object(5);
        if (slot.flash_ > 0.0f) {
            Sprite(flash)->visible_ = 1;
            int alpha = GameFtol(D(slot.flash_) * kFlashAlpha);
            std::uint32_t colour = static_cast<std::uint32_t>(alpha) << 24 | 0xFF0000u;
            std::memcpy(reinterpret_cast<std::uint8_t*>(flash) + 0xD4, &colour, 4);
        } else {
            Sprite(flash)->visible_ = 0;
        }

        // The face picture: a timed one (hit, mode 1) while it lasts, else low mana or normal.
        int state = 0;
        if (slot.faceMode_ != 0) {
            slot.faceTimer_ = Store(D(dt) + D(slot.faceTimer_));
            if (slot.faceMode_ == 1) {
                if (slot.faceTimer_ > kMode1PictureTime) slot.faceMode_ = 0;
            } else if (slot.faceMode_ == 2) {
                if (slot.faceTimer_ > kHitPictureTime) slot.faceMode_ = 0;
            }
            state = slot.faceMode_;
        }
        if (state == 0) {
            std::uint16_t maximum;
            std::memcpy(&maximum, reinterpret_cast<const std::uint8_t*>(unit) + 0x184, 2);
            if (maximum == 0 ||
                !(static_cast<double>(static_cast<int>(unit->mana)) / static_cast<double>(static_cast<int>(maximum)) >=
                  kLowMana))
                state = 3;
        }
        if (state != slot.faceState_) {
            slot.faceState_ = state;
            ShowFaceAt(i, state);
        }

        // The face model follows the unit's build (+0xC8): thin, medium or fat.
        float build;
        std::memcpy(&build, reinterpret_cast<const std::uint8_t*>(unit) + 0xC8, 4);
        if (slot.build_ < build || slot.build_ > build) {
            slot.build_ = build;
            mfc::CString name(reinterpret_cast<const char*>(0x007844A8));   // "me"
            if (!(build >= kThin))
                name = reinterpret_cast<const char*>(0x007945AC);           // "th"
            else if (build > kFat)
                name = reinterpret_cast<const char*>(0x007945A8);           // "fa"
            {
                const auto& prefix = *reinterpret_cast<const mfc::CString*>(&slot.modelPrefix_);
                const auto& suffix = *reinterpret_cast<const mfc::CString*>(&slot.modelSuffix_);
                mfc::CString withPrefix = prefix + name;
                mfc::CString full = withPrefix + suffix;
                name = full;
            }
            ReplaceSceneObject(object(1), name);
        }

        // Now and then (a third of the time per second) a wobble starts after a random delay:
        // a turn towards a random target.
        Quaternion wobble = {1.0f, 0.0f, 0.0f, 0.0f};
        if (slot.wobbleKind_ == 0 && dt > 0.0f) {
            std::uint32_t roll = Random()->Next();
            if (D(dt) * kWobbleRate > Exact(roll) * RandomScale32()) {
                slot.wobbleTime_ = Store(Exact(Random()->Next()) * RandomDelayScale());
                slot.wobbleKind_ = 3;
                std::uint32_t r1 = Random()->Next();
                std::uint32_t r2 = Random()->Next();
                Quaternion q1 = AxisAngle(0.0f, 1.0f, 0.0f, RandomTilt(r1));
                Quaternion q2 = AxisAngle(1.0f, 0.0f, 0.0f, RandomTilt(r2));
                slot.wobbleTarget_ = TargetProduct(q1, q2);
                slot.wobbleSpeed_ = Store(Exact(Random()->Next()) * RandomScale31() + 1.0f);
            }
        }
        if (slot.wobbleKind_ != 0) {
            slot.wobbleTime_ = Store(D(dt) + D(slot.wobbleTime_));
            if (slot.wobbleTime_ >= 0.0f) {
                float t = slot.wobbleTime_;
                switch (slot.wobbleKind_) {
                case 1:   // nod
                    if (D(Pi()) * kWobbleSpan > D(t))
                        wobble = AxisAngle(1.0f, 0.0f, 0.0f,
                                           Store(D(WobbleAngle()) * x87::fsin(D(t) * kWobbleFrequency) * kWobbleAmplitude));
                    else
                        slot.wobbleKind_ = 0;
                    break;
                case 2:   // shake
                    if (D(TurnPeriod()) * kWobbleSpan > D(t))
                        wobble = AxisAngle(0.0f, 1.0f, 0.0f,
                                           Store(D(WobbleAngle()) * x87::fsin(D(t) * kWobbleFrequency) * kWobbleAmplitude));
                    else
                        slot.wobbleKind_ = 0;
                    break;
                case 3:   // turn towards the target and back
                    if (D(TurnPeriod()) / D(slot.wobbleSpeed_) > D(t)) {
                        float w = Store(kHalf - x87::fcos(D(t) * D(slot.wobbleSpeed_)) * kHalf);
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
            face.Slerp(Store(D(dt) + D(dt)), previous, rest);
        else
            face = rest;
        slot.current_ = face;
        if (slot.wobbleKind_ != 0) face = WobbleProduct(face, wobble);
        *RotationOf(object(1)) = face;

        // The selection marker (object 0) sits above the selected unit's portrait (object 5i+4).
        if (SelectedUnitId() == unit->id_) {
            Sprite(sceneObjects_.data[0])->visible_ = 1;
            float x, y, depth;
            reinterpret_cast<SpriteQuery*>(object(4))->Position(&x, &y, &depth);
            Sprite(sceneObjects_.data[0])->Place(x, Store(D(y) - kMarkerRise), FloatFromBits(kMarkerDepth));
        }
    }
}
EI_REPLACE_METHOD(0x00641080, CHudPortraits::Update);
