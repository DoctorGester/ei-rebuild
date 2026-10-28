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
    switch (type) {
    case 0x2002: case 0x2006: case 0x200B: case 0x200C: case 0x200D: case 0x200E: case 0x200F:
    case 0x2018: case 0x2019: case 0x201B: case 0x201C: case 0x201D: case 0x201E: case 0x201F:
    case 0x2020: case 0x2021: case 0x2022: case 0x2023: case 0x2024: case 0x2025: case 0x2026:
    case 0x2030: case 0x2031: case 0x2032: case 0x2033: case 0x203F: case 0x2040: case 0x2041:
    case 0x2042: case 0x2043:
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
        g_clientWorld->terrain_->RemoveLight(light_);
        CLight* old = light_;
        light_ = nullptr;
        if (old != nullptr) old->Release();
    }
    color_ = Vector3{0.0f, 0.0f, 0.0f};
    range_ = 0.0f;
    lightFlags_ = 0;
}

int CEffectPointLight::RunTick() {
    if (moving_ == 0) return 1;
    float x = localPosition_.x + velocity_.x;
    float y = localPosition_.y + velocity_.y;
    float z = localPosition_.z + velocity_.z;
    localPosition_.x = x;
    localPosition_.y = y;
    localPosition_.z = z;
    if (light_ == nullptr) return 1;
    Vector3 delta;
    std::memcpy(&delta, &velocity_, sizeof(Vector3));
    if ((FrameCounter() & 0xF) == 0) {
        // Every 16th frame the light is re-registered at its new place.
        g_clientWorld->terrain_->RemoveLight(light_);
        std::memcpy(&LightPosition(light_), &localPosition_, sizeof(Vector3));
        g_clientWorld->terrain_->AddLight(light_);
        return 1;
    }
    g_clientWorld->terrain_->MoveLight(light_, &delta);
    std::memcpy(&LightPosition(light_), &localPosition_, sizeof(Vector3));
    return 1;
}

void CEffectPointLight::Start(int update) {
    if (update == 0) {
        light_.Assign(g_clientWorld->lightFactory_->CreateLight(0x31));
        Vector3 position;
        std::memcpy(&position.x, &localPosition_.x, sizeof(float));
        std::memcpy(&position.y, &localPosition_.y, sizeof(float));
        std::memcpy(&position.z, &localPosition_.z, sizeof(float));
        Vector3 color;
        std::memcpy(&color.x, &color_.x, sizeof(float));
        std::memcpy(&color.y, &color_.y, sizeof(float));
        std::memcpy(&color.z, &color_.z, sizeof(float));
        light_->SetPoint(&color, &position);
        SetLightRange(light_, Bits(range_) & 0x7FFFFFFFu);
        LightFlags(light_) |= lightFlags_;
        g_clientWorld->terrain_->AddLight(light_);
        if (moving_ != 0) LightFlags(light_) |= 0x100;
    } else {
        std::memcpy(&light_->color_[0], &color_.x, sizeof(float));
        std::memcpy(&light_->color_[1], &color_.y, sizeof(float));
        std::memcpy(&light_->color_[2], &color_.z, sizeof(float));
        float range = FromBits(Bits(range_) & 0x7FFFFFFFu);
        float shownRange;
        std::memcpy(&shownRange, &light_->range_, 4);
        float change = FromBits(Bits(static_cast<float>(shownRange - range)) & 0x7FFFFFFFu);
        EI_NAN_AUDIT(change);
        if (change < ConstAt(worlddata::RangeEpsilon())) {
            // The range is (nearly) the same: move the light.
            Vector3& at = LightPosition(light_);
            Vector3 delta;
            delta.z = static_cast<float>(localPosition_.z - at.z);
            delta.y = static_cast<float>(localPosition_.y - at.y);
            delta.x = static_cast<float>(localPosition_.x - at.x);
            g_clientWorld->terrain_->MoveLight(light_, &delta);
            std::memcpy(&LightPosition(light_), &localPosition_, sizeof(Vector3));
        } else {
            g_clientWorld->terrain_->RemoveLight(light_);
            std::memcpy(&LightPosition(light_), &localPosition_, sizeof(Vector3));
            SetLightRange(light_, Bits(range_) & 0x7FFFFFFFu);
            g_clientWorld->terrain_->AddLight(light_);
        }
    }
    // A negative range (or NaN) sets flag 0x400.
    if (range_ >= ConstAt(worlddata::ZeroRef()))
        LightFlags(light_) &= ~0x400u;
    else
        LightFlags(light_) |= 0x400;
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
    Read(stream, &localPosition_, 12);
    Read(stream, &color_, 12);
    Read(stream, &range_, 4);
    Read(stream, &velocity_, 12);
    moving_ = IsMoving(velocity_);
    if (Version(stream) < 0x67) return;
    Read(stream, &lightFlags_, 4);
}

