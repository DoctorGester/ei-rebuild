// CEffectMoshka: the bird that perches near the followed hero, circles it when it comes close and
// flies off to another perch. See world_effect.h and docs/notes/effects.md ("Moshka").
#include "effects.h"
#include "map_object_shadow.h"
#include "unit_ai.h"
#include "world_effect.h"

#include "ai_map.h"
#include "base_manager.h"
#include "camera.h"
#include "console.h"
#include "game_strings.h"
#include "map_object_figures.h"
#include "originals_effects.h"
#include "originals_units.h"
#include "random.h"
#include "world.h"
#include "world_effect_internal.h"

#include "../lib/crt.h"
#include "ei/hook.h"
#include "ei/x87.h"

#include <cmath>
#include <cstring>

using ei::SetVtable;

namespace {

constexpr int kClassFigureObject = 0x38;      // the object manager's class of the bird's figure
constexpr int kBirdParticles = 0x203C;        // the particle type the figure carries
constexpr float kBirdParticleScale = 0.5f;

enum MoshkaState { kPerched = 0, kCircling = 1, kFlyingOff = 2 };
constexpr float kPerchedFrames = 200.0f;      // after Follow
constexpr float kCirclingFrames = 100.0f;     // then a perch is looked for
constexpr float kFlyingOffFrames = 150.0f;    // after which the bird is reset
constexpr float kMaxStep = 5.0f;              // frames per Update at most
constexpr float kNearSquared = 25.0f;         // planar distance^2 at which a perched bird starts circling
constexpr float kCircleRadius = 1.5f;         // 007433DC, around the followed object
constexpr float kCircleHeight = 3.2f;         // 007433E0, above it
constexpr float kAngularSpeed = 0.2f;         // 0073BAD4, radians per frame
constexpr float kCircleStartHeight = 30.0f;
constexpr double kAngleOffset = 1.5;          // 007438F0
constexpr double kPerchSearchRadius = 50.0;   // 0073D0A0
constexpr float kHalfPi = 1.5707964f;         // 007C8430 (pi * 0.5, set at startup from 007433A8)
constexpr float kClearance = 0.5f;            // 0073B86C: above the ground, the sight threshold, height step
// The flight speed per frame: sqrt(distance to the followed object) * 0.03 + 0.3 (double
// constants 007438E8 / 007438E0, the floats 0.03 and 0.3 widened).
constexpr double kSpeedFactor = static_cast<double>(0.03f);
constexpr double kSpeedBase = static_cast<double>(0.3f);
// FindPerch: the eye 1.5 above the start (0073C51C), radius 50 (0074023C) less 5 (0073BF34) each
// try but at least 4, height 30 rising by 3 (0073BAA4).
constexpr float kEyeHeight = 1.5f;
constexpr float kPerchRadius = 50.0f;
constexpr float kPerchRadiusStep = 5.0f;
constexpr float kPerchMinRadius = 4.0f;
constexpr float kPerchHeight = 30.0f;
constexpr float kPerchHeightStep = 3.0f;
constexpr double kRandomAngle = 1.4621764913365538e-09;   // 0073CF18, 2 pi / 2^32

// FCOMP; TEST AH,40h: equal, or unordered.
bool SameOrNaN(float a, float b) { return a == b || x87::IsNaN(a) || x87::IsNaN(b); }

CAIMap* AIMap() { return WorldServer()->AIMap(); }

Vector3& PositionOf(CEngineObject* object) { return static_cast<CTransformObject*>(object)->localPosition_; }

} // namespace

std::uintptr_t CEffectMoshka::Vtable() { return static_cast<std::uintptr_t>(0x0074387C); }

CEffectMoshka* CEffectMoshka::Construct() {
    ConstructEffect();
    bird_ = nullptr;
    source_ = nullptr;
    SetVtable(this, Vtable());
    Reset();
    return this;
}

// The original's first, inline reset clears the source through 0056F1A0 and the figure through
// 0052B030; both end as Reset's.
void CEffectMoshka::Destruct() {
    SetVtable(this, Vtable());
    Reset();
    Reset();   // again through slot 18
    if (source_ != nullptr) source_->Release();
    if (bird_ != nullptr) bird_->Release();
    bird_ = nullptr;
    DestructEffect();
}

CEffectMoshka* CEffectMoshka::DeletingDestruct(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}

void CEffectMoshka::Reset() {
    ResetBase();
    if (bird_ != nullptr) WorldClient()->RemoveObject(bird_->id_);
    if (source_ != nullptr) Source()->Stop();
    CEngineObject* source = source_;
    source_ = nullptr;
    if (source != nullptr) source->Release();
    if (bird_ != nullptr) {
        bird_->Release();
        bird_ = nullptr;
    }
    lastTime_ = 0.0f;
    followedId_ = -1;
}

