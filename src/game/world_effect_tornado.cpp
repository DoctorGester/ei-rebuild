// CEffectTornado: a tornado crossing the map in a straight line, hurting the units it passes.
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

constexpr int kTornadoParticles = 0x200A;
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
    CMemoryStream* snapshot = delta::g_readSnapshot;
    std::uint32_t position = snapshot->Position();
    return position == snapshot->Size();
}

} // namespace

CEffectTornado::CEffectTornado() {
    sound_ = -1;
    Reset();   // inline
}

CEffectTornado::~CEffectTornado() {
    Reset();   // inline
    Reset();   // the virtual
}

void CEffectTornado::Reset() {
    CEffect::Reset();
    if (source_ != nullptr) source_->StopAndClear();
    if (sound_ != -1) g_clientWorld->sounds_->StopSound(sound_);
    sound_ = -1;
    source_.Reset();
}

void CEffectTornado::Launch(const Vector3* from, const Vector3* to) {
    double speed = x87::FromUnsigned(g_random.Next()) * kRandomScale * kSpeedRange + kSpeedMin;
    Vector3 way{static_cast<float>(to->x - from->x), static_cast<float>(to->y - from->y), static_cast<float>(to->z - from->z)};
    framesLeft_ = 0;
    double length = std::sqrt(LengthSquaredUnrounded<Sum3::ZYX, float>(way));
    if (length > 0.0) {
        double scale = speed / length;
        way.x = static_cast<float>(way.x * scale);
        way.y = static_cast<float>(way.y * scale);
        way.z = static_cast<float>(way.z * scale);
        float ticks = static_cast<float>(length / speed);
        long rounded = x87::RoundToInt(ticks);
        framesLeft_ = rounded < 0 ? 0 : static_cast<int>(rounded);
    }
    velocity_ = way;
    moving_ = effect::IsMoving(velocity_);
    MarkChanged();
    localPosition_ = *from;
    launchTick_ = effect::ContextWorld(this)->tick_;
}

int CEffectTornado::RunTick() {
    if (moving_ != 0) {
        localPosition_ = Vector3{static_cast<float>(localPosition_.x + velocity_.x), static_cast<float>(localPosition_.y + velocity_.y),
                                 static_cast<float>(localPosition_.z + velocity_.z)};
        if (source_ != nullptr) source_->localPosition_ = localPosition_;
        if (sound_ != -1) g_clientWorld->sounds_->SetSoundPosition(sound_, &localPosition_.x, kSoundNear, kSoundFar);
    }
    if (framesLeft_ <= kStopEmittingTicks && source_ != nullptr) source_->Stop();
    CWorld* world = effect::ContextWorld(this);
    if (world->IsServer() && framesLeft_ > kHurtUntil && (framesLeft_ & (kHurtEvery - 1)) == 0) {
        std::list<CUnit*> units{};
        ServerWorld()->aiMap_.sectorMap.FindUnitsInRadius(&localPosition_, &units, kHurtRadius);
        DamageInfo hit = TornadoHit();
        for (auto node = units.begin(); node != units.end();) {
            auto* unit = *node;
            ++node;
            if (unit->player != nullptr) unit->Controller()->TakeHit(&hit, nullptr, nullptr);
        }
        units.clear();
    }
    --framesLeft_;
    return framesLeft_ >= 0 ? 1 : 0;
}

int CEffectTornado::Unpack(int update, DeltaField* field) {
    Vector3 position;
    field->ReadDelta(&position, 12);
    field->ReadDelta(&framesLeft_, 4);
    field->ReadDelta(&velocity_, 12);
    moving_ = effect::IsMoving(velocity_);
    if (!SnapshotReadToEnd()) return 0;
    localPosition_ = position;
    Start(update);   // through the virtual
    return 1;
}

void CEffectTornado::Pack(DeltaField* field) {
    Vector3 position = localPosition_;
    field->WriteDelta(&position, 12);
    int ticks = framesLeft_;
    field->WriteDelta(&ticks, 4);
    field->WriteDelta(&velocity_, 12);
}

void CEffectTornado::Start(int update) {
    if (update == 0 && source_ != nullptr) {
        source_->Stop();
        source_.Reset();
    }
    if (source_ != nullptr) return;
    if (sound_ == -1) {
        sound_ = g_clientWorld->sounds_->Play3D(game::TornadoSoundText(), 0, &localPosition_.x, kSoundNear, kSoundFar, 1, 0);
    }
    CParticleSource* source = g_clientWorld->particleManager_->CreateSource(kTornadoParticles);
    source_.Assign(source);
    source_->localPosition_ = localPosition_;
    source_->ScaleSize(1.0f);
    int behind = g_clientWorld->tick_ - launchTick_;
    if (behind >= kMaxCatchUp) behind = kMaxCatchUp;
    for (int i = 1; i < behind; ++i) source_->RunTick();
}

void CEffectTornado::Load(CStream* s) {
    s->ReadValue(localPosition_);
    s->ReadValue(velocity_);
    s->ReadValue(framesLeft_);
    s->ReadValue(launchTick_);
}

void CEffectTornado::Save(CStream* s) {
    s->WriteValue(localPosition_);
    s->WriteValue(velocity_);
    int copy = framesLeft_;
    s->WriteValue(copy);
    copy = launchTick_;
    s->WriteValue(copy);
}
