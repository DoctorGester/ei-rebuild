// The sound names the client controller picks for a unit's footsteps and actions. See
// unit_client_models.h and docs/notes/units.md.
#include "unit_client_models.h"

#include "camera.h"
#include "item.h"
#include "map_object_figures.h"
#include "sound.h"
#include "terrain_sector.h"
#include "unit.h"
#include "unit_db.h"
#include "world.h"

#include "lib/crt.h"

#include "ei/hook.h"
#include "ei/x87.h"

#include <cmath>

namespace {
constexpr int kTerrainPlatform = 2;          // the kind used on a platform above the ground
constexpr int kTerrainNoCrawl1 = 6;          // kinds crawling does not change
constexpr int kTerrainNoCrawl2 = 0xE;
constexpr float kPlatformGap = 0.01f;        // 0073C548
constexpr int kCrawlSounds = 6;
constexpr std::uint32_t kKeyKind = 0x3C0000;    // FigureAnimation::key
constexpr std::uint32_t kKindIdle = 0x180000;
constexpr std::uint32_t kKindSkill = 0x40000;
constexpr std::uint32_t kKindAttack = 0x80000;
constexpr std::uint32_t kKindDeath = 0x1C0000;
constexpr std::uint32_t kKindHit = 0x200000;
constexpr int kNumberShift = 22;
constexpr std::uint32_t kNone = 0xFFFFFFFFu;
constexpr int kWeaponBow = 5;
constexpr int kWeaponCrossbow = 6;

// Rewritten below, called through their game addresses so their verify wrappers stay in the path
// (00534B60 is their only caller).
__declspec(naked) int __fastcall CallFootstepSound(CUnitClientSpecific*, void*, int, mfc::CString*) EI_ORIGINAL(0x00534590)
__declspec(naked) int __fastcall CallActionSound(CUnitClientSpecific*, void*, const FigureAnimation*, float, float,
                                                 mfc::CString*, std::uint8_t) EI_ORIGINAL(0x00534760)

// rand() scaled into 0..n-1 the original's way.
int RandomBelow(int n) { return gamecrt::rand() * n / 0x7FFF % n; }

const UnitModelRecord* ModelOf(const UnitPrototypeRecord* prototype) {
    return reinterpret_cast<const UnitModelRecord*>(
        unitdb::Models().RecordAt(static_cast<std::uint32_t>(prototype->modelIndex), unitdb::kModelSize));
}

// The skill sounds by animation number (0x13 tame, 0x14 steal, 0x15 science; loot for 0x16,
// 0x30, 0x34, 0x35).
const char* SkillSound(std::uint32_t number) {
    switch (number) {
    case 0x13: return "heroes\\human\\skills\\tame.wav";
    case 0x14: return "heroes\\human\\skills\\steal.wav";
    case 0x15: return "heroes\\human\\skills\\science.wav";
    case 0x16:
    case 0x30:
    case 0x34:
    case 0x35: return "heroes\\human\\skills\\loot.wav";
    default: return nullptr;
    }
}
} // namespace

int CUnitClientSpecific::FootstepSound(int terrain, mfc::CString* out) {
    const auto* prototype = reinterpret_cast<const UnitPrototypeRecord*>(unit->GetPrototype());
    if (!prototype) return 0;
    const UnitModelRecord* model = ModelOf(prototype);
    if (!model || model->SoundCounts()[terrain] == 0) return 0;
    CWorldClient* world = WorldClient();
    float x = unit->position.x;
    float y = unit->position.y;
    volatile float ground = world->ClampedGroundHeight(x, y);   // stored as a float
    Vector3 at{x, y, 0.0f};
    float surface = world->platforms_.SurfaceHeight(&at, world->ClampedGroundHeight(x, y));   // its slot 27
    volatile float gap = ground - surface;
    if (std::fabs(static_cast<float>(gap)) > kPlatformGap) terrain = kTerrainPlatform;
    if (unit->moveMode == 0 && terrain != kTerrainNoCrawl2 && terrain != kTerrainNoCrawl1) {
        out->Format("steps\\human\\crawl\\%d.wav", RandomBelow(kCrawlSounds) + 1);
        return 1;
    }
    int count = static_cast<int>(model->SoundCounts()[terrain]);
    const char* name = model->soundNames.begin()[terrain];
    out->Format("%s\\%d.wav", name, RandomBelow(count) + 1);
    return 1;
}
EI_REPLACE_METHOD(0x00534590, CUnitClientSpecific::FootstepSound);

