// The camp's page switch and the camp panels' layout code (see docs/notes/ui.md, "Camp,
// global map, hero screens").
#include "game_strings.h"
#include "ui.h"
#include "unit_create.h"
#include "item.h"
#include "ui_camp.h"

#include "item_db.h"
#include "item_db_text.h"
#include "party.h"
#include "spell.h"
#include "texture.h"
#include "ui_item_info.h"
#include "ui_screens.h"
#include "unit.h"
#include "world.h"

#include "lib/text_case.h"
#include "render/render_state.h"
#include "ei/rebirth.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include <algorithm>
#include <cstdint>
#include <string>
#include "world_glue.h"
#include "game_data.h"
#include "game_singletons.h"
#include "ui_cursor.h"
#include "lib/nan_audit.h"
#include "lib/str.h"

#include <cmath>

#include <initializer_list>

namespace {

// `results` is re-read every time: a made item goes to `owner` and into `strip`, the others to the local
// player and into the item strip.
void TakeResults(CCampPages* camp, CParty* const& results, CParty* owner, CUiItemStrip* strip) {
    while (!results->items_.empty()) {
        CItem* item = results->items_[0];
        if (IsMade(item))
            strip->ScrollToItem(camp->MoveItem(item, owner, results, 0, 1, 0));
        else
            camp->itemStrip_->ScrollToItem(camp->MoveItem(item, LocalPlayerData(), results, 0, 1, 0));
    }
}

// 1 full, 0.5 half.
void SetShade(CUiMesh* mesh, float shade) {
    mesh->tint_[0] = shade;
    mesh->tint_[1] = shade;
    mesh->tint_[2] = shade;
}

// A white centred line on a surface (Rebirth-patched).
void DrawLine(CUiSurface* surface, std::string_view text, RECT* rect) {
    surface->DrawTextIn(text, rect, 1, 0xFFFFFF, 1, 1);
}

// The database records of the camp's loot items (the record index): spells, spell
// modifiers and materials (null past the end, which is then read through).
std::uint32_t Index(const CItem* item) { return AsLoot(item)->index_; }
// The loot's record type: the record table it indexes.
LootRecordType RecordType(const CItem* item) { return AsLoot(item)->recordType_; }
itemdb::SpellRecord* SpellRecordOf(const CItem* item) { return itemdb::SpellAt(Index(item)); }
itemdb::SpellModifierRecord* ModifierRecordOf(const CItem* item) { return itemdb::SpellModifierAt(Index(item)); }
itemdb::MaterialRecordHead* BlueprintRecord(const CItem* item) { return itemdb::MaterialAt(Index(item)); }
// A part's prototype record by the loot's record type: weapon, armour, quick item.
itemdb::ItemRecordHead* PartRecord(const CItem* item, LootRecordType recordType) {
    switch (recordType) {
    case LootRecordType::Weapon: return itemdb::WeaponAt(Index(item));
    case LootRecordType::Armor: return itemdb::ArmorAt(Index(item));
    case LootRecordType::Quick: return itemdb::QuickItemAt(Index(item));
    default: return nullptr;
    }
}

double D(float f) { return f; }

constexpr float kPriceZero = 0.0f;
constexpr float kPriceOne = 1.0f;

// The item's base price, left unrounded.
double ItemPrice(CItem* item) {
    return item->GetPrice();
}

// How worn the item is: 1 - durability / maximum. Rebirth's FixRepairPrice treats a
// negative (or NaN) durability or maximum as 0 and clamps the result to [0, 1] (NaN stays).
float WornFraction(const CItem* item) {
    float durability = item->condition_, maximum = item->conditionMax_;
    if (!ei::rebirth::Enabled(ei::rebirth::Patch::FixRepairPrice))
        return static_cast<float>(static_cast<double>(kPriceOne) - static_cast<double>(durability) / maximum);
    float a = kPriceZero < durability ? durability : kPriceZero;
    float b = kPriceZero < maximum ? maximum : kPriceZero;
    double worn = static_cast<double>(kPriceOne) - static_cast<double>(a) / b;
    if (worn > kPriceOne) return kPriceOne;
    if (worn < kPriceZero) return kPriceZero;
    return static_cast<float>(worn);
}

// The stack count of an item.
int Count(const CItem* item) { return static_cast<int>(item->count_); }

constexpr std::uint32_t kRecordBias = 100000000;

// The hero's statistics with the armour it wears as their outer layers again.
void RebuildArmourLayers(PartyUnitRecord* hero) {
    UnitStats* stats = &hero->stats;
    stats->ClearOuterLayers();
    for (int i = 0; i < std::ssize(hero->armor); ++i) stats->AddArmourLayer(AsArmor(hero->armor[i]));
}

// Whether `list` holds `item`.
bool ListHolds(CParty* list, const CItem* item) {
    return std::ranges::find(list->items_, item) != list->items_.end();
}

// fmod(turn + sign * 2 * seconds, 2 pi) through the game's _CIfmod, which computes it on the FPU.
constexpr float kCampFullTurn = /*007C7EE8*/ 6.2831855f;
float TurnedBy(double seconds, float turn, bool backwards) {
    const double twice = seconds + seconds;
    const double turned = backwards ? turn - twice : twice + turn;
    return static_cast<float>(std::fmod(turned, kCampFullTurn));
}


// From the current hero, step until a record of the hero class (fatal after more steps than there are
// records).
template <typename Step>
int FindHero(int start, Step step) {
    CParty* party = LocalPlayerData();
    int i = start;
    for (int tries = 0;;) {
        int count = std::ssize(party->members_);
        if (tries > count) {
            game::FatalError("ERROR: no human personages in the party.\n");
            break;
        }
        i = step(i, count);
        ++tries;
        PartyUnitRecord* record = &party->members_[i];
        if (PrototypeObjectClass(record->texts[5]) == MapObjectType::Hero) break;
    }
    return i;
}

} // namespace