void CEffectPointLight::Save(CStream* stream) {
    Write(stream, &localPosition_, 12);
    Write(stream, &color_, 12);
    WriteCopy(stream, &range_);
    Write(stream, &velocity_, 12);
    WriteCopy(stream, &lightFlags_);
}

double CEffectPointLight::LightRadius() { return static_cast<double>(ConstAt(worlddata::DefaultRadius())) + range_; }


void CEffectParticle::Reset() {
    ClearBase(this);
    if (source_ != nullptr) {
        source_->Stop();
        CParticleSource* old = source_;
        source_ = nullptr;
        if (old != nullptr) old->Release();
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
        CParticleSource* old = source_;
        source_ = nullptr;
        if (old != nullptr) old->Release();
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
    if (GetClassId() == 0x4A && !SnapshotReadToEnd()) return 0;
    if (unitId_ != -1) {
        CUnit* unit = ObjectWorld(this)->FindUnit(unitId_);
        if (unit != nullptr) {
            Vector3 point;
            std::memcpy(&position, unit->EffectPoint(&point), sizeof(Vector3));
        }
    }
    localPosition_ = position;
    Start(update);
    return 1;
}

void CEffectParticle::Spawn() {
    if (source_ != nullptr) {
        // Already running: follow the effect.
        std::memcpy(&source_->localPosition_, &localPosition_, sizeof(Vector3));
        float baseSize;
        std::memcpy(&baseSize, &source_->baseSize, 4);
        source_->ScaleSize(static_cast<float>(size_ / baseSize));
        std::memcpy(&source_->alpha, &alpha_, sizeof(float));
        std::memcpy(&source_->typeParam, &typeParam_, sizeof(float));
        Attach();
        return;
    }

    source_.Assign(g_clientWorld->particleManager_->CreateSource(type_));
    std::memcpy(&source_->localPosition_, &localPosition_, sizeof(Vector3));
    float baseSize;
    std::memcpy(&baseSize, &source_->baseSize, 4);
    source_->ScaleSize(static_cast<float>(size_ / baseSize));
    source_->Attach(nullptr);
    std::memcpy(&source_->alpha, &alpha_, sizeof(float));
    std::memcpy(&source_->typeParam, &typeParam_, sizeof(float));
    Attach();

    // Catch up with the ticks since the effect began (at most 30 for lasting types).
    int age = g_clientWorld->tick_ - startTick_;
    if (!IsOneShotType(type_) && age >= 30) age = 30;
    if (age > 1) {
        for (int left = age - 1; left != 0; --left) source_->RunTick();
    }

    if (type_ == 0x2002) {
        float alpha = BelowOneOrOne(static_cast<float>(alpha_ * ConstAt(worlddata::ScorchAlpha())));
        float radius = typeParam_;
        float angle = RandomAngle(((&g_random))->Next());
        g_clientWorld->AddScorchMark(localPosition_.x, localPosition_.y, radius, radius, angle, alpha);
    }
    if (type_ == 0x2030) {
        float alpha = BelowOneOrOne(static_cast<float>(alpha_ * ConstAt(worlddata::ScorchAlpha())));
        std::uint32_t random = InlineRandom();
        float radius = size_;
        float angle = RandomAngle(random);
        g_clientWorld->AddScorchMark(localPosition_.x, localPosition_.y, radius, radius, angle, alpha);
        g_clientWorld->camera_->Shake(&localPosition_, size_, 2.0f, 0.25f);
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
    std::memcpy(&size_, &size, 4);
    MarkChanged();
}

void CEffectParticle::SetColors(float a, float b) {
    std::memcpy(&alpha_, &a, 4);
    std::memcpy(&typeParam_, &b, 4);
    MarkChanged();
}

void CEffectParticle::SetTarget(const Vector3* target) {
    std::memcpy(&target_, target, sizeof(Vector3));
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
    CObject3D* object = g_clientWorld->objectManager_->FindObjectAs<CObject3D>(static_cast<unsigned>(attachedTo_));
    Vector3 saved;
    std::memcpy(&saved, &source_->localPosition_, sizeof(Vector3));
    source_->Attach(object);
    if (object == nullptr) std::memcpy(&source_->localPosition_, &saved, sizeof(Vector3));
}

void CEffectParticle::Load(CStream* stream) {
    Read(stream, &localPosition_, 12);
    Read(stream, &type_, 4);
    Read(stream, &size_, 4);
    Read(stream, &attachedTo_, 4);
    Read(stream, &velocity_, 12);
    moving_ = IsMoving(velocity_);
    Read(stream, &startTick_, 4);
    Read(stream, &target_, 12);
    if (Version(stream) < 0x43) return;
    Read(stream, &unitId_, 4);
    if (Version(stream) < 0x58) return;
    Read(stream, &alpha_, 4);
    Read(stream, &typeParam_, 4);
}

void CEffectParticle::Save(CStream* stream) {
    Write(stream, &localPosition_, 12);
    Write(stream, &type_, 4);
    WriteCopy(stream, &size_);
    WriteCopy(stream, &attachedTo_);
    Write(stream, &velocity_, 12);
    WriteCopy(stream, &startTick_);
    Write(stream, &target_, 12);
    WriteCopy(stream, &unitId_);
    WriteCopy(stream, &alpha_);
    WriteCopy(stream, &typeParam_);
}

int CEffectParticle::RunTick() {
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
            std::memcpy(&localPosition_.z, &zBits, 4);
            if (source_ != nullptr) std::memcpy(&source_->localPosition_, &localPosition_, sizeof(Vector3));
        }
    }
    if (!IsOneShotType(type_)) return 1;
    int lifetime = type_ == 0x2019 ? 0x78 : 0x3C;
    int now = WorldOf(this)->tick_;
    return now > startTick_ + lifetime ? 0 : 1;
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
        CParticleSource* old = source_;
        source_ = nullptr;
        if (old != nullptr) old->Release();
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
    std::memcpy(&direction_, direction, sizeof(Vector3));
    MarkChanged();
}

