// How the client dresses a unit (UnitClientSpecific, unit_client_models.h): the armour figure parts and the
// composed skin texture, the equipment slots' ids and skins, and a party unit's wounds, face texture and weapon
// point. See docs/notes/units.md ("Creation, databases, save").
#include "unit_client_models.h"

#include "ai_map.h"
#include "core_maths.h"
#include "effects.h"
#include "figure_manager.h"
#include "game_singletons.h"
#include "item.h"
#include "item_db.h"
#include "item_names.h"
#include "map_object_figures.h"
#include "map_object_shadow.h"
#include "model.h"     // SceneRenderer, RenderHelper
#include "resource.h"
#include "texture.h"
#include "ui.h"
#include "unit.h"
#include "unit_db.h"
#include "world.h"
#include "ei/rebirth.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/nan_audit.h"
#include "lib/str.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <format>
#include <new>
#include <string>
#include <vector>

namespace {

constexpr int kLayerSkin = 0x32;
constexpr int kDetailedLayers = 8;
constexpr int kSimpleLayers = 2;
constexpr int kFlagDetailed = 0x100;
constexpr int kFlagSimple = 0x80;

const char* const kHair = "hr";
const char* const kHead = "hd";

// The armour row of armour slot `slot`, or -1 when the slot has no figure parts.
int RowOfSlot(int slot) {
    switch (slot) {
    case 0: return 2;
    case 1: return 0;
    case 2: return 1;
    default: return -1;
    }
}

// strcmp (bytes compared unsigned): zero when equal.
bool SameName(std::string_view a, std::string_view b) { return a == b; }

const itemdb::ArmorRecord* ArmourRecord(std::uint32_t type) { return itemdb::ArmorAt(type); }

constexpr int kWeaponBow = 5;
const char* const kBowArrow = "rh3.arrow00";

std::uint32_t WeaponIdOf(const UnitClientSpecific* self, std::uint32_t slot) {
    return slot < 4 ? self->weaponIds[slot] : kNoItemId;
}

// The bow's arrow part of a hero holding a bow, or null.
EngineObject* BowArrow(UnitClientSpecific* self) {
    std::uint32_t hand = WeaponIdOf(self, self->unit->activeWeapon);
    if (hand == kNoItemId || ::WeaponType(hand) != kWeaponBow) return nullptr;
    return self->unit->FindPart(kBowArrow);
}

// Whether `frame` was passed between the last update and now.
bool Passed(std::uint32_t frame, float last, float now) {
    double at = static_cast<double>(frame);
    EI_NAN_AUDIT(now);
    return at > last && at <= now;
}

// "<prototype name><texture>.mmp" into `texture`.
void AddPrototypePrefix(const Unit* unit, std::string& texture) {
    std::string named = unit->name + std::string(texture);
    std::string withExtension = named + ".mmp";
    texture = withExtension;
}

void AddTexture(SkinRequest& request, const std::string& texture) {
    request.textures.push_back(texture);
}

void AddArmourTexture(SkinRequest& request, const Unit* unit, std::string& texture, std::uint32_t id) {
    texture = ArmorModelName(PrototypeIndex(id), MaterialIndex(id));
    AddPrototypePrefix(unit, texture);
    AddTexture(request, texture);
}

// The layer texture names and the two keys of a skin.
void NameSkinTextures(const UnitClientSpecific* self, int detailed, SkinRequest& request, std::string& unitKey,
                      std::string& prototypeKey, std::string& texture) {
    const Unit* unit = self->unit;
    int id = unit->id;
    if (!detailed) {
        unitKey = std::format("mo{}s", id);
        prototypeKey = std::format("prot{}s", static_cast<int>(unit->prototypeId));
        request.flags = kFlagSimple;
        if (self->armourIds[0] != kNoItemId) AddArmourTexture(request, unit, texture, self->armourIds[0]);
        std::uint32_t hand = unit->activeWeapon;
        if (hand < 4 && self->weaponIds[hand] != kNoItemId) {
            texture = WeaponModelName(PrototypeIndex(self->weaponIds[hand]), MaterialIndex(self->weaponIds[hand]));
            AddPrototypePrefix(unit, texture);
            AddTexture(request, texture);
        }
        return;
    }
    unitKey = std::format("mo{}p", id);
    prototypeKey = std::format("prot{}p", static_cast<int>(unit->prototypeId));
    request.flags = kFlagDetailed;
    const UnitPrototypeRecord* prototype = unitdb::PrototypeRecord(static_cast<std::uint32_t>(unit->prototypeId));
    if (prototype)
        texture = str::Format("%sskin_%.2d.mmp", unit->name, prototype->skin);
    else
        texture = std::format("{}skin_00.mmp", unit->name);
    AddTexture(request, texture);
    for (int k = 3; k < 7; ++k)
        if (self->armourIds[k] != kNoItemId) AddArmourTexture(request, unit, texture, self->armourIds[k]);
    // Body armour and trousers, in the armours' drawing order (ArmorLayer).
    std::uint32_t body = self->armourIds[1];
    std::uint32_t legs = self->armourIds[2];
    if (body == kNoItemId || legs == kNoItemId) {
        std::uint32_t only = body != kNoItemId ? body : legs;
        if (only != kNoItemId) AddArmourTexture(request, unit, texture, only);
        return;
    }
    std::uint32_t bodyOrder = static_cast<std::uint32_t>(ArmorLayer(body >> 16));
    std::uint32_t legsOrder = static_cast<std::uint32_t>(ArmorLayer(legs >> 16));
    if (legsOrder < bodyOrder) {
        AddArmourTexture(request, unit, texture, legs);
        AddArmourTexture(request, unit, texture, body);
    } else {
        AddArmourTexture(request, unit, texture, body);
        AddArmourTexture(request, unit, texture, legs);
    }
}

} // namespace

