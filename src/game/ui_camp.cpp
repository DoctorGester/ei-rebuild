// The camp's page switch (0060E9E0) and the camp panels' layout code (see docs/notes/ui.md, "Camp,
// global map, hero screens").
#include "device.h"   // the old device's calls
#include "ui_camp.h"

#include "item_db.h"
#include "item_db_text.h"
#include "party.h"
#include "spell.h"
#include "texture.h"
#include "unit.h"
#include "world.h"

#include "ei/hook.h"
#include "render/render_state.h"
#include "ei/rebirth.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "originals_effects.h"
#include "originals_library.h"
#include "originals_ui.h"
#include "originals_units.h"
#include "originals_engine.h"
#include "originals_gameplay.h"
#include "originals_world.h"

#include <d3d.h>

#include <cstdint>
#include <cstring>
#include <initializer_list>

#pragma float_control(precise, on)

namespace {

const char* GameText(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }

// --- Helpers ---------------------------------------------------------------------------------

// 006233C0 (thiscall on the stats panel, RET 4; rewritten as CHeroStatsPanel::ShowStats): the
// statistics for camp page `page`, through its address.
__declspec(naked) void __fastcall StatsPanelShowStats(CHeroStatsPanel*, void*, int) EI_ORIGINAL(0x006233C0)

// The local player's items (the party the client glue's 00578BA0 returns).
CParty* PlayerParty() { return reinterpret_cast<CParty*>(LocalPlayerData()); }

// Item `i` of a party's item list, and the list's count.
std::uint8_t* ItemOf(CParty* list, int i) { return static_cast<std::uint8_t*>(list->items_.data[i]); }
int CountOf(CParty* list) { return list->items_.size; }
CItem* AsItem(std::uint8_t* item) { return reinterpret_cast<CItem*>(item); }
CEngineObject* AsObject(std::uint8_t* item) { return reinterpret_cast<CEngineObject*>(item); }

// An item a constructor made: it turns with the flag a move into a list shown turning sets
// (ItemTurn +0x10 at the item's +0x1C).
bool IsMade(std::uint8_t* item) {
    const ItemTurn* turn = ItemTurnOf(AsItem(item));
    return turn && turn->flag;
}

// 005EE1B0 (CItemScreen::MoveItem, through its address): moves `item` from `from` to `to`; returns
// the item now held.
std::uint8_t* MoveBetween(CCampPages* camp, std::uint8_t* item, void* to, void* from, int limit, int merge, int turning) {
    return reinterpret_cast<std::uint8_t*>(ItemScreenMoveItemOriginal(camp, nullptr, AsItem(item), to, from, limit, merge, turning));
}
// 00621570 (CUiItemStrip::ScrollToItem, through its address).
void ScrollTo(CUiItemStrip* strip, void* item) { ItemStripScrollToItemOriginal(strip, nullptr, item); }

// Takes the made items out of `results` (re-read every time, as the original does): a made item
// goes to `owner` and into `strip`, the others to the local player and into the item strip.
void TakeResults(CCampPages* camp, CParty* const& results, CParty* owner, CUiItemStrip* strip) {
    for (CParty* list = results; list->items_.size != 0; list = results) {
        std::uint8_t* item = ItemOf(list, 0);
        if (IsMade(item)) {
            ScrollTo(strip, MoveBetween(camp, item, owner, list, 0, 1, 0));
        } else {
            void* player = LocalPlayerData();
            ScrollTo(camp->itemStrip_, MoveBetween(camp, item, player, list, 0, 1, 0));
        }
    }
}

// A page button's or an action button's shade: the three colour factors (1 full, 0.5 half).
void SetShade(CUiMesh* mesh, float shade) {
    mesh->tint_[0] = shade;
    mesh->tint_[1] = shade;
    mesh->tint_[2] = shade;
}

// A white centred line on a surface between GetDC and ReleaseDC (005FDE20 is Rebirth-patched).
void DrawLine(CUiSurface* surface, const char* text, RECT* rect) {
    SurfaceDrawText(surface, nullptr, text, rect, 1, 0xFFFFFF, 1, 1);
}

} // namespace

// --- 0060E9E0 ----------------------------------------------------------------------------------

void CCampPages::ShowPage(int page) {
    equipment_->SetEnabled(0, 1);
    equipment2_->SetEnabled(0, 1);
    trade_->SetEnabled(0, 1);
    statsPanel_->SetEnabled(0, 1);
    helpPanel_->SetEnabled(0, 1);
    if (spellConstructor_) {
        spellConstructor1_->SetEnabled(0, 1);
        spellConstructor2_->SetEnabled(0, 1);
        spellStrip_->window.SetEnabled(0, 1);
    }
    if (itemConstructor_) {
        itemConstructor1_->SetEnabled(0, 1);
        itemConstructor3_->SetEnabled(0, 1);
        itemConstructor2_->SetEnabled(0, 1);
        partStrip_->window.SetEnabled(0, 1);
    }
    itemStrip_->slots_ = 9;   // 00621F20

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
        InfoPanelShowHelpOriginal(helpPanel_, nullptr, 0x65);
        spellConstructor1_->SetEnabled(1, 1);
        spellStrip_->window.SetEnabled(1, 1);
        itemStrip_->slots_ = 1;
        break;
    case 3:
        if (!spellConstructor_ || noConstructor_) { offered = false; break; }
        helpPanel_->SetEnabled(1, 1);
        spellConstructor2_->SetEnabled(1, 1);
        spellStrip_->window.SetEnabled(1, 1);
        break;
    case 4:
        if (!itemConstructor_) { offered = false; break; }
        helpPanel_->SetEnabled(1, 1);
        InfoPanelShowHelpOriginal(helpPanel_, nullptr, 0x64);
        itemConstructor1_->SetEnabled(1, 1);
        partStrip_->window.SetEnabled(1, 1);
        itemStrip_->slots_ = 5;
        break;
    case 5:
        if (!itemConstructor_ || noConstructor_) { offered = false; break; }
        helpPanel_->SetEnabled(1, 1);
        itemConstructor2_->SetEnabled(1, 1);
        partStrip_->window.SetEnabled(1, 1);
        break;
    case 6:
        if (!itemConstructor_) { offered = false; break; }
        helpPanel_->SetEnabled(1, 1);
        InfoPanelShowHelpOriginal(helpPanel_, nullptr, 0x68);
        itemConstructor3_->SetEnabled(1, 1);
        partStrip_->window.SetEnabled(1, 1);
        break;
    case 7: {
        // Leave: the trade is settled, the constructors' unused parts go back to their owners and
        // what they made goes into the strips.
        TradePanelSettleOriginal(trade_, nullptr, 0, 1);
        ItemScreenReturnAllOriginal(this, nullptr, spellParts_[0], spellParts_[1]);
        ItemScreenReturnAllOriginal(this, nullptr, LocalPlayerData(), spellParts_[2]);
        ItemScreenReturnAllOriginal(this, nullptr, itemParts_[0], itemParts_[1]);
        ItemScreenReturnAllOriginal(this, nullptr, LocalPlayerData(), itemParts_[2]);
        ItemScreenReturnAllOriginal(this, nullptr, LocalPlayerData(), itemExtra_);
        TakeResults(this, spellResults_, spellParts_[0], spellStrip_);
        TakeResults(this, itemResults_, itemParts_[0], partStrip_);
        interface_->PlaySound(GameText(0x0079372C), 0);   // buttons\globalmap\transit.wav
        if (fromHeroScreen_) {
            interface_->SetPendingValue(0);
            return;
        }
        GlueBackToWorldOriginal(UiClientGlue());
        return;
    }
    default:
        break;
    }

    if (!offered) {
        ShowPage(page_);   // the page is not offered: show the current one again
    } else {
        if (page_ != page) interface_->PlaySound(GameText(0x0079374C), 0);   // buttons\camp\slot.wav
        page_ = page;
        StatsPanelShowStats(statsPanel_, nullptr, page);
        TradePanelSelectHeroOriginal(trade_, nullptr, hero_);
        CUiSurface* title = surfaces_.data[1];
        SurfaceGetDC(title);
        SurfaceClearOriginal(title);
        RECT rect = {0, 10, 200, 30};
        DrawLine(title, pageTitles_[page_], &rect);
        SurfaceReleaseDC(title);
    }

    // The hero arrows (scene objects 0/1, regions 0/1) only where a hero can be chosen.
    if (page_ == 0 && (!itemConstructor_ || noConstructor_)) {
        SceneMesh(0)->visible_ = 0;
        SceneMesh(1)->visible_ = 0;
        regions_.data[0]->SetTip(0, 1, 0);
        regions_.data[1]->SetTip(0, 1, 0);
    } else {
        SceneMesh(0)->visible_ = 1;
        SceneMesh(1)->visible_ = 1;
        regions_.data[0]->SetTip(0x4EE8, 1, 0);
        regions_.data[1]->SetTip(0x4EE9, 1, 0);
    }
    // The page buttons (scene objects 2-9) half bright, the current one full.
    for (int i = 2; i < 10; ++i) SetShade(SceneMesh(i), 0.5f);
    SetShade(SceneMesh(page_ + 2), 1.0f);
    interface_->ShowHelp(0);
}
EI_REPLACE_METHOD(0x0060E9E0, CCampPages::ShowPage);

