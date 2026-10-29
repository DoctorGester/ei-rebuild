#pragma once

// Items: the in-game objects created from the item databases. prototypeId of CEngineObject is the item's
// packed id: prototype index << 16 | material index.

#include "base_manager.h"
#include "core_refcount.h"
#include "engine_object.h"
#include "item_db.h"
#include "lock_key.h"

#include <cstdint>
#include <string>

class CSpell;
class CSpellManager;
class CStream;

inline std::uint32_t PrototypeIndex(std::uint32_t packedId) { return packedId >> 16; }
inline std::uint32_t MaterialIndex(std::uint32_t packedId) { return packedId & 0xFFFF; }
inline std::uint32_t PackItemId(std::uint32_t prototypeIndex, std::uint32_t materialIndex) {
    return prototypeIndex << 16 | (materialIndex & 0xFFFF);
}

// The packed id of no item (an empty slot), and the half of a packed id that names no prototype / material.
constexpr std::uint32_t kNoItemId = 0xFFFFFFFFu;
constexpr std::uint32_t kNoIndex = 0xFFFF;

// The prototype record of an item of `kind` at `index` (scrolls: the quick item table); null for another kind or
// an index out of range.
itemdb::ItemRecordHead* PrototypeAt(ItemKind kind, std::uint32_t index);

// Armour types 0-2 are worn on the upper body.
inline bool IsUpperBodyArmor(int type) { return type == 0 || type == 1 || type == 2; }

class CItemLink;

// Base of every item. The six item classes override its virtuals (each calls CItem's first).
class CItem : public CEngineObject {
public:
    CItem();
    ~CItem() override;

    void Create() override;
    void Destroy() override;
    // The recharge: charge moves towards chargeMax by chargeRate * chargeMax. 0 when already full.
    bool RunTick() override;
    // The packed id, condition..unreadValue, the spell's id (-1: none; Load finds it in the context's spell
    // manager and counts it), count and magicLevel, four bytes each, in that order.
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;
    // The classes call it first: price, weight, unreadValue, charge and chargeMax, magicLevel, condition
    // and conditionMax from the record (see CItemConstructor::FillItem); chargeRate 0.001.
    virtual void InitFromRecord(const itemdb::ItemRecordHead* record);
    // 1 for the item itself; else the same kind, packed id, magicLevel and (equal or unordered floats)
    // condition..chargeMax and weight..unreadValue, and both without a spell or both with one of the same
    // packed id and modifiers.
    virtual bool IsSameAs(CItem* other);
    // `target`, when of the same kind, gets (after its Destroy) this item's packed id, condition..magicLevel,
    // a copy of the spell (CSpell::Clone with `spells`) and count 1. 0 for another kind.
    virtual bool CopyTo(CItem* target, CSpellManager* spells);
    virtual float GetPrice() { return price; }
    // "" for a plain item.
    virtual std::string ModelName();
    virtual std::string FigureName();
    virtual std::string PictureName();
    // The key the item works as on a lock (AcceptsKey, a lever's or a trap's ApplyScience): zeros; the quest
    // item's has its id.
    virtual LockKey GetUse();

    // A new item of the same kind from the owning manager, given this one's values through the copy virtual
    // with the spell manager `spells`; when that fails the new item is released and the result is null.
    CItem* Clone(CSpellManager* spells);

    // The prototype record for the item's kind and index; null for spell containers,
    // other kinds and indices out of range.
    itemdb::ItemRecordHead* GetPrototype();

    // QuickItemCategory::NoPrototype without a prototype.
    QuickItemCategory GetCategory();
    // type read as the item's kind.
    ItemKind Kind() const { return static_cast<ItemKind>(type); }

    // Attaches `spell` (counted; the previous one released), moving the price from the
    // previous spell's to the new one's.
    void AttachSpell(CSpell* spell);

    void Repair() { condition = conditionMax; }
    float Weight() const { return weight; }

    // a link record: the world server's CItemLink (the id the world's save keeps per item, or the party record's
    // item), the HUD's ItemTurn.
    Ref<CRefObject> link;
    CItemLink* ServerLink() const;
    // the condition and its maximum (both from the prototype's; -1.0 until then): at 0 or below (rounded) the
    // item is broken.
    float condition;
    float conditionMax;
    float charge;
    float chargeMax;              // the prototype's charge
    float chargeRate;             // 0.001 by default
    float weight;                 // the prototype's weight
    float price;                  // includes the attached spell's price
    float unreadValue;            // the prototype's unreadValue; only saved, loaded and compared by IsSameAs
    // the level of spell the item can hold (the prototype's magicLevel plus the material's; MagicCapacityOf).
    std::uint32_t magicLevel;
    Ref<CSpell> spell;            // the spell carried by the item (scrolls, wands)
    std::uint32_t count;          // the stack's count
};