// --- Armour figure parts ----------------------------------------------------------------------

bool UnitClientSpecific::ArmourPutOn(std::uint32_t itemId) {
    if (itemId == kNoItemId) return false;
    std::uint32_t prototypeType = PrototypeIndex(itemId);
    if (prototypeType == kNoIndex || MaterialIndex(itemId) == kNoIndex) return false;
    std::uint32_t slot = static_cast<std::uint32_t>(ArmourRecord(prototypeType)->type);
    if (slot >= 7) return false;
    if (armourIds[slot] == itemId) return true;
    armourIds[slot] = itemId;
    FigureManager* manager = g_clientWorld.figureManager;
    int row = RowOfSlot(static_cast<int>(slot));
    if (row < 0) return true;

    FigureModel* model;
    {
        std::string name = unit->name;
        model = manager->FindModel(&name);
    }
    if (!model) return false;
    const itemdb::ArmorRecord* armour = ArmourRecord(prototypeType);
    std::string armourPart;
    for (int i = 0; i < 7; ++i) {
        const char* plain = kArmourPartNames[row][i];
        if (!plain[0]) break;
        std::string part = plain;
        if (str::Compare(part,
                            kHair) == 0) {
            const UnitPrototypeRecord* prototype = unitdb::PrototypeRecord(static_cast<std::uint32_t>(unit->prototypeId));
            int hair = prototype->hair;
            if (hair == -1) continue;
            part = str::Format("hr.%.2d", hair);
        }
        FigureMesh* current = unit->FindPartByPrefix(part);
        armourPart = str::Format("%s.armor%.2d", part, armour->number);
        if (model->mesh->FindEntry(armourPart) != -1) {
            unit->LoadPart(armourPart);
            if (!SameName(plain, kHead)) unit->RemovePart(current);
        } else if (current) {
            if (SameName(plain, kHair)) {
                unit->RemovePart(current);
            } else if (str::Compare(part,
                                       current->GetName()) != 0) {
                unit->LoadPart(part);
                unit->RemovePart(current);
            }
        } else {
            unit->LoadPart(part);
        }
    }
    return true;
}

