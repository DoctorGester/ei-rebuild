// The visible effects: CEffectPointLight, CEffectParticle, CEffectWall, CEffectLightning. See
// world_effect.h and docs/notes/effects.md.

#include "camera.h"
#include "random.h"
#include "effects.h"
#include "lightning.h"
#include "net_delta.h"
#include "script.h"
#include "spell.h"
#include "terrain.h"
#include "texture.h"
#include "unit.h"
#include "world.h"
#include "world_effect_internal.h"
#include "ei/x87.h"
#include "game_singletons.h"
#include "lib/nan_audit.h"
#include "save_versions.h"

#include <bit>
#include <cstring>

using namespace effect;

namespace {



void ClearBase(CEffect* e) {
    e->localPosition_ = Vector3{0.0f, 0.0f, 0.0f};
    EmptyField(&e->state_);
    e->velocity_ = Vector3{0.0f, 0.0f, 0.0f};
    e->startTick_ = 0;
    e->moving_ = 0;
    e->changeStamp_ = -1;
}

// The light's range fields: range, range^2 and 1 / range^2 with the square left unrounded for the division.
void SetLightRange(CLight* light, std::uint32_t rangeBits) {
    light->range_ = std::bit_cast<float>(rangeBits);
    const double square = static_cast<double>(light->range_) * light->range_;
    light->rangeSquared_ = static_cast<float>(square);
    light->inverseRangeSquared_ = static_cast<float>(1.0 / square);
}

std::uint32_t& LightFlags(CLight* light) { return light->flags_; }
Vector3& LightPosition(CLight* light) { return light->localPosition_; }

// The particle tick's step towards the target, in x87 order: speed = |velocity|
// ((x*x + y*y) + z*z), dist = sqrt(dy*dy + dx*dx); nothing when dist is not above 0 (or NaN);
// otherwise t = speed / (dist < speed (or NaN) ? speed : dist), rounded to float.
int StepFraction(const Vector3* velocity, const Vector3* dxdy, float* t) {
    const double speed = std::sqrt(LengthSquaredUnrounded<Sum3::XYZ, double>(*velocity));
    const double dist = std::sqrt(LengthSquared2DUnrounded<double>(dxdy->y, dxdy->x));
    // On only when above.
    EI_NAN_AUDIT(dist);
    if (dist <= 0.0f) return 0;
    // The speed when the distance is below it or they are unordered.
    const double divisor = dist >= speed ? dist : speed;
    *t = static_cast<float>(speed / divisor);
    return 1;
}

// Particle types that play once (no warm-up limit, a lifetime of 60 ticks; 0x2019: 120).
bool IsOneShotType(int type) {
    switch (ParticleTypeOf(type)) {
    case ParticleType::FireBlast: case ParticleType::Healing: case ParticleType::Casting: case ParticleType::Nuke: case ParticleType::Banshee: case ParticleType::Mushroom: case ParticleType::Shower1:
    case ParticleType::ClayRing: case ParticleType::Teleport: case ParticleType::Modifier1: case ParticleType::Modifier2: case ParticleType::Modifier3: case ParticleType::Modifier4: case ParticleType::Modifier5:
    case ParticleType::Modifier6: case ParticleType::Modifier7: case ParticleType::Modifier8: case ParticleType::Modifier9: case ParticleType::Modifier10: case ParticleType::Modifier11: case ParticleType::Modifier12:
    case ParticleType::LightningBlast: case ParticleType::Shower2: case ParticleType::Shower3: case ParticleType::Shower4: case ParticleType::Cylinder1: case ParticleType::Cylinder2: case ParticleType::FireStar:
    case ParticleType::AcidStar: case ParticleType::Sparks:
        return true;
    default:
        return false;
    }
}

// The scorch mark's angle: a random value (exact) times a constant in double, rounded to float.
float RandomAngle(std::uint32_t random) {
    return static_cast<float>(x87::FromUnsigned(random) * 1.4668330997698233e-9);
}

// The ISAAC Next the 0x2030 scorch inlines.
std::uint32_t InlineRandom() {
    IsaacRandom& random = g_random;
    std::uint32_t index = random.count_--;
    if (index == 0) {
        random.Refill();
        random.count_ = 0xFF;
    }
    return random.results_[random.count_];
}

// The fire wall's scorch mark: angle = atan2(direction.y, direction.x) and outer =
// sqrt(x*x + y*y) + half + 0.3, both rounded to float only when stored, in x87 order.
void WallScorchShape(const Vector3* direction, const float* half, float* angleAndOuter) {
    angleAndOuter[0] = static_cast<float>(x87::fpatan(direction->y, direction->x));
    const double outer = std::sqrt(LengthSquared2DUnrounded<double>(direction->x, direction->y));
    angleAndOuter[1] = static_cast<float>((outer + *half) + 0.3f);
}

} // namespace


