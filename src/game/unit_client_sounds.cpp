// The sound names the client controller picks for a unit's footsteps and actions. See
// unit_client_models.h and docs/notes/units.md.
#include "camera.h"
#include "game_singletons.h"
#include "item.h"
#include "map_object_figures.h"
#include "options.h"
#include "player_client.h"
#include "sound.h"
#include "spell.h"
#include "terrain_sector.h"
#include "unit.h"
#include "unit_client_models.h"
#include "unit_db.h"
#include "world.h"

#include "lib/crt.h"
#include "lib/nan_audit.h"
#include "lib/str.h"
#include "lib/vecmath.h"

#include "ei/x87.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace {
constexpr int kTerrainPlatform = 2;          // the kind used on a platform above the ground
constexpr int kTerrainNoCrawl1 = 6;          // kinds crawling does not change
constexpr int kTerrainNoCrawl2 = 0xE;
constexpr float kPlatformGap = 0.01f;
constexpr int kCrawlSounds = 6;
constexpr int kWeaponBow = 5;
constexpr int kWeaponCrossbow = 6;


// rand() scaled into 0..n-1.

const UnitModelRecord* ModelOf(const UnitPrototypeRecord* prototype) {
    return unitdb::ModelRecord(static_cast<std::uint32_t>(prototype->modelIndex));
}

// The skill sounds by animation (the four SearchBody animations loot).
const char* SkillSound(SkillAnimation skill) {
    switch (skill) {
    case SkillAnimation::Tame:
        return "heroes\\human\\skills\\tame.wav";
    case SkillAnimation::Steal:
        return "heroes\\human\\skills\\steal.wav";
    case SkillAnimation::Use:
        return "heroes\\human\\skills\\science.wav";
    case SkillAnimation::SearchBodyRanged:
    case SkillAnimation::SearchBodyMode2:
    case SkillAnimation::SearchBodyMode2Ranged:
    case SkillAnimation::SearchBody:
        return "heroes\\human\\skills\\loot.wav";
    default: return nullptr;
    }
}

constexpr int kPriorityAction = 1000;
constexpr float kSoundNear = 8.0f;
constexpr float kSoundFar = 40.0f;
constexpr float kShakeDuration = 5.0f;
constexpr float kShakeDecay = 0.25f;
constexpr int kViewNoAttackSounds = 3;       // the local view player's sessionKind_
constexpr double kMinAxisLength = 0.0001;
constexpr float kCrawlPrintScale = 0.5f;
constexpr float kWalkPrintScale = 0.75f;
constexpr int kStepFrames = 4;
constexpr int kMaxFootPart = 9;

// Footprints show for every unit (else only for MapObjectFlag::InParty units).
bool AllFootprintsShown() { return options::g_options->drawVisibleOnly_ == 0; }

// The frame `frame` (int) was passed between `last` and `now` (not for an unordered `last`; the
// callers have returned on a NaN `now`).
bool PassedFrame(double frame, float last, float now) { return frame > last && frame <= now; }

void Shake(CUnit* unit) {
    const auto* prototype = unit->GetPrototype();
    float strength = prototype->shake;
    if (strength > 0.0f)
        g_camera.Shake(&unit->localPosition_, kShakeDuration, strength,
                                   kShakeDecay);
}

bool SoundBusy(int handle) {
    if (handle == -1) return false;
    CSoundManager* sounds = g_clientWorld.sounds_;
    return sounds->FindPlaying(handle) || sounds->FindDeferred(static_cast<std::uint32_t>(handle));
}

} // namespace

int CUnitClientSpecific::FootstepSound(int terrain, std::string* out) {
    const auto* prototype = unit->GetPrototype();
    if (!prototype) return 0;
    const UnitModelRecord* model = ModelOf(prototype);
    if (!model || model->SoundCounts()[terrain] == 0) return 0;
    CWorldClient* world = &g_clientWorld;
    float x = unit->localPosition_.x;
    float y = unit->localPosition_.y;
    float ground = world->ClampedGroundHeight(x, y);   // stored as a float
    Vector3 at{x, y, 0.0f};
    float surface = world->platforms_.SurfaceHeight(&at, world->ClampedGroundHeight(x, y));
    float gap = ground - surface;
    if (std::fabs(static_cast<float>(gap)) > kPlatformGap) terrain = kTerrainPlatform;
    if (unit->moveMode == 0 && terrain != kTerrainNoCrawl2 && terrain != kTerrainNoCrawl1) {
        *out = str::Format("steps\\human\\crawl\\%d.wav", libcrt::RandBelow(kCrawlSounds) + 1);
        return 1;
    }
    int count = static_cast<int>(model->SoundCounts()[terrain]);
    const std::string& name = model->soundNames.begin()[terrain];
    *out = str::Format("%s\\%d.wav", name, libcrt::RandBelow(count) + 1);
    return 1;
}

