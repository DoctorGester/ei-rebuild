// What the client shows when a unit is hit, struck by sparks or dies (CUnitClientSpecific slots
// 46-48) and the party light of slot 18. See unit_client_models.h and docs/notes/units.md.
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

#include "ei/hook.h"
#include "ei/x87.h"
#include "originals_effects.h"
#include "originals_units.h"

namespace {
constexpr std::uint32_t kFlagNoEffect = 8;            // HitEffect's flags
constexpr float kMinAmount = 0.0001f;                 // 0073CD44
constexpr int kNoArmourSlot = 7;
constexpr std::uint32_t kNoMaterial = 0xFFFF;
constexpr std::uint8_t kNoSoundKind = 7;
constexpr std::uint16_t kFirstPrototypeWeapon = 0x10; // UnitParams +0x20: kinds below, prototypes from here
constexpr int kSoundPriority = 1000;
constexpr float kSoundNear = 8.0f;
constexpr float kSoundFar = 40.0f;
constexpr std::uint32_t kFiguresLoaded = 0x80;        // the unit's flags_
constexpr float kBloodScale = 2.5f;                   // 0073CE18
constexpr float kBloodHeight = 0.1f;
constexpr float kBloodMinAlpha = 0.1f;                // 0073B850
constexpr float kBloodMaxAlpha = 1.0f;                // 0073B84C
constexpr float kBloodRate = 1.0f / 30.0f;            // 0x3D088889
constexpr std::uint32_t kLastDecalBlood = 4;
constexpr float kHalf = 0.5f;                         // 0073B86C
constexpr float kDecalScale = 0.3f;                   // 0073C54C
constexpr float kSpreadUnits = 100.0f;                // 0073BAB0
constexpr float kSpreadStep = 0.02f;                  // 0073CCD8
constexpr float kDecalLength = 3.0f;                  // 0073BAA4
constexpr double kDeathDecalScale = 0.75;             // 0073EEB0
constexpr int kDeathVariantBase = 2;
constexpr int kSparks = 0x2043;
constexpr float kSparkAlpha = 10.0f;
constexpr int kSparkAttach = 7;
constexpr int kMinFlashTicks = 4;

// The armour slots covering each hit location (0073EC78: four ints per location; columns 2, 0,
// 1 are tried in that order, 7 none). Indexed as the original does, without a range check.
const std::int32_t (*HitArmourRows())[4] { return reinterpret_cast<const std::int32_t(*)[4]>(0x0073EC78); }
constexpr int kArmourColumns[3] = {2, 0, 1};

// The figure part and the particle attach point of each hit location.
struct HitPart {
    const char* name;
    int attachPoint;
};
constexpr HitPart kHitParts[6] = {{"hd", 1}, {"bd", 7}, {"lh2", 3}, {"rh2", 4}, {"ll2", 5}, {"rl2", 6}};

// The particle type of each blood kind (1..8).
constexpr int kBloodParticles[8] = {0x200F, 0x2031, 0x2032, 0x2033, 0x200F, 0x2031, 0x2032, 0x2033};

// Not rewritten (forwarders):
// 0056CE90 (RET 0x18). The client world adds a decal at (x, y) of `size` x `length`; kind:
// variant | (blood kind - 1) << 16; `fresh` 1 for a hit, 0 for a death.
__declspec(naked) void __fastcall AddBloodDecal(CWorldClient*, void*, float /*x*/, float /*y*/, float /*size*/,
                                                float /*length*/, std::uint32_t /*kind*/, int /*fresh*/)
    EI_ORIGINAL(0x0056CE90)

// The party light (0079BEA0: r, g, b; 0079BEAC: radius).
const float* PartyLightColour() { return reinterpret_cast<const float*>(0x0079BEA0); }
float PartyLightRadius() { return *reinterpret_cast<const float*>(0x0079BEAC); }

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

int RandomBelow(int n) { return gamecrt::rand() * n / 0x7FFF % n; }

// rand() picks the decal's variant (0 or 1).
std::uint32_t DecalVariant() { return static_cast<std::uint32_t>(gamecrt::rand() * 2 / 0x7FFF % 2); }

std::uint32_t DecalKind(std::uint32_t variant, std::uint32_t blood) { return variant | (blood + 0xFFFF) << 16; }

const UnitModelRecord* ModelOf(CUnit* unit) {
    return reinterpret_cast<const UnitModelRecord*>(unit->GetPrototypeModel());
}

// The smaller of the unit's two horizontal extents (the original's select keeps +0x148 for NaN).
float SmallerExtent(const CUnit* unit) {
    float y = unit->above.y;
    float x = unit->above.x;
    return !(y >= x) ? y : x;
}

// The hero's armour material over `location`, or null.
const itemdb::MaterialRecordHead* ArmourMaterial(const CUnitClientSpecific* self, int location) {
    const std::int32_t* row = HitArmourRows()[location];
    for (int column : kArmourColumns) {
        int slot = row[column];
        if (slot == kNoArmourSlot) continue;
        std::uint32_t material = self->armourIds[slot] & 0xFFFF;
        if (material == kNoMaterial) continue;
        const auto* record = reinterpret_cast<const itemdb::MaterialRecordHead*>(
            itemdb::Materials().At(material, itemdb::kMaterialSize));
        if (record) return record;
    }
    return nullptr;
}

// The weapon class of the last hit (UnitParams +0x20).
std::uint8_t HitSoundKind(CUnit* unit) {
    std::uint16_t weapon = static_cast<std::uint16_t>(unit->Params()->lastHitSource);
    if (weapon < kFirstPrototypeWeapon) return static_cast<std::uint8_t>(weapon);
    const auto* prototype = reinterpret_cast<const UnitPrototypeRecord*>(
        unitdb::Prototypes().At(weapon - kFirstPrototypeWeapon, unitdb::kPrototypeSize));
    return prototype ? static_cast<std::uint8_t>(prototype->hitSoundKind) : kNoSoundKind;
}
} // namespace

