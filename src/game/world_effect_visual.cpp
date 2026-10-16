// The visible effects: CEffectPointLight, CEffectParticle, CEffectWall, CEffectLightning. See
// world_effect.h and docs/notes/effects.md.

#include "world_effect_internal.h"
#include "originals_effects.h"
#include "originals_engine.h"
#include "originals_units.h"
#include "ei/x87.h"

#include <bit>
#include <cstring>

using namespace effect;

namespace {

// The light manager (*007AF738), thiscall.
void __fastcall MoveLight(void* a0, void*, CEngineObject* a2, const Vector3* a3) { LightMoveOriginal(a0, nullptr, a2, (const float*)a3); }
// 00511B90 / 0056F1A0: reference-counted pointer assignment (ECX = the pointer).
void __fastcall AssignLight(CLight** a0, void*, void* a2) { CountedRefStoreOriginal((void*)a0, nullptr, a2); }
__declspec(naked) void __fastcall AssignSource(CParticleSource** /*slot*/, void*, void* /*object*/) EI_ORIGINAL(0x0056F1A0)

// CParticleSource (effects.cpp, through the game addresses).
inline void* __fastcall CreateSource(void* manager, void*, int type) { return ParticleCreateSourceOriginal(manager, nullptr, type); }
__declspec(naked) void __fastcall ScaleSource(void* /*source*/, void*, float /*factor*/) EI_ORIGINAL(0x00485480)
__declspec(naked) void __fastcall StopSource(void* /*source*/) EI_ORIGINAL(0x00485360)
// 0056D4C0 (world client 007AF5B8): a scorch mark on the ground. All arguments are float bits.
__declspec(naked) void __fastcall AddScorch(void*, void*, std::uint32_t /*x*/, std::uint32_t /*y*/, std::uint32_t /*outer*/, std::uint32_t /*inner*/, std::uint32_t /*angle*/, std::uint32_t /*alpha*/) EI_ORIGINAL(0x0056D4C0)
// 004113A0 (camera.cpp): a camera shake from an event at `at`.
// The global ISAAC generator (007A9D40): 004667F0 Next, 0049EE70 Refill.
__declspec(naked) void __fastcall RandomRefill(void* /*random*/) EI_ORIGINAL(0x0049EE70)
void* TheRandom() { return reinterpret_cast<void*>(0x007A9D40); }

// The drawn lightning bolt: 004680D0 starts it, 00467E10 moves its ends.
__declspec(naked) void __fastcall StartBolt(CEngineObject* /*bolt*/, void*, const Vector3* /*from*/, const Vector3* /*to*/, std::uint32_t /*width*/, std::uint32_t /*flag*/) EI_ORIGINAL(0x004680D0)
__declspec(naked) void __fastcall MoveBolt(CEngineObject* /*bolt*/, void*, const Vector3* /*from*/, const Vector3* /*to*/)
    EI_ORIGINAL(0x00467E10)

// CEffectParticle methods the wall calls directly (through their game addresses).
__declspec(naked) int __fastcall ParticleUnpackAt(CEffectParticle*, void*, int /*update*/, DeltaField* /*field*/)
    EI_ORIGINAL(0x00678D30)
__declspec(naked) void __fastcall ParticleLoadAt(CEffectParticle*, void*, void* /*stream*/) EI_ORIGINAL(0x006793E0)
__declspec(naked) void __fastcall ParticleSpawnAt(CEffectParticle*) EI_ORIGINAL(0x00678EC0)
__declspec(naked) void __fastcall ParticleAttachAt(CEffectParticle*) EI_ORIGINAL(0x00679350)

void ClearBase(CEffect* e) {
    e->localPosition_ = Vector3{0.0f, 0.0f, 0.0f};
    EmptyField(&e->state_);
    e->velocity_ = Vector3{0.0f, 0.0f, 0.0f};
    e->startTick_ = 0;
    e->moving_ = 0;
    e->changeStamp_ = -1;
}

// The light's range fields: +0xE0 = range, +0x100 = range^2, +0x104 = 1 / range^2 with the square
// left unrounded for the division (FMUL, FST, FDIV ST,ST(1)).
void SetLightRange(CLight* light, std::uint32_t rangeBits) {
    light->range_ = std::bit_cast<float>(rangeBits);
    const double square = static_cast<double>(light->range_) * light->range_;
    light->rangeSquared_ = static_cast<float>(square);
    light->inverseRangeSquared_ = static_cast<float>(1.0 / square);
}

std::uint32_t& LightFlags(CLight* light) { return light->flags_; }
Vector3& LightPosition(CLight* light) { return light->position_; }

// The particle tick's step towards the target, in the original's x87 order: speed = |velocity|
// ((x*x + y*y) + z*z), dist = sqrt(dy*dy + dx*dx); nothing when dist is not above 0 (or NaN);
// otherwise t = speed / (dist < speed (or NaN) ? speed : dist), rounded to float.
int StepFraction(const Vector3* velocity, const Vector3* dxdy, float* t) {
    const double speed = x87::fsqrt(LengthSquaredUnrounded<Sum3::XYZ, double>(*velocity));
    const double dist = x87::fsqrt(LengthSquared2DUnrounded<double>(dxdy->y, dxdy->x));
    // FCOM 0073B858; TEST AH,0x41: on only when above.
    if (!(dist > *reinterpret_cast<const float*>(0x0073B858))) return 0;
    // FCOM ST(1); TEST AH,1: the speed when the distance is below it or they are unordered.
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

// The scorch mark's angle: a random value times the double at 0073FFD0, the value loaded with
// FILD qword (exact), FMUL qword, rounded to float.
float RandomAngle(std::uint32_t random) {
    return static_cast<float>(x87::FromUnsigned(random) * *reinterpret_cast<const double*>(0x0073FFD0));
}

// The ISAAC Next the 0x2030 scorch inlines (same as 004667F0).
std::uint32_t InlineRandom() {
    auto* count = reinterpret_cast<std::uint32_t*>(0x007A9D44);
    std::uint32_t index = (*count)--;
    if (index == 0) {
        RandomRefill(TheRandom());
        *count = 0xFF;
    }
    return reinterpret_cast<const std::uint32_t*>(0x007A9D48)[*count];
}

// The fire wall's scorch mark: angle = atan2(direction.y, direction.x) and outer =
// sqrt(x*x + y*y) + half + 0.3, both rounded to float only when stored, in the original's x87 order.
void WallScorchShape(const Vector3* direction, const float* half, float* angleAndOuter) {
    angleAndOuter[0] = static_cast<float>(x87::fpatan(direction->y, direction->x));
    const double outer = x87::fsqrt(LengthSquared2DUnrounded<double>(direction->x, direction->y));
    angleAndOuter[1] = static_cast<float>((outer + *half) + *reinterpret_cast<const float*>(0x0073C54C));
}

} // namespace

// --- CEffectPointLight ---------------------------------------------------------------------------

void CEffectPointLight::Reset() {
    ClearBase(this);
    if (light_ != nullptr) {
        RemoveLight(LightManager(), nullptr, light_);
        CLight* old = light_;
        light_ = nullptr;
        if (old != nullptr) old->Release();
    }
    color_ = Vector3{0.0f, 0.0f, 0.0f};
    range_ = 0.0f;
    lightFlags_ = 0;
}

int CEffectPointLight::Tick() {
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
        RemoveLight(LightManager(), nullptr, light_);
        std::memcpy(&LightPosition(light_), &localPosition_, sizeof(Vector3));
        LightManagerAddOriginal(LightManager(), nullptr, light_);
        return 1;
    }
    MoveLight(LightManager(), nullptr, light_, &delta);
    std::memcpy(&LightPosition(light_), &localPosition_, sizeof(Vector3));
    return 1;
}

