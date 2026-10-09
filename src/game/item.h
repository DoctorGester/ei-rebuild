#pragma once

// Items: the in-game objects created from the item databases (item_db.h). Class names come from
// the game's own GetClassName strings; field names are provisional.
//
// unknown_18 of CEngineObject is the item's packed id: prototype index << 16 | material index.

#include "engine_object.h"
#include "item_db.h"
#include "../lib/mfc.h"

class CSpell;
class CSpellManager;
class CStream;

// CEngineObject::type_ of an item: which database its prototype lives in.
enum ItemKind : int {
    kItemWeapon = 0x3004,
    kItemArmor = 0x3005,
    kItemQuick = 0x3006,
    kItemQuest = 0x3007,
    kItemSpell = 0x3008,   // a spell container; prototype in the quick item database
    kItemLoot = 0x3009,
};

inline std::uint32_t PrototypeIndex(std::uint32_t packedId) { return packedId >> 16; }
inline std::uint32_t MaterialIndex(std::uint32_t packedId) { return packedId & 0xFFFF; }

// Base of every item (vtable 00743DA4).
class CItem : public CEngineObject {
public:
    // Virtual slots 13-15 after CEngineObject's (vtable 00743DA4; the subclasses override 13 and
    // 15 and call CItem's first). Rewritten bodies are the plain members below (rule 4).
    virtual void InitFromRecordSlot(const char* record);    // 13: 006A0490 InitFromRecord
    virtual BOOL IsSameAsSlot(CItem* other);                 // 14: 0069FE90 IsSameAs
    virtual BOOL CopyToSlot(CItem* target, CSpellManager* spells);   // 15: 0069FFC0 CopyTo

    // 0069FCA0 (item_lifetime.cpp). Constructor body: CEngineObject's fields, no link (+0x1C) or
    // spell, +0x20..+0x3C -1.0, count 1 (+0x40 is left as it is). Returns this.
    CItem* Construct();
    static std::uintptr_t Vtable();   // 00743DA4
    // 0069FE20 (slot 2). Count 1, the spell released; returns 1.
    int CreateItem();
    // 0069FE50 (slot 3). The spell released (slot 4) and the link (+0x1C, a CRefCounted: 16-bit
    // count, deleted at 0) let go.
    void DestroyItem();
    // 0069FD70 (item_lifetime.cpp) and its byte-identical copies per class: 006A05B0 weapon,
    // 006A2FC0 armour, 006A3B30 quick, 006A4420 quest, 006A4A00 loot, 006A5740 scroll (00462420
    // calls 0069FD70 too). The destructor body: CItem's vtable back, DestroyItem's work, the two
    // members' destructors (on the fields it emptied), then CEngineObject's.
    void DestructBody();
    // 0069FD30 and the per-class copies 006A0590, 006A2FA0, 006A3B10, 006A4400, 006A49E0, 006A5720
    // (slot 0; RET 4): DestructBody, then operator delete when bit 0 of `flags` is set.
    CItem* DeletingDestruct(unsigned flags);

    // 006A0270 / 006A0360 (slots 9 / 10, RET 4; item_save.cpp). Read / write the packed id
    // (+0x18), +0x20..+0x3C, the spell's id (-1: none; Load finds it in the spell manager of the
    // manager context, slot 22, and counts it), +0x48 and +0x40, four bytes each, in that order.
    void LoadItem(CStream* stream);
    void SaveItem(CStream* stream);

    // 0069FF70 (RET 4; party_items.cpp). A new item of the same kind from the owning manager
    // (slot 23), given this one's values through slot 15 with the spell manager `spells`; when
    // that fails the new item is released and the result is null.
    CItem* Clone(CSpellManager* spells);
    // 0069FFC0 (RET 8; slot 15's base body; party_items.cpp). Gives `target`, when it is of the
    // same kind, this item's packed id and values +0x20..+0x40 (after its Destroy, slot 3), a copy
    // of the attached spell (CSpell::Clone with `spells`; or none) and a count (+0x48) of 1.
    // Returns 0 for another kind, else 1.
    BOOL CopyTo(CItem* target, CSpellManager* spells);
    BOOL CallCopyTo(CItem* target, CSpellManager* spells);   // through 0069FFC0 (its verify wrapper)

