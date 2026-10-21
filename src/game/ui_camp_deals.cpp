// The camp's two action buttons (00611220 / 00611760) and taking a spell container apart
// (00612500). See docs/notes/ui.md, section 5.
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

#include "ei/hook.h"
#include "lib/stlp_hash.h"
#include "originals_gameplay.h"
#include "originals_ui.h"
#include "game_data.h"

#include <cstdint>

namespace {

const char* const kBuySound = "buttons\\camp\\buy.wav";
const char* const kCancelSound = "buttons\\messbox\\cancel.wav";
const char* const kPrototypeItem = "Prototype";
const char* const kRuneItem = "Rune";
// The spell prototypes (their by-name lookup 00613920 is a thiscall on the table).
void* SpellTable() { return ei::GamePointer<void>(0x007C8510); }

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
constexpr std::uint32_t kKindOwnPrice = 0;
constexpr std::uint32_t kRecordWeapon = 0;
constexpr std::uint32_t kRecordArmor = 1;
constexpr std::uint32_t kRecordQuickItem = 2;
constexpr int kLootClass = 0x3007;
constexpr int kWeaponClass = 0x3004;
constexpr int kArmorClass = 0x3005;
constexpr int kQuickItemClass = 0x3006;

CParty* PartyOf(void* list) { return static_cast<CParty*>(list); }
CParty* PlayerParty() { return reinterpret_cast<CParty*>(LocalPlayerData()); }
CItem* ItemAt(CParty* party, int index) { return party->items_.data[index]; }
int ItemCount(const CParty* party) { return party->items_.size; }
// The prototype index an item's packed id carries (its high word).
std::uint32_t PrototypeIndex(const CEngineObject* object) { return object->prototypeId_ >> 16; }
// An item record (+0x1C) marked as made by a constructor (+0x10).
bool MadeByConstructor(const CItem* item) {
    auto* record = reinterpret_cast<const std::uint8_t*>(item->link_);
    return record && record[0x10];
}

// The stock lists of a class template: everything (+0x11C) and spells and modifiers (+0x1D4).
void* AllowedList(ClassTemplate* t) { return &t->allowed; }
void* MagicList(ClassTemplate* t) { return &t->magic; }

// Every item of `list` back to `to`, then the strip scrolled to the last one.
void ReturnToStrip(CCampPages* camp, void* to, void* list, CUiItemStrip* strip) {
    CItem* last = camp->ReturnAll(static_cast<CParty*>(to), static_cast<CParty*>(list));
    strip->ScrollToItem(last);
}

// The made spell or item replaces the results: every result removed, the made one added
// and released.
void TakeMade(CParty* results, CEngineObject*& made) {
    while (ItemCount(results) != 0) results->RemoveItem(ItemAt(results, 0), 0);
    results->AddItem(static_cast<CItem*>(made), 0, 0);
    made->Release();
    made = nullptr;
}

// Every result moved back: made items to the constructor's first list and strip, the others to the
// player and the item strip.
void ReturnResults(CCampPages* camp, CParty* results, void* constructorList, CUiItemStrip* constructorStrip) {
    while (ItemCount(results) != 0) {
        CItem* item = ItemAt(results, 0);
        bool made = MadeByConstructor(item);
        void* to = made ? constructorList : static_cast<void*>(PlayerParty());
        CItem* moved = static_cast<CItemScreen*>(camp)->MoveItem(item, static_cast<CParty*>(to), results, 0, 1, 0);
        static_cast<CUiItemStrip*>(made ? constructorStrip : camp->itemStrip_)->ScrollToItem(moved);
    }
}

} // namespace

// --- 00612500: a spell container taken apart ---------------------------------------------------------

namespace {
// A new item named `name` from the results' item manager, counted.
CItemLoot* NewLoot(CParty* party, const char* name) {
    auto* item = static_cast<CItemLoot*>(ItemManagerCreateItemOriginal(party->ItemManager(), nullptr, name));
    ++item->refCount_;
    return item;
}
void AddAndRelease(CParty* party, CItemLoot* item) {
    party->AddItem(item, 0, 0);
    item->Release();
}
} // namespace

bool CCampPages::DeconstructSpell() {
    CParty* results = PartyOf(spellResults_);
    CItem* copy = results->CopyItem(ItemAt(results, 0));
    spellMade_ = reinterpret_cast<std::uint8_t*>(copy);
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
    reinterpret_cast<CEngineObject*>(spellMade_)->Release();
    spellMade_ = nullptr;
    return true;
}
// --- 006129C0: an item taken apart -------------------------------------------------------------------

