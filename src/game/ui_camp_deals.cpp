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

// Loot (CItemLoot) kinds and record types the constructors take.

CParty* PlayerParty() { return LocalPlayerData(); }
CItem* ItemAt(CParty* party, int index) { return party->items_[index]; }
int ItemCount(const CParty* party) { return std::ssize(party->items_); }
// The prototype index an item's packed id carries (its high word).
std::uint32_t PrototypeIndex(const CEngineObject* object) { return object->prototypeId_ >> 16; }
// An item record marked as made by a constructor.
bool MadeByConstructor(const CItem* item) {
    auto* record = reinterpret_cast<const std::uint8_t*>(item->link_);
    return record && record[0x10];
}

// The stock lists of a class template: everything and spells and modifiers.
ItemStock* AllowedList(ClassTemplate* t) { return &t->allowed; }
ItemStock* MagicList(ClassTemplate* t) { return &t->magic; }

// Every item of `list` back to `to`, then the strip scrolled to the last one.
void ReturnToStrip(CCampPages* camp, CParty* to, CParty* list, CUiItemStrip* strip) {
    CItem* last = camp->ReturnAll(to, list);
    strip->ScrollToItem(last);
}

// The made spell or item replaces the results: every result removed, the made one added
// and released.
void TakeMade(CParty* results, CItem*& made) {
    while (ItemCount(results) != 0) results->RemoveItem(ItemAt(results, 0), 0);
    results->AddItem(made, 0, 0);
    made->Release();
    made = nullptr;
}

// Every result moved back: made items to the constructor's first list and strip, the others to the
// player and the item strip.
void ReturnResults(CCampPages* camp, CParty* results, CParty* constructorList, CUiItemStrip* constructorStrip) {
    while (ItemCount(results) != 0) {
        CItem* item = ItemAt(results, 0);
        bool made = MadeByConstructor(item);
        CParty* to = made ? constructorList : PlayerParty();
        CItem* moved = camp->CItemScreen::MoveItem(item, to, results, 0, 1, 0);
        (made ? constructorStrip : camp->itemStrip_)->ScrollToItem(moved);
    }
}

// A new item named `name` from the results' item manager, counted.
CItemLoot* NewLoot(CParty* party, const char* name) {
    auto* item = AsLoot(party->ItemFactory()->CreateItem(name));
    ++item->refCount_;
    return item;
}
void AddAndRelease(CParty* party, CItemLoot* item) {
    party->AddItem(item, 0, 0);
    item->Release();
}

// The instruction (blueprint) items by the item's class: weapon, armour, quick item.
const char* InstructionName(int itemClass) {
    switch (itemClass) {
    case kItemWeapon: return "Instruction Weapon";
    case kItemArmor: return "Instruction Armor";
    case kItemQuick: return "Instruction Quick Item";
    default: return nullptr;
    }
}
const char* MaterialText() { return "material"; }
const char* SpellContainerText() { return "spell container"; }
// The quick item prototypes by name: QuickItemTable::Named (inserts a missing name).

// A new item named `name` from the results' item manager added to them (when one is made).
void AddNamed(CParty* results, std::string_view name, std::uint32_t prototypeIndex, bool setIndex) {
    auto* item = AsLoot(results->ItemFactory()->CreateItem(name));
    if (!item) return;
    ++item->refCount_;
    if (setIndex) item->index_ = prototypeIndex;
    AddAndRelease(results, item);
}

// The spell constructor's third list: each item repaired, its spell's (container), spell's or
// modifier's record added to the template's spell stock.
void StockSpells(CCampPages* camp, CParty* list) {
    ItemStock* stock = MagicList(camp->constructor_);
    for (int i = 0; i < ItemCount(list); ++i) {
        CItem* item = ItemAt(list, i);
        item->Repair();
        if (static_cast<std::uint8_t>(UiItemKinds::IsSpellContainer(item))) {
            const char* name = item->spell_->GetPrototype()->code.c_str();
            stock->AddSpell(g_spellDatabase->spells.Named(name));
        } else if (UiItemKinds::IsSpell(item)) {
            std::uint32_t index = AsLoot(item)->index_;
            stock->AddSpell(itemdb::SpellAt(index));
        } else if (UiItemKinds::IsModifier(item)) {
            std::uint32_t index = AsLoot(item)->index_;
            stock->AddModifier(itemdb::SpellModifierAt(index));
        }
    }
}

// The item constructor's third list, from the end: each item repaired; weapons, armour and quick
// items that can be taken apart and blueprint parts add their record to the template's stock;
// loot priced by itself (kLootSelf) leaves the list.
void StockItems(CCampPages* camp, CParty* list) {
    ItemStock* stock = AllowedList(camp->constructor_);
    for (int i = ItemCount(list) - 1; i >= 0; --i) {
        CItem* item = ItemAt(list, i);
        item->Repair();
        int kind = -1;
        std::uint32_t index = 0;
        if (UiItemKinds::IsBuildable(item)) {
            index = PrototypeIndex(item);
            if (item->type_ == kItemWeapon) kind = kRecordWeapon;
            else if (item->type_ == kItemArmor) kind = kRecordArmor;
            else if (item->type_ == kItemQuick) kind = kRecordQuick;
        } else if (UiItemKinds::IsPart(item)) {
            auto* loot = AsLoot(item);
            index = loot->index_;
            if (loot->recordType_ <= kRecordQuick) kind = static_cast<int>(loot->recordType_);
        } else if (item->type_ == kItemLoot && AsLoot(item)->kind_ == kLootSelf) {
            list->RemoveItem(item, 1);
        }
        if (kind == static_cast<int>(kRecordWeapon))
            stock->AddWeapon(itemdb::WeaponAt(index));
        else if (kind == static_cast<int>(kRecordArmor))
            stock->AddArmor(itemdb::ArmorAt(index));
        else if (kind == static_cast<int>(kRecordQuick))
            stock->AddQuickItem(itemdb::QuickItemAt(index));
    }
}