bool UnitClientSpecific::ArmourTakenOff(std::uint32_t slot) {
    if (slot >= 7) return false;
    std::uint32_t itemId = armourIds[slot];
    if (itemId == kNoItemId) return false;
    if (PrototypeIndex(itemId) == kNoIndex || MaterialIndex(itemId) == kNoIndex) return false;
    armourIds[slot] = kNoItemId;
    int row = RowOfSlot(static_cast<int>(slot));
    if (row < 0) return true;

    FigureModel* model;
    {
        std::string name = unit->name;
        model = g_clientWorld.figureManager->FindModel(&name);
    }
    if (!model) return false;   // the slot stays cleared
    for (int i = 0; i < 7; ++i) {
        const char* plain = kArmourPartNames[row][i];
        if (!plain[0]) break;
        std::string part = plain;
        if (str::Compare(part, kHair) == 0) {
            const UnitPrototypeRecord* prototype = unitdb::PrototypeRecord(static_cast<std::uint32_t>(unit->prototypeId));
            int hair = prototype->hair;
            if (hair == -1) continue;
            part = str::Format("hr.%.2d", hair);
        }
        std::vector<FigureMesh*> parts = unit->CollectPartsByPrefix(part);
        if (std::ssize(parts) == 1) {
            // One part of that name: replaced only when it is not the plain part already.
            if (str::Compare(part, parts[0]->GetName()) != 0) {
                unit->RemovePart(parts[0]);
                unit->LoadPart(part);
            }
        } else {
            for (int k = 0; k < std::ssize(parts); ++k) unit->RemovePart(parts[k]);
            unit->LoadPart(part);
        }
    }
    return true;
}

int UnitClientSpecific::ShowWeapon(std::uint32_t slot) {
    std::uint32_t previous = WeaponIdOf(this, unit->activeWeapon);
    std::uint32_t next = WeaponIdOf(this, slot);
    {
        std::string name = unit->name;
        g_clientWorld.figureManager->FindModel(&name);    // loads and caches the model
    }
    if (previous != kNoItemId) {
        for (const std::string& name : WeaponModelParts(previous >> 16)) unit->RemovePartNamed(name);
    }
    unit->activeWeapon = slot;
    weaponHidden = 0;
    if (next != kNoItemId) {
        for (const std::string& name : WeaponModelParts(next >> 16)) {
            EngineObject* part = unit->LoadPart(name);
            if (part) unit->AttachPartMesh(part);
        }
        arrowHidden = 0;
        std::uint32_t hand = WeaponIdOf(this, unit->activeWeapon);
        if (hand != kNoItemId && ::WeaponType(hand) == kWeaponBow) {
            if (EngineObject* arrow = unit->FindPart(kBowArrow)) {
                arrow->flags |= FigureMeshFlag::Hidden;
                arrowHidden = 1;
            }
        }
    }
    // RefreshModel after the change (the unit's controller is this object).
    unit->Controller()->RefreshModel();
    return 1;
}

// --- The weapon in hand and the bow's arrow during animations ------------------------------

void UnitClientSpecific::ShowWeaponParts(bool hidden) {
    if (weaponHidden == static_cast<std::uint8_t>(hidden)) return;
    std::uint32_t hand = WeaponIdOf(this, unit->activeWeapon);
    std::vector<std::string> names;
    if (hand != kNoItemId) names = WeaponModelParts(hand >> 16);
    for (const std::string& name : names) {
        if (EngineObject* part = unit->FindPart(name)) {
            if (hidden) part->flags |= FigureMeshFlag::Hidden;
            else part->flags &= ~FigureMeshFlag::Hidden;
        }
    }
    weaponHidden = hidden;
    arrowHidden = hidden;
}

void UnitClientSpecific::UpdateArrow() {
    if (unit->MapType() != MapObjectType::Hero) return;
    const FigureAnimationState* state = unit->AsObject3D()->animation;
    const FigureAnimation* current = state->current;
    if (current && (current->flags & 1)) {
        float now = state->time;
        EI_NAN_AUDIT(now, lastAnimationTime);
        if (now == lastAnimationTime) return;   // the same time
        if (Passed(current->arrowFrom, lastAnimationTime, now)) {
            if (!arrowHidden) return;
            if (EngineObject* arrow = BowArrow(this)) {
                arrow->flags &= ~FigureMeshFlag::Hidden;
                arrowHidden = 0;
            }
            return;
        }
        if (!Passed(current->arrowTo, lastAnimationTime, now)) return;
    }
    if (arrowHidden == 1) return;
    if (EngineObject* arrow = BowArrow(this)) {
        arrow->flags |= FigureMeshFlag::Hidden;
        arrowHidden = 1;
    }
}

