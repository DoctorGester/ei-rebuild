// Item construction: CItemConstructor and the item manager (its factory and its ways of making items).
// BuildItem has the manager create the empty item object (BeginCreateItem), then EndCreateItem fills it from a
// temporary database record: the prototype's values combined with the material's. See item_manager.h.
#include "item_manager.h"

#include "console.h"
#include "item.h"
#include "manager_context.h"
#include "spell.h"
#include "text_split.h"

#include "lib/str.h"

#include "ei/rebirth.h"
#include "ei/x87.h"

#include <cstdint>
#include <memory>
#include <string>

using namespace itemdb;

namespace {

constexpr std::uint32_t kFlagConstructing = 8;

const char* const kCantCreateItem = "CItemConstructor::BeginCreateItem.\n";

// --- The temporary database record ---------------------------------------------------------------
// FillItem creates a record of the item's kind, fills only the fields the item's InitFromRecord reads, and
// destroys it again. The prototype and material records are read without a null check where the game reads them
// without one (a missing record faults there).

// The temporary record for an item of `kind`; null for other kinds.
std::unique_ptr<ItemRecordHead> NewRecord(ItemKind kind) {
    switch (kind) {
    case ItemKind::Weapon: return std::make_unique<WeaponRecord>();
    case ItemKind::Armor: return std::make_unique<ArmorRecord>();
    case ItemKind::Quick:
    case ItemKind::Scroll: return std::make_unique<QuickItemRecord>();
    case ItemKind::Quest: return std::make_unique<QuestItemRecord>();
    case ItemKind::Loot: return std::make_unique<LootItemRecord>();
    default: return nullptr;
    }
}

template <typename Record>
std::uint32_t IndexIn(const NamedRecordTable<Record>& table, const ItemRecordHead* record) {
    return static_cast<std::uint32_t>(table.IndexOf(RecordAs<Record>(record)));
}

// FillItem's common part: price, values and charge of the record, from the prototype alone or scaled by the
// material.
void FillCommon(ItemRecordHead* record, const ItemRecordHead* prototype, const MaterialRecordHead* material) {
    if (!material) {
        record->price = prototype->price;
        record->weight = prototype->weight;
        record->unreadValue = prototype->unreadValue;
        record->charge = prototype->charge;
        record->condition = prototype->condition;
        record->magicLevel = prototype->magicLevel;
        record->materialUnits = prototype->materialUnits;
        return;
    }
    // Price: the prototype's material units (unsigned, through a double) times the material's unit price, plus
    // the prototype's price.
    double units = x87::FromUnsigned(prototype->materialUnits);
    record->price = static_cast<float>(units * material->unitPrice + prototype->price);
    record->weight = prototype->weight * material->weightFactor;
    record->unreadValue = prototype->unreadValue;
    record->charge = prototype->charge + material->magicPower;
    record->condition = prototype->condition * material->conditionFactor;
    record->magicLevel = prototype->magicLevel + material->magicLevel;
    record->materialUnits = prototype->materialUnits;
}

// FillItem's per-kind part; returns the prototype's index in its table.
std::uint32_t FillKind(ItemKind kind, ItemRecordHead* record, const ItemRecordHead* prototype,
                       const MaterialRecordHead* material) {
    switch (kind) {
    case ItemKind::Weapon: {
        auto* to = RecordAs<WeaponRecord>(record);
        auto* from = RecordAs<WeaponRecord>(prototype);
        to->reach = from->reach;
        to->damage = from->damage * material->damageFactor;
        to->damageSpread = from->damageSpread * material->damageFactor;
        return IndexIn(g_itemDatabase.weapons, prototype);
    }
    case ItemKind::Armor: {
        auto* to = RecordAs<ArmorRecord>(record);
        auto* from = RecordAs<ArmorRecord>(prototype);
        to->firstFlags = from->firstFlags | material->armourFlags;
        to->othersFlags = from->othersFlags | material->armourFlags;
        // Each absorption value scaled by the material's factor for its damage type and by the row's factor.
        for (int k = 0; k < kDamageTypeCount; ++k) {
            to->absorbFirst[k] = from->absorbFirst[k] * material->absorbFactors[k] * from->FirstFactor();
            to->absorbOthers[k] = from->absorbOthers[k] * material->absorbFactors[k] * from->othersFactor;
        }
        return IndexIn(g_itemDatabase.armors, prototype);
    }
    case ItemKind::Quick:
    case ItemKind::Scroll:
        RecordAs<QuickItemRecord>(record)->actionValue = RecordAs<QuickItemRecord>(prototype)->actionValue;
        return IndexIn(g_itemDatabase.quickItems, prototype);
    case ItemKind::Quest:
        record->category = prototype->category;
        return IndexIn(g_itemDatabase.questItems, prototype);
    case ItemKind::Loot: {
        auto* to = RecordAs<LootItemRecord>(record);
        auto* from = RecordAs<LootItemRecord>(prototype);
        to->category = from->category;
        to->recordType = from->recordType;
        // A material takes the material's index when one is given.
        if (from->Stands() == LootKind::Material && material)
            to->index = material->number;
        else
            to->index = from->index;
        return IndexIn(g_itemDatabase.loot, prototype);
    }
    default:
        return 0;   // not reached: the record of an unknown kind is null and faulted above
    }
}

bool NeedsMaterial(ItemKind kind) { return kind == ItemKind::Weapon || kind == ItemKind::Armor; }

const char* const kSpellContainer = "spell container";
const char* const kMaterialType = "material";
const char* const kNoPrototype = "Can't find prototype for item \"%s\".\n";
const char* const kCantCreate = "Can't create item \"%s\".\nCheck shape and material names\n";

// Index of `record` in `table`, -1 for no record.
template <typename Table, typename Record>
int TableIndex(const Table& table, const Record* record) {
    return record ? table.IndexOf(record) : -1;
}

// The table index of a loot item's second name part (an unknown name is filed in the table's map as null).
int LootTableIndex(LootRecordType which, const std::string& name) {
    ItemDatabase& items = g_itemDatabase;
    SpellDatabase& spells = g_spellDatabase;
    switch (which) {
    case LootRecordType::Weapon: return TableIndex(items.weapons, items.weapons.names[name]);
    case LootRecordType::Armor: return TableIndex(items.armors, items.armors.names[name]);
    case LootRecordType::Quick: return TableIndex(items.quickItems, items.quickItems.names[name]);
    case LootRecordType::Spell: return TableIndex(spells.spells, spells.spells.byCode[name]);
    case LootRecordType::Modifier: return TableIndex(spells.modifiers, spells.modifiers.byCode[name]);
    default: return 0;
    }
}

template <typename Record>
std::vector<std::string> RecordNames(const NamedRecordTable<Record>& table) {
    std::vector<std::string> names;
    names.reserve(table.records.size());
    for (const Record& record : table.records) names.push_back(record.name);
    return names;
}

} // namespace