void CEffectPointLight::Start(int update) {
    if (update == 0) {
        CEngineObject* created = LightFactory()->CreateObject(0x31, -1);
        AssignLight(&light_, nullptr, created);
        Vector3 position;
        x87::CopyFloat(&position.x, &localPosition_.x);
        x87::CopyFloat(&position.y, &localPosition_.y);
        x87::CopyFloat(&position.z, &localPosition_.z);
        Vector3 color;
        x87::CopyFloat(&color.x, &color_.x);
        x87::CopyFloat(&color.y, &color_.y);
        x87::CopyFloat(&color.z, &color_.z);
        SetLightColorPosition(light_, nullptr, &color, &position);
        SetLightRange(light_, Bits(range_) & 0x7FFFFFFFu);
        LightFlags(light_) |= lightFlags_;
        LightManagerAddOriginal(LightManager(), nullptr, light_);
        if (moving_ != 0) LightFlags(light_) |= 0x100;
    } else {
        x87::CopyFloat(&light_->color_[0], &color_.x);
        x87::CopyFloat(&light_->color_[1], &color_.y);
        x87::CopyFloat(&light_->color_[2], &color_.z);
        float range = FromBits(Bits(range_) & 0x7FFFFFFFu);
        float shownRange;
        std::memcpy(&shownRange, &light_->range_, 4);
        float change = FromBits(Bits(static_cast<float>(shownRange - range)) & 0x7FFFFFFFu);
        if (!(change >= ConstAt(worlddata::RangeEpsilon()))) {
            // The range is (nearly) the same: move the light.
            Vector3& at = LightPosition(light_);
            Vector3 delta;
            delta.z = static_cast<float>(localPosition_.z - at.z);
            delta.y = static_cast<float>(localPosition_.y - at.y);
            delta.x = static_cast<float>(localPosition_.x - at.x);
            MoveLight(LightManager(), nullptr, light_, &delta);
            std::memcpy(&LightPosition(light_), &localPosition_, sizeof(Vector3));
        } else {
            RemoveLight(LightManager(), nullptr, light_);
            std::memcpy(&LightPosition(light_), &localPosition_, sizeof(Vector3));
            SetLightRange(light_, Bits(range_) & 0x7FFFFFFFu);
            LightManagerAddOriginal(LightManager(), nullptr, light_);
        }
    }
    // A negative range (or NaN) sets flag 0x400.
    if (!(range_ >= ConstAt(worlddata::ZeroRef())))
        LightFlags(light_) |= 0x400;
    else
        LightFlags(light_) &= ~0x400u;
}

