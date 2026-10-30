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
#include "random.h"

#include "lib/nan_audit.h"
#include "lib/str.h"
#include "lib/vecmath.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <format>

namespace {
constexpr int kTerrainPlatform = 2;          // the kind used on a platform above the ground
constexpr int kTerrainNoCrawl1 = 6;          // kinds crawling does not change
constexpr int kTerrainNoCrawl2 = 0xE;
constexpr float kPlatformGap = 0.01f;
constexpr int kCrawlSounds = 6;
constexpr int kWeaponBow = 5;
constexpr int kWeaponCrossbow = 6;


// ClientRand() scaled into 0..n-1.


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
constexpr int kViewNoAttackSounds = 3;       // the local view player's sessionKind
constexpr float kMinAxisLength = 0.0001f;
constexpr float kCrawlPrintScale = 0.5f;
constexpr float kWalkPrintScale = 0.75f;
constexpr int kStepFrames = 4;
constexpr int kMaxFootPart = 9;

// Footprints show for every unit (else only for MapObjectFlag::InParty units).
bool AllFootprintsShown() { return options::g_options.drawVisibleOnly == 0; }

// The frame `frame` was passed between `last` and `now` (not for an unordered `last`; the callers have returned
// on a NaN `now`).
bool PassedFrame(int frame, float last, float now) { return frame > last && frame <= now; }

void Shake(CUnit* unit) {
    const auto* prototype = unit->GetPrototype();
    float strength = prototype->shake;
    if (strength > 0.0f)
        g_camera.Shake(unit->localPosition, kShakeDuration, strength,
                                   kShakeDecay);
}

bool SoundBusy(int handle) {
    if (handle == -1) return false;
    SoundManager* sounds = g_clientWorld.sounds;
    return sounds->FindPlaying(handle) || sounds->FindDeferred(static_cast<std::uint32_t>(handle));
}

} // namespace

std::optional<std::string> UnitClientSpecific::FootstepSound(int terrain) {
    const auto* prototype = unit->GetPrototype();
    if (!prototype) return std::nullopt;
    const UnitModelRecord* model = unitdb::ModelRecord(static_cast<std::uint32_t>(prototype->modelIndex));
    if (!model || model->SoundCounts()[terrain] == 0) return std::nullopt;
    WorldClient* world = &g_clientWorld;
    float x = unit->localPosition.x;
    float y = unit->localPosition.y;
    float ground = world->ClampedGroundHeight(x, y);   // stored as a float
    Vector3 at{x, y, 0.0f};
    float surface = world->platforms.SurfaceHeight(at, world->ClampedGroundHeight(x, y));
    float gap = ground - surface;
    if (std::fabs(gap) > kPlatformGap) terrain = kTerrainPlatform;
    if (unit->moveMode == 0 && terrain != kTerrainNoCrawl2 && terrain != kTerrainNoCrawl1)
        return std::format("steps\\human\\crawl\\{}.wav", ClientRandBelow(kCrawlSounds) + 1);
    int count = static_cast<int>(model->SoundCounts()[terrain]);
    const std::string& name = model->soundNames.begin()[terrain];
    return std::format("{}\\{}.wav", name, ClientRandBelow(count) + 1);
}

std::optional<std::string> UnitClientSpecific::ActionSound(const FigureAnimation* animation, float from, float to,
                                                            std::uint8_t miss) {
    const auto* prototype = unit->GetSecondPrototype();
    if (!prototype) return std::nullopt;
    const UnitModelRecord* model = unitdb::ModelRecord(static_cast<std::uint32_t>(prototype->modelIndex));
    int frame = animation->soundFrame;
    if (frame == 0) return std::nullopt;
    EI_NAN_AUDIT(from);
    if (frame < from) return std::nullopt;   // before `from`
    if (frame >= to) return std::nullopt;
    if (miss) return std::string("weapons\\miss.wav");
    int count = 0;
    while (count < 4 && animation->sounds[count] != 0) ++count;
    if (count == 0) return std::nullopt;
    std::uint32_t sound = animation->sounds[ClientRandBelow(count)];
    std::string folder = model->soundFolder;   // a missing model's soundFolder is read (sic)
    if (prototype->soundFolder.size() != 0) folder = prototype->soundFolder;
    const char* format = nullptr;
    switch (animation->Kind()) {
    case AnimationKind::Idle:
        if (static_cast<std::uint32_t>(ClientRandBelow(100)) >= model->idleSoundChance) return std::string();
        format = "%s\\idle\\%d.wav";
        break;
    case AnimationKind::Special: {
        const char* name = SkillSound(static_cast<SkillAnimation>(animation->Number()));
        return std::string(name ? name : "");
    }
    case AnimationKind::Attack: {
        std::uint32_t hand = unit->activeWeapon < 4 ? weaponIds[unit->activeWeapon] : kNoItemId;
        if (unit->MapType() == MapObjectType::Hero && unit->activeWeapon < 4 && hand != kNoItemId &&
            (WeaponType(hand) == kWeaponBow || WeaponType(hand) == kWeaponCrossbow))
            return std::format("weapons\\{}.wav", static_cast<int>(sound));
        if (static_cast<std::uint32_t>(ClientRandBelow(100)) >= model->attackSoundChance) return std::string();
        format = "%s\\attack\\%d.wav";
        break;
    }
    case AnimationKind::Death:
        format = "%s\\death\\%d.wav";
        break;
    case AnimationKind::Hit:
        format = "%s\\hit\\%d.wav";
        break;
    default: return std::nullopt;
    }
    return str::Format(format, folder, sound);
}

