// How the client dresses a unit: armour figure parts and the composed skin texture. See
// unit_client_models.h and docs/notes/units.md ("Creation, databases, save").
#include "figure_manager.h"
#include "item.h"
#include "resource.h"
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
#include "ei/vcall.h"
#include "ei/x87.h"
#include "originals_engine.h"
#include "originals_gameplay.h"
#include "originals_units.h"
#include "lib/mfc_array.h"
#include "game_data.h"

#include <cstring>
#include <new>

namespace {
// The item prototypes' head (type: an armour's slot, 0 helmet, 1 body, 2 trousers, ..., or a
// weapon's type; number: the number in "armor<nn>") and the unit prototypes (hair, -1 none; skin).
const itemdb::ItemRecordHead* ItemHead(const char* record) { return reinterpret_cast<const itemdb::ItemRecordHead*>(record); }
const UnitPrototypeRecord* Prototype(const char* record) { return reinterpret_cast<const UnitPrototypeRecord*>(record); }
constexpr std::uint32_t kNone = 0xFFFFFFFFu;
constexpr std::uint32_t kFiguresDetached = 0x80;   // the unit's flags_, passed on to 00517240

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

const char* ArmourRecord(std::uint32_t type) { return itemdb::Armors().RecordAt(type, itemdb::kArmorSize); }

// The figure manager (global 007AF5F4) and the texture manager (007AF744).
void* FigureManager() { return ei::GameData<void* const>(0x007AF5F4); }
void* TextureManager() { return ei::GameData<void* const>(0x007AF744); }

// Not rewritten, forwarders:
// 005AA5F0. Figure manager: the model named `name`.
// 00516A80. 3D object: its figure part named `name`, or null.
CEngineObject* __fastcall FindPart(CUnit* a0, void*, const char* a2) { return static_cast<CObject3D*>((void*)a0)->FindPartByPrefix(a2); }
// 00517240. 3D object: adds the figure part `name` of `model`; returns it (null when the model has
// no such part).
CEngineObject* __fastcall AddPart(CUnit* unit, void*, const char* name, void* model, std::uint32_t arg) {
    return static_cast<CEngineObject*>(static_cast<CObject3D*>(unit)->LoadPart(name, model, arg));
}
// 00539580. SkinLayerArray::SetSize (the 12-byte path records' instantiation).
void SetLayerCount(SkinLayerArray* layers, int size) { mfc::impl::ArrayAs<mfc::impl::PathRecords>(*layers).SetSize(size, -1); }

std::uint32_t FiguresFlag(const CUnit* unit) { return unit->flags_ & kFiguresDetached; }
} // namespace

// --- Armour figure parts ----------------------------------------------------------------------

bool CUnitClientSpecific::WearArmourModel(std::uint32_t itemId) {
    if (itemId == kNone) return false;
    std::uint32_t type = itemId >> 16;
    if (type == 0xFFFF || (itemId & 0xFFFF) == 0xFFFF) return false;
    std::uint32_t slot = static_cast<std::uint32_t>(ItemHead(ArmourRecord(type))->type);
    if (slot >= 7) return false;
    if (armourIds[slot] == itemId) return true;
    armourIds[slot] = itemId;
    void* manager = FigureManager();
    int row = RowOfSlot(static_cast<int>(slot));
    if (row < 0) return true;

    void* model;
    {
        stlp::string name(unit->name);
        model = static_cast<CFigureManager*>(manager)->FindModel(&name);
    }
    if (!model) return false;
    const char* armour = ArmourRecord(type);
    mfc::CString armourPart;
    for (int i = 0; i < 7; ++i) {
        const char* plain = ArmourPartNames()[row][i];
        if (!plain[0]) break;
        mfc::CString part(plain);
        if (gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(static_cast<const char*>(part)),
                            reinterpret_cast<const unsigned char*>(kHair)) == 0) {
            const char* prototype = unitdb::Prototypes().RecordAt(static_cast<std::uint32_t>(unit->prototypeId_),
                                                             unitdb::kPrototypeSize);
            int hair = Prototype(prototype)->hair;
            if (hair == -1) continue;
            part.Format("hr.%.2d", hair);
        }
        CEngineObject* current = FindPart(unit, nullptr, part);
        armourPart.Format("%s.armor%.2d", static_cast<const char*>(part), ItemHead(armour)->number);
        if (static_cast<const CMappedResource*>(*static_cast<void**>(model))->FindEntry(armourPart) != -1) {
            AddPart(unit, nullptr, armourPart, model, FiguresFlag(unit));
            if (!SameName(plain, kHead)) static_cast<CObject3D*>(unit)->RemovePart(static_cast<CFigureMesh*>(current));
        } else if (current) {
            if (SameName(plain, kHair)) {
                static_cast<CObject3D*>(unit)->RemovePart(static_cast<CFigureMesh*>(current));
            } else if (gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(static_cast<const char*>(part)),
                                       reinterpret_cast<const unsigned char*>(current->GetName())) != 0) {
                AddPart(unit, nullptr, part, model, FiguresFlag(unit));
                static_cast<CObject3D*>(unit)->RemovePart(static_cast<CFigureMesh*>(current));
            }
        } else {
            AddPart(unit, nullptr, part, model, FiguresFlag(unit));
        }
    }
    return true;
}
EI_REPLACE_METHOD(0x005381B0, CUnitClientSpecific::WearArmourModel);

