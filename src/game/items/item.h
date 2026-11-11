#pragma once

// Items: the in-game objects created from the item databases. prototypeId of EngineObject is the item's
// packed id: prototype index << 16 | material index.

#include "engine/core/base_manager.h"
#include "engine/core/core_refcount.h"
#include "engine/core/engine_object.h"
#include "game/items/item_db.h"
#include "game/objects/lock_key.h"

#include <array>
#include <bit>
#include <cstdint>
#include <string>

class Spell;
class SpellManager;
class ByteStream;

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

class ItemLink;
class ItemTurn;
class ItemWeapon;
class ItemArmor;
class ItemQuick;
class ItemQuest;
class ItemLoot;

// Base of every item. The six item classes override its virtuals (each calls Item's first).
class Item : public EngineObject {
public:
    void Create() override;
    void Destroy() override;
    // The recharge: charge moves towards chargeMax by chargeRate * chargeMax. 0 when already full.
    bool RunTick() override;
    // The packed id, condition..unreadValue, the spell's id (-1: none; Load finds it in the context's spell
    // manager and counts it), count and magicLevel, four bytes each, in that order. The loaders check the
    // packed id (HasPrototype).
    void Load(ByteStream* stream) override;
    void Save(ByteStream* stream) override;
    // The classes call it first: price, weight, unreadValue, charge and chargeMax, magicLevel, condition
    // and conditionMax from the record (see ItemConstructor::FillItem); chargeRate 0.001.
    virtual void InitFromRecord(const itemdb::ItemRecordHead* record);
    // True for the item itself; else the same kind, packed id, magicLevel and (equal or unordered floats)
    // condition..chargeMax and weight..unreadValue, and both without a spell or both with one of the same
    // packed id and modifiers.
    virtual bool IsSameAs(Item* other);
    // `target`, when of the same kind, gets (after its Destroy) this item's packed id, condition..magicLevel,
    // a copy of the spell (Spell::Clone with `spells`) and count 1. False for another kind.
    virtual bool CopyTo(Item* target, SpellManager* spells);
    // The base price: `price`; the loot item's depends on what it stands for (ItemLoot::GetPrice).
    virtual float GetPrice() { return price; }
    // "" for a plain item.
    virtual std::string ModelName();
    virtual std::string FigureName();
    virtual std::string PictureName();
    // The key the item works as on a lock (AcceptsKey, a lever's or a trap's ApplyScience): (8, the id, 0) for a
    // quest item, zeros for the others.
    LockKey GetUse();
    // The item as the class its Kind() says (the item manager makes each kind's class): null for another kind
    // (scrolls are quick items). Defined below the classes.
    ItemWeapon* AsWeapon();
    const ItemWeapon* AsWeapon() const;
    ItemArmor* AsArmor();
    const ItemArmor* AsArmor() const;
    ItemLoot* AsLoot();
    const ItemLoot* AsLoot() const;
    ItemQuick* AsQuick();
    ItemQuest* AsQuest();
    // The word in the place of a quest item's id, read from every class: the quest searches over a unit's
    // carried items (Player::HasLoot, the script's RemoveQuestItem) compare it without a class test, as in the
    // original (an original bug kept: docs/notes/items_spells.md).
    virtual std::uint32_t QuestWord() const { return 0; }

    // A new item of the same kind from the owning manager, given this one's values through the copy virtual
    // with the spell manager `spells`; when that fails the new item is released and the result is null.
    Item* Clone(SpellManager* spells);

    // The prototype record for the item's kind and index; null for spell containers.
    itemdb::ItemRecordHead* GetPrototype();
    // The packed id names a prototype of the item's kind; false for an item of a save, a party or a player
    // made with another item database. FillItem makes no item without a prototype, CopyTo copies one, and the
    // loaders check what they load (HasEveryRecord in item_manager.h), so every item has one and its readers
    // (the type getters, the names and texts) take it unchecked.
    bool HasPrototype();
    // Ends the game (game::FatalExit) without a prototype (HasPrototype).
    void RequirePrototype();

    // QuickItemCategory::NoPrototype without a prototype.
    QuickItemCategory GetCategory();
    // type read as the item's kind.
    ItemKind Kind() const { return static_cast<ItemKind>(type); }

    // Attaches `spell` (counted; the previous one released), moving the price from the
    // previous spell's to the new one's.
    void AttachSpell(Spell* spell);

    void Repair() { condition = conditionMax; }