void CEffectPointLight::Reset() {
    ClearBase(this);
    if (light_ != nullptr) {
        g_clientWorld.terrain_->RemoveLight(light_);
        light_.Reset();
    }
    color_ = Vector3{0.0f, 0.0f, 0.0f};
    range_ = 0.0f;
    lightFlags_ = 0;
}

bool CEffectPointLight::RunTick() {
    if (moving_ == 0) return true;
    float x = localPosition_.x + velocity_.x;
    float y = localPosition_.y + velocity_.y;
    float z = localPosition_.z + velocity_.z;
    localPosition_.x = x;
    localPosition_.y = y;
    localPosition_.z = z;
    if (light_ == nullptr) return true;
    Vector3 delta;
    delta = velocity_;
    if ((FrameCounter() & 0xF) == 0) {
        // Every 16th frame the light is re-registered at its new place.
        g_clientWorld.terrain_->RemoveLight(light_);
        LightPosition(light_) = localPosition_;
        g_clientWorld.terrain_->AddLight(light_);
        return true;
    }
    g_clientWorld.terrain_->MoveLight(light_, &delta);
    LightPosition(light_) = localPosition_;
    return true;
}

void CEffectPointLight::Start(int update) {
    if (update == 0) {
        light_.Assign(g_clientWorld.lightFactory_->CreateLight(0x31));
        Vector3 position;
        position.x = localPosition_.x;
        position.y = localPosition_.y;
        position.z = localPosition_.z;
        Vector3 color;
        color.x = color_.x;
        color.y = color_.y;
        color.z = color_.z;
        light_->SetPoint(&color, &position);
        SetLightRange(light_, Bits(range_) & 0x7FFFFFFFu);
        LightFlags(light_) |= lightFlags_;
        g_clientWorld.terrain_->AddLight(light_);
        if (moving_ != 0) LightFlags(light_) |= LightFlag::Moving;
    } else {
        light_->color_[0] = color_.x;
        light_->color_[1] = color_.y;
        light_->color_[2] = color_.z;
        float range = FromBits(Bits(range_) & 0x7FFFFFFFu);
        float shownRange;
        shownRange = light_->range_;
        float change = FromBits(Bits(static_cast<float>(shownRange - range)) & 0x7FFFFFFFu);
        EI_NAN_AUDIT(change);
        if (change < ConstAt(worlddata::RangeEpsilon())) {
            // The range is (nearly) the same: move the light.
            Vector3& at = LightPosition(light_);
            Vector3 delta;
            delta.z = static_cast<float>(localPosition_.z - at.z);
            delta.y = static_cast<float>(localPosition_.y - at.y);
            delta.x = static_cast<float>(localPosition_.x - at.x);
            g_clientWorld.terrain_->MoveLight(light_, &delta);
            LightPosition(light_) = localPosition_;
        } else {
            g_clientWorld.terrain_->RemoveLight(light_);
            LightPosition(light_) = localPosition_;
            SetLightRange(light_, Bits(range_) & 0x7FFFFFFFu);
            g_clientWorld.terrain_->AddLight(light_);
        }
    }
    // A negative range (or NaN) sets LightFlag::Off.
    if (range_ >= ConstAt(worlddata::ZeroRef()))
        LightFlags(light_) &= ~LightFlag::Off;
    else
        LightFlags(light_) |= LightFlag::Off;
}

int CEffectPointLight::Unpack(int update, DeltaField* field) {
    Vector3 position;
    field->ReadPosition(&position);
    field->ReadFloat16x3(&color_.x);
    field->ReadFloat16(&range_);
    field->ReadFloat16x3(&velocity_.x);
    field->ReadDelta(&lightFlags_, 4);
    moving_ = IsMoving(velocity_);
    if (!SnapshotReadToEnd()) return 0;
    localPosition_ = position;
    Start(update);
    return 1;
}

