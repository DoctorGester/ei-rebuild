// The camp's two action buttons and taking a spell container apart. See docs/notes/ui.md, section 5.
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
std::uint32_t PrototypeIndex(const CEngineObject* object) { return object->prototypeId_ >> 16; }

// Every item of `list` back to `to`, then the strip scrolled to the last one.
void ReturnToStrip(CCampPages* camp, CParty* to, CParty* list, CUiItemStrip* strip) {
    CItem* last = camp->ReturnAll(to, list);
    strip->ScrollToItem(last);
}

// The made spell or item replaces the results: every result removed, the made one added and dropped.
void TakeMade(CParty* results, Ref<CItem>& made) {
    while (!results->items_.empty()) results->RemoveItem(results->items_[0], 0);
    results->AddItem(made, 0, 0);
    made.Reset();
}

// Every result moved back: made items to the constructor's first list and strip, the others to the
// player and the item strip.
void ReturnResults(CCampPages* camp, CParty* results, CParty* constructorList, CUiItemStrip* constructorStrip) {
    while (!results->items_.empty()) {
        CItem* item = results->items_[0];
        bool made = IsMade(item);
        CParty* to = made ? constructorList : LocalPlayerData();
        CItem* moved = camp->MoveItem(item, to, results, 0, true, false);
        (made ? constructorStrip : camp->itemStrip_)->ScrollToItem(moved);
    }
}