    // 006A0110. The prototype record for the item's kind and index; null for spell containers,
    // other kinds and indices out of range.
    char* GetPrototype();

    // 006A4230. The prototype's category (+0x6C); INT_MAX if there is no prototype.
    int GetCategory();

    // 006A0070. Virtual slot 6, called every tick: moves charge_ towards chargeMax_ by
    // chargeRate_ * chargeMax_. Returns 0 when already full.
    BOOL Recharge();

    // 0069FE90 (item_slots.cpp). Virtual slot 14's base body: 1 for the item itself; else the
    // same kind, packed id, values +0x40 and (equal or unordered floats) +0x20..+0x2C, +0x34..+0x3C,
    // and both without a spell or both with one of the same packed id and modifiers.
    BOOL IsSameAs(CItem* other);

    // 006A0490. Virtual slot 13 of CItem (the subclasses call it first): takes the item's values
    // from a database record (see CItemConstructor::Build): +0x34 price, +0x38, +0x3C, +0x40
    // charge (current and maximum), +0x44, +0x48 (+0x20 and +0x24); charge rate 0.001.
    void InitFromRecord(const char* record);

    // 006A04D0. Attaches `spell` (counted; the previous one released), moving the price from the
    // previous spell's to the new one's.
    void AttachSpell(CSpell* spell);

    // 006A0520. Attaches `spell` (counted; the previous one released); the price is left alone.
    void SetSpell(CSpell* spell);
    // The condition back to its maximum (+0x20 = +0x24; the camp's repairs).
    void Repair() { unknown_20 = unknown_24; }

    std::uint32_t unknown_1C;
    std::uint32_t unknown_20;     // +0x20 both set from prototype +0x48
    std::uint32_t unknown_24;
    float charge_;                // +0x28
    float chargeMax_;             // +0x2C prototype +0x40
    float chargeRate_;            // +0x30 0.001 by default
    std::uint32_t unknown_34;
    float price_;                 // +0x38 includes the attached spell's price
    std::uint32_t unknown_3C;
    std::uint32_t unknown_40;
    void* spell_;                 // +0x44 CSpell carried by the item (scrolls, wands)
    std::uint32_t unknown_48;
};
// The counted record an item's +0x1C (CItem::unknown_1C, a RefPtr slot) points at when the world
// server builds a unit's items from its party record (0058EF20): a reference value (constructor
// 00419720 then vtable 0073FA88, or 0058FBB0) holding the id of the record's item.
struct ItemSourceRef {
    void* vtable;                 // +0x00
    std::uint32_t refs;           // +0x04 CRefCounted's count and flags (written as one dword)
    std::uint32_t itemId;         // +0x08 the source item's id_ (its +0x0C)
};
static_assert(sizeof(ItemSourceRef) == 0xC);
static_assert(offsetof(CItem, charge_) == 0x28);
static_assert(offsetof(CItem, spell_) == 0x44);
static_assert(sizeof(CItem) == 0x4C);

// The record an item's link (CItem::unknown_1C) points at as the world server makes it (0xC
// bytes, vtable 0073FA88): a counted value with a 16-bit count and the dword the world's save keeps
// per item (world_save.cpp, world_units.cpp, world_quests.cpp).
struct CItemLink {
    static void* Vtable() { return reinterpret_cast<void*>(0x0073FA88); }

    void* vtable;
    std::int16_t references;        // +0x04
    std::uint16_t unknown_06;
    std::uint32_t value;            // +0x08
};
static_assert(sizeof(CItemLink) == 0xC);

// vtable 00743DFC, GetClassId 0x11.
class CItemWeapon : public CItem {
public:
    // 006A07F0. The weapon type from the prototype (faults if the index is out of range).
    int GetType();

