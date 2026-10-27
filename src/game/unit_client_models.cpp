// How the client dresses a unit: armour figure parts and the composed skin texture. See
// unit_client_models.h and docs/notes/units.md ("Creation, databases, save").
#include "figure_manager.h"
#include "item.h"
#include "resource.h"
#include "texture.h"
#include "unit_client_models.h"
#include "game_strings.h"

#include "item_db.h"
#include "map_object_figures.h"
#include "unit.h"
#include "unit_db.h"

#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/mfc.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/mfc_array.h"
#include "game_data.h"
#include "game_singletons.h"
#include "lib/nan_audit.h"
#include "lib/mfc_slots.h"

#include <cstring>
#include <new>

namespace {
// The item prototypes' head (type: an armour's slot, 0 helmet, 1 body, 2 trousers, ..., or a
// weapon's type; number: the number in "armor<nn>") and the unit prototypes (hair, -1 none; skin).
constexpr std::uint32_t kNone = 0xFFFFFFFFu;
constexpr std::uint32_t kFiguresDetached = 0x80;   // the unit's flags_, passed

constexpr int kLayerSkin = 0x32;
constexpr int kLayerArmour = 0x3005;
constexpr int kLayerWeapon = 0x3004;
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

// The compiler-inlined strcmp of the original (bytes compared unsigned): zero when equal.
bool SameName(const char* a, const char* b) { return std::strcmp(a, b) == 0; }

const itemdb::ArmorRecord* ArmourRecord(std::uint32_t type) { return itemdb::ArmorAt(type); }

// Figure manager: the model named `name`.
// 3D object: its figure part named `name`, or null.
// SkinLayerArray::SetSize (the 12-byte path records' instantiation).
void SetLayerCount(SkinLayerArray* layers, int size) { mfc::impl::ArrayAs<mfc::impl::PathRecords>(*layers).SetSize(size, -1); }

std::uint32_t FiguresFlag(const CUnit* unit) { return unit->flags_ & kFiguresDetached; }
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
    CFigureManager* manager = TheFigureManager();
    int row = RowOfSlot(static_cast<int>(slot));
    if (row < 0) return true;

    FigureModel* model;
    {
        stlp::string name(unit->name);
        model = manager->FindModel(&name);
    }
    if (!model) return false;
    const itemdb::ArmorRecord* armour = ArmourRecord(type);
    mfc::CString armourPart;
    for (int i = 0; i < 7; ++i) {
        const char* plain = kArmourPartNames[row][i];
        if (!plain[0]) break;
        mfc::CString part(plain);
        if (gamecrt::mbscmp(part.GetString(),
                            kHair) == 0) {
            const UnitPrototypeRecord* prototype = unitdb::PrototypeRecord(static_cast<std::uint32_t>(unit->prototypeId_));
            int hair = prototype->hair;
            if (hair == -1) continue;
            part.Format("hr.%.2d", hair);
        }
        CFigureMesh* current = unit->FindPartByPrefix(part);
        armourPart.Format("%s.armor%.2d", part.GetString(), armour->number);
        if (model->mesh->FindEntry(armourPart) != -1) {
            unit->LoadPart(armourPart, model, FiguresFlag(unit));
            if (!SameName(plain, kHead)) unit->RemovePart(current);
        } else if (current) {
            if (SameName(plain, kHair)) {
                unit->RemovePart(current);
            } else if (gamecrt::mbscmp(part.GetString(),
                                       current->GetName()) != 0) {
                unit->LoadPart(part, model, FiguresFlag(unit));
                unit->RemovePart(current);
            }
        } else {
            unit->LoadPart(part, model, FiguresFlag(unit));
        }
    }
    return true;
}
EI_REWRITTEN(0x005381B0, CUnitClientSpecific::ArmourPutOn);

