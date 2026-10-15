// Checks for the battle HUD panels (ui_hud.cpp).
//
// Handlers whose only effects are the window's handled flag and the interface's mouse capture
// run side by side: the original first (through the trampoline), its two results recorded and
// undone, then ours, and the results compared. Handlers that play sounds, give orders or call
// other panels run only ours; the wrapper works out from the state before the call what the
// original's code does and checks the state after it.
#include "verify.h"

#include "../../src/game/ui.h"
#include "../../src/game/unit.h"
#include "../../src/game/world.h"

#include "ei/rebirth.h"

#include <cstring>

namespace {
void* s_unused;

template <typename W>
W* FromInput(void* input) {
    return reinterpret_cast<W*>(static_cast<std::uint8_t*>(input) - 0x94);
}

// The two things a plain handler may change.
struct InputState {
    std::uint32_t handled;
    void* capture;
    std::uint32_t result;
};

InputState Snapshot(CUiWindow* w, std::uint32_t result = 0) {
    return InputState{w->inputHandled_ ? 1u : 0u, w->interface_->mouseCapture_, result};
}

void Restore(CUiWindow* w, const InputState& s) {
    w->inputHandled_ = s.handled != 0;
    w->interface_->mouseCapture_ = s.capture;
}

using Handler = void(__fastcall*)(void*, void*, int, int, int);
using WheelHandler = BOOL(__fastcall*)(void*, void*, int, int, int, int);

// Runs the original and then `ours` from the same state; compares handled flag and capture.
template <typename W, typename Ours>
void SideBySide(const char* name, Handler original, void* input, int buttons, int x, int y, Ours ours) {
    W* w = FromInput<W>(input);
    InputState before = Snapshot(w);
    original(input, nullptr, buttons, x, y);
    InputState expected = Snapshot(w);
    Restore(w, before);
    ours(w);
    InputState actual = Snapshot(w);
    ei::verify::Check(name, expected, actual, "(%d,%d) handled %u->%u/%u capture %p->%p/%p", x, y, before.handled,
                      expected.handled, actual.handled, before.capture, expected.capture, actual.capture);
}

template <typename W, typename Ours>
BOOL SideBySideWheel(const char* name, WheelHandler original, void* input, int buttons, int delta, int x, int y,
                     Ours ours) {
    W* w = FromInput<W>(input);
    InputState before = Snapshot(w);
    BOOL result = original(input, nullptr, buttons, delta, x, y);
    InputState expected = Snapshot(w, static_cast<std::uint32_t>(result));
    Restore(w, before);
    BOOL mine = ours(w);
    InputState actual = Snapshot(w, static_cast<std::uint32_t>(mine));
    ei::verify::Check(name, expected, actual, "(%d,%d) handled %u/%u result %d/%d", x, y, expected.handled,
                      actual.handled, result, mine);
    return result;
}

// What a button-up handler must leave: the capture released when it was ours, untouched
// otherwise (and then nothing else may change).
bool CaptureReleased(bool wasCaptured, void* before, void* after) {
    return wasCaptured ? after == nullptr : after == before;
}

std::uint32_t Bits(float f) {
    std::uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

std::uint32_t TintBits(CEngineObject* object, int i) {
    std::uint32_t u;
    std::memcpy(&u, reinterpret_cast<std::uint8_t*>(object) + 0x224 + 4 * i, 4);
    return u;
}

// Whether scene objects first..first+count-1 are tinted 1.0 for `selected` and 0.5 otherwise.
bool TintsMatch(CUiWindow* w, int first, int count, int selected) {
    for (int i = 0; i < count; ++i) {
        std::uint32_t want = i == selected ? 0x3F800000u : 0x3F000000u;
        for (int k = 0; k < 3; ++k)
            if (TintBits(w->sceneObjects_.data[first + i], k) != want) return false;
    }
    return true;
}
} // namespace

// --- Plain handlers: side by side --------------------------------------------------------------
//
// Every one starts MOV EAX,[ESP+0xC] (or +0x10); PUSH ESI: 5 bytes.

#define UI_VERIFY_INPUT(Class, Method)                                                         \
    static Handler s_##Class##_##Method;                                                            \
    static void __fastcall Class##_##Method##Verify(void* input, void*, int buttons, int x, int y) {  \
        SideBySide<Class>(#Class "::" #Method, s_##Class##_##Method, input, buttons, x, y,          \
                          [&](Class* w) { w->Class::Method(buttons, x, y); });                      \
    }

#define UI_VERIFY_WHEEL(Class, Method)                                                         \
    static WheelHandler s_##Class##_##Method;                                                       \
    static BOOL __fastcall Class##_##Method##Verify(void* input, void*, int buttons, int delta, int x, \
                                                     int y) {                                       \
        return SideBySideWheel<Class>(#Class "::" #Method, s_##Class##_##Method, input, buttons, delta, \
                                      x, y, [&](Class* w) { return w->Class::Method(buttons, delta, x, y); }); \
    }

