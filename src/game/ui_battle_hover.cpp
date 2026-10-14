// The battle screen's mouse-over (00639EF0): the unit under the mouse in the unit panel and the
// cursor shape for what a click would do. See ui.h (CBattleScreen::HoverUnit) and docs/notes/ui.md
// ("Tenth pass").
#include "ui.h"

#include "unit.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "originals_units.h"
#include "originals_world.h"

#include <cstdint>
#include <cstring>
#include "originals_ui.h"

namespace {

template <typename T> T& Field(void* base, int offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(base) + offset);
}

constexpr int kSlotIsDead = 55;             // object slot 55 (+0xDC): non-zero for a dead unit
constexpr int kSlotCanTarget = 38;          // slot 38 (+0x98)
constexpr int kSlotSlot39 = 39;             // slot 39 (+0x9C)
constexpr int kSlotAcceptsSpell = 42;       // slot 42 (+0xA8), with the screen's +0x220
constexpr std::uint32_t kFlagSelected = 0x10;
constexpr std::uint32_t kFlagShowInPanel = 0x400000;
constexpr int kCategoryWand = 8;

// Cursor shapes (005E5200's index).
constexpr int kCursorAttack = 1;
constexpr int kCursorCast = 2;
constexpr int kCursorUse = 4;
constexpr int kCursorNo = 0xD;
constexpr int kCursorExit = 0xF;
constexpr int kCursorFlagBase = 0x10;   // + the first set flag of +0x18D..+0x192
constexpr int kCursorCantCast = 0x17;
constexpr int kCursorOrder = 0x18;

// The mouse cursor (global at 007C7BD8); 005E5200 selects shape `shape`.
struct HoverCursor {
    int SetShape(int shape);
};
int HoverCursor::SetShape(int a2) { return CursorSetShapeOriginal((void*)this, nullptr, a2); }
void SetCursor(int shape) { reinterpret_cast<HoverCursor*>(0x007C7BD8)->SetShape(shape); }

void* const kClientWorld = reinterpret_cast<void*>(0x007AF5B8);
void* const kPlayerGlue = reinterpret_cast<void*>(0x007AFEA5);

// The client world (007AF5B8): the object under a screen point (x, y pair), the ground point under
// a screen point (PickSurface), the zone exit at a point (FindExitAt, RET 8).
__declspec(naked) BOOL __fastcall HoverFindExit(void*, void*, const float*, int*) EI_ORIGINAL(0x00567440)
// The local player glue (007AFEA5): 00578A40 an exit's zone name (RET 4), 00578B50 the local
// player's variable `name` (RET 4, ST0).
// Units: 00530930 the prototype record (__fastcall), 00530150 IsHostileTo (RET 4), 0052F820
// CanCastSpell(index, point) and 0052F8B0 CanUseQuickItem(index, point) (RET 8; 0 = can),
// 00531920 the quick item (RET 4); 006A4230 an item's category.
static std::uint8_t* HoverPrototype(CUnit* unit) { return static_cast<std::uint8_t*>(::UnitPrototypeOf(unit)); }
__declspec(naked) void* __fastcall HoverQuickItem(CUnit*, void*, int) EI_ORIGINAL(0x00531920)
__declspec(naked) int __fastcall HoverItemCategory(void*) EI_ORIGINAL(0x006A4230)
// 0063B920 (thiscall on the battle screen, RET 8): shows `unit` in the unit panel block `block`.
void __fastcall HoverShowPanelUnit(void* a0, void*, CUnit* a2, void* a3) { BattleShowPanelUnitOriginal(a0, nullptr, (void*)a2, a3); }

// fcomp with 0.0 and C3: true for zero (either sign) and for NaN.
bool ZeroOrNaN(float value) {
    std::uint32_t bits;
    std::memcpy(&bits, &value, 4);
    return (bits & 0x7FFFFFFFu) == 0 || x87::IsNaN(value);
}

struct Point3 {
    float v[3];
};

} // namespace

