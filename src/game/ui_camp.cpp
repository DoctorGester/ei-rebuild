// The camp's page switch and the camp panels' layout code (see docs/notes/ui.md, "Camp,
// global map, hero screens").
#include "game_strings.h"
#include "item_manager.h"
#include "ui.h"
#include "unit_create.h"
#include "item.h"
#include "ui_camp.h"

#include "item_db.h"
#include "text_manager.h"
#include "party.h"
#include "spell.h"
#include "texture.h"
#include "ui_item_info.h"
#include "ui_screens.h"
#include "unit.h"
#include "world.h"

#include "lib/containers.h"
#include "lib/text_case.h"
#include "render/render_state.h"
#include "ei/rebirth.h"
#include "lib/crt.h"
#include <algorithm>
#include <cstdint>
#include <string>
#include "world_glue.h"
#include "game_singletons.h"
#include "ui_cursor.h"
#include "lib/nan_audit.h"

#include <cmath>

#include <initializer_list>
#include <format>

namespace {

// `results` is re-read every time: a made item goes to `owner` and into `strip`, the others to the local
// player and into the item strip.
void TakeResults(CampPages* camp, CParty* const& results, CParty* owner, UiItemStrip* strip) {
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

// A white centred line on a surface (Rebirth-patched).
void DrawLine(UiSurface* surface, std::string_view text, const Rect& rect) {
    surface->DrawTextIn(text, &rect, 1, 0xFFFFFF, 1, 1);
}

// The database records of the camp's loot items (the record index): spells, spell
// modifiers and materials (null past the end, which is then read through).
std::uint32_t Index(const Item* item) { return AsLoot(item)->index; }
// The loot's record type: the record table it indexes.
LootRecordType RecordType(const Item* item) { return AsLoot(item)->recordType; }
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

// How worn the item is: 1 - durability / maximum. Rebirth's FixRepairPrice treats a
// negative (or NaN) durability or maximum as 0 and clamps the result to [0, 1] (NaN stays).
float WornFraction(const Item* item) {
    float durability = item->condition, maximum = item->conditionMax;
    if (!ei::rebirth::Enabled(ei::rebirth::Patch::FixRepairPrice))
        return kPriceOne - durability / maximum;
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
    for (int i = 0; i < std::ssize(hero->armor); ++i) stats->AddArmourLayer(AsArmor(hero->armor[i]));
}

// Whether `list` holds `item`.
bool ListHolds(CParty* list, const Item* item) {
    return std::ranges::find(list->items, item) != list->items.end();
}

// fmod(turn + sign * 2 * seconds, 2 pi).
constexpr float kCampFullTurn = /*007C7EE8*/ 6.2831855f;
float TurnedBy(float seconds, float turn, bool backwards) {
    const float twice = seconds + seconds;
    const float turned = backwards ? turn - twice : twice + turn;
    return std::fmod(turned, kCampFullTurn);
}


// From the current hero, step until a record of the hero class (fatal after more steps than there are
// records).
template <typename Step>
int FindHero(int start, Step step) {
    CParty* party = LocalPlayerData();
    int i = start;
    for (int tries = 0;;) {
        int count = lib::Count(party->members);
        if (tries > count) {
            game::FatalError("ERROR: no human personages in the party.\n");
            break;
        }
        i = step(i, count);
        ++tries;
        PartyUnitRecord* record = &party->members[i];
        if (PrototypeObjectClass(record->texts[5]) == MapObjectType::Hero) break;
    }
    return i;
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
    itemStrip->slots = 9;

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
        itemStrip->slots = 1;
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
        itemStrip->slots = 5;
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
        ui->PlayInterfaceSound("buttons\\globalmap\\transit.wav");
        if (fromHeroScreen) {
            ui->SetPendingValue(0);
            return;
        }
        GameRejoin();
        return;
    }
    default:
        break;
    }

    if (!offered) {
        ShowPage(page);    // the page is not offered: show the current one again
    } else {
        if (page != newPage) ui->PlayInterfaceSound("buttons\\camp\\slot.wav");
        page = newPage;
        statsPanel->ShowStats(newPage);
        trade->SelectHero(hero);
        UiSurface* title = surfaces[1];
        title->Clear();
        DrawLine(title, pageTitles[page], Rect{0, 10, 200, 30});
    }