int CUnitClientSpecific::ActionSound(const FigureAnimation* animation, float from, float to, std::string* out,
                                     std::uint8_t miss) {
    const auto* prototype = unit->GetSecondPrototype();
    if (!prototype) return 0;
    const UnitModelRecord* model = ModelOf(prototype);
    int frame = animation->soundFrame;
    out->clear();
    if (frame == 0) return 0;
    double at = frame;
    EI_NAN_AUDIT(from);
    if (at < from) return 0;   // before `from`
    if (at >= to) return 0;
    if (miss) {
        *out = "weapons\\miss.wav";
        return 1;
    }
    int count = 0;
    while (count < 4 && animation->sounds[count] != 0) ++count;
    if (count == 0) return 0;
    std::uint32_t sound = animation->sounds[libcrt::RandBelow(count)];
    std::string folder = model->soundFolder;   // a missing model's soundFolder is read (sic)
    if (prototype->soundFolder.size() != 0) folder = prototype->soundFolder;
    const char* format = nullptr;
    switch (animation->Kind()) {
    case AnimationKind::Idle:
        if (static_cast<std::uint32_t>(libcrt::RandBelow(100)) >= model->idleSoundChance) return 1;
        format = "%s\\idle\\%d.wav";
        break;
    case AnimationKind::Special:
        if (const char* name = SkillSound(static_cast<SkillAnimation>(animation->Number()))) *out = name;
        return 1;
    case AnimationKind::Attack: {
        std::uint32_t hand = unit->activeWeapon < 4 ? weaponIds[unit->activeWeapon] : kNoItemId;
        if (unit->MapType() == MapObjectType::Hero && unit->activeWeapon < 4 && hand != kNoItemId &&
            (WeaponType(hand) == kWeaponBow || WeaponType(hand) == kWeaponCrossbow)) {
            *out = str::Format("weapons\\%d.wav", sound);
            return 1;
        }
        if (static_cast<std::uint32_t>(libcrt::RandBelow(100)) >= model->attackSoundChance) return 1;
        format = "%s\\attack\\%d.wav";
        break;
    }
    case AnimationKind::Death:
        format = "%s\\death\\%d.wav";
        break;
    case AnimationKind::Hit:
        format = "%s\\hit\\%d.wav";
        break;
    default: return 0;
    }
    *out = str::Format(format, folder, sound);
    return 1;
}

// --- Playing them --------------------------------------------------------------------------------

