// The camp (CampPages, ui_camp.h): the page switch and the camp panels' layout code, its Create, the two action
// buttons, taking a spell container apart, and its input (buttons, keys) with the message-result and help-topic slots.
// Rebirth's WideScreen replaces Create with the game's version followed by two curtain sprites over the screen's margins
// (screen_layout::AddCurtains), which this reproduces. See docs/notes/ui.md, sections 3 and 5.
#include "game/ui/ui_camp.h"
#include "game/players/class_templates.h"
#include "game/app/singletons.h"
#include "game/items/item.h"
#include "game/items/item_db.h"
#include "game/spells/spell_db.h"
#include "game/items/item_manager.h"
#include "game/items/item_stock.h"
#include "game/objects/object3d.h"
#include "game/map/map_database.h"
#include "game/players/party.h"
#include "game/players/player_client.h"
#include "engine/core/random.h"
#include "engine/data/resource_manager.h"
#include "game/ui/screen_layout.h"
#include "game/spells/spell.h"
#include "engine/data/text_manager.h"
#include "engine/scene/scene_renderer.h"
#include "engine/models/texture.h"
#include "game/ui/ui.h"
#include "game/sound/sound.h"
#include "game/players/party_manager.h"
#include "game/ui/ui_cursor.h"
#include "game/ui/ui_item_info.h"
#include "game/ui/ui_mesh.h"
#include "game/ui/ui_message_box.h"
#include "game/ui/ui_screens.h"
#include "game/units/unit.h"
#include "game/units/unit_create.h"
#include "game/objects/map_object_manager.h"
#include "game/world/world.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/rounding.h"
#include "lib/str.h"
#include "lib/text_case.h"
#include "platform/platform.h"
#include "render/render_state.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <format>
#include <initializer_list>
#include <optional>
#include <string>

namespace {
constexpr const char* kPutOnSound = "buttons\\camp\\put_on.wav";
constexpr const char* kPutOffSound = "buttons\\camp\\put_off.wav";

// `results` is re-read every time: a made item goes to `owner` and into `strip`, the others to the local
// player and into the item strip.
void TakeResults(CampPages* camp, Party* const& results, Party* owner, UiItemStrip* strip) {
    while (!results->items.empty()) {
        Item* item = results->items[0];
        if (IsMade(item))
            strip->ScrollToItem(camp->MoveItem(item, owner, results, 0, true, false));
        else
            camp->itemStrip->ScrollToItem(camp->MoveItem(item, LocalPlayerData(), results, 0, true, false));
    }
}

// 1 full, 0.5 half.
void SetShade(UiMesh* mesh, float shade) {
    mesh->tint[0] = shade;
    mesh->tint[1] = shade;
    mesh->tint[2] = shade;
}

// A white centred line of a surface's labels.
void AddLine(std::vector<UiLabel>& labels, std::string_view text, const Rect& rect) {
    labels.push_back({std::string(text), rect, {1, 0xFFFFFF, render::text::Format::Center}});
}

// The database records of the camp's loot items (the record index): spells, spell modifiers and materials; the
// camp's predicates (IsSpell, IsModifier, IsPart, IsMaterial) admit only items whose record exists.
std::uint32_t Index(const Item* item) { return item->AsLoot()->index; }
// The loot's record type: the record table it indexes.
LootRecordType RecordType(const Item* item) { return item->AsLoot()->recordType; }
itemdb::SpellRecord* SpellRecordOf(const Item* item) { return itemdb::SpellAt(Index(item)); }
itemdb::SpellModifierRecord* ModifierRecordOf(const Item* item) { return itemdb::SpellModifierAt(Index(item)); }
itemdb::MaterialRecordHead* BlueprintRecord(const Item* item) { return itemdb::MaterialAt(Index(item)); }
// A part's prototype record by the loot's record type: weapon, armour, quick item.
itemdb::ItemRecordHead* PartRecord(const Item* item, LootRecordType recordType) {
    switch (recordType) {
    case LootRecordType::Weapon: return itemdb::WeaponAt(Index(item));
    case LootRecordType::Armor: return itemdb::ArmorAt(Index(item));
    case LootRecordType::Quick: return itemdb::QuickItemAt(Index(item));
    default: return nullptr;
    }
}

constexpr float kPriceZero = 0.0f;
constexpr float kPriceOne = 1.0f;

// The item's base price.
float ItemPrice(Item* item) {
    return item->GetPrice();
}

// How worn the item is: 1 - durability / maximum, a negative (or NaN) durability or maximum taken as 0 and the
// result clamped to [0, 1] (NaN stays; Rebirth's FixRepairPrice, the original's had neither).
float WornFraction(const Item* item) {
    float durability = item->condition, maximum = item->conditionMax;
    float a = kPriceZero < durability ? durability : kPriceZero;
    float b = kPriceZero < maximum ? maximum : kPriceZero;
    float worn = kPriceOne - a / b;
    if (worn > kPriceOne) return kPriceOne;
    if (worn < kPriceZero) return kPriceZero;
    return worn;
}

// The stack count of an item.
int Count(const Item* item) { return static_cast<int>(item->count); }

constexpr std::uint32_t kRecordBias = 100000000;

// The hero's statistics with the armour it wears as their outer layers again.
void RebuildArmourLayers(PartyUnitRecord* hero) {
    UnitStats* stats = &hero->stats;
    stats->ClearOuterLayers();
    for (Item* worn : hero->armor) stats->AddArmourLayer(worn->AsArmor());
}

// Whether `list` holds `item`.
bool ListHolds(Party* list, const Item* item) {
    return std::ranges::find(list->items, item) != list->items.end();
}

// fmod(turn + sign * 2 * seconds, 2 pi).
constexpr float kCampFullTurn = /*007C7EE8*/ 6.2831855f;
float TurnedBy(float seconds, float turn, bool backwards) {
    const float twice = seconds + seconds;
    const float turned = backwards ? turn - twice : twice + turn;
    return std::fmod(turned, kCampFullTurn);
}

// From the current hero, step until a record of the hero class (the game ends after more steps than there are
// records).
template <typename Step>
int FindHero(int start, Step step) {
    Party* party = LocalPlayerData();
    int i = start;
    for (int tries = 0;;) {
        int count = lib::Count(party->members);
        if (tries > count) game::FatalExit("ERROR: no human personages in the party.\n");
        i = step(i, count);
        ++tries;
        PartyUnitRecord* record = &party->members[i];
        if (PrototypeObjectClass(record->prototype) == MapObjectType::Hero) break;
    }
    return i;
}

constexpr SkillAnimation kIdleAnimation = SkillAnimation::Special14;    // what the heroes play in the camp
constexpr int kPriceWeights = 9;

// The pages (CampPages::page) and the strips' action modes.
constexpr int kPageEquipment = 0;
constexpr int kPageSpellConstructor = 2;
constexpr int kPageItemConstructor = 4;
constexpr int kActionEquip = 1, kActionSpellParts = 2, kActionItemParts = 3;
constexpr int kModeSpellTrade = 4, kModeItemTrade = 5;
constexpr int kFilterItems = 0, kFilterSpells = 1, kFilterArmour = 2;

// Whether the local player's variable `name` is 1. The original also takes an
// unordered result (a NaN variable) as equal.
bool PlayerSwitch(const char* name) { return LocalPlayerVariable(name) == 1.0f; }

// An item list of the constructors: made through its factory (not checked).
Party* NewItemList() {
    Party* list = new Party();
    list->Create();
    return list;
}

// The time in animation frames: milliseconds / 1024 * 15 (a double: the milliseconds since the system started).
double FramesNow() { return platform::FrameClock() * (15.0 / 1024.0); }

const char* const kBuySound = "buttons\\camp\\buy.wav";
const char* const kPrototypeItem = "Prototype";
const char* const kRuneItem = "Rune";

// Pages of the action buttons.
enum class CampPage : int {
    Equipment = 0,
    Trade = 1,
    SpellTrade = 2,
    SpellMaker = 3,
    ItemTrade = 4,
    ItemMaker = 5,
    Repair = 6,
};

// The prototype index an item's packed id carries (its high word).
std::uint32_t PrototypeIndex(const EngineObject* object) { return object->prototypeId >> 16; }

// Every item of `list` back to `to`, then the strip scrolled to the last one.
void ReturnToStrip(CampPages* camp, Party* to, Party* list, UiItemStrip* strip) {
    Item* last = camp->ReturnAll(to, list);
    strip->ScrollToItem(last);
}

// The made spell or item replaces the results: every result removed, the made one added and dropped.
void TakeMade(Party* results, Ref<Item>& made) {
    while (!results->items.empty()) results->RemoveItem(results->items[0], false);
    results->AddItem(made, false, false);
    made.Reset();
}

// Every result moved back: made items to the constructor's first list and strip, the others to the
// player and the item strip.
void ReturnResults(CampPages* camp, Party* results, Party* constructorList, UiItemStrip* constructorStrip) {
    while (!results->items.empty()) {
        Item* item = results->items[0];
        bool made = IsMade(item);
        Party* to = made ? constructorList : LocalPlayerData();
        Item* moved = camp->MoveItem(item, to, results, 0, true, false);
        (made ? constructorStrip : camp->itemStrip)->ScrollToItem(moved);
    }
}

// A new item named `name` from the results' item manager as loot (null when none is made).
ItemLoot* NewLoot(Party* results, std::string_view name) {
    Item* item = results->ItemFactory()->CreateItem(name);
    return item ? item->AsLoot() : nullptr;
}
// `item` (not checked for null) added to the results, counted while it is added; its record index set first
// when `index` is given.
void AddLoot(Party* results, ItemLoot* item, std::optional<std::uint32_t> index) {
    item->AddRef();
    if (index) item->index = *index;
    results->AddItem(item, false, false);
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
void StockSpells(CampPages* camp, Party* list) {
    ItemStock* stock = &camp->constructor->magic;
    for (Item* item : list->items) {
        item->Repair();
        if (IsSpellContainer(item))
            stock->AddSpell(g_spellDatabase.spells.Named(item->spell->GetPrototype()->code));
        else if (IsSpell(item))
            stock->AddSpell(itemdb::SpellAt(item->AsLoot()->index));
        else if (IsModifier(item))
            stock->AddModifier(itemdb::SpellModifierAt(item->AsLoot()->index));
    }
}

// The item constructor's third list, from the end: each item repaired; weapons, armour and quick
// items that can be taken apart and blueprint parts add their record to the template's stock;
// loot priced by itself (LootKind::Self) leaves the list.
void StockItems(CampPages* camp, Party* list) {
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
            auto* loot = item->AsLoot();
            index = loot->index;
            if (loot->recordType <= LootRecordType::Quick) kind = loot->recordType;
        } else if (item->Kind() == ItemKind::Loot && item->AsLoot()->kind == LootKind::Self) {
            list->RemoveItem(item, true);
        }
        if (kind == LootRecordType::Weapon)
            stock->AddWeapon(itemdb::WeaponAt(index));
        else if (kind == LootRecordType::Armor)
            stock->AddArmor(itemdb::ArmorAt(index));
        else if (kind == LootRecordType::Quick)
            stock->AddQuickItem(itemdb::QuickItemAt(index));
    }
}