// --- 006100E0: CanEquip ----------------------------------------------------------------------------

namespace {

// The camp's item predicates (005EE050.., thiscall on the camp, which they do not read; AL).
bool IsSpellContainer(const std::uint8_t* item) {
    return static_cast<std::uint8_t>(ItemKindIsSpellContainerOriginal(item)) != 0;
}
bool IsSpell(const std::uint8_t* item) { return ItemKindIsSpellOriginal(item); }
bool IsModifier(const std::uint8_t* item) { return ItemKindIsModifierOriginal(item); }
bool IsPart(const std::uint8_t* item) { return ItemKindIsPartOriginal(item); }
bool IsKindOne(const std::uint8_t* item) { return ItemKindIsKindOneOriginal(item); }
bool IsBlueprint(const std::uint8_t* item) { return ItemKindIsBuildableOriginal(item); }

// 0068B7C0 (thiscall on the spell): the spell has a cast-flag modifier.
__declspec(naked) bool __fastcall SpellHasCastFlag(const std::uint8_t*) EI_ORIGINAL(0x0068B7C0)

// The database records of the camp's loot items (the record index at +0x54): spells, spell
// modifiers and materials (null past the end; the original then reads through null).
template <typename T>
T Read(const std::uint8_t* p, int offset) {
    T v;
    std::memcpy(&v, p + offset, sizeof(T));
    return v;
}
std::uint32_t Index(const std::uint8_t* item) { return Read<std::uint32_t>(item, 0x54); }
const std::uint8_t* Record(const RecordTable& table, std::uint32_t index, int stride) {
    return reinterpret_cast<const std::uint8_t*>(table.At(index, stride));
}
const std::uint8_t* SpellRecord(const std::uint8_t* item) { return Record(itemdb::Spells(), Index(item), 0x84); }
const std::uint8_t* ModifierRecord(const std::uint8_t* item) { return Record(itemdb::SpellModifiers(), Index(item), 0x24); }
const std::uint8_t* BlueprintRecord(const std::uint8_t* item) { return Record(itemdb::Materials(), Index(item), 0x50); }
// A part's prototype record by the loot's record type (+0x50): weapon, armour, quick item.
const std::uint8_t* PartRecord(const std::uint8_t* item, int recordType) {
    switch (recordType) {
    case 0: return Record(itemdb::Weapons(), Index(item), 0xB4);
    case 1: return Record(itemdb::Armors(), Index(item), 200);
    case 2: return Record(itemdb::QuickItems(), Index(item), 0x8C);
    default: return nullptr;
    }
}

int ItemType(std::uint8_t* item) { return AsObject(item)->type_; }

double D(float f) { return f; }
float Store(double v) {
    volatile float r = static_cast<float>(v);
    return r;
}
__declspec(naked) int __cdecl GameFtol(double) {
    __asm {
        fld qword ptr [esp + 4]
        mov eax, 0x006E853C
        call eax
        ret
    }
}

} // namespace

// 0060FAB0: prices (slot 54); CanEquip follows.

// The products and differences keep the original's order (no reassociation under /fp:fast).
#pragma float_control(precise, on, push)

namespace {
constexpr float kPriceZero = 0.0f;   // 0073B858
constexpr float kPriceOne = 1.0f;    // 0073B84C

// Slot 16 of an item: its base price, left unrounded in ST0.
double ItemPrice(const std::uint8_t* item) {
    using PriceFn = double(__thiscall*)(const std::uint8_t*);
    void** vtable;
    std::memcpy(&vtable, item, sizeof(vtable));
    return reinterpret_cast<PriceFn>(vtable[16])(item);
}

// How worn the item is: 1 - durability (+0x20) / maximum (+0x24). Rebirth's FixRepairPrice treats a
// negative (or NaN) durability or maximum as 0 and clamps the result to [0, 1] (NaN stays).
float WornFraction(const std::uint8_t* item) {
    float durability = Read<float>(item, 0x20), maximum = Read<float>(item, 0x24);
    if (!ei::rebirth::Enabled(ei::rebirth::Patch::FixRepairPrice))
        return static_cast<float>(static_cast<double>(kPriceOne) - static_cast<double>(durability) / maximum);
    float a = kPriceZero < durability ? durability : kPriceZero;
    float b = kPriceZero < maximum ? maximum : kPriceZero;
    double worn = static_cast<double>(kPriceOne) - static_cast<double>(a) / b;
    if (worn > kPriceOne) return kPriceOne;
    if (worn < kPriceZero) return kPriceZero;
    return static_cast<float>(worn);
}
} // namespace

int CCampPages::Price(std::uint8_t* item, int kind) {
    const float* multipliers = reinterpret_cast<const float*>(priceWeights_);
    float repair = kPriceZero;
    int type = ItemType(item);
    if (type == 0x3004 || type == 0x3005 || kind == 8) {
        float worn = WornFraction(item);
        repair = static_cast<float>(ItemPrice(item) * multipliers[8] * worn);
        if (kind == 8) {
            if (!(worn < kPriceZero || worn > kPriceZero)) return 0;                // FCOMP; TEST AH,0x40
            if (!(multipliers[8] < kPriceZero || multipliers[8] > kPriceZero)) return 0;
            int price = GameFtol(D(repair));
            return price > 0 ? price : 1;
        }
    }
    if (type == 0x3004 || type == 0x3005) {
        // Rebirth's FixRepairPrice also JMPs over this FMUL (0060FB87, written without an addon.log
        // line): for kind 7 (taking an item apart in the item constructor) the repair cost of the
        // wear is added instead of subtracted.
        if (kind == 7 && ei::rebirth::Enabled(ei::rebirth::Patch::FixRepairPrice))
            return GameFtol(ItemPrice(item) * multipliers[kind] + repair);
        return GameFtol(ItemPrice(item) * multipliers[kind] - repair);
    }
    return GameFtol(ItemPrice(item) * multipliers[kind]);
}

#pragma float_control(pop)

EI_REPLACE_METHOD(0x0060FAB0, CCampPages::Price);
EI_REBIRTH_IMPLEMENTS(0x0060FAB0, ei::rebirth::Patch::FixRepairPrice);

