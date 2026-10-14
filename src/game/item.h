#pragma once

// Items: the in-game objects created from the item databases (item_db.h). Class names come from
// the game's own GetClassName strings; field names are provisional.
//
// unknown_18 of CEngineObject is the item's packed id: prototype index << 16 | material index.

#include "engine_object.h"
#include "item_db.h"
#include "resource_manager.h"
#include "../lib/mfc.h"

#include <cstring>

class CSpell;
class CSpellConstructor;
class CSpellManager;
class CStream;

// CEngineObject::type_ of an item: which database its prototype lives in.
enum ItemKind : int {
    kItemWeapon = 0x3004,
    kItemArmor = 0x3005,
    kItemQuick = 0x3006,
    kItemLoot = 0x3007,
    kItemSpell = 0x3008,   // a spell container; prototype in the quick item database
    kItemQuest = 0x3009,
};

inline std::uint32_t PrototypeIndex(std::uint32_t packedId) { return packedId >> 16; }
inline std::uint32_t MaterialIndex(std::uint32_t packedId) { return packedId & 0xFFFF; }

// What an item's slot 21 fills in (three dwords; no caller is rewritten yet).
struct ItemUse {
    std::uint32_t kind;
    std::uint32_t value;
    std::uint32_t extra;
};

// Base of every item (vtable 00743DA4, 22 slots).
class CItem : public CEngineObject {
public:
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x00743DA4);

    // The virtual slots in vtable order (the subclasses override them and call CItem's first;
    // slots 11 and 12 are _purecall here). The rewritten bodies are registered through hook shims
    // (rule 4).

    // 0069FE20 (slot 2; item_lifetime.cpp). Count 1, the spell released; returns 1.
    int Create() override;
    // 0069FE50 (slot 3; item_lifetime.cpp). The spell released (slot 4) and the link (+0x1C, a
    // CRefCounted: 16-bit count, deleted at 0) let go.
    void Destroy() override;
    // 006A0070 (slot 6, called every tick). The recharge: moves charge_ towards chargeMax_ by
    // chargeRate_ * chargeMax_. Returns 0 when already full.
    int RunTick() override;
    // 006A0270 / 006A0360 (slots 9 / 10, RET 4; item_save.cpp). Read / write (a CStream) the
    // packed id (+0x18), +0x20..+0x3C, the spell's id (-1: none; Load finds it in the spell manager
    // of the manager context, slot 22, and counts it), +0x48 and +0x40, four bytes each, in that
    // order.
    void Load(void* stream) override;
    void Save(void* stream) override;
    // 006A0490. Slot 13 (the subclasses call it first): takes the item's values from a database
    // record (see CItemConstructor::Build): +0x34 price, +0x38, +0x3C, +0x40 charge (current and
    // maximum), +0x44, +0x48 (+0x20 and +0x24); charge rate 0.001.
    virtual void InitFromRecord(const itemdb::ItemRecordHead* record);
    // 0069FE90 (item_slots.cpp). Slot 14's base body: 1 for the item itself; else the same kind,
    // packed id, values +0x40 and (equal or unordered floats) +0x20..+0x2C, +0x34..+0x3C, and both
    // without a spell or both with one of the same packed id and modifiers.
    virtual BOOL IsSameAs(CItem* other);
    // 0069FFC0 (RET 8; slot 15's base body; party_items.cpp). Gives `target`, when it is of the
    // same kind, this item's packed id and values +0x20..+0x40 (after its Destroy, slot 3), a copy
    // of the attached spell (CSpell::Clone with `spells`; or none) and a count (+0x48) of 1.
    // Returns 0 for another kind, else 1.
    virtual BOOL CopyTo(CItem* target, CSpellManager* spells);
    // 004622F0 (four bytes, not hooked). Slot 16: the base price (CItemLoot's depends on what it
    // stands for).
    virtual float GetPrice();
    // 00462300 / 00462330 / 00462360 / 00462390 (class_slots.cpp: ItemNoName). Slots 17-20: the
    // model, first list, figure and picture names (item_names.cpp); "" for a plain item. Each is
    // built in the caller's result.
    virtual mfc::CString ModelName();
    virtual mfc::CString FirstListName();
    virtual mfc::CString FigureName();
    virtual mfc::CString PictureName();
    // 004623C0 (class_slots.cpp: ItemNoUse). Slot 21: fills `out` (zeros; the quest item's has its
    // id) and returns it.
    virtual ItemUse* GetUse(ItemUse* out);
    // The first name of slot 14, still used by ui_screens.cpp (another ticket's file).
    BOOL IsSameAsSlot(CItem* other) { return IsSameAs(other); }

    // 0069FCA0 (item_lifetime.cpp). Constructor body: CEngineObject's fields, no link (+0x1C) or
    // spell, +0x20..+0x3C -1.0, count 1 (+0x40 is left as it is). Returns this.
    CItem* Construct();
    // 0069FD70 (item_lifetime.cpp) and its byte-identical copies per class: 006A05B0 weapon,
    // 006A2FC0 armour, 006A3B30 quick, 006A4420 quest, 006A4A00 loot, 006A5740 scroll (00462420
    // calls 0069FD70 too). The destructor body: CItem's vtable back, Destroy's work, the two
    // members' destructors (on the fields it emptied), then CEngineObject's.
    void DestructBody();
    // 0069FD30 and the per-class copies 006A0590, 006A2FA0, 006A3B10, 006A4400, 006A49E0, 006A5720
    // (slot 0; RET 4): DestructBody, then operator delete when bit 0 of `flags` is set.
    CItem* DeletingDestruct(unsigned flags);

    // 0069FF70 (RET 4; party_items.cpp). A new item of the same kind from the owning manager
    // (slot 23), given this one's values through slot 15 with the spell manager `spells`; when
    // that fails the new item is released and the result is null.
    CItem* Clone(CSpellManager* spells);
    BOOL CallCopyTo(CItem* target, CSpellManager* spells);   // through 0069FFC0 (its verify wrapper)

    // 006A0110. The prototype record for the item's kind and index; null for spell containers,
    // other kinds and indices out of range.
    char* GetPrototype();

    // 006A4230. The prototype's category (+0x6C); INT_MAX if there is no prototype.
    int GetCategory();

    // 006A04D0. Attaches `spell` (counted; the previous one released), moving the price from the
    // previous spell's to the new one's.
    void AttachSpell(CSpell* spell);

    // 006A0520. Attaches `spell` (counted; the previous one released); the price is left alone.
    void SetSpell(CSpell* spell);
    // The condition back to its maximum (the camp's repairs).
    void Repair() { condition_ = conditionMax_; }
    // The item's weight (PartyUnitRecord::CarriedWeight adds them up).
    float Weight() const {
        float weight;
        std::memcpy(&weight, &weight_, sizeof(weight));
        return weight;
    }

    // +0x1C a counted link record (a RefPtr slot): the world server's CItemLink (the id the
    // world's save keeps per item), the party record's ItemSourceRef, the HUD's ItemTurn.
    std::uint32_t link_;
    // +0x20 / +0x24 the condition and its maximum (float bits; both from the prototype's
    // condition): at 0 or below (rounded) the item is broken.
    std::uint32_t condition_;
    std::uint32_t conditionMax_;
    float charge_;                // +0x28
    float chargeMax_;             // +0x2C prototype +0x40
    float chargeRate_;            // +0x30 0.001 by default
    std::uint32_t weight_;        // +0x34 float bits (Weight()); prototype value38
    float price_;                 // +0x38 includes the attached spell's price
    std::uint32_t value3C_;     // +0x3C prototype value3C
    // +0x40 the level of spell the item can hold (the prototype's magicLevel plus the material's;
    // CItemConstructor::MagicCapacity).
    std::uint32_t magicLevel_;
    CSpell* spell_;               // +0x44 the spell carried by the item (scrolls, wands)
    std::uint32_t count_;         // +0x48 the stack's count
};
// The counted record an item's link_ (a RefPtr slot) points at when the world
// server builds a unit's items from its party record (0058EF20): a reference value (constructor
// 00419720 then vtable 0073FA88, or 0058FBB0) holding the id of the record's item.
struct ItemSourceRef {
    void* vtable;                 // +0x00
    std::uint32_t refs;           // +0x04 CRefCounted's count and flags (written as one dword)
    std::uint32_t itemId;         // +0x08 the source item's id_ (its +0x0C)
};
static_assert(sizeof(ItemSourceRef) == 0xC);
static_assert(offsetof(CItem, link_) == 0x1C && offsetof(CItem, condition_) == 0x20);
static_assert(offsetof(CItem, charge_) == 0x28 && offsetof(CItem, weight_) == 0x34);
static_assert(offsetof(CItem, magicLevel_) == 0x40 && offsetof(CItem, count_) == 0x48);
static_assert(offsetof(CItem, spell_) == 0x44);
static_assert(sizeof(CItem) == 0x4C);

