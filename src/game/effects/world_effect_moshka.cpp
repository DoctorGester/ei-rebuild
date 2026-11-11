// EffectMoshka: the bird that perches near the followed hero, circles it when it comes close and
// flies off to another perch. See world_effect.h and docs/notes/effects.md ("Moshka").
#include "game/ai/ai_map.h"
#include "engine/core/base_manager.h"
#include "engine/scene/camera.h"
#include "game/effects/effects.h"
#include "game/app/singletons.h"
#include "game/objects/map_object.h"
#include "game/objects/object3d.h"
#include "game/objects/map_object_shadow.h"
#include "engine/core/random.h"
#include "game/ai/unit_ai.h"
#include "game/world/world.h"
#include "game/effects/world_effect.h"
#include "game/effects/world_effect_internal.h"

#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/maths.h"

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr int kBirdParticles = static_cast<int>(ParticleType::Bag);        // the particle type the figure carries
constexpr float kBirdParticleScale = 0.5f;

constexpr float kPerchedFrames = 200.0f;      // after Follow
constexpr float kCirclingFrames = 100.0f;     // then a perch is looked for
constexpr float kFlyingOffFrames = 150.0f;    // after which the bird is reset
constexpr float kMaxStep = 5.0f;              // frames per Update at most
constexpr float kCirclingRangeSquared = 25.0f;         // planar distance^2 at which a perched bird starts circling
constexpr float kCircleRadius = 1.5f;         // around the followed object
constexpr float kCircleHeight = 3.2f;         // above it
constexpr float kAngularSpeed = 0.2f;         // radians per frame
constexpr float kCircleStartHeight = 30.0f;
constexpr float kAngleOffset = 1.5f;
constexpr float kPerchSearchRadius = 50.0f;
constexpr float kClearance = 0.5f;            // above the ground, the sight threshold, height step
// The flight speed per frame: sqrt(distance to the followed object) * 0.03 + 0.3.
constexpr float kSpeedFactor = 0.03f;
constexpr float kSpeedBase = 0.3f;
// FindPerch: the eye 1.5 above the start, radius 50 less 5 each try but at least 4, height 30 rising by 3.
constexpr float kEyeHeight = 1.5f;
constexpr float kPerchRadius = 50.0f;
constexpr float kPerchRadiusStep = 5.0f;
constexpr float kPerchMinRadius = 4.0f;
constexpr float kPerchHeight = 30.0f;
constexpr float kPerchHeightStep = 3.0f;
constexpr float kFlightAngleRange = 6.28f;   // a random word to [0, 6.28)

// a == b, or unordered.
bool Same(float a, float b) {
    return a == b;
}



} // namespace

EffectMoshka::EffectMoshka() {
    Reset();
}

EffectMoshka::~EffectMoshka() { Reset(); }