ArmourPartNames CollectArmourPartNames(const UnitPrototypeRecord* prototype, const std::vector<std::uint32_t>& armourIds,
                                       const std::vector<std::uint32_t>& weaponIds) {
    ArmourPartNames names;
    char text[0x401];
    for (const auto& id : armourIds) {
        const itemdb::ArmorRecord* armour = ArmourRecord(id >> 16);
        if (!armour) continue;
        int row = RowOfSlot(armour->type);
        if (row < 0) continue;
        for (int i = 0; i < 7; ++i) {
            const char* plain = kArmourPartNames[row][i];
            if (!plain[0]) break;
            if (SameName(plain, kHair)) {
                int hair = prototype->hair;
                if (hair == -1) continue;
                std::snprintf(text, sizeof(text), "hr.%.2d.armor%.2d", hair, armour->number);
            } else {
                std::snprintf(text, sizeof(text), "%s.armor%.2d", plain, armour->number);
            }
            names.worn.push_back(text);
        }
    }
    for (const auto& id : weaponIds) AppendWeaponModelParts(id, &names.worn);

    // Every plain part of the three rows. All seven names of each row are walked (the
    // test on the name's address never fails), so the empty names of the helmet row go in too.
    for (int row = 0; row < 3; ++row) {
        for (int i = 0; i < 7; ++i) {
            const char* plain = kArmourPartNames[row][i];
            if (SameName(plain, kHair)) {
                int hair = prototype->hair;
                if (hair == -1) continue;
                std::snprintf(text, sizeof(text), "hr.%.2d", hair);
                names.plain.push_back(text);
            } else {
                names.plain.push_back(plain);
            }
        }
    }
    return names;
}

// --- Skin -------------------------------------------------------------------------------------

std::vector<SkinLayer> UnitClientSpecific::BuildSkinLayers(int detailed) {
    const itemdb::ArmorTable& armours = g_itemDatabase.armors;
    if (!detailed) {
        std::vector<SkinLayer> layers(kSimpleLayers);
        std::uint32_t armourId = armourIds[0];
        if (armourId != kNoItemId) {
            if (const itemdb::ArmorRecord* armour = armours.At(armourId >> 16)) {
                layers[0].id = armourId;
                layers[0].kind = static_cast<int>(ItemKind::Armor);
                layers[0].slot = armour->type;
            }
        }
        std::uint32_t hand = unit->activeWeapon;
        if (hand < 4 && (armourId = weaponIds[hand]) != kNoItemId) {
            if (const itemdb::WeaponRecord* weapon = itemdb::WeaponAt(armourId >> 16)) {
                layers[1].id = armourId;
                layers[1].kind = static_cast<int>(ItemKind::Weapon);
                layers[1].slot = weapon->type;
            }
        }
        return layers;
    }
    std::vector<SkinLayer> layers(kDetailedLayers);
    layers[0].id = static_cast<std::uint32_t>(unit->prototypeId);
    layers[0].kind = kLayerSkin;
    layers[0].slot = -1;
    for (int k = 0; k < 7; ++k) {
        std::uint32_t armourId = armourIds[k];
        if (armourId == kNoItemId) continue;
        const itemdb::ArmorRecord* armour = armours.At(armourId >> 16);
        if (!armour) continue;
        layers[k + 1].id = armourId;
        layers[k + 1].kind = static_cast<int>(ItemKind::Armor);
        layers[k + 1].slot = armour->type;
    }
    return layers;
}

bool UnitClientSpecific::ComposeSkin(int detailed, std::string* target) {
    TextureManager* textureManager = g_clientWorld.textureManager;
    bool result;
    {
        std::string unitKey;
        std::string prototypeKey;
        std::string texture;   // the work string
        SkinRequest request{};
        const std::vector<SkinLayer> layers = BuildSkinLayers(detailed);
        int bytes = lib::Count(layers) * static_cast<int>(sizeof(SkinLayer));
        request.layers.resize(bytes);
        std::memcpy(request.layers.data(), layers.data(), static_cast<std::size_t>(bytes));
        NameSkinTextures(this, detailed, request, unitKey, prototypeKey, texture);
        result = textureManager->Compose(target, &request, &unitKey, &prototypeKey, 3, 0x5551) != 0;
        lib::FreeStorage(request.layers);
    }
    return result;
}

// --- The client controller's Create and Destroy ----------------------------------------------------