std::optional<MagicCapacity> MagicCapacityOf(ItemKind kind, const ItemRecordHead* prototype,
                                             const MaterialRecordHead* material) {
    if (!material) {
        if (NeedsMaterial(kind)) return std::nullopt;
        return MagicCapacity{static_cast<float>(x87::FromUnsigned(prototype->magicLevel)), prototype->charge};
    }
    return MagicCapacity{static_cast<float>(x87::FromUnsigned(prototype->magicLevel + material->magicLevel)),
                         prototype->charge + material->magicPower};
}

// --- CItemConstructor ----------------------------------------------------------------------------

void CItemConstructor::Create() {
    CItemConstructor::Destroy();
}

void CItemConstructor::Destroy() {
    if (manager) manager->ForgetReference();
    manager = nullptr;
    item.Reset();
    material = nullptr;
    prototype = nullptr;
    spell = nullptr;
}

CItemConstructor::~CItemConstructor() { CItemConstructor::Destroy(); }

const char* CItemConstructor::ClassName() { return "CItemConstructor"; }
ClassId CItemConstructor::GetClassId() { return ClassId::ItemConstructor; }

Item* CItemConstructor::BuildItem(BaseManager* objectManager, ItemKind kind, ItemRecordHead* record,
                                   MaterialRecordHead* materialRecord, CSpell* itemSpell, int itemCount) {
    SetManager(objectManager);
    if (!BeginCreateItem(kind)) {
        SetManager(nullptr);
        return nullptr;
    }
    prototype = record;
    material = materialRecord;
    spell = itemSpell;
    count = itemCount;
    Item* newItem = EndCreateItem();
    SetManager(nullptr);
    return newItem;
}