    // 006A0770. Copies the prototype's seven damage-type weights to `out` (zeros if none).
    void GetDamageTypes(float* out);

    // 006A2DE0. Damage roll: damage_ + random fraction of damageSpread_ (uses the global ISAAC
    // generator). Returned unrounded in ST0 like the original (callers round when they store it);
    // declared double so the value is not rounded to float on return when the FPU runs at more
    // than 24-bit precision (it does under Rebirth).
    double RollDamage();

    // 006A06C0. Virtual slot 14: the same item as `other` (base comparison plus the weapon fields).
    BOOL IsSameAs(CItemWeapon* other);

    // Names built from the prototype (item_names.cpp; virtual slots 17-20). Slot 17: the model
    // name "<type>_<number>.<material name>.<variant>" (type "sw", "ax", ...; empty without a
    // prototype or material). Slot 18: the first string of the prototype's string list (+0x20),
    // else "". Slot 19: "unhuma" + slot 17. Slot 20: the picture name "initwe<type><number>weapon".
    mfc::CString ModelName();       // 006A0840
    mfc::CString FirstListName();   // 006A0F70
    mfc::CString FigureName();      // 006A2750
    mfc::CString PictureName();     // 006A27E0

    // 006A0730 (RET 8; slot 15; party_items.cpp). CItem::CopyTo, then +0x4C..+0x58.
    BOOL CopyTo(CItem* target, CSpellManager* spells);
    // 006A0540 (item_lifetime.cpp). CItem's, then +0x4C 0 and the three floats -1.0.
    CItemWeapon* Construct();
    // 006A0660 (slot 13; item_slots.cpp). CItem::InitFromRecord, then damage, spread and +0x58.
    void InitFromRecord(const char* record);
    // 006A2E60 / 006A2EB0 (slots 9 / 10; item_save.cpp). CItem's, then +0x4C..+0x58.
    void LoadItem(CStream* stream);
    void SaveItem(CStream* stream);

    std::uint32_t unknown_4C;
    float damage_;          // +0x50 prototype +0x90
    float damageSpread_;    // +0x54 prototype +0x94
    float unknown_58;       // +0x58 prototype +0x8C
};
static_assert(offsetof(CItemWeapon, damage_) == 0x50);

// vtable 00743E54, GetClassId 0x12.
class CItemArmor : public CItem {
public:
    // 006A39E0. The armor type from the prototype (faults if the index is out of range).
    int GetType();

    // 006A31A0. 2 for armor types 0, 1 and 2, 1 for every other type.
    int GetTypeGroup();

    // The same four names as CItemWeapon's (item_names.cpp), with the armor types ("hl", "pl",
    // ...) and the picture name "initar<type><number>armor".
    mfc::CString ModelName();       // 006A3240
    mfc::CString FirstListName();   // 006A36A0
    mfc::CString FigureName();      // 006A3720
    mfc::CString PictureName();     // 006A37B0

    // 006A3140 (RET 8; slot 15; party_items.cpp). CItem::CopyTo, then absorb_ and layerMask_.
    BOOL CopyTo(CItem* target, CSpellManager* spells);
    // 006A2F20 (item_lifetime.cpp). CItem's, then absorb_ and layerMask_ 0.
    CItemArmor* Construct();
    // 006A3070 (slot 13; item_slots.cpp). CItem::InitFromRecord, then the two absorption rows.
    void InitFromRecord(const char* record);
    // 006A30D0 (slot 14). CItem::IsSameAs, then absorb_ and layerMask_ equal as dwords.
    BOOL IsSameAs(CItemArmor* other);
    // 006A3A70 / 006A3AA0 (slots 9 / 10; item_save.cpp). CItem's, then the two absorption rows
    // (0x1C bytes each; layerMask_ is not saved).
    void LoadItem(CStream* stream);
    void SaveItem(CStream* stream);