void CCampPages::ShowPage(int page) {
    equipment_->SetEnabled(0, 1);
    equipment2_->SetEnabled(0, 1);
    trade_->SetEnabled(0, 1);
    statsPanel_->SetEnabled(0, 1);
    helpPanel_->SetEnabled(0, 1);
    if (spellConstructor_) {
        spellConstructor1_->SetEnabled(0, 1);
        spellConstructor2_->SetEnabled(0, 1);
        spellStrip_->SetEnabled(0, 1);
    }
    if (itemConstructor_) {
        itemConstructor1_->SetEnabled(0, 1);
        itemConstructor3_->SetEnabled(0, 1);
        itemConstructor2_->SetEnabled(0, 1);
        partStrip_->SetEnabled(0, 1);
    }
    itemStrip_->slots_ = 9;

    bool offered = true;
    switch (page) {
    case 0:
        statsPanel_->SetEnabled(1, 1);
        equipment_->SetEnabled(1, 1);
        break;
    case 1:
        statsPanel_->SetEnabled(1, 1);
        equipment2_->SetEnabled(1, 1);
        trade_->SetEnabled(1, 1);
        break;
    case 2:
        if (!spellConstructor_) { offered = false; break; }
        helpPanel_->SetEnabled(1, 1);
        helpPanel_->ShowHelp(0x65);
        spellConstructor1_->SetEnabled(1, 1);
        spellStrip_->SetEnabled(1, 1);
        itemStrip_->slots_ = 1;
        break;
    case 3:
        if (!spellConstructor_ || noConstructor_) { offered = false; break; }
        helpPanel_->SetEnabled(1, 1);
        spellConstructor2_->SetEnabled(1, 1);
        spellStrip_->SetEnabled(1, 1);
        break;
    case 4:
        if (!itemConstructor_) { offered = false; break; }
        helpPanel_->SetEnabled(1, 1);
        helpPanel_->ShowHelp(0x64);
        itemConstructor1_->SetEnabled(1, 1);
        partStrip_->SetEnabled(1, 1);
        itemStrip_->slots_ = 5;
        break;
    case 5:
        if (!itemConstructor_ || noConstructor_) { offered = false; break; }
        helpPanel_->SetEnabled(1, 1);
        itemConstructor2_->SetEnabled(1, 1);
        partStrip_->SetEnabled(1, 1);
        break;
    case 6:
        if (!itemConstructor_) { offered = false; break; }
        helpPanel_->SetEnabled(1, 1);
        helpPanel_->ShowHelp(0x68);
        itemConstructor3_->SetEnabled(1, 1);
        partStrip_->SetEnabled(1, 1);
        break;
    case 7: {
        // Leave: the trade is settled, the constructors' unused parts go back to their owners and
        // what they made goes into the strips.
        trade_->Settle(false, true);
        ReturnAll(spellParts_[0], spellParts_[1]);
        ReturnAll(LocalPlayerData(), spellParts_[2]);
        ReturnAll(itemParts_[0], itemParts_[1]);
        ReturnAll(LocalPlayerData(), itemParts_[2]);
        ReturnAll(LocalPlayerData(), itemExtra_);
        TakeResults(this, spellResults_, spellParts_[0], spellStrip_);
        TakeResults(this, itemResults_, itemParts_[0], partStrip_);
        interface_->PlaySound("buttons\\globalmap\\transit.wav", 0);
        if (fromHeroScreen_) {
            interface_->SetPendingValue(0);
            return;
        }
        GameRejoin();
        return;
    }
    default:
        break;
    }

    if (!offered) {
        ShowPage(page_);   // the page is not offered: show the current one again
    } else {
        if (page_ != page) interface_->PlaySound("buttons\\camp\\slot.wav", 0);
        page_ = page;
        statsPanel_->ShowStats(page);
        trade_->SelectHero(hero_);
        CUiSurface* title = surfaces_[1];
        title->Clear();
        RECT rect = {0, 10, 200, 30};
        DrawLine(title, pageTitles_[page_], &rect);
    }

    // The hero arrows (scene objects 0/1, regions 0/1) only where a hero can be chosen.
    if (page_ == 0 && (!itemConstructor_ || noConstructor_)) {
        SceneMesh(0)->visible_ = 0;
        SceneMesh(1)->visible_ = 0;
        regions_[0]->SetTip(0, 1, 0);
        regions_[1]->SetTip(0, 1, 0);
    } else {
        SceneMesh(0)->visible_ = 1;
        SceneMesh(1)->visible_ = 1;
        regions_[0]->SetTip(0x4EE8, 1, 0);
        regions_[1]->SetTip(0x4EE9, 1, 0);
    }
    // The page buttons (scene objects 2-9) half bright, the current one full.
    for (int i = 2; i < 10; ++i) SetShade(SceneMesh(i), 0.5f);
    SetShade(SceneMesh(page_ + 2), 1.0f);
    interface_->OpenHelp(0);
}