void CEffectPointLight::Pack(DeltaField* field) {
    Vector3 position = localPosition_;
    field->WritePosition(&position);
    field->WriteFloat16x3(&color_.x);
    field->WriteFloat16(Bits(range_));
    field->WriteFloat16x3(&velocity_.x);
    field->WriteDelta(&lightFlags_, 4);
}

void CEffectPointLight::Load(CStream* stream) {
    stream->ReadValue(localPosition_);
    stream->ReadValue(color_);
    stream->ReadValue(range_);
    stream->ReadValue(velocity_);
    moving_ = IsMoving(velocity_);
    if (Version(stream) < kVersionLightFlags) return;
    stream->ReadValue(lightFlags_);
}

void CEffectPointLight::Save(CStream* stream) {
    stream->WriteValue(localPosition_);
    stream->WriteValue(color_);
    stream->WriteValue(range_);
    stream->WriteValue(velocity_);
    stream->WriteValue(lightFlags_);
}

double CEffectPointLight::LightRadius() { return static_cast<double>(ConstAt(worlddata::DefaultRadius())) + range_; }


void CEffectParticle::Reset() {
    ClearBase(this);
    if (source_ != nullptr) {
        source_->Stop();
        source_.Reset();
    }
    size_ = 1.0f;
    alpha_ = 1.0f;
    typeParam_ = 1.0f;
    target_ = Vector3{0.0f, 0.0f, 0.0f};
    type_ = 0;
    attachedTo_ = -1;
    unitId_ = -1;
}

Vector3* CEffectParticle::Position(const ScriptGroup* group) {
    if (attachedTo_ == -1) return &localPosition_;
    CMapObject* object = ContextWorld(this)->ObjectWithId(attachedTo_);
    if (object == nullptr) return &localPosition_;
    if (group->Contains(object) == 0) {
        // Not in the group: far away.
        worldPosition_.x = FromBits(0xC9742400u);   // -1e6
        worldPosition_.y = FromBits(0xC9742400u);
        worldPosition_.z = 0.0f;
        return &worldPosition_;
    }
    const Vector3& at = object->localPosition_;
    float x = localPosition_.x + at.x;
    float y = at.y + localPosition_.y;
    float z = at.z + localPosition_.z;
    worldPosition_.x = x;
    worldPosition_.y = y;
    worldPosition_.z = z;
    return &worldPosition_;
}

void CEffectParticle::Start(int update) {
    if (update == 0 && source_ != nullptr) {
        source_->Stop();
        source_.Reset();
    }
    Spawn();
}

int CEffectParticle::Unpack(int update, DeltaField* field) {
    Vector3 position;
    field->ReadPosition(&position);
    field->ReadDelta(&startTick_, 4);
    field->ReadDelta(&type_, 4);
    field->ReadFloat16(&size_);
    field->ReadDelta(&attachedTo_, 4);
    field->ReadFloat16x3(&velocity_.x);
    field->ReadPosition(&target_);
    field->ReadDelta(&unitId_, 4);
    field->ReadFloat16(&alpha_);
    field->ReadFloat16(&typeParam_);
    moving_ = IsMoving(velocity_);
    // Only a plain particle effect checks the snapshot here (the wall checks after its own field).
    if (GetClassId() == ClassId::EffectParticle && !SnapshotReadToEnd()) return 0;
    if (unitId_ != -1) {
        CUnit* unit = World()->FindUnit(unitId_);
        if (unit != nullptr) {
            Vector3 point;
            position = *unit->EffectPoint(&point);
        }
    }
    localPosition_ = position;
    Start(update);
    return 1;
}