UI_VERIFY_INPUT(CHudWeaponBar, OnMouseDown)
EI_VERIFY(0x0063C6D0, CHudWeaponBar_OnMouseDownVerify, 5, s_CHudWeaponBar_OnMouseDown);
UI_VERIFY_INPUT(CHudWeaponBar, OnMouseMove)
EI_VERIFY(0x0063C7D0, CHudWeaponBar_OnMouseMoveVerify, 5, s_CHudWeaponBar_OnMouseMove);
UI_VERIFY_INPUT(CHudQuickItems, OnMouseDown)
EI_VERIFY(0x0063DAA0, CHudQuickItems_OnMouseDownVerify, 5, s_CHudQuickItems_OnMouseDown);
UI_VERIFY_INPUT(CHudQuickItems, OnDoubleClick)
EI_VERIFY(0x0063DAE0, CHudQuickItems_OnDoubleClickVerify, 5, s_CHudQuickItems_OnDoubleClick);
UI_VERIFY_INPUT(CHudQuickItems, OnMouseMove)
EI_VERIFY(0x0063E270, CHudQuickItems_OnMouseMoveVerify, 5, s_CHudQuickItems_OnMouseMove);
UI_VERIFY_INPUT(CHudSpellColumn, OnMouseDown)
EI_VERIFY(0x0063F1E0, CHudSpellColumn_OnMouseDownVerify, 5, s_CHudSpellColumn_OnMouseDown);
UI_VERIFY_INPUT(CHudSpellColumn, OnDoubleClick)
EI_VERIFY(0x0063F220, CHudSpellColumn_OnDoubleClickVerify, 5, s_CHudSpellColumn_OnDoubleClick);
UI_VERIFY_INPUT(CHudSpellColumn, OnMouseMove)
EI_VERIFY(0x0063F2D0, CHudSpellColumn_OnMouseMoveVerify, 5, s_CHudSpellColumn_OnMouseMove);
UI_VERIFY_INPUT(CHudActionColumn, OnMouseDown)
EI_VERIFY(0x0063FDA0, CHudActionColumn_OnMouseDownVerify, 5, s_CHudActionColumn_OnMouseDown);
UI_VERIFY_INPUT(CHudActionColumn, OnMouseMove)
EI_VERIFY(0x0063FE40, CHudActionColumn_OnMouseMoveVerify, 5, s_CHudActionColumn_OnMouseMove);
UI_VERIFY_INPUT(CHudMap, OnMouseMove)
EI_VERIFY(0x00645610, CHudMap_OnMouseMoveVerify, 5, s_CHudMap_OnMouseMove);
UI_VERIFY_INPUT(CHudUnitPanel, OnMouseDown)
EI_VERIFY(0x00648330, CHudUnitPanel_OnMouseDownVerify, 5, s_CHudUnitPanel_OnMouseDown);
UI_VERIFY_INPUT(CHudUnitPanel, OnMouseMove)
EI_VERIFY(0x006484C0, CHudUnitPanel_OnMouseMoveVerify, 5, s_CHudUnitPanel_OnMouseMove);
UI_VERIFY_WHEEL(CHudUnitPanel, OnWheel)
EI_VERIFY(0x006484F0, CHudUnitPanel_OnWheelVerify, 5, s_CHudUnitPanel_OnWheel);
UI_VERIFY_INPUT(CHudClock, OnMouseDown)
EI_VERIFY(0x00648D30, CHudClock_OnMouseDownVerify, 5, s_CHudClock_OnMouseDown);
UI_VERIFY_INPUT(CHudClock, OnMouseMove)
EI_VERIFY(0x00649160, CHudClock_OnMouseMoveVerify, 5, s_CHudClock_OnMouseMove);
UI_VERIFY_INPUT(CHudMovePanel, OnMouseDown)
EI_VERIFY(0x00649630, CHudMovePanel_OnMouseDownVerify, 5, s_CHudMovePanel_OnMouseDown);
UI_VERIFY_INPUT(CHudMovePanel, OnMouseMove)
EI_VERIFY(0x00649750, CHudMovePanel_OnMouseMoveVerify, 5, s_CHudMovePanel_OnMouseMove);
UI_VERIFY_INPUT(CHudTopPanel, OnMouseDown)
EI_VERIFY(0x0064A1C0, CHudTopPanel_OnMouseDownVerify, 5, s_CHudTopPanel_OnMouseDown);
UI_VERIFY_INPUT(CHudTopPanel, OnMouseMove)
EI_VERIFY(0x0064A2C0, CHudTopPanel_OnMouseMoveVerify, 5, s_CHudTopPanel_OnMouseMove);
UI_VERIFY_WHEEL(CHudTopPanel, OnWheel)
EI_VERIFY(0x0064A2F0, CHudTopPanel_OnWheelVerify, 5, s_CHudTopPanel_OnWheel);

// --- Handlers with side effects: ours only, state checked ---------------------------------------

