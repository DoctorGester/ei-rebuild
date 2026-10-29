// The HUD buttons that start or drop a pending action on the battle screen: a spell button
// an action button, a quick item slot, and the battle screen's pending action they share. See ui.h
// and docs/notes/ui.md ("Battle screen").
#include "ui.h"

#include "item.h"
#include "spell.h"
#include "unit.h"
#include "world.h"

#include "game_singletons.h"

#include <cstdint>

namespace {
constexpr const char* kSoundClick = "buttons\\battle\\click.wav";
constexpr const char* kSoundNoMagic = "buttons\\battle\\nomagic.wav";

// The shared tail of the quick item and the spell use: `spell` (an item's spell) used at once by `unit`'s
// side. An attack spell goes to the nearest unit `unit` sees (none: the "nomagic" sound and the pending
// action dropped; false), any other to `unit` itself; then UseOnUnit for a spell aimed at a unit, else
// UseAtPoint at the target's position, with `quick`.
bool UseAtOnce(CBattleScreen* screen, CInterface3D* ui, CUnit* unit, CSpell* spell, bool quick) {
    CUnit* target = unit;
    if (spell->IsOffensive()) {
        target = g_clientWorld.NearestVisibleUnit(unit);
        if (!target) {
            ui->PlaySound(kSoundNoMagic);
            screen->SetPendingAction(kPendingNone, -1, -1);
            return false;
        }
    } else if (!target) {
        return true;
    }
    if (spell->targetFlags_ & SpellTargetFlag::AtUnits)
        screen->UseOnUnit(target, quick, 0);
    else
        screen->UseAtPoint(target->localPosition_, unit, quick);
    return true;
}
} // namespace

// --- The pending action ------------------------------------------------------------------------

void CBattleScreen::SetPendingAction(PendingAction mode, int slot, int item) {
    pendingSlot_ = slot;
    pendingAction_ = mode;
    pendingItem_ = item;
    quickItems_->ClearHighlights();
    spellColumn_->ClearHighlights();
    actionColumn_->ClearHighlights();
    switch (mode) {
    case kPendingSpellOnUnit:
    case kPendingSpellAtPoint:
        spellColumn_->Highlight(slot, true);
        break;
    case kPendingQuickOnUnit:
    case kPendingQuickAtPoint:
    case kPendingItemOnObject:
    case kPendingKeyOnObject:
        quickItems_->Highlight(slot, true);
        break;
    case kPendingFollow:
    case kPendingOrder:
        actionColumn_->Highlight(slot, true);
        break;
    default:
        break;
    }
}

// --- A spell button ------------------------------------------------------------------

void CHudSpellColumn::Click(int index) {
    if (index < 0 || index >= count_) return;
    int previous = selected_;
    if (previous == index) {
        interface_->PlaySound(kSoundClick);
        owner_->SetPendingAction(kPendingNone, -1, -1);
        return;
    }
    if (previous >= 0 && previous < count_) Highlight(previous, false);
    Highlight(index, true);
    interface_->PlaySound(kSoundClick);
    CItem* item = unit_->GetSpell(index);
    if (!item) return;
    CSpell* spell = item->spell_;
    if (!spell) return;
    owner_->SetPendingAction((spell->targetFlags_ & SpellTargetFlag::AtUnits) ? kPendingSpellOnUnit : kPendingSpellAtPoint, index, -1);
}

// --- An action button ----------------------------------------------------------------

void CHudActionColumn::Select(int index) {
    if (index < 0 || index >= count_) return;
    int previous = selected_;
    if (previous == index) {
        interface_->PlaySound(kSoundClick);
        owner_->SetPendingAction(kPendingNone, -1, -1);
        return;
    }
    if (previous >= 0 && previous < count_) Highlight(previous, false);
    Highlight(index, true);
    interface_->PlaySound(kSoundClick);
    int action = static_cast<int>(actions_[index]);
    owner_->SetPendingAction(action == 0 ? kPendingOrder : kPendingFollow, index, action);
}

// --- A quick item slot ---------------------------------------------------------------

void CHudQuickItems::Pick(int slot) {
    if (!unit_) return;
    CItem* item = unit_->GetQuickItem(slot);
    if (!item) {
        owner_->SetPendingAction(kPendingNone, -1, -1);
        return;
    }
    interface_->PlaySound(kSoundClick);
    if (selected_ == slot) {
        owner_->SetPendingAction(kPendingNone, -1, -1);
        return;
    }
    QuickItemCategory category = item->GetCategory();
    if (category != QuickItemCategory::Wand && category != QuickItemCategory::Potion) return;
    PendingAction mode = kPendingNone;
    if (CSpell* spell = item->spell_)
        mode = (spell->targetFlags_ & SpellTargetFlag::AtUnits) ? kPendingQuickOnUnit : kPendingQuickAtPoint;
    owner_->SetPendingAction(mode, slot, -1);
}

// --- A quick item or spell used at once ------------------------------------

void CHudQuickItems::UseNow(int slot) {
    if (!unit_) return;
    CItem* item = unit_->GetQuickItem(slot);
    if (!item) return;
    owner_->SetPendingAction(kPendingQuickOnUnit, slot, -1);
    QuickItemCategory category = item->GetCategory();
    if ((category == QuickItemCategory::Wand || category == QuickItemCategory::Potion) && item->spell_)
        if (!UseAtOnce(owner_, interface_, unit_, item->spell_, true)) return;
    interface_->PlaySound(kSoundClick);
    owner_->SetPendingAction(kPendingNone, -1, -1);
}

void CHudSpellColumn::ClickAlternate(int index) {
    if (!unit_ || index < 0 || index >= count_) return;
    owner_->SetPendingAction(kPendingSpellOnUnit, index, -1);
    CItem* item = unit_->GetSpell(index);
    if (item && item->spell_)
        if (!UseAtOnce(owner_, interface_, unit_, item->spell_, false)) return;
    interface_->PlaySound(kSoundClick);
    owner_->SetPendingAction(kPendingNone, -1, -1);
}
