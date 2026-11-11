// Item construction: ItemConstructor and the item manager (its factory and its ways of making items).
// BuildItem has the manager create the empty item object (BeginCreateItem), then EndCreateItem fills it from a
// temporary database record: the prototype's values combined with the material's. See item_manager.h.
#include "game/items/item_manager.h"

#include "game/console/console.h"
#include "game/items/item.h"
#include "engine/core/manager_context.h"
#include "game/objects/map_object.h"   // MapObjectFlag
#include "game/spells/spell.h"
#include "engine/core/text_split.h"
#include "game/spells/spell_db.h"

#include "lib/crt.h"
#include "lib/str.h"
#include "platform/log.h"


#include <cstdint>
#include <memory>
#include <string>

using namespace itemdb;

namespace {

const char* const kCantCreateItem = "CItemConstructor::BeginCreateItem.\n";

// --- The temporary database record ---------------------------------------------------------------
// FillItem creates a record of the item's kind, fills only the fields the item's InitFromRecord reads, and
// destroys it again. It makes no item without a prototype (the original faulted reading it).

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
    // Price: the prototype's material units times the material's unit price, plus the prototype's price.
    record->price = (material->unitPrice * prototype->materialUnits) + prototype->price;
    record->weight = prototype->weight * material->weightFactor;
    record->unreadValue = prototype->unreadValue;
    record->charge = prototype->charge + material->magicPower;
    record->condition = prototype->condition * material->conditionFactor;
    record->magicLevel = prototype->magicLevel + material->magicLevel;
    record->materialUnits = prototype->materialUnits;
}

// FillItem's per-kind part; returns the prototype's index in its table.
int FillKind(ItemKind kind, ItemRecordHead* record, const ItemRecordHead* prototype,
             const MaterialRecordHead* material) {
    switch (kind) {
    case ItemKind::Weapon: {
        auto* to = RecordAs<WeaponRecord>(record);
        auto* from = RecordAs<WeaponRecord>(prototype);
        to->reach = from->reach;
        to->damage = from->damage * material->damageFactor;
        to->damageSpread = from->damageSpread * material->damageFactor;
        return g_itemDatabase.weapons.IndexOf(from);
    }
    case ItemKind::Armor: {
        auto* to = RecordAs<ArmorRecord>(record);
        auto* from = RecordAs<ArmorRecord>(prototype);
        to->firstFlags = from->firstFlags | material->armourFlags;
        to->othersFlags = from->othersFlags | material->armourFlags;
        // Each absorption value scaled by the material's factor for its damage type and by the row's factor.
        for (int k = 0; k < kDamageTypeCount; ++k) {
            to->absorbFirst[k] = from->absorbFirst[k] * material->absorbFactors[k] * from->firstFactor;
            to->absorbOthers[k] = from->absorbOthers[k] * material->absorbFactors[k] * from->othersFactor;
        }
        return g_itemDatabase.armors.IndexOf(from);
    }
    case ItemKind::Quick:
    case ItemKind::Scroll: {
        auto* from = RecordAs<QuickItemRecord>(prototype);
        RecordAs<QuickItemRecord>(record)->actionValue = from->actionValue;
        return g_itemDatabase.quickItems.IndexOf(from);
    }
    case ItemKind::Quest:
        record->category = prototype->category;
        return g_itemDatabase.questItems.IndexOf(RecordAs<QuestItemRecord>(prototype));
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
        return g_itemDatabase.loot.IndexOf(from);
    }
    default:
        return 0;   // not reached: FillItem makes no item of an unknown kind
    }
}

bool NeedsMaterial(ItemKind kind) { return kind == ItemKind::Weapon || kind == ItemKind::Armor; }

// The index of `name` in `table`: -1 for an unknown name (which the lookup files in the table's map as null).
template <typename Record>
int IndexNamed(RecordTable<Record>& table, std::string_view name) {
    return table.IndexOf(table.Named(name));
}

// The table index of a loot item's second name part.
int LootTableIndex(LootRecordType which, const std::string& name) {
    switch (which) {
    case LootRecordType::Weapon: return IndexNamed(g_itemDatabase.weapons, name);
    case LootRecordType::Armor: return IndexNamed(g_itemDatabase.armors, name);
    case LootRecordType::Quick: return IndexNamed(g_itemDatabase.quickItems, name);
    case LootRecordType::Spell: return IndexNamed(g_spellDatabase.spells, name);
    case LootRecordType::Modifier: return IndexNamed(g_spellDatabase.modifiers, name);
    default: return 0;
    }
}