static void __fastcall WeaponBarMouseUpVerify(void* input, void*, int buttons, int x, int y) {
    auto* w = FromInput<CHudWeaponBar>(input);
    void* capture = w->interface_->mouseCapture_;
    bool captured = capture == input;
    bool handled = w->inputHandled_;
    int region = w->RegionAt(x, y);
    int selected = w->selected_;
    w->CHudWeaponBar::OnMouseUp(buttons, x, y);
    bool valid = captured && region >= 0 && region < w->count_;
    bool ok = CaptureReleased(captured, capture, w->interface_->mouseCapture_) &&
              w->inputHandled_ == (valid ? true : handled) && w->selected_ == (valid ? region : selected);
    ei::verify::Check("CHudWeaponBar::OnMouseUp", true, ok, "(%d,%d) region %d captured %d selected %d->%d", x, y,
                      region, captured, selected, w->selected_);
}
EI_VERIFY(0x0063C710, WeaponBarMouseUpVerify, 0, s_unused);

// Spell and action column button-up: capture and handled flag (the click goes to original code).
template <typename W>
void ColumnMouseUp(const char* name, void* input, int buttons, int x, int y) {
    auto* w = FromInput<W>(input);
    void* capture = w->interface_->mouseCapture_;
    bool captured = capture == input;
    bool handled = w->inputHandled_;
    int region = w->RegionAt(x, y);
    bool valid = captured && region >= 0 && region < w->count_;
    w->W::OnMouseUp(buttons, x, y);
    bool ok = CaptureReleased(captured, capture, w->interface_->mouseCapture_) &&
              w->inputHandled_ == (valid ? true : handled);
    ei::verify::Check(name, true, ok, "(%d,%d) region %d captured %d", x, y, region, captured);
}
static void __fastcall SpellColumnMouseUpVerify(void* input, void*, int buttons, int x, int y) {
    ColumnMouseUp<CHudSpellColumn>("CHudSpellColumn::OnMouseUp", input, buttons, x, y);
}
static void __fastcall ActionColumnMouseUpVerify(void* input, void*, int buttons, int x, int y) {
    ColumnMouseUp<CHudActionColumn>("CHudActionColumn::OnMouseUp", input, buttons, x, y);
}
EI_VERIFY(0x0063F260, SpellColumnMouseUpVerify, 0, s_unused);
EI_VERIFY(0x0063FDE0, ActionColumnMouseUpVerify, 0, s_unused);

// Quick item panel button-up (0063DB20): capture and handled flag, and the battle screen's pending
// action (+0x17C mode, +0x180 slot, +0x184 item id) and the panels' highlights (+0x150) as the
// original's branches leave them. A unit's quick item goes to original code (0063DD70: mode 3 / 4
// for a wand or scroll with a spell, 0 when it cancels or the item is gone, else unchanged; with
// key +0x7F 0063E020: 0 or unchanged).
static int& ScreenInt(CBattleScreen* screen, int offset) {
    return *reinterpret_cast<int*>(reinterpret_cast<std::uint8_t*>(screen) + offset);
}
static int PanelSelected(CBattleScreen* screen, int offset) {
    return *reinterpret_cast<int*>(*reinterpret_cast<std::uint8_t**>(reinterpret_cast<std::uint8_t*>(screen) + offset) +
                                   0x150);
}
static void __fastcall QuickItemsMouseUpVerify(void* input, void*, int buttons, int x, int y) {
    auto* w = FromInput<CHudQuickItems>(input);
    void* capture = w->interface_->mouseCapture_;
    bool captured = capture == input;
    bool handled = w->inputHandled_;
    int region = w->RegionAt(x, y);
    int entry = captured ? w->RegionItem(region) : -1;
    bool own = entry >= 0 && region < 4 && w->entries_[entry].own;
    bool ctrl = w->interface_->doubleClick_;
    int selected = w->selected_;
    int id = entry >= 0 ? w->entries_[entry].id : 0;
    CBattleScreen* screen = w->owner_;
    int mode = ScreenInt(screen, 0x17C);
    w->CHudQuickItems::OnMouseUp(buttons, x, y);
    int modeAfter = ScreenInt(screen, 0x17C);
    bool ok = CaptureReleased(captured, capture, w->interface_->mouseCapture_) &&
              w->inputHandled_ == (entry >= 0 ? true : handled);
    const char* path = "none";
    if (entry < 0) {
        ok = ok && modeAfter == mode;
    } else if (own) {
        path = ctrl ? "use" : "pick";
        if (ctrl)
            ok = ok && (modeAfter == 0 || modeAfter == mode);
        else
            ok = ok && (modeAfter == 0 || modeAfter == mode ||
                        ((modeAfter == 3 || modeAfter == 4) && ScreenInt(screen, 0x180) == region));
    } else {
        bool cancel = selected == region;
        path = cancel ? "cancel" : "object";
        ok = ok && PanelSelected(screen, 0x19C) == -1 && PanelSelected(screen, 0x1A0) == -1;
        if (cancel)
            ok = ok && modeAfter == 0 && ScreenInt(screen, 0x180) == -1 && ScreenInt(screen, 0x184) == -1 &&
                 PanelSelected(screen, 0x198) == -1;
        else
            ok = ok && modeAfter == 6 && ScreenInt(screen, 0x180) == region && ScreenInt(screen, 0x184) == id;
    }
    ei::verify::Check("CHudQuickItems::OnMouseUp", true, ok, "(%d,%d) region %d entry %d %s mode %d->%d", x, y, region,
                      entry, path, mode, modeAfter);
}
EI_VERIFY(0x0063DB20, QuickItemsMouseUpVerify, 0, s_unused);

