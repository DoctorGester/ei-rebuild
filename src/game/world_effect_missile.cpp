// Missiles: CEffectMissile (the homing base), CEffectArrow (a spell missile carrying a particle
// source, a light and a sound) and CEffectArrowFigure (a bow shot drawn as a figure). See
// world_effect.h and docs/notes/effects.md ("Missiles").
#include "core_streams.h"
#include "map_object_shadow.h"
#include "unit_ai.h"
#include "world_effect.h"

#include "base_manager.h"
#include "camera.h"
#include "game_strings.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "unit.h"
#include "world.h"

#include "../lib/crt.h"
#include "game_singletons.h"
#include "lib/nan_audit.h"

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
constexpr std::uint32_t kLightFlags = 0x840;
constexpr int kClassArrowFigure = 0x5B;

CWorld* WorldOf(const CEngineObject* object) { return ObjectWorld(object); }

std::uint32_t Bits(const float& f) {
    std::uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

// The snapshot read against was consumed to its end (stream slots 5 and 4, in that order).
bool SnapshotReadToEnd() {
    CMemoryStream* snapshot = delta::g_readSnapshot;
    std::uint32_t position = snapshot->Position();
    return position == snapshot->Size();
}

} // namespace


int CEffectMissile::Steer() {
    int flying = 1;
    CWorld* world = WorldOf(this);
    CMapObject* target = world->ObjectWithId(targetId_);
    if (target == nullptr) {
        velocity_ = Vector3{0.0f, 0.0f, 0.0f};
        flying = 0;
        arrived_ = 1;
    } else {
        // At the middle of the target's bounding box.
        float aimZ = target->bounds.boxMax.z * 0.5f + target->bounds.center.z + target->localPosition_.z;
        velocity_.x = target->localPosition_.x - localPosition_.x;
        velocity_.y = target->localPosition_.y - localPosition_.y;
        velocity_.z = aimZ - localPosition_.z;
        float speed = kMissileSpeed;
        float distance = static_cast<float>(std::sqrt(LengthSquared2DUnrounded(velocity_.y, velocity_.x)));
        if (GetClassId() == kClassArrowFigure) speed = speed + speed;
        EI_NAN_AUDIT(distance);
        if (distance < speed) {   // within one step
            flying = 0;
            arrived_ = 1;
        } else {
            float scale = speed / distance;
            velocity_.x = scale * velocity_.x;
            velocity_.y = scale * velocity_.y;
            velocity_.z = scale * velocity_.z;
        }
    }
    if (!world->IsServer()) {
        // Below the ground plus the clearance: climb by what is missing, at most half the clearance less.
        double floor = world->RawSurfaceHeightAt(localPosition_.x, localPosition_.y) + kGroundClearance;
        if (floor > localPosition_.z) {
            float missing = static_cast<float>(floor - localPosition_.z);
            float limited = missing >= kGroundClearance ? kGroundClearance : missing;
            velocity_.z = missing - limited * 0.5f;
        }
    }
    return flying;
}

void CEffectMissile::SetTarget(int objectId) {
    arrived_ = 0;
    targetId_ = objectId;
}



int CEffectArrow::RunTick() {
    fromCaster_ = 0;
    int flying = Steer();
    localPosition_.x = localPosition_.x + velocity_.x;
    localPosition_.y = localPosition_.y + velocity_.y;
    localPosition_.z = localPosition_.z + velocity_.z;
    if (!WorldContext()->IsServer()) MoveParts();
    return flying;
}

void CEffectMissile::Reset() {
    CEffect::Reset();
    arrived_ = 0;
    targetId_ = -1;
    fromCaster_ = 0;
}

void CEffectArrow::Reset() {
    CEffectMissile::Reset();
    particle_.Reset();
    light_.Reset();
    sound_.Reset();
    casterId_ = -1;
    lightColor_ = Vector3{0.0f, 0.0f, 0.0f};
    lightRange_ = 0.0f;
    particleType_ = 0;
    particleSize_ = 1.0f;
}

void CEffectArrow::MoveParts() {
    particle_.localPosition_ = localPosition_;
    light_.localPosition_ = localPosition_;
    sound_.localPosition_ = localPosition_;
    particle_.Start(1);
    light_.Start(1);
    sound_.Start(1);
}