void RepairAll(Party* list) {
    for (Item* item : list->items) item->Repair();
}

constexpr const char* kMoveSound = "buttons\\camp\\move.wav";

// The help topics of pages 0-6 (the game's strings).
const char* PageHelpTopic(int page) {
    switch (page) {
    case 0: return "camp_weapons";
    case 1: return "camp_skills";
    case 2: return "camp_spell_trade";
    case 3: return "camp_spell_constr";
    case 4: return "camp_item_trade";
    case 5: return "camp_item_constr";
    case 6: return "camp_repair";
    default: return nullptr;
    }
}

constexpr int kEquipmentPage = 0;
constexpr int kLeavePage = 7;
constexpr std::uint32_t kAnswerYes = BoxResult(MessageQuestion::First, MessageAnswer::Ok);

constexpr std::uint32_t kInstructionCount = 10;
constexpr std::uint32_t kMaterialCount = 20;

// The list's copy of `item`: counted for the call, added (keeping its count, merged), released.
void AddCounted(Party* list, Item* item) {
    item->AddRef();
    list->AddItem(item, true, true);
    item->Release();
}

// An item named `name` from the list's item manager (not checked for null) with
// record index `index` and count `count`, added.
void AddNamed(Party* list, const char* name, std::uint32_t index, std::uint32_t count) {
    auto* item = list->ItemFactory()->CreateItem(name)->AsLoot();
    item->index = index;
    item->count = count;
    AddCounted(list, item);
}

// One item of class `kind` for `record` in every material whose soundName names the record's material class,
// made by the list's item constructor with `manager`; `materials` is the material table's count.
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

void CampPages::ShowPage(int newPage) {
    equipment->SetEnabled(false);
    equipment2->SetEnabled(false);
    trade->SetEnabled(false);
    statsPanel->SetEnabled(false);
    helpPanel->SetEnabled(false);
    if (spellConstructor) {
        spellConstructor1->SetEnabled(false);
        spellConstructor2->SetEnabled(false);
        spellStrip->SetEnabled(false);
    }
    if (itemConstructor) {
        itemConstructor1->SetEnabled(false);
        itemConstructor3->SetEnabled(false);
        itemConstructor2->SetEnabled(false);
        partStrip->SetEnabled(false);
    }
    itemStrip->priceKind = PriceKind::None;

    bool offered = true;
    switch (newPage) {
    case 0:
        statsPanel->SetEnabled(true);
        equipment->SetEnabled(true);
        break;
    case 1:
        statsPanel->SetEnabled(true);
        equipment2->SetEnabled(true);
        trade->SetEnabled(true);
        break;
    case 2:
        if (!spellConstructor) { offered = false; break; }
        helpPanel->SetEnabled(true);
        helpPanel->ShowHelp(0x65);
        spellConstructor1->SetEnabled(true);
        spellStrip->SetEnabled(true);
        itemStrip->priceKind = PriceKind::SpellSell;
        break;
    case 3:
        if (!spellConstructor || noConstructor) { offered = false; break; }
        helpPanel->SetEnabled(true);
        spellConstructor2->SetEnabled(true);
        spellStrip->SetEnabled(true);
        break;
    case 4:
        if (!itemConstructor) { offered = false; break; }
        helpPanel->SetEnabled(true);
        helpPanel->ShowHelp(0x64);
        itemConstructor1->SetEnabled(true);
        partStrip->SetEnabled(true);
        itemStrip->priceKind = PriceKind::ItemSell;
        break;
    case 5:
        if (!itemConstructor || noConstructor) { offered = false; break; }
        helpPanel->SetEnabled(true);
        itemConstructor2->SetEnabled(true);
        partStrip->SetEnabled(true);
        break;
    case 6:
        if (!itemConstructor) { offered = false; break; }
        helpPanel->SetEnabled(true);
        helpPanel->ShowHelp(0x68);
        itemConstructor3->SetEnabled(true);
        partStrip->SetEnabled(true);
        break;
    case 7: {
        // Leave: the trade is settled, the constructors' unused parts go back to their owners and
        // what they made goes into the strips.
        trade->Settle(false, true);
        ReturnAll(spellParts[0], spellParts[1]);
        ReturnAll(LocalPlayerData(), spellParts[2]);
        ReturnAll(itemParts[0], itemParts[1]);
        ReturnAll(LocalPlayerData(), itemParts[2]);
        ReturnAll(LocalPlayerData(), itemExtra);
        TakeResults(this, spellResults, spellParts[0], spellStrip);
        TakeResults(this, itemResults, itemParts[0], partStrip);
        g_interface.PlayInterfaceSound("buttons\\globalmap\\transit.wav");
        if (fromHeroScreen) {
            g_interface.SetPendingValue(0);
            return;
        }
        g_interface.closeAll = 1;
        g_clientWorld.LocalView()->Rejoin();
        return;
    }
    default:
        break;
    }

    if (!offered) {
        ShowPage(page);    // the page is not offered: show the current one again
    } else {
        if (page != newPage) g_interface.PlayInterfaceSound("buttons\\camp\\slot.wav");
        page = newPage;
        statsPanel->ShowStats(newPage);
        trade->SelectHero(hero);
        titleLabels.clear();
        AddLine(titleLabels, pageTitles[page], Rect{0, 10, 200, 30});
    }

    // The action buttons only where a hero can be chosen.
    if (page == 0 && (!itemConstructor || noConstructor)) {
        actionIcon->visible = false;
        secondActionIcon->visible = false;
        actionButton->SetTip(0);
        secondActionButton->SetTip(0);
    } else {
        actionIcon->visible = true;
        secondActionIcon->visible = true;
        actionButton->SetTip(0x4EE8);
        secondActionButton->SetTip(0x4EE9);
    }
    // The page buttons half bright, the current one full.
    for (UiMesh* icon : pageIcons) SetShade(icon, 0.5f);
    SetShade(pageIcons[page], 1.0f);
    g_interface.OpenHelp(false);
}