static int ActiveMode(CUiWindow* w) {
    return static_cast<CBattleScreen*>(w->interface_->GetActiveWindow())->portraitMode_;
}

static void __fastcall PortraitsMouseDownVerify(void* input, void*, int buttons, int x, int y) {
    auto* w = FromInput<CHudPortraits>(input);
    void* capture = w->interface_->mouseCapture_;
    bool handled = w->inputHandled_;
    int region = w->RegionAt(x, y);
    int mode = ActiveMode(w);
    w->CHudPortraits::OnMouseDown(buttons, x, y);
    bool ok;
    if (mode == 0) {
        bool hit = region >= 0;
        bool takes = hit && region < w->owner_->partyCount_;
        ok = w->inputHandled_ == (hit ? true : handled) &&
             w->interface_->mouseCapture_ == (takes ? input : capture);
    } else {
        bool valid = region >= 0 && region < w->count_;
        ok = w->inputHandled_ == handled && w->interface_->mouseCapture_ == capture &&
             ActiveMode(w) == (valid ? 0 : mode);
    }
    ei::verify::Check("CHudPortraits::OnMouseDown", true, ok, "(%d,%d) region %d mode %d", x, y, region, mode);
}
EI_VERIFY(0x00641CE0, PortraitsMouseDownVerify, 0, s_unused);

static void __fastcall PortraitsMouseUpVerify(void* input, void*, int buttons, int x, int y) {
    auto* w = FromInput<CHudPortraits>(input);
    void* capture = w->interface_->mouseCapture_;
    bool handled = w->inputHandled_;
    int region = w->RegionAt(x, y);
    bool active = ActiveMode(w) == 0 && capture == input;
    w->CHudPortraits::OnMouseUp(buttons, x, y);
    bool ok = CaptureReleased(active, capture, w->interface_->mouseCapture_) &&
              w->inputHandled_ == (active && region >= 0 ? true : handled);
    ei::verify::Check("CHudPortraits::OnMouseUp", true, ok, "(%d,%d) region %d active %d", x, y, region, active);
}
EI_VERIFY(0x00641E20, PortraitsMouseUpVerify, 0, s_unused);

static void __fastcall PortraitsMouseMoveVerify(void* input, void*, int buttons, int x, int y) {
    auto* w = FromInput<CHudPortraits>(input);
    void* capture = w->interface_->mouseCapture_;
    bool handled = w->inputHandled_;
    int region = w->RegionAt(x, y);
    int mode = ActiveMode(w);
    w->CHudPortraits::OnMouseMove(buttons, x, y);
    bool ok = w->interface_->mouseCapture_ == capture &&
              w->inputHandled_ == (mode == 0 && region >= 0 ? true : handled);
    ei::verify::Check("CHudPortraits::OnMouseMove", true, ok, "(%d,%d) region %d mode %d", x, y, region, mode);
}
EI_VERIFY(0x00641EA0, PortraitsMouseMoveVerify, 0, s_unused);

static void __fastcall MapMouseDownVerify(void* input, void*, int buttons, int x, int y) {
    auto* w = FromInput<CHudMap>(input);
    void* capture = w->interface_->mouseCapture_;
    bool handled = w->inputHandled_;
    int view = w->view_;
    int region = w->RegionAt(x, y);
    w->CHudMap::OnMouseDown(buttons, x, y);
    bool hit = region >= 0;
    bool ok = w->inputHandled_ == (hit ? true : handled) &&
              w->interface_->mouseCapture_ == (hit ? input : capture);
    if (region == 3 || region == 4)
        ok = ok && w->view_ == region - 2 && TintsMatch(w, 6, 2, region - 3);
    else
        ok = ok && w->view_ == view;
    ei::verify::Check("CHudMap::OnMouseDown", true, ok, "(%d,%d) region %d view %d->%d", x, y, region, view,
                      w->view_);
}
EI_VERIFY(0x00645060, MapMouseDownVerify, 0, s_unused);

