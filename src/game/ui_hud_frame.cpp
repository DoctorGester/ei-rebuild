// Per-frame updates of the battle HUD panels: the clock (CHudClock::Update, 006488E0) turns its
// face with the time of day, moves the hand and the speed dial and makes the pause button blink
// while the game is paused; the top panel (CHudTopPanel::Update, 00649FF0) slides in and out.
// See docs/notes/ui.md.
//
// Floating point follows the original's x87 code: `double` for values the original keeps on the
// FPU stack, Store where it stores to a float. Compiled with precise semantics so that the
// operation order is kept and NaN compares stay as written.
#include "ui.h"

#include "maths.h"

#include "ei/hook.h"
#include "ei/x87.h"

#include <concepts>
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
// A float argument would convert to an integer first: callers pass raw bits only.
template <std::floating_point T> float FloatFromBits(T) = delete;

float GameFloat(std::uintptr_t address) { return *reinterpret_cast<const volatile float*>(address); }
float Pi() { return GameFloat(0x007423F8); }
float BlinkRate() { return GameFloat(0x007C8140); }   // radians per blink phase unit (a global)
// The original reads its constants from memory at run time; Rebirth rewrites some of them
// (00742B08, the clock's x, for its widescreen layout), so they are read the same way.
float PausedHand() { return GameFloat(0x00742B18); }   // hand angle while paused, in pi
float FastHand() { return GameFloat(0x00742B14); }   // at fast speed
float NormalHand() { return GameFloat(0x0073C524); }   // at normal speed
float Half() { return GameFloat(0x0073B86C); }   // 0.5
float DialStep() { return GameFloat(0x0073CE40); }   // dial notches, in pi
float FaceStep() { return GameFloat(0x00741438); }   // one hour of the face, in pi
float FaceOffset() { return GameFloat(0x0073C518); }   // 15 hours
float HandRadius() { return GameFloat(0x00742B10); }   // 73
float DialRadius() { return GameFloat(0x0073BAF0); }   // 30
float CentreX() { return GameFloat(0x00742B08); }   // the clock centre (800; Rebirth writes the widescreen width here)
float CentreY() { return GameFloat(0x00742B0C); }   // 600
float BlinkBase() { return GameFloat(0x0073CDF8); }   // 1.4
float PulseAmplitude() { return GameFloat(0x0073C54C); }   // 0.3
float PulseMiddle() { return GameFloat(0x0073C538); }   // 0.7
float SlideTime() { return GameFloat(0x00742428); }   // 0.3 seconds for a full slide
bool GamePaused() { return *reinterpret_cast<const volatile bool*>(0x007AF624); }   // 00578D50

constexpr std::uint32_t kHandDepth = 0x41000000;   // 8.0f
constexpr std::uint32_t kSpriteDepth = 0x4101B287;   // 8.106: the dial marks, the top panel sling

// The time of day in hours: (client ticks * scale + offset) mod 24 (_CIfmod 006E95AA), stored.
float TimeOfDay() {
    float result;
    __asm {
        fild dword ptr ds:[0x007AF61C]
        fmul dword ptr ds:[0x007AF5D8]
        fadd dword ptr ds:[0x007AF5D4]
        fld qword ptr ds:[0x0073D108]
        mov eax, 0x006E95AA
        call eax
        fstp result
    }
    return result;
}

// 00600270 (not rewritten): time since the last frame in seconds, unrounded in ST0.
inline double __fastcall WindowFrameTime(CUiWindow* w) { return WindowFrameTimeOriginal(w); }

CUiMesh* Sprite(CEngineObject* object) { return reinterpret_cast<CUiMesh*>(object); }

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
    std::memcpy(&Sprite(object)->orientation_, &q, sizeof(q));
}

std::uint32_t Bits(float f) {
    std::uint32_t u;
    std::memcpy(&u, &f, sizeof(u));
    return u;
}

// The game's _ftol (006E853C): truncation of the value in ST0.
__declspec(naked) int __cdecl GameFtol(double) {
    __asm {
        fld qword ptr [esp + 4]
        mov eax, 0x006E853C
        call eax
        ret
    }
}

} // namespace

void CUiMesh::Place(float a0, float a1, float a2) { SpritePlaceOriginal((void*)this, nullptr, a0, a1, a2); }