int CEffectMoshka::Tick() {
    Update(static_cast<float>(effect::GameTick()));
    return 0;
}

Vector3* CEffectMoshka::FindPerch(Vector3* out, const Vector3* from) {
    Vector3 eye = *from;
    eye.z = static_cast<float>(eye.z + kEyeHeight);
    float height = kPerchHeight;
    float distance = kPerchRadius;
    Vector3 perch;
    for (;;) {
        float radius = !(distance >= kPerchMinRadius) ? kPerchMinRadius : distance;
        double angle = x87::FromUnsigned(IsaacRandom::Global().Next()) * kRandomAngle;
        perch.z = static_cast<float>(from->z + height);
        perch.x = static_cast<float>(x87::fcos(angle) * radius + from->x);
        perch.y = static_cast<float>(x87::fsin(angle) * radius + from->y);
        if (AIMap()->LineOfSight(&eye, &perch) > kClearance) break;
        height = height + kPerchHeightStep;
        distance = radius - kPerchRadiusStep;
    }
    *out = perch;
    return out;
}

void CEffectMoshka::Update(float now) {
    if (bird_ == nullptr) return;
    CEngineObject* followedObject = WorldClient()->ObjectWithId(followedId_);
    if (followedObject == nullptr) return;
    const Vector3& followed = PositionOf(followedObject);
    auto* bird = static_cast<CObject3D*>(bird_);
    Vector3 birdAt;
    std::memcpy(&birdAt, &bird->position, sizeof(birdAt));

    float elapsed = static_cast<float>(now - lastTime_);
    lastTime_ = now;
    float step = 0.0f < elapsed ? elapsed : 0.0f;
    float frames = step < kMaxStep ? step : kMaxStep;
    timer_ = timer_ - frames;
    bool timeUp = !(timer_ >= 0.0f);

    Vector3 target{};   // the original leaves it unset for other states (Follow sets 0, so none occur)
    if (state_ == kFlyingOff) {
        if (timeUp || (SameOrNaN(localPosition_.x, birdAt.x) && SameOrNaN(localPosition_.y, birdAt.y) &&
                       SameOrNaN(localPosition_.z, birdAt.z))) {
            Reset();   // through slot 18
            return;
        }
        target = localPosition_;
    } else if (state_ == kCircling) {
        float angle = static_cast<float>(now * kAngularSpeed + angle_);
        if (timeUp) {
            // A perch straight out from the circle, seen from the bird.
            Vector3 perch = birdAt;
            perch.z = static_cast<float>(perch.z + height_);
            double outward = kHalfPi + angle;
            perch.x = static_cast<float>(x87::fcos(outward) * kPerchSearchRadius + perch.x);
            perch.y = static_cast<float>(x87::fsin(outward) * kPerchSearchRadius + perch.y);
            if (AIMap()->LineOfSight(&bird->position, &perch) > kClearance) {
                state_ = kFlyingOff;
                timer_ = kFlyingOffFrames;
                localPosition_ = perch;
            } else {
                height_ = height_ + kClearance;
            }
        }
        target.z = static_cast<float>(kCircleHeight + followed.z);
        target.x = static_cast<float>(x87::fcos(angle) * kCircleRadius + followed.x);
        target.y = static_cast<float>(x87::fsin(angle) * kCircleRadius + followed.y);
    } else if (state_ == kPerched) {
        if (timeUp) {
            Vector3 perch;
            FindPerch(&perch, &followed);
            state_ = kFlyingOff;
            timer_ = kFlyingOffFrames;
            localPosition_ = perch;
        }
        target = followed;
        Vector3 toFollowed{static_cast<float>(target.x - birdAt.x), static_cast<float>(target.y - birdAt.y),
                           static_cast<float>(target.z - birdAt.z)};
        float dx = target.x - birdAt.x;
        float dy = target.y - birdAt.y;
        if (!(LengthSquared2DUnrounded(dy, dx) >= kNearSquared)) {   // close (or NaN): start circling from here
            double heading = x87::fpatan(-toFollowed.x, static_cast<double>(toFollowed.y));
            state_ = kCircling;
            timer_ = kCirclingFrames;
            height_ = kCircleStartHeight;
            angle_ = static_cast<float>((heading - now * kAngularSpeed) - kAngleOffset);
        }
        // Approach the point 1.5 to the side of the followed object, 3.2 above it.
        float sideX = toFollowed.y;
        float sideY = static_cast<float>(-toFollowed.x);
        float scale = static_cast<float>(kCircleRadius / x87::fsqrt(LengthSquared2DUnrounded(sideY, sideX)));
        target.x = static_cast<float>(target.x + static_cast<float>(scale * sideX));
        target.y = static_cast<float>(target.y + static_cast<float>(scale * sideY));
        target.z = static_cast<float>(kCircleHeight + target.z);
    }

    // Kept above the surface, the bird moves at most its speed towards the target.
    Vector3 ground{target.x, target.y, 0.0f};
    double raw = WorldClient()->RawSurfaceHeightAt(target.x, target.y);
    float floor = static_cast<float>(WorldClient()->platforms_.SurfaceHeight(&ground, static_cast<float>(raw)) + kClearance);
    float z = !(target.z >= floor) ? floor : target.z;
    Vector3 move{static_cast<float>(target.x - birdAt.x), static_cast<float>(target.y - birdAt.y), static_cast<float>(z - birdAt.z)};
    float distance = static_cast<float>(x87::fsqrt(LengthSquaredUnrounded<Sum3::ZYX, float>(move)));
    if (!SameOrNaN(distance, 0.0f)) {
        float gx = birdAt.x - followed.x;
        float gy = birdAt.y - followed.y;
        float gz = birdAt.z - followed.z;
        double range = x87::fsqrt(x87::fsqrt(LengthSquaredUnrounded<Sum3::ZYX>(gx, gy, gz)));
        double speed = (range * kSpeedFactor + kSpeedBase) * frames;
        if (distance > speed) {
            double scale = speed / distance;
            move.x = static_cast<float>(scale * move.x);
            move.y = static_cast<float>(scale * move.y);
            move.z = static_cast<float>(scale * move.z);
        }
        Vector3 to{static_cast<float>(move.x + birdAt.x), static_cast<float>(move.y + birdAt.y), static_cast<float>(move.z + birdAt.z)};
        WorldClient()->objectGrid_.Move(bird_, &to.x);
        static_cast<CTransformObject*>(bird_)->RollTowards(move);
    }
    bird->animation->Advance(frames);
}

