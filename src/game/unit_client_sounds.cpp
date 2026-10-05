// The sound names the client controller picks for a unit's footsteps and actions. See
// unit_client_models.h and docs/notes/units.md.
#include "unit_client_models.h"

#include "item.h"
#include "map_object_figures.h"
#include "unit.h"
#include "unit_db.h"
#include "world.h"

#include "lib/crt.h"

#include "ei/hook.h"

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

// rand() scaled into 0..n-1 the original's way.
int RandomBelow(int n) { return gamecrt::rand() * n / 0x7FFF % n; }

const UnitModelRecord* ModelOf(const UnitPrototypeRecord* prototype) {
    return reinterpret_cast<const UnitModelRecord*>(
        unitdb::Models().At(static_cast<std::uint32_t>(prototype->modelIndex), unitdb::kModelSize));
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
    float x = unit->positionX;
    float y = unit->positionY;
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
    const char* name = model->soundNames.begin[terrain];
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