int CEffectPointLight::Unpack(int update, DeltaField* field) {
    Vector3 position;
    ReadPosition(field, nullptr, &position);
    ReadFloat16x3(field, nullptr, &color_);
    ReadFloat16(field, nullptr, &range_);
    ReadFloat16x3(field, nullptr, &velocity_);
    ReadDelta(field, nullptr, &lightFlags_, 4);
    moving_ = IsMoving(velocity_);
    if (!SnapshotReadToEnd()) return 0;
    localPosition_ = position;
    EffectSlots(this)->Start(update);
    return 1;
}

void CEffectPointLight::Pack(DeltaField* field) {
    Vector3 position = localPosition_;
    WritePosition(field, nullptr, &position);
    WriteFloat16x3(field, nullptr, &color_);
    WriteFloat16(field, nullptr, Bits(range_));
    WriteFloat16x3(field, nullptr, &velocity_);
    WriteDelta(field, nullptr, &lightFlags_, 4);
}

void CEffectPointLight::LoadState(void* stream) {
    Read(stream, &localPosition_, 12);
    Read(stream, &color_, 12);
    Read(stream, &range_, 4);
    Read(stream, &velocity_, 12);
    moving_ = IsMoving(velocity_);
    if (Version(stream) < 0x67) return;
    Read(stream, &lightFlags_, 4);
}

void CEffectPointLight::SaveState(void* stream) {
    Write(stream, &localPosition_, 12);
    Write(stream, &color_, 12);
    WriteCopy(stream, &range_);
    Write(stream, &velocity_, 12);
    WriteCopy(stream, &lightFlags_);
}

double CEffectPointLight::Radius() { return static_cast<double>(ConstAt(worlddata::DefaultRadius())) + range_; }

// --- CEffectParticle -----------------------------------------------------------------------------

void CEffectParticle::Reset() {
    ClearBase(this);
    if (source_ != nullptr) {
        StopSource(source_);
        CParticleSource* old = source_;
        source_ = nullptr;
        if (old != nullptr) old->AsObject()->Release();
    }
    size_ = 1.0f;
    alpha_ = 1.0f;
    param164_ = 1.0f;
    target_ = Vector3{0.0f, 0.0f, 0.0f};
    type_ = 0;
    attachedTo_ = -1;
    unitId_ = -1;
}