bool CCampPages::CanEquip(std::uint8_t* item, int mode) {
    switch (page_) {
    case 0: {   // equipment: weapons, armour, spell containers (worn-out ones not to a hero)
        int type = ItemType(item);
        if ((type == 0x3004 || type == 0x3005) && x87::fistp(Read<float>(item, 0x20)) <= 0 && mode == 1) return false;
        return type == 0x3004 || type == 0x3005 || type == 0x3006;
    }
    case 1: {   // trade: a spell container only for a hero who can cast its spell
        if (mode != 1) return true;
        if (!IsSpellContainer(item)) return false;
        char* spell = reinterpret_cast<char*>(AsItem(item)->spell_);
        PartyUnitRecord* hero = VirtualHeroRecord();
        return HeroCanUseSpell(hero->stats, spell);
    }
    case 2:     // the spell constructor
        return IsSpellContainer(item) || IsSpell(item) || IsModifier(item);
    case 3: {   // the spell being built: a container, one spell, up to eight modifiers
        if (mode != 1 && mode != 2) return mode == 6;
        if (IsSpellContainer(item)) return CountOf(spellResults_) <= 0;
        if (IsSpell(item)) {
            if (CountOf(spellResults_) > 0) return false;
            const std::uint8_t* record = SpellRecord(item);
            int skill = 0, mana = 0;
            VirtualBestPartySkill(Read<int>(record, 0x14), &skill, &mana);
            if (mana < GameFtol(D(Read<float>(record, 0x18)))) return false;
            return skill >= Read<int>(record, 0x44);
        }
        if (!IsModifier(item)) return false;
        int modifiers = 0, skillSum = 0;
        float manaSum = 0.0f;
        bool hasSpell = false;
        int school = 0;
        for (int i = 0; i < CountOf(spellResults_); ++i) {
            if (IsSpellContainer(ItemOf(spellResults_, i))) return false;
            if (IsSpell(ItemOf(spellResults_, i))) {
                hasSpell = true;
                const std::uint8_t* record = SpellRecord(ItemOf(spellResults_, i));
                skillSum += Read<int>(record, 0x44);
                school = Read<int>(record, 0x14);
                manaSum = Store(D(manaSum) + D(Read<float>(record, 0x18)));
                // The modifier must suit the spell (its kind +0xC, flags at +0x78 of the spell).
                std::uint32_t kind = Read<std::uint32_t>(ModifierRecord(item), 0x0C);
                if (kind < 5 && record[0x78 + kind] == 0) return false;
            } else if (IsModifier(ItemOf(spellResults_, i))) {
                ++modifiers;
                const std::uint8_t* modifier = ModifierRecord(ItemOf(spellResults_, i));
                manaSum = Store(D(manaSum) + D(Read<float>(modifier, 0x10)));
                skillSum += Read<int>(modifier, 0x18);
            }
        }
        if (!hasSpell || modifiers >= 8) return false;
        const std::uint8_t* modifier = ModifierRecord(item);
        float manaTotal = Store(D(manaSum) + D(Read<float>(modifier, 0x10)));
        int skillTotal = skillSum + Read<int>(modifier, 0x18);
        int skill = 0, mana = 0;
        VirtualBestPartySkill(school, &skill, &mana);
        if (skill < skillTotal) return false;
        return mana >= GameFtol(D(manaTotal));
    }
    case 4:     // the item constructor: anything but quest items and magic
        if (ItemType(item) == 0x3009) return false;
        if (IsSpellContainer(item) || IsSpell(item)) return false;
        return !IsModifier(item);
    case 5: {   // the item being built: a blueprint, its parts, a container's spell
        if (mode != 1 && mode != 3) return mode == 7;
        if (IsPart(item) || IsBlueprint(item)) return CountOf(itemResults_) <= 0;
        if (IsKindOne(item)) {
            // The list's item manager and spell manager are asked for (slots 18 and 22; the
            // answers are not used).
            itemResults_->ItemManager();
            itemResults_->SpellManager();
            std::uint32_t kindOnes = 0;
            const std::uint8_t* part = nullptr;
            const std::uint8_t* firstKindOne = nullptr;
            if (CountOf(itemResults_) <= 0) return false;
            for (int i = 0; i < CountOf(itemResults_); ++i) {
                if (IsBlueprint(ItemOf(itemResults_, i))) return false;
                if (IsPart(ItemOf(itemResults_, i))) {
                    const std::uint8_t* entry = ItemOf(itemResults_, i);
                    int recordType = Read<int>(entry, 0x50);
                    if (recordType >= 0 && recordType <= 2) part = PartRecord(entry, recordType);
                } else if (IsKindOne(ItemOf(itemResults_, i))) {
                    ++kindOnes;
                    if (!firstKindOne) firstKindOne = BlueprintRecord(ItemOf(itemResults_, i));
                }
            }
            if (!part) return false;
            const std::uint8_t* own = BlueprintRecord(item);
            if (firstKindOne) return own == firstKindOne && kindOnes < Read<std::uint32_t>(part, 0x4C);
            return gamecrt::mbsicmp(Read<const unsigned char*>(part, 0x10), Read<const unsigned char*>(own, 0x04)) == 0;
        }
        if (!IsSpellContainer(item)) return false;
        if (!itemMade_) return false;
        std::uint8_t* spell = reinterpret_cast<std::uint8_t*>(AsItem(item)->spell_);
        if (!spell) return false;
        if (ItemType(itemMade_) != 0x3006 && !SpellHasCastFlag(spell)) return false;
        if (Read<std::uint32_t>(spell, 0x50) > Read<std::uint32_t>(itemMade_, 0x40)) return false;
        // FCOMPP / TEST AH,1: the container's +0x2C below the spell's +0x40 (or unordered) fails.
        float need = Read<float>(spell, 0x40);
        float room = Read<float>(itemMade_, 0x2C);
        if (!(room >= need)) return false;
        for (int i = 0; i < CountOf(itemResults_); ++i)
            if (IsSpellContainer(ItemOf(itemResults_, i))) return false;
        return true;
    }
    case 6: {   // repair: worn weapons and armour
        int type = ItemType(item);
        if (type != 0x3004 && type != 0x3005) return false;
        return Read<float>(item, 0x24) > Read<float>(item, 0x20);
    }
    default:
        return false;
    }
}

EI_REPLACE_METHOD(0x006100E0, CCampPages::CanEquip);

// --- 00611930: UpdateAction ------------------------------------------------------------------------

namespace {
// The stack count of an item (+0x48).
int Count(const std::uint8_t* item) { return Read<int>(item, 0x48); }
} // namespace

// CTradePanel::Bought (00625510) and the constructors' checks through their addresses.
__declspec(naked) std::uint8_t CTradePanel::CallBought() EI_ORIGINAL(0x00625510)
__declspec(naked) bool CCampPages::CallCanMakeSpell() EI_ORIGINAL(0x00612290)
__declspec(naked) bool CCampPages::CallCanMakeItem() EI_ORIGINAL(0x00612600)