int CUnitClientSpecific::ActionSound(const FigureAnimation* animation, float from, float to, mfc::CString* out,
                                     std::uint8_t miss) {
    const auto* prototype = reinterpret_cast<const UnitPrototypeRecord*>(unit->GetPrototype23C());
    if (!prototype) return 0;
    const UnitModelRecord* model = ModelOf(prototype);
    int frame = animation->soundFrame;
    out->Empty();
    if (frame == 0) return 0;
    double at = frame;
    if (!(at >= from)) return 0;   // before `from`, or NaN
    if (at >= to) return 0;
    if (miss) {
        *out = "weapons\\miss.wav";
        return 1;
    }
    int count = 0;
    while (count < 4 && animation->sounds[count] != 0) ++count;
    if (count == 0) return 0;
    std::uint32_t sound = animation->sounds[RandomBelow(count)];
    mfc::CString folder(model->soundFolder);   // the original reads a missing model's +0x124 (sic)
    if (prototype->soundFolder.GetLength() != 0) folder = prototype->soundFolder;
    const char* format = nullptr;
    switch (animation->key & kKeyKind) {
    case kKindIdle:
        if (static_cast<std::uint32_t>(RandomBelow(100)) >= model->idleSoundChance) return 1;
        format = "%s\\idle\\%d.wav";
        break;
    case kKindSkill:
        if (const char* name = SkillSound((animation->key >> kNumberShift) & 0xFF)) *out = name;
        return 1;
    case kKindAttack: {
        std::uint32_t hand = unit->activeWeapon < 4 ? weaponIds[unit->activeWeapon] : kNone;
        if (unit->type_ == kUnitTypeHero && unit->activeWeapon < 4 && hand != kNone &&
            (WeaponType(hand) == kWeaponBow || WeaponType(hand) == kWeaponCrossbow)) {
            out->Format("weapons\\%d.wav", sound);
            return 1;
        }
        if (static_cast<std::uint32_t>(RandomBelow(100)) >= model->attackSoundChance) return 1;
        format = "%s\\attack\\%d.wav";
        break;
    }
    case kKindDeath: format = "%s\\death\\%d.wav"; break;
    case kKindHit: format = "%s\\hit\\%d.wav"; break;
    default: return 0;
    }
    out->Format(format, static_cast<const char*>(folder), sound);
    return 1;
}
EI_REPLACE_METHOD(0x00534760, CUnitClientSpecific::ActionSound);

// --- Playing them --------------------------------------------------------------------------------