namespace {
// The figure parts 00516B50 collects: an array in 32-byte aligned storage (the block kept at
// data[-1]), as SkinRequest's layers.
using PartArray = lib::AlignedArray<CEngineObject*>;

int CompareNames(const char* a, const char* b) {
    return gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(a), reinterpret_cast<const unsigned char*>(b));
}
} // namespace

bool CUnitClientSpecific::RemoveArmourModel(std::uint32_t slot) {
    if (slot >= 7) return false;
    std::uint32_t itemId = armourIds[slot];
    if (itemId == kNone) return false;
    if ((itemId >> 16) == 0xFFFF || (itemId & 0xFFFF) == 0xFFFF) return false;
    armourIds[slot] = kNone;
    int row = RowOfSlot(static_cast<int>(slot));
    if (row < 0) return true;

    void* model;
    {
        stlp::string name(unit->name);
        model = static_cast<CFigureManager*>(FigureManager())->FindModel(&name);
    }
    if (!model) return false;   // the slot stays cleared
    for (int i = 0; i < 7; ++i) {
        const char* plain = ArmourPartNames()[row][i];
        if (!plain[0]) break;
        mfc::CString part(plain);
        if (CompareNames(part, kHair) == 0) {
            const char* prototype = unitdb::Prototypes().RecordAt(static_cast<std::uint32_t>(unit->prototypeId_),
                                                             unitdb::kPrototypeSize);
            int hair = Prototype(prototype)->hair;
            if (hair == -1) continue;
            part.Format("hr.%.2d", hair);
        }
        PartArray parts{nullptr, 0, 0, 0};
        unit->CollectPartsByPrefix(part, reinterpret_cast<lib::AlignedArray<void*>*>(&parts));   // 00516B50
        if (parts.size == 1) {
            // One part of that name: replaced only when it is not the plain part already.
            if (CompareNames(part, parts.data[0]->GetName()) != 0) {
                static_cast<CObject3D*>(unit)->RemovePart(static_cast<CFigureMesh*>(parts.data[0]));
                AddPart(unit, nullptr, part, model, FiguresFlag(unit));
            }
        } else {
            for (int k = 0; k < parts.size; ++k) static_cast<CObject3D*>(unit)->RemovePart(static_cast<CFigureMesh*>(parts.data[k]));
            AddPart(unit, nullptr, part, model, FiguresFlag(unit));
        }
        if (parts.data) gamecrt::operator_delete(reinterpret_cast<void**>(parts.data)[-1]);
    }
    return true;
}
EI_REPLACE_METHOD(0x005385A0, CUnitClientSpecific::RemoveArmourModel);

namespace {

constexpr int kWeaponBow = 5;
constexpr std::uint32_t kArrowPartFlag = 0x80;  // set in the arrow part's flags_
const char* const kBowArrow = "rh3.arrow00";

std::uint32_t WeaponIdOf(const CUnitClientSpecific* self, std::uint32_t slot) {
    return slot < 4 ? self->weaponIds[slot] : kNone;
}
} // namespace

