// What the client shows and plays for a unit (UnitClientSpecific, unit_client_models.h): the effects of a hit, of
// sparks and of death, the party light, the footstep and action sounds, and the speech queue. See
// docs/notes/units.md.
#include "unit_client_models.h"

#include "camera.h"
#include "effects.h"
#include "game_singletons.h"
#include "item.h"
#include "item_db.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "map_object_shadow.h"
#include "options.h"
#include "player_client.h"
#include "random.h"
#include "sound.h"
#include "spell.h"
#include "terrain_sector.h"
#include "unit.h"
#include "unit_db.h"
#include "world.h"
#include "lib/float_select.h"
#include "lib/nan_audit.h"
#include "lib/rounding.h"
#include "lib/str.h"
#include "lib/vecmath.h"
#include "platform/clock.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <format>
#include <iterator>
#include <string>

namespace {
constexpr float kMinAmount = 0.0001f;
constexpr int kNoArmourSlot = 7;
constexpr std::uint32_t kNoMaterial = 0xFFFF;
constexpr std::uint8_t kNoSoundKind = 7;
constexpr std::uint16_t kFirstPrototypeWeapon = 0x10; // UnitParams: kinds below, prototypes from here
constexpr int kSoundPriority = 1000;
constexpr float kSoundNear = 8.0f;
constexpr float kSoundFar = 40.0f;
constexpr float kBloodScale = 2.5f;
constexpr float kBloodHeight = 0.1f;
constexpr float kBloodMinAlpha = 0.1f;
constexpr float kBloodMaxAlpha = 1.0f;
constexpr float kBloodRate = 1.0f / 30.0f;
constexpr std::uint32_t kLastDecalBlood = 4;
constexpr float kDecalScale = 0.3f;
constexpr float kSpreadUnits = 100.0f;
constexpr float kSpreadStep = 0.02f;
constexpr float kDecalLength = 3.0f;
constexpr float kDeathDecalScale = 0.75f;
constexpr int kDeathVariantBase = 2;
constexpr int kSparks = static_cast<int>(ParticleType::Sparks);
constexpr float kSparkAlpha = 10.0f;
constexpr int kSparkAttach = 7;
constexpr int kMinFlashTicks = 4;

// The armour slots covering each hit location (four ints per location; columns 2, 0,
// 1 are tried in that order, 7 none). Indexed without a range check.
constexpr std::int32_t kHitArmourRows[6][4] = /*0073EC78*/ {{7, 7, 0, 7}, {3, 7, 1, 7}, {3, 6, 1, 7}, {3, 6, 1, 7}, {4, 5, 2, 7}, {4, 5, 2, 7}};
constexpr int kArmourColumns[3] = {2, 0, 1};

// The figure part and the particle attach point of each hit location.
struct HitPart {
    const char* name;
    int attachPoint;
};
constexpr HitPart kHitParts[6] = {{"hd", 1}, {"bd", 7}, {"lh2", 3}, {"rh2", 4}, {"ll2", 5}, {"rl2", 6}};

// The particle type of each blood kind (1..8).
constexpr int kBloodParticles[8] = {static_cast<int>(ParticleType::Shower1), static_cast<int>(ParticleType::Shower2), static_cast<int>(ParticleType::Shower3), static_cast<int>(ParticleType::Shower4), static_cast<int>(ParticleType::Shower1), static_cast<int>(ParticleType::Shower2), static_cast<int>(ParticleType::Shower3), static_cast<int>(ParticleType::Shower4)};

// The party light (r, g, b; radius).
const float* PartyLightColour() { return &options::g_options.heroLight.red; }

// The hit sound's folder by the weapon class (kinds 7 and 8 and above 15 have none).
const char* HitSoundFormat(std::uint8_t kind) {
    switch (kind) {
    case 0:
    case 1:
    case 13: return "weapons\\slash\\%s\\1.wav";
    case 2:
    case 3:
    case 5:
    case 6:
    case 10: return "weapons\\pierce\\%s\\1.wav";
    case 4:
    case 11: return "weapons\\crash\\%s\\1.wav";
    case 9: return "weapons\\hands1\\%s\\1.wav";
    case 12: return "weapons\\hands4\\%s\\1.wav";
    case 14: return "weapons\\hands6\\%s\\1.wav";
    case 15: return "weapons\\hands7\\%s\\1.wav";
    default: return nullptr;
    }
}

// ClientRand() picks the decal's variant (0 or 1).
std::uint32_t DecalVariant() { return static_cast<std::uint32_t>(ClientRandBelow(2)); }

std::uint32_t DecalKind(std::uint32_t variant, std::uint32_t blood) { return variant | (blood + 0xFFFF) << 16; }

// The smaller of the unit's two horizontal extents (the select keeps above.y for NaN).
float SmallerExtent(const Unit* unit) {
    float y = unit->bounds.boxMax.y;
    float x = unit->bounds.boxMax.x;
    return MinKeepA(y, x);
}

// The hero's armour material over `location`, or null.
const itemdb::MaterialRecordHead* ArmourMaterial(const UnitClientSpecific* self, int location) {
    const std::int32_t* row = kHitArmourRows[location];
    for (int column : kArmourColumns) {
        int slot = row[column];
        if (slot == kNoArmourSlot) continue;
        std::uint32_t material = self->armourIds[slot] & 0xFFFF;
        if (material == kNoMaterial) continue;
        const auto* record = itemdb::MaterialAt(material);
        if (record) return record;
    }
    return nullptr;
}

// The weapon class of the last hit.
std::uint8_t HitSoundKind(Unit* unit) {
    std::uint16_t weapon = static_cast<std::uint16_t>(unit->params.lastHitSource);
    if (weapon < kFirstPrototypeWeapon) return static_cast<std::uint8_t>(weapon);
    const auto* prototype = unitdb::PrototypeRecord(weapon - kFirstPrototypeWeapon);
    return prototype ? static_cast<std::uint8_t>(prototype->hitSoundKind) : kNoSoundKind;
}
} // namespace