Vector3* CEffectParticle::Position(void* group) {
    if (attachedTo_ == -1) return &localPosition_;
    CEngineObject* object = ContextWorld(this)->ObjectWithId(attachedTo_);
    if (object == nullptr) return &localPosition_;
    if (GroupContains(static_cast<const ScriptGroup*>(group), nullptr, object) == 0) {
        // Not in the group: far away.
        worldPosition_.x = FromBits(0xC9742400u);   // -1e6
        worldPosition_.y = FromBits(0xC9742400u);
        worldPosition_.z = 0.0f;
        return &worldPosition_;
    }
    const Vector3& at = static_cast<CTransformObject*>(object)->localPosition_;
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
        StopSource(source_);
        CParticleSource* old = source_;
        source_ = nullptr;
        if (old != nullptr) old->AsObject()->Release();
    }
    ParticleSpawnAt(this);
}

int CEffectParticle::Unpack(int update, DeltaField* field) {
    Vector3 position;
    ReadPosition(field, nullptr, &position);
    ReadDelta(field, nullptr, &startTick_, 4);
    ReadDelta(field, nullptr, &type_, 4);
    ReadFloat16(field, nullptr, &size_);
    ReadDelta(field, nullptr, &attachedTo_, 4);
    ReadFloat16x3(field, nullptr, &velocity_);
    ReadPosition(field, nullptr, &target_);
    ReadDelta(field, nullptr, &unitId_, 4);
    ReadFloat16(field, nullptr, &alpha_);
    ReadFloat16(field, nullptr, &param164_);
    moving_ = IsMoving(velocity_);
    // Only a plain particle effect checks the snapshot here (the wall checks after its own field).
    if (GetClassId() == 0x4A && !SnapshotReadToEnd()) return 0;
    if (unitId_ != -1) {
        void* unit = FindUnit(ObjectWorld(this), nullptr, unitId_);
        if (unit != nullptr) {
            Vector3 point;
            std::memcpy(&position, UnitEffectPoint(unit, nullptr, &point), sizeof(Vector3));
        }
    }
    localPosition_ = position;
    EffectSlots(this)->Start(update);
    return 1;
}

void CEffectParticle::Spawn() {
    if (source_ != nullptr) {
        // Already running: follow the effect.
        std::memcpy(&source_->position, &localPosition_, sizeof(Vector3));
        float baseSize;
        std::memcpy(&baseSize, &source_->baseSize, 4);
        ScaleSource(source_, nullptr, static_cast<float>(size_ / baseSize));
        x87::CopyFloat(&source_->alpha, &alpha_);
        x87::CopyFloat(&source_->typeParam, &param164_);
        ParticleAttachAt(this);
        return;
    }

    AssignSource(&source_, nullptr, CreateSource(ParticleManager(), nullptr, type_));
    std::memcpy(&source_->position, &localPosition_, sizeof(Vector3));
    float baseSize;
    std::memcpy(&baseSize, &source_->baseSize, 4);
    ScaleSource(source_, nullptr, static_cast<float>(size_ / baseSize));
    ParticleSourceAttachOriginal(source_, nullptr, nullptr);
    x87::CopyFloat(&source_->alpha, &alpha_);
    x87::CopyFloat(&source_->typeParam, &param164_);
    ParticleAttachAt(this);

    // Catch up with the ticks since the effect began (at most 30 for lasting types).
    int age = GameTick() - startTick_;
    if (!IsOneShotType(type_) && age >= 30) age = 30;
    if (age > 1) {
        for (int left = age - 1; left != 0; --left) source_->AsObject()->RunTick();
    }

    if (type_ == 0x2002) {
        std::uint32_t alpha = BelowOneOrOne(static_cast<float>(alpha_ * ConstAt(worlddata::ScorchAlpha())));
        std::uint32_t radius = Bits(param164_);
        float angle = RandomAngle(RandomNext(static_cast<IsaacRandom*>(TheRandom())));
        AddScorch(WorldClient(), nullptr, Bits(localPosition_.x), Bits(localPosition_.y), radius, radius, Bits(angle), alpha);
    }
    if (type_ == 0x2030) {
        std::uint32_t alpha = BelowOneOrOne(static_cast<float>(alpha_ * ConstAt(worlddata::ScorchAlpha())));
        std::uint32_t random = InlineRandom();
        std::uint32_t radius = Bits(size_);
        float angle = RandomAngle(random);
        AddScorch(WorldClient(), nullptr, Bits(localPosition_.x), Bits(localPosition_.y), radius, radius, Bits(angle), alpha);
        CameraShakeOriginal(CameraObject(), nullptr, &localPosition_, size_, 2.0f, 0.25f);
    }
}