int CampPages::Price(Item* item, PriceKind kind) {
    const float weight = priceWeights[static_cast<int>(kind)];
    const float repairWeight = priceWeights[static_cast<int>(PriceKind::Repair)];
    float repair = kPriceZero;
    ItemKind itemKind = item->Kind();
    const bool wears = itemKind == ItemKind::Weapon || itemKind == ItemKind::Armor;
    if (wears || kind == PriceKind::Repair) {
        float worn = WornFraction(item);
        repair = ItemPrice(item) * repairWeight * worn;
        if (kind == PriceKind::Repair) {
            if (worn == kPriceZero) return 0;
            if (repairWeight == kPriceZero) return 0;
            int price = static_cast<int>(repair);
            return price > 0 ? price : 1;
        }
    }
    if (wears) {
        // Taking a worn item apart pays its repair cost back instead of charging it (Rebirth's FixRepairPrice).
        if (kind == PriceKind::ItemApart) return static_cast<int>(ItemPrice(item) * weight + repair);
        return static_cast<int>(ItemPrice(item) * weight - repair);
    }
    return static_cast<int>(ItemPrice(item) * weight);
}

bool CampPages::CanEquip(Item* item, int mode) {
    switch (page) {
    case 0: {   // equipment: weapons, armour, spell containers (worn-out ones not to a hero)
        ItemKind itemKind = item->Kind();
        if ((itemKind == ItemKind::Weapon || itemKind == ItemKind::Armor) && lib::RoundEven(item->condition) <= 0 && mode == 1) return false;
        return itemKind == ItemKind::Weapon || itemKind == ItemKind::Armor || itemKind == ItemKind::Quick;
    }
    case 1: {   // trade: a spell container only for a hero who can cast its spell
        if (mode != 1) return true;
        if (!IsSpellContainer(item)) return false;
        Spell* spell = item->spell;
        PartyUnitRecord* heroRecord = HeroRecord();
        return HeroCanUseSpell(&heroRecord->stats, spell);

    }
    case 2:     // the spell constructor
        return IsSpellContainer(item) || IsSpell(item) || IsModifier(item);
    case 3: {   // the spell being built: a container, one spell, up to eight modifiers
        if (mode != 1 && mode != 2) return mode == 6;
        if (IsSpellContainer(item)) return std::ssize(spellResults->items) <= 0;
        if (IsSpell(item)) {
            if (std::ssize(spellResults->items) > 0) return false;
            const itemdb::SpellRecord* record = SpellRecordOf(item);
            PartySkill best = BestPartySkill(record->school);
            if (best.mana < static_cast<int>(record->mana)) return false;
            return best.skill >= record->skill;
        }
        if (!IsModifier(item)) return false;
        int modifiers = 0, skillSum = 0;
        float manaSum = 0.0f;
        bool hasSpell = false;
        SpellSchool school = SpellSchool::Fire;
        for (Item* slot : spellResults->items) {
            if (IsSpellContainer(slot)) return false;
            if (IsSpell(slot)) {
                hasSpell = true;
                const itemdb::SpellRecord* record = SpellRecordOf(slot);
                skillSum += record->skill;
                school = record->school;
                manaSum += record->mana;
                // The modifier must suit the spell (the spell's flag byte for the modifier's kind).
                std::uint32_t kind = ModifierRecordOf(item)->kind;
                if (kind < 5 && record->allowedModifiers[kind] == 0) return false;
            } else if (IsModifier(slot)) {
                ++modifiers;
                const itemdb::SpellModifierRecord* modifier = ModifierRecordOf(slot);
                manaSum += modifier->mana;
                skillSum += modifier->skill;
            }
        }
        if (!hasSpell || modifiers >= 8) return false;
        const itemdb::SpellModifierRecord* modifier = ModifierRecordOf(item);
        float manaTotal = manaSum + modifier->mana;
        int skillTotal = skillSum + modifier->skill;
        PartySkill best = BestPartySkill(school);
        if (best.skill < skillTotal) return false;
        return best.mana >= static_cast<int>(manaTotal);
    }
    case 4:     // the item constructor: anything but quest items and magic
        if (item->Kind() == ItemKind::Quest) return false;
        if (IsSpellContainer(item) || IsSpell(item)) return false;
        return !IsModifier(item);
    case 5: {   // the item being built: a blueprint, its parts, a container's spell
        if (mode != 1 && mode != 3) return mode == 7;
        if (IsPart(item) || IsBuildable(item)) return std::ssize(itemResults->items) <= 0;
        if (IsMaterial(item)) {
            std::uint32_t materials = 0;
            const itemdb::ItemRecordHead* part = nullptr;
            const itemdb::MaterialRecordHead* firstMaterial = nullptr;
            if (itemResults->items.empty()) return false;
            for (Item* slot : itemResults->items) {
                if (IsBuildable(slot)) return false;
                if (IsPart(slot)) {
                    LootRecordType recordType = RecordType(slot);
                    if (recordType <= LootRecordType::Quick) part = PartRecord(slot, recordType);
                } else if (IsMaterial(slot)) {
                    ++materials;
                    if (!firstMaterial) firstMaterial = BlueprintRecord(slot);
                }
            }
            if (!part) return false;
            const itemdb::MaterialRecordHead* own = BlueprintRecord(item);
            if (firstMaterial) return own == firstMaterial && materials < part->materialUnits;
            return text::CompareIgnoringCase(part->materialClass, own->soundName) == 0;
        }
        if (!IsSpellContainer(item)) return false;
        if (!itemMade) return false;
        Spell* spell = item->spell;
        if (!spell) return false;
        if (itemMade->Kind() != ItemKind::Quick && !spell->HasCastFlagModifier()) return false;
        if (spell->skillNeeded > itemMade->magicLevel) return false;
        // The container's charge maximum below the spell's mana cost fails.
        float need = spell->manaCost;
        float room = itemMade->chargeMax;
        if (room < need) return false;
        return std::ranges::none_of(itemResults->items, IsSpellContainer);
    }
    case 6: {   // repair: worn weapons and armour
        ItemKind itemKind = item->Kind();
        if (itemKind != ItemKind::Weapon && itemKind != ItemKind::Armor) return false;
        return item->conditionMax > item->condition;
    }
    default:
        return false;
    }
}