namespace {
constexpr std::uint32_t kKindWalk = 0x100000;
constexpr std::uint32_t kKindRun = 0x140000;
constexpr int kPriorityAction = 1000;
constexpr float kSoundNear = 8.0f;           // 0073EC1C
constexpr float kSoundFar = 40.0f;           // 0073EC20
constexpr float kShakeDuration = 5.0f;
constexpr float kShakeDecay = 0.25f;
constexpr std::uint32_t kEffectSilence = 0x10;
constexpr int kViewNoAttackSounds = 3;       // the local view player's +0x100
constexpr double kMinAxisLength = 0.0001;    // 0073EE98
constexpr float kPi = 3.14159274f;           // 0073EBE8
constexpr float kHalf = 0.5f;                // 0073B86C
constexpr float kCrawlPrintScale = 0.5f;     // 0073EBFC
constexpr float kWalkPrintScale = 0.75f;     // 0073EC00
constexpr std::uint32_t kFlagPrintsShown = 0x400000;   // the unit's flags_
constexpr int kStepFrames = 4;
constexpr int kMaxFootPart = 9;

// The client world's local view (CWorldClient::LocalView) and its player; only the field read here.
struct LocalViewPlayer {
    std::uint8_t unused_000[0x100];
    int mode;                       // +0x100 3: attacks play no sounds
};
struct LocalView {
    std::uint8_t unused_00[0x1C];
    LocalViewPlayer* player;        // +0x1C
};

// 0079BE6C: footprints show for every unit (else only for units with flags_ 0x400000).
bool AllFootprintsShown() { return *reinterpret_cast<const int*>(0x0079BE6C) == 0; }

// Not rewritten, forwarder: 0056D150, the client world adds a footprint (RET 0x24): its corner,
// size, angle, kind (left foot << 16 | texture), scale, whether it shows, the unit's previous
// footprint. Returns its id, negative when none was made.
__declspec(naked) int __fastcall AddFootprint(CWorldClient*, void*, float /*x*/, float /*y*/, float /*sizeX*/,
                                              float /*sizeY*/, float /*angle*/, int /*kind*/, float /*scale*/,
                                              int /*shown*/, int /*previous*/) EI_ORIGINAL(0x0056D150)

// The frame `frame` (int) was passed between `last` and `now` (not for an unordered `last`; a NaN
// `now` counts as passed).
bool PassedFrame(double frame, float last, float now) { return frame > last && !(frame > now); }

void Shake(CUnit* unit) {
    const auto* prototype = reinterpret_cast<const UnitPrototypeRecord*>(unit->GetPrototype());
    float strength = prototype->shake;
    if (strength > 0.0f)
        camera::TheCamera()->Shake(&unit->position, kShakeDuration, strength,
                                   kShakeDecay);
}

bool SoundBusy(int handle) {
    if (handle == -1) return false;
    CSoundManager* sounds = sound::Manager();
    return sounds->FindPlaying(handle) || sounds->FindDeferred(static_cast<std::uint32_t>(handle));
}
} // namespace

