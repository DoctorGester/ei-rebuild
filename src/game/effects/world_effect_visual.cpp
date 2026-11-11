// The visible effects: EffectPointLight, EffectParticle, EffectWall, EffectLightning. See
// world_effect.h and docs/notes/effects.md.

#include "game/world/camera.h"
#include "game/effects/effects.h"
#include "game/app/singletons.h"
#include "engine/scene/light.h"
#include "game/effects/lightning.h"
#include "engine/particles/particle_data.h"
#include "game/net/net_delta.h"
#include "engine/core/random.h"
#include "game/saves/save_versions.h"
#include "game/script/script.h"
#include "game/spells/spell.h"
#include "game/terrain/terrain.h"
#include "game/units/unit.h"
#include "game/world/world.h"
#include "game/effects/world_effect_internal.h"

#include "lib/float_select.h"
#include "lib/maths.h"

#include <cmath>
#include <cstring>

namespace {

constexpr float kScorchAlpha = 0.6f;

} // namespace

using namespace effect;

namespace {



// The light's range fields: range, range^2 and 1 / range^2.
void SetLightRange(Light* light, float range) {
    light->range = range;
    const float square = light->range * light->range;
    light->rangeSquared = square;
    light->inverseRangeSquared = 1.0f / square;
}

std::uint32_t& LightFlags(Light* light) { return light->flags; }
Vector3& LightPosition(Light* light) { return light->localPosition; }

// The particle tick's step towards the target: speed = |velocity|, dist = the planar distance; nothing when
// dist is not above 0 (or NaN); otherwise t = speed / (dist < speed (or NaN) ? speed : dist).
int StepFraction(const Vector3& velocity, const Vector3& dxdy, float* t) {
    const float speed = Length(velocity);
    const float dist = Length2D(XY(dxdy));
    // On only when above.
    if (dist <= 0.0f) return 0;
    // The speed when the distance is below it or they are unordered.
    const float divisor = MaxKeepB(dist, speed);
    *t = speed / divisor;
    return 1;
}

// The ticks a particle type plays for: its effect's [server] one_shot; 0 for a type that lasts until its object is
// removed (or an unknown type).
int OneShotTicks(int type) {
    const std::shared_ptr<const effectdata::Record> record = effectdata::g_effects.FindById(type);
    return record ? record->oneShot : 0;
}

// The fire wall's scorch mark: angle = atan2(direction.y, direction.x) and outer =
// sqrt(x*x + y*y) + half + 0.3.
void WallScorchShape(const Vector3& direction, const float* half, float* angleAndOuter) {
    angleAndOuter[0] = lib::Atan2(direction.y, direction.x);
    const float outer = Length2D(XY(direction));
    angleAndOuter[1] = (outer + *half) + 0.3f;
}

} // namespace


void EffectPointLight::Reset() {
    Effect::Reset();
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
    localPosition += velocity;
    if (light != nullptr) LightPosition(light) = localPosition;
    return true;
}

void EffectPointLight::Start(int update) {
    if (update == 0) {
        light.Assign(g_clientWorld.lightFactory->CreateLight(0x31));
        Vector3 position = localPosition;
        Vector3 tint = color;
        light->SetPoint(tint, position);
        SetLightRange(light, std::fabs(range));
        LightFlags(light) |= lightFlags;
        g_clientWorld.terrain->AddLight(light);
        if (moving != 0) LightFlags(light) |= LightFlag::Moving;
    } else {
        light->color[0] = color.x;
        light->color[1] = color.y;
        light->color[2] = color.z;
        float absRange = std::fabs(range);
        float shownRange;
        shownRange = light->range;
        float change = std::fabs(shownRange - absRange);
        LightPosition(light) = localPosition;
        // A range not (nearly) the same is taken over.
        if (!(change < 0.001f)) SetLightRange(light, std::fabs(range));
    }
    // A negative range (or NaN) sets LightFlag::Off.
    if (range >= 0.0f)
        LightFlags(light) &= ~LightFlag::Off;
    else
        LightFlags(light) |= LightFlag::Off;
}

