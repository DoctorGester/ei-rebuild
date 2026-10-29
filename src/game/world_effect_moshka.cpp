// CEffectMoshka: the bird that perches near the followed hero, circles it when it comes close and
// flies off to another perch. See world_effect.h and docs/notes/effects.md ("Moshka").
#include "ai_map.h"
#include "base_manager.h"
#include "camera.h"
#include "effects.h"
#include "game_singletons.h"
#include "game_strings.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "map_object_shadow.h"
#include "random.h"
#include "unit_ai.h"
#include "world.h"
#include "world_effect.h"
#include "world_effect_internal.h"

#include "lib/crt.h"
#include "lib/nan_audit.h"

#include "ei/x87.h"

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr int kBirdParticles = static_cast<int>(ParticleType::Bag);        // the particle type the figure carries
constexpr float kBirdParticleScale = 0.5f;

enum MoshkaState { kPerched = 0, kCircling = 1, kFlyingOff = 2 };
constexpr float kPerchedFrames = 200.0f;      // after Follow
constexpr float kCirclingFrames = 100.0f;     // then a perch is looked for
constexpr float kFlyingOffFrames = 150.0f;    // after which the bird is reset
constexpr float kMaxStep = 5.0f;              // frames per Update at most
constexpr float kNearSquared = 25.0f;         // planar distance^2 at which a perched bird starts circling
constexpr float kCircleRadius = 1.5f;         // around the followed object
constexpr float kCircleHeight = 3.2f;         // above it
constexpr float kAngularSpeed = 0.2f;         // radians per frame
constexpr float kCircleStartHeight = 30.0f;
constexpr double kAngleOffset = 1.5;
constexpr double kPerchSearchRadius = 50.0;
constexpr float kHalfPi = 1.5707964f;         // pi * 0.5, set at startup
constexpr float kClearance = 0.5f;            // above the ground, the sight threshold, height step
// The flight speed per frame: sqrt(distance to the followed object) * 0.03 + 0.3 (double
// the floats 0.03 and 0.3 widened).
constexpr double kSpeedFactor = static_cast<double>(0.03f);
constexpr double kSpeedBase = static_cast<double>(0.3f);
// FindPerch: the eye 1.5 above the start, radius 50 less 5 each try but at least 4, height 30 rising by 3.
constexpr float kEyeHeight = 1.5f;
constexpr float kPerchRadius = 50.0f;
constexpr float kPerchRadiusStep = 5.0f;
constexpr float kPerchMinRadius = 4.0f;
constexpr float kPerchHeight = 30.0f;
constexpr float kPerchHeightStep = 3.0f;
constexpr double kRandomAngle = 1.4621764913365538e-09;   // 2 pi / 2^32

// a == b, or unordered.
bool Same(float a, float b) {
    EI_NAN_AUDIT(a, b);
    return a == b;
}



} // namespace

CEffectMoshka::CEffectMoshka() {
    Reset();
}

// The destructor resets twice (inline, then the virtual): the inline one clears the source and the
// figure through counted-pointer assignments; both end as Reset's.
CEffectMoshka::~CEffectMoshka() {
    Reset();
    Reset();   // again, the virtual
}

void CEffectMoshka::Reset() {
    CEffect::Reset();
    if (bird_ != nullptr) g_clientWorld->RemoveObject(bird_->id_);
    if (source_ != nullptr) source_->Stop();
    source_.Reset();
    bird_.Reset();
    lastTime_ = 0.0f;
    followedId_ = -1;
}

int CEffectMoshka::RunTick() {
    Update(static_cast<float>(g_clientWorld->tick_));
    return 0;
}

Vector3* CEffectMoshka::FindPerch(Vector3* out, const Vector3* from) {
    Vector3 eye = *from;
    eye.z = static_cast<float>(eye.z + kEyeHeight);
    float height = kPerchHeight;
    float distance = kPerchRadius;
    Vector3 perch;
    for (;;) {
        float radius = distance >= kPerchMinRadius ? distance : kPerchMinRadius;
        double angle = x87::FromUnsigned(g_random.Next()) * kRandomAngle;
        perch.z = static_cast<float>(from->z + height);
        perch.x = static_cast<float>(x87::fcos(angle) * radius + from->x);
        perch.y = static_cast<float>(x87::fsin(angle) * radius + from->y);
        if (g_serverWorld->aiMap_.LineOfSight(&eye, &perch) > kClearance) break;
        height = height + kPerchHeightStep;
        distance = radius - kPerchRadiusStep;
    }
    *out = perch;
    return out;
}

