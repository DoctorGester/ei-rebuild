// CLightning's initialiser (called by the lightning client object's setter). Plain float maths (rule 7);
// random words converted with x87::FromUnsigned. See docs/notes/objects.md ("Rewritten").
#include "lightning.h"
#include "texture.h"

#include "random.h"

#include "ei/x87.h"
#include "lib/crt.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include "game_singletons.h"
#include "lib/nan_audit.h"

namespace {

float Absolute(float f) {
    std::uint32_t bits;
    std::memcpy(&bits, &f, 4);
    bits &= 0x7FFFFFFFu;
    std::memcpy(&f, &bits, 4);
    return f;
}

// The noise amplitude of a segment's keys: 1.5 for positive power, 1 for negative, else 2 (zero or NaN).
float KeyAmplitude(float power) {
    if (power > 0.0f) return 1.5f;
    return power < 0.0f ? 1.0f : 2.0f;
}

void FreshKeys(LightningSegment& s, float time, float amplitude) {
    s.noiseTime = time;
    for (int k = 0; k < 3; ++k) {
        s.sideKeys[k].Randomize(amplitude);
        s.upKeys[k].Randomize(amplitude);
    }
}

} // namespace

void CLightning::Start(const Vector3* from, const Vector3* to, float power, bool demon) {
    int ticks = CurrentClientTick();
    float fraction = ClientTickFraction();
    float time = static_cast<float>((static_cast<double>(ticks) + fraction) / 4.0f /*0073C4F8*/);
    power_ = power;
    const float strength = Absolute(power);
    amplitude_ = power_ > 0.0f /*0073B858*/ ? 0.05f /*0073C52C*/ : 0.3f /*0073C54C*/;   // 0.05 : 0.3
    // A bolt about 30 units high sways less.
    EI_NAN_AUDIT(to->z, from->z);
    if (Absolute(Absolute(to->z - from->z) - 30.0f /*0073BAF0*/) < 0.01f /*0073C548*/) amplitude_ = 0.05f;

    main_.Init(from, to);
    FreshKeys(main_, time, KeyAmplitude(power_));
    main_.width = strength;
    EI_NAN_AUDIT(power_);
    if (power_ < 0.0f /*0073B858*/) main_.width = strength * 1.5f /*0073C51C*/;

    EI_NAN_AUDIT(strength);
    if (strength != 0.0f) {
        const float positions[6] = {0.2f, 0.3f, 0.5f, 0.8f, 0.9f, 0.9f};
        std::memcpy(branchPos_, positions, sizeof(positions));
        for (int i = 0; i < 6; ++i) {
            float along = branchPos_[i];
            float amplitude = KeyAmplitude(power_);
            float rest = 1.0f - along;
            Vector3 origin;
            origin.x = along * from->x + rest * to->x;
            origin.y = along * from->y + rest * to->y;
            origin.z = along * from->z + rest * to->z;
            branches_[i].Init(&origin, to);
            FreshKeys(branches_[i], time, amplitude);
            double random = x87::FromUnsigned(g_random.Next());
            branches_[i].width = static_cast<float>((random * 1.8626451774202136e-10 /*0073C540*/ + 0.7f /*0073C538*/) * strength *
                                                    0.5f /*0073B86C*/);
        }
    } else {
        main_.width = 4.0f;
        for (int i = 0; i < 6; ++i) {
            float dx = from->x - to->x, dy = from->y - to->y, dz = from->z - to->z;
            float reach = static_cast<float>(std::sqrt(LengthSquaredUnrounded<Sum3::ZYX>(dx, dy, dz)) * 0.05f /*0073C52C*/);
            float low = -reach;
            const double scale = 2.3283064370807974e-10 /*0073C468*/;
            float offsetY = static_cast<float>((reach - low) * x87::FromUnsigned(g_random.Next()) * scale + low);
            float offsetX = static_cast<float>((reach - low) * x87::FromUnsigned(g_random.Next()) * scale + low);
            float offsetZ = static_cast<float>((reach - low) * x87::FromUnsigned(g_random.Next()) * scale + low);
            float amplitude = KeyAmplitude(power_);
            Vector3 shiftedTo{offsetX + to->x, offsetY + to->y, offsetZ + to->z};
            Vector3 shiftedFrom{offsetX + from->x, offsetY + from->y, offsetZ + from->z};
            branches_[i].Init(&shiftedFrom, &shiftedTo);
            FreshKeys(branches_[i], time, amplitude);
            double random = x87::FromUnsigned(g_random.Next());
            branches_[i].width = static_cast<float>(random * 1.8626451774202136e-10 /*0073C540*/ * 3.5f /*0073C534*/ +
                                                    2.45f /*0073C530*/);   // 2.45 .. 3.25
        }
    }

    Animate();
    UpdateBounds();
    bounds_.boxMin.x -= 1.0f;
    bounds_.boxMin.y -= 1.0f;
    bounds_.boxMin.z -= 1.0f;
    bounds_.boxMax.x += 1.0f;
    bounds_.boxMax.y += 1.0f;
    bounds_.boxMax.z += 1.0f;
    bounds_.radius = bounds_.boxMax.Length();
    if (demon) {
        glowTexture_.Load(6, "DemonLight");
        texture_.Load(6, "DemonLightning");
    } else {
        glowTexture_.Load(6, "SmtgLight");
        texture_.Load(6, "Lightning");
    }
}

LightningSegment::LightningSegment() {
}

LightningSegment::~LightningSegment() {
}

CLightning::CLightning() {
    ClearFields();
    texture_ = nullptr;
    glowTexture_ = nullptr;
    std::memset(static_cast<void*>(&bounds_), 0, sizeof(bounds_));
    std::memset(&boundsSpare_, 0, sizeof(boundsSpare_));
    CLightning::Destroy();
}

// The texture references are null after the reset (releasing them again is a no-op).
CLightning::~CLightning() { CLightning::Destroy(); }

const char* CLightning::GetClassName() { return "CLightning"; }
int CLightning::GetClassId() { return 0x1E; }

CEngineObject* CLightningManager::NewObject(int /*type*/) { return new CLightning(); }