    // The world server's link record: the id the world's save keeps per item, or the party record's item.
    Ref<ItemLink> serverLink;
    // The interface's turning state of the item's model (the item views make it).
    Ref<ItemTurn> turn;
    // the condition and its maximum (both from the prototype's; -1.0 until then): at 0 or below (rounded) the
    // item is broken.
    float condition = -1.0f;
    float conditionMax = -1.0f;
    float charge = -1.0f;
    float chargeMax = -1.0f;      // the prototype's charge
    float chargeRate = -1.0f;     // 0.001 once InitFromRecord ran
    float weight = -1.0f;         // the prototype's weight
    float price = -1.0f;          // includes the attached spell's price
    float unreadValue = -1.0f;    // the prototype's unreadValue; only saved, loaded and compared by IsSameAs
    // the level of spell the item can hold (the prototype's magicLevel plus the material's; MagicCapacityOf).
    std::uint32_t magicLevel = 0;
    Ref<Spell> spell;            // the spell carried by the item (scrolls, wands)
    std::uint32_t count = 1;      // the stack's count
};

// The record an item's serverLink points at as the world server makes it: a counted value holding
// the dword the world's save keeps per item. When the server builds a unit's items from its party record it
// holds the id of the record's item.
class ItemLink final : public RefObject {
public:
    // A new link with this one's value and no references.
    ItemLink* Clone() override;

    std::uint32_t value = 0;
};

// An item's turning state in the item views: made by the item draw and the moves.
class ItemTurn final : public RefObject {
public:
    ItemTurn* Clone() override;

    float angle = 0.0f;      // radians, wrapped at 2 pi
    float speed = 0.0f;
    bool flag = false;       // set by a move into a list shown turning
    float glow = 0.0f;       // phase of the spell glow
};

class ItemWeapon : public Item {
public:
    const char* ClassName() override;
    ClassId GetClassId() override;
    std::uint32_t QuestWord() const override { return actionValue; }

    // The weapon type from the prototype.
    WeaponType GetType();
    bool IsRanged() { return ::IsRanged(GetType()); }

    // The prototype's damage type weights; zeros without a prototype.
    std::array<float, kDamageTypeCount> DamageTypes();

    // damage + a random fraction of damageSpread (the global ISAAC generator).
    float RollDamage();

    bool IsSameAs(Item* other) override;

    // Names built from the prototype. Model name: "<type>_<number>.<material name>.<variant>" (type "sw",
    // "ax", ...; empty without a prototype or material). Figure name: "unhuma" + the model name. Picture name:
    // "initwe<type><number>weapon".
    std::string ModelName() override;
    std::string FigureName() override;
    std::string PictureName() override;

    bool CopyTo(Item* target, SpellManager* spells) override;
    void InitFromRecord(const itemdb::ItemRecordHead* record) override;
    void Load(ByteStream* stream) override;
    void Save(ByteStream* stream) override;

    std::uint32_t actionValue = 0;      // UnitServer::ScaledItemValue scales it by 15 / the rate, the hit takes it
    float damage = -1.0f;
    float damageSpread = -1.0f;
    float reach = -1.0f;    // prototype reach: the wielder's reach
};

class ItemArmor : public Item {
public:
    const char* ClassName() override;
    ClassId GetClassId() override;
    std::uint32_t QuestWord() const override { return std::bit_cast<std::uint32_t>(absorb[0][0]); }

    // The armor type from the prototype.
    ArmorType GetType();

    // The unit's armour layer group the piece adds to (UnitStatLayer): 2 for upper body armour, else 1.
    int GetTypeGroup() { return IsUpperBodyArmor(GetType()) ? 2 : 1; }

    // The same four names as ItemWeapon's, with the armor types ("hl", "pl",
    // ...) and the picture name "initar<type><number>armor".
    std::string ModelName() override;
    std::string FigureName() override;
    std::string PictureName() override;

    bool CopyTo(Item* target, SpellManager* spells) override;
    void InitFromRecord(const itemdb::ItemRecordHead* record) override;
    bool IsSameAs(Item* other) override;
    // Item's, then the two absorption rows (0x1C bytes each; layerMask is not saved).
    void Load(ByteStream* stream) override;
    void Save(ByteStream* stream) override;

    // What the piece adds to the body slots it covers (UnitStats::AddArmourLayer): [0] for the
    // first covered slot, [1] for the others.
    std::array<std::array<float, kDamageTypeCount>, 2> absorb{};
    std::uint32_t layerMask[2] = {};
};

class ItemQuick : public Item {
public:
    const char* ClassName() override;
    ClassId GetClassId() override;
    std::uint32_t QuestWord() const override { return actionValue; }

    // "qitem<number>.<material name>" when the prototype has a material ("uq" for a material out of range), else
    // "qitem<number>" (-1 without a prototype).
    std::string FigureName() override;
    std::string PictureName() override;
    bool CopyTo(Item* target, SpellManager* spells) override;
    // Also the scrolls'. The base's, then actionValue.
    void InitFromRecord(const itemdb::ItemRecordHead* record) override;
    bool IsSameAs(Item* other) override;
    // Also the scrolls'. Item's, then actionValue.
    void Load(ByteStream* stream) override;
    void Save(ByteStream* stream) override;

    std::uint32_t actionValue = 0;      // the record's actionValue (UnitServer::ScaledItemValue)
};