void CEffectMoshka::Update(float now) {
    if (bird_ == nullptr) return;
    CMapObject* followedObject = g_clientWorld->ObjectWithId(followedId_);
    if (followedObject == nullptr) return;
    const Vector3& followed = followedObject->localPosition_;
    CObject3D* bird = bird_;
    Vector3 birdAt;
    birdAt = bird->localPosition_;

    float elapsed = static_cast<float>(now - lastTime_);
    lastTime_ = now;
    float step = 0.0f < elapsed ? elapsed : 0.0f;
    float frames = step < kMaxStep ? step : kMaxStep;
    timer_ = timer_ - frames;
    EI_NAN_AUDIT(timer_);
    bool timeUp = timer_ < 0.0f;

    Vector3 target{};   // unset in the original for other states (Follow sets 0, so none occur)
    if (state_ == kFlyingOff) {
        if (timeUp || (Same(localPosition_.x, birdAt.x) && Same(localPosition_.y, birdAt.y) &&
                       Same(localPosition_.z, birdAt.z))) {
            Reset();   // the virtual
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
            if (g_serverWorld->aiMap_.LineOfSight(&bird->localPosition_, &perch) > kClearance) {
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
        EI_NAN_AUDIT(dy, dx);
        if (LengthSquared2DUnrounded(dy, dx) < kNearSquared) {   // close: start circling from here
            double heading = x87::fpatan(-toFollowed.x, static_cast<double>(toFollowed.y));
            state_ = kCircling;
            timer_ = kCirclingFrames;
            height_ = kCircleStartHeight;
            angle_ = static_cast<float>((heading - now * kAngularSpeed) - kAngleOffset);
        }
        // Approach the point 1.5 to the side of the followed object, 3.2 above it.
        float sideX = toFollowed.y;
        float sideY = static_cast<float>(-toFollowed.x);
        float scale = static_cast<float>(kCircleRadius / std::sqrt(LengthSquared2DUnrounded(sideY, sideX)));
        target.x = static_cast<float>(target.x + static_cast<float>(scale * sideX));
        target.y = static_cast<float>(target.y + static_cast<float>(scale * sideY));
        target.z = static_cast<float>(kCircleHeight + target.z);
    }

    // Kept above the surface, the bird moves at most its speed towards the target.
    Vector3 ground{target.x, target.y, 0.0f};
    double raw = g_clientWorld->RawSurfaceHeightAt(target.x, target.y);
    float floor = static_cast<float>(g_clientWorld->platforms_.SurfaceHeight(&ground, static_cast<float>(raw)) + kClearance);
    float z = target.z >= floor ? target.z : floor;
    Vector3 move{static_cast<float>(target.x - birdAt.x), static_cast<float>(target.y - birdAt.y), static_cast<float>(z - birdAt.z)};
    float distance = static_cast<float>(std::sqrt(LengthSquaredUnrounded<Sum3::ZYX, float>(move)));
    if (!Same(distance, 0.0f)) {
        float gx = birdAt.x - followed.x;
        float gy = birdAt.y - followed.y;
        float gz = birdAt.z - followed.z;
        double range = std::sqrt(std::sqrt(LengthSquaredUnrounded<Sum3::ZYX>(gx, gy, gz)));
        double speed = (range * kSpeedFactor + kSpeedBase) * frames;
        if (distance > speed) {
            double scale = speed / distance;
            move.x = static_cast<float>(scale * move.x);
            move.y = static_cast<float>(scale * move.y);
            move.z = static_cast<float>(scale * move.z);
        }
        Vector3 to{static_cast<float>(move.x + birdAt.x), static_cast<float>(move.y + birdAt.y), static_cast<float>(move.z + birdAt.z)};
        g_clientWorld->objectGrid_.Move(bird_, &to.x);
        bird_->RollTowards(move);
    }
    bird->animation->Advance(frames);
}

void CEffectMoshka::Follow(int objectId) {
    followedId_ = objectId;
    CMapObject* followed = g_clientWorld->ObjectWithId(objectId);
    if (followed == nullptr) return;
    std::string model = game::MoshkaModelText();
    std::string texture = game::MoshkaTextureText();
    int id = g_clientWorld->nextClientObjectId_--;
    auto* object = g_clientWorld->objectManager_->CreateObjectAs<CMapObject>(MapObjectType::StuffObject, id);
    bird_.Assign(object);
    // No null test (the original has none either). The figure the object holds is read before the
    // figure is made, like the original.
    CObject3D* bird = bird_;
    CObject3DClientSpecific* attached = bird->ClientSpecific();
    Vector3 perch;
    FindPerch(&perch, &followed->localPosition_);
    bird->localPosition_ = perch;
    std::vector<std::string> parts;
    bird->MakeFigure(model, &parts, 1);
    attached->RegisterPartMaterials();
    bird->PlayAnimation(nullptr);
    bird->RestartPose();
    attached->SetSkins(texture, game::kEmptyText);
    g_clientWorld->AddObject(bird_);
    attached->SetLit(1);
    if (FigureAnimation* first = bird->AnimationAt(0)) {
        bird->animation->queue.clear();
        bird->animation->Play(first);
    }
    CParticleSource* source = g_clientWorld->particleManager_->CreateSource(kBirdParticles);
    source_.Assign(source);
    source_->ScaleSize(kBirdParticleScale);
    source_->Attach(bird_);
    state_ = kPerched;
    timer_ = kPerchedFrames;
}

int CEffectMoshka::Unpack(int, DeltaField*) { return 1; }
