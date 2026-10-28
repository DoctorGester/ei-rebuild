// Item construction: CItemConstructor and the CItem methods it drives. The item manager's CreateItem paths run
// BeginCreateItem (the manager creates the empty item object), SetPrototype / SetMaterial / SetSpellObject, then
// EndCreateItem, whose Build fills the item from a temporary database record: the prototype's values combined with
// the material's.
#include "console.h"
#include "game_data.h"
#include "item.h"
#include "spell.h"
#include "text_split.h"

#include "lib/crt.h"
#include "lib/str.h"

#include "ei/rebirth.h"
#include "ei/x87.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>

using namespace itemdb;

namespace {



constexpr std::uint32_t kFlagConstructing = 8;

const char* const kCantCreateItem = "CItemConstructor::BeginCreateItem.\n";

// The charge rate every item starts with (0.001).
constexpr float kDefaultChargeRate = 0.001f;

// --- The temporary database record ---------------------------------------------------------------
// Build creates a record of the item's kind, fills only the fields the item's InitFromRecord reads, and
// destroys it again. The prototype and material records are read without a null check where the game reads them
// without one (a missing record faults there).

// The temporary record for an item of class `classId` (the base record's construction inline in
// Build); null for other classes (and when the allocation fails).
ItemRecordHead* NewRecord(ClassId classId) {
    switch (classId) {
    case ClassId::ItemWeapon: return new WeaponRecord();
    case ClassId::ItemArmor: return new ArmorRecord();
    case ClassId::ItemQuick:
    case ClassId::ItemSpell: return new QuickItemRecord();
    case ClassId::ItemQuest: return new QuestItemRecord();
    case ClassId::ItemLoot: return new LootItemRecord();
    default: return nullptr;
    }
}

// Destroys what every record has (the extra members of the quick, quest and loot records are not destroyed: the
// game leaks them, though they hold nothing here; the head's destructor is not virtual) and frees it.
void DeleteRecord(ItemRecordHead* record) {
    record->~ItemRecordHead();
    ::operator delete(record);
}

template <typename Record>
std::uint32_t IndexIn(const NamedRecordTable<Record>& table, const ItemRecordHead* record) {
    return static_cast<std::uint32_t>(table.IndexOf(RecordAs<Record>(record)));
}

// Build's common part: price, values and charge of the record, from the prototype alone or scaled by the material.
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
    float unitPrice = material->unitPrice, price = prototype->price;
    record->price = static_cast<float>(units * unitPrice + price);
    float weight = prototype->weight, weightFactor = material->weightFactor;
    record->weight = weight * weightFactor;
    record->unreadValue = prototype->unreadValue;
    float charge = prototype->charge, extraCharge = material->magicPower;
    record->charge = charge + extraCharge;
    float condition = prototype->condition, conditionFactor = material->conditionFactor;
    record->condition = condition * conditionFactor;
    record->magicLevel = prototype->magicLevel + material->magicLevel;
    record->materialUnits = prototype->materialUnits;
}

// Build's per-class part; returns the prototype's index in its table.
std::uint32_t FillClass(ClassId classId, ItemRecordHead* record, const ItemRecordHead* prototype,
                        const MaterialRecordHead* material) {
    switch (classId) {
    case ClassId::ItemWeapon: {
        auto* to = RecordAs<WeaponRecord>(record);
        auto* from = RecordAs<WeaponRecord>(prototype);
        to->reach = from->reach;
        float damage = from->damage, factor = material->damageFactor;
        to->damage = damage * factor;
        float spread = from->damageSpread, factor2 = material->damageFactor;
        to->damageSpread = spread * factor2;
        return IndexIn(g_itemDatabase->weapons, prototype);
    }
    case ClassId::ItemArmor: {
        auto* to = RecordAs<ArmorRecord>(record);
        auto* from = RecordAs<ArmorRecord>(prototype);
        to->firstFlags = from->firstFlags | material->armourFlags;
        to->othersFlags = from->othersFlags | material->armourFlags;
        // Seven absorption values in two rows, each scaled by the material's factor for its
        // damage type and by the row's factor.
        for (int k = 0; k < 7; ++k) {
            float a = from->absorbFirst[k], b = material->absorbFactors[k], c = from->FirstFactor();
            to->absorbFirst[k] = a * b * c;
            float d = from->absorbOthers[k], e = material->absorbFactors[k], f = from->othersFactor;
            to->absorbOthers[k] = d * e * f;
        }
        return IndexIn(g_itemDatabase->armors, prototype);
    }
    case ClassId::ItemQuick:
    case ClassId::ItemSpell:
        RecordAs<QuickItemRecord>(record)->actionValue = RecordAs<QuickItemRecord>(prototype)->actionValue;
        return IndexIn(g_itemDatabase->quickItems, prototype);
    case ClassId::ItemQuest:
        record->category = prototype->category;
        return IndexIn(g_itemDatabase->questItems, prototype);
    case ClassId::ItemLoot: {
        auto* to = RecordAs<LootItemRecord>(record);
        auto* from = RecordAs<LootItemRecord>(prototype);
        to->category = from->category;
        to->recordType = from->recordType;
        // A material takes the material's index when one is given.
        if (from->category == kLootMaterial && material)
            to->index = material->number;
        else
            to->index = from->index;
        return IndexIn(g_itemDatabase->loot, prototype);
    }
    default:
        return 0;   // not reached: the record of an unknown class is null and faulted above
    }
}

