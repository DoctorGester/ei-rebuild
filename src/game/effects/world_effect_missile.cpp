// Missiles: EffectMissile (the homing base), EffectArrow (a spell missile carrying a particle
// source, a light and a sound) and EffectArrowFigure (a bow shot drawn as a figure). See
// world_effect.h and docs/notes/effects.md ("Missiles").
#include "engine/core/base_manager.h"
#include "engine/scene/camera.h"
#include "engine/core/core_streams.h"
#include "game/app/singletons.h"
#include "engine/scene/light.h"
#include "game/objects/map_object.h"
#include "game/objects/object3d.h"
#include "game/objects/map_object_shadow.h"
#include "game/saves/save_versions.h"
#include "game/units/unit.h"
#include "game/ai/unit_ai.h"
#include "game/world/world.h"
#include "game/effects/world_effect.h"

#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/str.h"

#include <cmath>
#include <new>
#include <string>
#include <vector>

namespace {

constexpr float kMissileSpeed = 0.6666667f;     // per tick
constexpr float kGroundClearance = 1.0f;
constexpr float kLightDrift = 0.0001f;          // the light's velocity x (keeps it "moving")
constexpr float kSoundRadius = 30.0f;
constexpr std::uint32_t kLightFlags = lib::Bits(LightFlag::Specular | LightFlag::Strong);

} // namespace


int EffectMissile::Steer() {
    int flying = 1;
    World* world = OwningWorld();
    MapObject* target = world->ObjectWithId(targetId);
    if (target == nullptr) {
        velocity = Vector3{0.0f, 0.0f, 0.0f};
        flying = 0;
        arrived = 1;
    } else {
        // At the middle of the target's bounding box.
        float aimZ = target->bounds.boxMax.z * 0.5f + target->bounds.center.z + target->localPosition.z;
        velocity.x = target->localPosition.x - localPosition.x;
        velocity.y = target->localPosition.y - localPosition.y;
        velocity.z = aimZ - localPosition.z;
        float speed = kMissileSpeed;
        float distance = std::sqrt(LengthSquared2D(velocity.y, velocity.x));
        if (GetClassId() == ClassId::EffectArrowFigure) speed = speed + speed;
        if (distance < speed) {   // within one step
            flying = 0;
            arrived = 1;
        } else {
            float scale = speed / distance;
            velocity *= scale;
        }
    }
    if (!world->IsServer()) {
        // Below the ground plus the clearance: climb by what is missing, at most half the clearance less.
        float floor = world->RawSurfaceHeightAt(localPosition.x, localPosition.y) + kGroundClearance;
        if (floor > localPosition.z) {
            float missing = floor - localPosition.z;
            float limited = MinKeepA(missing, kGroundClearance);
            velocity.z = missing - limited * 0.5f;
        }
    }
    return flying;
}

void EffectMissile::SetTarget(int objectId) {
    arrived = 0;
    targetId = objectId;
}



bool EffectArrow::RunTick() {
    fromCaster = 0;
    int flying = Steer();
    localPosition += velocity;
    if (!OwningWorld()->IsServer()) MoveParts();
    return flying;
}

void EffectMissile::Reset() {
    Effect::Reset();
    arrived = 0;
    targetId = -1;
    fromCaster = 0;
}

void EffectArrow::Reset() {
    EffectMissile::Reset();
    particle.Reset();
    light.Reset();
    sound.Reset();
    casterId = -1;
    lightColor = Vector3{0.0f, 0.0f, 0.0f};
    lightRange = 0.0f;
    particleType = 0;
    particleSize = 1.0f;
}

void EffectArrow::MoveParts() {
    particle.localPosition = localPosition;
    light.localPosition = localPosition;
    sound.localPosition = localPosition;
    particle.Start(1);
    light.Start(1);
    sound.Start(1);
}

void EffectArrow::Start(int update) {
    particle.localPosition = localPosition;
    light.localPosition = localPosition;
    sound.localPosition = localPosition;
    particle.type = particleType;
    particle.size = particleSize;
    light.color = lightColor;
    light.range = lightRange;
    Vector3 drift{kLightDrift, 0.0f, 0.0f};
    light.SetVelocity(drift);
    light.lightFlags = kLightFlags;
    str::CopyInto(sound.name, soundName);
    sound.radius = kSoundRadius;
    sound.looping = 1;
    particle.Start(update);
    light.Start(update);
    sound.Start(update);
}

int EffectArrow::Unpack(int update, DeltaReader& field) {
    field.ReadDelta(&targetId, 4);
    field.ReadDelta(&casterId, 4);
    soundName = field.ReadString();
    lightColor = field.ReadFloat16x3();
    lightRange = field.ReadFloat16();
    field.ReadDelta(&particleType, 4);
    particleSize = field.ReadFloat16();
    Vector3 position;
    position = field.ReadPosition();
    velocity = field.ReadFloat16x3();
    field.ReadDelta(&fromCaster, 1);
    if (!field.SnapshotConsumed()) return 0;
    if (fromCaster != 0) {
        auto* caster = OwningWorld()->FindUnit(casterId);
        if (caster != nullptr) {
            position = caster->EffectPoint();
        }
    }
    localPosition = position;
    Start(update);
    return 1;
}