void EffectMoshka::Reset() {
    Effect::Reset();
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

Vector3 EffectMoshka::FindPerch(const Vector3& from) {
    Vector3 eye = from;
    eye.z += kEyeHeight;
    float perchHeight = kPerchHeight;
    float distance = kPerchRadius;
    Vector3 perch;
    for (;;) {
        float radius = MaxKeepB(distance, kPerchMinRadius);
        float flightAngle = g_clientRandom.Below(kFlightAngleRange);
        perch.z = from.z + perchHeight;
        perch.x = (lib::Cos(flightAngle) * radius) + from.x;
        perch.y = (lib::Sin(flightAngle) * radius) + from.y;
        if (g_serverWorld.aiMap.LineOfSight(eye, perch) > kClearance) break;
        perchHeight += kPerchHeightStep;
        distance = radius - kPerchRadiusStep;
    }
    return perch;
}

void EffectMoshka::Update(float now) {
    if (bird == nullptr) return;
    MapObject* followedObject = g_clientWorld.ObjectWithId(followedId);
    if (followedObject == nullptr) return;
    const Vector3& followed = followedObject->localPosition;
    Object3D* currentBird = bird;
    Vector3 birdAt;
    birdAt = currentBird->localPosition;

    float elapsed = now - lastTime;
    lastTime = now;
    float step = 0.0f < elapsed ? elapsed : 0.0f;
    float frames = step < kMaxStep ? step : kMaxStep;
    timer -= frames;
    bool timeUp = timer < 0.0f;

    Vector3 target{};   // unset in the original for other states (Follow sets 0, so none occur)
    if (state == State::FlyingOff) {
        if (timeUp || (Same(localPosition.x, birdAt.x) && Same(localPosition.y, birdAt.y) &&
                       Same(localPosition.z, birdAt.z))) {
            Reset();   // the virtual
            return;
        }
        target = localPosition;
    } else if (state == State::Circling) {
        float flightAngle = now * kAngularSpeed + angle;
        if (timeUp) {
            // A perch straight out from the circle, seen from the bird.
            Vector3 perch = birdAt;
            perch.z += height;
            float outward = kHalfPi + flightAngle;
            perch.x = (lib::Cos(outward) * kPerchSearchRadius) + perch.x;
            perch.y = (lib::Sin(outward) * kPerchSearchRadius) + perch.y;
            if (g_serverWorld.aiMap.LineOfSight(currentBird->localPosition, perch) > kClearance) {
                state = State::FlyingOff;
                timer = kFlyingOffFrames;
                localPosition = perch;
            } else {
                height += kClearance;
            }
        }
        target.z = kCircleHeight + followed.z;
        target.x = (lib::Cos(flightAngle) * kCircleRadius) + followed.x;
        target.y = (lib::Sin(flightAngle) * kCircleRadius) + followed.y;
    } else if (state == State::Perched) {
        if (timeUp) {
            Vector3 perch = FindPerch(followed);
            state = State::FlyingOff;
            timer = kFlyingOffFrames;
            localPosition = perch;
        }
        target = followed;
        Vector3 toFollowed{target.x - birdAt.x, target.y - birdAt.y,
                           target.z - birdAt.z};
        float dx = target.x - birdAt.x;
        float dy = target.y - birdAt.y;
        if (LengthSquared2D(dy, dx) < kCirclingRangeSquared) {   // close: start circling from here
            float heading = lib::Atan2(-toFollowed.x, toFollowed.y);
            state = State::Circling;
            timer = kCirclingFrames;
            height = kCircleStartHeight;
            angle = (heading - now * kAngularSpeed) - kAngleOffset;
        }
        // Approach the point 1.5 to the side of the followed object, 3.2 above it.
        float sideX = toFollowed.y;
        float sideY = -toFollowed.x;
        float scale = kCircleRadius / std::sqrt(LengthSquared2D(sideY, sideX));
        target.x += scale * sideX;
        target.y += scale * sideY;
        target.z = kCircleHeight + target.z;
    }

    // Kept above the surface, the bird moves at most its speed towards the target.
    Vector3 ground{target.x, target.y, 0.0f};
    float raw = g_clientWorld.RawSurfaceHeightAt(target.x, target.y);
    float floor = g_clientWorld.platforms.SurfaceHeight(ground, static_cast<float>(raw)) + kClearance;
    float z = MaxKeepB(target.z, floor);
    Vector3 move{target.x - birdAt.x, target.y - birdAt.y, z - birdAt.z};
    float distance = std::sqrt(LengthSquared(move));
    if (!Same(distance, 0.0f)) {
        float range = std::sqrt(std::sqrt(LengthSquared(birdAt - followed)));
        float speed = (range * kSpeedFactor + kSpeedBase) * frames;
        if (distance > speed) {
            float scale = speed / distance;
            move *= scale;
        }
        Vector3 to{move.x + birdAt.x, move.y + birdAt.y, move.z + birdAt.z};
        g_clientWorld.objectGrid.Move(bird, &to.x);
        bird->RollTowards(move);
    }
    currentBird->animation->Advance(frames);
}

void EffectMoshka::Follow(int objectId) {
    followedId = objectId;
    MapObject* followed = g_clientWorld.ObjectWithId(objectId);
    if (followed == nullptr) return;
    std::string model = "unmobi";
    std::string texture = "bird00";
    int birdId = g_clientWorld.nextClientObjectId--;
    auto* object = g_clientWorld.objectManager->CreateObjectAs<MapObject>(MapObjectType::StuffObject, birdId);
    bird.Assign(object);
    // No null test (the original has none either). The figure the object holds is read before the
    // figure is made, like the original.
    Object3D* currentBird = bird;
    Object3DClientSpecific* attached = currentBird->ClientSpecific();
    Vector3 perch = FindPerch(followed->localPosition);
    currentBird->localPosition = perch;
    std::vector<std::string> parts;
    currentBird->MakeFigure(model, &parts);
    attached->RegisterPartMaterials();
    currentBird->PlayAnimation(nullptr);
    currentBird->RestartPose();
    attached->SetSkins(texture, "");
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
    state = State::Perched;
    timer = kPerchedFrames;
}

int EffectMoshka::Unpack(int, DeltaReader&) { return 1; }