namespace {
// The figure parts collects: an array in 32-byte aligned storage (the block kept at
// data[-1]), as SkinRequest's layers.
using PartArray = FigurePartArray;

int CompareNames(const char* a, const char* b) {
    return gamecrt::mbscmp(a, b);
}
} // namespace

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
        stlp::string name(unit->name);
        model = TheFigureManager()->FindModel(&name);
    }
    if (!model) return false;   // the slot stays cleared
    for (int i = 0; i < 7; ++i) {
        const char* plain = kArmourPartNames[row][i];
        if (!plain[0]) break;
        mfc::CString part(plain);
        if (CompareNames(part, kHair) == 0) {
            const UnitPrototypeRecord* prototype = unitdb::PrototypeRecord(static_cast<std::uint32_t>(unit->prototypeId_));
            int hair = prototype->hair;
            if (hair == -1) continue;
            part.Format("hr.%.2d", hair);
        }
        PartArray parts{nullptr, 0, 0, 0};
        unit->CollectPartsByPrefix(part, &parts);
        if (parts.size == 1) {
            // One part of that name: replaced only when it is not the plain part already.
            if (CompareNames(part, parts.data[0]->GetName()) != 0) {
                unit->RemovePart(parts.data[0]);
                unit->LoadPart(part, model, FiguresFlag(unit));
            }
        } else {
            for (int k = 0; k < parts.size; ++k) unit->RemovePart(parts.data[k]);
            unit->LoadPart(part, model, FiguresFlag(unit));
        }
        if (parts.data) lib::FreeAligned32(parts.data);
    }
    return true;
}
EI_REWRITTEN(0x005385A0, CUnitClientSpecific::ArmourTakenOff);

namespace {

constexpr int kWeaponBow = 5;
constexpr std::uint32_t kArrowPartFlag = 0x80;  // set in the arrow part's flags_
const char* const kBowArrow = "rh3.arrow00";

std::uint32_t WeaponIdOf(const CUnitClientSpecific* self, std::uint32_t slot) {
    return slot < 4 ? self->weaponIds[slot] : kNone;
}
} // namespace