void CCampPages::UpdateAction() {
    mfc::CString action;
    mfc::CString label(GameText(0x0079388C));   // "camp_current_money"
    cost_ = 0;
    actionEnabled_ = false;
    actionShown_ = false;
    // Slot 54 (Rebirth-patched 0060FAB0): the price of `item` for deal `kind`.
    auto price = [this](void* item, int kind) { return VirtualPrice(item, kind); };

    // A trade page: what the camp buys (`buy`) minus what it sells (`sell`).
    auto trade = [&](CParty* bought, int buyKind, CParty* sold, int sellKind, std::uintptr_t text) {
        if (CountOf(bought) <= 0 && CountOf(sold) <= 0) return;
        action = GameText(text);
        actionEnabled_ = true;
        actionShown_ = true;
        for (int i = 0; i < CountOf(bought); ++i) {
            std::uint8_t* item = ItemOf(bought, i);
            int count = Count(item);
            cost_ += price(item, buyKind) * count;
        }
        for (int i = 0; i < CountOf(sold); ++i) {
            std::uint8_t* item = ItemOf(sold, i);
            int count = Count(item);
            cost_ -= price(item, sellKind) * count;
        }
    };
    // A constructor's page: its slots' cost; a finished item in the slots is taken apart,
    // otherwise the constructor makes what the slots allow.
    auto construct = [&](std::uint8_t*& made, CParty* slots, int slotKind, int recordKind, int apartKind,
                         bool (*isFinished)(const std::uint8_t*), bool (*isMaterial)(const std::uint8_t*),
                         bool (CCampPages::*canMake)(), std::uintptr_t apartText, std::uintptr_t makeText, int topic) {
        if (made) AsObject(made)->Release();
        made = nullptr;
        deconstructing_ = false;
        bool described = false;
        if (CountOf(slots) > 0) {
            actionShown_ = true;
            for (int i = 0; i < CountOf(slots); ++i) {
                std::uint8_t* item = ItemOf(slots, i);
                cost_ += price(item, slotKind);
                if (IsMade(item)) cost_ += price(item, recordKind);
                if (isFinished(item)) {
                    actionEnabled_ = true;
                    cost_ = price(item, apartKind);
                    if (IsMade(item)) cost_ += price(item, recordKind);
                    deconstructing_ = true;
                    InfoPanelDescribeOriginal(helpPanel_, nullptr, item);
                    described = true;
                    action = GameText(apartText);
                    break;
                }
                if (isMaterial(item)) {
                    actionEnabled_ = (this->*canMake)();
                    InfoPanelDescribeOriginal(helpPanel_, nullptr, made);
                    described = made != nullptr;
                    if (actionEnabled_) action = GameText(makeText);
                }
            }
            if (!actionEnabled_) cost_ = 0;
            if (described) return;
        }
        InfoPanelShowHelpOriginal(helpPanel_, nullptr, topic);
    };

    switch (page_) {
    case 0:
        // Repair all the hero's weapons and armour (where the item constructor works).
        if (itemConstructor_ && !noConstructor_) {
            PartyUnitRecord* hero = VirtualHeroRecord();
            for (int i = 0; i < static_cast<int>(hero->weaponCount); ++i) cost_ += price(hero->weapons[i], 8);
            for (int i = 0; i < static_cast<int>(hero->armorCount); ++i) cost_ += price(hero->armor[i], 8);
            actionEnabled_ = cost_ > 0;
            if (actionEnabled_) action = GameText(0x0079387C);   // "camp_repair_all"
        }
        break;
    case 1: {
        bool up = trade_->CallBought() != 0;
        actionShown_ = up;
        actionEnabled_ = up;
        cost_ = TradePanelPurchaseCostOriginal(trade_);
        if (actionEnabled_) action = GameText(0x0079386C);   // "camp_level_up"
        label = GameText(0x00793858);                       // "camp_current_exp"
        break;
    }
    case 2:
        trade(spellParts_[1], 0, spellParts_[2], 1, 0x007936FC);   // "camp_spell_trade"
        break;
    case 3:
        construct(spellMade_, spellResults_, 2, 0, 3, &IsSpellContainer, &IsSpell, &CCampPages::CallCanMakeSpell,
                  0x00793844, 0x007936E8, 0x67);   // "camp_spell_deconstr", "camp_spell_constr"
        break;
    case 4:
        trade(itemParts_[1], 4, itemParts_[2], 5, 0x007936D8);     // "camp_item_trade"
        break;
    case 5:
        construct(itemMade_, itemResults_, 6, 4, 7, &IsBlueprint, &IsPart, &CCampPages::CallCanMakeItem,
                  0x0079381C, 0x007936C4, 0x66);    // "camp_item_deconstr", "camp_item_constr"
        break;
    case 6:
        if (CountOf(itemExtra_) > 0) {
            action = GameText(0x00793830);   // "camp_item_repair"
            actionEnabled_ = true;
            actionShown_ = true;
            for (int i = 0; i < CountOf(itemExtra_); ++i) {
                std::uint8_t* item = ItemOf(itemExtra_, i);
                int count = Count(item);
                cost_ += price(item, 8) * count;
            }
        }
        break;
    default:
        break;
    }

    int amount;
    if (page_ == 1) {
        amount = TradePanelExperienceOriginal(trade_);
    } else {
        amount = PlayerParty()->Money();
        if (PlayerParty()->Money() < cost_) actionEnabled_ = false;
    }

    mfc::CString text;
    mfc::CString line;
    CTextManager* texts = GameTexts();
    CUiSurface* draw = surfaces_.data[0];
    SurfaceGetDC(draw);
    SurfaceClearOriginal(draw);
    if (action.GetLength() != 0) {
        texts->GetString(action, &text);
        RECT name = {0, 30, 200, 50};
        DrawLine(draw, text, &name);
        if (cost_ != 0)
            text.Format(GameText(0x00793818), -cost_);   // "%+d"
        else
            text = GameText(0x007856B8);                 // "0"
        RECT value = {0, 50, 200, 70};
        DrawLine(draw, text, &value);
    }
    texts->GetString(label, &text);
    line.Format(GameText(0x00792F14), static_cast<const char*>(text), amount);   // "%s %d"
    RECT top = {0, 10, 200, 30};
    DrawLine(draw, line, &top);
    SurfaceReleaseDC(draw);

    SetShade(SceneMesh(0), actionEnabled_ ? 1.0f : 0.5f);
    SetShade(SceneMesh(1), actionShown_ ? 1.0f : 0.5f);
}
EI_REPLACE_METHOD(0x00611930, CCampPages::UpdateAction);

// --- 0060F050: Draw ------------------------------------------------------------------------------

BOOL CCampPages::Draw(int pass) {
    int x = MouseScreenX();   // the mouse in screen pixels
    int y = MouseScreenY();
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
    // The items under the mouse (each panel's slot 25), panel by panel until one is found or a
    // topic is set.
    auto probe = [&](CUiWindow* panel) {
        if (!hoveredItem_ && topic == 0) hoveredItem_ = reinterpret_cast<CItem*>(panel->VirtualItemAt(x, y, &topic));
    };
    probe(equipment_);
    probe(equipment2_);
    probe(&itemStrip_->window);
    if (spellConstructor_) {
        probe(spellConstructor1_);
        probe(spellConstructor2_);
        probe(&spellStrip_->window);
    }
    if (itemConstructor_) {
        probe(itemConstructor1_);
        probe(itemConstructor2_);
        probe(itemConstructor3_);
        probe(&partStrip_->window);
    }

    if (hoveredItem_) {
        if (descPanel_) InfoPanelDescribeOriginal(descPanel_, nullptr, hoveredItem_);
    } else if (topic != 0) {
        if (descPanel_) InfoPanelShowHelpOriginal(descPanel_, nullptr, topic);
    } else {
        // The statistics under the mouse: the trade panel's, else the stats panel's.
        ItemHoverInfo info;
        info.kind = -1;
        info.index = 0;
        info.part = 0;
        info.weapon = nullptr;
        info.stats = nullptr;
        if (trade_->enabled_ == 0 || (TradePanelHoverInfoOriginal(trade_, nullptr, x, y, &info), info.kind < 0)) {
            if (statsPanel_->enabled_ != 0) StatsPanelHoverInfoOriginal(statsPanel_, nullptr, x, y, &info);
        }
        if (descPanel_) InfoPanelDescribeStatsOriginal(descPanel_, nullptr, &info);
    }
    UpdateAction();
    reinterpret_cast<CUiContainer*>(this)->DrawContainer(pass);
    if (page_ == 0) {
        float turn;
        std::memcpy(&turn, &heroTurn_, sizeof turn);
        WindowDrawHeroPreviewOriginal(this, nullptr, pass, heroes_[hero_], turn, 400.0f, 1);
    }
    return 1;
}