void CEffectParticle::Spawn() {
    if (source_ != nullptr) {
        // Already running: follow the effect.
        source_->localPosition_ = localPosition_;
        float baseSize;
        baseSize = source_->baseSize;
        source_->ScaleSize(static_cast<float>(size_ / baseSize));
        source_->alpha = alpha_;
        source_->typeParam = typeParam_;
        Attach();
        return;
    }

    source_.Assign(g_clientWorld.particleManager_->CreateSource(type_));
    source_->localPosition_ = localPosition_;
    float baseSize;
    baseSize = source_->baseSize;
    source_->ScaleSize(static_cast<float>(size_ / baseSize));
    source_->Attach(nullptr);
    source_->alpha = alpha_;
    source_->typeParam = typeParam_;
    Attach();

    // Catch up with the ticks since the effect began (at most 30 for lasting types).
    int age = g_clientWorld.tick_ - startTick_;
    if (!IsOneShotType(type_) && age >= 30) age = 30;
    if (age > 1) {
        for (int left = age - 1; left != 0; --left) source_->RunTick();
    }

    if (ParticleTypeOf(type_) == ParticleType::FireBlast) {
        float alpha = BelowOneOrOne(static_cast<float>(alpha_ * ConstAt(worlddata::ScorchAlpha())));
        float radius = typeParam_;
        float angle = RandomAngle(((&g_random))->Next());
        g_clientWorld.AddScorchMark(localPosition_.x, localPosition_.y, radius, radius, angle, alpha);
    }
    if (ParticleTypeOf(type_) == ParticleType::LightningBlast) {
        float alpha = BelowOneOrOne(static_cast<float>(alpha_ * ConstAt(worlddata::ScorchAlpha())));
        std::uint32_t random = InlineRandom();
        float radius = size_;
        float angle = RandomAngle(random);
        g_clientWorld.AddScorchMark(localPosition_.x, localPosition_.y, radius, radius, angle, alpha);
        g_clientWorld.camera_->Shake(&localPosition_, size_, 2.0f, 0.25f);
    }
}

void CEffectParticle::Pack(DeltaField* field) {
    Vector3 position = localPosition_;
    field->WritePosition(&position);
    int value = startTick_;
    field->WriteDelta(&value, 4);
    field->WriteDelta(&type_, 4);
    field->WriteFloat16(Bits(size_));
    value = attachedTo_;
    field->WriteDelta(&value, 4);
    field->WriteFloat16x3(&velocity_.x);
    field->WritePosition(&target_);
    value = unitId_;
    field->WriteDelta(&value, 4);
    field->WriteFloat16(Bits(alpha_));
    field->WriteFloat16(Bits(typeParam_));
}

void CEffectParticle::SetAttachedTo(int objectId) {
    attachedTo_ = objectId;
    MarkChanged();
}

void CEffectParticle::SetSize(float size) {
    size_ = size;
    MarkChanged();
}

void CEffectParticle::SetColors(float a, float b) {
    alpha_ = a;
    typeParam_ = b;
    MarkChanged();
}

void CEffectParticle::SetTarget(const Vector3* target) {
    target_ = *target;
    MarkChanged();
}

void CEffectParticle::Attach() {
    // Nothing to do when the source already follows that object, or follows none and none is set.
    // Without a source this goes on and reads through the null pointer, like the original.
    if (source_ != nullptr) {
        CEngineObject* followed = source_->attachedTo;
        if (followed != nullptr) {
            if (followed->id_ == attachedTo_) return;
        } else if (attachedTo_ == -1) {
            return;
        }
    }
    CObject3D* object = g_clientWorld.objectManager_->FindObjectAs<CObject3D>(static_cast<unsigned>(attachedTo_));
    Vector3 saved;
    saved = source_->localPosition_;
    source_->Attach(object);
    if (object == nullptr) source_->localPosition_ = saved;
}

void CEffectParticle::Load(CStream* stream) {
    stream->ReadValue(localPosition_);
    stream->ReadValue(type_);
    stream->ReadValue(size_);
    stream->ReadValue(attachedTo_);
    stream->ReadValue(velocity_);
    moving_ = IsMoving(velocity_);
    stream->ReadValue(startTick_);
    stream->ReadValue(target_);
    if (Version(stream) < kVersionEffectUnitId) return;
    stream->ReadValue(unitId_);
    if (Version(stream) < kVersionEffectAlpha) return;
    stream->ReadValue(alpha_);
    stream->ReadValue(typeParam_);
}

void CEffectParticle::Save(CStream* stream) {
    stream->WriteValue(localPosition_);
    stream->WriteValue(type_);
    stream->WriteValue(size_);
    stream->WriteValue(attachedTo_);
    stream->WriteValue(velocity_);
    stream->WriteValue(startTick_);
    stream->WriteValue(target_);
    stream->WriteValue(unitId_);
    stream->WriteValue(alpha_);
    stream->WriteValue(typeParam_);
}