void CItemConstructor::SetManager(BaseManager* newManager) {
    if (manager) manager->ForgetReference();
    manager = newManager;
    if (newManager) newManager->AddRef();
}

bool CItemConstructor::BeginCreateItem(ItemKind kind) {
    Item* newItem = manager->CreateObjectAs<Item>(kind, kNoObjectId);
    if (!newItem) return game::FatalError(kCantCreateItem);
    item.Assign(newItem);
    material = nullptr;
    prototype = nullptr;
    count = 1;
    flags |= kFlagConstructing;
    return true;
}

Item* CItemConstructor::EndCreateItem() {
    if (!FillItem()) {
        item.Reset();
        return nullptr;
    }
    flags &= ~kFlagConstructing;
    Item* newItem = item.Take();
    newItem->ForgetReference();
    spell = nullptr;
    count = 0;
    return newItem;
}

void CItemConstructor::Cancel() {
    item.Reset();
    spell = nullptr;
    material = nullptr;
    prototype = nullptr;
}

bool CItemConstructor::FillItem() {
    ItemKind kind = item->Kind();
    if (NeedsMaterial(kind) && !material) return false;

    std::unique_ptr<ItemRecordHead> record = NewRecord(kind);
    FillCommon(record.get(), prototype, material);
    std::uint32_t index = FillKind(kind, record.get(), prototype, material);

    item->InitFromRecord(record.get());
    if (kind == ItemKind::Quick && static_cast<QuickItemCategory>(prototype->category) == QuickItemCategory::Potion) {
        // A potion casts the spell its prototype's text makes (a given spell is not attached).
        CSpellManager* spellManager = manager->context->SpellManager();
        CSpell* newSpell = spellManager->CreateSpellFromText(RecordAs<QuickItemRecord>(prototype)->spellText);
        spell = newSpell;
        item->spell.Assign(newSpell);
    } else if (spell) {
        item->AttachSpell(spell);
    }

    item->prototypeId = PackItemId(index, material ? material->number : 0);
    item->count = static_cast<std::uint32_t>(count);
    return true;
}

// --- The item manager's ways of making items -------------------------------------------------------