void CampPages::UpdateAction() {
    std::string action;
    std::string label("camp_current_money");
    cost = 0;
    actionEnabled = false;
    actionShown = false;

    // A trade page: what the camp buys (`buy`) minus what it sells (`sell`).
    auto tradeWith = [&](Party* bought, PriceKind buyKind, Party* sold, PriceKind sellKind, const char* text) {
        if (bought->items.empty() && sold->items.empty()) return;
        action = text;
        actionEnabled = true;
        actionShown = true;
        for (Item* item : bought->items) cost += Price(item, buyKind) * Count(item);
        for (Item* item : sold->items) cost -= Price(item, sellKind) * Count(item);
    };
    // A constructor's page: its slots' cost; a finished item in the slots is taken apart,
    // otherwise the constructor makes what the slots allow.
    auto construct = [&](Ref<Item>& made, Party* slots, PriceKind slotKind, PriceKind recordKind, PriceKind apartKind,
                         bool (*isFinished)(const Item*), bool (*isMaterial)(const Item*),
                         auto canMake, const char* apartText, const char* makeText, int topic) {
        made.Reset();
        deconstructing = false;
        bool described = false;
        if (!slots->items.empty()) {
            actionShown = true;
            // By index: canMake builds items through the slots' managers.
            for (int i = 0; i < std::ssize(slots->items); ++i) {
                Item* item = slots->items[i];
                cost += Price(item, slotKind);
                if (IsMade(item)) cost += Price(item, recordKind);
                if (isFinished(item)) {
                    actionEnabled = true;
                    cost = Price(item, apartKind);
                    if (IsMade(item)) cost += Price(item, recordKind);
                    deconstructing = true;
                    helpPanel->Describe(item);
                    described = true;
                    action = apartText;
                    break;
                }
                if (isMaterial(item)) {
                    actionEnabled = canMake();
                    helpPanel->Describe(made);
                    described = made.Get() != nullptr;
                    if (actionEnabled) action = makeText;
                }
            }
            if (!actionEnabled) cost = 0;
            if (described) return;
        }
        helpPanel->ShowHelp(topic);
    };

    switch (page) {
    case 0:
        // Repair all the hero's weapons and armour (where the item constructor works).
        if (itemConstructor && !noConstructor) {
            PartyUnitRecord* heroRecord = HeroRecord();
            for (Item* weapon : heroRecord->weapons) cost += Price(weapon, PriceKind::Repair);
            for (Item* armour : heroRecord->armor) cost += Price(armour, PriceKind::Repair);
            actionEnabled = cost > 0;
            if (actionEnabled) action = "camp_repair_all";
        }
        break;
    case 1: {
        bool up = trade->Bought();
        actionShown = up;
        actionEnabled = up;
        cost = trade->PurchaseCost();
        if (actionEnabled) action = "camp_level_up";
        label = "camp_current_exp";
        break;
    }
    case 2:
        tradeWith(spellParts[1], PriceKind::SpellBuy, spellParts[2], PriceKind::SpellSell, "camp_spell_trade");
        break;
    case 3:
        construct(spellMade, spellResults, PriceKind::SpellMake, PriceKind::SpellBuy, PriceKind::SpellApart, &IsSpellContainer, &IsSpell, [this] { return CanMakeSpell(); },
                  "camp_spell_deconstr", "camp_spell_constr", 0x67);
        break;
    case 4:
        tradeWith(itemParts[1], PriceKind::ItemBuy, itemParts[2], PriceKind::ItemSell, "camp_item_trade");
        break;
    case 5:
        construct(itemMade, itemResults, PriceKind::ItemMake, PriceKind::ItemBuy, PriceKind::ItemApart, &IsBuildable, &IsPart, [this] { return CanMakeItem(); },
                  "camp_item_deconstr", "camp_item_constr", 0x66);
        break;
    case 6:
        if (!itemExtra->items.empty()) {
            action = "camp_item_repair";
            actionEnabled = true;
            actionShown = true;
            for (Item* item : itemExtra->items) cost += Price(item, PriceKind::Repair) * Count(item);
        }
        break;
    default:
        break;
    }

    int amount;
    if (page == 1) {
        amount = trade->Experience();
    } else {
        amount = LocalPlayerData()->money;
        if (LocalPlayerData()->money < cost) actionEnabled = false;
    }

    std::string text;
    std::string line;
    TextManager* texts = g_clientWorld.texts.get();
    actionLabels.clear();
    if (action.size() != 0) {
        text = texts->GetString(action);
        AddLine(actionLabels, text, Rect{0, 30, 200, 50});
        if (cost != 0)
            text = std::format("{:+}", -cost);
        else
            text = "0";
        AddLine(actionLabels, text, Rect{0, 50, 200, 70});
    }
    text = texts->GetString(label);
    line = std::format("{} {}", text, amount);
    AddLine(actionLabels, line, Rect{0, 10, 200, 30});

    SetShade(actionIcon, actionEnabled ? 1.0f : 0.5f);
    SetShade(secondActionIcon, actionShown ? 1.0f : 0.5f);
}

void CampPages::DrawSprites(SceneRenderer* pass) {
    const Point cursor = g_cursor.position;    // the mouse in screen pixels
    hoveredItem = nullptr;
    int topic = 0;
    // The page buttons' help topics.
    switch (RegionIndex(pageButtons, RegionUnder(cursor))) {
    case 0: topic = 0x69; break;
    case 1: topic = 0x6A; break;
    case 2: if (spellConstructor) topic = 0x65; break;
    case 3: if (spellConstructor && !noConstructor) topic = 0x67; break;
    case 4: if (itemConstructor) topic = 100; break;
    case 5: if (itemConstructor && !noConstructor) topic = 0x66; break;
    case 6: if (itemConstructor) topic = 0x68; break;
    case 7: topic = 0x6B; break;
    default: break;
    }
    // Panel by panel until an item is found or a topic is set.
    auto probe = [&](UiItemPanel* panel) {
        if (hoveredItem || topic != 0) return;
        const ItemUnder under = panel->ItemAt(cursor);
        hoveredItem = under.item;
        topic = under.topic;
    };
    probe(equipment);
    probe(equipment2);
    probe(itemStrip);
    if (spellConstructor) {
        probe(spellConstructor1);
        probe(spellConstructor2);
        probe(spellStrip);
    }
    if (itemConstructor) {
        probe(itemConstructor1);
        probe(itemConstructor2);
        probe(itemConstructor3);
        probe(partStrip);
    }

    if (hoveredItem) {
        if (descPanel) descPanel->Describe(hoveredItem);
    } else if (topic != 0) {
        if (descPanel) descPanel->ShowHelp(topic);
    } else {
        // The statistics under the mouse: the trade panel's, else the stats panel's.
        ItemHoverInfo info;
        if (trade->enabled) info = trade->HoverInfo(cursor);
        if (info.kind == HoverKind::None && statsPanel->enabled) info = statsPanel->HoverInfo(cursor);
        if (descPanel) descPanel->DescribeStats(info);
    }
    UpdateAction();
    UiContainer::DrawSprites(pass);
    if (page == 0) DrawHeroPreview(pass, heroes[hero], heroTurn, 400.0f, true);
}

// --- what the constructors would make ------------------------------------------------------------

bool CampPages::CanMakeSpell() {
    SpellConstructor* maker = spellResults->GetSpellConstructor();
    ItemManager* factory = spellResults->ItemFactory();
    SpellManager* manager = spellResults->GetSpellManager();
    const itemdb::SpellRecord* spell = nullptr;
    // The modifiers are kept in an 8-entry stack array (more would overwrite its
    // frame; CanEquip allows at most eight).
    itemdb::SpellModifierRecord* modifiers[32];
    int modifierCount = 0;
    if (std::ssize(spellResults->items) <= 0) return false;
    for (Item* item : spellResults->items) {
        if (IsSpell(item))
            spell = SpellRecordOf(item);
        else if (IsModifier(item) && modifierCount < 32)
            modifiers[modifierCount++] = ModifierRecordOf(item);
    }
    if (!spell) return false;
    maker->SetManager(manager);
    maker->BeginConstruct(spell);
    for (int i = 0; i < modifierCount; ++i) {
        if (!maker->AddModifier(modifiers[i])) {
            maker->SetManager(nullptr);
            return false;
        }
    }
    maker->EndConstruct();
    Spell* made = maker->container->spell.Clone(manager);
    maker->SetManager(nullptr);
    itemdb::QuickItemTable& quickItems = g_itemDatabase.quickItems;
    int index = quickItems.IndexOf(quickItems.Named(itemdb::kSpellContainer));
    Item* container = factory->MakeItem(ItemKind::Scroll, index, 0);
    if (container) container->AttachSpell(made);
    spellMade.Assign(container);
    if (!container) return false;
    PartySkill best = BestPartySkill(spell->school);
    if (best.mana < static_cast<int>(made->manaCost)) return false;
    return best.skill >= static_cast<int>(made->skillNeeded);
}

bool CampPages::CanMakeItem() {
    ItemConstructor* maker = itemResults->GetItemConstructor();
    ResourceManager* factory = itemResults->ItemFactory();
    SpellManager* manager = itemResults->GetSpellManager();
    itemdb::ItemRecordHead* part = nullptr;
    Ref<Spell> spell;   // a copy of the container's spell, released on the way out
    itemdb::MaterialRecordHead* blueprint = nullptr;
    int blueprintCount = 0;
    ItemKind itemKind{};         // uninitialised until a part sets it
    if (itemResults->items.empty()) return false;
    for (Item* item : itemResults->items) {
        if (IsSpellContainer(item)) {
            spell.Assign(item->spell->Clone(manager));
        } else if (IsPart(item)) {
            switch (RecordType(item)) {
            case LootRecordType::Weapon: itemKind = ItemKind::Weapon; part = PartRecord(item, LootRecordType::Weapon); break;
            case LootRecordType::Armor: itemKind = ItemKind::Armor; part = PartRecord(item, LootRecordType::Armor); break;
            case LootRecordType::Quick: itemKind = ItemKind::Quick; part = PartRecord(item, LootRecordType::Quick); break;
            default: break;
            }
        } else if (IsMaterial(item)) {
            itemdb::MaterialRecordHead* record = BlueprintRecord(item);
            if (!blueprint) {
                blueprint = record;
                blueprintCount = 1;
            } else if (blueprint != record) {
                return false;
            } else {
                ++blueprintCount;
            }
        }
    }
    if (!blueprint || !part) return false;
    if (static_cast<int>(part->materialUnits) != blueprintCount) return false;
    if (text::CompareIgnoringCase(part->materialClass, blueprint->soundName) != 0) return false;
    itemMade.Assign(maker->BuildItem(factory, itemKind, part, blueprint, spell));
    return itemMade.Get() != nullptr;
}