// The record an item's link (CItem::link_) points at as the world server makes it (0xC
// bytes, vtable 0073FA88): a counted value with a 16-bit count and the dword the world's save keeps
// per item (world_save.cpp, world_units.cpp, world_quests.cpp).
struct CItemLink {
    static void* Vtable() { return reinterpret_cast<void*>(0x0073FA88); }

    // item_link.cpp (A-W14). 0058FBB0: the constructor's last step, the vtable stored (the counted
    // value's 00419720 ran before); returns this. The world server's CreateUnits and
    // SyncPartyRecords inline it.
    CItemLink* Construct();
    // 0058FB90 (slot 2). Built again in place: the root vtable, then its own (the value stays).
    void Reset();
    // 0058FB60 (slot 3). A new link with this one's value and no references; null without memory.
    CItemLink* Clone();

    void* vtable;
    std::int16_t references;        // +0x04
    std::uint16_t refsHigh;             // +0x06 the reference dword's high half (zeroed with it)
    std::uint32_t value;            // +0x08
};
static_assert(sizeof(CItemLink) == 0xC);

// vtable 00743DFC, GetClassId 0x11.
class CItemWeapon : public CItem {
public:
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x00743DFC);

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
    BOOL IsSameAs(CItem* other) override;

    // Names built from the prototype (item_names.cpp; virtual slots 17-20). Slot 17: the model
    // name "<type>_<number>.<material name>.<variant>" (type "sw", "ax", ...; empty without a
    // prototype or material). Slot 18: the first string of the prototype's string list (+0x20),
    // else "". Slot 19: "unhuma" + slot 17. Slot 20: the picture name "initwe<type><number>weapon".
    mfc::CString ModelName() override;       // 006A0840
    mfc::CString FirstListName() override;   // 006A0F70
    mfc::CString FigureName() override;      // 006A2750
    mfc::CString PictureName() override;     // 006A27E0

    // 006A0730 (RET 8; slot 15; party_items.cpp). CItem::CopyTo, then +0x4C..+0x58.
    BOOL CopyTo(CItem* target, CSpellManager* spells) override;
    // 006A0540 (item_lifetime.cpp). CItem's, then +0x4C 0 and the three floats -1.0.
    CItemWeapon* Construct();
    // 006A0660 (slot 13; item_slots.cpp). CItem::InitFromRecord, then damage, spread and +0x58.
    void InitFromRecord(const itemdb::ItemRecordHead* record) override;
    // 006A2E60 / 006A2EB0 (slots 9 / 10; item_save.cpp). CItem's, then +0x4C..+0x58.
    void Load(void* stream) override;
    void Save(void* stream) override;

    std::uint32_t actionValue_;         // +0x4C 0 after construction; CUnitServer::ScaledItemValue scales it by 15 / the rate, the hit takes it (DamageInfo +0x1C)
    float damage_;          // +0x50 prototype +0x90
    float damageSpread_;    // +0x54 prototype +0x94
    float reach_;           // +0x58 prototype reach: the wielder's reach
};
static_assert(offsetof(CItemWeapon, damage_) == 0x50 && offsetof(CItemWeapon, reach_) == 0x58);