namespace {
// The instruction (blueprint) items by the item's class: weapon, armour, quick item.
const char* InstructionName(int itemClass) {
    switch (itemClass) {
    case kWeaponClass: return "Instruction Weapon";
    case kArmorClass: return "Instruction Armor";
    case kQuickItemClass: return "Instruction Quick Item";
    default: return nullptr;
    }
}
const char* MaterialText() { return "material"; }
const char* SpellContainerText() { return "spell container"; }
// The quick item prototypes by name (an STLport map; 00613820 inserts a missing name).
void* QuickItemsByName() { return ei::GamePointer<void>(0x007C8744); }
constexpr int kSpellContainerClass = 0x3008;

// A new item named `name` from the results' item manager added to them (when one is made).
void AddNamed(CParty* results, const char* name, std::uint32_t prototypeIndex, bool setIndex) {
    auto* item = static_cast<CItemLoot*>(ItemManagerCreateItemOriginal(results->ItemManager(), nullptr, name));
    if (!item) return;
    ++item->refCount_;
    if (setIndex) item->index_ = prototypeIndex;
    AddAndRelease(results, item);
}
} // namespace

bool CCampPages::DeconstructItem() {
    CParty* results = PartyOf(itemResults_);
    CItem* copy = results->CopyItem(ItemAt(results, 0));
    itemMade_ = reinterpret_cast<std::uint8_t*>(copy);
    ++copy->refCount_;
    results->RemoveItem(ItemAt(results, 0), 0);

    if (const char* instruction = InstructionName(copy->type_))
        AddNamed(results, instruction, PrototypeIndex(copy), true);
    // A material per one the prototype needs; a material index out of range reads its key through
    // null, as in the original.
    for (std::uint32_t i = 0; i < reinterpret_cast<const itemdb::ItemRecordHead*>(copy->GetPrototype())->materialUnits; ++i) {
        const itemdb::MaterialRecordHead* material = itemdb::MaterialAt(MaterialIndex(copy->prototypeId_));
        // The original builds CString("material") + '.' + key (operator+ twice); the text is the same.
        mfc::CString name(MaterialText());
        name += ".";
        name += material->key;
        AddNamed(results, name, 0, false);
    }
    if (copy->spell_) {
        CResourceManager* manager = results->ItemManager();
        auto* record = static_cast<const char*>(stlp::RecordNamed(static_cast<stlp::RecordsByName*>(QuickItemsByName()), nullptr, SpellContainerText()));
        int index = static_cast<int>(record - itemdb::QuickItems().begin) / itemdb::kQuickItemSize;
        auto* container = static_cast<CItemManager*>(manager)->MakeItem(kSpellContainerClass, index, 0, -1);
        if (container) {
            ++container->refCount_;
            CSpell* clone = copy->spell_->Clone(results->SpellManager());
            container->AttachSpell(clone);
            results->AddItem(container, 0, 0);
            container->Release();
        }
    }
    reinterpret_cast<CEngineObject*>(itemMade_)->Release();
    itemMade_ = nullptr;
    return true;
}
EI_REPLACE_METHOD(0x00612500, CCampPages::DeconstructSpell);
EI_REPLACE_METHOD(0x006129C0, CCampPages::DeconstructItem);

// --- 00611220: the first action button ---------------------------------------------------------

