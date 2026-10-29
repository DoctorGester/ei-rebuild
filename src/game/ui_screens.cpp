// Interface screens: the camp's item strips, the multiplayer hero screen, the escape menu, the credits and the
// exchange window. See ui_screens.h and docs/notes/ui.md.
#include "spell.h"
#include "options.h"
#include "ui.h"
#include "ui_screens.h"
#include "ui_camp.h"
#include "net_classes.h"
#include "game_strings.h"

#include "core_maths.h"
#include "item.h"
#include "unit.h"
#include "party.h"
#include "world.h"

#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/nan_audit.h"
#include "game_singletons.h"
#include "lib/text_case.h"

#include <algorithm>
#include <bit>
#include <string>

namespace {
const char* const kSoundCampScroll = "buttons\\camp\\scroll.wav";
const char* const kSoundCampMode = "buttons\\camp\\mode.wav";
const char* const kSoundMenuStone = "buttons\\menu\\stone.wav";
const char* const kSoundMenuOk = "buttons\\menu\\ok.wav";
const char* const kSoundMessageOk = "buttons\\messbox\\ok.wav";

// Item fields read by the strips: a loot item's record type and record index.
std::uint32_t ItemRecord(const CItem* item) { return AsLoot(item)->index_; }

bool TabKeysEnabled() { return options::g_options->switchFilters_ != 0; }

// The item as loot (type ItemKind::Loot), or null.
const CItemLoot* LootOrNull(const CItem* item) {
    return item->Kind() == ItemKind::Loot ? AsLoot(item) : nullptr;
}

} // namespace

// --- Item predicates -------------------------------------------------------------------------

bool IsSpell(const CItem* item) {
    const CItemLoot* loot = LootOrNull(item);
    return loot && loot->kind_ == LootKind::Record && loot->recordType_ == LootRecordType::Spell;
}

bool IsModifier(const CItem* item) {
    const CItemLoot* loot = LootOrNull(item);
    return loot && loot->kind_ == LootKind::Record && loot->recordType_ == LootRecordType::Modifier;
}

bool IsSpellContainer(const CItem* item) {
    return item->Kind() == ItemKind::Spell;
}

bool IsPart(const CItem* item) {
    const CItemLoot* loot = LootOrNull(item);
    return loot && loot->kind_ == LootKind::Record && loot->recordType_ <= LootRecordType::Quick;
}

bool IsMaterial(const CItem* item) {
    const CItemLoot* loot = LootOrNull(item);
    return loot && loot->kind_ == LootKind::Material;
}

bool IsBuildable(const CItem* item) {
    auto* object = const_cast<CItem*>(item);
    const itemdb::ItemRecordHead* prototype = object->GetPrototype();
    if (prototype && !prototype->buildable) return false;
    ItemKind type = object->Kind();
    if (type == ItemKind::Weapon || type == ItemKind::Armor) return true;
    if (type != ItemKind::Quick) return false;
    prototype = object->GetPrototype();
    return prototype &&
           text::CompareIgnoringCase(prototype->materialClass, game::NoneText()) != 0;
}

namespace uiorig {
void PlayUiSound(CInterface3D* ui, const char* name, int flags) { ui->PlaySound(name, flags); }
void LeaveGame() { g_gameFlow->CloseSessions(); }
} // namespace uiorig

// --- CUiItemStrip -----------------------------------------------------------------------------

void CUiItemStrip::SetTab(int tab) {
    float& position = position_[tab_];
    position = static_cast<float>(static_cast<int>(libcrt::Ftol(static_cast<double>(pending_) + position)));
    tab_ = tab;
    for (int i = 0; i < 6; ++i) {
        float shade = i == tab ? 1.0f : 0.5f;
        float* color = SceneMesh(i + 2)->tint_;
        color[0] = shade;
        color[1] = shade;
        color[2] = shade;
    }
    pending_ = 0.0f;
    velocity_ = 0.0f;
    scrollTimer_ = 0.0f;
    scrollDirection_ = 0;
}

