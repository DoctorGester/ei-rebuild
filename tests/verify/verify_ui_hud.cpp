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
            if (TintBits(w->sceneObjects_.data_[first + i], k) != want) return false;
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
    bool ok = !self->unit_ || self->selected_ == self->unit_->unknown_270;
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
                flag = unit->unknown_438;
            else if (flag != unit->unknown_438)
                flag = -2;
        }
        shown = flag == 0 ? 0x3EAC0000u : flag == 1 ? 0u : 0x3EF60000u;
    }
    self->CHudMovePanel::Update();
    bool ok = self->flag_ == flag;
    if (view) {
        std::uint32_t bits;
        std::memcpy(&bits, reinterpret_cast<std::uint8_t*>(self->sceneObjects_.data_[5]) + 0xCC, 4);
        ok = ok && bits == shown;
    }
    ei::verify::Check("CHudMovePanel::Update", true, ok, "flag %d/%d", flag, self->flag_);
}
EI_VERIFY(0x00649550, MovePanelUpdateVerify, 0, s_unused);

// --- Drawing: the render states are restored ------------------------------------------------------

namespace {
std::uint32_t RenderState(CUiWindow* w, int state) {
    using Fn = long(__stdcall*)(UiD3DDevice*, int, std::uint32_t*);
    UiD3DDevice* device = w->interface_->renderer_->device_;
    std::uint32_t value = 0xDEADBEEF;
    reinterpret_cast<Fn>(device->vtable_[21])(device, state, &value);
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
