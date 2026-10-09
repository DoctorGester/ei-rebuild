// The HUD buttons that start or drop a pending action on the battle screen: a spell button
// (0063EC90), an action button (0063FB50), a quick item slot (0063DD70), and the battle screen's
// pending action they share (inlined by the original). See ui.h and docs/notes/ui.md
// ("Nineteenth pass (ticket I10)").
#include "ui.h"

#include "item.h"
#include "spell.h"
#include "unit.h"
#include "world.h"

#include "ei/hook.h"
#include "originals_ui.h"

#include <cstdint>

namespace {
constexpr const char* kSoundClick = "buttons\\battle\\click.wav";       // 00794460
constexpr const char* kSoundNoMagic = "buttons\\battle\\nomagic.wav";   // 00794524
constexpr int kQuickSlots = 8;
// Item categories (CItem::GetCategory) that carry a spell to aim: wands and scrolls.
constexpr int kCategoryWand = 5;
constexpr int kCategoryScroll = 8;
// Spell data flag (CSpell +0x3C): the spell is aimed at a unit (kind 'u' in spells.sdb).
constexpr std::uint32_t kSpellOnUnit = 0x10000000;

// The pending actions these buttons start (CBattleScreen::pendingAction_).
enum PendingAction : int {
    kPendingNone = 0,
    kPendingSpellOnUnit = 1,
    kPendingSpellAtPoint = 2,
    kPendingQuickOnUnit = 3,
    kPendingQuickAtPoint = 4,
    kPendingFollow = 7,
    kPendingOrder = 8,
};

// The spell (+0x44) an item carries, or null.
CSpell* SpellOf(CEngineObject* item) { return static_cast<CSpell*>(static_cast<CItem*>(item)->spell_); }

// The shared tail of 0063E020 and 0063EF60: `spell` (an item's +0x44) used at once by `unit`'s
// side. An attack spell goes to the nearest unit `unit` sees (none: the "nomagic" sound and the
// pending action dropped; false), any other to `unit` itself; then UseOnUnit for a spell aimed at
// a unit, else UseAtPoint at the target's position (both through their addresses), with `quick`.
bool UseAtOnce(CBattleScreen* screen, CInterface3D* ui, CUnit* unit, CSpell* spell, char quick) {
    CUnit* target = unit;
    if (SpellIsOffensiveOriginal(spell)) {
        target = static_cast<CUnit*>(WorldClient()->NearestVisibleUnit(unit));
        if (!target) {
            ui->PlaySound(kSoundNoMagic, 0);
            screen->SetPendingAction(kPendingNone, -1, -1);
            return false;
        }
    } else if (!target) {
        return true;
    }
    if (spell->targetFlags_ & kSpellOnUnit)
        BattleUseOnUnitOriginal(screen, nullptr, target, quick, 0);
    else
        BattleUseAtPointOriginal(screen, nullptr, target->positionX, target->positionY, target->positionZ, unit, quick);
    return true;
}
} // namespace

// --- Forwarders --------------------------------------------------------------------------------

void CHudQuickItems::HighlightAt(int slot, int on) { QuickPanelSetOriginal(this, nullptr, slot, on); }
void CHudSpellColumn::HighlightAt(int index, int on) { SpellPanelSetOriginal(this, nullptr, index, on); }
void CHudActionColumn::HighlightAt(int index, int on) { ActionPanelSetOriginal(this, nullptr, index, on); }
__declspec(naked) void CHudSpellColumn::ClickAt(int) EI_ORIGINAL(0x0063EC90)
__declspec(naked) void CHudActionColumn::SelectAt(int) EI_ORIGINAL(0x0063FB50)

// --- The pending action ------------------------------------------------------------------------

void CBattleScreen::SetPendingAction(int mode, int slot, int item) {
    pendingSlot_ = slot;
    pendingAction_ = mode;
    pendingItem_ = item;
    for (int i = 0; i < kQuickSlots; ++i) quickItems_->HighlightAt(i, 0);
    quickItems_->selected_ = -1;
    for (int i = 0; i < spellColumn_->count_; ++i) spellColumn_->HighlightAt(i, 0);
    spellColumn_->selected_ = -1;
    for (int i = 0; i < actionColumn_->count_; ++i) actionColumn_->HighlightAt(i, 0);
    actionColumn_->selected_ = -1;
    switch (mode) {
    case 1:
    case 2:
        spellColumn_->HighlightAt(slot, 1);
        break;
    case 3:
    case 4:
    case 5:
    case 6:
        quickItems_->HighlightAt(slot, 1);
        break;
    case 7:
    case 8:
        actionColumn_->HighlightAt(slot, 1);
        break;
    default:
        break;
    }
}

// --- 0063EC90: a spell button ------------------------------------------------------------------