// --- leaving the camp ----------------------------------------------------------------------------

void CampPages::CloseScreen() {
    g_serverWorld.SetServerPaused(0);
    // The local player's unit records, their keys raised meanwhile.
    std::vector<PartyUnitRecord*> records;
    auto* player = LocalPlayerData();
    for (PartyUnitRecord& record : player->members) {
        record.unitId += kRecordBias;
        records.push_back(&record);
    }
    g_clientWorld.RemoveUnitsOf(records);
    for (PartyUnitRecord& record : player->members) record.unitId -= kRecordBias;

    lib::FreeStorage(heroes);
    spellMade.Reset();
    itemMade.Reset();
    helpPanel->Describe(nullptr);
    descPanel->Describe(nullptr);
    // The first of each constructor's lists (the class template's) is not touched here.
    for (Party* list : {spellParts[1], spellParts[2], spellResults, itemParts[1], itemParts[2], itemResults,
                         itemExtra}) {
        list->Destroy();
        delete list;
    }
    g_interface.camp = nullptr;
}

// --- taking an item off the hero -----------------------------------------------------------------

Item* CampPages::TakeOff(Item* item) {
    g_interface.PlayInterfaceSound(kPutOffSound);
    ItemKind kind = item->Kind();
    if (kind < ItemKind::Weapon || (kind > ItemKind::Quick && kind != ItemKind::Scroll)) return nullptr;
    PartyUnitRecord* heroRecord = HeroRecord();
    // The item among the player's items.
    Item* result = LocalPlayerData()->AddItem(item, false, true);
    switch (item->Kind()) {
    case ItemKind::Weapon: {
        // The chosen weapon moves back when the item lies before it.
        int count = lib::Count(heroRecord->weapons);
        Item** weapons = heroRecord->weapons.data();
        for (int i = 0; i < count; ++i) {
            if (weapons[i] != item) continue;
            if (static_cast<std::uint32_t>(i) < heroRecord->weaponIndex) --heroRecord->weaponIndex;
            break;
        }
        heroRecord->RemoveWeapon(item);
        if (heroRecord->weaponIndex >= static_cast<std::uint32_t>(std::ssize(heroRecord->weapons)))
            heroRecord->weaponIndex = static_cast<std::uint32_t>(std::ssize(heroRecord->weapons) - 1);
        RefreshWeapons();
        break;
    }
    case ItemKind::Armor: {
        if (heroes[hero]) {
            heroes[hero]->UnequipArmour(static_cast<int>(item->AsArmor()->GetType()));
            heroes[hero]->controller->ShowWeapon(heroRecord->weaponIndex);
            heroes[hero]->controller->RefreshModel();
            heroes[hero]->controller->RefreshEquipment();
        }
        heroRecord->RemoveArmor(item);
        RebuildArmourLayers(heroRecord);
        break;
    }
    case ItemKind::Quick:
        heroRecord->RemoveQuickItem(item);
        break;
    case ItemKind::Scroll:
        heroRecord->RemoveSpell(item);
        break;
    default:
        break;
    }
    statsPanel->DrawStats();
    return result;
}

// --- putting an item on the hero -----------------------------------------------------------------

void CampPages::PutOn(Item* item) {
    g_interface.PlayInterfaceSound(kPutOnSound);
    PartyUnitRecord* heroRecord = HeroRecord();
    switch (item->Kind()) {
    case ItemKind::Weapon:
        if (std::ssize(heroRecord->weapons) >= 4) return;
        heroRecord->AddWeapon(item);
        if (std::ssize(heroRecord->weapons) == 1) heroRecord->weaponIndex = 0;
        RefreshWeapons();
        break;
    case ItemKind::Armor: {
        // Each piece on the same body part comes off first.
        ArmorType armorType = item->AsArmor()->GetType();
        // By index: TakeOff removes the piece from the list (the one after it is skipped, as in the original).
        for (int i = 0; i < std::ssize(heroRecord->armor); ++i) {
            if (heroRecord->armor[i]->AsArmor()->GetType() == armorType)
                TakeOff(heroRecord->armor[i]);
        }
        if (heroes[hero]) {
            Item* worn = heroRecord->AddArmor(item);
            heroes[hero]->EquipArmour(worn);
            heroes[hero]->controller->RefreshEquipment();
        }
        RebuildArmourLayers(heroRecord);
        break;
    }
    case ItemKind::Quick:
        if (std::ssize(heroRecord->quickItems) >= 4) return;
        heroRecord->AddQuickItem(item);
        break;
    case ItemKind::Scroll:
        if (std::ssize(heroRecord->spells) >= 8) return;
        heroRecord->AddSpell(item);
        break;
    default:
        return;
    }
    LocalPlayerData()->RemoveItem(item, false);
    statsPanel->DrawStats();
}

// --- moving an item between the camp's lists -----------------------------------------------------

void CampPages::PutOnFromStrip(Item* item) {
    switch (page) {
    case 0:
    case 1:
        PutOn(item);
        return;
    case 2:
        MoveItem(item, spellParts[2], LocalPlayerData(), 8, true, false);
        break;
    case 3:
        MoveItem(item, spellResults, LocalPlayerData(), 0, false, false);
        break;
    case 4:
        MoveItem(item, itemParts[2], LocalPlayerData(), 8, true, false);
        break;
    case 5:
        if (IsMaterial(item) && g_interface.shiftDown) {
            // With Shift the whole stack, one at a time while the slots accept it.
            for (int n = Count(item); n > 0; --n) {
                if (!CanEquip(item, 1)) break;
                MoveItem(item, itemResults, LocalPlayerData(), 0, false, false);
            }
        } else {
            MoveItem(item, itemResults, LocalPlayerData(), 0, false, false);
        }
        break;
    case 6:
        MoveItem(item, itemExtra, LocalPlayerData(), 0x10, true, false);
        break;
    default:
        return;
    }
    g_interface.PlayInterfaceSound(kPutOnSound);
}

void CampPages::TakeItem(Item* item, int mode) {
    if (!CanEquip(item, mode)) {
        g_interface.PlayInterfaceSound(kMessageBoxCancelSound);
        return;
    }
    // Back to the player's items and the item strip (from `list`).
    auto toPlayer = [&](Item* it, Party* list) {
        itemStrip->ScrollToItem(MoveItem(it, LocalPlayerData(), list, 0, true, false));
    };
    // A made item back to the constructor's parts and its strip, the others to the player.
    auto back = [&](Item* it, Party* ownerParty, Party* list, UiItemStrip* strip) {
        if (IsMade(it))
            strip->ScrollToItem(MoveItem(it, ownerParty, list, 0, true, false));
        else
            toPlayer(it, list);
    };
    switch (mode) {
    case 0:
        itemStrip->ScrollToItem(TakeOff(item));
        return;
    case 1:
        PutOnFromStrip(item);
        return;
    case 2:
        if (page == 2)
            MoveItem(item, spellParts[1], spellParts[0], 8, true, false);
        else if (page == 3)
            MoveItem(item, spellResults, spellParts[0], 0, false, true);
        g_interface.PlayInterfaceSound(kPutOnSound);
        return;
    case 3:
        if (page == 4) {
            MoveItem(item, itemParts[1], itemParts[0], 8, true, false);
        } else if (page == 5) {
            if (IsMaterial(item) && g_interface.shiftDown) {
                for (int n = Count(item); n > 0; --n) {
                    if (!CanEquip(item, 3)) break;
                    MoveItem(item, itemResults, itemParts[0], 0, false, true);
                }
            } else {
                MoveItem(item, itemResults, itemParts[0], 0, false, true);
            }
        }
        g_interface.PlayInterfaceSound(kPutOnSound);
        return;
    case 4:
        // Out of the spell constructor's sold or bought list.
        if (ListHolds(spellParts[1], item)) {
            spellStrip->ScrollToItem(MoveItem(item, spellParts[0], spellParts[1], 0, true, false));
        } else if (ListHolds(spellParts[2], item)) {
            toPlayer(item, spellParts[2]);
        } else {
            return;
        }
        break;
    case 5:
        if (ListHolds(itemParts[1], item)) {
            partStrip->ScrollToItem(MoveItem(item, itemParts[0], itemParts[1], 0, true, false));
        } else if (ListHolds(itemParts[2], item)) {
            toPlayer(item, itemParts[2]);
        } else {
            return;
        }
        break;
    case 6:
        // A modifier leaves the spell maker's slots; anything else empties them.
        if (IsModifier(item))
            back(item, spellParts[0], spellResults, spellStrip);
        else
            TakeResults(this, spellResults, spellParts[0], spellStrip);
        break;
    case 7:
        if (IsSpellContainer(item)) {
            back(item, itemParts[0], itemResults, partStrip);
        } else if (IsMaterial(item)) {
            back(item, itemParts[0], itemResults, partStrip);
            // With Shift every other kind-one item goes too.
            if (g_interface.shiftDown) {
                for (int i = 0; i < std::ssize(itemResults->items); ++i) {
                    if (!IsMaterial(itemResults->items[i])) continue;
                    back(itemResults->items[i], itemParts[0], itemResults, partStrip);
                    --i;
                }
            }
            // Then the first spell container left goes as well.
            for (Item* result : itemResults->items) {
                if (!IsSpellContainer(result)) continue;
                back(result, itemParts[0], itemResults, partStrip);
                break;
            }
        } else {
            TakeResults(this, itemResults, itemParts[0], partStrip);
        }
        break;
    case 8:
        toPlayer(item, itemExtra);
        break;
    default:
        return;
    }
    g_interface.PlayInterfaceSound(kPutOffSound);
}