bool CEffectParticle::RunTick() {
    unitId_ = -1;
    if (moving_ != 0) {
        Vector3 d;
        d.x = static_cast<float>(target_.x - localPosition_.x);
        d.y = static_cast<float>(target_.y - localPosition_.y);
        d.z = static_cast<float>(target_.z - localPosition_.z);
        float t;
        if (StepFraction(&velocity_, &d, &t) != 0) {
            // Keeps the target's height above the ground while moving towards it.
            float aboveGround = static_cast<float>(target_.z - WorldOf(this)->RawSurfaceHeightAt(target_.x, target_.y));
            float stepY = static_cast<float>(t * d.y);
            float stepZ = static_cast<float>(d.z * t);
            float x = static_cast<float>(t * d.x + localPosition_.x);
            float y = static_cast<float>(stepY + localPosition_.y);
            float z = static_cast<float>(stepZ + localPosition_.z);
            float floor = static_cast<float>(WorldOf(this)->RawSurfaceHeightAt(x, y) + aboveGround);
            std::uint32_t zBits = z >= floor ? Bits(z) : Bits(floor);
            localPosition_.x = x;
            localPosition_.y = y;
            localPosition_.z = std::bit_cast<float>(zBits);
            if (source_ != nullptr) source_->localPosition_ = localPosition_;
        }
    }
    if (!IsOneShotType(type_)) return true;
    int lifetime = ParticleTypeOf(type_) == ParticleType::Teleport ? 0x78 : 0x3C;
    int now = WorldOf(this)->tick_;
    return now <= startTick_ + lifetime;
}


void CEffectWall::Reset() {
    localPosition_ = Vector3{0.0f, 0.0f, 0.0f};
    EmptyField(&state_);
    startTick_ = 0;
    velocity_ = Vector3{0.0f, 0.0f, 0.0f};
    moving_ = 0;
    changeStamp_ = -1;
    if (source_ != nullptr) {
        source_->Stop();
        source_.Reset();
    }
    size_ = 1.0f;
    alpha_ = 1.0f;
    typeParam_ = 1.0f;
    attachedTo_ = -1;
    target_ = Vector3{0.0f, 0.0f, 0.0f};
    type_ = 0;
    unitId_ = -1;
    direction_ = Vector3{0.0f, 0.0f, 0.0f};
}

void CEffectWall::SetDirection(const Vector3* direction) {
    direction_ = *direction;
    MarkChanged();
}

int CEffectWall::Unpack(int update, DeltaField* field) {
    if (CEffectParticle::Unpack(update, field) == 0) return 0;
    field->ReadFloat16x3(&direction_.x);
    if (!SnapshotReadToEnd()) return 0;
    if (source_ != nullptr) source_->extent = direction_;
    if (update == 0 && ParticleTypeOf(type_) == ParticleType::Tendril) {
        // A scorch mark under the new wall: as long as the wall, as wide as half its size.
        float alpha = BelowOneOrOne(static_cast<float>(alpha_ * ConstAt(worlddata::ScorchAlpha())));
        float half = static_cast<float>(size_ * ConstAt(worlddata::HalfRef()));
        float shape[2];   // angle, outer
        WallScorchShape(&direction_, &half, shape);
        float inner = static_cast<float>(half + ConstAt(worlddata::ScorchMargin()));
        g_clientWorld.AddScorchMark(localPosition_.x, localPosition_.y, shape[1], inner, shape[0], alpha);
    }
    return 1;
}

void CEffectWall::Pack(DeltaField* field) {
    Vector3 position = localPosition_;
    field->WritePosition(&position);
    int value = startTick_;
    field->WriteDelta(&value, 4);
    field->WriteDelta(&type_, 4);
    field->WriteFloat16(Bits(size_));
    value = attachedTo_;
    field->WriteDelta(&value, 4);
    field->WriteFloat16x3(&velocity_.x);
    field->WritePosition(&target_);
    value = unitId_;
    field->WriteDelta(&value, 4);
    field->WriteFloat16(Bits(alpha_));
    field->WriteFloat16(Bits(typeParam_));
    field->WriteFloat16x3(&direction_.x);
}

