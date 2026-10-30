// The camp's two action buttons and taking a spell container apart. See docs/notes/ui.md, section 5.
#include "item_manager.h"
#include "item_stock.h"
#include "ui_camp.h"
#include "game_strings.h"

#include "class_templates.h"
#include "item.h"
#include "item_db.h"
#include "party.h"
#include "spell.h"
#include "ui_item_info.h"
#include "ui_screens.h"
#include "unit.h"
#include "world.h"
#include "lib/containers.h"

#include <cstdint>
#include <optional>
#include <string>

namespace {

const char* const kBuySound = "buttons\\camp\\buy.wav";
const char* const kCancelSound = "buttons\\messbox\\cancel.wav";
const char* const kPrototypeItem = "Prototype";
const char* const kRuneItem = "Rune";

// Pages of the action buttons.
enum CampPage {
    kEquipment = 0,
    kTrade = 1,
    kSpellTrade = 2,
    kSpellMaker = 3,
    kItemTrade = 4,
    kItemMaker = 5,
    kRepair = 6,
};

// The prototype index an item's packed id carries (its high word).
std::uint32_t PrototypeIndex(const EngineObject* object) { return object->prototypeId >> 16; }

// Every item of `list` back to `to`, then the strip scrolled to the last one.
void ReturnToStrip(CampPages* camp, CParty* to, CParty* list, UiItemStrip* strip) {
    Item* last = camp->ReturnAll(to, list);
    strip->ScrollToItem(last);
}

// The made spell or item replaces the results: every result removed, the made one added and dropped.
void TakeMade(CParty* results, Ref<Item>& made) {
    while (!results->items.empty()) results->RemoveItem(results->items[0], 0);
    results->AddItem(made, 0, 0);
    made.Reset();
}

// Every result moved back: made items to the constructor's first list and strip, the others to the
// player and the item strip.
void ReturnResults(CampPages* camp, CParty* results, CParty* constructorList, UiItemStrip* constructorStrip) {
    while (!results->items.empty()) {
        Item* item = results->items[0];
        bool made = IsMade(item);
        CParty* to = made ? constructorList : LocalPlayerData();
        Item* moved = camp->MoveItem(item, to, results, 0, true, false);
        (made ? constructorStrip : camp->itemStrip)->ScrollToItem(moved);
    }
}

// A new item named `name` from the results' item manager as loot (null when none is made).
ItemLoot* NewLoot(CParty* results, std::string_view name) {
    return AsLoot(results->ItemFactory()->CreateItem(name));
}
// `item` (not checked for null) added to the results, counted while it is added; its record index set first
// when `index` is given.
void AddLoot(CParty* results, ItemLoot* item, std::optional<std::uint32_t> index) {
    item->AddRef();
    if (index) item->index = *index;
    results->AddItem(item, 0, 0);
    item->Release();
}

// The instruction (blueprint) items by the item's class: weapon, armour, quick item.
const char* InstructionName(ItemKind itemClass) {
    switch (itemClass) {
    case ItemKind::Weapon: return "Instruction Weapon";
    case ItemKind::Armor: return "Instruction Armor";
    case ItemKind::Quick: return "Instruction Quick Item";
    default: return nullptr;
    }
}

// The spell constructor's third list: each item repaired, its spell's (container), spell's or
// modifier's record added to the template's spell stock.
void StockSpells(CampPages* camp, CParty* list) {
    ItemStock* stock = &camp->constructor->magic;
    for (Item* item : list->items) {
        item->Repair();
        if (IsSpellContainer(item))
            stock->AddSpell(g_spellDatabase.spells.Named(item->spell->GetPrototype()->code.c_str()));
        else if (IsSpell(item))
            stock->AddSpell(itemdb::SpellAt(AsLoot(item)->index));
        else if (IsModifier(item))
            stock->AddModifier(itemdb::SpellModifierAt(AsLoot(item)->index));
    }
}

// The item constructor's third list, from the end: each item repaired; weapons, armour and quick
// items that can be taken apart and blueprint parts add their record to the template's stock;
// loot priced by itself (LootKind::Self) leaves the list.
void StockItems(CampPages* camp, CParty* list) {
    ItemStock* stock = &camp->constructor->allowed;
    for (int i = lib::Count(list->items) - 1; i >= 0; --i) {
        Item* item = list->items[i];
        item->Repair();
        std::optional<LootRecordType> kind;
        std::uint32_t index = 0;
        if (IsBuildable(item)) {
            index = PrototypeIndex(item);
            if (item->Kind() == ItemKind::Weapon) kind = LootRecordType::Weapon;
            else if (item->Kind() == ItemKind::Armor) kind = LootRecordType::Armor;
            else if (item->Kind() == ItemKind::Quick) kind = LootRecordType::Quick;
        } else if (IsPart(item)) {
            auto* loot = AsLoot(item);
            index = loot->index;
            if (loot->recordType <= LootRecordType::Quick) kind = loot->recordType;
        } else if (item->Kind() == ItemKind::Loot && AsLoot(item)->kind == LootKind::Self) {
            list->RemoveItem(item, 1);
        }
        if (kind == LootRecordType::Weapon)
            stock->AddWeapon(itemdb::WeaponAt(index));
        else if (kind == LootRecordType::Armor)
            stock->AddArmor(itemdb::ArmorAt(index));
        else if (kind == LootRecordType::Quick)
            stock->AddQuickItem(itemdb::QuickItemAt(index));
    }
}

void RepairAll(CParty* list) {
    for (Item* item : list->items) item->Repair();
}

} // namespace