// vtable 00743E54, GetClassId 0x12.
class CItemArmor : public CItem {
public:
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x00743E54);

    // 006A39E0. The armor type from the prototype (faults if the index is out of range).
    int GetType();

    // 006A31A0. 2 for armor types 0, 1 and 2, 1 for every other type.
    int GetTypeGroup();

    // The same four names as CItemWeapon's (item_names.cpp), with the armor types ("hl", "pl",
    // ...) and the picture name "initar<type><number>armor".
    mfc::CString ModelName() override;       // 006A3240
    mfc::CString FirstListName() override;   // 006A36A0
    mfc::CString FigureName() override;      // 006A3720
    mfc::CString PictureName() override;     // 006A37B0

    // 006A3140 (RET 8; slot 15; party_items.cpp). CItem::CopyTo, then absorb_ and layerMask_.
    BOOL CopyTo(CItem* target, CSpellManager* spells) override;
    // 006A2F20 (item_lifetime.cpp). CItem's, then absorb_ and layerMask_ 0.
    CItemArmor* Construct();
    // 006A3070 (slot 13; item_slots.cpp). CItem::InitFromRecord, then the two absorption rows.
    void InitFromRecord(const itemdb::ItemRecordHead* record) override;
    // 006A30D0 (slot 14). CItem::IsSameAs, then absorb_ and layerMask_ equal as dwords.
    BOOL IsSameAs(CItem* other) override;
    // 006A3A70 / 006A3AA0 (slots 9 / 10; item_save.cpp). CItem's, then the two absorption rows
    // (0x1C bytes each; layerMask_ is not saved).
    void Load(void* stream) override;
    void Save(void* stream) override;

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
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x00743EAC);

    // 006A3CD0. Slot 19: "qitem<number>.<material name>" when the prototype's +0x10 name is not
    // "none" ("uq" for a material out of range), else "qitem<number>" (-1 without a prototype).
    mfc::CString FigureName() override;
    // 006A4040. Slot 20: "initqi<number>item".
    mfc::CString PictureName() override;
    // 006A3C50 (RET 8; slot 15; party_items.cpp). CItem::CopyTo, then +0x4C.
    BOOL CopyTo(CItem* target, CSpellManager* spells) override;
    // 006A3AD0 (item_lifetime.cpp). CItem's and the vtable (+0x4C is left as it is).
    CItemQuick* Construct();
    // 006A3BE0 / 006A3C20 (slots 13 / 14, also the scrolls'; item_slots.cpp). The base's, then
    // +0x4C (prototype +0x70).
    void InitFromRecord(const itemdb::ItemRecordHead* record) override;
    BOOL IsSameAs(CItem* other) override;
    // 006A3C80 / 006A3CA0 (slots 9 / 10, also the scrolls'; item_save.cpp). CItem's, then +0x4C.
    void Load(void* stream) override;
    void Save(void* stream) override;

    std::uint32_t actionValue_;         // +0x4C the record's value70 (CUnitServer::ScaledItemValue)
};
static_assert(sizeof(CItemQuick) == 0x50);