int EffectPointLight::Unpack(int update, DeltaReader& field) {
    Vector3 position;
    position = field.ReadPosition();
    color = field.ReadFloat16x3();
    range = field.ReadFloat16();
    velocity = field.ReadFloat16x3();
    field.ReadDelta(&lightFlags, 4);
    moving = IsMoving(velocity);
    if (!field.SnapshotConsumed()) return 0;
    localPosition = position;
    Start(update);
    return 1;
}

void EffectPointLight::Pack(DeltaWriter& field) {
    Vector3 position = localPosition;
    field.WritePosition(position);
    field.WriteFloat16x3(color);
    field.WriteFloat16(range);
    field.WriteFloat16x3(velocity);
    field.WriteDelta(&lightFlags, 4);
}

void EffectPointLight::Load(ByteStream* stream) {
    stream->ReadValue(localPosition);
    stream->ReadValue(color);
    stream->ReadValue(range);
    stream->ReadValue(velocity);
    moving = IsMoving(velocity);
    if (stream->version < kVersionLightFlags) return;
    stream->ReadValue(lightFlags);
}

void EffectPointLight::Save(ByteStream* stream) {
    stream->WriteValue(localPosition);
    stream->WriteValue(color);
    stream->WriteValue(range);
    stream->WriteValue(velocity);
    stream->WriteValue(lightFlags);
}


void EffectParticle::Reset() {
    Effect::Reset();
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
    MapObject* object = OwningWorld()->ObjectWithId(attachedTo);
    if (object == nullptr) return &localPosition;
    if (group->Contains(object) == 0) {
        // Not in the group: far away.
        worldPosition = {-1e6f, -1e6f, 0.0f};
        return &worldPosition;
    }
    worldPosition = localPosition + object->localPosition;
    return &worldPosition;
}

void EffectParticle::Start(int update) {
    if (update == 0 && source != nullptr) {
        source->Stop();
        source.Reset();
    }
    Spawn();
}

int EffectParticle::Unpack(int update, DeltaReader& field) {
    Vector3 position;
    position = field.ReadPosition();
    field.ReadDelta(&startTick, 4);
    field.ReadDelta(&type, 4);
    size = field.ReadFloat16();
    field.ReadDelta(&attachedTo, 4);
    velocity = field.ReadFloat16x3();
    target = field.ReadPosition();
    field.ReadDelta(&unitId, 4);
    alpha = field.ReadFloat16();
    typeParam = field.ReadFloat16();
    moving = IsMoving(velocity);
    // Only a plain particle effect checks the snapshot here (the wall checks after its own field).
    if (GetClassId() == ClassId::EffectParticle && !field.SnapshotConsumed()) return 0;
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
        source->ScaleSize(size / baseSize);
        source->alpha = alpha;
        source->typeParam = typeParam;
        Attach();
        return;
    }

    source.Assign(g_clientWorld.particleManager->CreateSource(type));
    if (source == nullptr) return;   // a type this build does not know (ShownParticleType)
    source->localPosition = localPosition;
    float baseSize;
    baseSize = source->baseSize;
    source->ScaleSize(size / baseSize);
    source->Attach(nullptr);
    source->alpha = alpha;
    source->typeParam = typeParam;
    Attach();

    // Catch up with the ticks since the effect began (at most 30 for lasting types).
    int age = g_clientWorld.tick - startTick;
    if (OneShotTicks(type) == 0 && age >= 30) age = 30;
    if (age > 1) {
        for (int left = age - 1; left != 0; --left) source->RunTick();
    }

    effectdata::Spawned(*source);   // its "spawn" reactions (FireBlast's and LightningBlast's scorch marks)
}