namespace {

// The spell constructor's third list: each item repaired, its spell's (container), spell's or
// modifier's record added to the template's spell stock.
void StockSpells(CCampPages* camp, CParty* list) {
    void* stock = MagicList(camp->constructor_);
    for (int i = 0; i < ItemCount(list); ++i) {
        CItem* item = ItemAt(list, i);
        item->Repair();
        if (static_cast<std::uint8_t>(UiItemKinds::IsSpellContainer(item))) {
            char* prototype = item->spell_->GetPrototype();
            const char* name = *reinterpret_cast<const char* const*>(prototype + 4);
            static_cast<ItemStock*>(stock)->AddSpell(static_cast<char*>(static_cast<char*>(stlp::RecordNamed(static_cast<stlp::RecordsByName*>(SpellTable()), nullptr, name))));
        } else if (UiItemKinds::IsSpell(item)) {
            std::uint32_t index = static_cast<CItemLoot*>(item)->index_;
            static_cast<ItemStock*>(stock)->AddSpell(itemdb::Spells().RecordAt(index, itemdb::kSpellSize));
        } else if (UiItemKinds::IsModifier(item)) {
            std::uint32_t index = static_cast<CItemLoot*>(item)->index_;
            static_cast<ItemStock*>(stock)->AddModifier(itemdb::SpellModifiers().RecordAt(index, itemdb::kSpellModifierSize));
        }
    }
}

// The item constructor's third list, from the end: each item repaired; weapons, armour and quick
// items that can be taken apart and blueprint parts add their record to the template's stock;
// loot priced by itself (kind 0) leaves the list.
void StockItems(CCampPages* camp, CParty* list) {
    void* stock = AllowedList(camp->constructor_);
    for (int i = ItemCount(list) - 1; i >= 0; --i) {
        CItem* item = ItemAt(list, i);
        item->Repair();
        int kind = -1;
        std::uint32_t index = 0;
        if (UiItemKinds::IsBuildable(item)) {
            index = PrototypeIndex(item);
            if (item->type_ == kWeaponClass) kind = kRecordWeapon;
            else if (item->type_ == kArmorClass) kind = kRecordArmor;
            else if (item->type_ == kQuickItemClass) kind = kRecordQuickItem;
        } else if (UiItemKinds::IsPart(item)) {
            auto* loot = static_cast<CItemLoot*>(item);
            index = loot->index_;
            if (loot->recordType_ <= kRecordQuickItem) kind = static_cast<int>(loot->recordType_);
        } else if (item->type_ == kLootClass && static_cast<CItemLoot*>(item)->kind_ == kKindOwnPrice) {
            list->RemoveItem(item, 1);
        }
        if (kind == static_cast<int>(kRecordWeapon))
            static_cast<ItemStock*>(stock)->AddWeapon(itemdb::Weapons().RecordAt(index, itemdb::kWeaponSize));
        else if (kind == static_cast<int>(kRecordArmor))
            static_cast<ItemStock*>(stock)->AddArmor(itemdb::Armors().RecordAt(index, itemdb::kArmorSize));
        else if (kind == static_cast<int>(kRecordQuickItem))
            static_cast<ItemStock*>(stock)->AddQuickItem(itemdb::QuickItems().RecordAt(index, itemdb::kQuickItemSize));
    }
}

void RepairAll(CParty* list) {
    for (int i = 0; i < ItemCount(list); ++i) ItemAt(list, i)->Repair();
}

} // namespace

void CCampPages::DoAction() {
    interface_->PlaySound(kBuySound, 0);
    switch (page_) {
    case kEquipment:
        if (itemConstructor_ && !noConstructor_) {
            using HeroFn = PartyUnitRecord*(__thiscall*)(CCampPages*);
            PartyUnitRecord* hero = ei::VirtualSlot<HeroFn>(vtable_, 51)(this);
            for (int i = 0; i < static_cast<int>(hero->weaponCount); ++i) hero->weapons[i]->Repair();
            for (int i = 0; i < static_cast<int>(hero->armorCount); ++i) hero->armor[i]->Repair();
        }
        break;
    case kTrade:
        trade_->Settle(1, 0);
        return;
    case kSpellTrade:
        reinterpret_cast<CParty*>(constructor_->spells)->SpellManager();
        StockSpells(this, PartyOf(spellParts_[2]));
        ReturnToStrip(this, spellParts_[0], spellParts_[2], spellStrip_);
        ReturnToStrip(this, PlayerParty(), spellParts_[1], itemStrip_);
        break;
    case kSpellMaker:
        if (deconstructing_) {
            DeconstructSpell();
        } else if (spellMade_) {
            auto* made = reinterpret_cast<CEngineObject*>(spellMade_);
            TakeMade(PartyOf(spellResults_), made);
            spellMade_ = nullptr;
        }
        break;
    case kItemTrade:
        reinterpret_cast<CParty*>(constructor_->items)->ItemManager();
        StockItems(this, PartyOf(itemParts_[2]));
        ReturnToStrip(this, itemParts_[0], itemParts_[2], partStrip_);
        ReturnToStrip(this, PlayerParty(), itemParts_[1], itemStrip_);
        break;
    case kItemMaker:
        if (deconstructing_) {
            DeconstructItem();
        } else if (itemMade_) {
            auto* made = reinterpret_cast<CEngineObject*>(itemMade_);
            TakeMade(PartyOf(itemResults_), made);
            itemMade_ = nullptr;
        }
        break;
    case kRepair:
        RepairAll(PartyOf(itemExtra_));
        ReturnToStrip(this, PlayerParty(), itemExtra_, itemStrip_);
        break;
    default:
        break;
    }
    PlayerParty()->Money() -= cost_;
}
EI_REPLACE_METHOD(0x00611220, CCampPages::DoAction);

// --- 00611760: the second action button --------------------------------------------------------

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
        ReturnResults(this, PartyOf(spellResults_), spellParts_[0], spellStrip_);
        break;
    case kItemTrade:
        ReturnToStrip(this, itemParts_[0], itemParts_[1], partStrip_);
        ReturnToStrip(this, PlayerParty(), itemParts_[2], itemStrip_);
        break;
    case kItemMaker:
        ReturnResults(this, PartyOf(itemResults_), itemParts_[0], partStrip_);
        break;
    case kRepair:
        ReturnToStrip(this, PlayerParty(), itemExtra_, itemStrip_);
        break;
    default:
        break;
    }
}
