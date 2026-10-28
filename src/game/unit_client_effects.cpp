// What the client shows when a unit is hit, struck by sparks or dies (CUnitClientSpecific's StatChanged,
// Wound and Died) and the party light of PlayerChanged. See unit_client_models.h and docs/notes/units.md.
#include "unit_client_models.h"

#include "effects.h"
#include "item_db.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "map_object_shadow.h"
#include "sound.h"
#include "unit.h"
#include "unit_db.h"
#include "world.h"

#include "lib/crt.h"

#include "ei/x87.h"
#include "game_singletons.h"
#include "lib/nan_audit.h"
#include "options.h"
#include "lib/str.h"

#include <cstring>
#include <string>

namespace {
constexpr std::uint32_t kFlagNoEffect = 8;            // StatChanged's flags
constexpr float kMinAmount = 0.0001f;
constexpr int kNoArmourSlot = 7;
constexpr std::uint32_t kNoMaterial = 0xFFFF;
constexpr std::uint8_t kNoSoundKind = 7;
constexpr std::uint16_t kFirstPrototypeWeapon = 0x10; // UnitParams: kinds below, prototypes from here
constexpr int kSoundPriority = 1000;
constexpr float kSoundNear = 8.0f;
constexpr float kSoundFar = 40.0f;
constexpr std::uint32_t kFiguresLoaded = 0x80;        // the unit's flags_
constexpr float kBloodScale = 2.5f;
constexpr float kBloodHeight = 0.1f;
constexpr float kBloodMinAlpha = 0.1f;
constexpr float kBloodMaxAlpha = 1.0f;
constexpr float kBloodRate = 1.0f / 30.0f;
constexpr std::uint32_t kLastDecalBlood = 4;
constexpr float kHalf = 0.5f;
constexpr float kDecalScale = 0.3f;
constexpr float kSpreadUnits = 100.0f;
constexpr float kSpreadStep = 0.02f;
constexpr float kDecalLength = 3.0f;
constexpr double kDeathDecalScale = 0.75;
constexpr int kDeathVariantBase = 2;
constexpr int kSparks = 0x2043;
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
constexpr int kBloodParticles[8] = {0x200F, 0x2031, 0x2032, 0x2033, 0x200F, 0x2031, 0x2032, 0x2033};

// The party light (r, g, b; radius).
const float* PartyLightColour() { return &options::TheOptions()->heroLight_.red; }
float PartyLightRadius() { return options::TheOptions()->heroLight_.radius; }

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

int RandomBelow(int n) { return libcrt::Rand() * n / 0x7FFF % n; }

// rand() picks the decal's variant (0 or 1).
std::uint32_t DecalVariant() { return static_cast<std::uint32_t>(libcrt::Rand() * 2 / 0x7FFF % 2); }

std::uint32_t DecalKind(std::uint32_t variant, std::uint32_t blood) { return variant | (blood + 0xFFFF) << 16; }

const UnitModelRecord* ModelOf(CUnit* unit) {
    return unit->GetPrototypeModel();
}

// The smaller of the unit's two horizontal extents (the select keeps above.y for NaN).
float SmallerExtent(const CUnit* unit) {
    float y = unit->bounds.boxMax.y;
    float x = unit->bounds.boxMax.x;
    return y >= x ? x : y;
}

// The hero's armour material over `location`, or null.
const itemdb::MaterialRecordHead* ArmourMaterial(const CUnitClientSpecific* self, int location) {
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
std::uint8_t HitSoundKind(CUnit* unit) {
    std::uint16_t weapon = static_cast<std::uint16_t>(unit->Params()->lastHitSource);
    if (weapon < kFirstPrototypeWeapon) return static_cast<std::uint8_t>(weapon);
    const auto* prototype = unitdb::PrototypeRecord(weapon - kFirstPrototypeWeapon);
    return prototype ? static_cast<std::uint8_t>(prototype->hitSoundKind) : kNoSoundKind;
}
} // namespace

void CUnitClientSpecific::StatChanged(int location, float amount, std::uint32_t flags) {
    if (flags & kFlagNoEffect) return;
    EI_NAN_AUDIT(amount);
    if (amount <= kMinAmount) return;
    std::string material = ModelOf(unit)->hitMaterial;
    if (unit->type_ == kUnitTypeHero) {
        // Heroes sound by the armour over the location (the model's material without one), mapped
        // to "stone", "Metal" or "leather".
        if (const itemdb::MaterialRecordHead* armour = ArmourMaterial(this, location)) material = armour->soundName;
        const char* name = material.c_str();
        if (std::strcmp(name, "Crystal") == 0 ||
            std::strcmp(name, "Bone") == 0 ||
            std::strcmp(name, "Stone") == 0)
            material = "stone";
        else if (std::strcmp(name, "Metal") != 0)
            material = "leather";
    }
    std::string sound;
    if (const char* format = HitSoundFormat(HitSoundKind(unit))) sound = str::Format(format, material.c_str());
    if (sound.size() != 0) {
        auto* world = ClientWorld();
        world->sounds_->Play3D(sound.c_str(), kSoundPriority, &unit->localPosition_.x, kSoundNear, kSoundFar, 0, 0);
    }

    if (location < 0 || location > 5) return;
    const HitPart& hit = kHitParts[location];
    auto* part = unit->FindPartByPrefix(hit.name);
    if (!part || !(unit->flags_ & kFiguresLoaded)) return;
    std::uint32_t blood = ModelOf(unit)->bloodKind;
    if (blood >= 1 && blood <= 8) {
        auto* source = TheParticleManager()->CreateSource(kBloodParticles[blood - 1]);
        source->localPosition_ = {0.0f, 0.0f, kBloodHeight};
        source->ScaleSize(part->box_.bounds.radius * kBloodScale);
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
    float reach = 1.0f >= smaller ? 1.0f : smaller;
    float sizeStored = (part->box_.bounds.boxMax.y + part->box_.bounds.boxMax.x) * kHalf * kDecalScale;
    float size = sizeStored;
    Vector3 at;
    unit->PartWorldPosition(&at.x, part);
    float spreadStored = reach * kSpreadUnits;
    float spread = spreadStored;
    int offset = RandomBelow(x87::fistp(spread));
    at.x = static_cast<float>(offset * kSpreadStep - reach + at.x);
    offset = RandomBelow(x87::fistp(spread));
    at.y = static_cast<float>(offset * kSpreadStep - reach + at.y);
    std::uint32_t variant = DecalVariant();
    TheClientWorld()->AddBloodPrint(at.x, at.y, size, size * kDecalLength, DecalKind(variant, blood), 1);
}

void CUnitClientSpecific::Wound(float) {
    auto* source = TheParticleManager()->CreateSource(kSparks);
    source->localPosition_ = {0.0f, 0.0f, 0.0f};
    source->ScaleSize(unit->bounds.radius);
    source->alpha = kSparkAlpha;
    source->Attach(unit);
    source->attachPoint = kSparkAttach;
    auto* half = unit->Object3D()->ClientSpecific();
    if (flashCountdown == 0) half->SetMaterialsLit(1);
    if (flashCountdown <= kMinFlashTicks) flashCountdown = kMinFlashTicks;
}

void CUnitClientSpecific::Died() {
    unit->MapObject()->ClientCompanion()->HideLight();
    std::uint32_t blood = ModelOf(unit)->bloodKind;
    if (blood == 0 || blood > kLastDecalBlood) return;
    Vector3 at;
    float size;
    if (auto* body = unit->FindPartByPrefix("bd")) {
        float stored = (body->box_.bounds.boxMax.y + body->box_.bounds.boxMax.x) * kHalf;
        size = stored;
        unit->PartWorldPosition(&at.x, body);
    } else {
        float stored = static_cast<float>(SmallerExtent(unit) * kDeathDecalScale);
        size = stored;
        at.x = unit->localPosition_.x;
        at.y = unit->localPosition_.y;
        at.z = unit->localPosition_.z;
    }
    std::uint32_t variant = DecalVariant() + kDeathVariantBase;
    TheClientWorld()->AddBloodPrint(at.x, at.y, size, size * kDecalLength, DecalKind(variant, blood), 0);
}

void CUnitClientSpecific::PlayerChanged(void* player) {
    if (!player) return;
    const float* rgb = PartyLightColour();
    float colour[4] = {rgb[0], rgb[1], rgb[2], 1.0f};
    unit->MapObject()->ClientCompanion()->CreateLight(colour, PartyLightRadius(), false);
}
