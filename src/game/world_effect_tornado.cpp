// EffectTornado: a tornado crossing the map in a straight line, hurting the units it passes.
// See world_effect.h and docs/notes/effects.md ("Tornado").
#include "effects.h"
#include "game_singletons.h"
#include "game_strings.h"
#include "map_object.h"
#include "random.h"
#include "sector_map.h"
#include "sound.h"
#include "unit.h"
#include "world.h"
#include "world_effect.h"
#include "world_effect_internal.h"

#include "lib/crt.h"

#include "ei/x87.h"

#include <cmath>
#include <cstring>
#include <list>

namespace {

constexpr int kTornadoParticles = static_cast<int>(ParticleType::Tornado);
constexpr float kSoundNear = 10.0f;
constexpr float kSoundFar = 40.0f;
// The speed per tick: a random word * 2^-32 * 0.12 + 0.096.
constexpr double kRandomScale = 9.313226580990458e-11;
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
    hit.typeWeights[6] = 1.0f;
    hit.attackRoll = 100.0f;
    hit.amount = 5.0f;
    hit.multiplier = 1.0f;
    hit.location = 1;
    return hit;
}

// The snapshot read against was consumed to its end (Position and Size of the stream, in that order).
bool SnapshotReadToEnd() {
    MemoryStream* snapshot = delta::g_readSnapshot;
    std::uint32_t position = snapshot->Position();
    return position == snapshot->Size();
}

} // namespace

EffectTornado::EffectTornado() {
    sound = -1;
    Reset();   // inline
}

EffectTornado::~EffectTornado() {
    Reset();   // inline
    Reset();   // the virtual
}

void EffectTornado::Reset() {
    CEffect::Reset();
    if (source != nullptr) source->StopAndClear();
    if (sound != -1) g_clientWorld.sounds->StopSound(sound);
    sound = -1;
    source.Reset();
}

void EffectTornado::Launch(const Vector3& from, const Vector3& to) {
    double speed = x87::FromUnsigned(g_random.Next()) * kRandomScale * kSpeedRange + kSpeedMin;
    Vector3 way{static_cast<float>(to.x - from.x), static_cast<float>(to.y - from.y), static_cast<float>(to.z - from.z)};
    framesLeft = 0;
    double length = std::sqrt(LengthSquaredUnrounded<Sum3::ZYX, float>(way));
    if (length > 0.0) {
        double scale = speed / length;
        way.x = static_cast<float>(way.x * scale);
        way.y = static_cast<float>(way.y * scale);
        way.z = static_cast<float>(way.z * scale);
        float ticks = static_cast<float>(length / speed);
        long rounded = x87::RoundToInt(ticks);
        framesLeft = rounded < 0 ? 0 : static_cast<int>(rounded);
    }
    velocity = way;
    moving = effect::IsMoving(velocity);
    MarkChanged();
    localPosition = from;
    launchTick = WorldContext()->tick;
}

bool EffectTornado::RunTick() {
    if (moving != 0) {
        localPosition = Vector3{static_cast<float>(localPosition.x + velocity.x), static_cast<float>(localPosition.y + velocity.y),
                                 static_cast<float>(localPosition.z + velocity.z)};
        if (source != nullptr) source->localPosition = localPosition;
        if (sound != -1) g_clientWorld.sounds->SetSoundPosition(sound, &localPosition.x, kSoundNear, kSoundFar);
    }
    if (framesLeft <= kStopEmittingTicks && source != nullptr) source->Stop();
    CWorld* world = WorldContext();
    if (world->IsServer() && framesLeft > kHurtUntil && (framesLeft & (kHurtEvery - 1)) == 0) {
        std::list<CUnit*> units = ServerWorld()->aiMap.sectorMap.FindUnitsInRadius(localPosition, kHurtRadius);
        DamageInfo hit = TornadoHit();
        for (auto node = units.begin(); node != units.end();) {
            auto* unit = *node;
            ++node;
            if (unit->player != nullptr) unit->Controller()->TakeHit(&hit, nullptr, nullptr);
        }
        units.clear();
    }
    --framesLeft;
    return framesLeft >= 0;
}

int EffectTornado::Unpack(int update, DeltaField* field) {
    Vector3 position;
    field->ReadDelta(&position, 12);
    field->ReadDelta(&framesLeft, 4);
    field->ReadDelta(&velocity, 12);
    moving = effect::IsMoving(velocity);
    if (!SnapshotReadToEnd()) return 0;
    localPosition = position;
    Start(update);   // through the virtual
    return 1;
}

void EffectTornado::Pack(DeltaField* field) {
    Vector3 position = localPosition;
    field->WriteDelta(&position, 12);
    int ticks = framesLeft;
    field->WriteDelta(&ticks, 4);
    field->WriteDelta(&velocity, 12);
}

void EffectTornado::Start(int update) {
    if (update == 0 && source != nullptr) {
        source->Stop();
        source.Reset();
    }
    if (source != nullptr) return;
    if (sound == -1) {
        sound = g_clientWorld.sounds->Play3D(game::TornadoSoundText(), 0, &localPosition.x, kSoundNear, kSoundFar, 1);
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