    // The hero arrows (scene objects 0/1, regions 0/1) only where a hero can be chosen.
    if (page == 0 && (!itemConstructor || noConstructor)) {
        SceneMesh(0)->visible = false;
        SceneMesh(1)->visible = false;
        regions[0]->SetTip(0);
        regions[1]->SetTip(0);
    } else {
        SceneMesh(0)->visible = true;
        SceneMesh(1)->visible = true;
        regions[0]->SetTip(0x4EE8);
        regions[1]->SetTip(0x4EE9);
    }
    // The page buttons (scene objects 2-9) half bright, the current one full.
    for (int i = 2; i < 10; ++i) SetShade(SceneMesh(i), 0.5f);
    SetShade(SceneMesh(page + 2), 1.0f);
    ui->OpenHelp(false);
}

int CampPages::Price(Item* item, int kind) {
    const float* multipliers = priceWeights;
    float repair = kPriceZero;
    ItemKind itemKind = item->Kind();
    if (itemKind == ItemKind::Weapon || itemKind == ItemKind::Armor || kind == 8) {
        float worn = WornFraction(item);
        repair = ItemPrice(item) * multipliers[8] * worn;
        if (kind == 8) {
            EI_NAN_AUDIT(worn, multipliers[8]);
            if (worn == kPriceZero) return 0;
            if (multipliers[8] == kPriceZero) return 0;
            int price = static_cast<int>(repair);
            return price > 0 ? price : 1;
        }
    }
    if (itemKind == ItemKind::Weapon || itemKind == ItemKind::Armor) {
        // Rebirth's FixRepairPrice also jumps over this multiplication (written without an addon.log
        // line): for kind 7 (taking an item apart in the item constructor) the repair cost of the
        // wear is added instead of subtracted.
        if (kind == 7 && ei::rebirth::Enabled(ei::rebirth::Patch::FixRepairPrice))
            return static_cast<int>(ItemPrice(item) * multipliers[kind] + repair);
        return static_cast<int>(ItemPrice(item) * multipliers[kind] - repair);
    }
    return static_cast<int>(ItemPrice(item) * multipliers[kind]);
}