void CEffectParticle::Pack(DeltaField* field) {
    Vector3 position = localPosition_;
    WritePosition(field, nullptr, &position);
    int value = startTick_;
    WriteDelta(field, nullptr, &value, 4);
    WriteDelta(field, nullptr, &type_, 4);
    WriteFloat16(field, nullptr, Bits(size_));
    value = attachedTo_;
    WriteDelta(field, nullptr, &value, 4);
    WriteFloat16x3(field, nullptr, &velocity_);
    WritePosition(field, nullptr, &target_);
    value = unitId_;
    WriteDelta(field, nullptr, &value, 4);
    WriteFloat16(field, nullptr, Bits(alpha_));
    WriteFloat16(field, nullptr, Bits(param164_));
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
    std::memcpy(&param164_, &b, 4);
    MarkChanged();
}

void CEffectParticle::SetTarget(const Vector3* target) {
    std::memcpy(&target_, target, sizeof(Vector3));
    MarkChanged();
}

void CEffectParticle::Attach() {
    // Nothing to do when the source already follows that object, or follows none and none is set.
    // Without a source the original goes on and reads through the null pointer.
    if (source_ != nullptr) {
        auto* followed = static_cast<CEngineObject*>(source_->attachedTo);
        if (followed != nullptr) {
            if (followed->id_ == attachedTo_) return;
        } else if (attachedTo_ == -1) {
            return;
        }
    }
    void* object = FindObject(ObjectManager(), nullptr, attachedTo_);
    Vector3 saved;
    std::memcpy(&saved, &source_->position, sizeof(Vector3));
    ParticleSourceAttachOriginal(source_, nullptr, object);
    if (object == nullptr) std::memcpy(&source_->position, &saved, sizeof(Vector3));
}

void CEffectParticle::LoadState(void* stream) {
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
    Read(stream, &param164_, 4);
}

void CEffectParticle::SaveState(void* stream) {
    Write(stream, &localPosition_, 12);
    Write(stream, &type_, 4);
    WriteCopy(stream, &size_);
    WriteCopy(stream, &attachedTo_);
    Write(stream, &velocity_, 12);
    WriteCopy(stream, &startTick_);
    Write(stream, &target_, 12);
    WriteCopy(stream, &unitId_);
    WriteCopy(stream, &alpha_);
    WriteCopy(stream, &param164_);
}

int CEffectParticle::Tick() {
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
            std::uint32_t zBits = !(z >= floor) ? Bits(floor) : Bits(z);
            localPosition_.x = x;
            localPosition_.y = y;
            std::memcpy(&localPosition_.z, &zBits, 4);
            if (source_ != nullptr) std::memcpy(&source_->position, &localPosition_, sizeof(Vector3));
        }
    }
    if (!IsOneShotType(type_)) return 1;
    int lifetime = type_ == 0x2019 ? 0x78 : 0x3C;
    int now = WorldOf(this)->tick_;
    return now > startTick_ + lifetime ? 0 : 1;
}

// --- CEffectWall ---------------------------------------------------------------------------------