// Each record's IndexKey, in table order.
template <typename Record>
std::vector<std::string> RecordNames(const RecordTable<Record>& table) {
    std::vector<std::string> names;
    names.reserve(table.records.size());
    for (const Record& record : table.records) names.emplace_back(IndexKey(record));
    return names;
}

} // namespace

std::optional<MagicCapacity> MagicCapacityOf(ItemKind kind, const ItemRecordHead* prototype,
                                             const MaterialRecordHead* material) {
    if (!material) {
        if (NeedsMaterial(kind)) return std::nullopt;
        return MagicCapacity{static_cast<float>(prototype->magicLevel), prototype->charge};
    }
    return MagicCapacity{static_cast<float>(prototype->magicLevel + material->magicLevel),
                         prototype->charge + material->magicPower};
}

// --- ItemConstructor ----------------------------------------------------------------------------

void ItemConstructor::Create() {
    ItemConstructor::Destroy();
}

void ItemConstructor::Destroy() {
    if (manager) manager->ForgetReference();
    manager = nullptr;
    item.Reset();
    material = nullptr;
    prototype = nullptr;
    spell = nullptr;
}

ItemConstructor::~ItemConstructor() { ItemConstructor::Destroy(); }

const char* ItemConstructor::ClassName() { return "CItemConstructor"; }
ClassId ItemConstructor::GetClassId() { return ClassId::ItemConstructor; }

Item* ItemConstructor::BuildItem(BaseManager* objectManager, ItemKind kind, ItemRecordHead* record,
                                   MaterialRecordHead* materialRecord, Spell* itemSpell, int itemCount) {
    SetManager(objectManager);
    BeginCreateItem(kind);
    prototype = record;
    material = materialRecord;
    spell = itemSpell;
    count = itemCount;
    Item* newItem = EndCreateItem();
    SetManager(nullptr);
    return newItem;
}

void ItemConstructor::SetManager(BaseManager* newManager) {
    if (manager) manager->ForgetReference();
    manager = newManager;
    if (newManager) newManager->AddRef();
}

void ItemConstructor::BeginCreateItem(ItemKind kind) {
    Item* newItem = manager->CreateObjectAs<Item>(kind, kNoObjectId);
    if (!newItem) game::FatalExit("%s", kCantCreateItem);
    item.Assign(newItem);
    material = nullptr;
    prototype = nullptr;
    count = 1;
    flags |= MapObjectFlag::Constructing;
}

Item* ItemConstructor::EndCreateItem() {
    if (!FillItem()) {
        item.Reset();
        return nullptr;
    }
    flags &= ~MapObjectFlag::Constructing;
    Item* newItem = item.Take();
    newItem->ForgetReference();
    spell = nullptr;
    count = 0;
    return newItem;
}

void ItemConstructor::Cancel() {
    item.Reset();
    spell = nullptr;
    material = nullptr;
    prototype = nullptr;
}

