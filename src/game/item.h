#pragma once

// Items: the in-game objects created from the item databases. Class names come from
// the game's own GetClassName strings; field names are provisional.
// prototypeId_ of CEngineObject is the item's packed id: prototype index << 16 | material index.

#include "core_refcount.h"
#include "engine_object.h"
#include "manager_context.h"
#include "item_db.h"
#include "lock_key.h"   // LockKey
#include "resource_manager.h"
#include "../lib/mfc.h"
#include "game_data.h"

#include <cstring>

#undef GetClassName

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

// Base of every item (22 virtuals; two pure). The six item classes override them (each calls CItem's first).
class CItemLink;   // below

class CItem : public CEngineObject {
public:
    CItem();
    ~CItem() override;

    // Count 1, the spell released; returns 1.
    int Create() override;
    // The spell released and the link (+0x1C, a CRefCounted: 16-bit count, deleted at 0) let go.
    void Destroy() override;
    // Called every tick. The recharge: moves charge_ towards chargeMax_ by chargeRate_ * chargeMax_.
    // Returns 0 when already full.
    int RunTick() override;
    // Read / write (a CStream) the packed id (+0x18), +0x20..+0x3C, the spell's id (-1: none; Load finds it
    // in the spell manager of the manager context, and counts it), +0x48 and +0x40, four bytes each, in
    // that order.
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;
    // The classes call it first: takes the item's values from a database record (see
    // CItemConstructor::Build): +0x34 price, +0x38, +0x3C, +0x40 charge (current and maximum), +0x44, +0x48
    // (+0x20 and +0x24); charge rate 0.001.
    virtual void InitFromRecord(const itemdb::ItemRecordHead* record);
    // 1 for the item itself; else the same kind, packed id, values +0x40 and (equal or unordered floats)
    // +0x20..+0x2C, +0x34..+0x3C, and both without a spell or both with one of the same packed id and
    // modifiers.
    virtual BOOL IsSameAs(CItem* other);
    // Gives `target`, when it is of the same kind, this item's packed id and values +0x20..+0x40 (after its
    // Destroy), a copy of the attached spell (CSpell::Clone with `spells`; or none) and a count (+0x48) of
    // 1. Returns 0 for another kind, else 1.
    virtual BOOL CopyTo(CItem* target, CSpellManager* spells);
    virtual float GetPrice() { return price_; }
    // The model, first list, figure and picture names; "" for a plain item. Each is built
    // in the caller's result.
    virtual mfc::CString ModelName();
    virtual mfc::CString FirstListName();
    virtual mfc::CString FigureName();
    virtual mfc::CString PictureName();
    // The key the item works as on a lock (map object slot 42 AcceptsKey, a lever's or a trap's
    // ApplyScience): zeros; the quest item's has its id.
    virtual LockKey GetUse();
    BOOL IsSameAsSlot(CItem* other) { return IsSameAs(other); }

    // A new item of the same kind from the owning manager, given this one's values through the copy
    // virtual with the spell manager `spells`; when that fails the new item is released and the result is
    // null.
    CItem* Clone(CSpellManager* spells);

    // The prototype record for the item's kind and index; null for spell containers,
    // other kinds and indices out of range.
    itemdb::ItemRecordHead* GetPrototype();

    // The prototype's category (+0x6C); INT_MAX if there is no prototype.
    int GetCategory();

    // Attaches `spell` (counted; the previous one released), moving the price from the
    // previous spell's to the new one's.
    void AttachSpell(CSpell* spell);

    // Attaches `spell` (counted; the previous one released); the price is left alone.
    void SetSpell(CSpell* spell);
    void Repair() { condition_ = conditionMax_; }
    float Weight() const {
        float weight;
        std::memcpy(&weight, &weight_, sizeof(weight));
        return weight;
    }

    // a counted link record (a RefPtr slot): the world server's CItemLink (the id the
    // world's save keeps per item, or the party record's item), the HUD's ItemTurn.
    void* link_;
    // The link as the world server makes it (CItemLink, below; null when there is none).
    CItemLink* ServerLink() const;
    // the condition and its maximum (both from the prototype's; -1 bits, a NaN,
    // until then): at 0 or below (rounded) the item is broken.
    float condition_;
    float conditionMax_;
    float charge_;
    float chargeMax_;             // prototype +0x40
    float chargeRate_;            // 0.001 by default
    std::uint32_t weight_;        // float bits (Weight()); prototype value38
    float price_;                 // includes the attached spell's price
    std::uint32_t value3C_;     // prototype value3C
    // the level of spell the item can hold (the prototype's magicLevel plus the material's;
    // CItemConstructor::MagicCapacity).
    std::uint32_t magicLevel_;
    CSpell* spell_;               // the spell carried by the item (scrolls, wands)
    std::uint32_t count_;         // the stack's count
};