int CUnitClientSpecific::ShowWeapon(std::uint32_t slot) {
    std::uint32_t previous = WeaponIdOf(this, unit->activeWeapon);
    std::uint32_t next = WeaponIdOf(this, slot);
    console::CStringArray names;
    FigureModel* model;
    {
        stlp::string name(unit->name);
        model = TheFigureManager()->FindModel(&name);
    }
    if (previous != kNone) {
        WeaponModelParts(previous >> 16, &names);
        for (int i = 0; i < names.GetSize(); ++i) unit->RemovePartNamed(names[i]);
    }
    unit->activeWeapon = slot;
    weaponHidden = 0;
    if (next != kNone) {
        WeaponModelParts(next >> 16, &names);
        for (int i = 0; i < names.GetSize(); ++i) {
            CEngineObject* part = unit->LoadPart(names[i], model, FiguresFlag(unit));
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
    // The controller's slot 30 (RefreshModel), after the change (the unit's controller is this object).
    unit->Controller()->RefreshModel();
    return 1;
}
EI_REWRITTEN(0x00535590, CUnitClientSpecific::ShowWeapon);

// --- The weapon in hand and the bow's arrow during animations ------------------------------

void CUnitClientSpecific::ShowWeaponParts(bool hidden) {
    if (weaponHidden == static_cast<std::uint8_t>(hidden)) return;
    std::uint32_t hand = WeaponIdOf(this, unit->activeWeapon);
    console::CStringArray names;
    if (hand != kNone) WeaponModelParts(hand >> 16, &names);
    for (int i = 0; i < names.GetSize(); ++i) {
        if (CEngineObject* part = unit->FindPart(names[i])) {
            if (hidden) part->flags_ |= kArrowPartFlag;
            else part->flags_ &= ~kArrowPartFlag;
        }
    }
    weaponHidden = hidden;
    arrowHidden = hidden;
}
EI_REWRITTEN(0x00537CB0, CUnitClientSpecific::ShowWeaponParts);

namespace {
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
} // namespace

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
EI_REWRITTEN(0x00538C20, CUnitClientSpecific::UpdateArrow);

void __fastcall CollectArmourPartNames(const UnitPrototypeRecord* prototype, const stlp::vector<std::uint32_t>* armourIds,
                                       const stlp::vector<std::uint32_t>* weaponIds,
                                       stlp::vector<stlp::string>* names, stlp::vector<stlp::string>* plainNames) {
    char text[0x401];
    for (const std::uint32_t* id = armourIds->begin(); id != armourIds->end(); ++id) {
        const itemdb::ArmorRecord* armour = ArmourRecord(*id >> 16);
        if (!armour) continue;
        int row = RowOfSlot(armour->type);
        if (row < 0) continue;
        for (int i = 0; i < 7; ++i) {
            const char* plain = kArmourPartNames[row][i];
            if (!plain[0]) break;
            if (SameName(plain, kHair)) {
                int hair = prototype->hair;
                if (hair == -1) continue;
                gamecrt::sprintf(text, "hr.%.2d.armor%.2d", hair, armour->number);
            } else {
                gamecrt::sprintf(text, "%s.armor%.2d", plain, armour->number);
            }
            stlp::string name(text);
            names->push_back(name);
        }
    }
    for (const std::uint32_t* id = weaponIds->begin(); id != weaponIds->end(); ++id) AppendWeaponModelParts(*id, names);

    // Every plain part of the three rows. The original walks all seven names of each row (the
    // test on the name's address never fails), so the empty names of the helmet row go in too.
    for (int row = 0; row < 3; ++row) {
        for (int i = 0; i < 7; ++i) {
            const char* plain = kArmourPartNames[row][i];
            if (SameName(plain, kHair)) {
                int hair = prototype->hair;
                if (hair == -1) continue;
                gamecrt::sprintf(text, "hr.%.2d", hair);
                stlp::string name(text);
                plainNames->push_back(name);
            } else {
                stlp::string name(plain);
                plainNames->push_back(name);
            }
        }
    }
}
EI_REWRITTEN(0x00535CB0, CollectArmourPartNames);

// --- Skin -------------------------------------------------------------------------------------

void CUnitClientSpecific::BuildSkinLayers(int detailed, SkinLayerArray* out) {
    const itemdb::ArmorTable& armours = itemdb::TheItemDatabase().armors;
    if (!detailed) {
        SetLayerCount(out, 0);
        SetLayerCount(out, kSimpleLayers);
        std::uint32_t id = armourIds[0];
        if (id != kNone) {
            if (const itemdb::ArmorRecord* armour = armours.At(id >> 16)) {
                out->data[0].id = id;
                out->data[0].kind = kLayerArmour;
                out->data[0].slot = armour->type;
            }
        }
        std::uint32_t hand = unit->activeWeapon;
        if (hand < 4 && (id = weaponIds[hand]) != kNone) {
            if (const itemdb::WeaponRecord* weapon = itemdb::WeaponAt(id >> 16)) {
                out->data[1].id = id;
                out->data[1].kind = kLayerWeapon;
                out->data[1].slot = weapon->type;
            }
        }
        return;
    }
    SetLayerCount(out, 0);
    // The original inlines SetSize(8) here; the array is empty, so it allocates 8 fresh layers.
    SetLayerCount(out, kDetailedLayers);
    out->data[0].id = static_cast<std::uint32_t>(unit->prototypeId_);
    out->data[0].kind = kLayerSkin;
    out->data[0].slot = -1;
    for (int k = 0; k < 7; ++k) {
        std::uint32_t id = armourIds[k];
        if (id == kNone) continue;
        const itemdb::ArmorRecord* armour = armours.At(id >> 16);
        if (!armour) continue;
        out->data[k + 1].id = id;
        out->data[k + 1].kind = kLayerArmour;
        out->data[k + 1].slot = armour->type;
    }
}
EI_REWRITTEN(0x00536300, CUnitClientSpecific::BuildSkinLayers);

namespace {

// "<prototype name><texture>.mmp" into `texture`, the way the original builds it.
void AddPrototypePrefix(const CUnit* unit, mfc::CString& texture) {
    mfc::CString named = mfc::operator+(unit->name, texture);
    mfc::CString withExtension = mfc::operator+(named, ".mmp");
    texture = withExtension;
}

void AddTexture(SkinRequest& request, const mfc::CString& texture) {
    request.textures.SetAtGrow(request.textures.size, texture);
}

void AddArmourTexture(SkinRequest& request, const CUnit* unit, mfc::CString& texture, std::uint32_t id) {
    ArmorModelName(&texture, id >> 16, id & 0xFFFF);
    AddPrototypePrefix(unit, texture);
    AddTexture(request, texture);
}
} // namespace

namespace {
// The layer texture names and the two keys of a skin.
void NameSkinTextures(const CUnitClientSpecific* self, int detailed, SkinRequest& request, mfc::CString& unitKey,
                      mfc::CString& prototypeKey, mfc::CString& texture) {
    const CUnit* unit = self->unit;
    int id = unit->id_;
    if (!detailed) {
        unitKey.Format("mo%ds", id);
        prototypeKey.Format("prot%ds", unit->prototypeId_);
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
    unitKey.Format("mo%dp", id);
    prototypeKey.Format("prot%dp", unit->prototypeId_);
    request.flags = kFlagDetailed;
    const UnitPrototypeRecord* prototype = unitdb::PrototypeRecord(static_cast<std::uint32_t>(unit->prototypeId_));
    if (prototype)
        texture.Format("%sskin_%.2d.mmp", unit->name.GetString(), prototype->skin);
    else
        texture.Format("%sskin_00.mmp", unit->name.GetString());
    AddTexture(request, texture);
    for (int k = 3; k < 7; ++k)
        if (self->armourIds[k] != kNone) AddArmourTexture(request, unit, texture, self->armourIds[k]);
    // Body armour and trousers, in the armours' drawing order (ArmorFieldB8).
    std::uint32_t body = self->armourIds[1];
    std::uint32_t legs = self->armourIds[2];
    if (body == kNone || legs == kNone) {
        std::uint32_t only = body != kNone ? body : legs;
        if (only != kNone) AddArmourTexture(request, unit, texture, only);
        return;
    }
    std::uint32_t bodyOrder = static_cast<std::uint32_t>(ArmorFieldB8(body >> 16));
    std::uint32_t legsOrder = static_cast<std::uint32_t>(ArmorFieldB8(legs >> 16));
    if (legsOrder < bodyOrder) {
        AddArmourTexture(request, unit, texture, legs);
        AddArmourTexture(request, unit, texture, body);
    } else {
        AddArmourTexture(request, unit, texture, body);
        AddArmourTexture(request, unit, texture, legs);
    }
}
} // namespace

void* CUnitClientSpecific::ComposeSkin(int detailed, void* target) {
    CTextureManager* textureManager = TheTextureManager();
    SkinLayerArray layers{mfc::impl::SkinLayerArrayClass::Vtable(), nullptr, 0, 0, 0};
    void* result;
    {
        mfc::CString unitKey;
        mfc::CString prototypeKey;
        mfc::CString texture;   // the work string
        // The request lives in raw storage so that its string array is destroyed before its
        // layer bytes are freed and before the strings, as in the original.
        alignas(SkinRequest) unsigned char storage[sizeof(SkinRequest)];
        auto* request = new (storage) SkinRequest{{}, {}, 0};
        BuildSkinLayers(detailed, &layers);
        int bytes = layers.size * static_cast<int>(sizeof(SkinLayer));
        request->layers.SetSize(bytes);   // the aligned CArray SetSize, inlined in the original
        std::memcpy(request->layers.data, layers.data, static_cast<std::size_t>(bytes));
        NameSkinTextures(this, detailed, *request, unitKey, prototypeKey, texture);
        result = reinterpret_cast<void*>(
            textureManager->Compose(static_cast<mfc::CString*>(target), request, &unitKey, &prototypeKey, 3, 0, 0x5551));
        request->textures.~CStringArray();
        request->layers.FreeData();
    }
    if (layers.data) gamecrt::operator_delete(layers.data);
    return result;
}
EI_REWRITTEN(0x00536670, CUnitClientSpecific::ComposeSkin);