bool ItemConstructor::FillItem() {
    ItemKind kind = item->Kind();
    std::unique_ptr<ItemRecordHead> record = NewRecord(kind);
    if (!prototype || !record || (NeedsMaterial(kind) && !material)) return false;

    FillCommon(record.get(), prototype, material);
    int index = FillKind(kind, record.get(), prototype, material);

    item->InitFromRecord(record.get());
    if (kind == ItemKind::Quick && static_cast<QuickItemCategory>(prototype->category) == QuickItemCategory::Potion) {
        // A potion casts the spell its prototype's text makes (a given spell is not attached).
        SpellManager* spellManager = manager->context->GetSpellManager();
        Spell* newSpell = spellManager->CreateSpellFromText(RecordAs<QuickItemRecord>(prototype)->spellText);
        spell = newSpell;
        item->spell.Assign(newSpell);
    } else if (spell) {
        item->AttachSpell(spell);
    }

    item->prototypeId = PackItemId(static_cast<std::uint32_t>(index), material ? material->number : 0);
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
    // The name split at '.': at least one piece, empty for an empty name.
    std::vector<std::string> parts = text::Split(text, '.');
    for (std::string& part : parts) str::MakeLower(part);

    // "spell container[spell text]": a scroll holding that spell.
    if (parts[0] == kSpellContainer) return CreateSpellItem(extra, context->GetSpellManager());

    // "prototype[.material]".
    const std::string& prototypeName = parts[0];
    ItemRecordHead* prototype = g_itemDatabase.weapons.Named(prototypeName);
    if (!prototype) prototype = g_itemDatabase.armors.Named(prototypeName);
    if (!prototype) prototype = g_itemDatabase.quickItems.Named(prototypeName);
    if (!prototype) prototype = g_itemDatabase.questItems.Named(prototypeName);
    if (!prototype) prototype = g_itemDatabase.loot.Named(prototypeName);
    if (!prototype) {
        // An unknown prototype gives no item (Rebirth's addon.log line, in ours).
        ei::Log("GiveItem: can't find prototype %s", text);
        return nullptr;
    }
    MaterialRecordHead* material = parts.size() > 1 ? g_itemDatabase.materials.Named(parts[1]) : nullptr;
    // The bracket part: the count for materials, a spell for the other prototypes.
    Spell* spell = nullptr;
    int count = 1;
    if (!extra.empty()) {
        if (prototype->name == kMaterialType)
            count = str::ToInt(extra);
        else
            spell = context->GetSpellManager()->CreateSpellFromText(extra);
    }
    Item* item = context->GetItemConstructor()->BuildItem(this, prototype->Kind(), prototype, material, spell, count);
    // A failed build gives no item.
    if (!item) {
        ei::Log("GiveItem: can't make %s", text);
        return nullptr;
    }
    if (ItemLoot* loot = item->AsLoot(); loot && parts.size() > 1) {
        if (loot->kind == LootKind::Record) loot->index = static_cast<std::uint32_t>(LootTableIndex(loot->recordType, parts[1]));
    }
    return item;
}

Item* ItemManager::MakeItem(ItemKind kind, std::uint32_t index, std::uint32_t material) {
    if ((index & 0xFFFF) == kNoIndex) index = 0;
    MaterialRecordHead* materialRecord = NeedsMaterial(kind) ? MaterialAt(material) : nullptr;
    return context->GetItemConstructor()->BuildItem(this, kind, PrototypeAt(kind, index), materialRecord);
}

// The "spell container" record is looked up by name each time. A database without one gives index -1, which MakeItem
// takes as 0 (the original computed an index from the null record's address).
Item* ItemManager::CreateSpellItem(std::string_view spellText, SpellManager* spells) {
    Spell* spell = spells->CreateSpellFromText(spellText);
    if (!spell) return nullptr;
    int index = IndexNamed(g_itemDatabase.quickItems, kSpellContainer);
    Item* item = MakeItem(ItemKind::Scroll, index, 0);
    if (item) item->AttachSpell(spell);
    return item;
}

std::vector<std::string> ItemManager::ListMaterialNames() { return RecordNames(g_itemDatabase.materials); }
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

namespace {

// The first item without its prototype and the first spell without its records of the context's managers (null
// for none); the spell manager's containers are not spells.
struct MissingRecords {
    Item* item = nullptr;
    Spell* spell = nullptr;
};

MissingRecords FindMissingRecords(ManagerContext* context) {
    MissingRecords missing;
    if (ItemManager* manager = context->ItemFactory()) {
        ObjectListIteratorOf<Item> items;
        for (Item* item = items.First(manager); item && !missing.item; item = items.Next())
            if (!item->HasPrototype()) missing.item = item;
    }
    if (SpellManager* manager = context->GetSpellManager()) {
        ObjectListIterator objects;
        for (EngineObject* object = objects.First(manager); object && !missing.spell; object = objects.Next()) {
            if (object->type != static_cast<int>(SpellObjectType::Spell)) continue;
            if (auto* spell = static_cast<Spell*>(object); !spell->HasRecords()) missing.spell = spell;
        }
    }
    return missing;
}

} // namespace

bool HasEveryRecord(ManagerContext* context) {
    MissingRecords missing = FindMissingRecords(context);
    return !missing.item && !missing.spell;
}

void RequireEveryRecord(ManagerContext* context) {
    MissingRecords missing = FindMissingRecords(context);
    if (missing.item) missing.item->RequirePrototype();
    if (missing.spell) missing.spell->RequireRecords();
}