void UnitClientSpecific::StatChanged(int location, float amount, std::uint32_t statFlags) {
    if (statFlags & UnitStatsFlag::Alive) return;
    EI_NAN_AUDIT(amount);
    if (amount <= kMinAmount) return;
    std::string material = unit->GetPrototypeModel()->hitMaterial;
    if (unit->MapType() == MapObjectType::Hero) {
        // Heroes sound by the armour over the location (the model's material without one), mapped
        // to "stone", "Metal" or "leather".
        if (const itemdb::MaterialRecordHead* armour = ArmourMaterial(this, location)) material = armour->soundName;
        const std::string& name = material;
        if (name == "Crystal" || name == "Bone" || name == "Stone")
            material = "stone";
        else if (name != "Metal")
            material = "leather";
    }
    std::string sound;
    if (const char* format = HitSoundFormat(HitSoundKind(unit))) sound = str::Format(format, material);
    if (sound.size() != 0) {
        auto* world = ClientWorld();
        world->sounds->Play3D(sound, kSoundPriority, &unit->localPosition.x, kSoundNear, kSoundFar, 0);
    }

    if (location < 0 || location > 5) return;
    const HitPart& hit = kHitParts[location];
    auto* part = unit->FindPartByPrefix(hit.name);
    if (!part || !(unit->flags & MapObjectFlag::FiguresLoaded)) return;
    std::uint32_t blood = unit->GetPrototypeModel()->bloodKind;
    if (blood >= 1 && blood <= 8) {
        auto* source = g_clientWorld.particleManager->CreateSource(kBloodParticles[blood - 1]);
        source->localPosition = {0.0f, 0.0f, kBloodHeight};
        source->ScaleSize(part->box.bounds.radius * kBloodScale);
        source->typeParam = kBloodRate;
        float alpha = amount;
        if (alpha < kBloodMinAlpha) alpha = kBloodMinAlpha;
        else if (alpha > kBloodMaxAlpha) alpha = kBloodMaxAlpha;
        source->alpha = alpha;
        source->Attach(unit);
        source->attachPoint = hit.attachPoint;
    }
    if (blood == 0 || blood > kLastDecalBlood) return;
    float smaller = SmallerExtent(unit);
    float reach = MaxKeepB(1.0f, smaller);
    float sizeStored = (part->box.bounds.boxMax.y + part->box.bounds.boxMax.x) * 0.5f * kDecalScale;
    float size = sizeStored;
    Vector3 at;
    unit->PartWorldPosition(&at.x, part);
    float spreadStored = reach * kSpreadUnits;
    float spread = spreadStored;
    int offset = ClientRandBelow(lib::RoundEven(spread));
    at.x = static_cast<float>(offset) * kSpreadStep - reach + at.x;
    offset = ClientRandBelow(lib::RoundEven(spread));
    at.y = static_cast<float>(offset) * kSpreadStep - reach + at.y;
    std::uint32_t variant = DecalVariant();
    g_clientWorld.AddBloodPrint(at.x, at.y, size, size * kDecalLength, DecalKind(variant, blood), 1);
}

void UnitClientSpecific::Wound() {
    auto* source = g_clientWorld.particleManager->CreateSource(kSparks);
    source->localPosition = {0.0f, 0.0f, 0.0f};
    source->ScaleSize(unit->bounds.radius);
    source->alpha = kSparkAlpha;
    source->Attach(unit);
    source->attachPoint = kSparkAttach;
    auto* half = unit->AsObject3D()->ClientSpecific();
    if (flashCountdown == 0) half->SetMaterialsLit(true);
    if (flashCountdown <= kMinFlashTicks) flashCountdown = kMinFlashTicks;
}

void UnitClientSpecific::Died() {
    unit->AsMapObject()->ClientCompanion()->HideLight();
    std::uint32_t blood = unit->GetPrototypeModel()->bloodKind;
    if (blood == 0 || blood > kLastDecalBlood) return;
    Vector3 at;
    float size;
    if (auto* body = unit->FindPartByPrefix("bd")) {
        float stored = (body->box.bounds.boxMax.y + body->box.bounds.boxMax.x) * 0.5f;
        size = stored;
        unit->PartWorldPosition(&at.x, body);
    } else {
        float stored = SmallerExtent(unit) * kDeathDecalScale;
        size = stored;
        at.x = unit->localPosition.x;
        at.y = unit->localPosition.y;
        at.z = unit->localPosition.z;
    }
    std::uint32_t variant = DecalVariant() + kDeathVariantBase;
    g_clientWorld.AddBloodPrint(at.x, at.y, size, size * kDecalLength, DecalKind(variant, blood), 0);
}