void CUiItemStrip::SetFilter(int filter) {
    for (float& position : position_) position = 0.0f;
    pending_ = 0.0f;
    filter_ = filter;
    velocity_ = 0.0f;
    // Each filter has its own button texture column (the tab objects' texture offset) and tooltips.
    int tipBase;
    std::uint32_t column;
    switch (filter) {
    case 0: tipBase = 0x526A; column = 0x3F280000; break;   // 0.65625
    case 1: tipBase = 0x52CE; column = 0x3EA80000; break;   // 0.328125
    case 2: tipBase = 0x5206; column = 0; break;
    default: return;
    }
    for (int i = 2; i < 8; ++i) {
        SceneMesh(i)->textureOffsetU_ = std::bit_cast<float>(column);
        regions_[i]->SetTip(tipBase + i, 1, 0);
    }
}

void CUiItemStrip::SelectTab(int tab) {
    if (TabKeysEnabled()) SetTab(tab);
}

void CUiItemStrip::CycleTab(int first, int count) {
    if (!TabKeysEnabled()) return;
    int next = first;
    if (first <= tab_ && tab_ < first + count) {
        next = tab_ + 1;
        if (next == first + count) next = first;
    }
    SetTab(next);
}

int CUiItemStrip::ItemTab(const CItem* item) {
    if (IsPart(item) || IsMaterial(item)) return 0;
    if (IsSpell(item) || IsModifier(item)) return 1;
    ItemKind type = item->Kind();
    if (type == ItemKind::Weapon || type == ItemKind::Armor || type == ItemKind::Quick || type == ItemKind::Spell) return 2;
    if (type == ItemKind::Loot) return 3;
    return (type != ItemKind::Quest) + 4;
}

int CUiItemStrip::SpellTab(const CItem* item) {
    if (IsSpell(item)) {
        // A record out of range is read through null, as in the original.
        const itemdb::SpellRecord* spell = itemdb::SpellAt(ItemRecord(item));
        switch (spell->castType) {
        case 0: return 0;
        case 1: return 1;
        case 2: return 2;
        default: return 5;
        }
    }
    if (IsModifier(item)) {
        const itemdb::SpellModifierRecord* modifier = itemdb::SpellModifierAt(ItemRecord(item));
        return (static_cast<int>(modifier->kind) > 5) + 3;
    }
    return 5;
}

int CUiItemStrip::ArmorTab(const CItem* item) {
    if (IsPart(item)) {
        switch (AsLoot(item)->recordType_) {
        case LootRecordType::Weapon: return 0;
        case LootRecordType::Armor: {
            auto* armor = itemdb::ArmorAt(ItemRecord(item));
            return static_cast<std::uint32_t>(armor->type) > 2 ? 1 : 2;
        }
        case LootRecordType::Quick: return 3;
        default: return 5;
        }
    }
    if (item->Kind() == ItemKind::Quick) return 3;
    return IsMaterial(item) ? 4 : 5;
}

void CForwardingStrip::RebuildList() {
    list_.clear();
    if (filter_ == 0 && tab_ == 5) {
        for (int i = 0; i < std::ssize(*source_); ++i) {
            int index = std::ssize(list_);
            CItem* item = (*source_)[i];
            list_.resize(index + 1);
            list_[index] = item;
        }
    } else {
        for (int i = 0; i < std::ssize(*source_); ++i) {
            CItem* item = (*source_)[i];
            if (TabOf(item) == tab_) {
                int index = std::ssize(list_);
                list_.resize(index + 1);
                list_[index] = item;
            }
        }
    }
    count_ = std::ssize(list_);
}

int CUiItemStrip::TabOf(const CItem* item) {
    switch (filter_) {
    case 0: return ItemTab(item);
    case 1: return SpellTab(item);
    case 2: return ArmorTab(item);
    default: return 0;
    }
}

void CUiItemStrip::ScrollToItem(CItem* item) {
    if (!item) return;
    CItem* wanted = item;
    float found = -1.0f;   // the list index of the last match (stored as float)
    list_.clear();
    // The original inlines the array's SetSize in the second loop.
    auto append = [&](CItem* entry) {
        if (wanted->IsSameAsSlot(entry)) found = static_cast<float>(list_.size());
        int index = std::ssize(list_);
        list_.resize(index + 1);
        list_[index] = entry;
    };
    if (filter_ == 0 && tab_ == 5) {
        for (int i = 0; i < std::ssize(*source_); ++i) append((*source_)[i]);
    } else {
        int tab = TabOf(item);
        if (TabKeysEnabled()) SetTab(tab);
        for (int i = 0; i < std::ssize(*source_); ++i)
            if (TabOf((*source_)[i]) == tab_) append((*source_)[i]);
    }
    EI_NAN_AUDIT(found);
    if (found >= 0.0f) {
        float& position = position_[tab_];
        double lowest = static_cast<double>(found) - 8.0 + 2.0;   // kept on the FPU
        if (lowest > position) {
            position = static_cast<float>(lowest);
        } else if (position > found) {
            position = found;
        }
    }
    pending_ = 0.0f;
    velocity_ = 0.0f;
}