// --- the hero's unit after a change --------------------------------------------------------------

void CampPages::RefreshWeapons() {
    PartyUnitRecord* heroRecord = HeroRecord();
    Unit* unit = heroes[hero];
    unit->controller->ShowWeapon(static_cast<std::uint32_t>(-1));
    for (int slot = 0; slot < 4; ++slot) unit->RemoveWeapon(slot);
    for (Item* weapon : heroRecord->weapons)
        unit->AddWeapon(weapon);
    unit->SendEquipment();
    unit->controller->ShowWeapon(heroRecord->weaponIndex);
    unit->controller->RefreshModel();
    RefreshStats();
}

void CampPages::RefreshStats() {
    if (heroes[hero]) {
        PartyUnitRecord* heroRecord = HeroRecord();
        heroes[hero]->SetFromStats(&heroRecord->stats);
        heroes[hero]->controller->RefreshWounds();
    }
    statsPanel->DrawStats();
}

// --- turning and choosing the hero ---------------------------------------------------------------

void CampPages::Update() {
    UiContainer::Update();
    if (turning == 1)
        heroTurn = TurnedBy(FrameTime(), heroTurn, false);
    else if (turning == 2)
        heroTurn = TurnedBy(FrameTime(), heroTurn, true);
}

void CampPages::NextHero() {
    int i = FindHero(hero, [](int at, int count) { return at + 1 >= count ? 0 : at + 1; });
    if (i == hero) return;
    hero = i;
    ShowHero();
    RefreshWeapons();
}

void CampPages::PreviousHero() {
    int i = FindHero(hero, [](int at, int count) { return at - 1 < 0 ? count - 1 : at - 1; });
    if (i == hero) return;
    hero = i;
    ShowHero();
    RefreshWeapons();
}

void CampPages::ShowHero() {
    Unit* unit = heroes[hero];
    TextManager* table = g_clientWorld.texts.get();
    std::string name;
    std::string own(unit->GetName());
    if (own.size() == 0) {
        PartyUnitRecord* heroRecord = HeroRecord();
        own = heroRecord->name;
        if (table->Open("pers " + own)) name = table->ReadLine(true);
    } else {
        name = own;
    }
    statsPanel->SetHero(hero, name);
    trade->SelectHero(hero);
}

void CampPages::CreateScreen() {
    g_interface.camp = this;
    BuildPages();
    curtains = screen_layout::AddCurtains(this, 1);
}

void CampPages::LayoutChanged() { screen_layout::PlaceCurtains(curtains); }