EI_REPLACE_METHOD(0x0060F050, CCampPages::Draw);

// --- 00612290 / 00612600: what the constructors would make ---------------------------------------

namespace {

// 0052C690 (thiscall on a counted pointer, RET 4; a copy of ObjectHolderAssign): the slot takes
// `object` with a reference and releases the one it held.
__declspec(naked) void __fastcall CountedSlotAssign(void** /*slot*/, void*, void* /*object*/) EI_ORIGINAL(0x0052C690)

void ReleaseObject(void* object) {
    if (object) static_cast<CEngineObject*>(object)->Release();
}

char* RecordText(const std::uint8_t* record) { return const_cast<char*>(reinterpret_cast<const char*>(record)); }

} // namespace

bool CCampPages::CanMakeSpell() {
    // The slot list's spell constructor, item factory and spell manager (its slots 17, 18, 22).
    CSpellConstructor* constructor = spellResults_->SpellConstructor();
    CResourceManager* factory = spellResults_->ItemManager();
    CSpellManager* manager = spellResults_->SpellManager();
    const std::uint8_t* spell = nullptr;
    // The original keeps the modifiers in an 8-entry stack array (more would overwrite its
    // frame; CanEquip allows at most eight).
    const std::uint8_t* modifiers[32];
    int modifierCount = 0;
    if (CountOf(spellResults_) <= 0) return false;
    for (int i = 0; i < CountOf(spellResults_); ++i) {
        std::uint8_t* item = ItemOf(spellResults_, i);
        if (IsSpell(item))
            spell = SpellRecord(item);
        else if (IsModifier(item) && modifierCount < 32)
            modifiers[modifierCount++] = ModifierRecord(item);
    }
    if (!spell) return false;
    constructor->CallSetManager(manager);
    if (!constructor->CallBeginConstruct(RecordText(spell))) {
        constructor->CallSetManager(nullptr);
        return false;
    }
    for (int i = 0; i < modifierCount; ++i) {
        if (!constructor->CallAddModifier(RecordText(modifiers[i]))) {
            constructor->CallSetManager(nullptr);
            return false;
        }
    }
    if (!constructor->CallEndConstruct()) {
        constructor->CallSetManager(nullptr);
        return false;
    }
    CSpell* made = SpellCloneOriginal(reinterpret_cast<CSpell*>(constructor->container_->spell_), nullptr, manager);
    constructor->CallSetManager(nullptr);
    void* quickItems = const_cast<RecordTable*>(&itemdb::QuickItems());
    auto* record = static_cast<char*>(FindQuickItemOriginal(quickItems, nullptr, GameText(0x007938A0)));   // "spell container"
    int index = static_cast<int>(record - itemdb::QuickItems().begin) / 0x8C;
    auto* container = static_cast<std::uint8_t*>(FactoryMakeItemOriginal(factory, nullptr, 0x3008, index, 0, -1));
    if (container) ItemAttachSpellOriginal(AsItem(container), nullptr, made);
    spellMade_ = container;
    if (!container) return false;
    ++AsObject(container)->refCount_;
    int skill = 0, mana = 0;
    VirtualBestPartySkill(Read<int>(spell, 0x14), &skill, &mana);
    const auto* madeSpell = reinterpret_cast<const std::uint8_t*>(made);
    if (mana < GameFtol(D(Read<float>(madeSpell, 0x40)))) return false;
    return skill >= Read<int>(madeSpell, 0x50);
}
EI_REPLACE_METHOD(0x00612290, CCampPages::CanMakeSpell);

bool CCampPages::CanMakeItem() {
    // The slot list's item constructor, item factory and spell manager (its slots 16, 18, 22).
    CItemConstructor* constructor = itemResults_->ItemConstructor();
    CResourceManager* factory = itemResults_->ItemManager();
    CSpellManager* manager = itemResults_->SpellManager();
    const std::uint8_t* part = nullptr;
    void* spell = nullptr;   // a counted pointer (0052C690), released on the way out
    const std::uint8_t* blueprint = nullptr;
    int blueprintCount = 0;
    int type = 0;            // the original's is uninitialised until a part sets it
    auto fail = [&]() {
        ReleaseObject(spell);
        return false;
    };
    auto* withManager = reinterpret_cast<ItemConstructorManagerOriginal*>(constructor);
    if (CountOf(itemResults_) <= 0) return fail();
    for (int i = 0; i < CountOf(itemResults_); ++i) {
        std::uint8_t* item = ItemOf(itemResults_, i);
        if (IsSpellContainer(item)) {
            CSpell* copy = SpellCloneOriginal(static_cast<CSpell*>(AsItem(item)->spell_), nullptr, manager);
            CountedSlotAssign(&spell, nullptr, copy);
        } else if (IsPart(item)) {
            switch (Read<int>(item, 0x50)) {
            case 0: type = 0x3004; part = PartRecord(item, 0); break;
            case 1: type = 0x3005; part = PartRecord(item, 1); break;
            case 2: type = 0x3006; part = PartRecord(item, 2); break;
            default: break;
            }
        } else if (IsKindOne(item)) {
            const std::uint8_t* record = BlueprintRecord(item);
            if (!blueprint) {
                blueprint = record;
                blueprintCount = 1;
            } else if (blueprint != record) {
                return fail();
            } else {
                ++blueprintCount;
            }
        }
    }
    if (!blueprint || !part) return fail();
    if (Read<int>(part, 0x4C) != blueprintCount) return fail();
    if (gamecrt::mbsicmp(Read<const unsigned char*>(part, 0x10), Read<const unsigned char*>(blueprint, 0x04)) != 0)
        return fail();
    withManager->SetManager(factory);
    if (!ItemConstructorBeginCreateItemOriginal(constructor, nullptr, type, -1)) {
        withManager->SetManager(nullptr);
        return fail();
    }
    if (!ItemConstructorSetPrototypeOriginal(constructor, nullptr, RecordText(part)) ||
        !ItemConstructorSetMaterialOriginal(constructor, nullptr, RecordText(blueprint)) ||
        !ItemConstructorSetSpellObjectOriginal(constructor, nullptr, static_cast<CSpell*>(spell))) {
        withManager->SetManager(nullptr);
        return fail();
    }
    itemMade_ = reinterpret_cast<std::uint8_t*>(ItemConstructorEndCreateItemOriginal(constructor));
    withManager->SetManager(nullptr);
    if (!itemMade_) return fail();
    ++AsObject(itemMade_)->refCount_;
    ReleaseObject(spell);
    return true;
}
EI_REPLACE_METHOD(0x00612600, CCampPages::CanMakeItem);

// --- 0060E6B0: leaving the camp ---------------------------------------------------------------

namespace {
constexpr std::uint32_t kRecordBias = 100000000;
} // namespace