namespace {
constexpr int kNone = -1;
constexpr std::uint32_t kNoModel = 0xFFFFFFFFu;
constexpr int kDressedArmourSlots = 3;   // body, trousers, helmet

// Takes the face texture and releases it.
void ReleaseFace(UnitClientSpecific* self) {
    self->faceTexture.Reset();
}
} // namespace

void UnitClientSpecific::Create() {
    sources[0] = nullptr;
    sources[1] = nullptr;
    sources[2] = nullptr;
    lastTime = 0.0f;
    motion.Reset();
    lastAnimationTime = 0.0f;
    lastAnimation = nullptr;
    weaponHidden = 0;
    arrowHidden = 0;
    snapHeading = 1;
    fadeIn = 1.0f;
    idleSound = kNone;
    otherSound = kNone;
    speechSound = kNone;
    lastSpeech = nullptr;
    lastSpeechTime = 0;
    flashCountdown = 0;
    footprintRepeats = kNone;
    footprint = kNone;
    blinks = 0;
    blinkTimer = 0;
    eyesClosed = 0;
    for (std::uint32_t& armourId : armourIds) armourId = kNoModel;
    for (std::uint32_t& weaponId : weaponIds) weaponId = kNoModel;
    ReleaseFace(this);
    blinks = 0;
}

void UnitClientSpecific::Destroy() {
    for (ParticleSource*& source : sources) {
        if (!source) continue;
        source->Stop();
        source->Release();
        source = nullptr;
    }
    ReleaseFace(this);
    blinks = 0;
}

UnitClientSpecific::~UnitClientSpecific() {
    for (ParticleSource*& source : sources) {
        if (!source) continue;
        source->Stop();
        source->Release();
        source = nullptr;
    }
    ReleaseFace(this);   // with null; the release test after it finds null
    blinks = 0;
    motion.steps.clear();
}

UnitClientSpecific::UnitClientSpecific() {
    fadeIn = 1.0f;
}

// --- Worn armour ------------------------------------------------------------------------------------

// The game's function reads armourIds[3..6] (+0xAC..+0xB8), the slots after the three dressed ones, not
// weaponIds: the name is provisional. Found when zone bz1g's units mismatched.
bool UnitClientSpecific::HasOtherArmour() {
    for (int slot = 3; slot < 7; ++slot)
        if (armourIds[slot] != kNoModel) return true;
    return false;
}

bool UnitClientSpecific::HasMainArmour() {
    for (int slot = 0; slot < kDressedArmourSlots; ++slot)
        if (armourIds[slot] != kNoModel) return true;
    return false;
}

// --- The equipment slots -----------------------------------------------------------------------------

// The client controller's equipment slots: the armour and weapon ids it shows and the hero's
// composed skins after a change. See unit_client_models.h and docs/notes/units.md.

namespace {
constexpr int kArmourSlots = 7;
constexpr int kWeaponSlots = 4;

// The skin texture of a part set, or null.
Texture* SkinTexture(const TextureParts* set) { return set ? set->texture : nullptr; }

// `texture` takes `name` and is loaded again (the common tail).
void Reload(Texture* texture, const std::string& name) {
    texture->name = name;
    texture->flags &= ~TextureFlag::Loaded;
    texture->Manager()->LoadTexture(texture);
}

// The test: a new skin, or a name that differs from the texture's (the texture read
// unchecked when `checkTexture` is false).
bool NeedsReload(bool composed, Texture* texture, const std::string& name, bool checkTexture) {
    if (composed) return true;
    if (checkTexture && !texture) return false;
    return str::Compare(name,
                           texture->GetName()) != 0;
}
} // namespace

void UnitClientSpecific::RefreshEquipment() {
    std::string name;
    // EI_Plugin's Fixes: the test for a hero is gone, every unit is refreshed.
    if (unit->MapType() != MapObjectType::Hero && !ei::rebirth::Enabled(ei::rebirth::Patch::EiPluginFixes)) return;
    auto* half = unit->AsObject3D()->ClientSpecific();
    Texture* detailed = SkinTexture(half->partSets[0]);
    if (NeedsReload(ComposeSkin(1, &name), detailed, name, false)) Reload(detailed, name);
    Texture* simple = SkinTexture(half->partSets[1]);
    if (NeedsReload(ComposeSkin(0, &name), simple, name, true) && simple) Reload(simple, name);
}