void CEffectWall::Reset() {
    localPosition_ = Vector3{0.0f, 0.0f, 0.0f};
    EmptyField(&state_);
    startTick_ = 0;
    velocity_ = Vector3{0.0f, 0.0f, 0.0f};
    moving_ = 0;
    changeStamp_ = -1;
    if (source_ != nullptr) {
        StopSource(source_);
        CParticleSource* old = source_;
        source_ = nullptr;
        if (old != nullptr) old->AsObject()->Release();
    }
    size_ = 1.0f;
    alpha_ = 1.0f;
    param164_ = 1.0f;
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
    if (ParticleUnpackAt(this, nullptr, update, field) == 0) return 0;
    ReadFloat16x3(field, nullptr, &direction_);
    if (!SnapshotReadToEnd()) return 0;
    if (source_ != nullptr) std::memcpy(&source_->extent, &direction_, sizeof(Vector3));
    if (update == 0 && type_ == 0x2010) {
        // A scorch mark under the new wall: as long as the wall, as wide as half its size.
        std::uint32_t alpha = BelowOneOrOne(static_cast<float>(alpha_ * ConstAt(worlddata::ScorchAlpha())));
        float half = static_cast<float>(size_ * ConstAt(worlddata::HalfRef()));
        float shape[2];   // angle, outer
        WallScorchShape(&direction_, &half, shape);
        float inner = static_cast<float>(half + ConstAt(worlddata::ScorchMargin()));
        AddScorch(WorldClient(), nullptr, Bits(localPosition_.x), Bits(localPosition_.y), Bits(shape[1]), Bits(inner),
                  Bits(shape[0]), alpha);
    }
    return 1;
}

void CEffectWall::Pack(DeltaField* field) {
    Vector3 position = localPosition_;
    WritePosition(field, nullptr, &position);
    int value = startTick_;
    WriteDelta(field, nullptr, &value, 4);
    WriteDelta(field, nullptr, &type_, 4);
    WriteFloat16(field, nullptr, Bits(size_));
    value = attachedTo_;
    WriteDelta(field, nullptr, &value, 4);
    WriteFloat16x3(field, nullptr, &velocity_);
    WritePosition(field, nullptr, &target_);
    value = unitId_;
    WriteDelta(field, nullptr, &value, 4);
    WriteFloat16(field, nullptr, Bits(alpha_));
    WriteFloat16(field, nullptr, Bits(param164_));
    WriteFloat16x3(field, nullptr, &direction_);
}

void CEffectWall::LoadState(void* stream) {
    ParticleLoadAt(this, nullptr, stream);
    Read(stream, &direction_, 12);
}

void CEffectWall::SaveState(void* stream) {
    CEffectParticle::SaveState(stream);
    Write(stream, &direction_, 12);
}

// --- CEffectLightning ----------------------------------------------------------------------------

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
    CEngineObject* target = WorldOf(this)->ObjectWithId(toObject_);
    if (target != nullptr) {
        std::memcpy(&to_, &static_cast<CTransformObject*>(target)->localPosition_, sizeof(Vector3));
        to_.z = to_.z + ConstAt(worlddata::OneRef());
    }
    CEngineObject* source = WorldOf(this)->ObjectWithId(fromObject_);
    if (source != nullptr && source->GetClassId() == 0x50 && ContextWorld(this)->IsServer() == 0) {
        Vector3 point;
        std::memcpy(&from_, UnitEffectPoint(source, nullptr, &point), sizeof(Vector3));
    }
}

void CEffectLightning::Start(int update) {
    if (update != 0) return;
    auto* created = static_cast<CLightning*>(BoltFactory()->CreateObject(0, -1));
    CLightning* old = bolt_;
    bolt_ = created;
    if (created != nullptr) ++created->refCount_;
    if (old != nullptr) old->Release();
    UpdateEnds();
    StartBolt(bolt_, nullptr, &from_, &to_, Bits(width_), flag_);
}

int CEffectLightning::Unpack(int update, DeltaField* field) {
    Vector3 position;
    ReadPosition(field, nullptr, &position);
    ReadPosition(field, nullptr, &from_);
    ReadPosition(field, nullptr, &to_);
    ReadFloat16(field, nullptr, &width_);
    ReadDelta(field, nullptr, &flag_, 1);
    ReadDelta(field, nullptr, &toObject_, 4);
    ReadDelta(field, nullptr, &fromObject_, 4);
    if (!SnapshotReadToEnd()) return 0;
    UpdateEnds();
    localPosition_ = position;
    EffectSlots(this)->Start(update);
    return 1;
}