    // What the piece adds to the body slots it covers (UnitStats::AddArmourLayer): [0] for the
    // first covered slot, [1] for the others.
    float absorb_[2][7];            // +0x4C per damage type
    std::uint32_t layerMask_[2];    // +0x84
};
static_assert(offsetof(CItemArmor, absorb_) == 0x4C);
static_assert(offsetof(CItemArmor, layerMask_) == 0x84);

// vtable 00743EAC, GetClassId 0x13 (potions, wands).
class CItemQuick : public CItem {
public:
    // 006A3CD0. Slot 19: "qitem<number>.<material name>" when the prototype's +0x10 name is not
    // "none" ("uq" for a material out of range), else "qitem<number>" (-1 without a prototype).
    mfc::CString FigureName();
    // 006A4040. Slot 20: "initqi<number>item".
    mfc::CString PictureName();
    // 006A3C50 (RET 8; slot 15; party_items.cpp). CItem::CopyTo, then +0x4C.
    BOOL CopyTo(CItem* target, CSpellManager* spells);
    // 006A3AD0 (item_lifetime.cpp). CItem's and the vtable (+0x4C is left as it is).
    CItemQuick* Construct();
    // 006A3BE0 / 006A3C20 (slots 13 / 14, also the scrolls'; item_slots.cpp). The base's, then
    // +0x4C (prototype +0x70).
    void InitFromRecord(const char* record);
    BOOL IsSameAs(CItemQuick* other);
    // 006A3C80 / 006A3CA0 (slots 9 / 10, also the scrolls'; item_save.cpp). CItem's, then +0x4C.
    void LoadItem(CStream* stream);
    void SaveItem(CStream* stream);

    std::uint32_t unknown_4C;
};
static_assert(sizeof(CItemQuick) == 0x50);

// vtable 00743F04, GetClassId 0x14.
class CItemQuest : public CItem {
public:
    mfc::CString FigureName();      // 006A45C0 "quitem<number>"
    mfc::CString PictureName();     // 006A47B0 "initqu<number>item"
    // 006A4540 (RET 8; slot 15; party_items.cpp). CItem::CopyTo, then +0x4C.
    BOOL CopyTo(CItem* target, CSpellManager* spells);
    // 006A43A0 (item_lifetime.cpp). CItem's and the vtable (+0x4C is left as it is).
    CItemQuest* Construct();
    // 006A44D0 / 006A4510 (slots 13 / 14; item_slots.cpp). The base's, then +0x4C (prototype
    // +0x6C, the category).
    void InitFromRecord(const char* record);
    BOOL IsSameAs(CItemQuest* other);
    // 006A4570 / 006A4590 (slots 9 / 10; item_save.cpp). CItem's, then +0x4C.
    void LoadItem(CStream* stream);
    void SaveItem(CStream* stream);

    std::uint32_t unknown_4C;
};

// vtable 00743FB4, GetClassId 0x16: a scroll (spell container).
class CItemSpell : public CItem {
public:
    // 006A5820. Slot 19: "spell<+0x60 of the carried spell's prototype>" (no null checks).
    mfc::CString FigureName();
    // 006A57F0 (RET 8; slot 15; party_items.cpp). CItem::CopyTo, then +0x4C.
    BOOL CopyTo(CItem* target, CSpellManager* spells);
    // 006A56E0 (item_lifetime.cpp). CItem's and the vtable (+0x4C is left as it is).
    CItemSpell* Construct();

    std::uint32_t unknown_4C;
};

// vtable 00743F5C, GetClassId 0x15.
class CItemLoot : public CItem {
public:
    // 006A5470. Virtual slot 16: the base price. Depends on what the loot stands for (kind_):
    // itself (price_), a material, or a record of another database (recordType_).
    float GetPrice();