void UnitClientSpecific::PlayerChanged(void* player) {
    if (!player) return;
    const float* rgb = PartyLightColour();
    float colour[4] = {rgb[0], rgb[1], rgb[2], 1.0f};
    unit->AsMapObject()->ClientCompanion()->CreateLight(colour, options::g_options.heroLight.radius, false);
}

// --- Footstep and action sounds ----------------------------------------------------------------------

// The sound names the client controller picks for a unit's footsteps and actions. See
// unit_client_models.h and docs/notes/units.md.

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

void Shake(Unit* unit) {
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
        Vector3 facing = unit->localRotation.AxisY();
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

// --- Speech ------------------------------------------------------------------------------------------

// What a unit says on the client: the speech queue of UnitClientSpecific and
// playing a speech. See unit_client_models.h and docs/notes/units.md.

namespace {
constexpr int kQueuedDelay = 3;
constexpr std::uint32_t kRepeatAfter = 5000;          // ms before the same speech plays again
constexpr int kSpeechPriority = 1;
constexpr int kSpeechVolume = 100;

// The kinds a queued speech gives way to: (new kind, queued kind).
bool Replaces(AckKind kind, AckKind queued) {
    return (kind == AckKind::CantChangePosition && queued == AckKind::ChangePosition) ||
           (kind == AckKind::NoPath && queued == AckKind::Move) || (kind == AckKind::NoMana && queued == AckKind::Cast) ||
           (kind == AckKind::CantTeleport && queued == AckKind::Cast) ||
           (kind == AckKind::ScienceFailed && (queued == AckKind::UseObject || queued == AckKind::Steal)) ||
           (kind == AckKind::SpellTooComplex && queued == AckKind::Cast);
}

// Kinds that queue behind speeches already waiting.
bool Important(AckKind kind) {
    switch (kind) {
    case AckKind::OutOfStamina:
    case AckKind::ArmCrippled:
    case AckKind::LegCrippled:
    case AckKind::ArmourBroken:
    case AckKind::WeaponBroken:
    case AckKind::ArmourWorn:
    case AckKind::WeaponWorn:
    case AckKind::Scenario: return true;
    default: return false;
    }
}

void SetTalking(int unitId, bool talking) {
    EngineObject* serverUnit = g_serverWorld.FindUnit(unitId);
    if (!serverUnit) return;
    if (talking) serverUnit->flags |= MapObjectFlag::Blocked;
    else serverUnit->flags &= ~MapObjectFlag::Blocked;
}

} // namespace

bool SpeechFilter::Replaced(const ClientSpeech* queued) const {
    return Replaces(kind, queued->kind);
}

bool SpeechFilter::Finished(const ClientSpeech* queued) const {
    if (queued->sound == -1 || SoundBusy(queued->sound)) return false;
    if (queued->talking) SetTalking(unitId, false);
    return true;
}

void UnitClientSpecific::Say(const SpeechRecord* record, AckKind kind) {
    if (!record) return;
    SpeechFilter replacing{0, kind};
    std::erase_if(speech, [&](const ClientSpeech& queued) { return replacing.Replaced(&queued); });
    SpeechFilter finishing{unit->id, AckKind::Selected};
    std::erase_if(speech, [&](const ClientSpeech& queued) { return finishing.Finished(&queued); });
    bool empty = speech.empty();
    if (!empty && !Important(kind)) return;
    ClientSpeech said{};
    said.id = record;
    said.kind = kind;
    said.delay = kQueuedDelay;
    said.sound = -1;
    said.talking = kind == AckKind::ScenarioTalking;
    if (empty) {
        if (said.talking) SetTalking(unit->id, true);
        said.delay = 0;
        if (kind != AckKind::Attack && kind != AckKind::DecidedToAttack) {
            Speak(&said);
        } else if (!SoundBusy(speechSound) && Speak(&said)) {
            speechSound = said.sound;
        }
    }
    speech.push_back(said);
}

void UnitClientSpecific::StopSpeech() {
    for (const ClientSpeech& queued : speech) {
        if (queued.talking) SetTalking(unit->id, false);
        g_clientWorld.sounds->StopSound(queued.sound);
    }
    speech.clear();
}

bool UnitClientSpecific::Speak(ClientSpeech* said) {
    std::uint32_t now = platform::Milliseconds();
    if (lastSpeech == said->id && now - lastSpeechTime < kRepeatAfter) return false;
    said->sound = g_clientWorld.sounds->Play2D(said->id->sound.data(), kSpeechPriority, kSpeechVolume, 0, 0, 0, 1, 0);
    lastSpeech = said->id;
    lastSpeechTime = now;
    return true;
}