void CHudSpellColumn::Click(int index) {
    if (index < 0 || index >= count_) return;
    int previous = selected_;
    if (previous == index) {
        interface_->PlaySound(kSoundClick, 0);
        owner_->SetPendingAction(kPendingNone, -1, -1);
        return;
    }
    if (previous >= 0 && previous < count_) HighlightAt(previous, 0);
    HighlightAt(index, 1);
    interface_->PlaySound(kSoundClick, 0);
    CEngineObject* item = unit_->GetSpell(index);
    if (!item) return;
    CSpell* spell = SpellOf(item);
    if (!spell) return;
    int mode = (spell->targetFlags_ & kSpellOnUnit) ? kPendingSpellOnUnit : kPendingSpellAtPoint;
    owner_->SetPendingAction(mode, index, -1);
}
namespace {
void __fastcall SpellColumnClick(CHudSpellColumn* self, void*, int index) { self->Click(index); }
} // namespace
EI_REPLACE(0x0063EC90, SpellColumnClick);

// --- 0063FB50: an action button ----------------------------------------------------------------

void CHudActionColumn::Select(int index) {
    if (index < 0 || index >= count_) return;
    int previous = selected_;
    if (previous == index) {
        interface_->PlaySound(kSoundClick, 0);
        owner_->SetPendingAction(kPendingNone, -1, -1);
        return;
    }
    if (previous >= 0 && previous < count_) HighlightAt(previous, 0);
    HighlightAt(index, 1);
    interface_->PlaySound(kSoundClick, 0);
    int action = static_cast<int>(actions_.data[index]);
    owner_->SetPendingAction(action == 0 ? kPendingOrder : kPendingFollow, index, action);
}
namespace {
void __fastcall ActionColumnSelect(CHudActionColumn* self, void*, int index) { self->Select(index); }
} // namespace
EI_REPLACE(0x0063FB50, ActionColumnSelect);

// --- 0063DD70: a quick item slot ---------------------------------------------------------------

void CHudQuickItems::Pick(int slot) {
    if (!unit_) return;
    CEngineObject* item = unit_->GetQuickItem(slot);
    if (!item) {
        owner_->SetPendingAction(kPendingNone, -1, -1);
        return;
    }
    interface_->PlaySound(kSoundClick, 0);
    if (selected_ == slot) {
        owner_->SetPendingAction(kPendingNone, -1, -1);
        return;
    }
    int category = static_cast<CItem*>(item)->GetCategory();
    if (category != kCategoryWand && category != kCategoryScroll) return;
    int mode = kPendingNone;
    if (CSpell* spell = SpellOf(item))
        mode = (spell->targetFlags_ & kSpellOnUnit) ? kPendingQuickOnUnit : kPendingQuickAtPoint;
    owner_->SetPendingAction(mode, slot, -1);
}
namespace {
void __fastcall QuickItemsPick(CHudQuickItems* self, void*, int slot) { self->Pick(slot); }
} // namespace
EI_REPLACE(0x0063DD70, QuickItemsPick);

// --- 0063E020 / 0063EF60: a quick item or spell used at once ------------------------------------

__declspec(naked) void CHudQuickItems::UseNowAt(int) EI_ORIGINAL(0x0063E020)

void CHudQuickItems::UseNow(int slot) {
    if (!unit_) return;
    CEngineObject* item = unit_->GetQuickItem(slot);
    if (!item) return;
    owner_->SetPendingAction(kPendingQuickOnUnit, slot, -1);
    int category = static_cast<CItem*>(item)->GetCategory();
    if ((category == kCategoryWand || category == kCategoryScroll) && SpellOf(item))
        if (!UseAtOnce(owner_, interface_, unit_, SpellOf(item), 1)) return;
    interface_->PlaySound(kSoundClick, 0);
    owner_->SetPendingAction(kPendingNone, -1, -1);
}
namespace {
void __fastcall QuickItemsUseNow(CHudQuickItems* self, void*, int slot) { self->UseNow(slot); }
} // namespace
EI_REPLACE(0x0063E020, QuickItemsUseNow);

void CHudSpellColumn::ClickAlternate(int index) {
    if (!unit_ || index < 0 || index >= count_) return;
    owner_->SetPendingAction(kPendingSpellOnUnit, index, -1);
    CEngineObject* item = unit_->GetSpell(index);
    if (item && SpellOf(item))
        if (!UseAtOnce(owner_, interface_, unit_, SpellOf(item), 0)) return;
    interface_->PlaySound(kSoundClick, 0);
    owner_->SetPendingAction(kPendingNone, -1, -1);
}
namespace {
void __fastcall SpellColumnClickAlternate(CHudSpellColumn* self, void*, int index) { self->ClickAlternate(index); }
} // namespace
EI_REPLACE(0x0063EF60, SpellColumnClickAlternate);
