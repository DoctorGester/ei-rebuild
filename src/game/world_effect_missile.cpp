// Missiles: CEffectMissile (the homing base), CEffectArrow (a spell missile carrying a particle
// source, a light and a sound) and CEffectArrowFigure (a bow shot drawn as a figure). See
// world_effect.h and docs/notes/effects.md ("Missiles").
#include "core_streams.h"
#include "map_object_shadow.h"
#include "unit_ai.h"
#include "world_effect.h"

#include "base_manager.h"
#include "camera.h"
#include "console.h"
#include "game_strings.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "originals_engine.h"
#include "originals_units.h"
#include "originals_world.h"
#include "unit.h"
#include "world.h"

#include "../lib/crt.h"
#include "ei/hook.h"
#include "ei/vcall.h"
#include "game_data.h"
#include "game_singletons.h"
#include "lib/nan_audit.h"

#include <cmath>
#include <cstring>
#include <new>

using ei::SetVtable;

namespace {

constexpr float kMissileSpeed = 0.6666667f;     // 007433D4, per tick
constexpr float kGroundClearance = 1.0f;        // 007433D8
constexpr float kLightDrift = 0.0001f;          // the light's velocity x (keeps it "moving")
constexpr float kSoundRadius = 30.0f;
constexpr std::uint32_t kLightFlags = 0x840;
constexpr int kClassArrowFigure = 0x5B;

// The missile base's vtable, stored while the base part is built.
std::uintptr_t MissileVtable() { return ei::DataId(0x00743754); }

CWorld* WorldOf(const CEngineObject* object) { return ObjectWorld(object); }

std::uint32_t Bits(const float& f) {
    std::uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

// The snapshot read against was consumed to its end (stream slots 5 and 4, in that order).
bool SnapshotReadToEnd() {
    BitStream* snapshot = delta::ReadSnapshot();
    std::uint32_t position = snapshot->Position();
    return position == snapshot->Size();
}

} // namespace

// --- CEffectMissile ------------------------------------------------------------------------------

int CEffectMissile::Steer() {
    int flying = 1;
    CWorld* world = WorldOf(this);
    auto* target = static_cast<CMapObject*>(world->ObjectWithId(targetId_));
    if (target == nullptr) {
        velocity_ = Vector3{0.0f, 0.0f, 0.0f};
        flying = 0;
        arrived_ = 1;
    } else {
        // At the middle of the target's bounding box.
        float aimZ = target->above.z * 0.5f + target->center.z + target->position.z;
        velocity_.x = target->position.x - localPosition_.x;
        velocity_.y = target->position.y - localPosition_.y;
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

// --- CEffectArrow --------------------------------------------------------------------------------

std::uintptr_t CEffectArrow::Vtable() { return ei::DataId(0x007436F0); }

int CEffectArrow::Tick() {
    fromCaster_ = 0;
    int flying = Steer();
    localPosition_.x = localPosition_.x + velocity_.x;
    localPosition_.y = localPosition_.y + velocity_.y;
    localPosition_.z = localPosition_.z + velocity_.z;
    if (!WorldContext()->IsServer()) MoveParts();
    return flying;
}

void CEffectMissile::ResetMissile() {
    ResetBase();
    arrived_ = 0;
    targetId_ = -1;
    fromCaster_ = 0;
}

void CEffectArrow::Reset() {
    ResetMissile();
    Particle().Reset();
    Light().Reset();
    Sound().Reset();
    casterId_ = -1;
    lightColor_ = Vector3{0.0f, 0.0f, 0.0f};
    lightRange_ = 0.0f;
    particleType_ = 0;
    particleSize_ = 1.0f;
}

void CEffectArrow::MoveParts() {
    Particle().localPosition_ = localPosition_;
    Light().localPosition_ = localPosition_;
    Sound().localPosition_ = localPosition_;
    Particle().Start(1);
    Light().Start(1);
    Sound().Start(1);
}

void CEffectArrow::Start(int update) {
    Particle().localPosition_ = localPosition_;
    Light().localPosition_ = localPosition_;
    Sound().localPosition_ = localPosition_;
    Particle().type_ = particleType_;
    std::memcpy(&Particle().size_, &particleSize_, 4);
    Light().color_ = lightColor_;
    std::memcpy(&Light().range_, &lightRange_, 4);
    Vector3 drift{kLightDrift, 0.0f, 0.0f};
    Light().SetVelocity(&drift);
    Light().lightFlags_ = kLightFlags;
    const char* name = soundName_;
    std::memcpy(Sound().name_, name, std::strlen(name) + 1);   // unbounded, as the original
    Sound().radius_ = kSoundRadius;
    Sound().looping_ = 1;
    Particle().Start(update);
    Light().Start(update);
    Sound().Start(update);
}

int CEffectArrow::Unpack(int update, DeltaField* field) {
    field->ReadDelta(&targetId_, 4);
    field->ReadDelta(&casterId_, 4);
    {
        mfc::CString name(mfc::CString::kUnset);
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
        auto* caster = WorldFindUnit(ObjectWorld(this), nullptr, casterId_);
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

void CEffectArrow::LoadState(CStream* s) {
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

void CEffectArrow::SaveState(CStream* s) {
    std::uint32_t copy;
    s->Write(&localPosition_, 12);
    std::memcpy(&copy, &targetId_, 4);
    s->Write(&copy, 4);
    s->Write(&velocity_, 12);
    std::memcpy(&copy, &arrived_, 4);
    s->Write(&copy, 4);
    const char* name = soundName_;
    s->Write(name, static_cast<std::uint32_t>(std::strlen(name) + 1));
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

// The constructor builds the base under the missile's vtable, then the three parts with their own
// constructor bodies (inlined in the original), the empty name, and ends with Reset.
CEffectArrow* CEffectArrow::Construct() {
    ConstructEffect();
    SetVtable(this, MissileVtable());
    Particle().Construct();
    Light().Construct();
    Sound().Construct();
    new (&soundName_) mfc::CString;
    SetVtable(this, Vtable());
    Reset();
    return this;
}

// The destructor resets (slot 18), then destroys the name, the sound, the light, the particle part
// and the base.
void CEffectArrow::Destruct() {
    SetVtable(this, Vtable());
    Reset();
    soundName_.~CString();
    Sound().Destruct();
    Light().Destruct();
    Particle().Destruct();
    DestructEffect();
}

CEffectArrow* CEffectArrow::DeletingDestruct(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}

// --- CEffectArrowFigure --------------------------------------------------------------------------

namespace {
constexpr int kClassFigureObject = 0x38;   // the object manager's class of the drawn arrow
}

std::uintptr_t CEffectArrowFigure::Vtable() { return ei::DataId(0x007437B8); }

void CEffectArrowFigure::Reset() {
    ResetMissile();
    if (figure_ != nullptr) TheClientWorld()->RemoveObject(figure_->id_);
    hit_ = DamageInfo{};
    hit_.multiplier = 1.0f;
    if (figure_ != nullptr) {
        figure_->Release();
        figure_ = nullptr;
    }
    casterId_ = -1;
}

int CEffectArrowFigure::Tick() {
    fromCaster_ = 0;
    Steer();
    localPosition_.x = localPosition_.x + velocity_.x;
    localPosition_.y = localPosition_.y + velocity_.y;
    localPosition_.z = localPosition_.z + velocity_.z;
    if (!WorldContext()->IsServer()) MoveFigure();
    CWorld* world = WorldOf(this);
    if (arrived_ != 0 && world->IsServer()) {
        // The caster is looked up before the target is tested, as the original does.
        CEngineObject* target = world->ObjectWithId(targetId_);
        CEngineObject* caster = WorldFindUnit(world, nullptr, casterId_);
        if (target != nullptr) ObjectSlots(target)->ReceiveHit(&hit_, caster, caster);
    }
    return arrived_ == 0 ? 1 : 0;
}

void CEffectArrowFigure::MoveFigure() {
    if (figure_ == nullptr) return;
    TheClientWorld()->objectGrid_.Move(figure_, &localPosition_.x);
    static_cast<CTransformObject*>(figure_)->RollTowards(velocity_);
}

void CEffectArrowFigure::Start(int update) {
    if (update != 0) {
        MoveFigure();
        return;
    }
    mfc::CString model(game::ArrowFigureModelText());
    mfc::CString texture(game::ArrowFigureTextureText());
    int id = NextClientObjectId()--;
    CEngineObject* object = TheObjectManager()->CreateObject(kClassFigureObject, id);
    if (object != figure_) {
        CountedRefResetOriginal(&figure_);
        if (object != nullptr) ++object->refCount_;
        figure_ = object;
    }
    // No null test (the original has none either). The figure the object holds is read before the
    // figure is made, as the original does.
    auto* figure = static_cast<CObject3D*>(figure_);
    CObject3DClientSpecific* attached = figure->ClientSpecific();
    std::memcpy(&figure->position, &localPosition_, sizeof(figure->position));
    console::CStringArray parts;
    figure->MakeFigure(model, &parts, 1);
    attached->RegisterPartMaterials();
    figure->PlayAnimationSecond(0, nullptr);   // (the original passes the model name as the unused argument)
    figure->RestartPose();
    attached->SetSkins(texture, game::EmptyText());
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
        auto* caster = WorldFindUnit(ObjectWorld(this), nullptr, casterId_);
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

void CEffectArrowFigure::LoadState(CStream* s) {
    s->Read(&localPosition_, 12);
    s->Read(&targetId_, 4);
    s->Read(&casterId_, 4);
    s->Read(&velocity_, 12);
    s->Read(&arrived_, 4);
    AIRef::Of(&figure_)->Load(s, WorldContext());
    s->Read(&hit_, sizeof(hit_));
    s->Read(&fromCaster_, 1);
}

void CEffectArrowFigure::SaveState(CStream* s) {
    std::uint32_t copy;
    s->Write(&localPosition_, 12);
    std::memcpy(&copy, &targetId_, 4);
    s->Write(&copy, 4);
    std::memcpy(&copy, &casterId_, 4);
    s->Write(&copy, 4);
    s->Write(&velocity_, 12);
    std::memcpy(&copy, &arrived_, 4);
    s->Write(&copy, 4);
    AIRef::Of(&figure_)->Save(s);
    s->Write(&hit_, sizeof(hit_));
    std::uint8_t flag = fromCaster_;
    s->Write(&flag, 1);
}

// The constructor builds the base (the original under CTransformObject's vtable, ours under the
// effect base's: both are overwritten), no figure, then Reset.
CEffectArrowFigure* CEffectArrowFigure::Construct() {
    ConstructEffect();
    figure_ = nullptr;
    SetVtable(this, Vtable());
    Reset();
    return this;
}

void CEffectArrowFigure::Destruct() {
    SetVtable(this, Vtable());
    Reset();
    Reset();   // again through slot 18
    if (figure_ != nullptr) figure_->Release();
    figure_ = nullptr;
    DestructEffect();
}

CEffectArrowFigure* CEffectArrowFigure::DeletingDestruct(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}

EI_REWRITTEN(0x0067BB90, CEffectArrowFigure::Construct);
EI_REWRITTEN(0x0067BCD0, CEffectArrowFigure::Destruct);
EI_REPLACE_METHOD(0x0067BCB0, CEffectArrowFigure::DeletingDestruct);
EI_REPLACE_METHOD(0x0067BAA0, CEffectArrowFigure::Reset);
EI_REPLACE_METHOD(0x0067BE20, CEffectArrowFigure::Tick);
EI_REPLACE_METHOD(0x0067BEF0, CEffectArrowFigure::MoveFigure);
EI_REPLACE_METHOD(0x0067BF20, CEffectArrowFigure::Start);
EI_REPLACE_METHOD(0x0067C080, CEffectArrowFigure::Unpack);
EI_REPLACE_METHOD(0x0067C170, CEffectArrowFigure::Pack);
EI_REPLACE_METHOD(0x0067C200, CEffectArrowFigure::LoadState);
EI_REPLACE_METHOD(0x0067C290, CEffectArrowFigure::SaveState);

EI_REWRITTEN(0x0067AE30, CEffectArrow::Construct);
EI_REWRITTEN(0x0067B1C0, CEffectArrow::Destruct);
EI_REPLACE_METHOD(0x0067B1A0, CEffectArrow::DeletingDestruct);
EI_REWRITTEN(0x0067AAB0, CEffectMissile::Steer);
EI_REPLACE_METHOD(0x0067AA00, CEffectMissile::ResetMissile);
EI_REWRITTEN(0x0067AC70, CEffectMissile::SetTarget);
EI_REPLACE_METHOD(0x0067AC90, CEffectArrow::Tick);
EI_REPLACE_METHOD(0x0067AD10, CEffectArrow::Reset);
EI_REPLACE_METHOD(0x0067B450, CEffectArrow::MoveParts);
EI_REPLACE_METHOD(0x0067B4E0, CEffectArrow::Start);
EI_REPLACE_METHOD(0x0067B690, CEffectArrow::Unpack);
EI_REPLACE_METHOD(0x0067B800, CEffectArrow::Pack);
EI_REPLACE_METHOD(0x0067B8E0, CEffectArrow::LoadState);
EI_REPLACE_METHOD(0x0067B9A0, CEffectArrow::SaveState);