// A new item named `name` from the results' item manager as loot (null when none is made).
CItemLoot* NewLoot(CParty* results, std::string_view name) {
    return AsLoot(results->ItemFactory()->CreateItem(name));
}
// `item` (not checked for null) added to the results, counted while it is added; its record index set first
// when `index` is given.
void AddLoot(CParty* results, CItemLoot* item, std::optional<std::uint32_t> index) {
    item->AddRef();
    if (index) item->index_ = *index;
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
void StockSpells(CCampPages* camp, CParty* list) {
    ItemStock* stock = &camp->constructor_->magic;
    for (CItem* item : list->items_) {
        item->Repair();
        if (IsSpellContainer(item))
            stock->AddSpell(g_spellDatabase.spells.Named(item->spell_->GetPrototype()->code.c_str()));
        else if (IsSpell(item))
            stock->AddSpell(itemdb::SpellAt(AsLoot(item)->index_));
        else if (IsModifier(item))
            stock->AddModifier(itemdb::SpellModifierAt(AsLoot(item)->index_));
    }
}

// The item constructor's third list, from the end: each item repaired; weapons, armour and quick
// items that can be taken apart and blueprint parts add their record to the template's stock;
// loot priced by itself (LootKind::Self) leaves the list.
void StockItems(CCampPages* camp, CParty* list) {
    ItemStock* stock = &camp->constructor_->allowed;
    for (int i = std::ssize(list->items_) - 1; i >= 0; --i) {
        CItem* item = list->items_[i];
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
            index = loot->index_;
            if (loot->recordType_ <= LootRecordType::Quick) kind = loot->recordType_;
        } else if (item->Kind() == ItemKind::Loot && AsLoot(item)->kind_ == LootKind::Self) {
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
    for (CItem* item : list->items_) item->Repair();
}

} // namespace

// --- a spell container taken apart ---------------------------------------------------------------

void CCampPages::DeconstructSpell() {
    CParty* results = spellResults_;
    Ref<CItem> copy;
    copy.Assign(results->CopyItem(results->items_[0]));
    results->RemoveItem(results->items_[0], 0);
    auto* spell = copy->spell_.Get();

    AddLoot(results, NewLoot(results, kPrototypeItem), PrototypeIndex(spell));
    for (int i = 0; i < spell->GetModifierCount(); ++i)
        AddLoot(results, NewLoot(results, kRuneItem), spell->modifiers_[i] - 1);
}
// --- an item taken apart -------------------------------------------------------------------------

void CCampPages::DeconstructItem() {
    CParty* results = itemResults_;
    Ref<CItem> copy;
    copy.Assign(results->CopyItem(results->items_[0]));
    results->RemoveItem(results->items_[0], 0);

    if (const char* instruction = InstructionName(copy->Kind())) {
        if (CItemLoot* item = NewLoot(results, instruction)) AddLoot(results, item, PrototypeIndex(copy));
    }
    // A material per one the prototype needs; a material index out of range reads its key through null.
    for (std::uint32_t i = 0; i < copy->GetPrototype()->materialUnits; ++i) {
        const itemdb::MaterialRecordHead* material = itemdb::MaterialAt(MaterialIndex(copy->prototypeId_));
        if (CItemLoot* item = NewLoot(results, std::string("material.") + material->key))
            AddLoot(results, item, std::nullopt);
    }
    if (copy->spell_) {
        itemdb::QuickItemTable& quickItems = g_itemDatabase.quickItems;
        int index = quickItems.IndexOf(quickItems.Named("spell container"));
        Ref<CItem> container;
        container.Assign(results->ItemFactory()->MakeItem(ItemKind::Spell, index, 0));
        if (container) {
            container->AttachSpell(copy->spell_->Clone(results->SpellManager()));
            results->AddItem(container, 0, 0);
        }
    }
}

// --- the first action button ---------------------------------------------------------------------

void CCampPages::DoAction() {
    interface_->PlaySound(kBuySound);
    switch (page_) {
    case kEquipment:
        if (itemConstructor_ && !noConstructor_) {
            PartyUnitRecord* hero = HeroRecord();
            for (int i = 0; i < std::ssize(hero->weapons); ++i) hero->weapons[i]->Repair();
            for (int i = 0; i < std::ssize(hero->armor); ++i) hero->armor[i]->Repair();
        }
        break;
    case kTrade:
        trade_->Settle(true, false);
        return;
    case kSpellTrade:
        constructor_->SpellsParty()->SpellManager();
        StockSpells(this, spellParts_[2]);
        ReturnToStrip(this, spellParts_[0], spellParts_[2], spellStrip_);
        ReturnToStrip(this, LocalPlayerData(), spellParts_[1], itemStrip_);
        break;
    case kSpellMaker:
        if (deconstructing_) {
            DeconstructSpell();
        } else if (spellMade_) {
            TakeMade(spellResults_, spellMade_);
        }
        break;
    case kItemTrade:
        StockItems(this, itemParts_[2]);
        ReturnToStrip(this, itemParts_[0], itemParts_[2], partStrip_);
        ReturnToStrip(this, LocalPlayerData(), itemParts_[1], itemStrip_);
        break;
    case kItemMaker:
        if (deconstructing_) {
            DeconstructItem();
        } else if (itemMade_) {
            TakeMade(itemResults_, itemMade_);
        }
        break;
    case kRepair:
        RepairAll(itemExtra_);
        ReturnToStrip(this, LocalPlayerData(), itemExtra_, itemStrip_);
        break;
    default:
        break;
    }
    LocalPlayerData()->money_ -= cost_;
}

// --- the second action button --------------------------------------------------------------------

void CCampPages::UndoAction() {
    interface_->PlaySound(kCancelSound);
    switch (page_) {
    case kTrade:
        trade_->Settle(false, false);
        return;
    case kSpellTrade:
        ReturnToStrip(this, spellParts_[0], spellParts_[1], spellStrip_);
        ReturnToStrip(this, LocalPlayerData(), spellParts_[2], itemStrip_);
        break;
    case kSpellMaker:
        ReturnResults(this, spellResults_, spellParts_[0], spellStrip_);
        break;
    case kItemTrade:
        ReturnToStrip(this, itemParts_[0], itemParts_[1], partStrip_);
        ReturnToStrip(this, LocalPlayerData(), itemParts_[2], itemStrip_);
        break;
    case kItemMaker:
        ReturnResults(this, itemResults_, itemParts_[0], partStrip_);
        break;
    case kRepair:
        ReturnToStrip(this, LocalPlayerData(), itemExtra_, itemStrip_);
        break;
    default:
        break;
    }
}