void RepairAll(CParty* list) {
    for (int i = 0; i < ItemCount(list); ++i) ItemAt(list, i)->Repair();
}

} // namespace

// --- a spell container taken apart ---------------------------------------------------------------

bool CCampPages::DeconstructSpell() {
    CParty* results = spellResults_;
    CItem* copy = results->CopyItem(ItemAt(results, 0));
    spellMade_ = copy;
    ++copy->refCount_;
    results->RemoveItem(ItemAt(results, 0), 0);
    auto* spell = copy->spell_;

    CItemLoot* prototype = NewLoot(results, kPrototypeItem);
    prototype->index_ = PrototypeIndex(spell);
    AddAndRelease(results, prototype);
    for (int i = 0; i < spell->GetModifierCount(); ++i) {
        CItemLoot* rune = NewLoot(results, kRuneItem);
        rune->index_ = spell->modifiers_[i] - 1;
        AddAndRelease(results, rune);
    }
    spellMade_->Release();
    spellMade_ = nullptr;
    return true;
}
// --- an item taken apart -------------------------------------------------------------------------

bool CCampPages::DeconstructItem() {
    CParty* results = itemResults_;
    CItem* copy = results->CopyItem(ItemAt(results, 0));
    itemMade_ = copy;
    ++copy->refCount_;
    results->RemoveItem(ItemAt(results, 0), 0);

    if (const char* instruction = InstructionName(copy->type_))
        AddNamed(results, instruction, PrototypeIndex(copy), true);
    // A material per one the prototype needs; a material index out of range reads its key through null.
    for (std::uint32_t i = 0; i < copy->GetPrototype()->materialUnits; ++i) {
        const itemdb::MaterialRecordHead* material = itemdb::MaterialAt(MaterialIndex(copy->prototypeId_));
        // The text is "material" + '.' + key (two concatenations).
        std::string name = MaterialText();
        name += ".";
        name += material->key;
        AddNamed(results, name, 0, false);
    }
    if (copy->spell_) {
        CItemManager* manager = results->ItemFactory();
        itemdb::QuickItemTable& quickItems = g_itemDatabase->quickItems;
        int index = quickItems.IndexOf(quickItems.Named(SpellContainerText()));
        auto* container = manager->MakeItem(kItemSpell, index, 0, -1);
        if (container) {
            ++container->refCount_;
            CSpell* clone = copy->spell_->Clone(results->SpellManager());
            container->AttachSpell(clone);
            results->AddItem(container, 0, 0);
            container->Release();
        }
    }
    itemMade_->Release();
    itemMade_ = nullptr;
    return true;
}

// --- the first action button ---------------------------------------------------------------------

void CCampPages::DoAction() {
    interface_->PlaySound(kBuySound, 0);
    switch (page_) {
    case kEquipment:
        if (itemConstructor_ && !noConstructor_) {
            PartyUnitRecord* hero = HeroRecord();
            for (int i = 0; i < std::ssize(hero->weapons); ++i) hero->weapons[i]->Repair();
            for (int i = 0; i < std::ssize(hero->armor); ++i) hero->armor[i]->Repair();
        }
        break;
    case kTrade:
        trade_->Settle(1, 0);
        return;
    case kSpellTrade:
        constructor_->SpellsParty()->SpellManager();
        StockSpells(this, spellParts_[2]);
        ReturnToStrip(this, spellParts_[0], spellParts_[2], spellStrip_);
        ReturnToStrip(this, PlayerParty(), spellParts_[1], itemStrip_);
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
        ReturnToStrip(this, PlayerParty(), itemParts_[1], itemStrip_);
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
        ReturnToStrip(this, PlayerParty(), itemExtra_, itemStrip_);
        break;
    default:
        break;
    }
    PlayerParty()->money_ -= cost_;
}

// --- the second action button --------------------------------------------------------------------

void CCampPages::UndoAction() {
    interface_->PlaySound(kCancelSound, 0);
    switch (page_) {
    case kTrade:
        trade_->Settle(0, 0);
        return;
    case kSpellTrade:
        ReturnToStrip(this, spellParts_[0], spellParts_[1], spellStrip_);
        ReturnToStrip(this, PlayerParty(), spellParts_[2], itemStrip_);
        break;
    case kSpellMaker:
        ReturnResults(this, spellResults_, spellParts_[0], spellStrip_);
        break;
    case kItemTrade:
        ReturnToStrip(this, itemParts_[0], itemParts_[1], partStrip_);
        ReturnToStrip(this, PlayerParty(), itemParts_[2], itemStrip_);
        break;
    case kItemMaker:
        ReturnResults(this, itemResults_, itemParts_[0], partStrip_);
        break;
    case kRepair:
        ReturnToStrip(this, PlayerParty(), itemExtra_, itemStrip_);
        break;
    default:
        break;
    }
}