    // 006A4B90. Slot 19, by kind_: a record's name ("<type>_<number>.<variant>" for weapons and
    // armors, "qitem", "prototype" (spells, +0x60), "modifier" + number), "material<index>", or
    // "litem<number>" for the loot itself.
    mfc::CString FigureName();
    // 006A5030. Slot 20, the picture: "initwe..." / "initar..." / "initqi..." for records,
    // "initqi1item" for spells and modifiers, "initlimt<index>item" for materials,
    // "initlitr<number>item" for the loot itself.
    mfc::CString PictureName();
    // 006A4B50 (RET 8; slot 15; party_items.cpp). CItem::CopyTo, then kind_, recordType_, index_.
    BOOL CopyTo(CItem* target, CSpellManager* spells);
    // 006A49A0 (item_lifetime.cpp). CItem's and the vtable (kind, record type and index are left
    // as they are).
    CItemLoot* Construct();
    // 006A4AB0 / 006A4B00 (slots 13 / 14; item_slots.cpp). The base's, then kind_, recordType_
    // and index_ (prototype +0x6C..+0x74).
    void InitFromRecord(const char* record);
    BOOL IsSameAs(CItemLoot* other);
    // 006A5630 / 006A5680 (slots 9 / 10; item_save.cpp). CItem's, then kind_, recordType_ and
    // index_. Load reads the first two through the stack slot of `stream` (a read that writes
    // nothing leaves the pointer's value, then the previous value).
    void LoadItem(CStream* stream);
    void SaveItem(CStream* stream);

    std::uint32_t kind_;       // +0x4C 0 own price, 1 material, 2 record of recordType_
    std::uint32_t recordType_;  // +0x50 0 weapon, 1 armor, 2 quick item, 3 spell, 4 spell modifier
    std::uint32_t index_;       // +0x54 record index
};
static_assert(offsetof(CItemLoot, index_) == 0x54);

// Weapon stats from a packed id (__fastcall, id in ECX). Each faults if the prototype index is out
// of range; an out-of-range material reads the multiplier from address 0 likewise.
int __fastcall WeaponType(std::uint32_t packedId);          // 006A2B80
float __fastcall WeaponMinDamage(std::uint32_t packedId);   // 006A2A50
float __fastcall WeaponMaxDamage(std::uint32_t packedId);   // 006A2AE0
float __fastcall WeaponAttack(std::uint32_t packedId);      // 006A2BD0
int __fastcall WeaponDamageType(std::uint32_t packedId);    // 006A2C60 first non-zero weight, else 6
float __fastcall WeaponStat84(std::uint32_t packedId);      // 006A2D40
float __fastcall WeaponStat88(std::uint32_t packedId);      // 006A2D90

// 006A0CC0 (item_names.cpp). The base figure (skeleton) a weapon prototype (by index) is animated
// with, into `out` (emptied first): "basesword", "baseaxe", "basedagger", "basepike", "baseclub"
// with the prototype's number as "%.2d" appended, "basearrow00" / "basearrow01" for types 5 / 6,
// else "baser_hand3". False (only AL set) for index -1 or out of range.
bool __fastcall WeaponBaseFigure(std::uint32_t index, mfc::CString* out);

// 006A3480 (RET 4; item_model_names.cpp). The armour model name "<type>_<number>.<material
// name>.<variant>" of armour prototype `armorIndex` in material `materialIndex` into `out` (emptied
// first; left empty when either is out of range). The type code is copied into the stack slot of
// `materialIndex`, so for a type without a code the index's own bytes are the code.
void __fastcall ArmorModelName(mfc::CString* out, std::uint32_t armorIndex, std::uint32_t materialIndex);

// 006A3200. Armor prototype field +0xB8 by prototype index; 0 if out of range.
int __fastcall ArmorFieldB8(std::uint32_t index);

// --- Item construction (item_constructor.cpp) -------------------------------------------------