// The record an item's link (CItem::link) points at as the world server makes it: a counted value holding
// the dword the world's save keeps per item. When the server builds a unit's items from its party record it
// holds the id of the record's item.
class CItemLink final : public CRefObject {
public:
    // No references, no flags.
    CItemLink();
    // Built again in place (the count and the value stay): nothing changes. Returns 0.
    void OnShutdown() override;
    // A new link with this one's value and no references; null without memory.
    CItemLink* Clone() override;

    std::uint32_t value;
};
inline CItemLink* CItem::ServerLink() const { return static_cast<CItemLink*>(link.Get()); }

class CItemWeapon : public CItem {
public:
    CItemWeapon();
    const char* ClassName() override;
    ClassId GetClassId() override;

    // The weapon type from the prototype (faults if the index is out of range).
    int GetType();

    // The prototype's damage type weights; zeros without a prototype.
    void GetDamageTypes(float (&out)[kDamageTypeCount]);

    // damage + a random fraction of damageSpread (the global ISAAC generator), unrounded: callers round when
    // they store it. double so the value is not rounded to float on return when the FPU runs at more than
    // 24-bit precision (it does under Rebirth).
    double RollDamage();

    bool IsSameAs(CItem* other) override;

    // Names built from the prototype. Model name: "<type>_<number>.<material name>.<variant>" (type "sw",
    // "ax", ...; empty without a prototype or material). Figure name: "unhuma" + the model name. Picture name:
    // "initwe<type><number>weapon".
    std::string ModelName() override;
    std::string FigureName() override;
    std::string PictureName() override;

    bool CopyTo(CItem* target, CSpellManager* spells) override;
    void InitFromRecord(const itemdb::ItemRecordHead* record) override;
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;

    std::uint32_t actionValue;          // 0 after construction; CUnitServer::ScaledItemValue scales it by 15 / the rate, the hit takes it
    float damage;
    float damageSpread;
    float reach;            // prototype reach: the wielder's reach
};

class CItemArmor : public CItem {
public:
    const char* ClassName() override;
    ClassId GetClassId() override;

    // The armor type from the prototype (faults if the index is out of range).
    int GetType();

    // The unit's armour layer group the piece adds to (UnitStatLayer): 2 for upper body armour, else 1.
    int GetTypeGroup() { return IsUpperBodyArmor(GetType()) ? 2 : 1; }

    // The same four names as CItemWeapon's, with the armor types ("hl", "pl",
    // ...) and the picture name "initar<type><number>armor".
    std::string ModelName() override;
    std::string FigureName() override;
    std::string PictureName() override;

    bool CopyTo(CItem* target, CSpellManager* spells) override;
    void InitFromRecord(const itemdb::ItemRecordHead* record) override;
    bool IsSameAs(CItem* other) override;
    // CItem's, then the two absorption rows (0x1C bytes each; layerMask is not saved).
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;

    // What the piece adds to the body slots it covers (UnitStats::AddArmourLayer): [0] for the
    // first covered slot, [1] for the others.
    float absorb[2][kDamageTypeCount] = {};
    std::uint32_t layerMask[2] = {};
};

class CItemQuick : public CItem {
public:
    const char* ClassName() override;
    ClassId GetClassId() override;

    // "qitem<number>.<material name>" when the prototype's materialClass is not "none" ("uq" for a material
    // out of range), else "qitem<number>" (-1 without a prototype).
    std::string FigureName() override;
    std::string PictureName() override;
    bool CopyTo(CItem* target, CSpellManager* spells) override;
    // Also the scrolls'. The base's, then actionValue.
    void InitFromRecord(const itemdb::ItemRecordHead* record) override;
    bool IsSameAs(CItem* other) override;
    // Also the scrolls'. CItem's, then actionValue.
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;

    std::uint32_t actionValue;          // the record's actionValue (CUnitServer::ScaledItemValue)
};

class CItemQuest : public CItem {
public:
    const char* ClassName() override;
    ClassId GetClassId() override;

    std::string FigureName() override;    // "quitem<number>"
    std::string PictureName() override;   // "initqu<number>item"
    bool CopyTo(CItem* target, CSpellManager* spells) override;
    void InitFromRecord(const itemdb::ItemRecordHead* record) override;
    bool IsSameAs(CItem* other) override;
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;
    // (8, the id, 0); the other classes return zeros.
    LockKey GetUse() override;

