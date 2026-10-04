// CLightning's initialiser (004680D0; called by the lightning client object's setter 0067A479).
// Plain float maths (rule 7); random words converted with x87::FromUnsigned. See
// docs/notes/objects.md ("Sixth round").
#include "lightning.h"

#include "random.h"

#include "ei/hook.h"
#include "ei/x87.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace {

// 00500A00: releases the texture in `holder` and loads `name` (kind 6).
__declspec(naked) void __fastcall LoadTexture(void** /*holder*/, void*, int /*kind*/, const char* /*name*/) EI_ORIGINAL(0x00500A00)

IsaacRandom* Rng() { return reinterpret_cast<IsaacRandom*>(0x007A9D40); }
float GameFloat(std::uintptr_t address) {
    float f;
    std::memcpy(&f, reinterpret_cast<const void*>(address), sizeof(f));
    return f;
}
double GameDouble(std::uintptr_t address) {
    double d;
    std::memcpy(&d, reinterpret_cast<const void*>(address), sizeof(d));
    return d;
}
float Absolute(float f) {
    std::uint32_t bits;
    std::memcpy(&bits, &f, 4);
    bits &= 0x7FFFFFFFu;
    std::memcpy(&f, &bits, 4);
    return f;
}

// The noise amplitude of a segment's keys: 1.5 for positive power, 2 for zero (or NaN), else 1.
float KeyAmplitude(float power) {
    if (power > 0.0f) return 1.5f;
    return !(power < 0.0f) && !(power > 0.0f) ? 2.0f : 1.0f;
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
    int ticks = *reinterpret_cast<const volatile int*>(0x007AF61C);
    float fraction = *reinterpret_cast<const volatile float*>(0x007AFCA8);
    float time = static_cast<float>((static_cast<double>(ticks) + fraction) / GameFloat(0x0073C4F8));   // 4
    power_ = power;
    const float strength = Absolute(power);
    amplitude_ = power_ > GameFloat(0x0073B858) ? GameFloat(0x0073C52C) : GameFloat(0x0073C54C);   // 0.05 : 0.3
    // A bolt about 30 units high sways less.
    if (!(Absolute(Absolute(to->z - from->z) - GameFloat(0x0073BAF0)) >= GameFloat(0x0073C548))) amplitude_ = 0.05f;

    main_.Init(from, to);
    FreshKeys(main_, time, KeyAmplitude(power_));
    main_.width = strength;
    if (!(power_ >= GameFloat(0x0073B858))) main_.width = strength * GameFloat(0x0073C51C);   // 1.5

    if (strength > 0.0f || strength < 0.0f) {
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
            double random = x87::FromUnsigned(Rng()->Next());
            branches_[i].width = static_cast<float>((random * GameDouble(0x0073C540) + GameFloat(0x0073C538)) * strength *
                                                    GameFloat(0x0073B86C));
        }
    } else {
        main_.width = 4.0f;
        for (int i = 0; i < 6; ++i) {
            float dx = from->x - to->x, dy = from->y - to->y, dz = from->z - to->z;
            float reach = std::sqrt(dz * dz + dy * dy + dx * dx) * GameFloat(0x0073C52C);
            float low = -reach;
            const double scale = GameDouble(0x0073C468);
            float offsetY = static_cast<float>((reach - low) * x87::FromUnsigned(Rng()->Next()) * scale + low);
            float offsetX = static_cast<float>((reach - low) * x87::FromUnsigned(Rng()->Next()) * scale + low);
            float offsetZ = static_cast<float>((reach - low) * x87::FromUnsigned(Rng()->Next()) * scale + low);
            float amplitude = KeyAmplitude(power_);
            Vector3 shiftedTo{offsetX + to->x, offsetY + to->y, offsetZ + to->z};
            Vector3 shiftedFrom{offsetX + from->x, offsetY + from->y, offsetZ + from->z};
            branches_[i].Init(&shiftedFrom, &shiftedTo);
            FreshKeys(branches_[i], time, amplitude);
            double random = x87::FromUnsigned(Rng()->Next());
            branches_[i].width = static_cast<float>(random * GameDouble(0x0073C540) * GameFloat(0x0073C534) +
                                                    GameFloat(0x0073C530));   // 2.45 .. 3.25
        }
    }

    Animate();
    UpdateBounds();
    boundsMin_.x -= 1.0f;
    boundsMin_.y -= 1.0f;
    boundsMin_.z -= 1.0f;
    boundsMax_.x += 1.0f;
    boundsMax_.y += 1.0f;
    boundsMax_.z += 1.0f;
    radius_ = boundsMax_.Length();
    auto** glow = reinterpret_cast<void**>(&glowTexture_);
    auto** main = reinterpret_cast<void**>(&texture_);
    if (demon) {
        LoadTexture(glow, nullptr, 6, reinterpret_cast<const char*>(0x00787CE4));   // "DemonLight"
        LoadTexture(main, nullptr, 6, reinterpret_cast<const char*>(0x00787CD4));   // "DemonLightning"
    } else {
        LoadTexture(glow, nullptr, 6, reinterpret_cast<const char*>(0x00787CC8));   // "SmtgLight"
        LoadTexture(main, nullptr, 6, reinterpret_cast<const char*>(0x00787CBC));   // "Lightning"
    }
}

namespace {
void __fastcall StartHook(CLightning* self, void*, const Vector3* from, const Vector3* to, float power, bool demon) {
    self->Start(from, to, power, demon);
}
} // namespace
EI_REPLACE(0x004680D0, StartHook);