void CCampPages::Close() {
    SetServerFlagOriginal(0);   // 00578D10 (on the game glue 007AFEA4)
    device::SetRenderState(static_cast<IDirect3DDevice7*>(RenderDeviceObject()->d3dDevice_), D3DRENDERSTATE_ANTIALIAS, 0);
    render::state::Render(D3DRENDERSTATE_ANTIALIAS, 0);
    // The local player's unit records, as the record array the client glue takes (005789E0; a
    // CArray of record pointers, vtable 0073FAA4, SetSize 005719B0), their keys raised meanwhile.
    RecordPtrArray records = {reinterpret_cast<void*>(0x0073FAA4), nullptr, 0, 0, 0};
    auto* player = reinterpret_cast<CParty*>(GameLocalPlayerDataOriginal(UiClientGlue()));
    for (int i = 0; i < player->members_.size; ++i) {
        PartyUnitRecord* record = &player->members_.data[i];
        record->unitId += kRecordBias;
        int index = records.size;
        RecordArraySetSizeOriginal(&records, nullptr, index + 1, -1);
        records.data[index] = reinterpret_cast<std::uint8_t*>(&player->members_.data[i]);
    }
    GameTakeUnitRecordsOriginal(UiClientGlue(), nullptr, &records);
    for (int i = 0; i < player->members_.size; ++i) player->members_.data[i].unitId -= kRecordBias;

    // The heroes array emptied (the CArray's SetSize(0) without elements to destroy).
    if (heroes_.data) {
        gamecrt::operator_delete(heroes_.data);
        heroes_.data = nullptr;
    }
    heroes_.maxSize = 0;
    heroes_.size = 0;
    if (spellMade_) AsObject(spellMade_)->Release();
    spellMade_ = nullptr;
    if (itemMade_) AsObject(itemMade_)->Release();
    itemMade_ = nullptr;
    InfoPanelDescribeOriginal(helpPanel_, nullptr, nullptr);
    InfoPanelDescribeOriginal(descPanel_, nullptr, nullptr);
    // The part lists emptied (slot 3) and deleted (slot 0, the deleting destructor); the first of
    // each constructor's lists (the class template's) is not touched here.
    for (CParty** list : {&spellParts_[1], &spellParts_[2], &spellResults_, &itemParts_[1], &itemParts_[2],
                          &itemResults_, &itemExtra_}) {
        (*list)->Destroy();
        delete static_cast<CEngineObject*>(*list);   // slot 0 with 1, when the re-read field is set
    }
    // The record array's destructor.
    records.vtable = reinterpret_cast<void*>(0x0073FAA4);
    if (records.data) gamecrt::operator_delete(records.data);
}
EI_REPLACE_METHOD(0x0060E6B0, CCampPages::Close);

// --- 0060E3E0: the trade panel's destructor body (vtable 007417A8) --------------------------------

namespace {

// 00613630 (__fastcall: block, count): frees a vector's block of 0x3C-byte entries (STLport node
// allocator for small blocks). 006135F0 (stdcall, RET 8): the same for 0x18-byte entries.
__declspec(naked) void __fastcall FreeBlock3C(void*, int) EI_ORIGINAL(0x00613630)
__declspec(naked) void __stdcall FreeBlock18(void*, int) EI_ORIGINAL(0x006135F0)
// 00613CF0 (rewritten as TradeRow::Destroy; thiscall): a row's destructor, through its address.
__declspec(naked) void __fastcall DestroyRow(TradeRow*) EI_ORIGINAL(0x00613CF0)

// The rows' destructors, then the block (its capacity in rows).
void DestroyRows(TradeRows& rows) {
    for (TradeRow* at = rows.begin; at != rows.end; ++at) DestroyRow(at);
    FreeBlock18(rows.begin, static_cast<int>(rows.capacity - rows.begin));
}

} // namespace

void __fastcall TradePanelDestroy(CUiWindow* window) {
    auto* self = static_cast<CTradePanel*>(window);
    StringArrayDestruct(&self->texts_);
    FreeBlock3C(self->records_.begin, static_cast<int>(self->records_.capacity - self->records_.begin));
    DestroyRows(self->science_);
    DestroyRows(self->schools_);
    DestroyRows(self->perks_);
    DestroyRows(self->learnable_);
    self->DestroyBase();
}
EI_REPLACE(0x0060E3E0, TradePanelDestroy);

// --- 0060FE40: taking an item off the hero ---------------------------------------------------

namespace {

constexpr std::uintptr_t kSoundPutOn = 0x007937B0;    // "buttons\camp\put_on.wav"
constexpr std::uintptr_t kSoundPutOff = 0x007937C8;   // "buttons\camp\put_off.wav"
constexpr std::uintptr_t kSoundCancel = 0x007937E4;   // "buttons\messbox\cancel.wav"

// The local player's data as the client glue's 00578BA0 returns it (the party).
void* GluePlayerData() { return GameLocalPlayerDataOriginal(UiClientGlue()); }

// The hero's statistics (UnitStats at the record's +0x20).
UnitStats* StatsOf(PartyUnitRecord* hero) { return reinterpret_cast<UnitStats*>(hero->stats); }

// 005232B0 / 005230C0 (rewritten as UnitStats::ClearOuterLayers / AddArmourLayer; thiscall on the
// statistics): clears the outer layers / adds one (RET 4), through their addresses.
__declspec(naked) void __fastcall StatsClearOuterLayers(UnitStats*) EI_ORIGINAL(0x005232B0)
__declspec(naked) void __fastcall StatsAddLayer(UnitStats*, void*, CEngineObject*) EI_ORIGINAL(0x005230C0)

// The hero's statistics with the armour it wears as their outer layers again.
void RebuildArmourLayers(PartyUnitRecord* hero) {
    UnitStats* stats = StatsOf(hero);
    StatsClearOuterLayers(stats);
    for (int i = 0; i < static_cast<int>(hero->armorCount); ++i) StatsAddLayer(stats, nullptr, hero->armor[i]);
}

} // namespace

int CCampPages::TakeOff(std::uint8_t* item) {
    interface_->PlaySound(GameText(kSoundPutOff), 0);
    int kind = ItemType(item);
    if (kind < 0x3004 || (kind > 0x3006 && kind != 0x3008)) return 0;
    PartyUnitRecord* hero = VirtualHeroRecord();
    // 00661DB0: the item among the player's items.
    int result = reinterpret_cast<int>(PartyAddItemOriginal(GluePlayerData(), nullptr, item, 0, 1));
    switch (ItemType(item)) {
    case 0x3004: {
        // The chosen weapon moves back when the item lies before it.
        int count = static_cast<int>(hero->weaponCount);
        CEngineObject** weapons = hero->weapons;
        for (int i = 0; i < count; ++i) {
            if (weapons[i] != AsObject(item)) continue;
            if (static_cast<std::uint32_t>(i) < hero->weaponIndex) --hero->weaponIndex;
            break;
        }
        RecordRemoveQuickEntryOriginal(hero, nullptr, item);   // 00660A50
        if (hero->weaponIndex >= hero->weaponCount) hero->weaponIndex = hero->weaponCount - 1;
        VirtualRefreshWeapons();
        break;
    }
    case 0x3005: {
        if (heroes_[hero_]) {
            int type = ArmorGetTypeOriginal(reinterpret_cast<CItemArmor*>(item));
            UnitUnequipArmour(heroes_[hero_], nullptr, type);
            heroes_[hero_]->Controller()->ShowWeapon(hero->weaponIndex);
            heroes_[hero_]->Controller()->RefreshModel();
            heroes_[hero_]->Controller()->RefreshEquipment();
        }
        RecordRemoveWornEntryOriginal(hero, nullptr, item);    // 00660E10
        RebuildArmourLayers(hero);
        break;
    }
    case 0x3006:
        RecordRemoveBeltEntryOriginal(hero, nullptr, item);    // 00660C30
        break;
    case 0x3008:
        RecordRemoveSpellEntryOriginal(hero, nullptr, item);   // 00660FF0
        break;
    default:
        break;
    }
    StatsPanelDrawStatsOriginal(statsPanel_);
    return result;
}
EI_REPLACE_METHOD(0x0060FE40, CCampPages::TakeOff);

// --- 0060FCA0: putting an item on the hero ----------------------------------------------------

// The members the camp calls through their addresses (their hooks and wrappers see the calls).
__declspec(naked) int CCampPages::CallTakeOff(std::uint8_t*) EI_ORIGINAL(0x0060FE40)
__declspec(naked) void CCampPages::CallPutOn(std::uint8_t*) EI_ORIGINAL(0x0060FCA0)
__declspec(naked) void CCampPages::CallPutOnFromStrip(std::uint8_t*) EI_ORIGINAL(0x00610920)
__declspec(naked) void CCampPages::CallShowHero() EI_ORIGINAL(0x0060F830)

