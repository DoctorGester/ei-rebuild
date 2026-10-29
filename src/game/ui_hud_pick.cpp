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
    if (spell->targetFlags & SpellTargetFlag::AtUnits)
        screen->UseOnUnit(target, quick, 0);
    else
        screen->UseAtPoint(target->localPosition, unit, quick);
    return true;
}
} // namespace

// --- The pending action ------------------------------------------------------------------------

void CBattleScreen::SetPendingAction(PendingAction mode, int slot, int item) {
    pendingSlot = slot;
    pendingAction = mode;
    pendingItem = item;
    quickItems->ClearHighlights();
    spellColumn->ClearHighlights();
    actionColumn->ClearHighlights();
    switch (mode) {
    case kPendingSpellOnUnit:
    case kPendingSpellAtPoint:
        spellColumn->Highlight(slot, true);
        break;
    case kPendingQuickOnUnit:
    case kPendingQuickAtPoint:
    case kPendingItemOnObject:
    case kPendingKeyOnObject:
        quickItems->Highlight(slot, true);
        break;
    case kPendingFollow:
    case kPendingOrder:
        actionColumn->Highlight(slot, true);
        break;
    default:
        break;
    }
}

// --- A spell button ------------------------------------------------------------------

void CHudSpellColumn::Click(int index) {
    if (index < 0 || index >= count) return;
    int previous = selected;
    if (previous == index) {
        ui->PlaySound(kSoundClick);
        owner->SetPendingAction(kPendingNone, -1, -1);
        return;
    }
    if (previous >= 0 && previous < count) Highlight(previous, false);
    Highlight(index, true);
    ui->PlaySound(kSoundClick);
    CItem* item = unit->GetSpell(index);
    if (!item) return;
    CSpell* spell = item->spell;
    if (!spell) return;
    owner->SetPendingAction((spell->targetFlags & SpellTargetFlag::AtUnits) ? kPendingSpellOnUnit : kPendingSpellAtPoint, index, -1);
}

// --- An action button ----------------------------------------------------------------

void CHudActionColumn::Select(int index) {
    if (index < 0 || index >= count) return;
    int previous = selected;
    if (previous == index) {
        ui->PlaySound(kSoundClick);
        owner->SetPendingAction(kPendingNone, -1, -1);
        return;
    }
    if (previous >= 0 && previous < count) Highlight(previous, false);
    Highlight(index, true);
    ui->PlaySound(kSoundClick);
    int action = static_cast<int>(actions[index]);
    owner->SetPendingAction(action == 0 ? kPendingOrder : kPendingFollow, index, action);
}

// --- A quick item slot ---------------------------------------------------------------

void CHudQuickItems::Pick(int slot) {
    if (!unit) return;
    CItem* item = unit->GetQuickItem(slot);
    if (!item) {
        owner->SetPendingAction(kPendingNone, -1, -1);
        return;
    }
    ui->PlaySound(kSoundClick);
    if (selected == slot) {
        owner->SetPendingAction(kPendingNone, -1, -1);
        return;
    }
    QuickItemCategory category = item->GetCategory();
    if (category != QuickItemCategory::Wand && category != QuickItemCategory::Potion) return;
    PendingAction mode = kPendingNone;
    if (CSpell* spell = item->spell)
        mode = (spell->targetFlags & SpellTargetFlag::AtUnits) ? kPendingQuickOnUnit : kPendingQuickAtPoint;
    owner->SetPendingAction(mode, slot, -1);
}

// --- A quick item or spell used at once ------------------------------------

void CHudQuickItems::UseNow(int slot) {
    if (!unit) return;
    CItem* item = unit->GetQuickItem(slot);
    if (!item) return;
    owner->SetPendingAction(kPendingQuickOnUnit, slot, -1);
    QuickItemCategory category = item->GetCategory();
    if ((category == QuickItemCategory::Wand || category == QuickItemCategory::Potion) && item->spell)
        if (!UseAtOnce(owner, ui, unit, item->spell, true)) return;
    ui->PlaySound(kSoundClick);
    owner->SetPendingAction(kPendingNone, -1, -1);
}

void CHudSpellColumn::ClickAlternate(int index) {
    if (!unit || index < 0 || index >= count) return;
    owner->SetPendingAction(kPendingSpellOnUnit, index, -1);
    CItem* item = unit->GetSpell(index);
    if (item && item->spell)
        if (!UseAtOnce(owner, ui, unit, item->spell, false)) return;
    ui->PlaySound(kSoundClick);
    owner->SetPendingAction(kPendingNone, -1, -1);
}