bool CampPages::CanEquip(Item* item, int mode) {
    switch (page) {
    case 0: {   // equipment: weapons, armour, spell containers (worn-out ones not to a hero)
        ItemKind itemKind = item->Kind();
        if ((itemKind == ItemKind::Weapon || itemKind == ItemKind::Armor) && std::lround(item->condition) <= 0 && mode == 1) return false;
        return itemKind == ItemKind::Weapon || itemKind == ItemKind::Armor || itemKind == ItemKind::Quick;
    }
    case 1: {   // trade: a spell container only for a hero who can cast its spell
        if (mode != 1) return true;
        if (!IsSpellContainer(item)) return false;
        CSpell* spell = item->spell;
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
        int school = 0;
        for (Item* slot : spellResults->items) {
            if (IsSpellContainer(slot)) return false;
            if (IsSpell(slot)) {
                hasSpell = true;
                const itemdb::SpellRecord* record = SpellRecordOf(slot);
                skillSum += record->skill;
                school = record->school;
                manaSum = manaSum + record->mana;
                // The modifier must suit the spell (the spell's flag byte for the modifier's kind).
                std::uint32_t kind = ModifierRecordOf(item)->kind;
                if (kind < 5 && record->allowedModifiers[kind] == 0) return false;
            } else if (IsModifier(slot)) {
                ++modifiers;
                const itemdb::SpellModifierRecord* modifier = ModifierRecordOf(slot);
                manaSum = manaSum + modifier->mana;
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
        CSpell* spell = item->spell;
        if (!spell) return false;
        if (itemMade->Kind() != ItemKind::Quick && !spell->HasCastFlagModifier()) return false;
        if (spell->skillNeeded > itemMade->magicLevel) return false;
        // The container's charge maximum below the spell's mana cost fails.
        float need = spell->manaCost;
        float room = itemMade->chargeMax;
        EI_NAN_AUDIT(room, need);
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
    auto tradeWith = [&](CParty* bought, int buyKind, CParty* sold, int sellKind, const char* text) {
        if (bought->items.empty() && sold->items.empty()) return;
        action = text;
        actionEnabled = true;
        actionShown = true;
        for (Item* item : bought->items) cost += Price(item, buyKind) * Count(item);
        for (Item* item : sold->items) cost -= Price(item, sellKind) * Count(item);
    };
    // A constructor's page: its slots' cost; a finished item in the slots is taken apart,
    // otherwise the constructor makes what the slots allow.
    auto construct = [&](Ref<Item>& made, CParty* slots, int slotKind, int recordKind, int apartKind,
                         bool (*isFinished)(const Item*), bool (*isMaterial)(const Item*),
                         bool (CampPages::*canMake)(), const char* apartText, const char* makeText, int topic) {
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
                    actionEnabled = (this->*canMake)();
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
            for (Item* weapon : heroRecord->weapons) cost += Price(weapon, 8);
            for (Item* armour : heroRecord->armor) cost += Price(armour, 8);
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
        tradeWith(spellParts[1], 0, spellParts[2], 1, "camp_spell_trade");
        break;
    case 3:
        construct(spellMade, spellResults, 2, 0, 3, &IsSpellContainer, &IsSpell, &CampPages::CanMakeSpell,
                  "camp_spell_deconstr", "camp_spell_constr", 0x67);
        break;
    case 4:
        tradeWith(itemParts[1], 4, itemParts[2], 5, "camp_item_trade");
        break;
    case 5:
        construct(itemMade, itemResults, 6, 4, 7, &IsBuildable, &IsPart, &CampPages::CanMakeItem,
                  "camp_item_deconstr", "camp_item_constr", 0x66);
        break;
    case 6:
        if (!itemExtra->items.empty()) {
            action = "camp_item_repair";
            actionEnabled = true;
            actionShown = true;
            for (Item* item : itemExtra->items) cost += Price(item, 8) * Count(item);
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
    TextManager* texts = g_clientWorld.texts;
    UiSurface* draw = surfaces[0];
    draw->Clear();
    if (action.size() != 0) {
        text = texts->GetString(action);
        DrawLine(draw, text, Rect{0, 30, 200, 50});
        if (cost != 0)
            text = std::format("{:+}", -cost);
        else
            text = "0";
        DrawLine(draw, text, Rect{0, 50, 200, 70});
    }
    text = texts->GetString(label);
    line = std::format("{} {}", text, amount);
    DrawLine(draw, line, Rect{0, 10, 200, 30});

    SetShade(SceneMesh(0), actionEnabled ? 1.0f : 0.5f);
    SetShade(SceneMesh(1), actionShown ? 1.0f : 0.5f);
}

void CampPages::Draw(SceneRenderer* pass) {
    int x = g_cursor.position.x;    // the mouse in screen pixels
    int y = g_cursor.position.y;
    hoveredItem = nullptr;
    int topic = 0;
    // The page buttons' help topics.
    switch (RegionAt(x, y)) {
    case 4: topic = 0x69; break;
    case 5: topic = 0x6A; break;
    case 6: if (spellConstructor) topic = 0x65; break;
    case 7: if (spellConstructor && !noConstructor) topic = 0x67; break;
    case 8: if (itemConstructor) topic = 100; break;
    case 9: if (itemConstructor && !noConstructor) topic = 0x66; break;
    case 10: if (itemConstructor) topic = 0x68; break;
    case 11: topic = 0x6B; break;
    default: break;
    }
    // Panel by panel until an item is found or a topic is set.
    auto probe = [&](UiItemPanel* panel) {
        if (!hoveredItem && topic == 0) hoveredItem = panel->ItemAt(x, y, &topic);
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
        if (trade->enabled) info = trade->HoverInfo(x, y);
        if (info.kind == HoverKind::None && statsPanel->enabled) info = statsPanel->HoverInfo(x, y);
        if (descPanel) descPanel->DescribeStats(info);
    }
    UpdateAction();
    UiContainer::Draw(pass);
    if (page == 0) DrawHeroPreview(pass, heroes[hero], heroTurn, 400.0f, true);
}

// --- what the constructors would make ------------------------------------------------------------

bool CampPages::CanMakeSpell() {
    CSpellConstructor* maker = spellResults->GetSpellConstructor();
    ItemManager* factory = spellResults->ItemFactory();
    CSpellManager* manager = spellResults->GetSpellManager();
    const itemdb::SpellRecord* spell = nullptr;
    // The modifiers are kept in an 8-entry stack array (more would overwrite its
    // frame; CanEquip allows at most eight).
    itemdb::SpellModifierRecord* modifiers[32];
    int modifierCount = 0;
    if (std::ssize(spellResults->items) <= 0) return false;
    for (int i = 0; i < std::ssize(spellResults->items); ++i) {
        Item* item = spellResults->items[i];
        if (IsSpell(item))
            spell = SpellRecordOf(item);
        else if (IsModifier(item) && modifierCount < 32)
            modifiers[modifierCount++] = ModifierRecordOf(item);
    }
    if (!spell) return false;
    maker->SetManager(manager);
    if (!maker->BeginConstruct(spell)) {
        maker->SetManager(nullptr);
        return false;
    }
    for (int i = 0; i < modifierCount; ++i) {
        if (!maker->AddModifier(modifiers[i])) {
            maker->SetManager(nullptr);
            return false;
        }
    }
    if (!maker->EndConstruct()) {
        maker->SetManager(nullptr);
        return false;
    }
    CSpell* made = maker->container->spell.Clone(manager);
    maker->SetManager(nullptr);
    itemdb::QuickItemTable& quickItems = g_itemDatabase.quickItems;
    int index = quickItems.IndexOf(quickItems.Named("spell container"));
    Item* container = factory->MakeItem(ItemKind::Scroll, index, 0);
    if (container) container->AttachSpell(made);
    spellMade.Assign(container);
    if (!container) return false;
    PartySkill best = BestPartySkill(spell->school);
    if (best.mana < static_cast<int>(made->manaCost)) return false;
    return best.skill >= static_cast<int>(made->skillNeeded);
}

bool CampPages::CanMakeItem() {
    CItemConstructor* maker = itemResults->GetItemConstructor();
    ResourceManager* factory = itemResults->ItemFactory();
    CSpellManager* manager = itemResults->GetSpellManager();
    itemdb::ItemRecordHead* part = nullptr;
    Ref<CSpell> spell;   // a copy of the container's spell, released on the way out
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
    GameSetServerPaused(0);
    // The local player's unit records, their keys raised meanwhile.
    std::vector<PartyUnitRecord*> records;
    auto* player = LocalPlayerData();
    for (PartyUnitRecord& record : player->members) {
        record.unitId += kRecordBias;
        records.push_back(&record);
    }
    GameRemoveUnitsOf(&records);
    for (PartyUnitRecord& record : player->members) record.unitId -= kRecordBias;

    lib::FreeStorage(heroes);
    spellMade.Reset();
    itemMade.Reset();
    helpPanel->Describe(nullptr);
    descPanel->Describe(nullptr);
    // The first of each constructor's lists (the class template's) is not touched here.
    for (CParty* list : {spellParts[1], spellParts[2], spellResults, itemParts[1], itemParts[2], itemResults,
                         itemExtra}) {
        list->Destroy();
        delete list;
    }
}

// --- taking an item off the hero -----------------------------------------------------------------

Item* CampPages::TakeOff(Item* item) {
    ui->PlayInterfaceSound(game::PutOffSoundText());
    ItemKind kind = item->Kind();
    if (kind < ItemKind::Weapon || (kind > ItemKind::Quick && kind != ItemKind::Scroll)) return nullptr;
    PartyUnitRecord* heroRecord = HeroRecord();
    // The item among the player's items.
    Item* result = LocalPlayerData()->AddItem(item, 0, 1);
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
            int armorType = AsArmor(item)->GetType();
            heroes[hero]->UnequipArmour(armorType);
            heroes[hero]->Controller()->ShowWeapon(heroRecord->weaponIndex);
            heroes[hero]->Controller()->RefreshModel();
            heroes[hero]->Controller()->RefreshEquipment();
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
    ui->PlayInterfaceSound(game::PutOnSoundText());
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
        int armorType = AsArmor(item)->GetType();
        for (int i = 0; i < std::ssize(heroRecord->armor); ++i) {
            if (AsArmor(heroRecord->armor[i])->GetType() == armorType)
                TakeOff(heroRecord->armor[i]);
        }
        if (heroes[hero]) {
            Item* worn = heroRecord->AddArmor(item);
            heroes[hero]->EquipArmour(worn);
            heroes[hero]->Controller()->RefreshEquipment();
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
    LocalPlayerData()->RemoveItem(item, 0);
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
        if (IsMaterial(item) && ui->shiftDown) {
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
    ui->PlayInterfaceSound(game::PutOnSoundText());
}

void CampPages::TakeItem(Item* item, int mode) {
    if (!CanEquip(item, mode)) {
        ui->PlayInterfaceSound(game::CancelSoundText());
        return;
    }
    // Back to the player's items and the item strip (from `list`).
    auto toPlayer = [&](Item* it, CParty* list) {
        itemStrip->ScrollToItem(MoveItem(it, LocalPlayerData(), list, 0, true, false));
    };
    // A made item back to the constructor's parts and its strip, the others to the player.
    auto back = [&](Item* it, CParty* ownerParty, CParty* list, UiItemStrip* strip) {
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
        ui->PlayInterfaceSound(game::PutOnSoundText());
        return;
    case 3:
        if (page == 4) {
            MoveItem(item, itemParts[1], itemParts[0], 8, true, false);
        } else if (page == 5) {
            if (IsMaterial(item) && ui->shiftDown) {
                for (int n = Count(item); n > 0; --n) {
                    if (!CanEquip(item, 3)) break;
                    MoveItem(item, itemResults, itemParts[0], 0, false, true);
                }
            } else {
                MoveItem(item, itemResults, itemParts[0], 0, false, true);
            }
        }
        ui->PlayInterfaceSound(game::PutOnSoundText());
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
            if (ui->shiftDown) {
                for (int i = 0; i < std::ssize(itemResults->items); ++i) {
                    if (!IsMaterial(itemResults->items[i])) continue;
                    back(itemResults->items[i], itemParts[0], itemResults, partStrip);
                    --i;
                }
            }
            // Then the first spell container left goes as well.
            for (int i = 0; i < std::ssize(itemResults->items); ++i) {
                if (!IsSpellContainer(itemResults->items[i])) continue;
                back(itemResults->items[i], itemParts[0], itemResults, partStrip);
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
    ui->PlayInterfaceSound(game::PutOffSoundText());
}

// --- the hero's unit after a change --------------------------------------------------------------

void CampPages::RefreshWeapons() {
    PartyUnitRecord* heroRecord = HeroRecord();
    CUnit* unit = heroes[hero];
    unit->Controller()->ShowWeapon(static_cast<std::uint32_t>(-1));
    for (int slot = 0; slot < 4; ++slot) unit->RemoveWeapon(slot);
    for (int i = 0; i < std::ssize(heroRecord->weapons); ++i)
        unit->AddWeapon(heroRecord->weapons[i]);
    unit->SendEquipment();
    unit->Controller()->ShowWeapon(heroRecord->weaponIndex);
    unit->Controller()->RefreshModel();
    RefreshStats();
}

void CampPages::RefreshStats() {
    if (heroes[hero]) {
        PartyUnitRecord* heroRecord = HeroRecord();
        heroes[hero]->SetFromStats(&heroRecord->stats);
        heroes[hero]->Controller()->RefreshWounds();
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
    CUnit* unit = heroes[hero];
    TextManager* table = g_clientWorld.texts;
    std::string name;
    std::string own = unit->GetName();
    if (own.size() == 0) {
        PartyUnitRecord* heroRecord = HeroRecord();
        own = heroRecord->texts[1];
        if (table->Open("pers " + own)) name = table->ReadLine(true);
    } else {
        name = own;
    }
    statsPanel->SetHero(hero, name);
    trade->SelectHero(hero);
}