// The record an item's link (CItem::link_) points at as the world server makes it: a counted
// value holding the dword the world's save keeps per item (world_save.cpp, world_units.cpp,
// world_quests.cpp). When the server builds a unit's items from its party record it
// holds the id of the record's item. Methods in item_link.cpp (A-W14).
class CItemLink final : public CRefObject {
public:
    // No references, no flags (the count's dword is cleared before the allocation is tested).
    CItemLink();
    // Built again in place (the count and the value stay): nothing changes. Returns 0.
    int OnShutdown() override;
    // A new link with this one's value and no references; null without memory.
    CItemLink* Clone() override;

    std::uint32_t value;
};
inline CItemLink* CItem::ServerLink() const { return static_cast<CItemLink*>(link_); }

class CItemWeapon : public CItem {
public:
    CItemWeapon();
    const char* GetClassName() override;
    int GetClassId() override;             // 0x11

    // The weapon type from the prototype (faults if the index is out of range).
    int GetType();

    // Copies the prototype's seven damage-type weights to `out` (zeros if none).
    void GetDamageTypes(float* out);

    // Damage roll: damage_ + random fraction of damageSpread_ (uses the global ISAAC generator). Returned
    // unrounded in ST0 (callers round when they store it); declared double so the value is not rounded to
    // float on return when the FPU runs at more than 24-bit precision (it does under Rebirth).
    double RollDamage();

    // Virtual: the same item as `other` (base comparison plus the weapon fields).
    BOOL IsSameAs(CItem* other) override;

    // Names built from the prototype. Model name: "<type>_<number>.<material
    // name>.<variant>" (type "sw", "ax", ...; empty without a prototype or material). First list name: the
    // first string of the prototype's string list (+0x20), else "". Figure name: "unhuma" + the model name.
    // Picture name: "initwe<type><number>weapon".
    mfc::CString ModelName() override;
    mfc::CString FirstListName() override;
    mfc::CString FigureName() override;
    mfc::CString PictureName() override;

    BOOL CopyTo(CItem* target, CSpellManager* spells) override;
    void InitFromRecord(const itemdb::ItemRecordHead* record) override;
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;

    std::uint32_t actionValue_;         // 0 after construction; CUnitServer::ScaledItemValue scales it by 15 / the rate, the hit takes it (DamageInfo +0x1C)
    float damage_;          // prototype +0x90
    float damageSpread_;    // prototype +0x94
    float reach_;           // prototype reach: the wielder's reach
};

class CItemArmor : public CItem {
public:
    CItemArmor();
    const char* GetClassName() override;
    int GetClassId() override;             // 0x12

    // The armor type from the prototype (faults if the index is out of range).
    int GetType();

    // 2 for armor types 0, 1 and 2, 1 for every other type.
    int GetTypeGroup();

    // The same four names as CItemWeapon's, with the armor types ("hl", "pl",
    // ...) and the picture name "initar<type><number>armor".
    mfc::CString ModelName() override;
    mfc::CString FirstListName() override;
    mfc::CString FigureName() override;
    mfc::CString PictureName() override;

    BOOL CopyTo(CItem* target, CSpellManager* spells) override;
    void InitFromRecord(const itemdb::ItemRecordHead* record) override;
    BOOL IsSameAs(CItem* other) override;
    // CItem's, then the two absorption rows (0x1C bytes each; layerMask_ is not saved).
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;

    // What the piece adds to the body slots it covers (UnitStats::AddArmourLayer): [0] for the
    // first covered slot, [1] for the others.
    float absorb_[2][7];            // per damage type
    std::uint32_t layerMask_[2];
};

class CItemQuick : public CItem {
public:
    CItemQuick();
    const char* GetClassName() override;
    int GetClassId() override;             // 0x13