    // the quest item's id (the prototype's category): RemoveQuestItem(unit, id), the
    // player's quest items, the zone's wanted list.
    std::uint32_t questItemId;
};

// A scroll, a spell container: a quick item with its own names, class name and id.
class CItemScroll : public CItemQuick {
public:
    const char* ClassName() override;
    ClassId GetClassId() override;

    // "spell<the carried spell's prototype icon>" (no null checks).
    std::string FigureName() override;
    // "initqi1item".
    std::string PictureName() override;
};

class CItemLoot : public CItem {
public:
    const char* ClassName() override;
    ClassId GetClassId() override;

    // The base price. Depends on what the loot stands for (kind): itself (price), a material, or a record
    // of another database (recordType).
    float GetPrice() override;

    // By kind: a record's name ("<type>_<number>.<variant>" for weapons and armors, "qitem", "prototype"
    // (spells, by icon), "modifier" + number), "material<index>", or "litem<number>" for the loot itself.
    std::string FigureName() override;
    // "initwe..." / "initar..." / "initqi..." for records, "initqi1item" for spells and modifiers,
    // "initlimt<index>item" for materials, "initlitr<number>item" for the loot itself.
    std::string PictureName() override;
    bool CopyTo(CItem* target, CSpellManager* spells) override;
    // The base's, then kind, recordType and index.
    void InitFromRecord(const itemdb::ItemRecordHead* record) override;
    bool IsSameAs(CItem* other) override;
    // CItem's, then kind, recordType and index.
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;

    LootKind kind;
    LootRecordType recordType;    // for LootKind::Record
    std::uint32_t index;        // record index
};

// Weapon stats from a packed id. Each faults if the prototype index is out of range; an out-of-range
// material reads the multiplier from address 0 likewise.
int WeaponType(std::uint32_t packedId);
float WeaponMinDamage(std::uint32_t packedId);
float WeaponMaxDamage(std::uint32_t packedId);
// The prototype's weight times the material's weightFactor; the unit's attack info shows it as the speed.
float WeaponWeight(std::uint32_t packedId);
int WeaponDamageType(std::uint32_t packedId);    // first non-zero weight, else 6
float WeaponAttackBonus(std::uint32_t packedId);
float WeaponDefenceBonus(std::uint32_t packedId);

// The armour prototype's layer by prototype index; 0 if out of range.
int ArmorLayer(std::uint32_t index);

// The item with id `id` in an item manager (a party's, the world's item factory), or null.
inline CItem* FindItem(const CBaseManager* items, unsigned id) { return items->FindObjectAs<CItem>(id); }

// An item known to be a weapon as the CItemWeapon it is: a weapon slot's item (CUnit::weapons,
// PartyUnitRecord::weapons hold weapons only), an item of the weapon kind, the copy or match of a
// weapon. The weapon's methods are called without a class test.
inline CItemWeapon* AsWeapon(CItem* item) { return static_cast<CItemWeapon*>(item); }
inline const CItemWeapon* AsWeapon(const CItem* item) { return static_cast<const CItemWeapon*>(item); }
// An item known to be armour as the CItemArmor it is: a hero record's armour (PartyUnitRecord::armor holds
// armour only), an item of the armour kind, the copy or match of an armour piece, the item put on as armour
// (CUnit::EquipArmour). The armour's methods are called without a class test.
inline CItemArmor* AsArmor(CItem* item) { return static_cast<CItemArmor*>(item); }
inline const CItemArmor* AsArmor(const CItem* item) { return static_cast<const CItemArmor*>(item); }
// An item known to be loot as the CItemLoot it is: an item of the loot kind (ItemKind::Loot), one made by name from
// a loot prototype (runes, materials, instructions), the copy or match of a loot item. Its fields are read
// without a class test.
inline CItemLoot* AsLoot(CItem* item) { return static_cast<CItemLoot*>(item); }
inline const CItemLoot* AsLoot(const CItem* item) { return static_cast<const CItemLoot*>(item); }
// An item known to be a quick item / a quest item: the copy or match of one (the base IsSameAs
// compares the classes first), an item of class kClassQuestItem.
inline CItemQuick* AsQuick(CItem* item) { return static_cast<CItemQuick*>(item); }
inline CItemQuest* AsQuest(CItem* item) { return static_cast<CItemQuest*>(item); }
