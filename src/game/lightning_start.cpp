// Lightning's initialiser (called by the lightning client object's setter). Plain float maths (rule 7). See docs/notes/objects.md ("Rewritten").
#include "game_singletons.h"
#include "lightning.h"
#include "random.h"
#include "texture.h"
#include "world.h"

#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/nan_audit.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace {

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

void Lightning::Start(const Vector3& from, const Vector3& to, float newPower, bool demon) {
    int ticks = g_clientWorld.tick;
    float fraction = g_clientWorld.tickFraction;
    float time = (ticks + fraction) / 4.0f;
    power = newPower;
    const float strength = AbsBits(newPower);
    amplitude = power > 0.0f ? 0.05f : 0.3f;     // 0.05 : 0.3
    // A bolt about 30 units high sways less.
    EI_NAN_AUDIT(to.z, from.z);
    if (AbsBits(AbsBits(to.z - from.z) - 30.0f) < 0.01f) amplitude = 0.05f;

    main.Init(from, to);
    FreshKeys(main, time, KeyAmplitude(power));
    main.width = strength;
    EI_NAN_AUDIT(power);
    if (power < 0.0f) main.width = strength * 1.5f;

    EI_NAN_AUDIT(strength);
    if (strength != 0.0f) {
        const float positions[6] = {0.2f, 0.3f, 0.5f, 0.8f, 0.9f, 0.9f};
        std::memcpy(branchPos, positions, sizeof(positions));
        for (int i = 0; i < 6; ++i) {
            float along = branchPos[i];
            float newAmplitude = KeyAmplitude(power);
            float rest = 1.0f - along;
            Vector3 origin;
            origin.x = along * from.x + rest * to.x;
            origin.y = along * from.y + rest * to.y;
            origin.z = along * from.z + rest * to.z;
            branches[i].Init(origin, to);
            FreshKeys(branches[i], time, newAmplitude);
            branches[i].width = (g_clientRandom.Next() * 1.8626452e-10f + 0.7f) * strength * 0.5f;
        }
    } else {
        main.width = 4.0f;
        for (int i = 0; i < 6; ++i) {
            float dx = from.x - to.x, dy = from.y - to.y, dz = from.z - to.z;
            float reach = std::sqrt(LengthSquaredUnrounded<Sum3::ZYX>(dx, dy, dz)) * 0.05f;
            float low = -reach;
            const float scale = 2.3283064e-10f;
            float offsetY = (reach - low) * g_clientRandom.Next() * scale + low;
            float offsetX = (reach - low) * g_clientRandom.Next() * scale + low;
            float offsetZ = (reach - low) * g_clientRandom.Next() * scale + low;
            float newAmplitude = KeyAmplitude(power);
            Vector3 shiftedTo{offsetX + to.x, offsetY + to.y, offsetZ + to.z};
            Vector3 shiftedFrom{offsetX + from.x, offsetY + from.y, offsetZ + from.z};
            branches[i].Init(shiftedFrom, shiftedTo);
            FreshKeys(branches[i], time, newAmplitude);
            branches[i].width = g_clientRandom.Next() * 1.8626452e-10f * 3.5f + 2.45f;   // 2.45 .. 3.25
        }
    }

    Animate();
    UpdateBounds();
    bounds.boxMin.x -= 1.0f;
    bounds.boxMin.y -= 1.0f;
    bounds.boxMin.z -= 1.0f;
    bounds.boxMax.x += 1.0f;
    bounds.boxMax.y += 1.0f;
    bounds.boxMax.z += 1.0f;
    bounds.radius = bounds.boxMax.Length();
    if (demon) {
        LoadNamedTexture(glowTexture, static_cast<int>(TextureKind::Effects), "DemonLight");
        LoadNamedTexture(texture, static_cast<int>(TextureKind::Effects), "DemonLightning");
    } else {
        LoadNamedTexture(glowTexture, static_cast<int>(TextureKind::Effects), "SmtgLight");
        LoadNamedTexture(texture, static_cast<int>(TextureKind::Effects), "Lightning");
    }
}

LightningSegment::LightningSegment() {
}

LightningSegment::~LightningSegment() {
}

Lightning::Lightning() {
    std::memset(static_cast<void*>(&bounds), 0, sizeof(bounds));
    boundsSpare = {};
    Lightning::Destroy();
}

// The texture references are null after the reset (releasing them again is a no-op).
Lightning::~Lightning() { Lightning::Destroy(); }

const char* Lightning::ClassName() { return "CLightning"; }
ClassId Lightning::GetClassId() { return ClassId::Lightning; }

EngineObject* LightningManager::NewObject(int /*type*/) { return new Lightning(); }
