// The visible effects: EffectPointLight, EffectParticle, EffectWall, EffectLightning. See
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
#include "game_singletons.h"
#include "lib/float_select.h"
#include "lib/nan_audit.h"
#include "lib/pc24.h"
#include "save_versions.h"

#include <bit>
#include <cstring>

using namespace effect;

namespace {



void ClearBase(Effect* e) {
    e->localPosition = Vector3{0.0f, 0.0f, 0.0f};
    EmptyField(&e->state);
    e->velocity = Vector3{0.0f, 0.0f, 0.0f};
    e->startTick = 0;
    e->moving = 0;
    e->changeStamp = -1;
}

// The light's range fields: range, range^2 and 1 / range^2.
void SetLightRange(Light* light, std::uint32_t rangeBits) {
    light->range = std::bit_cast<float>(rangeBits);
    const float square = light->range * light->range;
    light->rangeSquared = square;
    light->inverseRangeSquared = 1.0f / square;
}

std::uint32_t& LightFlags(Light* light) { return light->flags; }
Vector3& LightPosition(Light* light) { return light->localPosition; }

// The particle tick's step towards the target, in x87 order: speed = |velocity|
// ((x*x + y*y) + z*z), dist = sqrt(dy*dy + dx*dx); nothing when dist is not above 0 (or NaN);
// otherwise t = speed / (dist < speed (or NaN) ? speed : dist), rounded to float.
int StepFraction(const Vector3& velocity, const Vector3& dxdy, float* t) {
    const float speed = std::sqrt(LengthSquaredUnrounded<Sum3::XYZ>(velocity));
    const float dist = std::sqrt(LengthSquared2DUnrounded(dxdy.y, dxdy.x));
    // On only when above.
    EI_NAN_AUDIT(dist);
    if (dist <= 0.0f) return 0;
    // The speed when the distance is below it or they are unordered.
    const float divisor = MaxKeepB(dist, speed);
    *t = speed / divisor;
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

// The scorch mark's angle: a random value (exact) times a double constant, rounded to float.
float RandomAngle(std::uint32_t random) { return pc24::Mul(static_cast<double>(random), 1.4668330997698233e-9); }

// The ISAAC Next the 0x2030 scorch inlines.
std::uint32_t InlineRandom() {
    IsaacRandom& random = g_random;
    std::uint32_t index = random.count--;
    if (index == 0) {
        random.Refill();
        random.count = 0xFF;
    }
    return random.results[random.count];
}

// The fire wall's scorch mark: angle = atan2(direction.y, direction.x) and outer =
// sqrt(x*x + y*y) + half + 0.3, in x87 order.
void WallScorchShape(const Vector3& direction, const float* half, float* angleAndOuter) {
    angleAndOuter[0] = pc24::Round(pc24::Atan2(direction.y, direction.x));
    const float outer = std::sqrt(LengthSquared2DUnrounded(direction.x, direction.y));
    angleAndOuter[1] = (outer + *half) + 0.3f;
}

} // namespace


void EffectPointLight::Reset() {
    ClearBase(this);
    if (light != nullptr) {
        g_clientWorld.terrain->RemoveLight(light);
        light.Reset();
    }
    color = Vector3{0.0f, 0.0f, 0.0f};
    range = 0.0f;
    lightFlags = 0;
}

bool EffectPointLight::RunTick() {
    if (moving == 0) return true;
    float x = localPosition.x + velocity.x;
    float y = localPosition.y + velocity.y;
    float z = localPosition.z + velocity.z;
    localPosition.x = x;
    localPosition.y = y;
    localPosition.z = z;
    if (light != nullptr) LightPosition(light) = localPosition;
    return true;
}

void EffectPointLight::Start(int update) {
    if (update == 0) {
        light.Assign(g_clientWorld.lightFactory->CreateLight(0x31));
        Vector3 position;
        position.x = localPosition.x;
        position.y = localPosition.y;
        position.z = localPosition.z;
        Vector3 tint;
        tint.x = color.x;
        tint.y = color.y;
        tint.z = color.z;
        light->SetPoint(tint, position);
        SetLightRange(light, Bits(range) & 0x7FFFFFFFu);
        LightFlags(light) |= lightFlags;
        g_clientWorld.terrain->AddLight(light);
        if (moving != 0) LightFlags(light) |= LightFlag::Moving;
    } else {
        light->color[0] = color.x;
        light->color[1] = color.y;
        light->color[2] = color.z;
        float absRange = FromBits(Bits(range) & 0x7FFFFFFFu);
        float shownRange;
        shownRange = light->range;
        float change = FromBits(Bits(static_cast<float>(shownRange - absRange)) & 0x7FFFFFFFu);
        EI_NAN_AUDIT(change);
        LightPosition(light) = localPosition;
        // A range not (nearly) the same is taken over.
        if (!(change < ConstAt(worlddata::RangeEpsilon()))) SetLightRange(light, Bits(range) & 0x7FFFFFFFu);
    }
    // A negative range (or NaN) sets LightFlag::Off.
    if (range >= ConstAt(worlddata::ZeroRef()))
        LightFlags(light) &= ~LightFlag::Off;
    else
        LightFlags(light) |= LightFlag::Off;
}

int EffectPointLight::Unpack(int update, DeltaField* field) {
    Vector3 position;
    position = field->ReadPosition();
    color = field->ReadFloat16x3();
    range = field->ReadFloat16();
    velocity = field->ReadFloat16x3();
    field->ReadDelta(&lightFlags, 4);
    moving = IsMoving(velocity);
    if (!SnapshotReadToEnd()) return 0;
    localPosition = position;
    Start(update);
    return 1;
}

void EffectPointLight::Pack(DeltaField* field) {
    Vector3 position = localPosition;
    field->WritePosition(position);
    field->WriteFloat16x3(&color.x);
    field->WriteFloat16(Bits(range));
    field->WriteFloat16x3(&velocity.x);
    field->WriteDelta(&lightFlags, 4);
}

void EffectPointLight::Load(ByteStream* stream) {
    stream->ReadValue(localPosition);
    stream->ReadValue(color);
    stream->ReadValue(range);
    stream->ReadValue(velocity);
    moving = IsMoving(velocity);
    if (Version(stream) < kVersionLightFlags) return;
    stream->ReadValue(lightFlags);
}

void EffectPointLight::Save(ByteStream* stream) {
    stream->WriteValue(localPosition);
    stream->WriteValue(color);
    stream->WriteValue(range);
    stream->WriteValue(velocity);
    stream->WriteValue(lightFlags);
}

double EffectPointLight::LightRadius() { return ConstAt(worlddata::DefaultRadius()) + range; }


void EffectParticle::Reset() {
    ClearBase(this);
    if (source != nullptr) {
        source->Stop();
        source.Reset();
    }
    size = 1.0f;
    alpha = 1.0f;
    typeParam = 1.0f;
    target = Vector3{0.0f, 0.0f, 0.0f};
    type = 0;
    attachedTo = -1;
    unitId = -1;
}

Vector3* EffectParticle::Position(const ScriptGroup* group) {
    if (attachedTo == -1) return &localPosition;
    MapObject* object = WorldContext()->ObjectWithId(attachedTo);
    if (object == nullptr) return &localPosition;
    if (group->Contains(object) == 0) {
        // Not in the group: far away.
        worldPosition.x = FromBits(0xC9742400u);    // -1e6
        worldPosition.y = FromBits(0xC9742400u);
        worldPosition.z = 0.0f;
        return &worldPosition;
    }
    const Vector3& at = object->localPosition;
    float x = localPosition.x + at.x;
    float y = at.y + localPosition.y;
    float z = at.z + localPosition.z;
    worldPosition.x = x;
    worldPosition.y = y;
    worldPosition.z = z;
    return &worldPosition;
}

void EffectParticle::Start(int update) {
    if (update == 0 && source != nullptr) {
        source->Stop();
        source.Reset();
    }
    Spawn();
}

int EffectParticle::Unpack(int update, DeltaField* field) {
    Vector3 position;
    position = field->ReadPosition();
    field->ReadDelta(&startTick, 4);
    field->ReadDelta(&type, 4);
    size = field->ReadFloat16();
    field->ReadDelta(&attachedTo, 4);
    velocity = field->ReadFloat16x3();
    target = field->ReadPosition();
    field->ReadDelta(&unitId, 4);
    alpha = field->ReadFloat16();
    typeParam = field->ReadFloat16();
    moving = IsMoving(velocity);
    // Only a plain particle effect checks the snapshot here (the wall checks after its own field).
    if (GetClassId() == ClassId::EffectParticle && !SnapshotReadToEnd()) return 0;
    if (unitId != -1) {
        Unit* unit = OwningWorld()->FindUnit(unitId);
        if (unit != nullptr) {
            position = unit->EffectPoint();
        }
    }
    localPosition = position;
    Start(update);
    return 1;
}

void EffectParticle::Spawn() {
    if (source != nullptr) {
        // Already running: follow the effect.
        source->localPosition = localPosition;
        float baseSize;
        baseSize = source->baseSize;
        source->ScaleSize(static_cast<float>(size / baseSize));
        source->alpha = alpha;
        source->typeParam = typeParam;
        Attach();
        return;
    }

    source.Assign(g_clientWorld.particleManager->CreateSource(type));
    source->localPosition = localPosition;
    float baseSize;
    baseSize = source->baseSize;
    source->ScaleSize(static_cast<float>(size / baseSize));
    source->Attach(nullptr);
    source->alpha = alpha;
    source->typeParam = typeParam;
    Attach();

    // Catch up with the ticks since the effect began (at most 30 for lasting types).
    int age = g_clientWorld.tick - startTick;
    if (!IsOneShotType(type) && age >= 30) age = 30;
    if (age > 1) {
        for (int left = age - 1; left != 0; --left) source->RunTick();
    }

    if (ParticleTypeOf(type) == ParticleType::FireBlast) {
        float fadeAlpha = BelowOneOrOne(static_cast<float>(alpha * ConstAt(worlddata::ScorchAlpha())));
        float radius = typeParam;
        float angle = RandomAngle(((&g_random))->Next());
        g_clientWorld.AddScorchMark(localPosition.x, localPosition.y, radius, radius, angle, fadeAlpha);
    }
    if (ParticleTypeOf(type) == ParticleType::LightningBlast) {
        float fadeAlpha = BelowOneOrOne(static_cast<float>(alpha * ConstAt(worlddata::ScorchAlpha())));
        std::uint32_t random = InlineRandom();
        float radius = size;
        float angle = RandomAngle(random);
        g_clientWorld.AddScorchMark(localPosition.x, localPosition.y, radius, radius, angle, fadeAlpha);
        g_clientWorld.camera->Shake(localPosition, size, 2.0f, 0.25f);
    }
}

void EffectParticle::Pack(DeltaField* field) {
    Vector3 position = localPosition;
    field->WritePosition(position);
    int value = startTick;
    field->WriteDelta(&value, 4);
    field->WriteDelta(&type, 4);
    field->WriteFloat16(Bits(size));
    value = attachedTo;
    field->WriteDelta(&value, 4);
    field->WriteFloat16x3(&velocity.x);
    field->WritePosition(target);
    value = unitId;
    field->WriteDelta(&value, 4);
    field->WriteFloat16(Bits(alpha));
    field->WriteFloat16(Bits(typeParam));
}

void EffectParticle::SetAttachedTo(int objectId) {
    attachedTo = objectId;
    MarkChanged();
}

void EffectParticle::SetSize(float newSize) {
    size = newSize;
    MarkChanged();
}

void EffectParticle::SetColors(float newAlpha, float newTypeParam) {
    alpha = newAlpha;
    typeParam = newTypeParam;
    MarkChanged();
}

void EffectParticle::SetTarget(const Vector3& newTarget) {
    target = newTarget;
    MarkChanged();
}

void EffectParticle::Attach() {
    // Nothing to do when the source already follows that object, or follows none and none is set.
    // Without a source this goes on and reads through the null pointer, like the original.
    if (source != nullptr) {
        EngineObject* followed = source->attachedTo;
        if (followed != nullptr) {
            if (followed->id == attachedTo) return;
        } else if (attachedTo == -1) {
            return;
        }
    }
    Object3D* object = g_clientWorld.objectManager->FindObjectAs<Object3D>(static_cast<unsigned>(attachedTo));
    Vector3 saved;
    saved = source->localPosition;
    source->Attach(object);
    if (object == nullptr) source->localPosition = saved;
}

void EffectParticle::Load(ByteStream* stream) {
    stream->ReadValue(localPosition);
    stream->ReadValue(type);
    stream->ReadValue(size);
    stream->ReadValue(attachedTo);
    stream->ReadValue(velocity);
    moving = IsMoving(velocity);
    stream->ReadValue(startTick);
    stream->ReadValue(target);
    if (Version(stream) < kVersionEffectUnitId) return;
    stream->ReadValue(unitId);
    if (Version(stream) < kVersionEffectAlpha) return;
    stream->ReadValue(alpha);
    stream->ReadValue(typeParam);
}

void EffectParticle::Save(ByteStream* stream) {
    stream->WriteValue(localPosition);
    stream->WriteValue(type);
    stream->WriteValue(size);
    stream->WriteValue(attachedTo);
    stream->WriteValue(velocity);
    stream->WriteValue(startTick);
    stream->WriteValue(target);
    stream->WriteValue(unitId);
    stream->WriteValue(alpha);
    stream->WriteValue(typeParam);
}

bool EffectParticle::RunTick() {
    unitId = -1;
    if (moving != 0) {
        Vector3 d;
        d.x = static_cast<float>(target.x - localPosition.x);
        d.y = static_cast<float>(target.y - localPosition.y);
        d.z = static_cast<float>(target.z - localPosition.z);
        float t;
        if (StepFraction(velocity, d, &t) != 0) {
            // Keeps the target's height above the ground while moving towards it.
            float aboveGround = pc24::Sub(target.z, OwningWorld()->RawSurfaceHeightAt(target.x, target.y));
            float stepY = static_cast<float>(t * d.y);
            float stepZ = static_cast<float>(d.z * t);
            float x = static_cast<float>(t * d.x + localPosition.x);
            float y = static_cast<float>(stepY + localPosition.y);
            float z = static_cast<float>(stepZ + localPosition.z);
            float floor = pc24::Add(OwningWorld()->RawSurfaceHeightAt(x, y), aboveGround);
            localPosition.x = x;
            localPosition.y = y;
            localPosition.z = MaxKeepB(z, floor);
            if (source != nullptr) source->localPosition = localPosition;
        }
    }
    if (!IsOneShotType(type)) return true;
    int lifetime = ParticleTypeOf(type) == ParticleType::Teleport ? 0x78 : 0x3C;
    int now = OwningWorld()->tick;
    return now <= startTick + lifetime;
}


void EffectWall::Reset() {
    localPosition = Vector3{0.0f, 0.0f, 0.0f};
    EmptyField(&state);
    startTick = 0;
    velocity = Vector3{0.0f, 0.0f, 0.0f};
    moving = 0;
    changeStamp = -1;
    if (source != nullptr) {
        source->Stop();
        source.Reset();
    }
    size = 1.0f;
    alpha = 1.0f;
    typeParam = 1.0f;
    attachedTo = -1;
    target = Vector3{0.0f, 0.0f, 0.0f};
    type = 0;
    unitId = -1;
    direction = Vector3{0.0f, 0.0f, 0.0f};
}

void EffectWall::SetDirection(const Vector3& newDirection) {
    direction = newDirection;
    MarkChanged();
}

int EffectWall::Unpack(int update, DeltaField* field) {
    if (EffectParticle::Unpack(update, field) == 0) return 0;
    direction = field->ReadFloat16x3();
    if (!SnapshotReadToEnd()) return 0;
    if (source != nullptr) source->extent = direction;
    if (update == 0 && ParticleTypeOf(type) == ParticleType::Tendril) {
        // A scorch mark under the new wall: as long as the wall, as wide as half its size.
        float fadeAlpha = BelowOneOrOne(static_cast<float>(alpha * ConstAt(worlddata::ScorchAlpha())));
        float half = static_cast<float>(size * ConstAt(worlddata::HalfRef()));
        float shape[2];   // angle, outer
        WallScorchShape(direction, &half, shape);
        float inner = static_cast<float>(half + ConstAt(worlddata::ScorchMargin()));
        g_clientWorld.AddScorchMark(localPosition.x, localPosition.y, shape[1], inner, shape[0], fadeAlpha);
    }
    return 1;
}

void EffectWall::Pack(DeltaField* field) {
    Vector3 position = localPosition;
    field->WritePosition(position);
    int value = startTick;
    field->WriteDelta(&value, 4);
    field->WriteDelta(&type, 4);
    field->WriteFloat16(Bits(size));
    value = attachedTo;
    field->WriteDelta(&value, 4);
    field->WriteFloat16x3(&velocity.x);
    field->WritePosition(target);
    value = unitId;
    field->WriteDelta(&value, 4);
    field->WriteFloat16(Bits(alpha));
    field->WriteFloat16(Bits(typeParam));
    field->WriteFloat16x3(&direction.x);
}

void EffectWall::Load(ByteStream* stream) {
    EffectParticle::Load(stream);
    stream->ReadValue(direction);
}

void EffectWall::Save(ByteStream* stream) {
    EffectParticle::Save(stream);
    stream->WriteValue(direction);
}


void EffectLightning::Reset() {
    ClearBase(this);
    width = 1.0f;
    from = Vector3{0.0f, 0.0f, 0.0f};
    toObject = -1;
    fromObject = -1;
    to = Vector3{0.0f, 0.0f, 0.0f};
    bolt.Reset();
    flag = 0;
}

void EffectLightning::UpdateEnds() {
    MapObject* target = OwningWorld()->ObjectWithId(toObject);
    if (target != nullptr) {
        to = target->localPosition;
        to.z = to.z + ConstAt(worlddata::OneRef());
    }
    Unit* source = UnitOrNull(OwningWorld()->ObjectWithId(fromObject));
    if (source != nullptr && WorldContext()->IsServer() == 0) {
        from = source->EffectPoint();
    }
}

void EffectLightning::Start(int update) {
    if (update != 0) return;
    auto* created = g_clientWorld.listManager->CreateObjectAs<Lightning>(0, -1);
    bolt.Assign(created);
    UpdateEnds();
    bolt->Start(from, to, width, flag);
}

int EffectLightning::Unpack(int update, DeltaField* field) {
    Vector3 position;
    position = field->ReadPosition();
    from = field->ReadPosition();
    to = field->ReadPosition();
    width = field->ReadFloat16();
    field->ReadDelta(&flag, 1);
    field->ReadDelta(&toObject, 4);
    field->ReadDelta(&fromObject, 4);
    if (!SnapshotReadToEnd()) return 0;
    UpdateEnds();
    localPosition = position;
    Start(update);
    return 1;
}

void EffectLightning::Pack(DeltaField* field) {
    Vector3 position = localPosition;
    field->WritePosition(position);
    field->WritePosition(from);
    field->WritePosition(to);
    field->WriteFloat16(Bits(width));
    field->WriteDelta(&flag, 1);
    int value = toObject;
    field->WriteDelta(&value, 4);
    value = fromObject;
    field->WriteDelta(&value, 4);
}

void EffectLightning::SetEnds(const Vector3& newFrom, const Vector3& newTo, float newWidth) {
    from = newFrom;
    to = newTo;
    width = newWidth;
    // The midpoint: the x sum stays on the FPU, the y and z sums are rounded first.
    float half = ConstAt(worlddata::HalfRef());
    float sumY = static_cast<float>(from.y + to.y);
    float sumZ = static_cast<float>(from.z + to.z);
    localPosition.x = static_cast<float>((to.x + from.x) * half);
    localPosition.y = static_cast<float>(sumY * half);
    localPosition.z = static_cast<float>(sumZ * half);
    MarkChanged();
}

void EffectLightning::Load(ByteStream* stream) {
    stream->ReadValue(localPosition);
    stream->ReadValue(from);
    stream->ReadValue(to);
    stream->ReadValue(width);
    stream->ReadValue(toObject);
    stream->ReadValue(fromObject);
    if (Version(stream) < 100) return;
    stream->ReadValue(flag);
}

void EffectLightning::Save(ByteStream* stream) {
    stream->WriteValue(localPosition);
    stream->WriteValue(from);
    stream->WriteValue(to);
    stream->WriteValue(width);
    stream->WriteValue(toObject);
    stream->WriteValue(fromObject);
    stream->WriteValue(flag);
}

void EffectLightning::SetObjects(int newFromObject, int newToObject) {
    toObject = newToObject;
    fromObject = newFromObject;
    MarkChanged();
}

void EffectLightning::SetFlag(std::uint8_t newFlag) {
    flag = newFlag;
    MarkChanged();
}

bool EffectLightning::RunTick() {
    if (bolt == nullptr || (toObject == -1 && fromObject == -1)) return true;
    UpdateEnds();
    bolt->SetEndpoints(from, to);
    return true;
}