int UnitClientSpecific::RefreshModel() {
    if (unit->MapType() != MapObjectType::Hero) return 1;
    std::string name;
    auto* half = unit->AsObject3D()->ClientSpecific();
    Texture* simple = SkinTexture(half->partSets[1]);
    if (NeedsReload(ComposeSkin(0, &name), simple, name, true) && simple) Reload(simple, name);
    return 1;
}

void UnitClientSpecific::SetArmourModels(const std::uint32_t* ids) {
    for (int slot = 0; slot < kArmourSlots; ++slot) ArmourTakenOff(static_cast<std::uint32_t>(slot));
    for (std::uint32_t& armourId : armourIds) armourId = kNoItemId;
    for (int i = 0; i < kArmourSlots; ++i) ArmourPutOn(ids[i]);
}

void UnitClientSpecific::SetWeaponModels(const std::uint32_t* ids) {
    for (std::uint32_t& weaponId : weaponIds) weaponId = kNoItemId;
    for (int i = 0; i < kWeaponSlots; ++i) WeaponAdded(ids[i]);
}

bool UnitClientSpecific::WeaponAdded(std::uint32_t modelId) {
    if (modelId == kNoItemId) return false;
    int slot = 0;
    while (slot < kWeaponSlots && weaponIds[slot] != kNoItemId) ++slot;
    if (slot == kWeaponSlots) return false;
    if ((modelId & 0xFFFF0000u) == 0xFFFF0000u || (modelId & 0xFFFFu) == 0xFFFFu) return false;
    weaponIds[slot] = modelId;
    return true;
}

bool UnitClientSpecific::RemoveWeaponModel(std::uint32_t modelId) {
    for (std::uint32_t& slot : weaponIds) {
        if (slot == modelId) {
            slot = kNoItemId;
            return true;
        }
    }
    return false;
}

// --- Wounds, face texture and weapon point -----------------------------------------------------------

// A party unit's wounds, face texture and weapon point on the client (UnitClientSpecific): the wound level
// of each body part and the composed wounds texture the figure shows called by RefreshWounds See
// unit_client_models.h.

namespace {
constexpr int kBodyParts = 6;
constexpr float kFull = 1.0f;
constexpr float kMedium = 0.33f;
constexpr float kLight = 0.75f;
constexpr int kLegsLeft = 4, kLegsRight = 5, kHandLeft = 2, kHandRight = 3, kBody = 1, kHeadLevel = 0;
// armourIds: 0 the helmet, 1 the body armour, 2 the trousers.
constexpr int kHelmetSlot = 0, kBodyArmourSlot = 1, kTrousersSlot = 2;
constexpr std::uint32_t kNoArmour = 0xFFFFFFFF;

constexpr int kHeroTextureSize = 0x80;
constexpr int kTextureSize = 0x40;
constexpr int kMipCount = 3;
constexpr int kFormat = 0x4444;
constexpr const char* kWoundTexture = "%s%sw%d.mmp";
constexpr const char* kWoundCacheName = "mo%dw";
// The parts' texture names of a hero and of other units, each list ended by an empty name.
constexpr const char* kHeroParts[kBodyParts + 1] = /*0078F974*/ {"hd", "bd", "lh", "rh", "ll", "rl", ""};
constexpr const char* kParts[kBodyParts + 1] = /*0078F990*/ {"hd", "bd", "h", "h", "l", "l", ""};

// The level of one part (0 none .. 3 heavy).
std::uint8_t PartLevel(const UnitParamsSlot& part) {
    if (part.kind == 0 || part.maximum == 0) return 0;
    float maximum = static_cast<float>(part.maximum);
    float ratio = static_cast<float>(part.current) / maximum;
    EI_NAN_AUDIT(ratio, maximum);
    if (ratio == kFull) return 0;   // unhurt
    float stored = static_cast<float>(ratio);
    if (stored <= kFull / maximum) return 3;
    if (stored <= kMedium) return 2;
    if (stored >= kLight) return 0;
    return 1;
}

// Whether the armour piece `id` (packed: prototype index in the high half) shows the wounds
// under it. The record is read unchecked (an out-of-range index reads address 0xB4).
bool ShowsWounds(std::uint32_t id) {
    const auto* record = itemdb::ArmorAt(id >> 16);
    return record->showsWounds != 0;
}

std::uint8_t Worse(std::uint8_t a, std::uint8_t b) { return a > b ? a : b; }

constexpr int kFaceTextureKind = static_cast<int>(TextureKind::SkyFace);
constexpr const char* kHeroFace = "%smouth_%.2d";
constexpr const char* kFace = "%smouth";

constexpr const char* kHandNode = "baserh3";

} // namespace

