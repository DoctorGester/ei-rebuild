// Missiles: EffectMissile (the homing base), EffectArrow (a spell missile carrying a particle
// source, a light and a sound) and EffectArrowFigure (a bow shot drawn as a figure). See
// world_effect.h and docs/notes/effects.md ("Missiles").
#include "base_manager.h"
#include "camera.h"
#include "core_streams.h"
#include "game_singletons.h"
#include "game_strings.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "map_object_shadow.h"
#include "save_versions.h"
#include "unit.h"
#include "unit_ai.h"
#include "world.h"
#include "world_effect.h"

#include "lib/crt.h"
#include "lib/nan_audit.h"
#include "lib/pc24.h"

#include <bit>
#include <cmath>
#include <cstring>
#include <new>
#include <string>
#include <vector>

namespace {

constexpr float kMissileSpeed = 0.6666667f;     // per tick
constexpr float kGroundClearance = 1.0f;
constexpr float kLightDrift = 0.0001f;          // the light's velocity x (keeps it "moving")
constexpr float kSoundRadius = 30.0f;
constexpr std::uint32_t kLightFlags = lib::Bits(LightFlag::Specular | LightFlag::Strong);

std::uint32_t Bits(const float& f) {
    std::uint32_t u;
    u = std::bit_cast<std::uint32_t>(f);
    return u;
}

// The snapshot read against was consumed to its end (Position and Size of the stream, in that order).
bool SnapshotReadToEnd() {
    MemoryStream* snapshot = delta::g_readSnapshot;
    std::uint32_t position = snapshot->Position();
    return position == snapshot->Size();
}


} // namespace


