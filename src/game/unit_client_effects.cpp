// What the client shows when a unit is hit, struck by sparks or dies (UnitClientSpecific's StatChanged,
// Wound and Died) and the party light of PlayerChanged. See unit_client_models.h and docs/notes/units.md.
#include "effects.h"
#include "game_singletons.h"
#include "item_db.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "map_object_shadow.h"
#include "options.h"
#include "sound.h"
#include "unit.h"
#include "unit_client_models.h"
#include "unit_db.h"
#include "world.h"
#include "random.h"

#include "lib/float_select.h"
#include "lib/nan_audit.h"
#include "lib/str.h"

#include "lib/rounding.h"

#include <cstring>
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