void CCampPages::PutOn(std::uint8_t* item) {
    interface_->PlaySound(GameText(kSoundPutOn), 0);
    PartyUnitRecord* hero = VirtualHeroRecord();
    switch (ItemType(item)) {
    case 0x3004:
        if (hero->weaponCount >= 4) return;
        RecordAddQuickEntryOriginal(hero, nullptr, item);   // 006608D0
        if (hero->weaponCount == 1) hero->weaponIndex = 0;
        VirtualRefreshWeapons();
        break;
    case 0x3005: {
        // Each piece on the same body part comes off first.
        int type = ArmorGetTypeOriginal(reinterpret_cast<CItemArmor*>(item));
        for (int i = 0; i < static_cast<int>(hero->armorCount); ++i) {
            if (ArmorGetTypeOriginal(reinterpret_cast<CItemArmor*>(hero->armor[i])) == type)
                CallTakeOff(reinterpret_cast<std::uint8_t*>(hero->armor[i]));
        }
        if (heroes_[hero_]) {
            void* worn = RecordAddWornEntryOriginal(hero, nullptr, item);   // 00660C90
            UnitEquipArmourOriginal(heroes_[hero_], nullptr, worn);         // 00530730
            heroes_[hero_]->Controller()->RefreshEquipment();
        }
        RebuildArmourLayers(hero);
        break;
    }
    case 0x3006:
        if (hero->quickItemCount >= 4) return;
        RecordAddBeltEntryOriginal(hero, nullptr, item);    // 00660AB0
        break;
    case 0x3008:
        if (hero->spellCount >= 8) return;
        RecordAddSpellEntryOriginal(hero, nullptr, item);   // 00660E70
        break;
    default:
        return;
    }
    PartyRemoveItemOriginal(GluePlayerData(), nullptr, item, 0);   // 00661F30
    StatsPanelDrawStatsOriginal(statsPanel_);
}
EI_REPLACE_METHOD(0x0060FCA0, CCampPages::PutOn);

// --- 00610920 / 00610AD0: moving an item between the camp's lists ------------------------------

namespace {
// The interface's Shift flag (set by its key handler 005F78C0).
bool ShiftHeld(CInterface3D* ui) { return ui->shiftDown_ != 0; }

// Whether `list` holds `item`.
bool ListHolds(CParty* list, const std::uint8_t* item) {
    for (int i = 0; i < CountOf(list); ++i)
        if (list->items_.data[i] == static_cast<const void*>(item)) return true;
    return false;
}
} // namespace

void CCampPages::PutOnFromStrip(std::uint8_t* item) {
    switch (page_) {
    case 0:
    case 1:
        CallPutOn(item);
        return;
    case 2:
        MoveBetween(this, item, spellParts_[2], LocalPlayerData(), 8, 1, 0);
        break;
    case 3:
        MoveBetween(this, item, spellResults_, LocalPlayerData(), 0, 0, 0);
        break;
    case 4:
        MoveBetween(this, item, itemParts_[2], LocalPlayerData(), 8, 1, 0);
        break;
    case 5:
        if (IsKindOne(item) && ShiftHeld(interface_)) {
            // With Shift the whole stack (+0x48), one at a time while the slots accept it.
            for (int n = Read<int>(item, 0x48); n > 0; --n) {
                if (!VirtualCanEquip(item, 1)) break;
                MoveBetween(this, item, itemResults_, LocalPlayerData(), 0, 0, 0);
            }
        } else {
            MoveBetween(this, item, itemResults_, LocalPlayerData(), 0, 0, 0);
        }
        break;
    case 6:
        MoveBetween(this, item, itemExtra_, LocalPlayerData(), 0x10, 1, 0);
        break;
    default:
        return;
    }
    interface_->PlaySound(GameText(kSoundPutOn), 0);
}
EI_REPLACE_METHOD(0x00610920, CCampPages::PutOnFromStrip);