    // "qitem<number>.<material name>" when the prototype's +0x10 name is not
    // "none" ("uq" for a material out of range), else "qitem<number>" (-1 without a prototype).
    mfc::CString FigureName() override;
    mfc::CString PictureName() override;
    BOOL CopyTo(CItem* target, CSpellManager* spells) override;
    // Also the scrolls'. The base's, then +0x4C (prototype +0x70).
    void InitFromRecord(const itemdb::ItemRecordHead* record) override;
    BOOL IsSameAs(CItem* other) override;
    // Also the scrolls'. CItem's, then +0x4C.
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;

    std::uint32_t actionValue_;         // the record's value70 (CUnitServer::ScaledItemValue)
};

class CItemQuest : public CItem {
public:
    CItemQuest();
    const char* GetClassName() override;
    int GetClassId() override;             // 0x14

    mfc::CString FigureName() override;    // "quitem<number>"
    mfc::CString PictureName() override;   // "initqu<number>item"
    BOOL CopyTo(CItem* target, CSpellManager* spells) override;
    void InitFromRecord(const itemdb::ItemRecordHead* record) override;
    BOOL IsSameAs(CItem* other) override;
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;
    // Fills `out` with (8, the id, 0) and returns it; the other classes write three zeros.
    LockKey GetUse() override;

    // the quest item's id (the prototype's category): RemoveQuestItem(unit, id), the
    // player's quest items, the zone's wanted list.
    std::uint32_t questItemId_;
};

// A scroll, a spell container: a quick item (its virtual table has CItemQuick's save and load and match and
// init-from-record entries) with its own copy, names, class name and id.
class CItemSpell : public CItemQuick {
public:
    CItemSpell();
    const char* GetClassName() override;
    int GetClassId() override;             // 0x16

    // "spell<+0x60 of the carried spell's prototype>" (no null checks).
    mfc::CString FigureName() override;
    // Picture name: "initqi1item".
    mfc::CString PictureName() override;
    BOOL CopyTo(CItem* target, CSpellManager* spells) override;
};

class CItemLoot : public CItem {
public:
    CItemLoot();
    const char* GetClassName() override;
    int GetClassId() override;             // 0x15

    // The base price. Depends on what the loot stands for (kind_): itself (price_), a material, or a record
    // of another database (recordType_).
    float GetPrice() override;

    // By kind_: a record's name ("<type>_<number>.<variant>" for weapons and
    // armors, "qitem", "prototype" (spells, +0x60), "modifier" + number), "material<index>", or
    // "litem<number>" for the loot itself.
    mfc::CString FigureName() override;
    // "initwe..." / "initar..." / "initqi..." for records,
    // "initqi1item" for spells and modifiers, "initlimt<index>item" for materials,
    // "initlitr<number>item" for the loot itself.
    mfc::CString PictureName() override;
    BOOL CopyTo(CItem* target, CSpellManager* spells) override;
    // The base's, then kind_, recordType_ and index_ (prototype +0x6C..+0x74).
    void InitFromRecord(const itemdb::ItemRecordHead* record) override;
    BOOL IsSameAs(CItem* other) override;
    // CItem's, then kind_, recordType_ and index_. Load reads the first two through the stack slot of
    // `stream` (a read that writes nothing leaves the pointer's value, then the previous value).
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;

    std::uint32_t kind_;       // 0 own price, 1 material, 2 record of recordType_
    std::uint32_t recordType_;  // 0 weapon, 1 armor, 2 quick item, 3 spell, 4 spell modifier
    std::uint32_t index_;       // record index
};

// Weapon stats from a packed id. Each faults if the prototype index is out of range; an out-of-range
// material reads the multiplier from address 0 likewise.
int __fastcall WeaponType(std::uint32_t packedId);
float __fastcall WeaponMinDamage(std::uint32_t packedId);
float __fastcall WeaponMaxDamage(std::uint32_t packedId);
float __fastcall WeaponAttack(std::uint32_t packedId);
int __fastcall WeaponDamageType(std::uint32_t packedId);    // first non-zero weight, else 6
float __fastcall WeaponStat84(std::uint32_t packedId);
float __fastcall WeaponStat88(std::uint32_t packedId);

// The base figure (skeleton) a weapon prototype (by index) is animated with, into `out` (emptied first):
// "basesword", "baseaxe", "basedagger", "basepike", "baseclub" with the prototype's number as "%.2d"
// appended, "basearrow00" / "basearrow01" for types 5 / 6, else "baser_hand3". False (only AL set) for
// index -1 or out of range.
bool __fastcall WeaponBaseFigure(std::uint32_t index, mfc::CString* out);