void CUnitClientSpecific::PlayStepSounds() {
    const CFigureAnimationState* state = unit->Object3D()->animation;
    const FigureAnimation* current = state->current;
    float now = state->time;
    Vector3 position{unit->localPosition_.x, unit->localPosition_.y, unit->localPosition_.z};
    int priority = 0;
    std::string sound;
    if (lastAnimation != current) {
        lastAnimationTime = 0.0f;
        lastAnimation = current;
    }
    EI_NAN_AUDIT(now, lastAnimationTime);
    if (now == lastAnimationTime) return;   // the same frame
    if (!current) return;
    CSoundManager* sounds = g_clientWorld.sounds_;
    AnimationKind kind = current->Kind();

    bool action = false;     // ActionSound at `priority`
    bool steps = false;
    switch (kind) {
    case AnimationKind::Attack: {
        if (g_clientWorld.LocalView()->player_->sessionKind_ == kViewNoAttackSounds) break;
        sounds->StopSound(idleSound);
        if (PassedFrame(x87::FromUnsigned(current->moment), lastAnimationTime, now)) Shake(unit);
        if (unit->MapType() == MapObjectType::Hero && unit->actionSound &&
            ActionSound(current, lastAnimationTime, now, &sound, static_cast<std::uint8_t>(unit->actionSound)))
            sounds->Play3D(sound, kPriorityAction, &position.x, kSoundNear, kSoundFar, 0, 0);
        priority = kPriorityAction;
        action = true;
        break;
    }
    case AnimationKind::Death:
        sounds->StopSound(idleSound);
        sounds->StopSound(speechSound);
        priority = kPriorityAction;
        action = true;
        break;
    case AnimationKind::Special:
        priority = kPriorityAction;
        action = true;
        break;
    case AnimationKind::Idle:
    case AnimationKind::Hit:
        action = true;
        break;
    case AnimationKind::Run:
    case AnimationKind::Walk:
        steps = true;
        break;
    default:
        break;
    }
    if (action && !ActionSound(current, lastAnimationTime, now, &sound, 0)) sound.clear();

    for (int step = 0; steps && step < kStepFrames; ++step) {
        int frame = static_cast<int>(current->stepFrames[step]) - 1;
        if (frame == 0) break;
        if (!PassedFrame(frame, lastAnimationTime, now)) continue;
        if (!unit->HasEffect(SpellType::Silence)) {
            auto* world = ClientWorld();
            int ground = world->terrain_->SurfaceClass(position.x, position.y);
            FootstepSound(ground, &sound);
        }
        if (unit->moveMode == 0) break;
        const auto* prototype = unit->GetPrototype();
        if (prototype) {
            float strength = prototype->shake;
            if (strength > 0.0f)
                g_camera.Shake(&unit->localPosition_, kShakeDuration, strength,
                                   kShakeDecay);
        }
        const auto* model = unit->GetPrototypeModel();
        if (!model) break;
        int printKind = model->footprintKind;
        if (printKind <= -1) break;
        std::uint32_t part = model->footPart;
        if (part == 0 || part >= kMaxFootPart) break;
        std::uint8_t left = static_cast<std::uint8_t>(~step & 1);
        if (model->swapFeet) left = left == 0;
        char partName[16];
        std::snprintf(partName, sizeof(partName), left ? "ll%d" : "rl%d", part);
        CFigureMesh* foot = unit->Object3D()->FindPartByPrefix(partName);
        if (!foot) break;
        Vector3 footAt;
        unit->Object3D()->PartWorldPosition(&footAt.x, foot);
        Vector3 facing;
        unit->localRotation_.AxisY(&facing);
        double length = std::sqrt(LengthSquared2DUnrounded(facing.y, facing.x));
        if (kMinAxisLength > length) length = kMinAxisLength;   // a NaN length stays
        float cosine = static_cast<float>(facing.y / length);
        float clamped = cosine;
        EI_NAN_AUDIT(clamped, facing.x);
        if (clamped > 1.0f) clamped = 1.0f;
        else if (clamped < -1.0f) clamped = -1.0f;
        float angle = static_cast<float>(kPi - std::acos(static_cast<double>(clamped)));
        float turn = angle;
        if (facing.x < 0.0f) turn = -turn;
        float sizeY = foot->box_.bounds.boxMax.y;
        float sizeX = foot->box_.bounds.boxMax.x;
        int shown = AllFootprintsShown() || (unit->flags_ & MapObjectFlag::InParty) ? 1 : 0;
        // The prototype's conspicuity[0] is the footprint scale (read unchecked: a unit without a prototype
        // faults here).
        float scale = prototype->conspicuity[0];
        if (unit->moveMode == 0) scale *= kCrawlPrintScale;
        else if (unit->moveMode == 1) scale *= kWalkPrintScale;
        int id = g_clientWorld.AddFootprint(footAt.x - sizeX * facing.x * 0.5f,
                              footAt.y - sizeY * facing.y * 0.5f, sizeX, sizeY, turn, (left << 16) | printKind, scale,
                              shown, footprint);
        if (footprintRepeats > 0) --footprintRepeats;
        else footprint = -1;
        if (id >= 0) {
            footprint = id;
            footprintRepeats = libcrt::RandBelow(4) + 4;
        }
        break;
    }

    if (sound.size() != 0) {
        int handle = -1;
        if (kind == AnimationKind::Attack) {
            if (!SoundBusy(speechSound)) {
                handle = sounds->Play3D(sound, priority, &position.x, kSoundNear, kSoundFar, 0, 0);
                speechSound = handle;
            }
        } else {
            handle = sounds->Play3D(sound, priority, &position.x, kSoundNear, kSoundFar, 0, 0);
        }
        if (kind == AnimationKind::Idle) idleSound = handle;
    }
    lastAnimation = current;
    lastAnimationTime = now;
}