// vtable 00743F04, GetClassId 0x14.
class CItemQuest : public CItem {
public:
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x00743F04);

    mfc::CString FigureName() override;    // 006A45C0 "quitem<number>"
    mfc::CString PictureName() override;   // 006A47B0 "initqu<number>item"
    // 006A4540 (RET 8; slot 15; party_items.cpp). CItem::CopyTo, then +0x4C.
    BOOL CopyTo(CItem* target, CSpellManager* spells) override;
    // 006A43A0 (item_lifetime.cpp). CItem's and the vtable (+0x4C is left as it is).
    CItemQuest* Construct();
    // 006A44D0 / 006A4510 (slots 13 / 14; item_slots.cpp). The base's, then +0x4C (prototype
    // +0x6C, the category).
    void InitFromRecord(const itemdb::ItemRecordHead* record) override;
    BOOL IsSameAs(CItem* other) override;
    // 006A4570 / 006A4590 (slots 9 / 10; item_save.cpp). CItem's, then +0x4C.
    void Load(void* stream) override;
    void Save(void* stream) override;
    // 006A43C0 (RET 4; slot 21; item_slots.cpp, A-H25). Fills `out` with (8, the id, 0) and
    // returns it; the other classes share 004623C0, which writes three zeros.
    ItemUse* GetUse(ItemUse* out) override;

    // +0x4C the quest item's id (the prototype's category): RemoveQuestItem(unit, id), the
    // player's quest items, the zone's wanted list.
    std::uint32_t questItemId_;
};
static_assert(offsetof(CItemQuest, questItemId_) == 0x4C);

