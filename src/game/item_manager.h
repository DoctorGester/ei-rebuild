#pragma once

// Making items: the item constructor (the world and every party own one) and the item manager, the item factory.
// The items themselves are item.h.

#include "item.h"
#include "resource_manager.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class CSpell;
class CSpellManager;

// What an item built from `prototype` and `material` can hold: the level of spell (the prototype's magicLevel
// plus the material's, unsigned, as a float) and the power (the prototype's charge plus the material's
// magicPower); without a material the prototype's values alone.
struct MagicCapacity {
    float level;
    float power;
};
// None for a weapon or armour (`kind`) without a material.
std::optional<MagicCapacity> MagicCapacityOf(ItemKind kind, const itemdb::ItemRecordHead* prototype,
                                             const itemdb::MaterialRecordHead* material);

// The item constructor (the world and every party own one). BuildItem makes an item in one call; the steps it
// runs are public for the trader stock, which creates the item before it draws the material (a cancelled draw
// still uses up an object id): BeginCreateItem (the manager creates the empty item object), the records and
// spell, then EndCreateItem, which fills the item from a record combining them (FillItem).
class CItemConstructor : public CEngineObject {
public:
    ~CItemConstructor() override;
    const char* ClassName() override;
    ClassId GetClassId() override;

    // The reset: the manager forgotten (its count dropped), the item released, the records and spell cleared.
    void Create() override;

    void Destroy() override;

    // An item of `kind` from `manager`, filled from `prototype` and `material` (FillItem), with `spell`
    // attached and `count` as its count. Null when the manager cannot create the item (after a fatal error the
    // game carries on from) or the build fails.
    CItem* BuildItem(CBaseManager* manager, ItemKind kind, itemdb::ItemRecordHead* prototype,
                     itemdb::MaterialRecordHead* material = nullptr, CSpell* spell = nullptr, int count = 1);

    // Counts a reference to `manager`, dropping the previous one's (no release).
    void SetManager(CBaseManager* manager);

    // Creates the item through the manager (with `kind`, no id asked for): counted, records cleared, count 1, flag 8
    // set. False after a fatal error the game carries on from (Rebirth's DisableExitOnCrashReport).
    bool BeginCreateItem(ItemKind kind);

    // Builds the item. On failure releases it and returns null; otherwise hands it over
    // (its count dropped without release, flag 8 cleared) and returns it.
    CItem* EndCreateItem();

    // Releases the item; records and spell cleared.
    void Cancel();

    CBaseManager* manager_ = nullptr;   // the item manager; counted but never released
    itemdb::MaterialRecordHead* material_ = nullptr;
    itemdb::ItemRecordHead* prototype_ = nullptr;
    Ref<CItem> item_;
    CSpell* spell_ = nullptr;           // not counted
    int count_;                         // copied to the item's count_; set by BeginCreateItem

private:
    // Fills the item from a temporary database record: the prototype's values combined with the material's,
    // then the item's InitFromRecord; attaches the prototype's spell (potions) or the given one; sets the
    // packed id and the count. False for a weapon or armor without a material.
    bool FillItem();
};

// The item manager (the world and every party make one); context_ holds its item constructor and its spell
// manager.
struct CItemManager : CResourceManager {
    CItemManager();
    ~CItemManager() override;
    const char* ClassName() override;
    ClassId GetClassId() override;
    const char* GetFolderName() override;    // "items"

    // The item factory: a new constructed item for kind ItemKind::Weapon weapon, ItemKind::Armor armour, ItemKind::Quick quick, ItemKind::Loot
    // loot, ItemKind::Spell scroll, ItemKind::Quest quest, its type_ set to the kind; null for another kind or without memory.
    CEngineObject* NewObject(int kind) override;

    // An item of `kind` built by the context's item constructor (BeginCreateItem with `kind`)
    // from prototype record `index` of the kind's table (an index whose low 16 bits are 0xFFFF: 0) and, for
    // weapons and armor, material record `material`; an index out of range or another kind gives a null
    // record. Returns the item (null when the build fails).
    CItem* MakeItem(ItemKind kind, std::uint32_t index, std::uint32_t material);

    // An item from its name, "prototype[.material][[extra]]" (lowercased): "spell container[spell]" makes a
    // scroll; otherwise the prototype is looked up in the weapon, armor, quick, quest and loot tables (none:
    // FatalError, null) and built with the material, the bracket part as a spell (or, for prototypes named
    // "material", as the count); a loot item standing for a record gets the table index of the second name
    // part as index_. A failed build reports FatalError and the game crashes; Rebirth's
    // FixCreateItem returns null in both cases without FatalError.
    CItem* CreateItem(std::string_view name);

    // A scroll ("spell container", kind ItemKind::Spell) holding the spell that `spells` builds from `spellText`;
    // null when the spell or the item cannot be made (the spell is then left as it is).
    CItem* CreateSpellItem(std::string_view spellText, CSpellManager* spells);

    // The table's record names in table order: the materials' keys, the others' name.
    std::vector<std::string> ListMaterialNames();
    std::vector<std::string> ListWeaponNames();
    std::vector<std::string> ListArmorNames();
    std::vector<std::string> ListQuickItemNames();
};