void CEffectLightning::Pack(DeltaField* field) {
    Vector3 position = localPosition_;
    WritePosition(field, nullptr, &position);
    WritePosition(field, nullptr, &from_);
    WritePosition(field, nullptr, &to_);
    WriteFloat16(field, nullptr, Bits(width_));
    WriteDelta(field, nullptr, &flag_, 1);
    int value = toObject_;
    WriteDelta(field, nullptr, &value, 4);
    value = fromObject_;
    WriteDelta(field, nullptr, &value, 4);
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

void CEffectLightning::LoadState(void* stream) {
    Read(stream, &localPosition_, 12);
    Read(stream, &from_, 12);
    Read(stream, &to_, 12);
    Read(stream, &width_, 4);
    Read(stream, &toObject_, 4);
    Read(stream, &fromObject_, 4);
    if (Version(stream) < 100) return;
    Read(stream, &flag_, 1);
}

void CEffectLightning::SaveState(void* stream) {
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

int CEffectLightning::Tick() {
    if (bolt_ == nullptr || (toObject_ == -1 && fromObject_ == -1)) return 1;
    UpdateEnds();
    MoveBolt(bolt_, nullptr, &from_, &to_);
    return 1;
}

// --- Hooks ---------------------------------------------------------------------------------------

EI_REPLACE_METHOD(0x006780C0, CEffectPointLight::Reset);
EI_REPLACE_METHOD(0x006781B0, CEffectPointLight::Tick);
EI_REPLACE_METHOD(0x006782D0, CEffectPointLight::Start);
EI_REPLACE_METHOD(0x00678520, CEffectPointLight::Unpack);
EI_REPLACE_METHOD(0x00678610, CEffectPointLight::Pack);
EI_REPLACE_METHOD(0x00678690, CEffectPointLight::LoadState);
EI_REPLACE_METHOD(0x00678740, CEffectPointLight::SaveState);
EI_REPLACE_METHOD(0x00677F70, CEffectPointLight::Radius);

EI_REPLACE_METHOD(0x00678A90, CEffectParticle::Reset);
EI_REPLACE_METHOD(0x00678BA0, CEffectParticle::Position);
EI_REPLACE_METHOD(0x00678CF0, CEffectParticle::Start);
EI_REPLACE_METHOD(0x00678D30, CEffectParticle::Unpack);
EI_REPLACE_METHOD(0x00678EC0, CEffectParticle::Spawn);
EI_REPLACE_METHOD(0x006791A0, CEffectParticle::Pack);
EI_REPLACE_METHOD(0x00679280, CEffectParticle::SetAttachedTo);
EI_REPLACE_METHOD(0x006792B0, CEffectParticle::SetSize);
EI_REPLACE_METHOD(0x006792E0, CEffectParticle::SetColors);
EI_REPLACE_METHOD(0x00679310, CEffectParticle::SetTarget);
EI_REPLACE_METHOD(0x00679350, CEffectParticle::Attach);
EI_REPLACE_METHOD(0x006793E0, CEffectParticle::LoadState);
EI_REPLACE_METHOD(0x006794E0, CEffectParticle::SaveState);
EI_REPLACE_METHOD(0x006795C0, CEffectParticle::Tick);

EI_REPLACE_METHOD(0x00679AF0, CEffectWall::Reset);
EI_REPLACE_METHOD(0x00679C30, CEffectWall::SetDirection);
EI_REPLACE_METHOD(0x00679C70, CEffectWall::Unpack);
EI_REPLACE_METHOD(0x00679DB0, CEffectWall::Pack);
EI_REPLACE_METHOD(0x00679EA0, CEffectWall::LoadState);
EI_REPLACE_METHOD(0x00679ED0, CEffectWall::SaveState);

EI_REPLACE_METHOD(0x0067A1B0, CEffectLightning::Reset);
EI_REPLACE_METHOD(0x0067A370, CEffectLightning::Start);
EI_REPLACE_METHOD(0x0067A490, CEffectLightning::Unpack);
EI_REPLACE_METHOD(0x0067A600, CEffectLightning::Pack);
EI_REPLACE_METHOD(0x0067A6B0, CEffectLightning::SetEnds);
EI_REPLACE_METHOD(0x0067A770, CEffectLightning::LoadState);
EI_REPLACE_METHOD(0x0067A7F0, CEffectLightning::SaveState);
EI_REPLACE_METHOD(0x0067A890, CEffectLightning::SetObjects);
EI_REPLACE_METHOD(0x0067A8C0, CEffectLightning::SetFlag);
EI_REPLACE_METHOD(0x0067A8F0, CEffectLightning::Tick);