// --- Playing them --------------------------------------------------------------------------------

void UnitClientSpecific::PlayStepSounds() {
    const FigureAnimationState* state = unit->AsObject3D()->animation;
    const FigureAnimation* current = state->current;
    float now = state->time;
    Vector3 position{unit->localPosition.x, unit->localPosition.y, unit->localPosition.z};
    int priority = 0;
    std::string sound;
    if (lastAnimation != current) {
        lastAnimationTime = 0.0f;
        lastAnimation = current;
    }
    EI_NAN_AUDIT(now, lastAnimationTime);
    if (now == lastAnimationTime) return;   // the same frame
    if (!current) return;
    SoundManager* sounds = g_clientWorld.sounds;
    AnimationKind kind = current->Kind();

    bool action = false;     // ActionSound at `priority`
    bool steps = false;
    switch (kind) {
    case AnimationKind::Attack: {
        if (g_clientWorld.LocalView()->player->sessionKind == kViewNoAttackSounds) break;
        sounds->StopSound(idleSound);
        if (PassedFrame(static_cast<int>(current->moment), lastAnimationTime, now)) Shake(unit);
        if (unit->MapType() == MapObjectType::Hero && unit->actionSound) {
            if (std::optional<std::string> miss = ActionSound(current, lastAnimationTime, now, static_cast<std::uint8_t>(unit->actionSound)))
                sounds->Play3D(*miss, kPriorityAction, &position.x, kSoundNear, kSoundFar, 0);
        }
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
    if (action) sound = ActionSound(current, lastAnimationTime, now, 0).value_or(std::string());

    for (int step = 0; steps && step < kStepFrames; ++step) {
        int frame = static_cast<int>(current->stepFrames[step]) - 1;
        if (frame == 0) break;
        if (!PassedFrame(frame, lastAnimationTime, now)) continue;
        if (!unit->HasEffect(SpellType::Silence)) {
            auto* world = ClientWorld();
            int ground = world->terrain->SurfaceClass(position.x, position.y);
            if (std::optional<std::string> footstep = FootstepSound(ground)) sound = std::move(*footstep);   // else the last one stays
        }
        if (unit->moveMode == 0) break;
        const auto* prototype = unit->GetPrototype();
        if (prototype) {
            float strength = prototype->shake;
            if (strength > 0.0f)
                g_camera.Shake(unit->localPosition, kShakeDuration, strength,
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
        FigureMesh* foot = unit->AsObject3D()->FindPartByPrefix(partName);
        if (!foot) break;
        Vector3 footAt;
        unit->AsObject3D()->PartWorldPosition(&footAt.x, foot);
        Vector3 facing;
        unit->localRotation.AxisY(&facing);
        float length = std::sqrt(LengthSquared2DUnrounded(facing.y, facing.x));
        if (kMinAxisLength > length) length = kMinAxisLength;   // a NaN length stays
        float cosine = facing.y / length;
        float clamped = cosine;
        EI_NAN_AUDIT(clamped, facing.x);
        if (clamped > 1.0f) clamped = 1.0f;
        else if (clamped < -1.0f) clamped = -1.0f;
        float angle = kPi - std::acos(clamped);
        float turn = angle;
        if (facing.x < 0.0f) turn = -turn;
        float sizeY = foot->box.bounds.boxMax.y;
        float sizeX = foot->box.bounds.boxMax.x;
        int shown = AllFootprintsShown() || (unit->flags & MapObjectFlag::InParty) ? 1 : 0;
        // The prototype's conspicuity[0] is the footprint scale (read unchecked: a unit without a prototype
        // faults here).
        float scale = prototype->conspicuity[0];
        if (unit->moveMode == 0) scale *= kCrawlPrintScale;
        else if (unit->moveMode == 1) scale *= kWalkPrintScale;
        int footprintId = g_clientWorld.AddFootprint(footAt.x - sizeX * facing.x * 0.5f,
                              footAt.y - sizeY * facing.y * 0.5f, sizeX, sizeY, turn, (left << 16) | printKind, scale,
                              shown, footprint);
        if (footprintRepeats > 0) --footprintRepeats;
        else footprint = -1;
        if (footprintId >= 0) {
            footprint = footprintId;
            footprintRepeats = ClientRandBelow(4) + 4;
        }
        break;
    }

    if (sound.size() != 0) {
        int handle = -1;
        if (kind == AnimationKind::Attack) {
            if (!SoundBusy(speechSound)) {
                handle = sounds->Play3D(sound, priority, &position.x, kSoundNear, kSoundFar, 0);
                speechSound = handle;
            }
        } else {
            handle = sounds->Play3D(sound, priority, &position.x, kSoundNear, kSoundFar, 0);
        }
        if (kind == AnimationKind::Idle) idleSound = handle;
    }
    lastAnimation = current;
    lastAnimationTime = now;
}