const char* const kSpellContainer = "spell container";
const char* const kMaterialType = "material";
const char* const kNoPrototype = "Can't find prototype for item \"%s\".\n";
const char* const kCantCreate = "Can't create item \"%s\".\nCheck shape and material names\n";

// Index of `record` in `table` (signed division of the byte offset), -1 for no record.
template <typename Table, typename Record>
int TableIndex(const Table& table, const Record* record) {
    return record ? table.IndexOf(record) : -1;
}

// The table index of a loot item's second name part.
int LootTableIndex(LootRecordType which, std::string_view name) {
    if (which > kRecordModifier) return 0;
    std::string key(name);
    ItemDatabase& items = *g_itemDatabase;
    SpellDatabase& spells = *g_spellDatabase;
    switch (which) {
    case kRecordWeapon:
        return TableIndex(items.weapons, items.weapons.names[key]);
    case kRecordArmor:
        return TableIndex(items.armors, items.armors.names[key]);
    case kRecordQuick:
        return TableIndex(items.quickItems, items.quickItems.names[key]);
    case kRecordSpell:
        return TableIndex(spells.spells, spells.spells.byCode[key]);
    default:
        return TableIndex(spells.modifiers, spells.modifiers.byCode[key]);
    }
}

// The lists' loop: the array takes the table's count, then every element the name of the record
// with its index (the count is read again for each; a record out of range would be read at null).
template <typename Record, typename NameOf>
void ListRecordNames(const NamedRecordTable<Record>& table, std::vector<std::string>* names, NameOf nameOf) {
    names->resize(table.Count());
    for (std::size_t i = 0; i < names->size(); ++i) {
        const Record* record = table.At(static_cast<std::uint32_t>(i));
        (*names)[i] = *nameOf(record);
    }
}

const std::string* HeadName(const ItemRecordHead* record) { return &record->name; }

} // namespace

// --- CItem ---------------------------------------------------------------------------------------

void CItem::InitFromRecord(const ItemRecordHead* head) {
    // The condition and the charge pass through the FPU, which quiets a signalling NaN; the other fields are
    // copied bit for bit.
    conditionMax_ = head->condition;
    condition_ = head->condition;
    chargeRate_ = kDefaultChargeRate;
    chargeMax_ = head->charge;
    charge_ = head->charge;
    weight_ = head->weight;
    price_ = head->price;
    unreadValue_ = head->unreadValue;
    magicLevel_ = head->magicLevel;
}

void CItem::AttachSpell(CSpell* spell) {
    CSpell* previous = spell_;
    if (previous) price_ = price_ - std::bit_cast<float>(previous->price_);
    spell_ = spell;
    if (spell) ++spell->refCount_;
    if (previous) previous->Release();
    if (spell_) price_ = spell_->price_ + price_;
}

void CItem::SetSpell(CSpell* spell) {
    CSpell* previous = spell_;
    spell_ = spell;
    if (spell) ++spell->refCount_;
    if (previous) previous->Release();
}

// --- CItemConstructor ----------------------------------------------------------------------------

int CItemConstructor::Create() {
    CItemConstructor::Destroy();
    return 1;
}

void CItemConstructor::Destroy() {
    if (manager_) --manager_->refCount_;
    manager_ = nullptr;
    if (item_) item_->Release();
    item_ = nullptr;
    material_ = nullptr;
    prototype_ = nullptr;
    spell_ = nullptr;
}

CItemConstructor::CItemConstructor() {
    ClearFields();
    material_ = nullptr;
    prototype_ = nullptr;
    item_ = nullptr;
    spell_ = nullptr;
    manager_ = nullptr;
}

