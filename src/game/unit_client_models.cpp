// How the client dresses a unit: armour figure parts and the composed skin texture. See
// unit_client_models.h and docs/notes/units.md ("Creation, databases, save").
#include "unit_client_models.h"

#include "ai_map.h"
#include "effects.h"
#include "ei/x87.h"
#include "figure_manager.h"
#include "game_singletons.h"
#include "item.h"
#include "item_db.h"
#include "item_names.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/nan_audit.h"
#include "lib/str.h"
#include "map_object_figures.h"
#include "resource.h"
#include "texture.h"
#include "unit.h"
#include "unit_db.h"

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
    double at = x87::FromUnsigned(frame);
    EI_NAN_AUDIT(now);
    return at > last && at <= now;
}

// "<prototype name><texture>.mmp" into `texture`.
void AddPrototypePrefix(const CUnit* unit, std::string& texture) {
    std::string named = unit->name + std::string(texture);
    std::string withExtension = named + ".mmp";
    texture = withExtension;
}

void AddTexture(SkinRequest& request, const std::string& texture) {
    request.textures.push_back(texture);
}

void AddArmourTexture(SkinRequest& request, const CUnit* unit, std::string& texture, std::uint32_t id) {
    texture = ArmorModelName(PrototypeIndex(id), MaterialIndex(id));
    AddPrototypePrefix(unit, texture);
    AddTexture(request, texture);
}

// The layer texture names and the two keys of a skin.
void NameSkinTextures(const UnitClientSpecific* self, int detailed, SkinRequest& request, std::string& unitKey,
                      std::string& prototypeKey, std::string& texture) {
    const CUnit* unit = self->unit;
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
    CFigureManager* manager = g_clientWorld.figureManager;
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
    const FigureAnimationState* state = unit->Object3D()->animation;
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

void* UnitClientSpecific::ComposeSkin(int detailed, void* target) {
    CTextureManager* textureManager = g_clientWorld.textureManager;
    void* result;
    {
        std::string unitKey;
        std::string prototypeKey;
        std::string texture;   // the work string
        SkinRequest request{};
        const std::vector<SkinLayer> layers = BuildSkinLayers(detailed);
        int bytes = std::ssize(layers) * static_cast<int>(sizeof(SkinLayer));
        request.layers.resize(bytes);
        std::memcpy(request.layers.data(), layers.data(), static_cast<std::size_t>(bytes));
        NameSkinTextures(this, detailed, request, unitKey, prototypeKey, texture);
        result = reinterpret_cast<void*>(
            textureManager->Compose(static_cast<std::string*>(target), &request, &unitKey, &prototypeKey, 3, 0x5551));
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
