// EffectMoshka: the bird that perches near the followed hero, circles it when it comes close and
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

EffectMoshka::EffectMoshka() {
    Reset();
}

EffectMoshka::~EffectMoshka() { Reset(); }

void EffectMoshka::Reset() {
    CEffect::Reset();
    if (bird != nullptr) g_clientWorld.RemoveObject(bird->id);
    if (source != nullptr) source->Stop();
    source.Reset();
    bird.Reset();
    lastTime = 0.0f;
    followedId = -1;
}

bool EffectMoshka::RunTick() {
    Update(static_cast<float>(g_clientWorld.tick));
    return false;
}

Vector3* EffectMoshka::FindPerch(Vector3* out, const Vector3& from) {
    Vector3 eye = from;
    eye.z = static_cast<float>(eye.z + kEyeHeight);
    float perchHeight = kPerchHeight;
    float distance = kPerchRadius;
    Vector3 perch;
    for (;;) {
        float radius = distance >= kPerchMinRadius ? distance : kPerchMinRadius;
        double flightAngle = x87::FromUnsigned(g_random.Next()) * kRandomAngle;
        perch.z = static_cast<float>(from.z + perchHeight);
        perch.x = static_cast<float>(x87::fcos(flightAngle) * radius + from.x);
        perch.y = static_cast<float>(x87::fsin(flightAngle) * radius + from.y);
        if (g_serverWorld.aiMap.LineOfSight(eye, perch) > kClearance) break;
        perchHeight = perchHeight + kPerchHeightStep;
        distance = radius - kPerchRadiusStep;
    }
    *out = perch;
    return out;
}

void EffectMoshka::Update(float now) {
    if (bird == nullptr) return;
    CMapObject* followedObject = g_clientWorld.ObjectWithId(followedId);
    if (followedObject == nullptr) return;
    const Vector3& followed = followedObject->localPosition;
    CObject3D* currentBird = bird;
    Vector3 birdAt;
    birdAt = currentBird->localPosition;

    float elapsed = static_cast<float>(now - lastTime);
    lastTime = now;
    float step = 0.0f < elapsed ? elapsed : 0.0f;
    float frames = step < kMaxStep ? step : kMaxStep;
    timer = timer - frames;
    EI_NAN_AUDIT(timer);
    bool timeUp = timer < 0.0f;

    Vector3 target{};   // unset in the original for other states (Follow sets 0, so none occur)
    if (state == kFlyingOff) {
        if (timeUp || (Same(localPosition.x, birdAt.x) && Same(localPosition.y, birdAt.y) &&
                       Same(localPosition.z, birdAt.z))) {
            Reset();   // the virtual
            return;
        }
        target = localPosition;
    } else if (state == kCircling) {
        float flightAngle = static_cast<float>(now * kAngularSpeed + angle);
        if (timeUp) {
            // A perch straight out from the circle, seen from the bird.
            Vector3 perch = birdAt;
            perch.z = static_cast<float>(perch.z + height);
            double outward = kHalfPi + flightAngle;
            perch.x = static_cast<float>(x87::fcos(outward) * kPerchSearchRadius + perch.x);
            perch.y = static_cast<float>(x87::fsin(outward) * kPerchSearchRadius + perch.y);
            if (g_serverWorld.aiMap.LineOfSight(currentBird->localPosition, perch) > kClearance) {
                state = kFlyingOff;
                timer = kFlyingOffFrames;
                localPosition = perch;
            } else {
                height = height + kClearance;
            }
        }
        target.z = static_cast<float>(kCircleHeight + followed.z);
        target.x = static_cast<float>(x87::fcos(flightAngle) * kCircleRadius + followed.x);
        target.y = static_cast<float>(x87::fsin(flightAngle) * kCircleRadius + followed.y);
    } else if (state == kPerched) {
        if (timeUp) {
            Vector3 perch;
            FindPerch(&perch, followed);
            state = kFlyingOff;
            timer = kFlyingOffFrames;
            localPosition = perch;
        }
        target = followed;
        Vector3 toFollowed{static_cast<float>(target.x - birdAt.x), static_cast<float>(target.y - birdAt.y),
                           static_cast<float>(target.z - birdAt.z)};
        float dx = target.x - birdAt.x;
        float dy = target.y - birdAt.y;
        EI_NAN_AUDIT(dy, dx);
        if (LengthSquared2DUnrounded(dy, dx) < kNearSquared) {   // close: start circling from here
            double heading = x87::fpatan(-toFollowed.x, static_cast<double>(toFollowed.y));
            state = kCircling;
            timer = kCirclingFrames;
            height = kCircleStartHeight;
            angle = static_cast<float>((heading - now * kAngularSpeed) - kAngleOffset);
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
    double raw = g_clientWorld.RawSurfaceHeightAt(target.x, target.y);
    float floor = static_cast<float>(g_clientWorld.platforms.SurfaceHeight(ground, static_cast<float>(raw)) + kClearance);
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
        g_clientWorld.objectGrid.Move(bird, &to.x);
        bird->RollTowards(move);
    }
    currentBird->animation->Advance(frames);
}

void EffectMoshka::Follow(int objectId) {
    followedId = objectId;
    CMapObject* followed = g_clientWorld.ObjectWithId(objectId);
    if (followed == nullptr) return;
    std::string model = game::MoshkaModelText();
    std::string texture = game::MoshkaTextureText();
    int birdId = g_clientWorld.nextClientObjectId--;
    auto* object = g_clientWorld.objectManager->CreateObjectAs<CMapObject>(MapObjectType::StuffObject, birdId);
    bird.Assign(object);
    // No null test (the original has none either). The figure the object holds is read before the
    // figure is made, like the original.
    CObject3D* currentBird = bird;
    Object3DClientSpecific* attached = currentBird->ClientSpecific();
    Vector3 perch;
    FindPerch(&perch, followed->localPosition);
    currentBird->localPosition = perch;
    std::vector<std::string> parts;
    currentBird->MakeFigure(model, &parts, 1);
    attached->RegisterPartMaterials();
    currentBird->PlayAnimation(nullptr);
    currentBird->RestartPose();
    attached->SetSkins(texture, game::kEmptyText);
    g_clientWorld.AddObject(bird);
    attached->SetLit(true);
    if (FigureAnimation* first = currentBird->AnimationAt(0)) {
        currentBird->animation->queue.clear();
        currentBird->animation->Play(first);
    }
    ParticleSource* particles = g_clientWorld.particleManager->CreateSource(kBirdParticles);
    source.Assign(particles);
    source->ScaleSize(kBirdParticleScale);
    source->Attach(bird);
    state = kPerched;
    timer = kPerchedFrames;
}

int EffectMoshka::Unpack(int, DeltaField*) { return 1; }