// The armour model name "<type>_<number>.<material name>.<variant>" of armour prototype `armorIndex` in
// material `materialIndex` into `out` (emptied first; left empty when either is out of range). The type
// code is copied into the stack slot of `materialIndex`, so for a type without a code the index's own bytes
// are the code.
void __fastcall ArmorModelName(mfc::CString* out, std::uint32_t armorIndex, std::uint32_t materialIndex);
// The same for weapon prototype `weaponIndex` ("sw", "ax", ...).
void __fastcall WeaponModelName(mfc::CString* out, std::uint32_t weaponIndex, std::uint32_t materialIndex);
// The same from the records themselves (no null checks, `out` not emptied first); for a type without a code
// the code is the bytes of `out`'s address.
void __fastcall WeaponRecordModelName(mfc::CString* out, const itemdb::WeaponRecord* weapon,
                                      const itemdb::MaterialRecordHead* material);

// Armor prototype field +0xB8 by prototype index; 0 if out of range.
int __fastcall ArmorFieldB8(std::uint32_t index);

// --- Item construction -------------------------------------------------

// The item constructor: the manager creates an item object (BeginCreateItem), the caller names the prototype and
// material records and optionally a spell, and EndCreateItem fills the item from a record combining them (Build).
class CItemConstructor : public CEngineObject {
public:
    // CEngineObject's fields, no manager, records, item or spell (count_ is left as it is).
    CItemConstructor();
    ~CItemConstructor() override;
    const char* GetClassName() override;
    int GetClassId() override;             // 0x1C

    // The reset: forgets the manager (its count dropped), releases the item and clears the records and the
    // spell. Returns 1.
    int Create() override;

    void Destroy() override;

    // Counts a reference to `manager` (+4), dropping the previous one's (no release).
    void SetManager(CBaseManager* manager);

    // Adopts an existing item (counted; the previous one released); records cleared.
    void SetItem(CItem* item);

    // Creates the item through the manager (with `kind`, `key`): counted, records cleared, count 1, flag 8
    // set. Returns 1, or a fatal error's result.
    BOOL BeginCreateItem(int kind, int key);

    BOOL SetMaterial(itemdb::MaterialRecordHead* material);   // Returns 1.
    BOOL SetPrototype(itemdb::ItemRecordHead* prototype);     // Returns 1.
    BOOL SetSpellObject(CSpell* spell); // Returns 1 (not counted).

    // Builds the item. On failure releases it and returns null; otherwise hands it over
    // (its count dropped without release, flag 8 cleared) and returns it.
    CItem* EndCreateItem();

    // Releases the item; records and spell cleared. Returns 1.
    BOOL Cancel();

    // Fills the item from a temporary database record: the prototype's values combined with the material's,
    // then the item's InitFromRecord; attaches the prototype's spell (wands, category 8) or the given one;
    // sets the packed id and the count. Returns 0 for a weapon or armor without a material, else 1.
    BOOL Build();

    // Whether the item being built can hold a spell: not a weapon or armour (class 0x11 / 0x12) without a
    // material. `level` gets the prototype's +0x44 plus the material's +0x1C (unsigned, as a float),
    // `power` the prototype's charge plus the material's +0x18; without a material the prototype's values
    // alone.
    bool MagicCapacity(float* level, float* power);

    CBaseManager* manager_;   // the item manager; counted (+4) but never released
    itemdb::MaterialRecordHead* material_;   // material record or null
    itemdb::ItemRecordHead* prototype_;      // prototype record
    CItem* item_;             // counted
    CSpell* spell_;
    int count_;               // copied to the item's +0x48
};

// The item with id `id` in an item manager (a party's, the world's item factory), or null.
inline CItem* FindItem(const CBaseManager* items, unsigned id) { return items->FindObjectAs<CItem>(id); }