int CUnitClientSpecific::ChangeWeaponModel(std::uint32_t slot) {
    std::uint32_t previous = WeaponIdOf(this, unit->activeWeapon);
    std::uint32_t next = WeaponIdOf(this, slot);
    console::CStringArray names;
    void* model;
    {
        stlp::string name(unit->name);
        model = static_cast<CFigureManager*>(FigureManager())->FindModel(&name);
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
            CEngineObject* part = AddPart(unit, nullptr, names[i], model, FiguresFlag(unit));
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
    // The controller's slot 30 (RefreshModel), after the change.
    static_cast<CUnitController*>(static_cast<CEngineObject*>(this))->RefreshModel();
    return 1;
}
EI_REPLACE_METHOD(0x00535590, CUnitClientSpecific::ChangeWeaponModel);

// --- The weapon in hand and the bow's arrow during animations (A-U) ------------------------------

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
EI_REPLACE_METHOD(0x00537CB0, CUnitClientSpecific::ShowWeaponParts);

namespace {
// The bow's arrow part of a hero holding a bow, or null.
CEngineObject* BowArrow(CUnitClientSpecific* self) {
    std::uint32_t hand = WeaponIdOf(self, self->unit->activeWeapon);
    if (hand == kNone || ::WeaponType(hand) != kWeaponBow) return nullptr;
    return self->unit->FindPart(kBowArrow);
}

// The original's x87 tests: `frame` was passed between the last update and now (not when the
// last time was unordered; a NaN time counts as passed).
bool Passed(std::uint32_t frame, float last, float now) {
    double at = x87::FromUnsigned(frame);
    return at > last && !(at > now);
}
} // namespace

void CUnitClientSpecific::UpdateArrow() {
    if (unit->type_ != kUnitTypeHero) return;
    const CFigureAnimationState* state = unit->Object3D()->animation;
    const FigureAnimation* current = state->current;
    if (current && (current->flags & 1)) {
        float now = state->time;
        if (!(now < lastAnimationTime || now > lastAnimationTime)) return;   // the same time, or NaN
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
EI_REPLACE_METHOD(0x00538C20, CUnitClientSpecific::UpdateArrow);

void __fastcall CollectArmourPartNames(const char* prototype, const stlp::vector<std::uint32_t>* armourIds,
                                       const stlp::vector<std::uint32_t>* weaponIds,
                                       stlp::vector<stlp::string>* names, stlp::vector<stlp::string>* plainNames) {
    char text[0x401];
    for (const std::uint32_t* id = armourIds->begin(); id != armourIds->end(); ++id) {
        const char* armour = ArmourRecord(*id >> 16);
        if (!armour) continue;
        int row = RowOfSlot(ItemHead(armour)->type);
        if (row < 0) continue;
        for (int i = 0; i < 7; ++i) {
            const char* plain = ArmourPartNames()[row][i];
            if (!plain[0]) break;
            if (SameName(plain, kHair)) {
                int hair = Prototype(prototype)->hair;
                if (hair == -1) continue;
                gamecrt::sprintf(text, "hr.%.2d.armor%.2d", hair, ItemHead(armour)->number);
            } else {
                gamecrt::sprintf(text, "%s.armor%.2d", plain, ItemHead(armour)->number);
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
            const char* plain = ArmourPartNames()[row][i];
            if (SameName(plain, kHair)) {
                int hair = Prototype(prototype)->hair;
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
EI_REPLACE(0x00535CB0, CollectArmourPartNames);

// --- Skin -------------------------------------------------------------------------------------

void CUnitClientSpecific::BuildSkinLayers(int detailed, SkinLayerArray* out) {
    const RecordTable& armours = itemdb::Armors();
    if (!detailed) {
        SetLayerCount(out, 0);
        SetLayerCount(out, kSimpleLayers);
        std::uint32_t id = armourIds[0];
        if (id != kNone) {
            if (const char* armour = armours.RecordAt(id >> 16, itemdb::kArmorSize)) {
                out->data[0].id = id;
                out->data[0].kind = kLayerArmour;
                out->data[0].slot = ItemHead(armour)->type;
            }
        }
        std::uint32_t hand = unit->activeWeapon;
        if (hand < 4 && (id = weaponIds[hand]) != kNone) {
            if (const char* weapon = itemdb::Weapons().RecordAt(id >> 16, itemdb::kWeaponSize)) {
                out->data[1].id = id;
                out->data[1].kind = kLayerWeapon;
                out->data[1].slot = ItemHead(weapon)->type;
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
        const char* armour = armours.RecordAt(id >> 16, itemdb::kArmorSize);
        if (!armour) continue;
        out->data[k + 1].id = id;
        out->data[k + 1].kind = kLayerArmour;
        out->data[k + 1].slot = ItemHead(armour)->type;
    }
}
EI_REPLACE_METHOD(0x00536300, CUnitClientSpecific::BuildSkinLayers);

namespace {
// SkinRequest's layer bytes grown to `bytes` (the original inlines the aligned CArray SetSize).
void SetLayerBytes(SkinRequest& request, int bytes) {
    if (bytes == 0) {
        if (request.layers) {
            gamecrt::operator_delete(reinterpret_cast<void**>(request.layers)[-1]);
            request.layers = nullptr;
        }
        request.capacity = 0;
        request.layerBytes = 0;
        return;
    }
    auto allocate = [](int count) {
        auto* block = static_cast<char*>(gamecrt::operator_new(static_cast<std::size_t>(count) + 0x24));
        auto* data = reinterpret_cast<std::uint8_t*>((reinterpret_cast<std::uintptr_t>(block) + 0x23) & ~std::uintptr_t{0x1F});
        reinterpret_cast<void**>(data)[-1] = block;
        return data;
    };
    if (!request.layers) {
        request.layers = allocate(bytes);
        request.capacity = bytes;
        request.layerBytes = bytes;
        return;
    }
    if (bytes <= request.capacity) {
        request.layerBytes = bytes;
        return;
    }
    int grow = request.growBy;
    if (grow == 0) {
        grow = request.layerBytes / 8;
        if (grow < 4) grow = 4;
        else if (grow > 0x400) grow = 0x400;
    }
    int capacity = grow + request.capacity;
    if (bytes >= capacity) capacity = bytes;
    std::uint8_t* fresh = allocate(capacity);
    std::memcpy(fresh, request.layers, static_cast<std::size_t>(request.layerBytes));
    gamecrt::operator_delete(reinterpret_cast<void**>(request.layers)[-1]);
    request.layers = fresh;
    request.layerBytes = bytes;
    request.capacity = capacity;
}

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
    const char* prototype = unitdb::Prototypes().RecordAt(static_cast<std::uint32_t>(unit->prototypeId_), unitdb::kPrototypeSize);
    if (prototype)
        texture.Format("%sskin_%.2d.mmp", unit->name, Prototype(prototype)->skin);
    else
        texture.Format("%sskin_00.mmp", unit->name);
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
    void* textureManager = TextureManager();
    SkinLayerArray layers{reinterpret_cast<void*>(0x0073EEA0), nullptr, 0, 0, 0};
    void* result;
    {
        mfc::CString unitKey;
        mfc::CString prototypeKey;
        mfc::CString texture;   // the work string
        // The request lives in raw storage so that its string array is destroyed before its
        // layer bytes are freed and before the strings, as in the original.
        alignas(SkinRequest) unsigned char storage[sizeof(SkinRequest)];
        auto* request = new (storage) SkinRequest{nullptr, 0, 0, 0, {}, 0};
        BuildSkinLayers(detailed, &layers);
        int bytes = layers.size * static_cast<int>(sizeof(SkinLayer));
        SetLayerBytes(*request, bytes);
        std::memcpy(request->layers, layers.data, static_cast<std::size_t>(bytes));
        NameSkinTextures(this, detailed, *request, unitKey, prototypeKey, texture);
        result = reinterpret_cast<void*>(
            ComposeTexture(textureManager, nullptr, static_cast<mfc::CString*>(target), request, &unitKey, &prototypeKey, 3, 0, 0x5551));
        request->textures.~CStringArray();
        if (request->layers) gamecrt::operator_delete(reinterpret_cast<void**>(request->layers)[-1]);
    }
    if (layers.data) gamecrt::operator_delete(layers.data);
    return result;
}
EI_REPLACE_METHOD(0x00536670, CUnitClientSpecific::ComposeSkin);