// vtable 00743FB4, GetClassId 0x16: a scroll (spell container). Its slots 9, 10, 13 and 14 are
// CItemQuick's bodies.
class CItemSpell : public CItem {
public:
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x00743FB4);

    // 006A5820. Slot 19: "spell<+0x60 of the carried spell's prototype>" (no null checks).
    mfc::CString FigureName() override;
    // 006A58E0 (A-H25). Slot 20: "initqi1item".
    mfc::CString PictureName() override;
    // 006A57F0 (RET 8; slot 15; party_items.cpp). CItem::CopyTo, then +0x4C.
    BOOL CopyTo(CItem* target, CSpellManager* spells) override;
    // 006A56E0 (item_lifetime.cpp). CItem's and the vtable (+0x4C is left as it is).
    CItemSpell* Construct();

    std::uint32_t actionValue_;         // +0x4C left as it is by Construct; CopyTo copies it
};

// vtable 00743F5C, GetClassId 0x15.
class CItemLoot : public CItem {
public:
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x00743F5C);

    // 006A5470. Virtual slot 16: the base price. Depends on what the loot stands for (kind_):
    // itself (price_), a material, or a record of another database (recordType_).
    float GetPrice() override;

    // 006A4B90. Slot 19, by kind_: a record's name ("<type>_<number>.<variant>" for weapons and
    // armors, "qitem", "prototype" (spells, +0x60), "modifier" + number), "material<index>", or
    // "litem<number>" for the loot itself.
    mfc::CString FigureName() override;
    // 006A5030. Slot 20, the picture: "initwe..." / "initar..." / "initqi..." for records,
    // "initqi1item" for spells and modifiers, "initlimt<index>item" for materials,
    // "initlitr<number>item" for the loot itself.
    mfc::CString PictureName() override;
    // 006A4B50 (RET 8; slot 15; party_items.cpp). CItem::CopyTo, then kind_, recordType_, index_.
    BOOL CopyTo(CItem* target, CSpellManager* spells) override;
    // 006A49A0 (item_lifetime.cpp). CItem's and the vtable (kind, record type and index are left
    // as they are).
    CItemLoot* Construct();
    // 006A4AB0 / 006A4B00 (slots 13 / 14; item_slots.cpp). The base's, then kind_, recordType_
    // and index_ (prototype +0x6C..+0x74).
    void InitFromRecord(const itemdb::ItemRecordHead* record) override;
    BOOL IsSameAs(CItem* other) override;
    // 006A5630 / 006A5680 (slots 9 / 10; item_save.cpp). CItem's, then kind_, recordType_ and
    // index_. Load reads the first two through the stack slot of `stream` (a read that writes
    // nothing leaves the pointer's value, then the previous value).
    void Load(void* stream) override;
    void Save(void* stream) override;

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
// 006A0AA0 (RET 4; A-H25). The same for weapon prototype `weaponIndex` ("sw", "ax", ...).
void __fastcall WeaponModelName(mfc::CString* out, std::uint32_t weaponIndex, std::uint32_t materialIndex);
// 006A0BF0 (RET 4; A-H25). The same from the records themselves (no null checks, `out` not
// emptied first); for a type without a code the code is the bytes of `out`'s address.
void __fastcall WeaponRecordModelName(mfc::CString* out, const itemdb::WeaponRecord* weapon,
                                      const itemdb::MaterialRecordHead* material);

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

    // 006A5B90. Virtual slot 2, the reset: forgets the manager (its count dropped), releases the
    // item and clears the records and the spell. Returns 1.
    int Create() override;

    // 006A5BD0. Virtual slot 3: the same, no result.
    void Destroy() override;

    // 006A5B20. The destructor body (vtable 00744020): Destroy's work, then the base destructor.
    void DestructBody();
    // 006A5B00 (slot 0; A-H25). DestructBody, then operator delete when bit 0 of `flags` is set.
    CItemConstructor* DeletingDestruct(unsigned flags);

    // 006A5C00. Counts a reference to `manager` (+4), dropping the previous one's (no release).
    void SetManager(CBaseManager* manager);

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

    // 006A6480 (RET 8; sets only AL; A-H25). Whether the item being built can hold a spell: not a
    // weapon or armour (class 0x11 / 0x12) without a material. `level` gets the prototype's +0x44
    // plus the material's +0x1C (unsigned, as a float), `power` the prototype's charge plus the
    // material's +0x18; without a material the prototype's values alone.
    bool MagicCapacity(float* level, float* power);

    CBaseManager* manager_;   // +0x1C the item manager; counted (+4) but never released
    char* material_;          // +0x20 material record or null
    char* prototype_;         // +0x24 prototype record
    CItem* item_;             // +0x28 counted
    CSpell* spell_;     // +0x2C
    int count_;               // +0x30 copied to the item's +0x48
};
static_assert(sizeof(CItemConstructor) == 0x34);