void CUnitClientSpecific::PlayStepSounds() {
    const CFigureAnimationState* state = unit->Object3D()->animation;
    const FigureAnimation* current = state->current;
    float now = state->time;
    Vector3 position{unit->position.x, unit->position.y, unit->position.z};
    int priority = 0;
    mfc::CString sound;
    if (lastAnimation != current) {
        lastAnimationTime = 0.0f;
        lastAnimation = current;
    }
    if (!(now < lastAnimationTime || now > lastAnimationTime)) return;   // the same frame, or NaN
    if (!current) return;
    CSoundManager* sounds = sound::Manager();
    std::uint32_t kind = current->key & kKeyKind;

    bool action = false;     // ActionSound at `priority`
    bool steps = false;
    switch (kind) {
    case kKindAttack: {
        auto* view = reinterpret_cast<LocalView*>(WorldClient()->LocalView());
        if (view->player->mode == kViewNoAttackSounds) break;
        sounds->StopSound(idleSound);
        if (PassedFrame(x87::FromUnsigned(current->moment), lastAnimationTime, now)) Shake(unit);
        if (unit->type_ == kUnitTypeHero && unit->actionSound &&
            CallActionSound(this, nullptr, current, lastAnimationTime, now, &sound, static_cast<std::uint8_t>(unit->actionSound)))
            sounds->Play3D(sound, kPriorityAction, &position.x, kSoundNear, kSoundFar, 0, 0);
        priority = kPriorityAction;
        action = true;
        break;
    }
    case kKindDeath:
        sounds->StopSound(idleSound);
        sounds->StopSound(speechSound);
        priority = kPriorityAction;
        action = true;
        break;
    case kKindSkill:
        priority = kPriorityAction;
        action = true;
        break;
    case kKindIdle:
    case kKindHit:
        action = true;
        break;
    case kKindWalk:
    case kKindRun:
        steps = true;
        break;
    default:
        break;
    }
    if (action && !CallActionSound(this, nullptr, current, lastAnimationTime, now, &sound, 0)) sound.Empty();

    for (int step = 0; steps && step < kStepFrames; ++step) {
        int frame = static_cast<int>(current->stepFrames[step]) - 1;
        if (frame == 0) break;
        if (!PassedFrame(frame, lastAnimationTime, now)) continue;
        if (!unit->HasEffect(kEffectSilence)) {
            auto* world = static_cast<CWorldClient*>(ManagerContext());
            int ground = world->terrain_->SurfaceClass(position.x, position.y);
            CallFootstepSound(this, nullptr, ground, &sound);
        }
        if (unit->moveMode == 0) break;
        const auto* prototype = reinterpret_cast<const UnitPrototypeRecord*>(unit->GetPrototype());
        if (prototype) {
            float strength = prototype->shake;
            if (strength > 0.0f)
                camera::TheCamera()->Shake(&unit->position, kShakeDuration, strength,
                                   kShakeDecay);
        }
        const auto* model = reinterpret_cast<const UnitModelRecord*>(unit->GetPrototypeModel());
        if (!model) break;
        int printKind = model->footprintKind;
        if (printKind <= -1) break;
        std::uint32_t part = model->footPart;
        if (part == 0 || part >= kMaxFootPart) break;
        std::uint8_t left = static_cast<std::uint8_t>(~step & 1);
        if (model->swapFeet) left = left == 0;
        char partName[16];
        gamecrt::sprintf(partName, left ? "ll%d" : "rl%d", part);
        CFigureMesh* foot = unit->Object3D()->FindPartByPrefix(partName);
        if (!foot) break;
        Vector3 footAt;
        unit->Object3D()->PartWorldPosition(&footAt.x, foot);
        Vector3 facing;
        unit->orientation.AxisY(&facing);
        double length = x87::fsqrt(LengthSquared2DUnrounded(facing.y, facing.x));
        if (kMinAxisLength > length) length = kMinAxisLength;   // a NaN length stays
        volatile float cosine = static_cast<float>(facing.y / length);
        float clamped = cosine;
        if (clamped > 1.0f) clamped = 1.0f;
        else if (!(clamped >= -1.0f)) clamped = -1.0f;   // NaN too
        volatile float angle = static_cast<float>(kPi - std::acos(static_cast<double>(clamped)));
        float turn = angle;
        if (!(facing.x >= 0.0f)) turn = -turn;   // NaN too
        float sizeY = foot->bounds_.boxMax.y;
        float sizeX = foot->bounds_.boxMax.x;
        int shown = AllFootprintsShown() || (unit->flags_ & kFlagPrintsShown) ? 1 : 0;
        // The prototype's +0xB0 (valuesE0[0]) is the footprint scale (read unchecked: a unit without a
        // prototype faults here in the original too).
        float scale = prototype->valuesE0[0];
        if (unit->moveMode == 0) scale *= kCrawlPrintScale;
        else if (unit->moveMode == 1) scale *= kWalkPrintScale;
        int id = AddFootprint(WorldClient(), nullptr, footAt.x - sizeX * facing.x * kHalf,
                              footAt.y - sizeY * facing.y * kHalf, sizeX, sizeY, turn, (left << 16) | printKind, scale,
                              shown, footprint);
        if (footprintRepeats > 0) --footprintRepeats;
        else footprint = -1;
        if (id >= 0) {
            footprint = id;
            footprintRepeats = RandomBelow(4) + 4;
        }
        break;
    }

    if (sound.GetLength() != 0) {
        int handle = -1;
        if (kind == kKindAttack) {
            if (!SoundBusy(speechSound)) {
                handle = sounds->Play3D(sound, priority, &position.x, kSoundNear, kSoundFar, 0, 0);
                speechSound = handle;
            }
        } else {
            handle = sounds->Play3D(sound, priority, &position.x, kSoundNear, kSoundFar, 0, 0);
        }
        if (kind == kKindIdle) idleSound = handle;
    }
    lastAnimation = current;
    lastAnimationTime = now;
}
EI_REPLACE_METHOD(0x00534B60, CUnitClientSpecific::PlayStepSounds);