int EffectMissile::Steer() {
    int flying = 1;
    CWorld* world = World();
    CMapObject* target = world->ObjectWithId(targetId);
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
        float distance = static_cast<float>(std::sqrt(LengthSquared2DUnrounded(velocity.y, velocity.x)));
        if (GetClassId() == ClassId::EffectArrowFigure) speed = speed + speed;
        EI_NAN_AUDIT(distance);
        if (distance < speed) {   // within one step
            flying = 0;
            arrived = 1;
        } else {
            float scale = speed / distance;
            velocity.x = scale * velocity.x;
            velocity.y = scale * velocity.y;
            velocity.z = scale * velocity.z;
        }
    }
    if (!world->IsServer()) {
        // Below the ground plus the clearance: climb by what is missing, at most half the clearance less.
        float floor = pc24::Add(world->RawSurfaceHeightAt(localPosition.x, localPosition.y), kGroundClearance);
        if (floor > localPosition.z) {
            float missing = floor - localPosition.z;
            float limited = missing >= kGroundClearance ? kGroundClearance : missing;
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
    localPosition.x = localPosition.x + velocity.x;
    localPosition.y = localPosition.y + velocity.y;
    localPosition.z = localPosition.z + velocity.z;
    if (!WorldContext()->IsServer()) MoveParts();
    return flying;
}

void EffectMissile::Reset() {
    CEffect::Reset();
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
    const char* name = soundName.c_str();
    std::memcpy(sound.name, name, std::strlen(name) + 1);     // unbounded, like the original
    sound.radius = kSoundRadius;
    sound.looping = 1;
    particle.Start(update);
    light.Start(update);
    sound.Start(update);
}

int EffectArrow::Unpack(int update, DeltaField* field) {
    field->ReadDelta(&targetId, 4);
    field->ReadDelta(&casterId, 4);
    soundName = field->ReadString();
    field->ReadFloat16x3(&lightColor.x);
    field->ReadFloat16(&lightRange);
    field->ReadDelta(&particleType, 4);
    field->ReadFloat16(&particleSize);
    Vector3 position;
    field->ReadPosition(&position);
    field->ReadFloat16x3(&velocity.x);
    field->ReadDelta(&fromCaster, 1);
    if (!SnapshotReadToEnd()) return 0;
    if (fromCaster != 0) {
        auto* caster = World()->FindUnit(casterId);
        if (caster != nullptr) {
            Vector3 point;
            position = *caster->EffectPoint(&point);
        }
    }
    localPosition = position;
    Start(update);
    return 1;
}

void EffectArrow::Pack(DeltaField* field) {
    Steer();
    Vector3 position = localPosition;
    int value = targetId;
    field->WriteDelta(&value, 4);
    value = casterId;
    field->WriteDelta(&value, 4);
    field->WriteStringBoth(soundName);
    field->WriteFloat16x3(&lightColor.x);
    field->WriteFloat16(Bits(lightRange));
    field->WriteDelta(&particleType, 4);
    field->WriteFloat16(Bits(particleSize));
    field->WritePosition(position);
    field->WriteFloat16x3(&velocity.x);
    field->WriteDelta(&fromCaster, 1);
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
    if (static_cast<int>(s->version) < kVersionMissileCaster) return;
    s->ReadValue(casterId);
    s->ReadValue(fromCaster);
}

void EffectArrow::Save(ByteStream* s) {
    std::uint32_t copy;
    s->WriteValue(localPosition);
    std::memcpy(&copy, &targetId, 4);
    s->WriteValue(copy);
    s->WriteValue(velocity);
    std::memcpy(&copy, &arrived, 4);
    s->WriteValue(copy);
    s->WriteString(soundName);
    s->WriteValue(lightColor);
    copy = std::bit_cast<std::uint32_t>(lightRange);
    s->WriteValue(copy);
    s->WriteValue(particleType);
    copy = std::bit_cast<std::uint32_t>(particleSize);
    s->WriteValue(copy);
    std::memcpy(&copy, &casterId, 4);
    s->WriteValue(copy);
    std::uint8_t flag = fromCaster;
    s->WriteValue(flag);
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
    localPosition.x = localPosition.x + velocity.x;
    localPosition.y = localPosition.y + velocity.y;
    localPosition.z = localPosition.z + velocity.z;
    if (!WorldContext()->IsServer()) MoveParts();
    CWorld* world = World();
    if (arrived != 0 && world->IsServer()) {
        // The caster is looked up before the target is tested, as the original does.
        CMapObject* target = world->ObjectWithId(targetId);
        CUnit* caster = world->FindUnit(casterId);
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
    std::string model = game::ArrowFigureModelText();
    std::string texture = game::ArrowFigureTextureText();
    int objectId = g_clientWorld.nextClientObjectId--;
    auto* object = g_clientWorld.objectManager->CreateObjectAs<CMapObject>(MapObjectType::StuffObject, objectId);
    figure.Assign(object);
    // No null test (the original has none either). The figure the object holds is read before the
    // figure is made, like the original.
    CObject3D* currentFigure = figure;
    Object3DClientSpecific* attached = currentFigure->ClientSpecific();
    currentFigure->localPosition = localPosition;
    std::vector<std::string> parts;
    currentFigure->MakeFigure(model, &parts, 1);
    attached->RegisterPartMaterials();
    currentFigure->PlayAnimation(nullptr);
    currentFigure->RestartPose();
    attached->SetSkins(texture, game::kEmptyText);
    g_clientWorld.AddObject(figure);
}

int EffectArrowFigure::Unpack(int update, DeltaField* field) {
    fromCaster = 0;
    field->ReadDelta(&targetId, 4);
    field->ReadDelta(&casterId, 4);
    Vector3 position;
    field->ReadPosition(&position);
    field->ReadFloat16x3(&velocity.x);
    field->ReadDelta(&fromCaster, 1);
    if (!SnapshotReadToEnd()) return 0;
    if (fromCaster != 0) {
        auto* caster = World()->FindUnit(casterId);
        if (caster != nullptr) {
            Vector3 point;
            position = *caster->EffectPoint(&point);
        }
    }
    localPosition = position;
    Start(update);
    return 1;
}

void EffectArrowFigure::Pack(DeltaField* field) {
    Steer();
    Vector3 position = localPosition;
    int value = targetId;
    field->WriteDelta(&value, 4);
    value = casterId;
    field->WriteDelta(&value, 4);
    field->WritePosition(position);
    field->WriteFloat16x3(&velocity.x);
    field->WriteDelta(&fromCaster, 1);
}

void EffectArrowFigure::Load(ByteStream* s) {
    s->ReadValue(localPosition);
    s->ReadValue(targetId);
    s->ReadValue(casterId);
    s->ReadValue(velocity);
    s->ReadValue(arrived);
    LoadRef(figure, s, WorldContext());
    s->ReadValue(hit);
    s->ReadValue(fromCaster);
}

void EffectArrowFigure::Save(ByteStream* s) {
    std::uint32_t copy;
    s->WriteValue(localPosition);
    std::memcpy(&copy, &targetId, 4);
    s->WriteValue(copy);
    std::memcpy(&copy, &casterId, 4);
    s->WriteValue(copy);
    s->WriteValue(velocity);
    std::memcpy(&copy, &arrived, 4);
    s->WriteValue(copy);
    SaveRef(figure, s);
    s->WriteValue(hit);
    std::uint8_t flag = fromCaster;
    s->WriteValue(flag);
}

EffectArrowFigure::EffectArrowFigure() {
    Reset();
}

EffectArrowFigure::~EffectArrowFigure() { Reset(); }