void CBattleScreen::HoverUnit(CUnit* unit, int x, int y) {
    auto unitDead = [](CUnit* u) { return ei::VCall<int>(u, kSlotIsDead) != 0; };
    auto pick = [x, y]() {
        float out[3];
        float* p = HoverPickSurface(kClientWorld, nullptr, out, x, y);
        return Point3{{p[0], p[1], p[2]}};
    };
    auto objectAt = [x, y]() {
        int point[2] = {x, y};
        return HoverObjectAt(kClientWorld, nullptr, point);
    };
    auto unitPosition = [](CUnit* u) { return Field<Point3>(u, 0x1C); };

    // The unit panel's hover block (+0x150 the unit, +0x160 / +0x158 from its prototype, +0x168).
    // A unit without flag 0x400000 counts as none below; a dead one is kept.
    void* panel = Field<void*>(this, 0x1A8);
    if (unit && !(unit->flags_ & kFlagShowInPanel)) unit = nullptr;
    if (unit && !unitDead(unit)) {
        if (unit != Field<CUnit*>(panel, 0x150)) {
            Field<CUnit*>(panel, 0x150) = unit;
            Field<int>(panel, 0x160) = Field<int>(HoverPrototype(unit), 0x20);
            Field<int>(panel, 0x158) = Field<int>(HoverPrototype(Field<CUnit*>(panel, 0x150)), 0x24);
            HoverShowPanelUnit(Field<void*>(panel, 0x148), nullptr, Field<CUnit*>(panel, 0x150),
                               &Field<std::uint8_t>(panel, 0x168));
        }
    } else if (Field<CUnit*>(panel, 0x150)) {
        Field<CUnit*>(panel, 0x150) = nullptr;
    }

    int count = partyCount_;
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
        Point3 at = pick();
        int exit;
        if (!HoverFindExit(kClientWorld, nullptr, at.v, &exit)) return;
        SetCursor(kCursorExit);
        return;
    }
    for (int i = 0; i < 6; ++i) {
        if (Field<std::uint8_t>(this, 0x18D + i)) {
            SetCursor(kCursorFlagBase + i);
            return;
        }
    }
    if (interface_->ctrlDown_) {
        SetCursor(kCursorAttack);
        return;
    }

    switch (Field<int>(this, 0x17C)) {
    case 0:   // plain
        if (unit) {
            if (unitDead(unit)) {
                shape = kCursorUse;
                break;
            }
            if (partyCount_ == 0 || !party_[0]) return;
            if (!HoverHostileTo(party_[0], nullptr, unit)) return;
            shape = kCursorAttack;
            break;
        }
        {
            CUnit* object = objectAt();
            if (object && ei::VCall<int>(object, kSlotCanTarget) != 0 && ei::VCall<int>(object, kSlotSlot39) == 0 &&
                selected == 1) {
                shape = kCursorUse;
                break;
            }
            // Over a zone exit: the exit cursor unless the zone's "z.<name>" variable is 1 ("none"
            // is asked as is).
            Point3 at = pick();
            int exit;
            if (!HoverFindExit(kClientWorld, nullptr, at.v, &exit)) return;
            mfc::CString name(GameExitZoneOriginal(kPlayerGlue, nullptr, exit));
            if (gamecrt::mbsicmp(reinterpret_cast<const unsigned char*>(static_cast<const char*>(name)),
                                 reinterpret_cast<const unsigned char*>(0x007846F0)) != 0) {   // "none"
                name = mfc::CString(reinterpret_cast<const char*>(0x007900E8)) + name;     // "z."
                double value = HoverPlayerVariable(kPlayerGlue, nullptr, name);
                const float one = *reinterpret_cast<const volatile float*>(0x0073B84C);
                // FCOMP; TEST AH,0x40; JNZ: equal or NaN skips.
                if (!x87::IsNaN(static_cast<float>(value)) && value != one) SetCursor(kCursorExit);
            }
            return;
        }
    case 1: {   // a spell on a unit
        if (!unit || unitDead(unit)) {
            shape = kCursorCantCast;
            break;
        }
        Point3 at = unitPosition(unit);
        if (!first || UnitCanCastSpell(first, nullptr, Field<int>(this, 0x180), at.v) != 0) {
            shape = kCursorCantCast;
            break;
        }
        shape = kCursorCast;
        break;
    }
    case 2:   // a spell on a point
    case 4: {   // a quick item on a point
        Point3 at = (unit && !unitDead(unit)) ? unitPosition(unit) : pick();
        if ((ZeroOrNaN(at.v[0]) && ZeroOrNaN(at.v[1])) || !first) {
            shape = kCursorCantCast;
            break;
        }
        int refused = Field<int>(this, 0x17C) == 2 ? UnitCanCastSpell(first, nullptr, Field<int>(this, 0x180), at.v)
                                                    : UnitCanUseQuickItem(first, nullptr, Field<int>(this, 0x180), at.v);
        shape = refused ? kCursorCantCast : kCursorCast;
        break;
    }
    case 3: {   // a quick item on a unit (a wand not on the user itself)
        shape = kCursorCantCast;
        if (!unit || unitDead(unit)) break;
        Point3 at = unitPosition(unit);
        if (!first || UnitCanUseQuickItem(first, nullptr, Field<int>(this, 0x180), at.v) != 0) break;
        void* item = HoverQuickItem(first, nullptr, Field<int>(this, 0x180));
        if (item && HoverItemCategory(item) == kCategoryWand && unit != first) break;
        shape = kCursorCast;
        break;
    }
    case 5: {   // an item on an object
        CUnit* target = unit ? unit : objectAt();
        shape = (target && first && ei::VCall<int>(target, kSlotCanTarget) != 0) ? kCursorUse : kCursorNo;
        break;
    }
    case 6: {   // a spell on an object
        CUnit* target = unit ? unit : objectAt();
        shape = (target && first && ei::VCall<int>(target, kSlotCanTarget) != 0 &&
                 ei::VCall<int>(target, kSlotAcceptsSpell, &Field<std::uint8_t>(this, 0x220)) != 0)
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
        shape = (target && first && ei::VCall<int>(target, kSlotCanTarget) != 0) ? kCursorUse : kCursorNo;
        break;
    }
    default:
        return;
    }
    SetCursor(shape);
}
EI_REPLACE_METHOD(0x00639EF0, CBattleScreen::HoverUnit);