void CEffectMoshka::Follow(int objectId) {
    followedId_ = objectId;
    CEngineObject* followed = WorldClient()->ObjectWithId(objectId);
    if (followed == nullptr) return;
    mfc::CString model(game::MoshkaModelText());
    mfc::CString texture(game::MoshkaTextureText());
    int id = NextClientObjectId()--;
    CEngineObject* object = camera::ObjectManager()->CreateObject(kClassFigureObject, id);
    AIRef::Of(&bird_)->Assign(object);
    // No null test (the original has none either). The figure the object holds is read before the
    // figure is made, as the original does.
    auto* bird = static_cast<CObject3D*>(bird_);
    CEngineObject* attached = bird->attached;
    Vector3 perch;
    FindPerch(&perch, &PositionOf(followed));
    std::memcpy(&bird->position, &perch, sizeof(bird->position));
    console::CStringArray parts;
    static_cast<CObject3D*>(bird)->MakeFigure(model, &parts, 1);
    static_cast<CObject3DClientSpecific*>(attached)->RegisterPartMaterials();
    Object3DSetAnimationsOriginal(bird, nullptr, model, 0);
    bird->RestartPose();
    static_cast<CObject3DClientSpecific*>(attached)->SetSkins(texture, game::EmptyText());
    WorldClient()->AddObject(bird_);
    static_cast<CObject3DSpecific*>(attached)->SetLit(1);
    if (FigureAnimation* first = bird->AnimationAt(0)) {
        bird->animation->queue.RemoveAll();
        bird->animation->Play(first);
    }
    auto* source = reinterpret_cast<CEngineObject*>(static_cast<CParticleManager*>(ParticleManagerObject())->CreateSource(kBirdParticles));
    CEngineObject* old = source_;
    source_ = source;
    if (source != nullptr) ++source->refCount_;
    if (old != nullptr) old->Release();
    Source()->ScaleSize(kBirdParticleScale);
    Source()->Attach(bird_);
    state_ = kPerched;
    timer_ = kPerchedFrames;
}

int CEffectMoshka::Unpack(int, DeltaField*) { return 1; }

EI_REPLACE_METHOD(0x0067C950, CEffectMoshka::Construct);
EI_REPLACE_METHOD(0x0067CAF0, CEffectMoshka::Destruct);
EI_REPLACE_METHOD(0x0067CAD0, CEffectMoshka::DeletingDestruct);
EI_REPLACE_METHOD(0x0067CC90, CEffectMoshka::Reset);
EI_REPLACE_METHOD(0x0067CD70, CEffectMoshka::Tick);
EI_REPLACE_METHOD(0x0067CD90, CEffectMoshka::FindPerch);
EI_REPLACE_METHOD(0x0067CEF0, CEffectMoshka::Update);
EI_REPLACE_METHOD(0x0067D420, CEffectMoshka::Follow);
EI_REPLACE_METHOD(0x0067D640, CEffectMoshka::Unpack);
