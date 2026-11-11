// EffectTornado: a tornado crossing the map in a straight line, hurting the units it passes.
// See world_effect.h and docs/notes/effects.md ("Tornado").
#include "game/effects/effects.h"
#include "game/app/singletons.h"
#include "game/objects/map_object.h"
#include "engine/core/random.h"
#include "game/ai/sector_map.h"
#include "game/sound/sound.h"
#include "game/units/unit.h"
#include "game/world/world.h"
#include "game/effects/world_effect.h"
#include "game/effects/world_effect_internal.h"

#include "lib/crt.h"
#include "lib/rounding.h"


#include <cmath>
#include <cstring>

namespace {

constexpr int kTornadoParticles = static_cast<int>(ParticleType::Tornado);
constexpr float kSoundNear = 10.0f;
constexpr float kSoundFar = 40.0f;
// The speed per tick: a random word to [0, 0.4) * 0.12 + 0.096.
constexpr float kRandomRange = 0.4f;
constexpr float kSpeedRange = 0.12f;
constexpr float kSpeedMin = 0.096f;
constexpr int kStopEmittingTicks = 110;        // the source stops emitting from here on
constexpr int kHurtUntil = 20;                 // no more hits for the last 20 ticks
constexpr int kHurtEvery = 8;
constexpr float kHurtRadius = 5.0f;
constexpr int kMaxCatchUp = 30;                // particle ticks a new source makes up for

// The hit every unit within kHurtRadius takes (built on the stack).
DamageInfo TornadoHit() {
    DamageInfo hit{};
    hit.Weight(DamageType::Other) = 1.0f;
    hit.attackRoll = 100.0f;
    hit.amount = 5.0f;
    hit.multiplier = 1.0f;
    hit.location = 1;
    return hit;
}

} // namespace

EffectTornado::EffectTornado() {
    Reset();   // inline
}

EffectTornado::~EffectTornado() {
    Reset();   // inline
    Reset();   // the virtual
}

void EffectTornado::Reset() {
    Effect::Reset();
    if (source != nullptr) source->StopAndClear();
    if (sound != -1) g_clientWorld.sounds->StopSound(sound);
    sound = -1;
    source.Reset();
}

void EffectTornado::Launch(const Vector3& from, const Vector3& to) {
    float speed = g_random.Below(kRandomRange) * kSpeedRange + kSpeedMin;
    Vector3 way{to.x - from.x, to.y - from.y, to.z - from.z};
    framesLeft = 0;
    float length = std::sqrt(LengthSquared(way));
    if (length > 0.0f) {
        float scale = speed / length;
        way *= scale;
        float ticks = length / speed;
        int rounded = lib::RoundEven(ticks);
        framesLeft = rounded < 0 ? 0 : static_cast<int>(rounded);
    }
    velocity = way;
    moving = effect::IsMoving(velocity);
    MarkChanged();
    localPosition = from;
    launchTick = OwningWorld()->tick;
}

bool EffectTornado::RunTick() {
    if (moving != 0) {
        localPosition = Vector3{localPosition.x + velocity.x, localPosition.y + velocity.y,
                                 localPosition.z + velocity.z};
        if (source != nullptr) source->localPosition = localPosition;
        if (sound != -1) g_clientWorld.sounds->SetSoundPosition(sound, localPosition, kSoundNear, kSoundFar);
    }
    if (framesLeft <= kStopEmittingTicks && source != nullptr) source->Stop();
    World* world = OwningWorld();
    if (world->IsServer() && framesLeft > kHurtUntil && (framesLeft & (kHurtEvery - 1)) == 0) {
        std::vector<Unit*> units = ServerWorld()->aiMap.sectorMap.FindUnitsInRadius(localPosition, kHurtRadius);
        DamageInfo hit = TornadoHit();
        for (Unit* unit : units) {
            if (unit->player != nullptr) unit->controller->TakeHit(&hit, nullptr, nullptr);
        }
    }
    --framesLeft;
    return framesLeft >= 0;
}

int EffectTornado::Unpack(int update, DeltaReader& field) {
    Vector3 position;
    field.ReadDelta(&position, 12);
    field.ReadDelta(&framesLeft, 4);
    field.ReadDelta(&velocity, 12);
    moving = effect::IsMoving(velocity);
    if (!field.SnapshotConsumed()) return 0;
    localPosition = position;
    Start(update);   // through the virtual
    return 1;
}

void EffectTornado::Pack(DeltaWriter& field) {
    Vector3 position = localPosition;
    field.WriteDelta(&position, 12);
    int ticks = framesLeft;
    field.WriteDelta(&ticks, 4);
    field.WriteDelta(&velocity, 12);
}

void EffectTornado::Start(int update) {
    if (update == 0 && source != nullptr) {
        source->Stop();
        source.Reset();
    }
    if (source != nullptr) return;
    if (sound == -1) {
        sound = g_clientWorld.sounds->Play3D("nature\\tornado\\circle.wav", 0, localPosition, kSoundNear, kSoundFar, true);
    }
    ParticleSource* particles = g_clientWorld.particleManager->CreateSource(kTornadoParticles);
    source.Assign(particles);
    source->localPosition = localPosition;
    source->ScaleSize(1.0f);
    int behind = g_clientWorld.tick - launchTick;
    if (behind >= kMaxCatchUp) behind = kMaxCatchUp;
    for (int i = 1; i < behind; ++i) source->RunTick();
}

void EffectTornado::Load(ByteStream* s) {
    s->ReadValue(localPosition);
    s->ReadValue(velocity);
    s->ReadValue(framesLeft);
    s->ReadValue(launchTick);
}

void EffectTornado::Save(ByteStream* s) {
    s->WriteValue(localPosition);
    s->WriteValue(velocity);
    int copy = framesLeft;
    s->WriteValue(copy);
    copy = launchTick;
    s->WriteValue(copy);
}