void CEffectWall::Load(CStream* stream) {
    CEffectParticle::Load(stream);
    stream->ReadValue(direction_);
}

void CEffectWall::Save(CStream* stream) {
    CEffectParticle::Save(stream);
    stream->WriteValue(direction_);
}


void CEffectLightning::Reset() {
    ClearBase(this);
    width_ = 1.0f;
    from_ = Vector3{0.0f, 0.0f, 0.0f};
    toObject_ = -1;
    fromObject_ = -1;
    to_ = Vector3{0.0f, 0.0f, 0.0f};
    bolt_.Reset();
    flag_ = 0;
}

void CEffectLightning::UpdateEnds() {
    CMapObject* target = WorldOf(this)->ObjectWithId(toObject_);
    if (target != nullptr) {
        to_ = target->localPosition_;
        to_.z = to_.z + ConstAt(worlddata::OneRef());
    }
    CUnit* source = UnitOrNull(WorldOf(this)->ObjectWithId(fromObject_));
    if (source != nullptr && ContextWorld(this)->IsServer() == 0) {
        Vector3 point;
        from_ = *source->EffectPoint(&point);
    }
}

void CEffectLightning::Start(int update) {
    if (update != 0) return;
    auto* created = g_clientWorld.listManager_->CreateObjectAs<CLightning>(0, -1);
    bolt_.Assign(created);
    UpdateEnds();
    bolt_->Start(&from_, &to_, width_, flag_);
}

int CEffectLightning::Unpack(int update, DeltaField* field) {
    Vector3 position;
    field->ReadPosition(&position);
    field->ReadPosition(&from_);
    field->ReadPosition(&to_);
    field->ReadFloat16(&width_);
    field->ReadDelta(&flag_, 1);
    field->ReadDelta(&toObject_, 4);
    field->ReadDelta(&fromObject_, 4);
    if (!SnapshotReadToEnd()) return 0;
    UpdateEnds();
    localPosition_ = position;
    Start(update);
    return 1;
}

void CEffectLightning::Pack(DeltaField* field) {
    Vector3 position = localPosition_;
    field->WritePosition(&position);
    field->WritePosition(&from_);
    field->WritePosition(&to_);
    field->WriteFloat16(Bits(width_));
    field->WriteDelta(&flag_, 1);
    int value = toObject_;
    field->WriteDelta(&value, 4);
    value = fromObject_;
    field->WriteDelta(&value, 4);
}

void CEffectLightning::SetEnds(const Vector3* from, const Vector3* to, float width) {
    from_ = *from;
    to_ = *to;
    width_ = width;
    // The midpoint: the x sum stays on the FPU, the y and z sums are rounded first.
    float half = ConstAt(worlddata::HalfRef());
    float sumY = static_cast<float>(from_.y + to_.y);
    float sumZ = static_cast<float>(from_.z + to_.z);
    localPosition_.x = static_cast<float>((to_.x + from_.x) * half);
    localPosition_.y = static_cast<float>(sumY * half);
    localPosition_.z = static_cast<float>(sumZ * half);
    MarkChanged();
}

void CEffectLightning::Load(CStream* stream) {
    stream->ReadValue(localPosition_);
    stream->ReadValue(from_);
    stream->ReadValue(to_);
    stream->ReadValue(width_);
    stream->ReadValue(toObject_);
    stream->ReadValue(fromObject_);
    if (Version(stream) < 100) return;
    stream->ReadValue(flag_);
}

void CEffectLightning::Save(CStream* stream) {
    stream->WriteValue(localPosition_);
    stream->WriteValue(from_);
    stream->WriteValue(to_);
    stream->WriteValue(width_);
    stream->WriteValue(toObject_);
    stream->WriteValue(fromObject_);
    stream->WriteValue(flag_);
}

void CEffectLightning::SetObjects(int fromObject, int toObject) {
    toObject_ = toObject;
    fromObject_ = fromObject;
    MarkChanged();
}

void CEffectLightning::SetFlag(std::uint8_t flag) {
    flag_ = flag;
    MarkChanged();
}

bool CEffectLightning::RunTick() {
    if (bolt_ == nullptr || (toObject_ == -1 && fromObject_ == -1)) return true;
    UpdateEnds();
    bolt_->SetEndpoints(&from_, &to_);
    return true;
}
