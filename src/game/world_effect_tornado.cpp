// CEffectTornado: a tornado crossing the map in a straight line, hurting the units it passes.
// See world_effect.h and docs/notes/effects.md ("Tornado").
#include "effects.h"
#include "world_effect.h"
#include "game_data.h"

#include "game_strings.h"
#include "map_object.h"
#include "originals_effects.h"
#include "random.h"
#include "sector_map.h"
#include "sound.h"
#include "unit.h"
#include "world.h"
#include "world_effect_internal.h"

#include "../lib/crt.h"
#include "ei/hook.h"
#include "ei/x87.h"

#include <cmath>
#include <cstring>

using ei::SetVtable;

namespace {

constexpr int kTornadoParticles = 0x200A;
constexpr float kSoundNear = 10.0f;            // 007433E4
constexpr float kSoundFar = 40.0f;             // 007433E8
// The speed per tick: a random word * 0073CCB0 * 0.12 (0073CCC0) + 0.096 (00743958).
constexpr double kRandomScale = 9.313226580990458e-11;
constexpr float kSpeedRange = 0.12f;
constexpr float kSpeedMin = 0.096f;
constexpr int kStopEmittingTicks = 110;        // the source stops emitting from here on
constexpr int kHurtUntil = 20;                 // no more hits for the last 20 ticks
constexpr int kHurtEvery = 8;
constexpr float kHurtRadius = 5.0f;
constexpr int kMaxCatchUp = 30;                // particle ticks a new source makes up for

// The hit every unit within kHurtRadius takes (the original builds it on the stack).
DamageInfo TornadoHit() {
    DamageInfo hit{};
    hit.typeWeights[6] = 1.0f;
    hit.attackRoll = 0x42C80000u;   // 100.0
    hit.amount = 5.0f;
    hit.multiplier = 1.0f;
    hit.location = 1;
    return hit;
}

// The snapshot read against was consumed to its end (stream slots 5 and 4, in that order).
bool SnapshotReadToEnd() {
    BitStream* snapshot = delta::ReadSnapshot();
    std::uint32_t position = snapshot->Position();
    return position == snapshot->Size();
}

CWorld* WorldOf(const CEngineObject* object) { return static_cast<CWorld*>(object->ManagerContext()); }

} // namespace

std::uintptr_t CEffectTornado::Vtable() { return ei::DataId(0x007438F8); }

CEffectTornado* CEffectTornado::Construct() {
    ConstructEffect();
    source_ = nullptr;
    SetVtable(this, Vtable());
    sound_ = -1;
    Reset();   // inline in the original
    return this;
}

void CEffectTornado::Destruct() {
    SetVtable(this, Vtable());
    Reset();   // inline in the original
    Reset();   // through slot 18
    if (source_ != nullptr) source_->Release();
    DestructEffect();
}

CEffectTornado* CEffectTornado::DeletingDestruct(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}

void CEffectTornado::Reset() {
    ResetBase();
    if (source_ != nullptr) Source()->StopAndClear();
    if (sound_ != -1) sound::Manager()->StopSound(sound_);
    sound_ = -1;
    CEngineObject* source = source_;
    source_ = nullptr;
    if (source != nullptr) source->Release();
}

void CEffectTornado::Launch(const Vector3* from, const Vector3* to) {
    double speed = x87::FromUnsigned(IsaacRandom::Global().Next()) * kRandomScale * kSpeedRange + kSpeedMin;
    Vector3 way{static_cast<float>(to->x - from->x), static_cast<float>(to->y - from->y), static_cast<float>(to->z - from->z)};
    framesLeft_ = 0;
    double length = x87::fsqrt(LengthSquaredUnrounded<Sum3::ZYX, float>(way));
    if (length > 0.0) {
        double scale = speed / length;
        way.x = static_cast<float>(way.x * scale);
        way.y = static_cast<float>(way.y * scale);
        way.z = static_cast<float>(way.z * scale);
        float ticks = static_cast<float>(length / speed);
        long rounded = x87::RoundToInt(ticks);   // FISTP
        framesLeft_ = rounded < 0 ? 0 : static_cast<int>(rounded);
    }
    velocity_ = way;
    moving_ = effect::IsMoving(velocity_);
    MarkChanged();
    localPosition_ = *from;
    launchTick_ = WorldOf(this)->tick_;
}