void EffectParticle::Pack(DeltaWriter& field) {
    Vector3 position = localPosition;
    field.WritePosition(position);
    int value = startTick;
    field.WriteDelta(&value, 4);
    field.WriteDelta(&type, 4);
    field.WriteFloat16(size);
    value = attachedTo;
    field.WriteDelta(&value, 4);
    field.WriteFloat16x3(velocity);
    field.WritePosition(target);
    value = unitId;
    field.WriteDelta(&value, 4);
    field.WriteFloat16(alpha);
    field.WriteFloat16(typeParam);
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
    MapObject* object = g_clientWorld.objectManager->FindObjectAs<MapObject>(static_cast<unsigned>(attachedTo));
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
    if (stream->version < kVersionEffectUnitId) return;
    stream->ReadValue(unitId);
    if (stream->version < kVersionEffectAlpha) return;
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
        Vector3 d = target - localPosition;
        float t;
        if (StepFraction(velocity, d, &t) != 0) {
            // Keeps the target's height above the ground while moving towards it.
            float aboveGround = target.z - OwningWorld()->RawSurfaceHeightAt(target.x, target.y);
            Vector3 p = d * t + localPosition;
            float floor = OwningWorld()->RawSurfaceHeightAt(p.x, p.y) + aboveGround;
            localPosition = {p.x, p.y, MaxKeepB(p.z, floor)};
            if (source != nullptr) source->localPosition = localPosition;
        }
    }
    const int lifetime = OneShotTicks(type);
    if (lifetime == 0) return true;
    int now = OwningWorld()->tick;
    return now <= startTick + lifetime;
}


void EffectWall::Reset() {
    localPosition = Vector3{0.0f, 0.0f, 0.0f};
    state.Empty();
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

int EffectWall::Unpack(int update, DeltaReader& field) {
    if (EffectParticle::Unpack(update, field) == 0) return 0;
    direction = field.ReadFloat16x3();
    if (!field.SnapshotConsumed()) return 0;
    if (source != nullptr) source->extent = direction;
    if (update == 0 && ParticleTypeOf(type) == ParticleType::Tendril) {
        // A scorch mark under the new wall: as long as the wall, as wide as half its size.
        float fadeAlpha = BelowOneOrOne(alpha * kScorchAlpha);
        float half = size * 0.5f;
        float shape[2];   // angle, outer
        WallScorchShape(direction, &half, shape);
        float inner = half + 0.3f;
        g_clientWorld.AddScorchMark(localPosition.x, localPosition.y, shape[1], inner, shape[0], fadeAlpha);
    }
    return 1;
}

void EffectWall::Pack(DeltaWriter& field) {
    Vector3 position = localPosition;
    field.WritePosition(position);
    int value = startTick;
    field.WriteDelta(&value, 4);
    field.WriteDelta(&type, 4);
    field.WriteFloat16(size);
    value = attachedTo;
    field.WriteDelta(&value, 4);
    field.WriteFloat16x3(velocity);
    field.WritePosition(target);
    value = unitId;
    field.WriteDelta(&value, 4);
    field.WriteFloat16(alpha);
    field.WriteFloat16(typeParam);
    field.WriteFloat16x3(direction);
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
    Effect::Reset();
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
        to.z += 1.0f;
    }
    Unit* source = UnitOrNull(OwningWorld()->ObjectWithId(fromObject));
    if (source != nullptr && OwningWorld()->IsServer() == 0) {
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

int EffectLightning::Unpack(int update, DeltaReader& field) {
    Vector3 position;
    position = field.ReadPosition();
    from = field.ReadPosition();
    to = field.ReadPosition();
    width = field.ReadFloat16();
    field.ReadDelta(&flag, 1);
    field.ReadDelta(&toObject, 4);
    field.ReadDelta(&fromObject, 4);
    if (!field.SnapshotConsumed()) return 0;
    UpdateEnds();
    localPosition = position;
    Start(update);
    return 1;
}

void EffectLightning::Pack(DeltaWriter& field) {
    Vector3 position = localPosition;
    field.WritePosition(position);
    field.WritePosition(from);
    field.WritePosition(to);
    field.WriteFloat16(width);
    field.WriteDelta(&flag, 1);
    int value = toObject;
    field.WriteDelta(&value, 4);
    value = fromObject;
    field.WriteDelta(&value, 4);
}

void EffectLightning::SetEnds(const Vector3& newFrom, const Vector3& newTo, float newWidth) {
    from = newFrom;
    to = newTo;
    width = newWidth;
    localPosition = (from + to) * 0.5f;
    MarkChanged();
}

void EffectLightning::Load(ByteStream* stream) {
    stream->ReadValue(localPosition);
    stream->ReadValue(from);
    stream->ReadValue(to);
    stream->ReadValue(width);
    stream->ReadValue(toObject);
    stream->ReadValue(fromObject);
    if (stream->version < kVersionLightningFlag) return;
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