// --- a spell container taken apart ---------------------------------------------------------------

void CampPages::DeconstructSpell() {
    CParty* results = spellResults;
    Ref<Item> copy;
    copy.Assign(results->CopyItem(results->items[0]));
    results->RemoveItem(results->items[0], 0);
    auto* spell = copy->spell.Get();

    AddLoot(results, NewLoot(results, kPrototypeItem), PrototypeIndex(spell));
    for (int i = 0; i < spell->GetModifierCount(); ++i)
        AddLoot(results, NewLoot(results, kRuneItem), spell->modifiers[i] - 1);
}
// --- an item taken apart -------------------------------------------------------------------------

void CampPages::DeconstructItem() {
    CParty* results = itemResults;
    Ref<Item> copy;
    copy.Assign(results->CopyItem(results->items[0]));
    results->RemoveItem(results->items[0], 0);

    if (const char* instruction = InstructionName(copy->Kind())) {
        if (ItemLoot* item = NewLoot(results, instruction)) AddLoot(results, item, PrototypeIndex(copy));
    }
    // A material per one the prototype needs; a material index out of range reads its key through null.
    for (std::uint32_t i = 0; i < copy->GetPrototype()->materialUnits; ++i) {
        const itemdb::MaterialRecordHead* material = itemdb::MaterialAt(MaterialIndex(copy->prototypeId));
        if (ItemLoot* item = NewLoot(results, std::string("material.") + material->key))
            AddLoot(results, item, std::nullopt);
    }
    if (copy->spell) {
        itemdb::QuickItemTable& quickItems = g_itemDatabase.quickItems;
        int index = quickItems.IndexOf(quickItems.Named("spell container"));
        Ref<Item> container;
        container.Assign(results->ItemFactory()->MakeItem(ItemKind::Scroll, index, 0));
        if (container) {
            container->AttachSpell(copy->spell->Clone(results->GetSpellManager()));
            results->AddItem(container, 0, 0);
        }
    }
}

// --- the first action button ---------------------------------------------------------------------

void CampPages::DoAction() {
    ui->PlayInterfaceSound(kBuySound);
    switch (page) {
    case kEquipment:
        if (itemConstructor && !noConstructor) {
            PartyUnitRecord* heroRecord = HeroRecord();
            for (int i = 0; i < std::ssize(heroRecord->weapons); ++i) heroRecord->weapons[i]->Repair();
            for (int i = 0; i < std::ssize(heroRecord->armor); ++i) heroRecord->armor[i]->Repair();
        }
        break;
    case kTrade:
        trade->Settle(true, false);
        return;
    case kSpellTrade:
        constructor->SpellsParty()->GetSpellManager();
        StockSpells(this, spellParts[2]);
        ReturnToStrip(this, spellParts[0], spellParts[2], spellStrip);
        ReturnToStrip(this, LocalPlayerData(), spellParts[1], itemStrip);
        break;
    case kSpellMaker:
        if (deconstructing) {
            DeconstructSpell();
        } else if (spellMade) {
            TakeMade(spellResults, spellMade);
        }
        break;
    case kItemTrade:
        StockItems(this, itemParts[2]);
        ReturnToStrip(this, itemParts[0], itemParts[2], partStrip);
        ReturnToStrip(this, LocalPlayerData(), itemParts[1], itemStrip);
        break;
    case kItemMaker:
        if (deconstructing) {
            DeconstructItem();
        } else if (itemMade) {
            TakeMade(itemResults, itemMade);
        }
        break;
    case kRepair:
        RepairAll(itemExtra);
        ReturnToStrip(this, LocalPlayerData(), itemExtra, itemStrip);
        break;
    default:
        break;
    }
    LocalPlayerData()->money -= cost;
}

// --- the second action button --------------------------------------------------------------------

void CampPages::UndoAction() {
    ui->PlayInterfaceSound(kCancelSound);
    switch (page) {
    case kTrade:
        trade->Settle(false, false);
        return;
    case kSpellTrade:
        ReturnToStrip(this, spellParts[0], spellParts[1], spellStrip);
        ReturnToStrip(this, LocalPlayerData(), spellParts[2], itemStrip);
        break;
    case kSpellMaker:
        ReturnResults(this, spellResults, spellParts[0], spellStrip);
        break;
    case kItemTrade:
        ReturnToStrip(this, itemParts[0], itemParts[1], partStrip);
        ReturnToStrip(this, LocalPlayerData(), itemParts[2], itemStrip);
        break;
    case kItemMaker:
        ReturnResults(this, itemResults, itemParts[0], partStrip);
        break;
    case kRepair:
        ReturnToStrip(this, LocalPlayerData(), itemExtra, itemStrip);
        break;
    default:
        break;
    }
}