// 00645210 (A-H26; Rebirth-patched for WideScreen, reproduced behind the flag, W3): the map's button
// release. It plays sounds and moves the camera, so ours only; the wrapper works out from the
// state before the call what the original's branches leave. ui_hud reaches the first branch (a
// view button let go: its two clicks on the view buttons); no scenario clicks the sling button,
// the map itself or "camera north" yet.
static void __fastcall MapMouseUpVerify(void* input, void*, int buttons, int x, int y) {
    auto* w = FromInput<CHudMap>(input);
    void* capture = w->interface_->mouseCapture_;
    const bool captured = capture == input;
    const bool handled = w->inputHandled_;
    const int view = w->view_;
    const int region = w->RegionAt(x, y);
    const int sliding = w->sliding_;
    const std::uint32_t direction = Bits(w->slideDirection_);
    const std::uint32_t north = Bits(w->northTime_);
    w->CHudMap::OnMouseUp(buttons, x, y);
    bool ok = CaptureReleased(captured, capture, w->interface_->mouseCapture_);
    const char* path = "not captured";
    if (!captured) {
        ok = ok && w->inputHandled_ == handled && w->view_ == view;
    } else if (view != 0) {
        path = "view button";
        ok = ok && w->view_ == 0 && w->inputHandled_ == handled && TintsMatch(w, 6, 2, -1);
    } else {
        path = "region";
        ok = ok && w->view_ == 0 && w->inputHandled_ == (region >= 0 ? true : handled);
    }
    const bool sling = captured && view == 0 && region == 0;
    if (sling)
        ok = ok && w->sliding_ == 1 && Bits(w->slideDirection_) == (direction ^ 0x80000000u);
    else
        ok = ok && w->sliding_ == sliding && Bits(w->slideDirection_) == direction;
    const bool toNorth = captured && view == 0 && region == 5;
    ok = ok && Bits(w->northTime_) == (toNorth ? 0x3F000000u : north);
    ei::verify::Check("CHudMap::OnMouseUp", true, ok, "(%d,%d) region %d view %d %s", x, y, region, view, path);
}
EI_VERIFY(0x00645210, MapMouseUpVerify, 0, s_unused);

static void __fastcall UnitPanelMouseUpVerify(void* input, void*, int buttons, int x, int y) {
    auto* w = FromInput<CHudUnitPanel>(input);
    void* capture = w->interface_->mouseCapture_;
    bool captured = capture == input;
    bool handled = w->inputHandled_;
    int region = w->RegionAt(x, y);
    int sliding = w->sliding_;
    std::uint32_t direction = Bits(w->slideDirection_);
    int part = w->bodyPart_;
    w->CHudUnitPanel::OnMouseUp(buttons, x, y);
    bool ok = CaptureReleased(captured, capture, w->interface_->mouseCapture_) &&
              w->inputHandled_ == (captured && region >= 0 ? true : handled);
    if (captured && region == 0)
        ok = ok && w->sliding_ == 1 && Bits(w->slideDirection_) == (direction ^ 0x80000000u) && w->bodyPart_ == part;
    else
        ok = ok && w->sliding_ == sliding && Bits(w->slideDirection_) == direction;
    if (captured && region >= 1 && region <= 4)
        ok = ok && w->bodyPart_ == region - 1 && TintsMatch(w, 3, 4, region - 1);
    else if (!(captured && region == 0))
        ok = ok && w->bodyPart_ == part;
    ei::verify::Check("CHudUnitPanel::OnMouseUp", true, ok, "(%d,%d) region %d captured %d", x, y, region, captured);
}
EI_VERIFY(0x00648370, UnitPanelMouseUpVerify, 0, s_unused);

static void __fastcall MovePanelMouseUpVerify(void* input, void*, int buttons, int x, int y) {
    auto* w = FromInput<CHudMovePanel>(input);
    void* capture = w->interface_->mouseCapture_;
    bool captured = capture == input;
    bool handled = w->inputHandled_;
    int region = w->RegionAt(x, y);
    w->CHudMovePanel::OnMouseUp(buttons, x, y);
    bool ok = CaptureReleased(captured, capture, w->interface_->mouseCapture_) &&
              w->inputHandled_ == (captured && region != -1 ? true : handled);
    ei::verify::Check("CHudMovePanel::OnMouseUp", true, ok, "(%d,%d) region %d captured %d", x, y, region, captured);
}
EI_VERIFY(0x00649670, MovePanelMouseUpVerify, 0, s_unused);

static void __fastcall TopPanelMouseUpVerify(void* input, void*, int buttons, int x, int y) {
    auto* w = FromInput<CHudTopPanel>(input);
    void* capture = w->interface_->mouseCapture_;
    bool captured = capture == input;
    bool handled = w->inputHandled_;
    int region = w->RegionAt(x, y);
    int sliding = w->sliding_;
    std::uint32_t direction = Bits(w->slideDirection_);
    int page = w->page_;
    w->CHudTopPanel::OnMouseUp(buttons, x, y);
    bool ok = CaptureReleased(captured, capture, w->interface_->mouseCapture_) &&
              w->inputHandled_ == (captured && region >= 0 ? true : handled);
    if (captured && region == 0)
        ok = ok && w->sliding_ == 1 && Bits(w->slideDirection_) == (direction ^ 0x80000000u);
    else
        ok = ok && w->sliding_ == sliding && Bits(w->slideDirection_) == direction;
    ok = ok && w->page_ == (captured && (region == 1 || region == 2) ? region - 1 : page);
    ei::verify::Check("CHudTopPanel::OnMouseUp", true, ok, "(%d,%d) region %d captured %d", x, y, region, captured);
}
EI_VERIFY(0x0064A200, TopPanelMouseUpVerify, 0, s_unused);