// The item constructor (vtable 00744020, 0x34 bytes): the manager creates an item object
// (BeginCreateItem), the caller names the prototype and material records and optionally a spell,
// and EndCreateItem fills the item from a record combining them (Build).
class CItemConstructor : public CEngineObject {
public:
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x00744020);

    // 006A5AB0 (party_lifetime.cpp). Constructor body: CEngineObject's fields, no manager,
    // records, item or spell (count_ is left as it is), the vtable. Returns this.
    CItemConstructor* Construct();

    // 006A5B90. Virtual slot 2: forgets the manager (its count dropped), releases the item and
    // clears the records and the spell. Returns 1.
    BOOL Reset();

    // 006A5BD0. Virtual slot 3: the same, no result.
    void Detach();

    // 006A5B20. The destructor body (vtable 00744020): Detach's work, then the base destructor.
    void DestructBody();

    // 006A5C00. Counts a reference to `manager` (+4), dropping the previous one's (no release).
    void SetManager(CEngineObject* manager);

    // 006A5C20. Adopts an existing item (counted; the previous one released); records cleared.
    void SetItem(CItem* item);

    // 006A5C50. Creates the item through the manager (slot 23 with `kind`, `key`): counted,
    // records cleared, count 1, flag 8 set. Returns 1, or a fatal error's result.
    BOOL BeginCreateItem(int kind, int key);

    BOOL SetMaterial(char* material);       // 006A5CC0. Returns 1.
    BOOL SetPrototype(char* prototype);     // 006A5CD0. Returns 1.
    BOOL SetSpellObject(CSpell* spell); // 006A5CE0. Returns 1 (not counted).

    // 006A5CF0. Builds the item. On failure releases it and returns null; otherwise hands it over
    // (its count dropped without release, flag 8 cleared) and returns it.
    CItem* EndCreateItem();

    // 006A5D30. Releases the item; records and spell cleared. Returns 1.
    BOOL Cancel();

    // 006A5D60. Fills the item from a temporary database record: the prototype's values combined
    // with the material's, then the item's slot 13 (InitFromRecord); attaches the prototype's
    // spell (wands, category 8) or the given one; sets the packed id and the count. Returns 0
    // for a weapon or armor without a material, else 1.
    BOOL Build();

    CEngineObject* manager_;  // +0x1C counted (+4) but never released
    char* material_;          // +0x20 material record or null
    char* prototype_;         // +0x24 prototype record
    CItem* item_;             // +0x28 counted
    CSpell* spell_;     // +0x2C
    int count_;               // +0x30 copied to the item's +0x48
};
static_assert(sizeof(CItemConstructor) == 0x34);

// The item manager (007AFFB4; a CResourceManager with vtable 0073F25C, 0xBC bytes, as the world
// and every party make one). Only the functions rewritten here are declared; +0xAC is its
// manager context (slot 16: the item constructor, slot 22: the spell manager).
struct CItemManager {
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x0073F25C);
    static constexpr std::size_t kSize = 0xBC;

    // 006AC970 (RET 4). An item from its name, "prototype[.material][[extra]]" (lowercased):
    // "spell container[spell]" makes a scroll; otherwise the prototype is looked up in the weapon,
    // armor, quick, 007C8758 and 007C8778 tables (none: FatalError, null) and built with the
    // material, the bracket part as a spell (or, for prototypes of type "material", as the count);
    // a class-0x15 item of kind 2 gets the table index of the second name part at +0x54. A failed
    // build reports FatalError and the original crashes; Rebirth's FixCreateItem returns null in
    // both cases without FatalError.
    CItem* CreateItem(const char* name);
};

// --- Weapon model parts (item_parts.cpp)------------------------------------------------------

namespace console {
class CStringArray;
} // namespace console
namespace stlp {
class string;
template <typename T> class vector;
} // namespace stlp

// 006A0FF0. The names of the figure parts a weapon prototype (by index) is drawn with, replacing
// the contents of `out`: "rh3.sword07" for a sword with number 7, the bow and crossbow pieces plus
// the quiver and arrows. Nothing for an index out of range or an unknown weapon type.
void __fastcall WeaponModelParts(std::uint32_t index, console::CStringArray* out);

// 006A1780. The same names for a packed item id, appended to an STLport vector of strings.
void __fastcall AppendWeaponModelParts(std::uint32_t packedId, stlp::vector<stlp::string>* out);