int CEffectTornado::Tick() {
    if (moving_ != 0) {
        localPosition_ = Vector3{static_cast<float>(localPosition_.x + velocity_.x), static_cast<float>(localPosition_.y + velocity_.y),
                                 static_cast<float>(localPosition_.z + velocity_.z)};
        if (source_ != nullptr) Source()->position = localPosition_;
        if (sound_ != -1) sound::Manager()->SetSoundPosition(sound_, &localPosition_.x, kSoundNear, kSoundFar);
    }
    if (framesLeft_ <= kStopEmittingTicks && source_ != nullptr) Source()->Stop();
    CWorld* world = WorldOf(this);
    if (world->IsServer() && framesLeft_ > kHurtUntil && (framesLeft_ & (kHurtEvery - 1)) == 0) {
        mfc::CTypedPtrList<CEngineObject> units{mfc::PtrListVtable(), nullptr, nullptr, 0, nullptr, nullptr, 10};
        static_cast<CWorldServer*>(WorldOf(this))->Sectors()->FindUnitsInRadius(&localPosition_, &units, kHurtRadius);
        DamageInfo hit = TornadoHit();
        for (auto* node = units.head; node != nullptr;) {
            auto* unit = static_cast<CUnit*>(node->data);
            node = node->next;
            if (unit->player != nullptr) ServerSlots(unit->controller)->TakeHitSlot(&hit, nullptr, nullptr);
        }
        units.RemoveAll();
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
    Start(update);   // through slot 23
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
        Source()->Stop();
        CEngineObject* source = source_;
        source_ = nullptr;
        if (source != nullptr) source->Release();
    }
    if (source_ != nullptr) return;
    if (sound_ == -1) {
        sound_ = sound::Manager()->Play3D(game::TornadoSoundText(), 0, &localPosition_.x, kSoundNear, kSoundFar, 1, 0);
    }
    auto* source =
        reinterpret_cast<CEngineObject*>(static_cast<CParticleManager*>(ParticleManagerObject())->CreateSource(kTornadoParticles));
    CEngineObject* old = source_;
    source_ = source;
    if (source != nullptr) ++source->refCount_;
    if (old != nullptr) old->Release();
    Source()->position = localPosition_;
    Source()->ScaleSize(1.0f);
    int behind = effect::GameTick() - launchTick_;
    if (behind >= kMaxCatchUp) behind = kMaxCatchUp;
    for (int i = 1; i < behind; ++i) source_->RunTick();
}

void CEffectTornado::LoadState(void* stream) {
    auto* s = static_cast<CStream*>(stream);
    s->Read(&localPosition_, 12);
    s->Read(&velocity_, 12);
    s->Read(&framesLeft_, 4);
    s->Read(&launchTick_, 4);
}

void CEffectTornado::SaveState(void* stream) {
    auto* s = static_cast<CStream*>(stream);
    s->Write(&localPosition_, 12);
    s->Write(&velocity_, 12);
    int copy = framesLeft_;
    s->Write(&copy, 4);
    copy = launchTick_;
    s->Write(&copy, 4);
}

EI_REPLACE_METHOD(0x0067D670, CEffectTornado::Construct);
EI_REPLACE_METHOD(0x0067D800, CEffectTornado::Destruct);
EI_REPLACE_METHOD(0x0067D7E0, CEffectTornado::DeletingDestruct);
EI_REPLACE_METHOD(0x0067D970, CEffectTornado::Reset);
EI_REPLACE_METHOD(0x0067DA50, CEffectTornado::Launch);
EI_REPLACE_METHOD(0x0067DBD0, CEffectTornado::Tick);
EI_REPLACE_METHOD(0x0067DDD0, CEffectTornado::Unpack);
EI_REPLACE_METHOD(0x0067DEB0, CEffectTornado::Pack);
EI_REPLACE_METHOD(0x0067DF10, CEffectTornado::Start);
EI_REPLACE_METHOD(0x0067E020, CEffectTornado::LoadState);
EI_REPLACE_METHOD(0x0067E070, CEffectTornado::SaveState);