// --- Per-frame updates ---------------------------------------------------------------------------

static void __fastcall WeaponBarUpdateVerify(CHudWeaponBar* self, void*) {
    self->CHudWeaponBar::Update();
    bool ok = !self->unit_ || self->selected_ == self->unit_->pendingWeapon;
    ei::verify::Check("CHudWeaponBar::Update", true, ok, "selected %d", self->selected_);
}
EI_VERIFY(0x0063C600, WeaponBarUpdateVerify, 0, s_unused);

static void __fastcall MovePanelUpdateVerify(CHudMovePanel* self, void*) {
    bool view = WorldClient()->LocalView() != nullptr;
    int flag = self->flag_;
    std::uint32_t shown = 0;
    if (view) {
        flag = -1;
        for (int i = 0; i < self->owner_->partyCount_; ++i) {
            CUnit* unit = self->owner_->party_[i];
            if (!(unit->flags_ & 0x10)) continue;
            if (flag == -1)
                flag = unit->fightAtWill;
            else if (flag != unit->fightAtWill)
                flag = -2;
        }
        shown = flag == 0 ? 0x3EAC0000u : flag == 1 ? 0u : 0x3EF60000u;
    }
    self->CHudMovePanel::Update();
    bool ok = self->flag_ == flag;
    if (view) {
        std::uint32_t bits;
        std::memcpy(&bits, reinterpret_cast<std::uint8_t*>(self->sceneObjects_.data[5]) + 0xCC, 4);
        ok = ok && bits == shown;
    }
    ei::verify::Check("CHudMovePanel::Update", true, ok, "flag %d/%d", flag, self->flag_);
}
EI_VERIFY(0x00649550, MovePanelUpdateVerify, 0, s_unused);

// Ours only (it formats tooltip texts): nothing else of the panel may change.
static void __fastcall QuickItemsUpdateVerify(CHudQuickItems* self, void*) {
    int items[8];
    std::memcpy(items, self->regionItem_, sizeof(items));
    CUnit* unit = self->unit_;
    self->CHudQuickItems::Update();
    bool ok = self->unit_ == unit && std::memcmp(items, self->regionItem_, sizeof(items)) == 0;
    ei::verify::Check("CHudQuickItems::Update", true, ok, "unit %p", unit);
}
EI_VERIFY(0x0063D9D0, QuickItemsUpdateVerify, 0, s_unused);

// --- Drawing: the render states are restored ------------------------------------------------------

namespace {
std::uint32_t RenderState(CUiWindow* w, int state) {
    std::uint32_t value = 0xDEADBEEF;
    w->interface_->renderer_->device_->GetRenderState(static_cast<std::uint32_t>(state), &value);
    return value;
}

template <typename W>
BOOL CheckDraw(const char* name, W* w, int pass, bool restoresAlpha) {
    std::uint32_t ref = RenderState(w, kRenderStateAlphaRef);
    std::uint32_t test = RenderState(w, kRenderStateAlphaTest);
    BOOL result = w->W::Draw(pass);
    bool ok = result == 1 && RenderState(w, kRenderStateSpecular) == 0;
    if (restoresAlpha)
        ok = ok && RenderState(w, kRenderStateAlphaRef) == ref && RenderState(w, kRenderStateAlphaTest) == test;
    ei::verify::Check(name, true, ok, "pass %d", pass);
    return result;
}
} // namespace

static BOOL __fastcall WeaponBarDrawVerify(CHudWeaponBar* self, void*, int pass) {
    return CheckDraw("CHudWeaponBar::Draw", self, pass, true);
}
static BOOL __fastcall QuickItemsDrawVerify(CHudQuickItems* self, void*, int pass) {
    return CheckDraw("CHudQuickItems::Draw", self, pass, false);
}
static BOOL __fastcall SpellColumnDrawVerify(CHudSpellColumn* self, void*, int pass) {
    return CheckDraw("CHudSpellColumn::Draw", self, pass, false);
}
static BOOL __fastcall ActionColumnDrawVerify(CHudActionColumn* self, void*, int pass) {
    return CheckDraw("CHudActionColumn::Draw", self, pass, false);
}
EI_VERIFY(0x0063C4F0, WeaponBarDrawVerify, 0, s_unused);
EI_VERIFY(0x0063CDC0, QuickItemsDrawVerify, 0, s_unused);
EI_VERIFY(0x0063F190, SpellColumnDrawVerify, 0, s_unused);
EI_VERIFY(0x0063FD50, ActionColumnDrawVerify, 0, s_unused);

