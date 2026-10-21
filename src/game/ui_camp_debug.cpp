// The camp's debug fill of the constructors' lists (00612CF0, slot 39). See ui_camp.h and
// docs/notes/ui.md, section 5.
#include "spell.h"
#include "ui_camp.h"
#include "game_strings.h"

#include "console.h"
#include "item.h"
#include "item_db.h"
#include "party.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "originals_gameplay.h"
#include "originals_library.h"
#include "resource_manager.h"

#include <cstdint>

namespace {

const char* InstructionWeapon() { return "Instruction Weapon"; }
const char* InstructionArmor() { return "Instruction Armor"; }
const char* InstructionQuickItem() { return "Instruction Quick Item"; }
const char* PrototypeWord() { return "prototype"; }
const char* WandWord() { return "wand"; }
const char* MaterialWord() { return "Material"; }
const char* Dot() { return "."; }
const char* RuneItem() { return "Rune"; }
const char* PrototypeItem() { return "Prototype"; }

// The item and spell databases (their name lists, originals_gameplay.h).
void* ItemDatabase() { return *reinterpret_cast<void* const*>(0x007AF60C); }
void* SpellDatabase() { return *reinterpret_cast<void* const*>(0x007AF604); }

constexpr int kWeaponClass = 0x3004;
constexpr int kArmorClass = 0x3005;
constexpr std::uint32_t kInstructionCount = 10;
constexpr std::uint32_t kMaterialCount = 20;

// The list's copy of `item`: counted for the call, added (keeping its count, merged), released.
void AddCounted(CParty* list, CItem* item) {
    ++item->refCount_;
    list->AddItem(item, 1, 1);
    item->Release();
}

// An item named `name` from the list's item manager (not checked for null, as the original) with
// record index `index` and count `count`, added.
void AddNamed(CParty* list, const char* name, std::uint32_t index, std::uint32_t count) {
    auto* item = static_cast<CItemLoot*>(ItemManagerCreateItemOriginal(list->ItemManager(), nullptr, name));
    item->index_ = index;
    item->count_ = count;
    AddCounted(list, item);
}

// One item of class `kind` for `record` in every material whose +0x04 names the record's material
// (+0x10), made by the list's item constructor with `manager`. A material index out of the table
// reads through null, as in the original.
void AddInMaterials(CParty* list, CResourceManager* manager, CItemConstructor* maker, itemdb::ItemRecordHead* record,
                    int kind, int materials) {
    for (int j = 0; j < materials; ++j) {
        const char* wanted = record->materialClass;
        char* material = itemdb::Materials().RecordAt(static_cast<std::uint32_t>(j), itemdb::kMaterialSize);
        const char* name = reinterpret_cast<const itemdb::MaterialRecordHead*>(material)->soundName;
        if (libcrt::Mbsicmp(reinterpret_cast<const unsigned char*>(name), reinterpret_cast<const unsigned char*>(wanted)) != 0)
            continue;
        maker->SetManager(manager);
        maker->BeginCreateItem(kind, -1);
        maker->SetPrototype(reinterpret_cast<char*>(record));
        maker->SetMaterial(itemdb::Materials().RecordAt(static_cast<std::uint32_t>(j), itemdb::kMaterialSize));
        CItem* item = maker->EndCreateItem();
        maker->SetManager(nullptr);
        AddCounted(list, item);
    }
}

// The record's name (+0x04) contains `word`.
bool NameHas(const itemdb::ItemRecordHead* record, const char* word) {
    return CStringFindTextOriginal(&record->text04, nullptr, word) >= 0;
}
itemdb::ItemRecordHead* Record(const RecordTable& table, int index, int stride) {
    return reinterpret_cast<itemdb::ItemRecordHead*>(table.RecordAt(static_cast<std::uint32_t>(index), stride));
}

} // namespace

void CCampPages::DebugFill() {
    console::CStringArray names;
    console::CStringArray materials;
    static_cast<CItemManager*>(ItemDatabase())->ListMaterialNames(&materials);

    if (auto* items = static_cast<CParty*>(itemParts_[0])) {
        PartyResetOriginal(items, nullptr, 1, 0);

        static_cast<CItemManager*>(ItemDatabase())->ListWeaponNames(&names);
        for (int i = 0; i < names.size; ++i) {
            AddNamed(items, InstructionWeapon(), static_cast<std::uint32_t>(i), kInstructionCount);
            CResourceManager* manager = items->ItemManager();
            CItemConstructor* maker = items->ItemConstructor();
            itemdb::ItemRecordHead* weapon = Record(itemdb::Weapons(), i, itemdb::kWeaponSize);
            AddInMaterials(items, manager, maker, weapon, kWeaponClass, materials.size);
        }

        static_cast<CItemManager*>(ItemDatabase())->ListArmorNames(&names);
        for (int i = 0; i < names.size; ++i) {
            CResourceManager* manager = items->ItemManager();
            itemdb::ItemRecordHead* armor = Record(itemdb::Armors(), i, itemdb::kArmorSize);
            if (NameHas(armor, PrototypeWord())) continue;
            AddNamed(items, InstructionArmor(), static_cast<std::uint32_t>(i), kInstructionCount);
            CItemConstructor* maker = items->ItemConstructor();
            AddInMaterials(items, manager, maker, armor, kArmorClass, materials.size);
        }

        static_cast<CItemManager*>(ItemDatabase())->ListQuickItemNames(&names);
        for (int i = 0; i < names.size; ++i) {
            itemdb::ItemRecordHead* quick = Record(itemdb::QuickItems(), i, itemdb::kQuickItemSize);
            if (NameHas(quick, WandWord()))
                AddNamed(items, InstructionQuickItem(), static_cast<std::uint32_t>(i), kInstructionCount);
        }

        // "Material.<name>" (CString + "." + name, as the original's two operator+), twenty each,
        // the record index left as made.
        static_cast<CItemManager*>(ItemDatabase())->ListMaterialNames(&names);
        for (int i = 0; i < names.size; ++i) {
            mfc::CString name(MaterialWord());
            name += Dot();
            name += names.data[i];
            auto* item = static_cast<CItem*>(ItemManagerCreateItemOriginal(items->ItemManager(), nullptr, name));
            item->count_ = kMaterialCount;
            AddCounted(items, item);
        }
    }

    if (auto* spells = static_cast<CParty*>(spellParts_[0])) {
        PartyResetOriginal(spells, nullptr, 1, 0);
        static_cast<CSpellManager*>(SpellDatabase())->ListModifierNames(&names);
        for (int i = 0; i < names.size; ++i)
            AddNamed(spells, RuneItem(), static_cast<std::uint32_t>(i), kInstructionCount);
        static_cast<CSpellManager*>(SpellDatabase())->ListSpellNames(&names);
        for (int i = 0; i < names.size; ++i)
            AddNamed(spells, PrototypeItem(), static_cast<std::uint32_t>(i), kInstructionCount);
    }
}
EI_REPLACE_METHOD(0x00612CF0, CCampPages::DebugFill);