CItemConstructor::~CItemConstructor() { CItemConstructor::Destroy(); }

const char* CItemConstructor::ClassName() { return "CItemConstructor"; }
ClassId CItemConstructor::GetClassId() { return ClassId::ItemConstructor; }

void CItemConstructor::SetManager(CBaseManager* manager) {
    if (manager_) --manager_->refCount_;
    manager_ = manager;
    if (manager) ++manager->refCount_;
}

BOOL CItemConstructor::BeginCreateItem(int kind, int key) {
    CItem* item = manager_->CreateObjectAs<CItem>(kind, key);
    if (!item) return game::FatalError(kCantCreateItem);
    if (item_) item_->Release();
    item_ = item;
    ++item->refCount_;
    material_ = nullptr;
    prototype_ = nullptr;
    count_ = 1;
    flags_ |= kFlagConstructing;
    return 1;
}

BOOL CItemConstructor::SetMaterial(MaterialRecordHead* material) {
    material_ = material;
    return 1;
}

BOOL CItemConstructor::SetPrototype(ItemRecordHead* prototype) {
    prototype_ = prototype;
    return 1;
}

BOOL CItemConstructor::SetSpellObject(CSpell* spell) {
    spell_ = spell;
    return 1;
}

CItem* CItemConstructor::EndCreateItem() {
    if (!Build()) {
        item_->Release();
        item_ = nullptr;
        return nullptr;
    }
    flags_ &= ~kFlagConstructing;
    CItem* item = item_;
    --item->refCount_;
    item_ = nullptr;
    spell_ = nullptr;
    count_ = 0;
    return item;
}

BOOL CItemConstructor::Cancel() {
    if (item_) item_->Release();
    item_ = nullptr;
    spell_ = nullptr;
    material_ = nullptr;
    prototype_ = nullptr;
    return 1;
}

BOOL CItemConstructor::Build() {
    ClassId classId = item_->GetClassId();
    if ((classId == ClassId::ItemWeapon || item_->GetClassId() == ClassId::ItemArmor) && !material_) return 0;

    const ItemRecordHead* prototype = prototype_;
    const MaterialRecordHead* material = material_;
    ItemRecordHead* record = NewRecord(item_->GetClassId());
    FillCommon(record, prototype, material);
    std::uint32_t index = FillClass(item_->GetClassId(), record, prototype, material);

    item_->InitFromRecord(record);
    if (item_->GetClassId() == ClassId::ItemQuick && static_cast<int>(prototype->category) == kCategoryPotion) {
        CSpellManager* spellManager = manager_->context_->SpellManager();
        CSpell* spell = spellManager->CreateSpellFromText((RecordAs<QuickItemRecord>(prototype)->spellText));
        spell_ = spell;
        item_->SetSpell(spell);
    } else if (spell_) {
        item_->AttachSpell(spell_);
    }
    if (record) DeleteRecord(record);

    if (material)
        item_->prototypeId_ = (material->number & 0xFFFF) | index << 16;
    else
        item_->prototypeId_ = index << 16;
    item_->count_ = static_cast<std::uint32_t>(count_);
    return 1;
}

// --- The item manager's CreateItem ----------------------------------------------------

CItem* CItemManager::CreateItem(std::string_view name) {
    auto* context = context_;
    CItemConstructor* constructor = context->ItemConstructor();
    std::string extra;               // the part in brackets: "name[extra]"
    std::string text(name);
    std::vector<std::string> parts;      // the name split at '.'
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
    text::Split(text, &parts, '.');
    for (std::string& part : parts) str::MakeLower(part);

    // "spell container[spell text]": a scroll holding that spell.
    if (str::Compare(parts[0],
                        kSpellContainer) == 0) {
        CSpell* spell = context->SpellManager()->CreateSpellFromText(extra);
        CItem* item = nullptr;
        if (spell) {
            QuickItemTable& quickItems = g_itemDatabase->quickItems;
            int index = quickItems.IndexOf(quickItems.Named(kSpellContainer));
            item = (CItem*)MakeItem(kItemSpell, index, 0, -1);
            if (item) item->AttachSpell(spell);
        }
        return item;
    }

    // "prototype[.material]".
    const std::string& prototypeName = parts[0];
    ItemDatabase& items = *g_itemDatabase;
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
    constructor->SetManager(this);
    constructor->BeginCreateItem(prototype->Kind(), -1);
    MaterialRecordHead* material = nullptr;
    if (parts.size() > 1) material = items.materials.Named(parts[1]);
    constructor->SetPrototype(prototype);
    constructor->SetMaterial(material);
    // The bracket part: a spell for most prototypes, the count for materials.
    const std::string& type = prototype->name;
    if (extra.size() != 0 && type != kMaterialType) {
        CSpellManager* spells = context->SpellManager();
        if (CSpell* spell = spells->CreateSpellFromText(extra)) constructor->SetSpellObject(spell);
    } else if (type == kMaterialType &&
               extra.size() != 0) {
        constructor->count_ = str::ToInt(extra);
    }
    CItem* item = constructor->EndCreateItem();
    if (!item) {
        // Rebirth's FixCreateItem: logged, and no item. Without it the error is reported and the code goes on
        // with the null item (and crashes).
        if (ei::rebirth::Enabled(ei::rebirth::Patch::FixCreateItem)) return nullptr;
        game::FatalError(kCantCreate, std::string(name));
    }
    constructor->SetManager(nullptr);
    auto* loot = AsLoot(item);
    if (item->GetClassId() == ClassId::ItemLoot && loot->kind_ == kLootRecord && parts.size() > 1) {
        loot->index_ = static_cast<std::uint32_t>(LootTableIndex(loot->recordType_, parts[1]));
    }
    return item;
}