void CampPages::BuildPages() {
    spellConstructor = false;
    itemConstructor = false;
    spellParts[0] = nullptr;
    itemParts[0] = nullptr;
    g_serverWorld.SetServerPaused(1);
    constructor = nullptr;
    int maker = static_cast<int>(LocalPlayerVariable("constr.current"));
    if (maker > 0) {
        constructor = g_partyManager.templates.Find(maker);
        if (!constructor) {
            g_interface.closeAll = 1;
            g_clientWorld.LocalView()->Rejoin();
            return;
        }
        spellConstructor = constructor->sellsSpells != 0;
        itemConstructor = constructor->sellsItems != 0;
        std::memcpy(priceWeights, constructor->weights, kPriceWeights * sizeof(float));     // the dwords as they are
        spellParts[0] = constructor->SpellsParty();
        itemParts[0] = constructor->ItemsParty();
    }
    noConstructor = PlayerSwitch("i.noconstr");
    if (PlayerSwitch("i.offspellconstr")) spellConstructor = false;
    if (PlayerSwitch("i.onspellconstr")) spellConstructor = true;
    if (PlayerSwitch("i.offitemconstr")) itemConstructor = false;
    if (PlayerSwitch("i.onitemconstr")) itemConstructor = true;

    LoadTexture("campinfo", TextureKind::Interface);
    // The regions: the two action buttons, the two hero turn areas, the eight page buttons (a
    // page the camp does not offer has no tip).
    auto region = [this](int left, int top, int right, int bottom, int tip) {
        UiRegion* added = AddRegion();
        added->SetRect(left, top, right, bottom);
        if (tip) added->SetTip(tip);
        return added;
    };
    actionButton = region(12, 450, 52, 490, 0x4EE8);
    secondActionButton = region(148, 450, 188, 490, 0x4EE9);
    turnLeftArea = region(300, 470, 330, 500, 0);
    turnRightArea = region(470, 470, 500, 500, 0);
    pageButtons[0] = region(620, 430, 660, 460, 0x4E84);
    pageButtons[1] = region(620, 460, 660, 490, 0x4E85);
    pageButtons[2] = region(660, 460, 700, 490, spellConstructor ? 0x4E89 : 0);
    pageButtons[3] = region(700, 460, 740, 490, spellConstructor && !noConstructor ? 0x4E8A : 0);
    pageButtons[4] = region(660, 430, 700, 460, itemConstructor ? 0x4E86 : 0);
    pageButtons[5] = region(700, 430, 740, 460, itemConstructor && !noConstructor ? 0x4E87 : 0);
    pageButtons[6] = region(740, 430, 780, 460, itemConstructor ? 0x4E88 : 0);
    pageButtons[7] = region(740, 460, 780, 490, 0x4E8B);

    // The buttons' sprites, in the regions' order without the turn areas.
    auto button = [this](float left, float top, float right, float bottom, int u0, int v0, int u1, int v1) {
        UiMesh* sprite = AddBlankSprite();
        sprite->SetRectangle(kDialogDepth, kSpriteScale, left, top, right, bottom, u0, v0, u1, v1);
        return sprite;
    };
    actionIcon = button(12.0f, 450.0f, 52.0f, 490.0f, 0xA5, 0xAC, 0xCD, 0xD4);
    secondActionIcon = button(148.0f, 450.0f, 188.0f, 490.0f, 0xA5, 0xD6, 0xCD, 0xFE);
    pageIcons[0] = button(620.0f, 430.0f, 660.0f, 460.0f, 0x02, 0xC1, 0x2A, 0xDF);
    pageIcons[1] = button(620.0f, 460.0f, 660.0f, 490.0f, 0x02, 0xE0, 0x2A, 0xFE);
    pageIcons[2] = button(660.0f, 460.0f, 700.0f, 490.0f, 0x2B, 0xE0, 0x53, 0xFE);
    pageIcons[2]->visible = spellConstructor;
    pageIcons[3] = button(700.0f, 460.0f, 740.0f, 490.0f, 0x54, 0xE0, 0x7C, 0xFE);
    pageIcons[3]->visible = spellConstructor;
    if (noConstructor) pageIcons[3]->visible = false;
    pageIcons[4] = button(660.0f, 430.0f, 700.0f, 460.0f, 0x2B, 0xC1, 0x53, 0xDF);
    pageIcons[4]->visible = itemConstructor;
    pageIcons[5] = button(700.0f, 430.0f, 740.0f, 460.0f, 0x54, 0xC1, 0x7C, 0xDF);
    pageIcons[5]->visible = itemConstructor;
    if (noConstructor) pageIcons[5]->visible = false;
    pageIcons[6] = button(740.0f, 430.0f, 780.0f, 460.0f, 0x7D, 0xC1, 0xA5, 0xDF);
    pageIcons[6]->visible = itemConstructor;
    pageIcons[7] = button(740.0f, 460.0f, 780.0f, 490.0f, 0x7D, 0xE0, 0xA5, 0xFE);
    pageIcons[7]->tint[0] = 0.5f;
    pageIcons[7]->tint[1] = 0.5f;
    pageIcons[7]->tint[2] = 0.5f;

    CreateBase();
    AddSurface(NewSurface(Rect{0, 400, 200, 500}))->PaintWith([this](UiSurface& out) { PaintLabels(out, actionLabels); });
    AddSurface(NewSurface(Rect{600, 400, 800, 430}))->PaintWith([this](UiSurface& out) { PaintLabels(out, titleLabels); });

    // The constructors' lists (the first of each is the class template's).
    spellParts[1] = NewItemList();
    spellParts[2] = NewItemList();
    spellResults = NewItemList();
    itemParts[1] = NewItemList();
    itemParts[2] = NewItemList();
    itemResults = NewItemList();
    itemExtra = NewItemList();

    // The description panel on the right.
    descPanel = AddChild(std::make_unique<ItemInfoPanel>(this));
    descPanel->Build(600, 100);

    // The hero's statistics.
    statsPanel = AddChild(std::make_unique<HeroStatsPanel>(this));
    statsPanel->AddObject(objects[0]);
    statsPanel->Build();

    // The help panel on the left.
    helpPanel = AddChild(std::make_unique<ItemInfoPanel>(this));
    helpPanel->Build(0, 100);

    // The party's items.
    itemStrip = AddChild(std::make_unique<ForwardingStrip>(this));
    itemStrip->Setup(&LocalPlayerData()->items, false);
    itemStrip->mode = kActionEquip;
    itemStrip->SetFilter(kFilterItems);

    // The hero's equipment (with the body) and quick items.
    equipment = AddChild(std::make_unique<HeroEquipPanel>(this));
    equipment->AddObject(objects[2]);
    equipment->Build(true);

    equipment2 = AddChild(std::make_unique<HeroQuickStrip>(this));
    equipment2->AddObject(objects[2]);
    equipment2->Build(true);

    // The skills for experience.
    trade = AddChild(std::make_unique<TradePanel>(this));
    trade->AddObject(objects[0]);
    trade->Build();

    if (spellConstructor) {
        spellStrip = AddChild(std::make_unique<ForwardingStrip>(this));
        spellStrip->Setup(&spellParts[0]->items, true);
        spellStrip->mode = kActionSpellParts;
        spellStrip->SetFilter(kFilterSpells);
        spellStrip->priceKind = PriceKind::SpellBuy;

        spellConstructor1 = AddChild(std::make_unique<TradeGrid>(this));
        spellConstructor1->Build(&spellParts[1]->items, &spellParts[2]->items, true, false);
        spellConstructor1->mode = kModeSpellTrade;

        spellConstructor2 = AddChild(std::make_unique<SpellMakerPage>(this));
        spellConstructor2->BuildSpellMaker(spellResults);
    }

    if (itemConstructor) {
        partStrip = AddChild(std::make_unique<ForwardingStrip>(this));
        partStrip->Setup(&itemParts[0]->items, true);
        partStrip->mode = kActionItemParts;
        partStrip->SetFilter(kFilterArmour);
        partStrip->priceKind = PriceKind::ItemBuy;

        itemConstructor1 = AddChild(std::make_unique<TradeGrid>(this));
        itemConstructor1->Build(&itemParts[1]->items, &itemParts[2]->items, false, false);
        itemConstructor1->mode = kModeItemTrade;

        itemConstructor2 = AddChild(std::make_unique<ConstructorPage>(this));
        itemConstructor2->BuildItemMaker(itemResults);

        itemConstructor3 = AddChild(std::make_unique<RepairGrid>(this));
        itemConstructor3->Build(&itemExtra->items);
    }

    // The party's heroes become units of the client world: every record's id is raised while the
    // world takes the hero records, and put back after the units were set up.
    heroTurn = 0.0f;
    turning = 0;
    std::vector<PartyUnitRecord*> records;
    Party* player = LocalPlayerData();
    const int count = lib::Count(player->members);
    for (PartyUnitRecord& record : player->members) {
        record.unitId += kRecordBias;
        if (PrototypeObjectClass(record.prototype) == MapObjectType::Hero) records.push_back(&record);
    }
    g_clientWorld.CreatePartyUnits(records, 1);

    heroes.assign(count, nullptr);
    for (int i = 0; i < count; ++i) {
        if (PrototypeObjectClass(player->members[i].prototype) != MapObjectType::Hero) continue;
        auto* unit = g_clientWorld.FindUnit(player->members[i].unitId);
        heroes[i] = unit;
        // The idle animation from a random frame (up to 50 back).
        if (FigureAnimation* animation = unit->FindSkillAnimation(kIdleAnimation)) {
            std::uint32_t random = g_clientRandom.Next();
            double frames = FramesNow();
            int start = static_cast<int>(frames - static_cast<int>(random % 51));
            unit->SetAnimationEntries(-1, static_cast<std::int8_t>(animation->index), start, 0);
        }
        unit->controller->ShowWeapon(kNoWeaponSlot);
        unit->controller->RefreshModel();
        UnitController* controller = unit->controller;
        controller->RestartAnimation(static_cast<float>(FramesNow()));
        PartyUnitRecord* record = &player->members[i];
        unit->SetFromStats(&record->stats);
        unit->controller->RefreshWounds();
        // The armour the record wears.
        auto* heroStats = &record->stats;
        heroStats->ClearOuterLayers();
        for (Item* worn : record->armor)
            heroStats->AddArmourLayer(worn->AsArmor());
    }
    for (int i = 0; i < count; ++i) player->members[i].unitId -= kRecordBias;

    hero = -1;
    NextHero();

    // The pages' titles.
    TextManager* texts = g_clientWorld.texts.get();
    std::string title;
    const char* const titleKeys[] = {
        "mode_weapons",
        "mode_spells",
        "mode_spelltrade",
        "mode_spellconstr",
        "mode_itemtrade",
        "mode_itemconstr",
        "mode_itemrepair",
    };
    for (const char* key : titleKeys) {
        title = texts->GetString(key);
        pageTitles.push_back(title);
    }
    pageTitles.push_back("quit");

    page = itemConstructor ? kPageItemConstructor : spellConstructor ? kPageSpellConstructor : kPageEquipment;
    ShowPage(page);
    UpdateAction();

    // A constructor's stock on the first visit: from the party's lowest and highest skill in each
    // spell school.
    if (constructor && constructor->restockDue) {
        float lowest[kSchoolCount];
        float highest[kSchoolCount];
        for (int school = 0; school < kSchoolCount; ++school) {
            lowest[school] = 1e20f;
            highest[school] = -1e20f;
            for (auto& member : LocalPlayerData()->members) {
                auto* memberStats = &member.stats;
                float skill = memberStats->SpellSchoolSkill(static_cast<SpellSchool>(school));
                if (skill < lowest[school]) lowest[school] = skill;
                if (highest[school] < skill) highest[school] = skill;
            }
        }
        constructor->SetLevels(lowest, highest);
        ClassTemplateGenerateMagic(constructor);
        ClassTemplateGenerateItems(constructor);
        constructor->restockDue = 0;
    }

    // The music of the allod the party's zone is on.
    const MapRecord* zone = g_mapDatabase.FindMap(LocalPlayerZone());
    g_clientWorld.music->PlayConstructor(zone ? std::string_view(zone->allod) : std::string_view("gipat"));

}

// --- a spell container taken apart ---------------------------------------------------------------

void CampPages::DeconstructSpell() {
    Party* results = spellResults;
    Ref<Item> copy;
    copy.Assign(results->CopyItem(results->items[0]));
    results->RemoveItem(results->items[0], false);
    auto* spell = copy->spell.Get();

    AddLoot(results, NewLoot(results, kPrototypeItem), PrototypeIndex(spell));
    for (int i = 0; i < spell->GetModifierCount(); ++i)
        AddLoot(results, NewLoot(results, kRuneItem), spell->modifiers[i] - 1);
}

// --- an item taken apart -------------------------------------------------------------------------