void CEffectArrow::Start(int update) {
    particle_.localPosition_ = localPosition_;
    light_.localPosition_ = localPosition_;
    sound_.localPosition_ = localPosition_;
    particle_.type_ = particleType_;
    std::memcpy(&particle_.size_, &particleSize_, 4);
    light_.color_ = lightColor_;
    std::memcpy(&light_.range_, &lightRange_, 4);
    Vector3 drift{kLightDrift, 0.0f, 0.0f};
    light_.SetVelocity(&drift);
    light_.lightFlags_ = kLightFlags;
    const char* name = soundName_.c_str();
    std::memcpy(sound_.name_, name, std::strlen(name) + 1);   // unbounded, like the original
    sound_.radius_ = kSoundRadius;
    sound_.looping_ = 1;
    particle_.Start(update);
    light_.Start(update);
    sound_.Start(update);
}

int CEffectArrow::Unpack(int update, DeltaField* field) {
    field->ReadDelta(&targetId_, 4);
    field->ReadDelta(&casterId_, 4);
    {
        std::string name;
        field->ReadString(&name);
        soundName_ = name;
    }
    field->ReadFloat16x3(&lightColor_.x);
    field->ReadFloat16(&lightRange_);
    field->ReadDelta(&particleType_, 4);
    field->ReadFloat16(&particleSize_);
    Vector3 position;
    field->ReadPosition(&position);
    field->ReadFloat16x3(&velocity_.x);
    field->ReadDelta(&fromCaster_, 1);
    if (!SnapshotReadToEnd()) return 0;
    if (fromCaster_ != 0) {
        auto* caster = ObjectWorld(this)->FindUnit(casterId_);
        if (caster != nullptr) {
            Vector3 point;
            position = *caster->EffectPoint(&point);
        }
    }
    localPosition_ = position;
    Start(update);
    return 1;
}

void CEffectArrow::Pack(DeltaField* field) {
    Steer();
    Vector3 position = localPosition_;
    int value = targetId_;
    field->WriteDelta(&value, 4);
    value = casterId_;
    field->WriteDelta(&value, 4);
    field->WriteStringBoth(soundName_);
    field->WriteFloat16x3(&lightColor_.x);
    field->WriteFloat16(Bits(lightRange_));
    field->WriteDelta(&particleType_, 4);
    field->WriteFloat16(Bits(particleSize_));
    field->WritePosition(&position);
    field->WriteFloat16x3(&velocity_.x);
    field->WriteDelta(&fromCaster_, 1);
}

void CEffectArrow::Load(CStream* s) {
    s->Read(&localPosition_, 12);
    s->Read(&targetId_, 4);
    s->Read(&velocity_, 12);
    s->Read(&arrived_, 4);
    s->ReadInto(&soundName_);
    s->Read(&lightColor_, 12);
    s->Read(&lightRange_, 4);
    s->Read(&particleType_, 4);
    s->Read(&particleSize_, 4);
    if (static_cast<int>(s->version_) < 0x43) return;
    s->Read(&casterId_, 4);
    s->Read(&fromCaster_, 1);
}

void CEffectArrow::Save(CStream* s) {
    std::uint32_t copy;
    s->Write(&localPosition_, 12);
    std::memcpy(&copy, &targetId_, 4);
    s->Write(&copy, 4);
    s->Write(&velocity_, 12);
    std::memcpy(&copy, &arrived_, 4);
    s->Write(&copy, 4);
    s->WriteString(soundName_);
    s->Write(&lightColor_, 12);
    std::memcpy(&copy, &lightRange_, 4);
    s->Write(&copy, 4);
    s->Write(&particleType_, 4);
    std::memcpy(&copy, &particleSize_, 4);
    s->Write(&copy, 4);
    std::memcpy(&copy, &casterId_, 4);
    s->Write(&copy, 4);
    std::uint8_t flag = fromCaster_;
    s->Write(&flag, 1);
}

CEffectArrow::CEffectArrow() { Reset(); }

CEffectArrow::~CEffectArrow() { Reset(); }

// The abstract missile's destructor (no live object has its table).
CEffectMissile::~CEffectMissile() = default;

// --- CEffectArrowFigure --------------------------------------------------------------------------

namespace {
constexpr int kClassFigureObject = 0x38;   // the object manager's class of the drawn arrow
}


void CEffectArrowFigure::Reset() {
    CEffectMissile::Reset();
    if (figure_ != nullptr) TheClientWorld()->RemoveObject(figure_->id_);
    hit_ = DamageInfo{};
    hit_.multiplier = 1.0f;
    if (figure_ != nullptr) {
        figure_->Release();
        figure_ = nullptr;
    }
    casterId_ = -1;
}