Item* ItemManager::CreateItem(std::string_view name) {
    std::string extra;               // the part in brackets: "name[extra]"
    std::string text(name);
    int bracket = str::Find(text, '[');
    if (bracket != -1) {
        extra = str::Right(text, static_cast<int>(text.size()) - bracket - 1);
        str::TrimLeft(extra, '[');
        str::TrimRight(extra, ']');
        str::TrimLeft(extra);
        str::TrimRight(extra);
        text = str::Left(text, bracket);
        str::TrimRight(text);
    }
    std::vector<std::string> parts;      // the name split at '.'
    parts = text::Split(text, '.');
    for (std::string& part : parts) str::MakeLower(part);

    // "spell container[spell text]": a scroll holding that spell.
    if (parts[0] == kSpellContainer) return CreateSpellItem(extra, context->SpellManager());

    // "prototype[.material]".
    const std::string& prototypeName = parts[0];
    ItemDatabase& items = g_itemDatabase;
    ItemRecordHead* prototype = items.weapons.Named(prototypeName);
    if (!prototype) prototype = items.armors.Named(prototypeName);
    if (!prototype) prototype = items.quickItems.Named(prototypeName);
    if (!prototype) prototype = items.questItems.Named(prototypeName);
    if (!prototype) prototype = items.loot.Named(prototypeName);
    if (!prototype) {
        // Rebirth's FixCreateItem logs instead of the fatal error.
        if (!ei::rebirth::Enabled(ei::rebirth::Patch::FixCreateItem))
            game::FatalError(kNoPrototype, parts[0]);
        return nullptr;
    }
    MaterialRecordHead* material = parts.size() > 1 ? items.materials.Named(parts[1]) : nullptr;
    // The bracket part: the count for materials, a spell for the other prototypes.
    CSpell* spell = nullptr;
    int count = 1;
    if (!extra.empty()) {
        if (prototype->name == kMaterialType)
            count = str::ToInt(extra);
        else
            spell = context->SpellManager()->CreateSpellFromText(extra);
    }
    Item* item = context->ItemConstructor()->BuildItem(this, prototype->Kind(), prototype, material, spell, count);
    if (!item) {
        // Rebirth's FixCreateItem: logged, and no item. Without it the error is reported and the code goes on
        // with the null item (and crashes).
        if (ei::rebirth::Enabled(ei::rebirth::Patch::FixCreateItem)) return nullptr;
        game::FatalError(kCantCreate, std::string(name));
    }
    if (item->GetClassId() == ClassId::ItemLoot && parts.size() > 1) {
        auto* loot = AsLoot(item);
        if (loot->kind == LootKind::Record) loot->index = static_cast<std::uint32_t>(LootTableIndex(loot->recordType, parts[1]));
    }
    return item;
}

Item* ItemManager::MakeItem(ItemKind kind, std::uint32_t index, std::uint32_t material) {
    if ((index & 0xFFFF) == kNoIndex) index = 0;
    MaterialRecordHead* materialRecord = NeedsMaterial(kind) ? MaterialAt(material) : nullptr;
    return context->ItemConstructor()->BuildItem(this, kind, PrototypeAt(kind, index), materialRecord);
}

// The "spell container" record is looked up by name each time; without one the index is that of a
// null record (kept: MakeItem then finds no prototype).
Item* ItemManager::CreateSpellItem(std::string_view spellText, CSpellManager* spells) {
    CSpell* spell = spells->CreateSpellFromText(spellText);
    if (!spell) return nullptr;
    QuickItemTable& quickItems = g_itemDatabase.quickItems;
    int index = quickItems.IndexOf(quickItems.Named(kSpellContainer));
    Item* item = MakeItem(ItemKind::Scroll, index, 0);
    if (item) item->AttachSpell(spell);
    return item;
}

std::vector<std::string> ItemManager::ListMaterialNames() {
    std::vector<std::string> names;
    names.reserve(g_itemDatabase.materials.records.size());
    for (const MaterialRecordHead& record : g_itemDatabase.materials.records) names.push_back(record.key);
    return names;
}
std::vector<std::string> ItemManager::ListWeaponNames() { return RecordNames(g_itemDatabase.weapons); }
std::vector<std::string> ItemManager::ListArmorNames() { return RecordNames(g_itemDatabase.armors); }
std::vector<std::string> ItemManager::ListQuickItemNames() { return RecordNames(g_itemDatabase.quickItems); }

// --- The item manager's factory ------------------------------------------------------------------

// Default-initialised: the subclasses' own fields are set by InitFromRecord or Load.
EngineObject* ItemManager::NewObject(int kind) {
    EngineObject* object;
    switch (static_cast<ItemKind>(kind)) {
    case ItemKind::Weapon: object = new ItemWeapon; break;
    case ItemKind::Armor: object = new ItemArmor; break;
    case ItemKind::Quick: object = new ItemQuick; break;
    case ItemKind::Loot: object = new ItemLoot; break;
    case ItemKind::Scroll: object = new ItemScroll; break;
    case ItemKind::Quest: object = new ItemQuest; break;
    default: return nullptr;
    }
    object->type = kind;
    return object;
}