int CCampPages::Price(CItem* item, int kind) {
    const float* multipliers = priceWeights_;
    float repair = kPriceZero;
    ItemKind type = item->Kind();
    if (type == ItemKind::Weapon || type == ItemKind::Armor || kind == 8) {
        float worn = WornFraction(item);
        repair = static_cast<float>(ItemPrice(item) * multipliers[8] * worn);
        if (kind == 8) {
            EI_NAN_AUDIT(worn, multipliers[8]);
            if (worn == kPriceZero) return 0;
            if (multipliers[8] == kPriceZero) return 0;
            int price = static_cast<int>(libcrt::Ftol(D(repair)));
            return price > 0 ? price : 1;
        }
    }
    if (type == ItemKind::Weapon || type == ItemKind::Armor) {
        // Rebirth's FixRepairPrice also jumps over this multiplication (written without an addon.log
        // line): for kind 7 (taking an item apart in the item constructor) the repair cost of the
        // wear is added instead of subtracted.
        if (kind == 7 && ei::rebirth::Enabled(ei::rebirth::Patch::FixRepairPrice))
            return static_cast<int>(libcrt::Ftol(ItemPrice(item) * multipliers[kind] + repair));
        return static_cast<int>(libcrt::Ftol(ItemPrice(item) * multipliers[kind] - repair));
    }
    return static_cast<int>(libcrt::Ftol(ItemPrice(item) * multipliers[kind]));
}