class ItemQuest : public Item {
public:
    const char* ClassName() override;
    ClassId GetClassId() override;
    std::uint32_t QuestWord() const override { return questItemId; }

    std::string FigureName() override;    // "quitem<number>"
    std::string PictureName() override;   // "initqu<number>item"
    bool CopyTo(Item* target, SpellManager* spells) override;
    void InitFromRecord(const itemdb::ItemRecordHead* record) override;
    bool IsSameAs(Item* other) override;
    void Load(ByteStream* stream) override;
    void Save(ByteStream* stream) override;

    // the quest item's id (the prototype's category): RemoveQuestItem(unit, id), the
    // player's quest items, the zone's wanted list.
    std::uint32_t questItemId = 0;
};

// A scroll, a spell container: a quick item with its own names, class name and id.
class ItemScroll : public ItemQuick {
public:
    const char* ClassName() override;
    ClassId GetClassId() override;

    // "spell<the carried spell's prototype icon>"; empty without the spell or its record.
    std::string FigureName() override;
    // "initqi1item".
    std::string PictureName() override;
};

class ItemLoot : public Item {
public:
    const char* ClassName() override;
    ClassId GetClassId() override;
    std::uint32_t QuestWord() const override { return static_cast<std::uint32_t>(kind); }

    // By kind: a record's name ("<type>_<number>.<variant>" for weapons and armors, "qitem", "prototype"
    // (spells, by icon), "modifier" + number), "material<index>", or "litem<number>" for the loot itself.
    std::string FigureName() override;
    // "initwe..." / "initar..." / "initqi..." for records, "initqi1item" for spells and modifiers,
    // "initlimt<index>item" for materials, "initlitr<number>item" for the loot itself.
    std::string PictureName() override;
    // The base price. Depends on what the loot stands for (kind): itself (price), a material, or a record
    // of another database (recordType).
    float GetPrice() override;
    // The record the loot stands for exists: always for itself (its prototype), the material for a
    // LootKind::Material, the record type's record for a LootKind::Record; false for another kind. One that
    // names a record past its table (a save or player of another database, a script's unknown name) is plain
    // loot to the camp's screens and the item info, and has no texts.
    bool HasRecord() const;
    bool CopyTo(Item* target, SpellManager* spells) override;
    // The base's, then kind, recordType and index.
    void InitFromRecord(const itemdb::ItemRecordHead* record) override;
    bool IsSameAs(Item* other) override;
    // Item's, then kind, recordType and index.
    void Load(ByteStream* stream) override;
    void Save(ByteStream* stream) override;

    LootKind kind = {};
    LootRecordType recordType = {};  // for LootKind::Record
    std::uint32_t index = 0;    // record index
};

inline ItemWeapon* Item::AsWeapon() { return Kind() == ItemKind::Weapon ? static_cast<ItemWeapon*>(this) : nullptr; }
inline const ItemWeapon* Item::AsWeapon() const {
    return Kind() == ItemKind::Weapon ? static_cast<const ItemWeapon*>(this) : nullptr;
}
inline ItemArmor* Item::AsArmor() { return Kind() == ItemKind::Armor ? static_cast<ItemArmor*>(this) : nullptr; }
inline const ItemArmor* Item::AsArmor() const {
    return Kind() == ItemKind::Armor ? static_cast<const ItemArmor*>(this) : nullptr;
}
inline ItemLoot* Item::AsLoot() { return Kind() == ItemKind::Loot ? static_cast<ItemLoot*>(this) : nullptr; }
inline const ItemLoot* Item::AsLoot() const {
    return Kind() == ItemKind::Loot ? static_cast<const ItemLoot*>(this) : nullptr;
}
inline ItemQuick* Item::AsQuick() {
    ItemKind kind = Kind();
    return kind == ItemKind::Quick || kind == ItemKind::Scroll ? static_cast<ItemQuick*>(this) : nullptr;
}
inline ItemQuest* Item::AsQuest() { return Kind() == ItemKind::Quest ? static_cast<ItemQuest*>(this) : nullptr; }

// Weapon stats from a packed id. An out-of-range prototype or material gives 0 (the type NoWeapon, the damage
// type 6); the original faulted.
WeaponType WeaponTypeOf(std::uint32_t packedId);
float WeaponMinDamage(std::uint32_t packedId);
float WeaponMaxDamage(std::uint32_t packedId);
// The prototype's weight times the material's weightFactor; the unit's attack info shows it as the speed.
float WeaponWeight(std::uint32_t packedId);
int WeaponDamageType(std::uint32_t packedId);    // first non-zero weight, else 6
float WeaponAttackBonus(std::uint32_t packedId);
float WeaponDefenceBonus(std::uint32_t packedId);

// The armour prototype's layer by prototype index; 0 if out of range.
int ArmorLayer(std::uint32_t index);