void CHudClock::Update() {
    interface_->fastGame_ = fast_;
    float hours = TimeOfDay();
    CEngineObject** objects = sceneObjects_.data;
    CEngineObject* face = objects[0];

    // The blink phase runs from 0 to 1 once a second.
    double blink = WindowFrameTime(this) + D(blink_);
    blink_ = Store(blink);
    if (blink >= 1.0f) {
        double next;
        do {
            next = D(blink_) - 1.0f;
            blink_ = Store(next);
        } while (next >= 1.0f);
    }
    float pulse = Store(x87::fcos(D(BlinkRate()) * D(blink_)) * PulseAmplitude() + PulseMiddle());

    float handAngle;
    if (GamePaused()) {
        SetTint(face, Store(BlinkBase() - D(pulse)));
        handAngle = Store(D(Pi()) * PausedHand());
        Sprite(objects[1])->visible_ = blink_ > Half() ? 1 : 0;
    } else {
        SetTint(face, 1.0f);
        double step;
        if (fast_ != 0) {
            handAngle = Store(D(Pi()) * FastHand());
            step = WindowFrameTime(this);
        } else {
            handAngle = Store(D(Pi()) * NormalHand());
            step = WindowFrameTime(this) * Half();
        }
        dial_ = Store(step + D(dial_));
        Sprite(objects[1])->visible_ = 1;
    }

    // The dial turns by whole notches' worth and wraps.
    float notch = Store(D(Pi()) * DialStep());
    if (dial_ > notch) {
        double next;
        do {
            next = D(dial_) - D(notch);
            dial_ = Store(next);
            notch = Store(D(Pi()) * DialStep());
        } while (next > notch);
    }

    // The hand (object 1) about -z, placed on its circle.
    Turn(objects[1], -1.0f, handAngle);
    double handSin = x87::fsin(handAngle);
    float handY = Store(CentreY() - handSin * HandRadius());
    float handX = Store(x87::fcos(handAngle) * HandRadius() + CentreX());
    Sprite(objects[1])->Place(handX, handY, FloatFromBits(kHandDepth));

    // The face turns with the time of day.
    Turn(face, 1.0f, Store((D(hours) + FaceOffset()) * D(Pi()) * FaceStep()));

    // The four dial marks (objects 2-5), a notch apart.
    float angle = Store(D(dial_) - D(Pi()) * FaceStep());
    for (int k = 2; k < 6; ++k) {
        CEngineObject* mark = objects[k];
        Turn(mark, 1.0f, angle);
        double c = x87::fcos(angle);
        float y = Store(CentreY() - c * DialRadius());
        float x = Store(x87::fsin(angle) * DialRadius() + CentreX());
        Sprite(mark)->Place(x, y, FloatFromBits(kSpriteDepth));
        angle = Store(D(angle) - D(Pi()) * DialStep());
    }

    SetTint(objects[6], highlight_ ? pulse : 1.0f);
}
EI_REPLACE_METHOD(0x006488E0, CHudClock::Update);

// --- Top panel ---------------------------------------------------------------------------------

__declspec(naked) void CHudTopPanel::CallLayout() EI_ORIGINAL(0x00649E20)

namespace {
constexpr float kSlingX = 400.0f;               // the sling button's place: x
constexpr float kSlingInY = -52.0f;             // y while the panel slides out (turned over)
constexpr float kSlingOutY = 83.0f;             // y otherwise
// The sling button's rotation while the panel is shown (a global quaternion at 007C8150).
const void* SlingRestRotation() { return reinterpret_cast<const void*>(0x007C8150); }
} // namespace

void CHudTopPanel::Update() {
    if (sliding_) {
        double slide = WindowFrameTime(this) * D(slideDirection_) / SlideTime() + D(slide_);
        slide_ = Store(slide);
        if (slide > 1.0f) slide_ = 1.0f;
        if (!(slide_ >= 0.0f)) slide_ = 0.0f;
        // Ease in and out: 2s^2 for the first half, 1 - 2(1-s)^2 for the second.
        double eased;
        if (!(slide_ >= 0.5f)) {
            double s = D(slide_);
            eased = s * s;
            eased = eased + eased;
        } else {
            double r = 1.0f - D(slide_);
            double r2 = r * r;
            eased = 1.0f - (r2 + r2);
        }
        offset_ = GameFtol(static_cast<double>(travel_) * eased);
        if (Bits(slide_) == 0x3F800000 || slide_ == 0.0f || x87::IsNaN(slide_)) sliding_ = 0;
        CallLayout();
        CEngineObject* sling = sceneObjects_.data[2];
        float y;
        if (Bits(slideDirection_) == 0xBF800000) {   // -1: sliding out
            Turn(sling, 1.0f, Pi());
            y = kSlingInY;
        } else {
            std::memcpy(&Sprite(sling)->orientation_, SlingRestRotation(), 16);
            y = kSlingOutY;
        }
        Sprite(sling)->Place(kSlingX, y, FloatFromBits(kSpriteDepth));
    }
    interface_->hudPanelsIn_[1] = slideDirection_ > 0.0f ? 1 : 0;   // the panel is (coming) in
    interface_->hudTopPage_ = page_;
}
EI_REPLACE_METHOD(0x00649FF0, CHudTopPanel::Update);