CItem* CForwardingStrip::ItemAt(int x, int y, int* cursor) {
    if (!enabled_) return nullptr;
    if (RegionAt(x, y) != 8) return nullptr;
    switch (mode_) {
    case 1: *cursor = 0x16; break;
    case 2: *cursor = 0x18; break;
    case 3: *cursor = 0x17; break;
    }
    POINT point{x, y};
    interface_->ScalePointFromScreenEntry(&point);
    // A hundred pixels per slot from x 50, plus the tab's scroll position.
    constexpr float kSlotsPerPixel = 0.01f;
    int index = static_cast<int>(libcrt::Ftol(static_cast<double>(point.x - 50) * kSlotsPerPixel + position_[tab_]));
    RebuildList();
    if (index < 0 || index >= std::ssize(list_)) return nullptr;
    return list_[index];
}

void CUiItemStrip::SettleScroll() {
    // Releasing an arrow completes the step under way: the pending scroll keeps its fraction of a
    // slot and gains a whole slot when that fraction is under half.
    float pending = pending_;
    EI_NAN_AUDIT(pending);
    if (pending < 0.0f) {
        float fraction = static_cast<float>(pending - static_cast<double>(static_cast<int>(libcrt::Ftol(pending))));
        pending_ = fraction;
        if (fraction > -0.5f) pending_ = fraction - 1.0f;
    } else if (pending > 0.0f) {
        float fraction = static_cast<float>(pending - static_cast<double>(static_cast<int>(libcrt::Ftol(pending))));
        pending_ = fraction;
        if (fraction < 0.5f) pending_ = fraction + 1.0f;
    }
    scrollDirection_ = 0;
    scrollTimer_ = 0.0f;
    UiCapture(interface_) = nullptr;
}

void CUiItemStrip::OnLButtonDown(int, int x, int y) {
    CInterface3D* ui = interface_;
    int region = RegionAt(x, y);
    if (region == 0 || region == 1) {
        if (!UiShiftDown(ui)) {
            uiorig::PlayUiSound(ui, kSoundCampScroll, 0);
            UiCapture(ui) = Input();
            scrollDirection_ = region == 0 ? 1 : 2;
        } else {
            // Shift jumps to the start or (clamped later) the end.
            position_[tab_] = region == 0 ? 0.0f : 1e6f;
            pending_ = 0.0f;
            velocity_ = 0.0f;
        }
    }
    if (tabsEnabled_ && region >= 2 && region < 8) {
        uiorig::PlayUiSound(ui, kSoundCampMode, 0);
        SetTab(region - 2);
    }
    if (region >= 0) handled_ = true;
}

void CUiItemStrip::OnLButtonUp(int, int, int) {
    if (scrollDirection_ != 0) SettleScroll();
}

void CUiItemStrip::OnMouseMove(int, int x, int y) {
    int region = RegionAt(x, y);
    // Leaving the held arrow stops scrolling.
    if ((scrollDirection_ == 1 && region != 0) || (scrollDirection_ == 2 && region != 1)) SettleScroll();
}

BOOL CUiItemStrip::OnWheel(int, short delta, int x, int y) {
    if (RegionAt(x, y) == 8) {
        pending_ = static_cast<float>(static_cast<double>(delta / 120) + pending_);
        uiorig::PlayUiSound(interface_, kSoundCampScroll, 0);
    }
    return 1;
}

void CForwardingStrip::OnLButtonDownForward(int flags, int x, int y) {
    OnLButtonDown(flags, x, y);
    // The original passes its own first argument's stack memory as the cursor output.
    CItem* item = ItemAt(x, y, &flags);
    if (item) OwnerTakeItem(item);
}

// --- Heroes' skills ---------------------------------------------------------------------------

int HeroSchoolSkill(UnitStats* stats, int school) {
    return x87::fistp(static_cast<float>(stats->SpellSchoolSkill(school)));
}

int HeroManaUnits(UnitStats* stats) {
    // The maximum is stored as a float before the division; the quotient is not rounded.
    float maximum = static_cast<float>(stats->ManaCurve());
    return static_cast<int>(libcrt::Ftol(maximum / stats->Period20()));
}