int CEffectWall::Unpack(int update, DeltaField* field) {
    if (CEffectParticle::Unpack(update, field) == 0) return 0;
    field->ReadFloat16x3(&direction_.x);
    if (!SnapshotReadToEnd()) return 0;
    if (source_ != nullptr) std::memcpy(&source_->extent, &direction_, sizeof(Vector3));
    if (update == 0 && type_ == 0x2010) {
        // A scorch mark under the new wall: as long as the wall, as wide as half its size.
        float alpha = BelowOneOrOne(static_cast<float>(alpha_ * ConstAt(worlddata::ScorchAlpha())));
        float half = static_cast<float>(size_ * ConstAt(worlddata::HalfRef()));
        float shape[2];   // angle, outer
        WallScorchShape(&direction_, &half, shape);
        float inner = static_cast<float>(half + ConstAt(worlddata::ScorchMargin()));
        g_clientWorld->AddScorchMark(localPosition_.x, localPosition_.y, shape[1], inner, shape[0], alpha);
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
    Read(stream, &direction_, 12);
}

void CEffectWall::Save(CStream* stream) {
    CEffectParticle::Save(stream);
    Write(stream, &direction_, 12);
}


void CEffectLightning::Reset() {
    ClearBase(this);
    width_ = 1.0f;
    from_ = Vector3{0.0f, 0.0f, 0.0f};
    toObject_ = -1;
    fromObject_ = -1;
    to_ = Vector3{0.0f, 0.0f, 0.0f};
    CLightning* old = bolt_;
    bolt_ = nullptr;
    if (old != nullptr) old->Release();
    flag_ = 0;
}

void CEffectLightning::UpdateEnds() {
    CMapObject* target = WorldOf(this)->ObjectWithId(toObject_);
    if (target != nullptr) {
        std::memcpy(&to_, &target->localPosition_, sizeof(Vector3));
        to_.z = to_.z + ConstAt(worlddata::OneRef());
    }
    CUnit* source = UnitOrNull(WorldOf(this)->ObjectWithId(fromObject_));
    if (source != nullptr && ContextWorld(this)->IsServer() == 0) {
        Vector3 point;
        std::memcpy(&from_, source->EffectPoint(&point), sizeof(Vector3));
    }
}

void CEffectLightning::Start(int update) {
    if (update != 0) return;
    auto* created = g_clientWorld->listManager_->CreateObjectAs<CLightning>(0, -1);
    CLightning* old = bolt_;
    bolt_ = created;
    if (created != nullptr) ++created->refCount_;
    if (old != nullptr) old->Release();
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
    std::memcpy(&from_, from, sizeof(Vector3));
    std::memcpy(&to_, to, sizeof(Vector3));
    std::memcpy(&width_, &width, 4);
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
    Read(stream, &localPosition_, 12);
    Read(stream, &from_, 12);
    Read(stream, &to_, 12);
    Read(stream, &width_, 4);
    Read(stream, &toObject_, 4);
    Read(stream, &fromObject_, 4);
    if (Version(stream) < 100) return;
    Read(stream, &flag_, 1);
}

void CEffectLightning::Save(CStream* stream) {
    Write(stream, &localPosition_, 12);
    Write(stream, &from_, 12);
    Write(stream, &to_, 12);
    WriteCopy(stream, &width_);
    WriteCopy(stream, &toObject_);
    WriteCopy(stream, &fromObject_);
    Write(stream, &flag_, 1);
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

int CEffectLightning::RunTick() {
    if (bolt_ == nullptr || (toObject_ == -1 && fromObject_ == -1)) return 1;
    UpdateEnds();
    bolt_->SetEndpoints(&from_, &to_);
    return 1;
}