void CCampPages::MoveItem(std::uint8_t* item, int mode) {
    if (!VirtualCanEquip(item, mode)) {
        interface_->PlaySound(GameText(kSoundCancel), 0);
        return;
    }
    // Back to the player's items and the item strip (from `list`).
    auto toPlayer = [&](std::uint8_t* it, CParty* list) {
        ScrollTo(itemStrip_, MoveBetween(this, it, LocalPlayerData(), list, 0, 1, 0));
    };
    // A made item back to the constructor's parts and its strip, the others to the player.
    auto back = [&](std::uint8_t* it, CParty* owner, CParty* list, CUiItemStrip* strip) {
        if (IsMade(it))
            ScrollTo(strip, MoveBetween(this, it, owner, list, 0, 1, 0));
        else
            toPlayer(it, list);
    };
    switch (mode) {
    case 0:
        ScrollTo(itemStrip_, reinterpret_cast<void*>(static_cast<std::intptr_t>(CallTakeOff(item))));
        return;
    case 1:
        CallPutOnFromStrip(item);
        return;
    case 2:
        if (page_ == 2)
            MoveBetween(this, item, spellParts_[1], spellParts_[0], 8, 1, 0);
        else if (page_ == 3)
            MoveBetween(this, item, spellResults_, spellParts_[0], 0, 0, 1);
        interface_->PlaySound(GameText(kSoundPutOn), 0);
        return;
    case 3:
        if (page_ == 4) {
            MoveBetween(this, item, itemParts_[1], itemParts_[0], 8, 1, 0);
        } else if (page_ == 5) {
            if (IsKindOne(item) && ShiftHeld(interface_)) {
                for (int n = Read<int>(item, 0x48); n > 0; --n) {
                    if (!VirtualCanEquip(item, 3)) break;
                    MoveBetween(this, item, itemResults_, itemParts_[0], 0, 0, 1);
                }
            } else {
                MoveBetween(this, item, itemResults_, itemParts_[0], 0, 0, 1);
            }
        }
        interface_->PlaySound(GameText(kSoundPutOn), 0);
        return;
    case 4:
        // Out of the spell constructor's sold or bought list.
        if (ListHolds(spellParts_[1], item)) {
            ScrollTo(spellStrip_, MoveBetween(this, item, spellParts_[0], spellParts_[1], 0, 1, 0));
        } else if (ListHolds(spellParts_[2], item)) {
            toPlayer(item, spellParts_[2]);
        } else {
            return;
        }
        break;
    case 5:
        if (ListHolds(itemParts_[1], item)) {
            ScrollTo(partStrip_, MoveBetween(this, item, itemParts_[0], itemParts_[1], 0, 1, 0));
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
        } else if (IsKindOne(item)) {
            back(item, itemParts_[0], itemResults_, partStrip_);
            // With Shift every other kind-one item goes too.
            if (ShiftHeld(interface_)) {
                for (int i = 0; i < CountOf(itemResults_); ++i) {
                    if (!IsKindOne(ItemOf(itemResults_, i))) continue;
                    back(ItemOf(itemResults_, i), itemParts_[0], itemResults_, partStrip_);
                    --i;
                }
            }
            // Then the first spell container left goes as well.
            for (int i = 0; i < CountOf(itemResults_); ++i) {
                if (!IsSpellContainer(ItemOf(itemResults_, i))) continue;
                back(ItemOf(itemResults_, i), itemParts_[0], itemResults_, partStrip_);
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
    interface_->PlaySound(GameText(kSoundPutOff), 0);
}
EI_REPLACE_METHOD(0x00610AD0, CCampPages::MoveItem);

// --- 0060F9E0 / 0060F980: the hero's unit after a change ---------------------------------------

namespace {
// 00532A20 (rewritten as CUnit::SetFromStats; thiscall, RET 4): the unit takes statistics, through
// its address.
__declspec(naked) void __fastcall UnitTakeStats(CUnit*, void*, UnitStats*) EI_ORIGINAL(0x00532A20)
} // namespace

void CCampPages::RefreshWeapons() {
    PartyUnitRecord* hero = VirtualHeroRecord();
    CUnit* unit = heroes_[hero_];
    unit->Controller()->ShowWeapon(static_cast<std::uint32_t>(-1));
    for (int slot = 0; slot < 4; ++slot) UnitRemoveWeaponOriginal(unit, nullptr, slot);   // 005310D0
    for (int i = 0; i < static_cast<int>(hero->weaponCount); ++i)
        UnitAddWeaponOriginal(unit, nullptr, hero->weapons[i]);                           // 00531070
    UnitSendEquipmentOriginal(unit);                                                      // 00531110
    unit->Controller()->ShowWeapon(hero->weaponIndex);
    unit->Controller()->RefreshModel();
    VirtualRefreshStats();
}
EI_REPLACE_METHOD(0x0060F9E0, CCampPages::RefreshWeapons);

void CCampPages::RefreshStats() {
    if (heroes_[hero_]) {
        PartyUnitRecord* hero = VirtualHeroRecord();
        UnitTakeStats(heroes_[hero_], nullptr, StatsOf(hero));
        heroes_[hero_]->Controller()->RefreshWounds();
    }
    StatsPanelDrawStatsOriginal(statsPanel_);
}
EI_REPLACE_METHOD(0x0060F980, CCampPages::RefreshStats);

// --- 0060F6B0 / 0060F710 / 0060F7A0 / 0060F830: turning and choosing the hero ------------------

namespace {
// fmod(turn + sign * 2 * seconds, *[007C7EE8]) through the game's _CIfmod, as the original
// computes it on the FPU.
float TurnedBy(double seconds, float turn, bool backwards) {
    float result;
    if (!backwards) {
        __asm {
            fld seconds
            fadd st(0), st(0)
            fadd turn
            fld dword ptr ds:[0x007C7EE8]
            mov eax, 0x006E95AA
            call eax
            fstp result
        }
    } else {
        __asm {
            fld seconds
            fadd st(0), st(0)
            fsubr turn
            fld dword ptr ds:[0x007C7EE8]
            mov eax, 0x006E95AA
            call eax
            fstp result
        }
    }
    return result;
}

constexpr int kHeroClass = 0x32;
} // namespace

void CCampPages::Update() {
    UiUpdateChildrenOriginal(this);   // 006010E0: the container's update
    int turning = turning_;
    float turn;
    std::memcpy(&turn, &heroTurn_, 4);
    // 00600270: the frame time in seconds (ST0, unrounded).
    if (turning == 1) {
        float turned = TurnedBy(WindowFrameTimeOriginal(this), turn, false);
        std::memcpy(&heroTurn_, &turned, 4);
    } else if (turning == 2) {
        float turned = TurnedBy(WindowFrameTimeOriginal(this), turn, true);
        std::memcpy(&heroTurn_, &turned, 4);
    }
}
EI_REPLACE_METHOD(0x0060F6B0, CCampPages::Update);

namespace {
// The search both directions share: from the current hero, step until a record of the hero
// class (fatal after more steps than there are records). 00513240 on the map objects manager
// (007AF5F8): a prototype's object class.
template <typename Step>
int FindHero(int start, Step step) {
    void* manager = *reinterpret_cast<void* const*>(0x007AF5F8);
    auto* party = static_cast<CParty*>(GluePlayerData());
    int i = start;
    for (int tries = 0;;) {
        int count = party->members_.size;
        if (tries > count) {
            game::FatalError(reinterpret_cast<const char*>(0x0079377C));
            break;
        }
        i = step(i, count);
        ++tries;
        PartyUnitRecord* record = &party->members_.data[i];
        if (PrototypeClass(manager, nullptr, record->texts[5]) == kHeroClass) break;
    }
    return i;
}
} // namespace

void CCampPages::NextHero() {
    int i = FindHero(hero_, [](int at, int count) { return at + 1 >= count ? 0 : at + 1; });
    if (i == hero_) return;
    hero_ = i;
    CallShowHero();
    VirtualRefreshWeapons();
}
void CCampPages::PreviousHero() {
    int i = FindHero(hero_, [](int at, int count) { return at - 1 < 0 ? count - 1 : at - 1; });
    if (i == hero_) return;
    hero_ = i;
    CallShowHero();
    VirtualRefreshWeapons();
}
EI_REPLACE_METHOD(0x0060F710, CCampPages::NextHero);
EI_REPLACE_METHOD(0x0060F7A0, CCampPages::PreviousHero);

void CCampPages::ShowHero() {
    CUnit* unit = heroes_[hero_];
    // The text table (00578E20 returns [007AF788]): 004DEFC0 whether it has a key (RET 4), 004DF8D0
    // its text into a CString (RET 8).
    void* table = GameFlowOriginal();
    mfc::CString name;
    mfc::CString own(static_cast<CEngineObject*>(unit)->GetName());
    if (own.GetLength() == 0) {
        PartyUnitRecord* hero = VirtualHeroRecord();
        own = hero->texts[1];
        int found;
        {
            mfc::CString prefix(GameText(0x007937A8));   // "pers "
            mfc::CString key = prefix + own;
            found = TextsOpenSection(table, nullptr, key);
        }
        if (found) TextsReadLineOriginal(table, nullptr, &name, 1);
    } else {
        name = own;
    }
    StatsPanelSetHeroOriginal(statsPanel_, nullptr, hero_, name);
    TradePanelSelectHeroOriginal(trade_, nullptr, hero_);
}
EI_REPLACE_METHOD(0x0060F830, CCampPages::ShowHero);

// --- 005F6240: the constructor ----------------------------------------------------------------

namespace {
// The item caches' maps (STLport, thiscall): 005FA420 / 005FA630 construct the header node from
// an allocator (RET 4; the byte they copy is overwritten), 005FA400 / 005FA610 empty it.
__declspec(naked) void __fastcall ConstructTextureMap(void*, void*, const void*) EI_ORIGINAL(0x005FA630)

// A cache: the map's header, an empty tree whose compare byte is `compare`, the limit 50.
void ConstructCache(ItemObjectCache& cache, void(__fastcall* construct)(void*, void*, const void*),
                    void(__fastcall* empty)(void*), const void* allocator, std::uint8_t compare) {
    construct(&cache, nullptr, allocator);
    cache.count = 0;
    cache.compare = compare;
    empty(&cache);
    cache.limit = 50;
}
} // namespace

CCampPages* CCampPages::Construct(CInterface3D* ui, int fromHeroScreen) {
    ConstructContainerAt(this, nullptr, ui);
    hoveredItem_ = nullptr;
    // The original passes the address of its first argument (the models') and of a stack byte
    // (the textures') as the allocators; the compare byte is the flag's low byte.
    std::uint8_t compare = static_cast<std::uint8_t>(fromHeroScreen);
    CInterface3D* slot = ui;
    std::uint8_t junk = 0;
    ConstructCache(models_, ConstructModelMap, EmptyModelMap, &slot, compare);
    ConstructCache(textures_, ConstructTextureMap, EmptyTextureMap, &junk, compare);
    vtable_ = reinterpret_cast<void**>(0x00740A2C);
    inputVtable_ = reinterpret_cast<void**>(0x007409F8);
    std::memcpy(&fromHeroScreen_, &compare, 1);   // the byte as it is
    heroes_.Construct(0x007402AC);
    StringArrayConstruct(&pageTitles_);
    vtable_ = reinterpret_cast<void**>(0x0074091C);
    inputVtable_ = reinterpret_cast<void**>(0x007408E8);
    return this;
}
EI_REPLACE_METHOD(0x005F6240, CCampPages::Construct);