// The context a manager belongs to (CBaseManager::context_): the world or a party, whose slots
// 13-24 are the same getters (CWorld, CParty). Declared up to the slot used here.
class CManagerContext : public CEngineObject {
public:
    virtual void* ThisWorld();                     // 13: the world (0055EB40 returns this), null for a party (0065F4B0)
    virtual void TimedObjectManager();             // 14
    virtual void FigureManager();                  // 15
    virtual CItemConstructor* ItemConstructor();   // 16
    virtual CSpellConstructor* SpellConstructor(); // 17
    virtual void ItemFactory();                    // 18
    virtual void MapObjectManager();               // 19
    virtual void PlayerList();                     // 20
    virtual CBaseManager* SpecificManager();       // 21: makes the figures' drawers (class 0x45; null for a party)
    virtual CSpellManager* SpellManager();         // 22
};

// The item manager (007AFFB4; a CResourceManager with vtable 0073F25C, 0xBC bytes, as the world
// and every party make one). Only the functions rewritten here are declared; context_ (+0xAC) is
// its manager context (slot 16: the item constructor, slot 22: the spell manager).
struct CItemManager : CResourceManager {
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x0073F25C);
    static constexpr std::size_t kSize = 0xBC;

    // 006AC780 (slot 2; item_lifetime.cpp, A-H25). CBaseManager::Create's result as 0 / 1 (the
    // original also copies the text folder's name into a temporary it does not use).
    int Create() override;
    // 006AC810 (slot 14, RET 4; item_lifetime.cpp, A-H25). The item factory: a new constructed
    // item for kind 0x3004 weapon, 0x3005 armour, 0x3006 quick, 0x3007 loot, 0x3008 scroll, 0x3009
    // quest, its type_ set to the kind; null for another kind or without memory.
    CEngineObject* NewObject(int kind) override;

    // 006AD000 (RET 0x10; item_constructor.cpp, A-H20). An item of `kind` built by the context's
    // item constructor (BeginCreateItem with `kind` and `key`) from prototype record `index` of
    // the kind's table (an index whose low 16 bits are 0xFFFF: 0) and, for weapons and armor,
    // material record `material`; an index out of range or another kind gives a null record.
    // Returns the item (null when the build fails).
    CItem* MakeItem(int kind, std::uint32_t index, std::uint32_t material, int key);

    // 006AC970 (RET 4). An item from its name, "prototype[.material][[extra]]" (lowercased):
    // "spell container[spell]" makes a scroll; otherwise the prototype is looked up in the weapon,
    // armor, quick, 007C8758 and 007C8778 tables (none: FatalError, null) and built with the
    // material, the bracket part as a spell (or, for prototypes of type "material", as the count);
    // a class-0x15 item of kind 2 gets the table index of the second name part at +0x54. A failed
    // build reports FatalError and the original crashes; Rebirth's FixCreateItem returns null in
    // both cases without FatalError.
    CItem* CreateItem(const char* name);

    // 006AD1E0 (RET 8; A-H25). A scroll ("spell container", kind 0x3008) holding the spell that
    // `spells` builds from `spellText`; null when the spell or the item cannot be made (the spell
    // is then left as it is).
    CItem* CreateSpellItem(const char* spellText, CSpellManager* spells);

    // 006AD260 / 006AD2F0 / 006AD380 / 006AD410 (RET 4; A-H25). `names` resized to the table's
    // record count and filled with the records' names: the materials' keys (+0x00), the weapons',
    // armours' and quick items' +0x04. `this` is not used.
    void ListMaterialNames(mfc::CStringArray* names);
    void ListWeaponNames(mfc::CStringArray* names);
    void ListArmorNames(mfc::CStringArray* names);
    void ListQuickItemNames(mfc::CStringArray* names);
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