// An item known to be a weapon as the CItemWeapon it is: a weapon slot's item (CUnit::weapons,
// PartyUnitRecord::weapons hold weapons only), an item of the weapon kind, the copy or match of a
// weapon. The weapon's methods are called without a class test.
inline CItemWeapon* AsWeapon(CItem* item) { return static_cast<CItemWeapon*>(item); }
// An item known to be armour as the CItemArmor it is: a hero record's armour (PartyUnitRecord::armor holds
// armour only), an item of the armour kind, the copy or match of an armour piece, the item put on as armour
// (CUnit::EquipArmour). The armour's methods are called without a class test.
inline CItemArmor* AsArmor(CItem* item) { return static_cast<CItemArmor*>(item); }
// An item known to be loot as the CItemLoot it is: an item of the loot kind (0x3007), one made by name from
// a loot prototype (runes, materials, instructions), the copy or match of a loot item. Its fields are read
// without a class test.
inline CItemLoot* AsLoot(CItem* item) { return static_cast<CItemLoot*>(item); }
inline const CItemLoot* AsLoot(const CItem* item) { return static_cast<const CItemLoot*>(item); }
// An item known to be a quick item / a quest item: the copy or match of one (the base IsSameAs
// compares the classes first), an item of class kClassQuestItem.
inline CItemQuick* AsQuick(CItem* item) { return static_cast<CItemQuick*>(item); }
inline CItemQuest* AsQuest(CItem* item) { return static_cast<CItemQuest*>(item); }

// The item manager (0xBC bytes, as the world and every party make one; a real virtual class). Only the functions
// rewritten here are declared; context_ (+0xAC) is its manager context (its item constructor and its spell
// manager).
struct CItemManager : CResourceManager {
    static constexpr std::size_t kSize = 0xBC;

    // CResourceManager's constructor, then this class's vtable (inlined where a manager is made: the world's,
    // a party's Create; world_managers.cpp).
    CItemManager();
    ~CItemManager() override;
    const char* GetClassName() override;
    int GetClassId() override;               // class id 0x2F
    const char* GetFolderName() override;    // "items"
    // The deleting destructor.

    int Create() override;
    // The item factory: a new constructed item for kind 0x3004 weapon, 0x3005 armour, 0x3006 quick, 0x3007
    // loot, 0x3008 scroll, 0x3009 quest, its type_ set to the kind; null for another kind or without
    // memory.
    CEngineObject* NewObject(int kind) override;

    // An item of `kind` built by the context's item constructor (BeginCreateItem with `kind` and `key`)
    // from prototype record `index` of the kind's table (an index whose low 16 bits are 0xFFFF: 0) and, for
    // weapons and armor, material record `material`; an index out of range or another kind gives a null
    // record. Returns the item (null when the build fails).
    CItem* MakeItem(int kind, std::uint32_t index, std::uint32_t material, int key);

    // An item from its name, "prototype[.material][[extra]]" (lowercased): "spell container[spell]" makes a
    // scroll; otherwise the prototype is looked up in the weapon, armor, quick, class-0x14 and class-0x15
    // tables (none: FatalError, null) and built with the material, the bracket part as a spell (or, for
    // prototypes of type "material", as the count); a class-0x15 item of kind 2 gets the table index of the
    // second name part at +0x54. A failed build reports FatalError and the game crashes; Rebirth's
    // FixCreateItem returns null in both cases without FatalError.
    CItem* CreateItem(const char* name);

    // A scroll ("spell container", kind 0x3008) holding the spell that `spells` builds from `spellText`;
    // null when the spell or the item cannot be made (the spell is then left as it is).
    CItem* CreateSpellItem(const char* spellText, CSpellManager* spells);

    // `names` resized to the table's record count and filled with the records' names: the materials' keys
    // (+0x00), the weapons', armours' and quick items' +0x04. `this` is not used.
    void ListMaterialNames(mfc::CStringArray* names);
    void ListWeaponNames(mfc::CStringArray* names);
    void ListArmorNames(mfc::CStringArray* names);
    void ListQuickItemNames(mfc::CStringArray* names);
};
static_assert(sizeof(CItemManager) == CItemManager::kSize);

// --- Weapon model parts------------------------------------------------------

namespace console {
class CStringArray;
} // namespace console
namespace stlp {
class string;
template <typename T> class vector;
} // namespace stlp

// The names of the figure parts a weapon prototype (by index) is drawn with, replacing
// the contents of `out`: "rh3.sword07" for a sword with number 7, the bow and crossbow pieces plus
// the quiver and arrows. Nothing for an index out of range or an unknown weapon type.
void __fastcall WeaponModelParts(std::uint32_t index, console::CStringArray* out);

// The same names for a packed item id, appended to an STLport vector of strings.
void __fastcall AppendWeaponModelParts(std::uint32_t packedId, stlp::vector<stlp::string>* out);