bool CCampPages::CanEquip(CItem* item, int mode) {
    switch (page_) {
    case 0: {   // equipment: weapons, armour, spell containers (worn-out ones not to a hero)
        ItemKind type = item->Kind();
        if ((type == ItemKind::Weapon || type == ItemKind::Armor) && x87::fistp(item->condition_) <= 0 && mode == 1) return false;
        return type == ItemKind::Weapon || type == ItemKind::Armor || type == ItemKind::Quick;
    }
    case 1: {   // trade: a spell container only for a hero who can cast its spell
        if (mode != 1) return true;
        if (!IsSpellContainer(item)) return false;
        CSpell* spell = item->spell_;
        PartyUnitRecord* hero = HeroRecord();
        return HeroCanUseSpell(&hero->stats, spell);

    }
    case 2:     // the spell constructor
        return IsSpellContainer(item) || IsSpell(item) || IsModifier(item);
    case 3: {   // the spell being built: a container, one spell, up to eight modifiers
        if (mode != 1 && mode != 2) return mode == 6;
        if (IsSpellContainer(item)) return std::ssize(spellResults_->items_) <= 0;
        if (IsSpell(item)) {
            if (std::ssize(spellResults_->items_) > 0) return false;
            const itemdb::SpellRecord* record = SpellRecordOf(item);
            PartySkill best = BestPartySkill(record->school);
            if (best.mana < static_cast<int>(libcrt::Ftol(D(record->mana)))) return false;
            return best.skill >= record->skill;
        }
        if (!IsModifier(item)) return false;
        int modifiers = 0, skillSum = 0;
        float manaSum = 0.0f;
        bool hasSpell = false;
        int school = 0;
        for (CItem* slot : spellResults_->items_) {
            if (IsSpellContainer(slot)) return false;
            if (IsSpell(slot)) {
                hasSpell = true;
                const itemdb::SpellRecord* record = SpellRecordOf(slot);
                skillSum += record->skill;
                school = record->school;
                manaSum = static_cast<float>(D(manaSum) + D(record->mana));
                // The modifier must suit the spell (the spell's flag byte for the modifier's kind).
                std::uint32_t kind = ModifierRecordOf(item)->kind;
                if (kind < 5 && record->allowedModifiers[kind] == 0) return false;
            } else if (IsModifier(slot)) {
                ++modifiers;
                const itemdb::SpellModifierRecord* modifier = ModifierRecordOf(slot);
                manaSum = static_cast<float>(D(manaSum) + D(modifier->mana));
                skillSum += modifier->skill;
            }
        }
        if (!hasSpell || modifiers >= 8) return false;
        const itemdb::SpellModifierRecord* modifier = ModifierRecordOf(item);
        float manaTotal = static_cast<float>(D(manaSum) + D(modifier->mana));
        int skillTotal = skillSum + modifier->skill;
        PartySkill best = BestPartySkill(school);
        if (best.skill < skillTotal) return false;
        return best.mana >= static_cast<int>(libcrt::Ftol(D(manaTotal)));
    }
    case 4:     // the item constructor: anything but quest items and magic
        if (item->Kind() == ItemKind::Quest) return false;
        if (IsSpellContainer(item) || IsSpell(item)) return false;
        return !IsModifier(item);
    case 5: {   // the item being built: a blueprint, its parts, a container's spell
        if (mode != 1 && mode != 3) return mode == 7;
        if (IsPart(item) || IsBuildable(item)) return std::ssize(itemResults_->items_) <= 0;
        if (IsMaterial(item)) {
            std::uint32_t materials = 0;
            const itemdb::ItemRecordHead* part = nullptr;
            const itemdb::MaterialRecordHead* firstMaterial = nullptr;
            if (itemResults_->items_.empty()) return false;
            for (CItem* slot : itemResults_->items_) {
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
        if (!itemMade_) return false;
        CSpell* spell = item->spell_;
        if (!spell) return false;
        if (itemMade_->Kind() != ItemKind::Quick && !spell->HasCastFlagModifier()) return false;
        if (spell->skillNeeded_ > itemMade_->magicLevel_) return false;
        // The container's charge maximum below the spell's mana cost fails.
        float need = spell->manaCost_;
        float room = itemMade_->chargeMax_;
        EI_NAN_AUDIT(room, need);
        if (room < need) return false;
        return std::ranges::none_of(itemResults_->items_, IsSpellContainer);
    }
    case 6: {   // repair: worn weapons and armour
        ItemKind type = item->Kind();
        if (type != ItemKind::Weapon && type != ItemKind::Armor) return false;
        return item->conditionMax_ > item->condition_;
    }
    default:
        return false;
    }
}

void CCampPages::UpdateAction() {
    std::string action;
    std::string label("camp_current_money");
    cost_ = 0;
    actionEnabled_ = false;
    actionShown_ = false;

    // A trade page: what the camp buys (`buy`) minus what it sells (`sell`).
    auto trade = [&](CParty* bought, int buyKind, CParty* sold, int sellKind, const char* text) {
        if (bought->items_.empty() && sold->items_.empty()) return;
        action = text;
        actionEnabled_ = true;
        actionShown_ = true;
        for (CItem* item : bought->items_) cost_ += Price(item, buyKind) * Count(item);
        for (CItem* item : sold->items_) cost_ -= Price(item, sellKind) * Count(item);
    };
    // A constructor's page: its slots' cost; a finished item in the slots is taken apart,
    // otherwise the constructor makes what the slots allow.
    auto construct = [&](Ref<CItem>& made, CParty* slots, int slotKind, int recordKind, int apartKind,
                         bool (*isFinished)(const CItem*), bool (*isMaterial)(const CItem*),
                         bool (CCampPages::*canMake)(), const char* apartText, const char* makeText, int topic) {
        made.Reset();
        deconstructing_ = false;
        bool described = false;
        if (!slots->items_.empty()) {
            actionShown_ = true;
            // By index: canMake builds items through the slots' managers.
            for (int i = 0; i < std::ssize(slots->items_); ++i) {
                CItem* item = slots->items_[i];
                cost_ += Price(item, slotKind);
                if (IsMade(item)) cost_ += Price(item, recordKind);
                if (isFinished(item)) {
                    actionEnabled_ = true;
                    cost_ = Price(item, apartKind);
                    if (IsMade(item)) cost_ += Price(item, recordKind);
                    deconstructing_ = true;
                    helpPanel_->Describe(item);
                    described = true;
                    action = apartText;
                    break;
                }
                if (isMaterial(item)) {
                    actionEnabled_ = (this->*canMake)();
                    helpPanel_->Describe(made);
                    described = made.Get() != nullptr;
                    if (actionEnabled_) action = makeText;
                }
            }
            if (!actionEnabled_) cost_ = 0;
            if (described) return;
        }
        helpPanel_->ShowHelp(topic);
    };

    switch (page_) {
    case 0:
        // Repair all the hero's weapons and armour (where the item constructor works).
        if (itemConstructor_ && !noConstructor_) {
            PartyUnitRecord* hero = HeroRecord();
            for (CItem* weapon : hero->weapons) cost_ += Price(weapon, 8);
            for (CItem* armour : hero->armor) cost_ += Price(armour, 8);
            actionEnabled_ = cost_ > 0;
            if (actionEnabled_) action = "camp_repair_all";
        }
        break;
    case 1: {
        bool up = trade_->Bought() != 0;
        actionShown_ = up;
        actionEnabled_ = up;
        cost_ = trade_->PurchaseCost();
        if (actionEnabled_) action = "camp_level_up";
        label = "camp_current_exp";
        break;
    }
    case 2:
        trade(spellParts_[1], 0, spellParts_[2], 1, "camp_spell_trade");
        break;
    case 3:
        construct(spellMade_, spellResults_, 2, 0, 3, &IsSpellContainer, &IsSpell, &CCampPages::CanMakeSpell,
                  "camp_spell_deconstr", "camp_spell_constr", 0x67);
        break;
    case 4:
        trade(itemParts_[1], 4, itemParts_[2], 5, "camp_item_trade");
        break;
    case 5:
        construct(itemMade_, itemResults_, 6, 4, 7, &IsBuildable, &IsPart, &CCampPages::CanMakeItem,
                  "camp_item_deconstr", "camp_item_constr", 0x66);
        break;
    case 6:
        if (!itemExtra_->items_.empty()) {
            action = "camp_item_repair";
            actionEnabled_ = true;
            actionShown_ = true;
            for (CItem* item : itemExtra_->items_) cost_ += Price(item, 8) * Count(item);
        }
        break;
    default:
        break;
    }

    int amount;
    if (page_ == 1) {
        amount = trade_->Experience();
    } else {
        amount = LocalPlayerData()->money_;
        if (LocalPlayerData()->money_ < cost_) actionEnabled_ = false;
    }

    std::string text;
    std::string line;
    CTextManager* texts = g_clientWorld->texts_;
    CUiSurface* draw = surfaces_[0];
    draw->Clear();
    if (action.size() != 0) {
        texts->GetString(action, &text);
        RECT name = {0, 30, 200, 50};
        DrawLine(draw, text, &name);
        if (cost_ != 0)
            text = str::Format("%+d", -cost_);
        else
            text = "0";
        RECT value = {0, 50, 200, 70};
        DrawLine(draw, text, &value);
    }
    texts->GetString(label, &text);
    line = str::Format("%s %d", text, amount);
    RECT top = {0, 10, 200, 30};
    DrawLine(draw, line, &top);

    SetShade(SceneMesh(0), actionEnabled_ ? 1.0f : 0.5f);
    SetShade(SceneMesh(1), actionShown_ ? 1.0f : 0.5f);
}

BOOL CCampPages::Draw(CSceneRenderer* pass) {
    int x = g_cursor->position_.x;   // the mouse in screen pixels
    int y = g_cursor->position_.y;
    hoveredItem_ = nullptr;
    int topic = 0;
    // The page buttons' help topics.
    switch (RegionAt(x, y)) {
    case 4: topic = 0x69; break;
    case 5: topic = 0x6A; break;
    case 6: if (spellConstructor_) topic = 0x65; break;
    case 7: if (spellConstructor_ && !noConstructor_) topic = 0x67; break;
    case 8: if (itemConstructor_) topic = 100; break;
    case 9: if (itemConstructor_ && !noConstructor_) topic = 0x66; break;
    case 10: if (itemConstructor_) topic = 0x68; break;
    case 11: topic = 0x6B; break;
    default: break;
    }
    // Panel by panel until an item is found or a topic is set.
    auto probe = [&](CUiItemPanel* panel) {
        if (!hoveredItem_ && topic == 0) hoveredItem_ = panel->ItemAt(x, y, &topic);
    };
    probe(equipment_);
    probe(equipment2_);
    probe(itemStrip_);
    if (spellConstructor_) {
        probe(spellConstructor1_);
        probe(spellConstructor2_);
        probe(spellStrip_);
    }
    if (itemConstructor_) {
        probe(itemConstructor1_);
        probe(itemConstructor2_);
        probe(itemConstructor3_);
        probe(partStrip_);
    }

    if (hoveredItem_) {
        if (descPanel_) descPanel_->Describe(hoveredItem_);
    } else if (topic != 0) {
        if (descPanel_) descPanel_->ShowHelp(topic);
    } else {
        // The statistics under the mouse: the trade panel's, else the stats panel's.
        ItemHoverInfo info;
        info.kind = -1;
        info.index = 0;
        info.part = 0;
        info.weapon = nullptr;
        info.stats = nullptr;
        if (trade_->enabled_ == 0 || (trade_->HoverInfo(x, y, &info), info.kind < 0)) {
            if (statsPanel_->enabled_ != 0) statsPanel_->HoverInfo(x, y, &info);
        }
        if (descPanel_) descPanel_->DescribeStats(&info);
    }
    UpdateAction();
    CUiContainer::Draw(pass);
    if (page_ == 0) DrawHeroPreview(pass, heroes_[hero_], heroTurn_, 400.0f, 1);
    return 1;
}

// --- what the constructors would make ------------------------------------------------------------

bool CCampPages::CanMakeSpell() {
    CSpellConstructor* constructor = spellResults_->SpellConstructor();
    CItemManager* factory = spellResults_->ItemFactory();
    CSpellManager* manager = spellResults_->SpellManager();
    const itemdb::SpellRecord* spell = nullptr;
    // The modifiers are kept in an 8-entry stack array (more would overwrite its
    // frame; CanEquip allows at most eight).
    itemdb::SpellModifierRecord* modifiers[32];
    int modifierCount = 0;
    if (std::ssize(spellResults_->items_) <= 0) return false;
    for (int i = 0; i < std::ssize(spellResults_->items_); ++i) {
        CItem* item = spellResults_->items_[i];
        if (IsSpell(item))
            spell = SpellRecordOf(item);
        else if (IsModifier(item) && modifierCount < 32)
            modifiers[modifierCount++] = ModifierRecordOf(item);
    }
    if (!spell) return false;
    constructor->SetManager(manager);
    if (!constructor->BeginConstruct(spell)) {
        constructor->SetManager(nullptr);
        return false;
    }
    for (int i = 0; i < modifierCount; ++i) {
        if (!constructor->AddModifier(modifiers[i])) {
            constructor->SetManager(nullptr);
            return false;
        }
    }
    if (!constructor->EndConstruct()) {
        constructor->SetManager(nullptr);
        return false;
    }
    CSpell* made = constructor->container_->spell_.Clone(manager);
    constructor->SetManager(nullptr);
    itemdb::QuickItemTable& quickItems = g_itemDatabase->quickItems;
    int index = quickItems.IndexOf(quickItems.Named("spell container"));
    CItem* container = factory->MakeItem(ItemKind::Spell, index, 0, -1);
    if (container) container->AttachSpell(made);
    spellMade_.Assign(container);
    if (!container) return false;
    PartySkill best = BestPartySkill(spell->school);
    if (best.mana < static_cast<int>(libcrt::Ftol(D(made->manaCost_)))) return false;
    return best.skill >= static_cast<int>(made->skillNeeded_);
}

bool CCampPages::CanMakeItem() {
    CItemConstructor* constructor = itemResults_->ItemConstructor();
    CResourceManager* factory = itemResults_->ItemFactory();
    CSpellManager* manager = itemResults_->SpellManager();
    itemdb::ItemRecordHead* part = nullptr;
    Ref<CSpell> spell;   // a copy of the container's spell, released on the way out
    itemdb::MaterialRecordHead* blueprint = nullptr;
    int blueprintCount = 0;
    ItemKind type{};         // uninitialised until a part sets it
    if (itemResults_->items_.empty()) return false;
    for (CItem* item : itemResults_->items_) {
        if (IsSpellContainer(item)) {
            spell.Assign(item->spell_->Clone(manager));
        } else if (IsPart(item)) {
            switch (RecordType(item)) {
            case LootRecordType::Weapon: type = ItemKind::Weapon; part = PartRecord(item, LootRecordType::Weapon); break;
            case LootRecordType::Armor: type = ItemKind::Armor; part = PartRecord(item, LootRecordType::Armor); break;
            case LootRecordType::Quick: type = ItemKind::Quick; part = PartRecord(item, LootRecordType::Quick); break;
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
    constructor->SetManager(factory);
    if (!constructor->BeginCreateItem(type, -1)) {
        constructor->SetManager(nullptr);
        return false;
    }
    constructor->SetPrototype(part);
    constructor->SetMaterial(blueprint);
    constructor->SetSpellObject(spell);
    itemMade_.Assign(constructor->EndCreateItem());
    constructor->SetManager(nullptr);
    return itemMade_.Get() != nullptr;
}

// --- leaving the camp ----------------------------------------------------------------------------

void CCampPages::CloseScreen() {
    GameSetServerPaused(0);
    // The local player's unit records, their keys raised meanwhile.
    std::vector<PartyUnitRecord*> records;
    auto* player = LocalPlayerData();
    for (PartyUnitRecord& record : player->members_) {
        record.unitId += kRecordBias;
        records.push_back(&record);
    }
    GameRemoveUnitsOf(&records);
    for (PartyUnitRecord& record : player->members_) record.unitId -= kRecordBias;

    lib::FreeStorage(heroes_);
    spellMade_.Reset();
    itemMade_.Reset();
    helpPanel_->Describe(nullptr);
    descPanel_->Describe(nullptr);
    // The first of each constructor's lists (the class template's) is not touched here.
    for (CParty* list : {spellParts_[1], spellParts_[2], spellResults_, itemParts_[1], itemParts_[2], itemResults_,
                         itemExtra_}) {
        list->Destroy();
        delete list;
    }
}

// --- taking an item off the hero -----------------------------------------------------------------

CItem* CCampPages::TakeOff(CItem* item) {
    interface_->PlaySound(game::PutOffSoundText(), 0);
    ItemKind kind = item->Kind();
    if (kind < ItemKind::Weapon || (kind > ItemKind::Quick && kind != ItemKind::Spell)) return nullptr;
    PartyUnitRecord* hero = HeroRecord();
    // The item among the player's items.
    CItem* result = LocalPlayerData()->AddItem(item, 0, 1);
    switch (item->Kind()) {
    case ItemKind::Weapon: {
        // The chosen weapon moves back when the item lies before it.
        int count = std::ssize(hero->weapons);
        CItem** weapons = hero->weapons.data();
        for (int i = 0; i < count; ++i) {
            if (weapons[i] != item) continue;
            if (static_cast<std::uint32_t>(i) < hero->weaponIndex) --hero->weaponIndex;
            break;
        }
        hero->RemoveWeapon(item);
        if (hero->weaponIndex >= static_cast<std::uint32_t>(std::ssize(hero->weapons)))
            hero->weaponIndex = static_cast<std::uint32_t>(std::ssize(hero->weapons) - 1);
        RefreshWeapons();
        break;
    }
    case ItemKind::Armor: {
        if (heroes_[hero_]) {
            int type = AsArmor(item)->GetType();
            heroes_[hero_]->UnequipArmour(type);
            heroes_[hero_]->Controller()->ShowWeapon(hero->weaponIndex);
            heroes_[hero_]->Controller()->RefreshModel();
            heroes_[hero_]->Controller()->RefreshEquipment();
        }
        hero->RemoveArmor(item);
        RebuildArmourLayers(hero);
        break;
    }
    case ItemKind::Quick:
        hero->RemoveQuickItem(item);
        break;
    case ItemKind::Spell:
        hero->RemoveSpell(item);
        break;
    default:
        break;
    }
    statsPanel_->DrawStats();
    return result;
}

// --- putting an item on the hero -----------------------------------------------------------------

void CCampPages::PutOn(CItem* item) {
    interface_->PlaySound(game::PutOnSoundText(), 0);
    PartyUnitRecord* hero = HeroRecord();
    switch (item->Kind()) {
    case ItemKind::Weapon:
        if (std::ssize(hero->weapons) >= 4) return;
        hero->AddWeapon(item);
        if (std::ssize(hero->weapons) == 1) hero->weaponIndex = 0;
        RefreshWeapons();
        break;
    case ItemKind::Armor: {
        // Each piece on the same body part comes off first.
        int type = AsArmor(item)->GetType();
        for (int i = 0; i < std::ssize(hero->armor); ++i) {
            if (AsArmor(hero->armor[i])->GetType() == type)
                TakeOff(hero->armor[i]);
        }
        if (heroes_[hero_]) {
            CItem* worn = hero->AddArmor(item);
            heroes_[hero_]->EquipArmour(worn);
            heroes_[hero_]->Controller()->RefreshEquipment();
        }
        RebuildArmourLayers(hero);
        break;
    }
    case ItemKind::Quick:
        if (std::ssize(hero->quickItems) >= 4) return;
        hero->AddQuickItem(item);
        break;
    case ItemKind::Spell:
        if (std::ssize(hero->spells) >= 8) return;
        hero->AddSpell(item);
        break;
    default:
        return;
    }
    LocalPlayerData()->RemoveItem(item, 0);
    statsPanel_->DrawStats();
}

// --- moving an item between the camp's lists -----------------------------------------------------

void CCampPages::PutOnFromStrip(CItem* item) {
    switch (page_) {
    case 0:
    case 1:
        PutOn(item);
        return;
    case 2:
        MoveItem(item, spellParts_[2], LocalPlayerData(), 8, 1, 0);
        break;
    case 3:
        MoveItem(item, spellResults_, LocalPlayerData(), 0, 0, 0);
        break;
    case 4:
        MoveItem(item, itemParts_[2], LocalPlayerData(), 8, 1, 0);
        break;
    case 5:
        if (IsMaterial(item) && interface_->shiftDown_) {
            // With Shift the whole stack, one at a time while the slots accept it.
            for (int n = Count(item); n > 0; --n) {
                if (!CanEquip(item, 1)) break;
                MoveItem(item, itemResults_, LocalPlayerData(), 0, 0, 0);
            }
        } else {
            MoveItem(item, itemResults_, LocalPlayerData(), 0, 0, 0);
        }
        break;
    case 6:
        MoveItem(item, itemExtra_, LocalPlayerData(), 0x10, 1, 0);
        break;
    default:
        return;
    }
    interface_->PlaySound(game::PutOnSoundText(), 0);
}

void CCampPages::TakeItem(CItem* item, int mode) {
    if (!CanEquip(item, mode)) {
        interface_->PlaySound(game::CancelSoundText(), 0);
        return;
    }
    // Back to the player's items and the item strip (from `list`).
    auto toPlayer = [&](CItem* it, CParty* list) {
        itemStrip_->ScrollToItem(MoveItem(it, LocalPlayerData(), list, 0, 1, 0));
    };
    // A made item back to the constructor's parts and its strip, the others to the player.
    auto back = [&](CItem* it, CParty* owner, CParty* list, CUiItemStrip* strip) {
        if (IsMade(it))
            strip->ScrollToItem(MoveItem(it, owner, list, 0, 1, 0));
        else
            toPlayer(it, list);
    };
    switch (mode) {
    case 0:
        itemStrip_->ScrollToItem(TakeOff(item));
        return;
    case 1:
        PutOnFromStrip(item);
        return;
    case 2:
        if (page_ == 2)
            MoveItem(item, spellParts_[1], spellParts_[0], 8, 1, 0);
        else if (page_ == 3)
            MoveItem(item, spellResults_, spellParts_[0], 0, 0, 1);
        interface_->PlaySound(game::PutOnSoundText(), 0);
        return;
    case 3:
        if (page_ == 4) {
            MoveItem(item, itemParts_[1], itemParts_[0], 8, 1, 0);
        } else if (page_ == 5) {
            if (IsMaterial(item) && interface_->shiftDown_) {
                for (int n = Count(item); n > 0; --n) {
                    if (!CanEquip(item, 3)) break;
                    MoveItem(item, itemResults_, itemParts_[0], 0, 0, 1);
                }
            } else {
                MoveItem(item, itemResults_, itemParts_[0], 0, 0, 1);
            }
        }
        interface_->PlaySound(game::PutOnSoundText(), 0);
        return;
    case 4:
        // Out of the spell constructor's sold or bought list.
        if (ListHolds(spellParts_[1], item)) {
            spellStrip_->ScrollToItem(MoveItem(item, spellParts_[0], spellParts_[1], 0, 1, 0));
        } else if (ListHolds(spellParts_[2], item)) {
            toPlayer(item, spellParts_[2]);
        } else {
            return;
        }
        break;
    case 5:
        if (ListHolds(itemParts_[1], item)) {
            partStrip_->ScrollToItem(MoveItem(item, itemParts_[0], itemParts_[1], 0, 1, 0));
        } else if (ListHolds(itemParts_[2], item)) {
            toPlayer(item, itemParts_[2]);
        } else {
            return;
        }
        break;
    case 6:
        // A modifier leaves the spell maker's slots; anything else empties them.
        if (IsModifier(item))
            back(item, spellParts_[0], spellResults_, spellStrip_);
        else
            TakeResults(this, spellResults_, spellParts_[0], spellStrip_);
        break;
    case 7:
        if (IsSpellContainer(item)) {
            back(item, itemParts_[0], itemResults_, partStrip_);
        } else if (IsMaterial(item)) {
            back(item, itemParts_[0], itemResults_, partStrip_);
            // With Shift every other kind-one item goes too.
            if (interface_->shiftDown_) {
                for (int i = 0; i < std::ssize(itemResults_->items_); ++i) {
                    if (!IsMaterial(itemResults_->items_[i])) continue;
                    back(itemResults_->items_[i], itemParts_[0], itemResults_, partStrip_);
                    --i;
                }
            }
            // Then the first spell container left goes as well.
            for (int i = 0; i < std::ssize(itemResults_->items_); ++i) {
                if (!IsSpellContainer(itemResults_->items_[i])) continue;
                back(itemResults_->items_[i], itemParts_[0], itemResults_, partStrip_);
                break;
            }
        } else {
            TakeResults(this, itemResults_, itemParts_[0], partStrip_);
        }
        break;
    case 8:
        toPlayer(item, itemExtra_);
        break;
    default:
        return;
    }
    interface_->PlaySound(game::PutOffSoundText(), 0);
}

// --- the hero's unit after a change --------------------------------------------------------------

void CCampPages::RefreshWeapons() {
    PartyUnitRecord* hero = HeroRecord();
    CUnit* unit = heroes_[hero_];
    unit->Controller()->ShowWeapon(static_cast<std::uint32_t>(-1));
    for (int slot = 0; slot < 4; ++slot) unit->RemoveWeapon(slot);
    for (int i = 0; i < std::ssize(hero->weapons); ++i)
        unit->AddWeapon(hero->weapons[i]);
    unit->SendEquipment();
    unit->Controller()->ShowWeapon(hero->weaponIndex);
    unit->Controller()->RefreshModel();
    RefreshStats();
}

void CCampPages::RefreshStats() {
    if (heroes_[hero_]) {
        PartyUnitRecord* hero = HeroRecord();
        heroes_[hero_]->SetFromStats(&hero->stats);
        heroes_[hero_]->Controller()->RefreshWounds();
    }
    statsPanel_->DrawStats();
}

// --- turning and choosing the hero ---------------------------------------------------------------

void CCampPages::Update() {
    UpdateBase();
    // The frame time in seconds (unrounded).
    if (turning_ == 1)
        heroTurn_ = TurnedBy(FrameTime(), heroTurn_, false);
    else if (turning_ == 2)
        heroTurn_ = TurnedBy(FrameTime(), heroTurn_, true);
}

void CCampPages::NextHero() {
    int i = FindHero(hero_, [](int at, int count) { return at + 1 >= count ? 0 : at + 1; });
    if (i == hero_) return;
    hero_ = i;
    ShowHero();
    RefreshWeapons();
}
void CCampPages::PreviousHero() {
    int i = FindHero(hero_, [](int at, int count) { return at - 1 < 0 ? count - 1 : at - 1; });
    if (i == hero_) return;
    hero_ = i;
    ShowHero();
    RefreshWeapons();
}

void CCampPages::ShowHero() {
    CUnit* unit = heroes_[hero_];
    CTextManager* table = g_clientWorld->texts_;
    std::string name;
    std::string own = unit->GetName();
    if (own.size() == 0) {
        PartyUnitRecord* hero = HeroRecord();
        own = hero->texts[1];
        if (table->Open("pers " + own)) table->ReadLine(&name, 1);
    } else {
        name = own;
    }
    statsPanel_->SetHero(hero_, name);
    trade_->SelectHero(hero_);
}