// 00638EF0 (CBattleScreen::SelectUnit) sends orders and plays sounds: ours only. The pending action
// (+0x17C) is still one of the nine; actions 5, 6 and 8 that issued their order went back to 0
// with no item or spell (+0x180, +0x184 = -1).
static void __fastcall SelectUnitVerify(CBattleScreen* self, void*, CUnit* unit, int x, int y) {
    auto field = [self](int offset) { return *reinterpret_cast<int*>(reinterpret_cast<std::uint8_t*>(self) + offset); };
    int before = field(0x17C);
    self->SelectUnit(unit, x, y);
    int after = field(0x17C);
    bool ok = after >= 0 && after <= 8;
    if ((before == 5 || before == 6 || before == 8) && after == 0) ok = ok && field(0x180) == -1 && field(0x184) == -1;
    ei::verify::Check("CBattleScreen::SelectUnit", true, ok, "unit %p (%d, %d), action %d -> %d, item %d spell %d", unit, x, y,
                      before, after, field(0x180), field(0x184));
}
EI_VERIFY(0x00638EF0, SelectUnitVerify, 0, s_unused);

// 00636F10 (CBattleScreen::OrderAt) gives the selected units an order through the local view and
// draws random numbers for the portraits: ours only. Reached by a click on the ground with party
// members selected (SelectUnit's plain action without a unit or a targetable object under the
// point: ui_order_move, probably world_client's clicks beside the hero), and with the modifier flags or
// Shift (mode 1). Checked: every member selected before the call has its portrait nodding (record
// +0x38 = 1, or 2 when the order made the unit answer at once through slot 32) with a delay that
// is not positive (+0x3C); the zone-exit flag (+0x18C) is 0 or 1 and,
// when set, +0x188 is the exit the point lies on (00578A00).
static void __fastcall OrderAtVerify(CBattleScreen* self, void*, float x, float y, float z, int mode, int ctrl) {
    auto* bytes = reinterpret_cast<std::uint8_t*>(self);
    bool selected[64] = {};
    int count = self->partyCount_ < 64 ? self->partyCount_ : 64;
    for (int i = 0; i < count; ++i) {
        CUnit* member = self->party_[i];
        selected[i] = member && (member->flags_ & 0x10);
    }
    self->OrderAt(x, y, z, mode, ctrl);
    std::uint8_t* records = *reinterpret_cast<std::uint8_t**>(*reinterpret_cast<std::uint8_t**>(bytes + 0x1A4) + 0x158);
    int wrong = -1;
    for (int i = 0; i < count && wrong < 0; ++i) {
        if (!selected[i]) continue;
        int kind;
        float delay;
        std::memcpy(&kind, records + i * 0x78 + 0x38, 4);
        std::memcpy(&delay, records + i * 0x78 + 0x3C, 4);
        if ((kind != 1 && kind != 2) || delay > 0.0f) wrong = i;
    }
    std::uint8_t flag = bytes[0x18C];
    bool exitOk = flag == 0;
    if (flag == 1) {
        float point[3] = {x, y, z};
        int exit = -1;
        using ExitAtFn = int(__thiscall*)(void*, const float*, int*);
        int found = reinterpret_cast<ExitAtFn>(0x00578A00)(reinterpret_cast<void*>(0x007AFEA5), point, &exit);
        int saved;
        std::memcpy(&saved, bytes + 0x188, 4);
        exitOk = found != 0 && exit == saved;
    }
    ei::verify::Check("CBattleScreen::OrderAt", true, wrong < 0 && exitOk,
                      "(%g, %g, %g) mode %d: portrait %d not nodding, exit flag %d (+0x188 %d)", static_cast<double>(x),
                      static_cast<double>(y), static_cast<double>(z), mode & 0xFF, wrong, flag,
                      *reinterpret_cast<int*>(bytes + 0x188));
}
EI_VERIFY(0x00636F10, OrderAtVerify, 0, s_unused);

// 00648F10 (A-H25; a click on the clock's objectives button, from the Rebirth-patched 00648D70):
// plays a sound and opens a window, so ours only: the pause button no longer pulses.
static void __fastcall ClockOpenObjectivesVerify(CHudClock* self) {
    self->CHudClock::OpenObjectives();
    ei::verify::Check("CHudClock::OpenObjectives", false, self->highlight_, "clock %p", static_cast<void*>(self));
}
EI_VERIFY(0x00648F10, ClockOpenObjectivesVerify, 0, s_unused);