void CUnitClientSpecific::HitEffect(int location, float amount, std::uint32_t flags) {
    if (flags & kFlagNoEffect) return;
    if (!(amount > kMinAmount)) return;
    mfc::CString material(ModelOf(unit)->hitMaterial);
    if (unit->type_ == kUnitTypeHero) {
        // Heroes sound by the armour over the location (the model's material without one), mapped
        // to "stone", "Metal" or "leather".
        if (const itemdb::MaterialRecordHead* armour = ArmourMaterial(this, location)) material = armour->soundName;
        const auto* name = reinterpret_cast<const unsigned char*>(static_cast<const char*>(material));
        if (gamecrt::mbscmp(name, reinterpret_cast<const unsigned char*>("Crystal")) == 0 ||
            gamecrt::mbscmp(name, reinterpret_cast<const unsigned char*>("Bone")) == 0 ||
            gamecrt::mbscmp(name, reinterpret_cast<const unsigned char*>("Stone")) == 0)
            material = "stone";
        else if (gamecrt::mbscmp(name, reinterpret_cast<const unsigned char*>("Metal")) != 0)
            material = "leather";
    }
    mfc::CString sound;
    if (const char* format = HitSoundFormat(HitSoundKind(unit))) sound.Format(format, static_cast<const char*>(material));
    if (sound.GetLength() != 0) {
        auto* world = static_cast<CWorldClient*>(ManagerContext());
        world->sounds_->Play3D(sound, kSoundPriority, &unit->position.x, kSoundNear, kSoundFar, 0, 0);
    }

    if (location < 0 || location > 5) return;
    const HitPart& hit = kHitParts[location];
    auto* part = static_cast<FigurePart*>(ObjectFindPartOriginal(unit, nullptr, hit.name));
    if (!part || !(unit->flags_ & kFiguresLoaded)) return;
    std::uint32_t blood = ModelOf(unit)->bloodKind;
    if (blood >= 1 && blood <= 8) {
        auto* source = static_cast<CParticleSource*>(
            ParticleCreateSourceOriginal(ParticleManagerObject(), nullptr, kBloodParticles[blood - 1]));
        source->position = {0.0f, 0.0f, kBloodHeight};
        source->ScaleSize(part->radius * kBloodScale);
        source->typeParam = kBloodRate;
        float alpha = amount;
        if (!(alpha >= kBloodMinAlpha)) alpha = kBloodMinAlpha;
        else if (alpha > kBloodMaxAlpha) alpha = kBloodMaxAlpha;
        source->alpha = alpha;
        source->Attach(unit);
        source->attachPoint = hit.attachPoint;
    }
    if (blood == 0 || blood > kLastDecalBlood) return;
    float smaller = SmallerExtent(unit);
    float reach = !(1.0f >= smaller) ? smaller : 1.0f;
    volatile float sizeStored = (part->boxMax.y + part->boxMax.x) * kHalf * kDecalScale;
    float size = sizeStored;
    Vector3 at;
    ObjectPartPositionOriginal(unit, nullptr, &at.x, part);
    volatile float spreadStored = reach * kSpreadUnits;
    float spread = spreadStored;
    int offset = RandomBelow(x87::fistp(spread));
    at.x = static_cast<float>(offset * kSpreadStep - reach + at.x);
    offset = RandomBelow(x87::fistp(spread));
    at.y = static_cast<float>(offset * kSpreadStep - reach + at.y);
    std::uint32_t variant = DecalVariant();
    AddBloodDecal(WorldClient(), nullptr, at.x, at.y, size, size * kDecalLength, DecalKind(variant, blood), 1);
}
EI_REPLACE_METHOD(0x00537500, CUnitClientSpecific::HitEffect);