void CampPages::DeconstructItem() {
    Party* results = itemResults;
    Ref<Item> copy;
    copy.Assign(results->CopyItem(results->items[0]));
    results->RemoveItem(results->items[0], false);

    if (const char* instruction = InstructionName(copy->Kind())) {
        if (ItemLoot* item = NewLoot(results, instruction)) AddLoot(results, item, PrototypeIndex(copy));
    }
    // A material per one the prototype needs; none without the material's record (an item of another database).
    const itemdb::MaterialRecordHead* material = itemdb::MaterialAt(MaterialIndex(copy->prototypeId));
    for (std::uint32_t i = 0; material && i < copy->GetPrototype()->materialUnits; ++i) {
        if (ItemLoot* item = NewLoot(results, std::string("material.") + material->key))
            AddLoot(results, item, std::nullopt);
    }
    if (copy->spell) {
        itemdb::QuickItemTable& quickItems = g_itemDatabase.quickItems;
        int index = quickItems.IndexOf(quickItems.Named(itemdb::kSpellContainer));
        Ref<Item> container;
        container.Assign(results->ItemFactory()->MakeItem(ItemKind::Scroll, index, 0));
        if (container) {
            container->AttachSpell(copy->spell->Clone(results->GetSpellManager()));
            results->AddItem(container, false, false);
        }
    }
}

// --- the first action button ---------------------------------------------------------------------

void CampPages::DoAction() {
    g_interface.PlayInterfaceSound(kBuySound);
    switch (static_cast<CampPage>(page)) {
    case CampPage::Equipment:
        if (itemConstructor && !noConstructor) {
            PartyUnitRecord* heroRecord = HeroRecord();
            for (Item* weapon : heroRecord->weapons) weapon->Repair();
            for (Item* worn : heroRecord->armor) worn->Repair();
        }
        break;
    case CampPage::Trade:
        trade->Settle(true, false);
        return;
    case CampPage::SpellTrade:
        constructor->SpellsParty()->GetSpellManager();
        StockSpells(this, spellParts[2]);
        ReturnToStrip(this, spellParts[0], spellParts[2], spellStrip);
        ReturnToStrip(this, LocalPlayerData(), spellParts[1], itemStrip);
        break;
    case CampPage::SpellMaker:
        if (deconstructing) {
            DeconstructSpell();
        } else if (spellMade) {
            TakeMade(spellResults, spellMade);
        }
        break;
    case CampPage::ItemTrade:
        StockItems(this, itemParts[2]);
        ReturnToStrip(this, itemParts[0], itemParts[2], partStrip);
        ReturnToStrip(this, LocalPlayerData(), itemParts[1], itemStrip);
        break;
    case CampPage::ItemMaker:
        if (deconstructing) {
            DeconstructItem();
        } else if (itemMade) {
            TakeMade(itemResults, itemMade);
        }
        break;
    case CampPage::Repair:
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
    g_interface.PlayInterfaceSound(kMessageBoxCancelSound);
    switch (static_cast<CampPage>(page)) {
    case CampPage::Trade:
        trade->Settle(false, false);
        return;
    case CampPage::SpellTrade:
        ReturnToStrip(this, spellParts[0], spellParts[1], spellStrip);
        ReturnToStrip(this, LocalPlayerData(), spellParts[2], itemStrip);
        break;
    case CampPage::SpellMaker:
        ReturnResults(this, spellResults, spellParts[0], spellStrip);
        break;
    case CampPage::ItemTrade:
        ReturnToStrip(this, itemParts[0], itemParts[1], partStrip);
        ReturnToStrip(this, LocalPlayerData(), itemParts[2], itemStrip);
        break;
    case CampPage::ItemMaker:
        ReturnResults(this, itemResults, itemParts[0], partStrip);
        break;
    case CampPage::Repair:
        ReturnToStrip(this, LocalPlayerData(), itemExtra, itemStrip);
        break;
    default:
        break;
    }
}

void CampPages::OnEvent(UiEvent& event) {
    switch (event.kind) {
    case UiEventKind::LeftDown: LeftDown(event); break;
    case UiEventKind::LeftUp: LeftUp(event); break;
    case UiEventKind::KeyDown: KeyDown(event); break;
    default: ItemScreen::OnEvent(event); break;
    }
}

void CampPages::LeftDown(UiEvent& event) {
    UiContainer::OnEvent(event);
    UiRegion* region = RegionUnder(event.at);
    const int pageButton = RegionIndex(pageButtons, region);
    if (region == actionButton) {
        UpdateAction();
        if (actionEnabled) DoAction();
    } else if (region == secondActionButton) {
        UpdateAction();
        if (actionShown) UndoAction();
    } else if (region == turnLeftArea || region == turnRightArea) {
        if (page == kEquipmentPage) {
            g_interface.PlayInterfaceSound(kMoveSound);
            turning = region == turnLeftArea ? 1 : 2;
            CaptureMouse();
        }
    } else if (pageButton >= 0 && page != pageButton) {
        ShowPage(pageButton);
    }
}

void CampPages::LeftUp(UiEvent& event) {
    UiContainer::OnEvent(event);
    ReleaseMouse();
    turning = 0;
}

void CampPages::KeyDown(UiEvent& event) {
    if (event.key == platform::Key::Escape) {
        ShowPage(kLeavePage);
        event.handled = true;
        return;
    }
    if ((event.key == platform::Key::Backspace || event.key == platform::Key::Return) && IsMultiplayerClient()) {
        if (event.key == platform::Key::Backspace)
            g_interface.ChatKeyBackspace();
        else
            g_interface.ChatKeyEnter();
        event.handled = true;
        return;
    }
    UiContainer::OnEvent(event);
}

void CampPages::MessageResult(std::uint32_t result) {
    if (result != kAnswerYes) return;
    PartyUnitRecord* heroRecord = HeroRecord();
    if (heroRecord->weapons.empty()) return;
    Item* weapon = heroRecord->weapons[0];
    Item* item = LocalPlayerData()->AddItem(weapon, false, true);
    heroRecord->RemoveWeapon(weapon);   // the weapon list's Remove
    heroRecord->weaponIndex = kNoWeaponSlot;
    itemStrip->ScrollToItem(item);
    RefreshWeapons();
}

const char* CampPages::HelpTopic() { return PageHelpTopic(page); }

// The camp's debug fill of the constructors' lists. See ui_camp.h and
// docs/notes/ui.md, section 5.

void CampPages::DebugFill() {
    std::vector<std::string> names;
    std::vector<std::string> materials = g_clientWorld.itemManager->ListMaterialNames();
    const int materialCount = static_cast<int>(materials.size());

    if (Party* items = itemParts[0]) {
        items->Clear(true, false);

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
        spells->Clear(true, false);
        names = g_clientWorld.spellManager->ListModifierNames();
        for (int i = 0; i < static_cast<int>(names.size()); ++i)
            AddNamed(spells, "Rune", static_cast<std::uint32_t>(i), kInstructionCount);
        names = g_clientWorld.spellManager->ListSpellNames();
        for (int i = 0; i < static_cast<int>(names.size()); ++i)
            AddNamed(spells, "Prototype", static_cast<std::uint32_t>(i), kInstructionCount);
    }
}

// --- the camp's hero record ----------------------------------------------------------------------

PartyUnitRecord* CampPages::HeroRecord() {
    return &LocalPlayerData()->members[hero];
}

// --- CampPages -------------------------------------------------------------------------------

PartySkill CampPages::BestPartySkill(SpellSchool school) {
    PartySkill best;
    for (auto& member : LocalPlayerData()->members) {
        UnitStats* stats = &member.stats;
        best.skill = (std::max)(best.skill, HeroSchoolSkill(stats, school));
        best.mana = (std::max)(best.mana, HeroManaUnits(stats));
    }
    return best;
}

void CampPages::TopicClicked(int newPage) {
    switch (newPage) {
    case 1: case 2: case 3: case 4: case 5: case 6: case 7: case 8: case 9: case 10: case 16:
        itemStrip->SelectTab(2);
        break;
    case 13:
        itemStrip->SelectTab(2);
        partStrip->SelectTab(5);
        break;
    case 14:
        itemStrip->SelectTab(0);
        partStrip->CycleTab(0, 4);
        break;
    case 15:
        itemStrip->SelectTab(0);
        partStrip->SelectTab(4);
        break;
    case 19:
        itemStrip->SelectTab(2);
        spellStrip->SelectTab(5);
        break;
    case 20:
        itemStrip->SelectTab(1);
        spellStrip->CycleTab(0, 3);
        break;
    case 21:
        itemStrip->SelectTab(1);
        spellStrip->CycleTab(3, 2);
        break;
    }
}