// 00648D70 (A-H26; Rebirth's MPSpeedControl reproduced): the clock's button
// release. It plays sounds, pauses the game and opens a window, so ours only; the wrapper works out
// from the state before the call what the original's branches leave. Reached by a click on the
// clock (the pause button, the two speed buttons, the objectives button); no scenario clicks there
// yet (ui_hud only moves the mouse over the clock).
static void __fastcall ClockMouseUpVerify(void* input, void*, int buttons, int x, int y) {
    auto* w = FromInput<CHudClock>(input);
    void* capture = w->interface_->mouseCapture_;
    const bool captured = capture == input;
    const bool handled = w->inputHandled_;
    const int region = w->RegionAt(x, y);
    const int fast = w->fast_;
    const int period = *reinterpret_cast<int*>(0x0078FF34);
    const bool network = reinterpret_cast<bool(__cdecl*)()>(0x0057F440)();
    w->CHudClock::OnMouseUp(buttons, x, y);
    const bool hit = captured && region >= 0 && region <= 3;
    bool ok = CaptureReleased(captured, capture, w->interface_->mouseCapture_) &&
              w->inputHandled_ == (hit ? true : handled);
    const int periodAfter = *reinterpret_cast<int*>(0x0078FF34);
    if (hit && (region == 1 || region == 2) && (!network || ei::rebirth::Enabled(ei::rebirth::Patch::MPSpeedControl)))
        ok = ok && w->fast_ == region - 1 && periodAfter == (region == 2 ? 27 : 55) &&
             w->interface_->fastGame_ == w->fast_;
    else
        ok = ok && w->fast_ == fast && periodAfter == period;
    if (hit && region == 3) ok = ok && !w->highlight_;
    ei::verify::Check("CHudClock::OnMouseUp", true, ok, "(%d,%d) region %d captured %d fast %d->%d period %d->%d", x, y,
                      region, captured, fast, w->fast_, period, periodAfter);
}
EI_VERIFY(0x00648D70, ClockMouseUpVerify, 0, s_unused);

// 00649910 (A-H26; Rebirth-patched, so hooked in the vanilla launch only): MOV EAX,[ESP+4] (4),
// SUB ESP,0x34 (3). Side by side: it writes only the panel's mode, the marker's visibility,
// orientation and position (scene object 0) and the four mode buttons' tints; the original runs
// first, what it wrote is recorded and put back, then ours. The floats (the marker's turn and
// place) are compared within a few ULPs. Reached every frame of the battle screen
// (CHudMovePanel::Update), so by every scenario that starts a game.
namespace {
struct MoveModeState {
    int mode;
    int visible;
    Quaternion orientation;
    Vector3 position;
    std::uint32_t tints[4][3];
};
MoveModeState CaptureMoveMode(CHudMovePanel* w) {
    MoveModeState s;
    auto* marker = reinterpret_cast<CUiMesh*>(w->sceneObjects_.data[0]);
    s.mode = w->mode_;
    s.visible = marker->visible_;
    s.orientation = marker->orientation_;
    s.position = marker->position_;
    for (int i = 0; i < 4; ++i)
        for (int k = 0; k < 3; ++k) s.tints[i][k] = TintBits(w->sceneObjects_.data[1 + i], k);
    return s;
}
void RestoreMoveMode(CHudMovePanel* w, const MoveModeState& s) {
    auto* marker = reinterpret_cast<CUiMesh*>(w->sceneObjects_.data[0]);
    w->mode_ = s.mode;
    marker->visible_ = s.visible;
    marker->orientation_ = s.orientation;
    marker->position_ = s.position;
    for (int i = 0; i < 4; ++i)
        std::memcpy(reinterpret_cast<CUiMesh*>(w->sceneObjects_.data[1 + i])->tint_, s.tints[i], sizeof(s.tints[i]));
}
} // namespace
static void(__fastcall* s_showMode)(CHudMovePanel*, void*, int);
static void __fastcall MovePanelShowModeVerify(CHudMovePanel* self, void*, int mode) {
    const MoveModeState before = CaptureMoveMode(self);
    s_showMode(self, nullptr, mode);
    const MoveModeState expected = CaptureMoveMode(self);
    RestoreMoveMode(self, before);
    self->ShowModeVanilla(mode);
    const MoveModeState actual = CaptureMoveMode(self);
    const bool same = expected.mode == actual.mode && expected.visible == actual.visible &&
                      std::memcmp(expected.tints, actual.tints, sizeof(expected.tints)) == 0;
    ei::verify::Check("CHudMovePanel::ShowMode", true, same, "mode %d: mode %d/%d visible %d/%d", mode, expected.mode,
                      actual.mode, expected.visible, actual.visible);
    const float* e = &expected.orientation.w;
    const float* a = &actual.orientation.w;
    for (int i = 0; i < 4; ++i) ei::verify::CheckNear("CHudMovePanel::ShowMode", e[i], a[i], "mode %d turn %d", mode, i);
    ei::verify::CheckNear("CHudMovePanel::ShowMode", expected.position.x, actual.position.x, "mode %d x", mode);
    ei::verify::CheckNear("CHudMovePanel::ShowMode", expected.position.y, actual.position.y, "mode %d y", mode);
    ei::verify::CheckNear("CHudMovePanel::ShowMode", expected.position.z, actual.position.z, "mode %d depth", mode);
}
EI_VERIFY(0x00649910, MovePanelShowModeVerify, 7, s_showMode);
