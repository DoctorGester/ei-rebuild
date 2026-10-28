// How the client dresses a unit: armour figure parts and the composed skin texture. See
// unit_client_models.h and docs/notes/units.md ("Creation, databases, save").
#include "figure_manager.h"
#include "item.h"
#include "resource.h"
#include "texture.h"
#include "unit_client_models.h"

#include "item_db.h"
#include "map_object_figures.h"
#include "unit.h"
#include "unit_db.h"

#include "lib/containers.h"
#include "lib/crt.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include "ei/x87.h"
#include "game_singletons.h"
#include "lib/nan_audit.h"
#include "lib/str.h"

#include <cstdio>
#include <new>
#include <vector>

namespace {
// The item prototypes' head (type: an armour's slot, 0 helmet, 1 body, 2 trousers, ..., or a
// weapon's type; number: the number in "armor<nn>") and the unit prototypes (hair, -1 none; skin).
constexpr std::uint32_t kNone = 0xFFFFFFFFu;

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
constexpr std::uint32_t kArrowPartFlag = 0x80;  // set in the arrow part's flags_
const char* const kBowArrow = "rh3.arrow00";

std::uint32_t WeaponIdOf(const CUnitClientSpecific* self, std::uint32_t slot) {
    return slot < 4 ? self->weaponIds[slot] : kNone;
}

// The bow's arrow part of a hero holding a bow, or null.
CEngineObject* BowArrow(CUnitClientSpecific* self) {
    std::uint32_t hand = WeaponIdOf(self, self->unit->activeWeapon);
    if (hand == kNone || ::WeaponType(hand) != kWeaponBow) return nullptr;
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
    ArmorModelName(&texture, id >> 16, id & 0xFFFF);
    AddPrototypePrefix(unit, texture);
    AddTexture(request, texture);
}

// The layer texture names and the two keys of a skin.
void NameSkinTextures(const CUnitClientSpecific* self, int detailed, SkinRequest& request, std::string& unitKey,
                      std::string& prototypeKey, std::string& texture) {
    const CUnit* unit = self->unit;
    int id = unit->id_;
    if (!detailed) {
        unitKey = str::Format("mo%ds", id);
        prototypeKey = str::Format("prot%ds", unit->prototypeId_);
        request.flags = kFlagSimple;
        if (self->armourIds[0] != kNone) AddArmourTexture(request, unit, texture, self->armourIds[0]);
        std::uint32_t hand = unit->activeWeapon;
        if (hand < 4 && self->weaponIds[hand] != kNone) {
            WeaponModelName(&texture, self->weaponIds[hand] >> 16, self->weaponIds[hand] & 0xFFFF);
            AddPrototypePrefix(unit, texture);
            AddTexture(request, texture);
        }
        return;
    }
    unitKey = str::Format("mo%dp", id);
    prototypeKey = str::Format("prot%dp", unit->prototypeId_);
    request.flags = kFlagDetailed;
    const UnitPrototypeRecord* prototype = unitdb::PrototypeRecord(static_cast<std::uint32_t>(unit->prototypeId_));
    if (prototype)
        texture = str::Format("%sskin_%.2d.mmp", unit->name, prototype->skin);
    else
        texture = str::Format("%sskin_00.mmp", unit->name);
    AddTexture(request, texture);
    for (int k = 3; k < 7; ++k)
        if (self->armourIds[k] != kNone) AddArmourTexture(request, unit, texture, self->armourIds[k]);
    // Body armour and trousers, in the armours' drawing order (ArmorLayer).
    std::uint32_t body = self->armourIds[1];
    std::uint32_t legs = self->armourIds[2];
    if (body == kNone || legs == kNone) {
        std::uint32_t only = body != kNone ? body : legs;
        if (only != kNone) AddArmourTexture(request, unit, texture, only);
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

bool CUnitClientSpecific::ArmourPutOn(std::uint32_t itemId) {
    if (itemId == kNone) return false;
    std::uint32_t type = itemId >> 16;
    if (type == 0xFFFF || (itemId & 0xFFFF) == 0xFFFF) return false;
    std::uint32_t slot = static_cast<std::uint32_t>(ArmourRecord(type)->type);
    if (slot >= 7) return false;
    if (armourIds[slot] == itemId) return true;
    armourIds[slot] = itemId;
    CFigureManager* manager = g_clientWorld->figureManager_;
    int row = RowOfSlot(static_cast<int>(slot));
    if (row < 0) return true;

    FigureModel* model;
    {
        std::string name = unit->name;
        model = manager->FindModel(&name);
    }
    if (!model) return false;
    const itemdb::ArmorRecord* armour = ArmourRecord(type);
    std::string armourPart;
    for (int i = 0; i < 7; ++i) {
        const char* plain = kArmourPartNames[row][i];
        if (!plain[0]) break;
        std::string part = plain;
        if (str::Compare(part,
                            kHair) == 0) {
            const UnitPrototypeRecord* prototype = unitdb::PrototypeRecord(static_cast<std::uint32_t>(unit->prototypeId_));
            int hair = prototype->hair;
            if (hair == -1) continue;
            part = str::Format("hr.%.2d", hair);
        }
        CFigureMesh* current = unit->FindPartByPrefix(part);
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

bool CUnitClientSpecific::ArmourTakenOff(std::uint32_t slot) {
    if (slot >= 7) return false;
    std::uint32_t itemId = armourIds[slot];
    if (itemId == kNone) return false;
    if ((itemId >> 16) == 0xFFFF || (itemId & 0xFFFF) == 0xFFFF) return false;
    armourIds[slot] = kNone;
    int row = RowOfSlot(static_cast<int>(slot));
    if (row < 0) return true;

    FigureModel* model;
    {
        std::string name = unit->name;
        model = g_clientWorld->figureManager_->FindModel(&name);
    }
    if (!model) return false;   // the slot stays cleared
    for (int i = 0; i < 7; ++i) {
        const char* plain = kArmourPartNames[row][i];
        if (!plain[0]) break;
        std::string part = plain;
        if (str::Compare(part, kHair) == 0) {
            const UnitPrototypeRecord* prototype = unitdb::PrototypeRecord(static_cast<std::uint32_t>(unit->prototypeId_));
            int hair = prototype->hair;
            if (hair == -1) continue;
            part = str::Format("hr.%.2d", hair);
        }
        std::vector<CFigureMesh*> parts;
        unit->CollectPartsByPrefix(part, &parts);
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
        lib::FreeStorage(parts);
    }
    return true;
}

int CUnitClientSpecific::ShowWeapon(std::uint32_t slot) {
    std::uint32_t previous = WeaponIdOf(this, unit->activeWeapon);
    std::uint32_t next = WeaponIdOf(this, slot);
    std::vector<std::string> names;
    {
        std::string name = unit->name;
        g_clientWorld->figureManager_->FindModel(&name);   // loads and caches the model
    }
    if (previous != kNone) {
        WeaponModelParts(previous >> 16, &names);
        for (const std::string& name : names) unit->RemovePartNamed(name);
    }
    unit->activeWeapon = slot;
    weaponHidden = 0;
    if (next != kNone) {
        WeaponModelParts(next >> 16, &names);
        for (const std::string& name : names) {
            CEngineObject* part = unit->LoadPart(name);
            if (part) unit->AttachPartMesh(part);
        }
        arrowHidden = 0;
        std::uint32_t hand = WeaponIdOf(this, unit->activeWeapon);
        if (hand != kNone && ::WeaponType(hand) == kWeaponBow) {
            if (CEngineObject* arrow = unit->FindPart(kBowArrow)) {
                arrow->flags_ |= kArrowPartFlag;
                arrowHidden = 1;
            }
        }
    }
    // RefreshModel after the change (the unit's controller is this object).
    unit->Controller()->RefreshModel();
    return 1;
}

// --- The weapon in hand and the bow's arrow during animations ------------------------------

void CUnitClientSpecific::ShowWeaponParts(bool hidden) {
    if (weaponHidden == static_cast<std::uint8_t>(hidden)) return;
    std::uint32_t hand = WeaponIdOf(this, unit->activeWeapon);
    std::vector<std::string> names;
    if (hand != kNone) WeaponModelParts(hand >> 16, &names);
    for (const std::string& name : names) {
        if (CEngineObject* part = unit->FindPart(name)) {
            if (hidden) part->flags_ |= kArrowPartFlag;
            else part->flags_ &= ~kArrowPartFlag;
        }
    }
    weaponHidden = hidden;
    arrowHidden = hidden;
}

void CUnitClientSpecific::UpdateArrow() {
    if (unit->type_ != kUnitTypeHero) return;
    const CFigureAnimationState* state = unit->Object3D()->animation;
    const FigureAnimation* current = state->current;
    if (current && (current->flags & 1)) {
        float now = state->time;
        EI_NAN_AUDIT(now, lastAnimationTime);
        if (now == lastAnimationTime) return;   // the same time
        if (Passed(current->arrowFrom, lastAnimationTime, now)) {
            if (!arrowHidden) return;
            if (CEngineObject* arrow = BowArrow(this)) {
                arrow->flags_ &= ~kArrowPartFlag;
                arrowHidden = 0;
            }
            return;
        }
        if (!Passed(current->arrowTo, lastAnimationTime, now)) return;
    }
    if (arrowHidden == 1) return;
    if (CEngineObject* arrow = BowArrow(this)) {
        arrow->flags_ |= kArrowPartFlag;
        arrowHidden = 1;
    }
}

void CollectArmourPartNames(const UnitPrototypeRecord* prototype, const std::vector<std::uint32_t>* armourIds,
                                       const std::vector<std::uint32_t>* weaponIds,
                                       std::vector<std::string>* names, std::vector<std::string>* plainNames) {
    char text[0x401];
    for (const auto& id : *armourIds) {
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
            std::string name(text);
            names->push_back(name);
        }
    }
    for (const auto& id : *weaponIds) AppendWeaponModelParts(id, names);

    // Every plain part of the three rows. All seven names of each row are walked (the
    // test on the name's address never fails), so the empty names of the helmet row go in too.
    for (int row = 0; row < 3; ++row) {
        for (int i = 0; i < 7; ++i) {
            const char* plain = kArmourPartNames[row][i];
            if (SameName(plain, kHair)) {
                int hair = prototype->hair;
                if (hair == -1) continue;
                std::snprintf(text, sizeof(text), "hr.%.2d", hair);
                std::string name(text);
                plainNames->push_back(name);
            } else {
                std::string name(plain);
                plainNames->push_back(name);
            }
        }
    }
}

// --- Skin -------------------------------------------------------------------------------------

void CUnitClientSpecific::BuildSkinLayers(int detailed, std::vector<SkinLayer>* out) {
    const itemdb::ArmorTable& armours = g_itemDatabase->armors;
    if (!detailed) {
        out->assign(kSimpleLayers, SkinLayer{});
        std::uint32_t id = armourIds[0];
        if (id != kNone) {
            if (const itemdb::ArmorRecord* armour = armours.At(id >> 16)) {
                (*out)[0].id = id;
                (*out)[0].kind = kItemArmor;
                (*out)[0].slot = armour->type;
            }
        }
        std::uint32_t hand = unit->activeWeapon;
        if (hand < 4 && (id = weaponIds[hand]) != kNone) {
            if (const itemdb::WeaponRecord* weapon = itemdb::WeaponAt(id >> 16)) {
                (*out)[1].id = id;
                (*out)[1].kind = kItemWeapon;
                (*out)[1].slot = weapon->type;
            }
        }
        return;
    }
    out->assign(kDetailedLayers, SkinLayer{});
    (*out)[0].id = static_cast<std::uint32_t>(unit->prototypeId_);
    (*out)[0].kind = kLayerSkin;
    (*out)[0].slot = -1;
    for (int k = 0; k < 7; ++k) {
        std::uint32_t id = armourIds[k];
        if (id == kNone) continue;
        const itemdb::ArmorRecord* armour = armours.At(id >> 16);
        if (!armour) continue;
        (*out)[k + 1].id = id;
        (*out)[k + 1].kind = kItemArmor;
        (*out)[k + 1].slot = armour->type;
    }
}

void* CUnitClientSpecific::ComposeSkin(int detailed, void* target) {
    CTextureManager* textureManager = g_clientWorld->textureManager_;
    std::vector<SkinLayer> layers;
    void* result;
    {
        std::string unitKey;
        std::string prototypeKey;
        std::string texture;   // the work string
        SkinRequest request{};
        BuildSkinLayers(detailed, &layers);
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