// --- The item manager's MakeItem ----------------------------------------------------------------

CItem* CItemManager::MakeItem(int kind, std::uint32_t index, std::uint32_t material, int key) {
    CItemConstructor* constructor = context_->ItemConstructor();
    if (static_cast<std::int16_t>(index) == -1) index = 0;
    constructor->SetManager(this);
    constructor->BeginCreateItem(kind, key);
    ItemRecordHead* prototype = nullptr;
    MaterialRecordHead* materialRecord = nullptr;
    switch (kind) {
    case kItemWeapon:
        prototype = WeaponAt(index);
        materialRecord = MaterialAt(material);
        break;
    case kItemArmor:
        prototype = ArmorAt(index);
        materialRecord = MaterialAt(material);
        break;
    case kItemQuick:
    case kItemSpell:
        prototype = QuickItemAt(index);
        break;
    case kItemLoot:
        prototype = LootAt(index);
        break;
    case kItemQuest:
        prototype = QuestItemAt(index);
        break;
    default:
        break;
    }
    constructor->SetPrototype(prototype);
    constructor->SetMaterial(materialRecord);
    CItem* item = constructor->EndCreateItem();
    constructor->SetManager(nullptr);
    return item;
}

// --- The scroll of a spell text, the name lists, the constructor's magic capacity ----------------

// The "spell container" record is looked up by name each time; without one the index is that of a
// null record (kept: MakeItem then finds no prototype).
CItem* CItemManager::CreateSpellItem(std::string_view spellText, CSpellManager* spells) {
    CSpell* spell = spells->CreateSpellFromText(spellText);
    if (!spell) return nullptr;
    QuickItemTable& quickItems = g_itemDatabase->quickItems;
    int index = quickItems.IndexOf(quickItems.Named(kSpellContainer));
    CItem* item = (CItem*)MakeItem(kItemSpell, index, 0, -1);
    if (item) item->AttachSpell(spell);
    return item;
}

void CItemManager::ListMaterialNames(std::vector<std::string>* names) {
    ListRecordNames(g_itemDatabase->materials, names, [](const MaterialRecordHead* record) { return &record->key; });
}
void CItemManager::ListWeaponNames(std::vector<std::string>* names) { ListRecordNames(g_itemDatabase->weapons, names, HeadName); }
void CItemManager::ListArmorNames(std::vector<std::string>* names) { ListRecordNames(g_itemDatabase->armors, names, HeadName); }
void CItemManager::ListQuickItemNames(std::vector<std::string>* names) {
    ListRecordNames(g_itemDatabase->quickItems, names, HeadName);
}

bool CItemConstructor::MagicCapacity(float* level, float* power) {
    if ((item_->GetClassId() == ClassId::ItemWeapon || item_->GetClassId() == ClassId::ItemArmor) && !material_) return false;
    const ItemRecordHead* prototype = prototype_;
    const MaterialRecordHead* material = material_;
    if (material) {
        *level = static_cast<float>(x87::FromUnsigned(prototype->magicLevel + material->magicLevel));
        *power = prototype->charge + material->magicPower;
    } else {
        *level = static_cast<float>(x87::FromUnsigned(prototype->magicLevel));
        *power = prototype->charge;
    }
    return true;
}