bool HeroCanUseSpell(UnitStats* stats, CSpell* spell) {
    bool ok = true;
    int required = static_cast<int>(spell->skillNeeded_);
    if (HeroSchoolSkill(stats, spell->GetPrototype()->school) < required) ok = false;
    float cost = spell->manaCost_;
    int units = HeroManaUnits(stats);
    if (units < static_cast<int>(libcrt::Ftol(cost))) ok = false;
    return ok;
}

// --- CCampPages -------------------------------------------------------------------------------

PartySkill CCampPages::BestPartySkill(int school) {
    PartySkill best;
    for (int i = 0; i < std::ssize(LocalPlayerData()->members_); ++i) {
        UnitStats* stats = &LocalPlayerData()->members_[i].stats;
        best.skill = (std::max)(best.skill, HeroSchoolSkill(stats, school));
        best.mana = (std::max)(best.mana, HeroManaUnits(stats));
    }
    return best;
}

void CCampPages::TopicClicked(int page) {
    switch (page) {
    case 1: case 2: case 3: case 4: case 5: case 6: case 7: case 8: case 9: case 10: case 16:
        itemStrip_->SelectTab(2);
        break;
    case 13:
        itemStrip_->SelectTab(2);
        partStrip_->SelectTab(5);
        break;
    case 14:
        itemStrip_->SelectTab(0);
        partStrip_->CycleTab(0, 4);
        break;
    case 15:
        itemStrip_->SelectTab(0);
        partStrip_->SelectTab(4);
        break;
    case 19:
        itemStrip_->SelectTab(2);
        spellStrip_->SelectTab(5);
        break;
    case 20:
        itemStrip_->SelectTab(1);
        spellStrip_->CycleTab(0, 3);
        break;
    case 21:
        itemStrip_->SelectTab(1);
        spellStrip_->CycleTab(3, 2);
        break;
    }
}

// --- CMpHeroScreen ----------------------------------------------------------------------------

void CMpHeroScreen::CallShowHero(int index) { ShowHero(index); }

void CMpHeroScreen::ClampHeroSelection() {
    if (heroIndex_ < 0) heroIndex_ = 0;
    auto count = static_cast<std::uint32_t>(SavedHeroCount());
    if (static_cast<std::uint32_t>(heroIndex_) >= count) heroIndex_ = static_cast<int>(count) - 1;
    int selected = heroIndex_;
    // Ten rows are visible.
    if (heroTop_ + 10 <= selected) heroTop_ = selected - 9;
    if (heroTop_ > selected) heroTop_ = selected;
    heroScroll_->SetPosition(heroTop_);
    heroScroll_->TakePosition(&heroTop_);
    CallShowHero(heroIndex_);
}

void CMpHeroScreen::ClampVoiceSelection() {
    if (voiceIndex_ < 0) voiceIndex_ = 0;
    int count = static_cast<int>(voices_->end() - voices_->begin());
    if (static_cast<std::uint32_t>(voiceIndex_) >= static_cast<std::uint32_t>(count)) voiceIndex_ = count - 1;
    int selected = voiceIndex_;
    // Eleven rows are visible.
    if (voiceTop_ + 11 <= selected) voiceTop_ = selected - 10;
    if (voiceTop_ > selected) voiceTop_ = selected;
    voiceScroll_->SetPosition(voiceTop_);
    voiceScroll_->TakePosition(&voiceTop_);
    // Each voice is {file, name}; the hero keeps the file name in texts[6].
    const std::string& file = voices_->begin()[voiceIndex_].file;
    HeroRecord()->texts[6] = file;
    DrawVoices();
}

double CMpHeroScreen::EquipmentWeight() {
    PartyUnitRecord* hero = HeroRecord();
    double weight = 0.0;
    if (weapon_) weight = weapon_->Weight();
    if (armor_) weight += armor_->Weight();
    if (spellContainer_) weight += spellContainer_->Weight();
    // The original reads the armour list as the quick items.
    int count = std::ssize(hero->armor);
    CItem** quick = hero->armor.data();
    for (int i = 0; i < count; ++i)
        if (quick[i]) weight += quick[i]->Weight();

    return weight;
}