void CUnitClientSpecific::SparkEffect(int) {
    auto* source = static_cast<CParticleSource*>(ParticleCreateSourceOriginal(ParticleManagerObject(), nullptr, kSparks));
    source->position = {0.0f, 0.0f, 0.0f};
    source->ScaleSize(unit->radius);
    source->alpha = kSparkAlpha;
    source->Attach(unit);
    source->attachPoint = kSparkAttach;
    auto* half = static_cast<CObject3DClientSpecific*>(unit->Object3D()->attached);
    if (flashCountdown == 0) ClientPartColoursOriginal(half, nullptr, 1);
    if (flashCountdown <= kMinFlashTicks) flashCountdown = kMinFlashTicks;
}
EI_REPLACE_METHOD(0x00537C00, CUnitClientSpecific::SparkEffect);

void CUnitClientSpecific::DeathEffect() {
    CompanionHideLightOriginal(unit->MapObject()->companion);
    std::uint32_t blood = ModelOf(unit)->bloodKind;
    if (blood == 0 || blood > kLastDecalBlood) return;
    Vector3 at;
    float size;
    if (auto* body = static_cast<FigurePart*>(ObjectFindPartOriginal(unit, nullptr, "bd"))) {
        volatile float stored = (body->boxMax.y + body->boxMax.x) * kHalf;
        size = stored;
        ObjectPartPositionOriginal(unit, nullptr, &at.x, body);
    } else {
        volatile float stored = static_cast<float>(SmallerExtent(unit) * kDeathDecalScale);
        size = stored;
        at.x = unit->position.x;
        at.y = unit->position.y;
        at.z = unit->position.z;
    }
    std::uint32_t variant = DecalVariant() + kDeathVariantBase;
    AddBloodDecal(WorldClient(), nullptr, at.x, at.y, size, size * kDecalLength, DecalKind(variant, blood), 0);
}
EI_REPLACE_METHOD(0x005373D0, CUnitClientSpecific::DeathEffect);

void CUnitClientSpecific::OwnerPlayerChanged(void* player) {
    if (!player) return;
    const float* rgb = PartyLightColour();
    float colour[4] = {rgb[0], rgb[1], rgb[2], 1.0f};
    CompanionCreateLightOriginal(unit->MapObject()->companion, nullptr, colour, PartyLightRadius(), 0);
}
EI_REPLACE_METHOD(0x00534140, CUnitClientSpecific::OwnerPlayerChanged);