void EffectArrow::Pack(DeltaWriter& field) {
    Steer();
    Vector3 position = localPosition;
    int value = targetId;
    field.WriteDelta(&value, 4);
    value = casterId;
    field.WriteDelta(&value, 4);
    field.WriteStringBoth(soundName);
    field.WriteFloat16x3(lightColor);
    field.WriteFloat16(lightRange);
    field.WriteDelta(&particleType, 4);
    field.WriteFloat16(particleSize);
    field.WritePosition(position);
    field.WriteFloat16x3(velocity);
    field.WriteDelta(&fromCaster, 1);
}

void EffectArrow::Load(ByteStream* s) {
    s->ReadValue(localPosition);
    s->ReadValue(targetId);
    s->ReadValue(velocity);
    s->ReadValue(arrived);
    soundName = s->ReadString();
    s->ReadValue(lightColor);
    s->ReadValue(lightRange);
    s->ReadValue(particleType);
    s->ReadValue(particleSize);
    if (s->version < kVersionMissileCaster) return;
    s->ReadValue(casterId);
    s->ReadValue(fromCaster);
}

void EffectArrow::Save(ByteStream* s) {
    s->WriteValue(localPosition);
    s->WriteValue(targetId);
    s->WriteValue(velocity);
    s->WriteValue(arrived);
    s->WriteString(soundName);
    s->WriteValue(lightColor);
    s->WriteValue(lightRange);
    s->WriteValue(particleType);
    s->WriteValue(particleSize);
    s->WriteValue(casterId);
    s->WriteValue(fromCaster);
}

EffectArrow::EffectArrow() { Reset(); }

EffectArrow::~EffectArrow() { Reset(); }

// The abstract missile's destructor (no live object has its table).
EffectMissile::~EffectMissile() = default;

// --- EffectArrowFigure --------------------------------------------------------------------------

void EffectArrowFigure::Reset() {
    EffectMissile::Reset();
    if (figure != nullptr) g_clientWorld.RemoveObject(figure->id);
    hit = DamageInfo{};
    hit.multiplier = 1.0f;
    figure.Reset();
    casterId = -1;
}

bool EffectArrowFigure::RunTick() {
    fromCaster = 0;
    Steer();
    localPosition += velocity;
    if (!OwningWorld()->IsServer()) MoveParts();
    World* world = OwningWorld();
    if (arrived != 0 && world->IsServer()) {
        // The caster is looked up before the target is tested, as the original does.
        MapObject* target = world->ObjectWithId(targetId);
        Unit* caster = world->FindUnit(casterId);
        if (target != nullptr) target->ReceiveHit(&hit, caster, caster);
    }
    return arrived == 0;
}

void EffectArrowFigure::MoveParts() {
    if (figure == nullptr) return;
    g_clientWorld.objectGrid.Move(figure, &localPosition.x);
    figure->RollTowards(velocity);
}

void EffectArrowFigure::Start(int update) {
    if (update != 0) {
        MoveParts();
        return;
    }
    std::string model = "efar1";
    std::string texture = "arrow01";
    int objectId = g_clientWorld.nextClientObjectId--;
    auto* object = g_clientWorld.objectManager->CreateObjectAs<MapObject>(MapObjectType::StuffObject, objectId);
    figure.Assign(object);
    // No null test (the original has none either). The figure the object holds is read before the
    // figure is made, like the original.
    Object3D* currentFigure = figure;
    Object3DClientSpecific* attached = currentFigure->ClientSpecific();
    currentFigure->localPosition = localPosition;
    std::vector<std::string> parts;
    currentFigure->MakeFigure(model, &parts);
    attached->RegisterPartMaterials();
    currentFigure->PlayAnimation(nullptr);
    currentFigure->RestartPose();
    attached->SetSkins(texture, "");
    g_clientWorld.AddObject(figure);
}

int EffectArrowFigure::Unpack(int update, DeltaReader& field) {
    fromCaster = 0;
    field.ReadDelta(&targetId, 4);
    field.ReadDelta(&casterId, 4);
    Vector3 position;
    position = field.ReadPosition();
    velocity = field.ReadFloat16x3();
    field.ReadDelta(&fromCaster, 1);
    if (!field.SnapshotConsumed()) return 0;
    if (fromCaster != 0) {
        auto* caster = OwningWorld()->FindUnit(casterId);
        if (caster != nullptr) {
            position = caster->EffectPoint();
        }
    }
    localPosition = position;
    Start(update);
    return 1;
}

void EffectArrowFigure::Pack(DeltaWriter& field) {
    Steer();
    Vector3 position = localPosition;
    int value = targetId;
    field.WriteDelta(&value, 4);
    value = casterId;
    field.WriteDelta(&value, 4);
    field.WritePosition(position);
    field.WriteFloat16x3(velocity);
    field.WriteDelta(&fromCaster, 1);
}

void EffectArrowFigure::Load(ByteStream* s) {
    s->ReadValue(localPosition);
    s->ReadValue(targetId);
    s->ReadValue(casterId);
    s->ReadValue(velocity);
    s->ReadValue(arrived);
    LoadRef(figure, s, OwningWorld());
    s->ReadValue(hit);
    s->ReadValue(fromCaster);
}

void EffectArrowFigure::Save(ByteStream* s) {
    s->WriteValue(localPosition);
    s->WriteValue(targetId);
    s->WriteValue(casterId);
    s->WriteValue(velocity);
    s->WriteValue(arrived);
    s->WriteId(figure);
    s->WriteValue(hit);
    s->WriteValue(fromCaster);
}

EffectArrowFigure::EffectArrowFigure() {
    Reset();
}

EffectArrowFigure::~EffectArrowFigure() { Reset(); }