void CMpHeroScreen::OnItemChosen(CItem* item, int mode) {
    HeroRecord();   // called for nothing, as in the original
    if (mode == 0) {
        bool equipped = true;
        switch (item->Kind()) {
        case ItemKind::Weapon:
            weapon_ = item;
            weaponPanel_->ShowWeapon(item);
            break;
        case ItemKind::Quick:
            armor_ = item;
            weaponPanel_->ShowArmour(item);
            break;
        case ItemKind::Spell:
            spellContainer_ = item;
            spellPanel_->ShowEntry(item);
            break;
        default:
            equipped = false;
            break;
        }
        if (equipped) uiorig::PlayUiSound(interface_, kSoundMessageOk, 0);
    }
    RefreshStats();
}

bool CMpHeroScreen::CanEquip(CItem* item, int) {
    if (item->Kind() != ItemKind::Spell) return true;
    CSpell* spell = item->spell_;
    if (!spell) return true;
    return HeroCanUseSpell(&HeroRecord()->stats, spell);

}

PartyUnitRecord* CMpHeroScreen::GetHero() { return LocalPlayerData()->members_.data(); }

void CMpHeroScreen::OnLButtonUp(int flags, int x, int y) {
    CUiContainer::LeftUp(flags, x, y);
    // Releasing a rotate arrow.
    void*& capture = UiCapture(interface_);
    if (capture == Input()) capture = nullptr;
    rotating_ = 0;
}

// --- CEscMenu ---------------------------------------------------------------------------------

void CEscMenu::OnMouseMove(int flags, int x, int y) {
    CUiModel* model = models_[0];
    int region = RegionAt(x, y);
    // Hovering a stone plays its animation (the first two only in single player).
    switch (region) {
    case 0:
        if (hovered_ != 0 && !IsMultiplayerClient()) {
            uiorig::PlayUiSound(interface_, kSoundMenuStone, 0);
            model->Play(2);
        }
        hovered_ = 0;
        break;
    case 1:
        if (hovered_ != 1 && !IsMultiplayerClient()) {
            uiorig::PlayUiSound(interface_, kSoundMenuStone, 0);
            model->Play(3);
        }
        hovered_ = 1;
        break;
    case 2:
    case 3:
    case 4: {
        static const int kAnimation[5] = {0, 0, 5, 6, 4};
        if (hovered_ != region) {
            uiorig::PlayUiSound(interface_, kSoundMenuStone, 0);
            model->Play(kAnimation[region]);
        }
        hovered_ = region;
        break;
    }
    default:
        if (hovered_ != -1) model->Stop();
        hovered_ = -1;
        break;
    }
    CUiContainer::MouseMove(flags, x, y);
}

void CEscMenu::OnKeyDown(int key, int repeat, int flags) {
    CUiContainer::KeyDown(key, repeat, flags);
    if (key == VK_ESCAPE) {
        uiorig::PlayUiSound(interface_, kSoundMenuOk, 0);
        interface_->SetPendingValue(0);
        handled_ = true;
    }
}

void CEscMenu::OnMessageResult(int result) {
    // 0x10001: the quit box's yes; 0x100001/0x100002/0x110001 close the menu.
    if (static_cast<std::uint32_t>(result) > 0x100002) {
        if (result == 0x110001) interface_->SetPendingValue(0);
    } else if (static_cast<std::uint32_t>(result) >= 0x100001) {
        interface_->SetPendingValue(0);
    } else if (result == 0x10001) {
        uiorig::LeaveGame();
    }
}

// --- CCreditsScreen ---------------------------------------------------------------------------

void CCreditsScreen::Close() {
    if (fromGame_) interface_->SetPendingValue(0xCCCCCCCC);
    else uiorig::LeaveGame();
}

void CCreditsScreen::OnLButtonDown(int, int, int) { Close(); }

void CCreditsScreen::OnKeyDown(int key, int, int) {
    if (key == VK_RETURN || key == VK_ESCAPE || key == VK_SPACE) Close();
}

// --- CExchangeScreen --------------------------------------------------------------------------

void CExchangeScreen::OnKeyDown(int key, int repeat, int flags) {
    CUiContainer::KeyDown(key, repeat, flags);
    if (handled_) return;
    switch (key) {
    case VK_BACK:
        interface_->ChatKeyBackspace();
        handled_ = true;
        break;
    case VK_RETURN:
        interface_->ChatKeyEnter();
        handled_ = true;
        break;
    case VK_ESCAPE:
        net::ClientDropParty(net::g_gameClient);
        handled_ = true;
        break;
    }
}