int CEffectArrowFigure::RunTick() {
    fromCaster_ = 0;
    Steer();
    localPosition_.x = localPosition_.x + velocity_.x;
    localPosition_.y = localPosition_.y + velocity_.y;
    localPosition_.z = localPosition_.z + velocity_.z;
    if (!WorldContext()->IsServer()) MoveParts();
    CWorld* world = WorldOf(this);
    if (arrived_ != 0 && world->IsServer()) {
        // The caster is looked up before the target is tested, as the original does.
        CMapObject* target = world->ObjectWithId(targetId_);
        CUnit* caster = world->FindUnit(casterId_);
        if (target != nullptr) target->ReceiveHit(&hit_, caster, caster);
    }
    return arrived_ == 0 ? 1 : 0;
}

void CEffectArrowFigure::MoveParts() {
    if (figure_ == nullptr) return;
    TheClientWorld()->objectGrid_.Move(figure_, &localPosition_.x);
    figure_->RollTowards(velocity_);
}

void CEffectArrowFigure::Start(int update) {
    if (update != 0) {
        MoveParts();
        return;
    }
    std::string model = game::ArrowFigureModelText();
    std::string texture = game::ArrowFigureTextureText();
    int id = NextClientObjectId()--;
    auto* object = TheObjectManager()->CreateObjectAs<CMapObject>(kClassFigureObject, id);
    if (object != figure_) {
        figure_.Reset();
        if (object != nullptr) ++object->refCount_;
        figure_ = object;
    }
    // No null test (the original has none either). The figure the object holds is read before the
    // figure is made, like the original.
    CObject3D* figure = figure_;
    CObject3DClientSpecific* attached = figure->ClientSpecific();
    std::memcpy(&figure->localPosition_, &localPosition_, sizeof(figure->localPosition_));
    std::vector<std::string> parts;
    figure->MakeFigure(model.c_str(), &parts, 1);
    attached->RegisterPartMaterials();
    figure->PlayAnimationSecond(0, nullptr);   // (the model name is passed as the unused argument)
    figure->RestartPose();
    attached->SetSkins(texture.c_str(), game::kEmptyText);
    TheClientWorld()->AddObject(figure_);
}

int CEffectArrowFigure::Unpack(int update, DeltaField* field) {
    fromCaster_ = 0;
    field->ReadDelta(&targetId_, 4);
    field->ReadDelta(&casterId_, 4);
    Vector3 position;
    field->ReadPosition(&position);
    field->ReadFloat16x3(&velocity_.x);
    field->ReadDelta(&fromCaster_, 1);
    if (!SnapshotReadToEnd()) return 0;
    if (fromCaster_ != 0) {
        auto* caster = ObjectWorld(this)->FindUnit(casterId_);
        if (caster != nullptr) {
            Vector3 point;
            position = *caster->EffectPoint(&point);
        }
    }
    localPosition_ = position;
    Start(update);
    return 1;
}

void CEffectArrowFigure::Pack(DeltaField* field) {
    Steer();
    Vector3 position = localPosition_;
    int value = targetId_;
    field->WriteDelta(&value, 4);
    value = casterId_;
    field->WriteDelta(&value, 4);
    field->WritePosition(&position);
    field->WriteFloat16x3(&velocity_.x);
    field->WriteDelta(&fromCaster_, 1);
}

void CEffectArrowFigure::Load(CStream* s) {
    s->Read(&localPosition_, 12);
    s->Read(&targetId_, 4);
    s->Read(&casterId_, 4);
    s->Read(&velocity_, 12);
    s->Read(&arrived_, 4);
    figure_.Load(s, WorldContext());
    s->Read(&hit_, sizeof(hit_));
    s->Read(&fromCaster_, 1);
}

void CEffectArrowFigure::Save(CStream* s) {
    std::uint32_t copy;
    s->Write(&localPosition_, 12);
    std::memcpy(&copy, &targetId_, 4);
    s->Write(&copy, 4);
    std::memcpy(&copy, &casterId_, 4);
    s->Write(&copy, 4);
    s->Write(&velocity_, 12);
    std::memcpy(&copy, &arrived_, 4);
    s->Write(&copy, 4);
    figure_.Save(s);
    s->Write(&hit_, sizeof(hit_));
    std::uint8_t flag = fromCaster_;
    s->Write(&flag, 1);
}

CEffectArrowFigure::CEffectArrowFigure() {
    figure_ = nullptr;
    Reset();
}

CEffectArrowFigure::~CEffectArrowFigure() {
    Reset();
    Reset();   // again, the virtual
    if (figure_ != nullptr) figure_->Release();
    figure_ = nullptr;
}