bool UnitClientSpecific::WoundLevels(std::uint8_t** levels) {
    bool wounded = false;
    for (int i = 0; i < kBodyParts; ++i) {
        std::uint8_t level = PartLevel(unit->params.slots[i]);
        (*levels)[i] = level;
        if (level) wounded = true;
    }
    std::uint8_t* out = *levels;
    if (unit->MapType() == MapObjectType::Hero) {
        if (armourIds[kTrousersSlot] != kNoArmour && !ShowsWounds(armourIds[kTrousersSlot])) {
            out[kLegsRight] = 0;
            out[kLegsLeft] = 0;
        }
        if (armourIds[kBodyArmourSlot] != kNoArmour && !ShowsWounds(armourIds[kBodyArmourSlot])) {
            out[kHandRight] = 0;
            out[kHandLeft] = 0;
            out[kBody] = 0;
        }
        if (armourIds[kHelmetSlot] != kNoArmour && !ShowsWounds(armourIds[kHelmetSlot])) out[kHeadLevel] = 0;
        return wounded;
    }
    out[kLegsLeft] = Worse(out[kLegsLeft], out[kLegsRight]);
    out[kLegsRight] = out[kLegsLeft];
    out[kHandLeft] = Worse(out[kHandLeft], out[kHandRight]);
    out[kHandRight] = out[kHandLeft];
    return wounded;
}

int UnitClientSpecific::ComposeWoundsTexture() {
    int result = 0;
    {
        std::string name;
        std::string out;
        SkinRequest request{};
        // Six level bytes.
        request.layers.resize(kBodyParts);
        std::uint8_t* levels = request.layers.data();
        if (WoundLevels(&levels)) {
            bool hero = unit->MapType() == MapObjectType::Hero;
            request.flags = hero ? kHeroTextureSize : kTextureSize;
            for (int i = 0; i < kBodyParts; ++i) {
                std::uint8_t level = request.layers[i];
                if (!level) continue;
                name = str::Format(kWoundTexture, unit->name, unit->MapType() == MapObjectType::Hero ? kHeroParts[i] : kParts[i],
                            static_cast<int>(level));
                request.textures.push_back(name);
            }
            name = str::Format(kWoundCacheName, unit->id);
            result = g_clientWorld.textureManager->Compose(&out, &request, &name, &name, kMipCount, kFormat);
        }
    }
    return result;
}

// --- The face texture -------------------------------------------------------------------------

bool UnitClientSpecific::LoadFaceTexture() {
    if (faceTexture) return true;
    const auto* prototype = unit->GetPrototype();
    if (!prototype) return false;
    char name[0x400];
    if (unit->MapType() == MapObjectType::Hero) std::snprintf(name, sizeof(name), kHeroFace, unit->name.c_str(), prototype->skin);
    else std::snprintf(name, sizeof(name), kFace, unit->name.c_str());
    Texture* texture = g_clientWorld.textureManager->FindOrCreate(kFaceTextureKind, name);
    faceTexture.Assign(texture);
    g_clientWorld.textureManager->LoadTexture(faceTexture);
    if (faceTexture->flags & TextureFlag::Loaded) return true;
    faceTexture.Reset();
    blinks = 0;
    return false;
}

// --- The weapon point -------------------------------------------------------------------------

std::optional<Vector3> UnitClientSpecific::WeaponPoint() {
    std::uint32_t slot = unit->activeWeapon;
    std::uint32_t weapon = slot < kWeaponSlots ? weaponIds[slot] : kNoItemId;
    std::string node = WeaponBaseFigure(weapon);
    if (node.empty()) node = kHandNode;
    const FigureBone* found = unit->AsObject3D()->animation->FindBone(node);
    if (!found) return std::nullopt;
    // The bone's position in figure space.
    return found->position;
}
