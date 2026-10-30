// The camp's debug fill of the constructors' lists. See ui_camp.h and
// docs/notes/ui.md, section 5.
#include "item_manager.h"
#include "spell.h"
#include "ui_camp.h"

#include "console.h"
#include "item.h"
#include "item_db.h"
#include "party.h"

#include "lib/text_case.h"
#include "resource_manager.h"
#include "world.h"
#include "lib/str.h"

#include <cstdint>
#include <string>

namespace {

constexpr std::uint32_t kInstructionCount = 10;
constexpr std::uint32_t kMaterialCount = 20;

// The list's copy of `item`: counted for the call, added (keeping its count, merged), released.
void AddCounted(Party* list, Item* item) {
    item->AddRef();
    list->AddItem(item, 1, 1);
    item->Release();
}

// An item named `name` from the list's item manager (not checked for null) with
// record index `index` and count `count`, added.
void AddNamed(Party* list, const char* name, std::uint32_t index, std::uint32_t count) {
    auto* item = AsLoot(list->ItemFactory()->CreateItem(name));
    item->index = index;
    item->count = count;
    AddCounted(list, item);
}

// One item of class `kind` for `record` in every material whose soundName names the record's material class,
// made by the list's item constructor with `manager`. A material index out of the table reads through null.
void AddInMaterials(Party* list, ResourceManager* manager, ItemConstructor* maker, itemdb::ItemRecordHead* record,
                    ItemKind kind, int materials) {
    for (int j = 0; j < materials; ++j) {
        if (text::CompareIgnoringCase(itemdb::MaterialAt(static_cast<std::uint32_t>(j))->soundName, record->materialClass) != 0)
            continue;
        AddCounted(list, maker->BuildItem(manager, kind, record, itemdb::MaterialAt(static_cast<std::uint32_t>(j))));
    }
}

// The record's name contains `word`.
bool NameHas(const itemdb::ItemRecordHead* record, const char* word) {
    return str::Find(record->name, word) >= 0;
}

} // namespace

void CampPages::DebugFill() {
    std::vector<std::string> names;
    std::vector<std::string> materials = g_clientWorld.itemManager->ListMaterialNames();
    const int materialCount = static_cast<int>(materials.size());

    if (Party* items = itemParts[0]) {
        items->Clear(1, 0);

        names = g_clientWorld.itemManager->ListWeaponNames();
        for (int i = 0; i < static_cast<int>(names.size()); ++i) {
            AddNamed(items, "Instruction Weapon", static_cast<std::uint32_t>(i), kInstructionCount);
            ResourceManager* manager = items->ItemFactory();
            ItemConstructor* maker = items->GetItemConstructor();
            itemdb::ItemRecordHead* weapon = itemdb::WeaponAt(static_cast<std::uint32_t>(i));
            AddInMaterials(items, manager, maker, weapon, ItemKind::Weapon, materialCount);
        }

        names = g_clientWorld.itemManager->ListArmorNames();
        for (int i = 0; i < static_cast<int>(names.size()); ++i) {
            ResourceManager* manager = items->ItemFactory();
            itemdb::ItemRecordHead* armor = itemdb::ArmorAt(static_cast<std::uint32_t>(i));
            if (NameHas(armor, "prototype")) continue;
            AddNamed(items, "Instruction Armor", static_cast<std::uint32_t>(i), kInstructionCount);
            ItemConstructor* maker = items->GetItemConstructor();
            AddInMaterials(items, manager, maker, armor, ItemKind::Armor, materialCount);
        }

        names = g_clientWorld.itemManager->ListQuickItemNames();
        for (int i = 0; i < static_cast<int>(names.size()); ++i) {
            itemdb::ItemRecordHead* quick = itemdb::QuickItemAt(static_cast<std::uint32_t>(i));
            if (NameHas(quick, "wand"))
                AddNamed(items, "Instruction Quick Item", static_cast<std::uint32_t>(i), kInstructionCount);
        }

        // "Material.<name>", twenty each, the record index left as made.
        names = g_clientWorld.itemManager->ListMaterialNames();
        for (const std::string& material : names) {
            auto* item = items->ItemFactory()->CreateItem("Material." + material);
            item->count = kMaterialCount;
            AddCounted(items, item);
        }
    }

    if (Party* spells = spellParts[0]) {
        spells->Clear(1, 0);
        names = g_clientWorld.spellManager->ListModifierNames();
        for (int i = 0; i < static_cast<int>(names.size()); ++i)
            AddNamed(spells, "Rune", static_cast<std::uint32_t>(i), kInstructionCount);
        names = g_clientWorld.spellManager->ListSpellNames();
        for (int i = 0; i < static_cast<int>(names.size()); ++i)
            AddNamed(spells, "Prototype", static_cast<std::uint32_t>(i), kInstructionCount);
    }
}
