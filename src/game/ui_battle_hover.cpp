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
#include <cstdint>
#include <cstring>
#include <string>
#include "lib/text_case.h"

#include "world.h"
#include "game_data.h"
#include "game_singletons.h"

namespace {

constexpr std::uint32_t kFlagSelected = 0x10;
constexpr std::uint32_t kFlagShowInPanel = 0x400000;
constexpr int kCategoryWand = 8;

// Cursor shapes ('s index).
constexpr int kCursorAttack = 1;
constexpr int kCursorCast = 2;
constexpr int kCursorUse = 4;
constexpr int kCursorNo = 0xD;
constexpr int kCursorExit = 0xF;
constexpr int kCursorFlagBase = 0x10;   // + the first set flag of +0x18D..+0x192
constexpr int kCursorCantCast = 0x17;
constexpr int kCursorOrder = 0x18;

// The mouse cursor; selects shape `shape`.
void SetCursor(int shape) { TheCursor()->SetShape(shape); }

static const UnitPrototypeRecord* HoverPrototype(CUnit* unit) {
    return unit->GetPrototype();
}

// fcomp with 0.0 and: true for zero (either sign).
bool IsZero(float value) {
    EI_NAN_AUDIT(value);
    return value == 0.0f;
}

} // namespace

void CBattleScreen::HoverUnit(CUnit* unit, int x, int y) {
    auto unitDead = [](CUnit* u) { return u->IsDead() != 0; };
    auto pick = [x, y]() {
        Vector3 out;
        Vector3* p = TheClientWorld()->PickSurface(&out, x, y);
        return Vector3{p->x, p->y, p->z};
    };
    auto objectAt = [x, y]() {
        POINT point = {x, y};
        return static_cast<CUnit*>(TheClientWorld()->PickAt2(&point));
    };
    auto unitPosition = [](CUnit* u) {
        Vector3 at;
        std::memcpy(&at, &u->localPosition_, sizeof(at));
        return at;
    };

    // The unit panel's hover block (hoverUnit_, viewSize_[1] / viewHeight_[1] copied as dwords from
    // its prototype's +0x20 / +0x24, strings_[1]). A unit without flag 0x400000 counts as none
    // below; a dead one is kept.
    CHudUnitPanel* panel = unitPanel_;
    if (unit && !(unit->flags_ & kFlagShowInPanel)) unit = nullptr;
    if (unit && !unitDead(unit)) {
        if (unit != panel->hoverUnit_) {
            panel->hoverUnit_ = unit;
            std::memcpy(&panel->viewSize_[1], &HoverPrototype(unit)->panelViewSize, 4);
            std::memcpy(&panel->viewHeight_[1], &HoverPrototype(panel->hoverUnit_)->height, 4);
            panel->owner_->UnitName(panel->hoverUnit_, &panel->strings_[1]);
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
        if (!TheClientWorld()->FindExitAt(&at.x, &exit)) return;
        SetCursor(kCursorExit);
        return;
    }
    for (int i = 0; i < 6; ++i) {
        if (orderKeys_[i]) {
            SetCursor(kCursorFlagBase + i);
            return;
        }
    }
    if (interface_->ctrlDown_) {
        SetCursor(kCursorAttack);
        return;
    }

    switch (pendingAction_) {
    case 0:   // plain
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
            if (!TheClientWorld()->FindExitAt(&at.x, &exit)) return;
            std::string name = GameExitTarget(exit);
            if (text::CompareIgnoringCase(name,
                                game::NoneText()) != 0) {
                name = std::string("z.") + name;
                double value = LocalPlayerVariable(name);
                const float one = 1.0f /*0073B84C*/;
                // FCOMP; TEST AH,0x40; JNZ: equal skips.
                EI_NAN_AUDIT(value);
                if (value != one) SetCursor(kCursorExit);
            }
            return;
        }
    case 1: {   // a spell on a unit
        if (!unit || unitDead(unit)) {
            shape = kCursorCantCast;
            break;
        }
        Vector3 at = unitPosition(unit);
        if (!first || first->CanCastSpell(pendingSlot_, &at.x) != 0) {
            shape = kCursorCantCast;
            break;
        }
        shape = kCursorCast;
        break;
    }
    case 2:   // a spell on a point
    case 4: {   // a quick item on a point
        Vector3 at = (unit && !unitDead(unit)) ? unitPosition(unit) : pick();
        if ((IsZero(at.x) && IsZero(at.y)) || !first) {
            shape = kCursorCantCast;
            break;
        }
        int refused = pendingAction_ == 2 ? first->CanCastSpell(pendingSlot_, &at.x)
                                          : first->CanUseQuickItem(pendingSlot_, &at.x);
        shape = refused ? kCursorCantCast : kCursorCast;
        break;
    }
    case 3: {   // a quick item on a unit (a wand not on the user itself)
        shape = kCursorCantCast;
        if (!unit || unitDead(unit)) break;
        Vector3 at = unitPosition(unit);
        if (!first || first->CanUseQuickItem(pendingSlot_, &at.x) != 0) break;
        CItem* item = first->GetQuickItem(pendingSlot_);
        if (item && item->GetCategory() == kCategoryWand && unit != first) break;
        shape = kCursorCast;
        break;
    }
    case 5: {   // an item on an object
        CUnit* target = unit ? unit : objectAt();
        shape = (target && first && target->IsUsable() != 0) ? kCursorUse : kCursorNo;
        break;
    }
    case 6: {   // a spell on an object
        CUnit* target = unit ? unit : objectAt();
        shape = (target && first && target->IsUsable() != 0 && target->AcceptsKey(&itemUseKey_) != 0)
                    ? kCursorUse
                    : kCursorNo;
        break;
    }
    case 7:   // follow
        shape = (first && unit && !unitDead(unit)) ? kCursorExit : kCursorNo;
        break;
    case 8: {   // an order on an object
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
    SetCursor(shape);
}
