// The battle screen's mouse-over: the unit under the mouse in the unit panel and the
// cursor shape for what a click would do. See ui.h (CBattleScreen::HoverUnit) and docs/notes/ui.md
// ("Battle screen").
#include "item.h"
#include "world_glue.h"
#include "ui.h"

#include "ui_cursor.h"
#include "unit.h"
#include "unit_db.h"

#include "ei/x87.h"
#include "lib/nan_audit.h"
#include <bit>
#include <cstdint>
#include <cstring>
#include <string>
#include "lib/text_case.h"

#include "world.h"
#include "game_singletons.h"

namespace {

constexpr std::uint32_t kFlagSelected = 0x10;
constexpr std::uint32_t kFlagShowInPanel = 0x400000;

// Cursor shapes ('s index).
constexpr int kCursorAttack = 1;
constexpr int kCursorCast = 2;
constexpr int kCursorUse = 4;
constexpr int kCursorNo = 0xD;
constexpr int kCursorExit = 0xF;
constexpr int kCursorFlagBase = 0x10;   // + the first set flag of +0x18D..+0x192
constexpr int kCursorCantCast = 0x17;
constexpr int kCursorOrder = 0x18;

// fcomp with 0.0 and: true for zero (either sign).
bool IsZero(float value) {
    EI_NAN_AUDIT(value);
    return value == 0.0f;
}

} // namespace

void CBattleScreen::HoverUnit(CUnit* unit, int x, int y) {
    auto unitDead = [](CUnit* u) { return u->IsDead(); };
    auto pick = [x, y]() {
        Vector3 out;
        Vector3* p = g_clientWorld->PickSurface(&out, x, y);
        return Vector3{p->x, p->y, p->z};
    };
    auto objectAt = [x, y]() {
        POINT point = {x, y};
        return static_cast<CUnit*>(g_clientWorld->PickAt2(&point));
    };
    auto unitPosition = [](CUnit* u) {
        Vector3 at;
        at = u->localPosition_;
        return at;
    };

    // The unit panel's hover block (hoverUnit_, viewSize_[1] / viewHeight_[1] copied bit for bit from
    // its prototype's +0x20 / +0x24, strings_[1]). A unit without flag 0x400000 counts as none
    // below; a dead one is kept.
    CHudUnitPanel* panel = unitPanel_;
    if (unit && !(unit->flags_ & kFlagShowInPanel)) unit = nullptr;
    if (unit && !unitDead(unit)) {
        if (unit != panel->hoverUnit_) {
            panel->hoverUnit_ = unit;
            panel->viewSize_[1] = unit->GetPrototype()->panelViewSize;
            panel->viewHeight_[1] = std::bit_cast<float>(panel->hoverUnit_->GetPrototype()->height);
            panel->strings_[1] = UnitName(panel->hoverUnit_);
        }
    } else if (panel->hoverUnit_) {
        panel->hoverUnit_ = nullptr;
    }

    int count = std::ssize(party_);
    if (count <= 0) return;
    CUnit* first = nullptr;
    int selected = 0;
    for (int i = 0; i < count; ++i) {
        CUnit* member = party_[i];
        if (!member || !(member->flags_ & kFlagSelected)) continue;
        if (!first) first = member;
        ++selected;
    }
    if (selected == 0) return;

    int shape = 0;
    if (interface_->altDown_) {
        Vector3 at = pick();
        int exit;
        if (!g_clientWorld->FindExitAt(&at.x, &exit)) return;
        g_cursor.SetShape(kCursorExit);
        return;
    }
    for (int i = 0; i < 6; ++i) {
        if (orderKeys_[i]) {
            g_cursor.SetShape(kCursorFlagBase + i);
            return;
        }
    }
    if (interface_->ctrlDown_) {
        g_cursor.SetShape(kCursorAttack);
        return;
    }

    switch (pendingAction_) {
    case kPendingNone:
        if (unit) {
            if (unitDead(unit)) {
                shape = kCursorUse;
                break;
            }
            if (party_.empty() || !party_[0]) return;
            if (!party_[0]->IsHostileTo(unit)) return;
            shape = kCursorAttack;
            break;
        }
        {
            CUnit* object = objectAt();
            if (object && object->IsUsable() != 0 && object->IsDirectUseBlocked() == 0 &&
                selected == 1) {
                shape = kCursorUse;
                break;
            }
            // Over a zone exit: the exit cursor unless the zone's "z.<name>" variable is 1 ("none"
            // is asked as is).
            Vector3 at = pick();
            int exit;
            if (!g_clientWorld->FindExitAt(&at.x, &exit)) return;
            std::string name = GameExitTarget(exit);
            if (text::CompareIgnoringCase(name,
                                game::NoneText()) != 0) {
                name = std::string("z.") + name;
                double value = LocalPlayerVariable(name);
                const float one = 1.0f;
                // equal skips.
                EI_NAN_AUDIT(value);
                if (value != one) g_cursor.SetShape(kCursorExit);
            }
            return;
        }
    case kPendingSpellOnUnit: {
        if (!unit || unitDead(unit)) {
            shape = kCursorCantCast;
            break;
        }
        if (!first || first->CanCastSpell(pendingSlot_) != 0) {
            shape = kCursorCantCast;
            break;
        }
        shape = kCursorCast;
        break;
    }
    case kPendingSpellAtPoint:
    case kPendingQuickAtPoint: {
        Vector3 at = (unit && !unitDead(unit)) ? unitPosition(unit) : pick();
        if ((IsZero(at.x) && IsZero(at.y)) || !first) {
            shape = kCursorCantCast;
            break;
        }
        int refused = pendingAction_ == kPendingSpellAtPoint ? first->CanCastSpell(pendingSlot_)
                                          : first->CanUseQuickItem(pendingSlot_);
        shape = refused ? kCursorCantCast : kCursorCast;
        break;
    }
    case kPendingQuickOnUnit: {   // a potion only on the user itself
        shape = kCursorCantCast;
        if (!unit || unitDead(unit)) break;
        if (!first || first->CanUseQuickItem(pendingSlot_) != 0) break;
        CItem* item = first->GetQuickItem(pendingSlot_);
        if (item && item->GetCategory() == QuickItemCategory::Potion && unit != first) break;
        shape = kCursorCast;
        break;
    }
    case kPendingItemOnObject: {
        CUnit* target = unit ? unit : objectAt();
        shape = (target && first && target->IsUsable() != 0) ? kCursorUse : kCursorNo;
        break;
    }
    case kPendingKeyOnObject: {
        CUnit* target = unit ? unit : objectAt();
        shape = (target && first && target->IsUsable() != 0 && target->AcceptsKey(&itemUseKey_) != 0)
                    ? kCursorUse
                    : kCursorNo;
        break;
    }
    case kPendingFollow:
        shape = (first && unit && !unitDead(unit)) ? kCursorExit : kCursorNo;
        break;
    case kPendingOrder: {
        if (unit) {
            shape = kCursorNo;
            if (!first) break;
            bool member = false;
            for (int i = 0; i < count; ++i) member |= unit == party_[i];
            if (!member) shape = kCursorOrder;
            break;
        }
        CUnit* target = objectAt();
        shape = (target && first && target->IsUsable() != 0) ? kCursorUse : kCursorNo;
        break;
    }
    default:
        return;
    }
    g_cursor.SetShape(shape);
}
