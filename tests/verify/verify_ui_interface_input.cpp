// Checks for the interface's input dispatch (ui_interface_input.cpp). Mouse and key events run
// side by side without
// delivering anything twice: the input sides it may choose (the one that took the mouse and the
// top window's) get a stand-in vtable whose event slots only record the call; the original runs,
// then ours, on the same state, and the recorded deliveries (side, slot, arguments) and the flags
// +0x7F / +0x80 must agree. The stand-ins are then removed and ours runs once for real. Reached by
// every mouse message (the dispatcher 00457970): every scenario with `click`, `move`, `wheel`,
// `dblclick`, the harness's moves, and the game's own mouse.
#include "verify.h"

#include "../../src/game/ui.h"

#include "../../src/game/console.h"

#include <cstdint>
#include <cstring>

namespace {

struct Delivery {
    const void* side;
    int slot;
    int a, b, c, d;
};
Delivery s_delivered;
int s_deliveries;

template <int Slot>
void __fastcall Recorder(UiInputSide* side, void*, int a, int b, int c) {
    s_delivered = Delivery{side, Slot, a, b, c, 0};
    ++s_deliveries;
}
int __fastcall WheelRecorder(UiInputSide* side, void*, int flags, int delta, int x, int y) {
    s_delivered = Delivery{side, UiInputSide::kWheel, flags, delta, x, y};
    ++s_deliveries;
    return 1;
}

constexpr int kSlots = 13;

// A stand-in vtable on up to two input sides (the same side once).
class StandIns {
public:
    explicit StandIns(CInterface3D* ui) {
        void* table[kSlots] = {};
        table[2] = reinterpret_cast<void*>(&Recorder<2>);
        table[3] = reinterpret_cast<void*>(&Recorder<3>);
        table[4] = reinterpret_cast<void*>(&Recorder<4>);
        table[5] = reinterpret_cast<void*>(&Recorder<5>);
        table[6] = reinterpret_cast<void*>(&Recorder<6>);
        table[7] = reinterpret_cast<void*>(&Recorder<7>);
        table[8] = reinterpret_cast<void*>(&Recorder<8>);
        table[10] = reinterpret_cast<void*>(&Recorder<10>);
        table[11] = reinterpret_cast<void*>(&Recorder<11>);
        table[12] = reinterpret_cast<void*>(&WheelRecorder);
        std::memcpy(table_, table, sizeof(table_));
        Install(static_cast<UiInputSide*>(ui->mouseCapture_));
        if (ui->stackBegin_ != ui->stackEnd_) Install(ui->stackEnd_[-1]->Input());
        if (ui->root_) Install(ui->root_->Input());
    }
    ~StandIns() {
        for (int i = 0; i < count_; ++i) sides_[i]->vtable_ = saved_[i];
    }

private:
    void Install(UiInputSide* side) {
        if (!side) return;
        for (int i = 0; i < count_; ++i)
            if (sides_[i] == side) return;
        sides_[count_] = side;
        saved_[count_] = side->vtable_;
        side->vtable_ = table_;
        ++count_;
    }
    void* table_[kSlots];
    UiInputSide* sides_[3] = {};
    void** saved_[3] = {};
    int count_ = 0;
};

struct Outcome {
    Delivery delivered;
    int deliveries;
    bool doubleClick, rightDown, ctrl, alt;
    std::uint8_t shift;
    int result;
};

template <typename Run>
Outcome Record(CInterface3D* ui, Run run) {
    s_deliveries = 0;
    s_delivered = Delivery{};
    int result = run();
    return Outcome{s_delivered, s_deliveries, ui->doubleClick_, ui->rightDown_, ui->ctrlDown_, ui->altDown_, ui->shiftDown_,
                   result};
}

bool Same(const Outcome& e, const Outcome& a) {
    return std::memcmp(&e.delivered, &a.delivered, sizeof(Delivery)) == 0 && e.deliveries == a.deliveries &&
           e.doubleClick == a.doubleClick && e.rightDown == a.rightDown && e.ctrl == a.ctrl && e.alt == a.alt &&
           e.shift == a.shift && e.result == a.result;
}

// Runs the original and ours against the stand-ins, compares, then ours for real.
template <typename Original, typename Ours>
int Compare(const char* name, CInterface3D* ui, Original original, Ours ours, int a, int b, int c) {
    bool doubleClick = ui->doubleClick_;
    bool rightDown = ui->rightDown_;
    bool ctrl = ui->ctrlDown_;
    bool alt = ui->altDown_;
    std::uint8_t shift = ui->shiftDown_;
    auto restore = [&] {
        ui->doubleClick_ = doubleClick;
        ui->rightDown_ = rightDown;
        ui->ctrlDown_ = ctrl;
        ui->altDown_ = alt;
        ui->shiftDown_ = shift;
    };
    Outcome expected, actual;
    {
        StandIns standIns(ui);
        expected = Record(ui, original);
        restore();
        actual = Record(ui, ours);
        restore();
    }
    ei::verify::Check(name, true, Same(expected, actual), "(%d,%d,%d) slot %d/%d to %p/%p count %d/%d flags %d%d/%d%d", a,
                      b, c, expected.delivered.slot, actual.delivered.slot, expected.delivered.side, actual.delivered.side,
                      expected.deliveries, actual.deliveries, expected.doubleClick, expected.rightDown, actual.doubleClick,
                      actual.rightDown);
    return ours();
}

using MouseFn = void(__fastcall*)(CInterface3D*, void*, int, int, int);

} // namespace

#define UI_MOUSE_VERIFY(addr, Method, stolen)                                                                  \
    static MouseFn s_##Method;                                                                                 \
    static void __fastcall Method##Verify(CInterface3D* self, void*, int buttons, int x, int y) {             \
        Compare(                                                                                               \
            "CInterface3D::" #Method, self, [&] { s_##Method(self, nullptr, buttons, x, y); return 0; },      \
            [&] { self->CInterface3D::Method(buttons, x, y); return 0; }, buttons, x, y);                      \
    }                                                                                                          \
    EI_VERIFY(addr, Method##Verify, stolen, s_##Method)

// Stolen bytes: MOV EAX,ECX; MOV ECX,[EAX+0xC]; MOV byte [EAX+0x7F],0 (9) for 005F76A0; PUSH ESI;
// MOV ESI,ECX; MOV ECX,[ESI+0xC] (6) for 005F76E0; MOV EAX,ECX; MOV ECX,[EAX+0xC]; TEST ECX,ECX (7)
// for 005F7740 and 005F7880; with a flag store (12) for 005F7780 / 005F77C0 / 005F7840, (9) for
// 005F7800.
UI_MOUSE_VERIFY(0x005F76A0, OnLeftDown, 9);
UI_MOUSE_VERIFY(0x005F76E0, OnLeftUp, 6);
UI_MOUSE_VERIFY(0x005F7740, OnMouseMove, 7);
UI_MOUSE_VERIFY(0x005F7780, OnRightDown, 12);
UI_MOUSE_VERIFY(0x005F77C0, OnRightUp, 12);
UI_MOUSE_VERIFY(0x005F7800, OnLeftDouble, 9);
UI_MOUSE_VERIFY(0x005F7840, OnRightDouble, 12);

static int(__fastcall* s_wheel)(CInterface3D*, void*, int, int, int, int);
static int __fastcall WheelVerify(CInterface3D* self, void*, int flags, int delta, int x, int y) {
    return Compare(
        "CInterface3D::OnWheel", self, [&] { return s_wheel(self, nullptr, flags, delta, x, y); },
        [&] { return self->CInterface3D::OnWheel(flags, delta, x, y); }, delta, x, y);
}
EI_VERIFY(0x005F7880, WheelVerify, 7, s_wheel);

// CInterface3D::OnChar / OnKeyUp (005F7B40 / 005F7BE0): the same with the chat line's and the top
// window's sides stood in, while the console is hidden (its handlers are plain functions; then
// ours runs alone). Reached by every key the scenarios send (key, console steps).
// 005F7B40: PUSH ESI; PUSH EDI; MOV EDI,[ESP+0x14] (6). 005F7BE0: MOV EDX,[ESP+4]; MOV EAX,ECX
// (6).
using KeyFn = void(__fastcall*)(CInterface3D*, void*, int, int, int);
static KeyFn s_char;
static void __fastcall CharVerify(CInterface3D* self, void*, int key, int repeat, int flags) {
    if (self->console_->shown_) {
        self->CInterface3D::OnChar(key, repeat, flags);
        return;
    }
    Compare(
        "CInterface3D::OnChar", self, [&] { s_char(self, nullptr, key, repeat, flags); return 0; },
        [&] { self->CInterface3D::OnChar(key, repeat, flags); return 0; }, key, repeat, flags);
}
EI_VERIFY(0x005F7B40, CharVerify, 6, s_char);

static KeyFn s_keyUp;
static void __fastcall KeyUpVerify(CInterface3D* self, void*, int key, int repeat, int flags) {
    if (self->console_->shown_) {
        self->CInterface3D::OnKeyUp(key, repeat, flags);
        return;
    }
    Compare(
        "CInterface3D::OnKeyUp", self, [&] { s_keyUp(self, nullptr, key, repeat, flags); return 0; },
        [&] { self->CInterface3D::OnKeyUp(key, repeat, flags); return 0; }, key, repeat, flags);
}
EI_VERIFY(0x005F7BE0, KeyUpVerify, 6, s_keyUp);

// ConsoleWindow::OnKeyUp (005E47E0): side by side on ctrl_ / shift_ (a key released while the
// console is shown: console.txt). MOV EAX,[ESP+4]; SUB EAX,0x10 (7).
static void(__fastcall* s_consoleKeyUp)(console::ConsoleWindow*, void*, int, int, int);
static void __fastcall ConsoleKeyUpVerify(console::ConsoleWindow* self, void*, int key, int repeat, int flags) {
    bool ctrl = self->ctrl_;
    bool shift = self->shift_;
    s_consoleKeyUp(self, nullptr, key, repeat, flags);
    bool expectedCtrl = self->ctrl_;
    bool expectedShift = self->shift_;
    self->ctrl_ = ctrl;
    self->shift_ = shift;
    self->console::ConsoleWindow::OnKeyUp(key, repeat, flags);
    ei::verify::Check("ConsoleWindow::OnKeyUp", true, expectedCtrl == self->ctrl_ && expectedShift == self->shift_,
                      "key %d ctrl %d/%d shift %d/%d", key, expectedCtrl, self->ctrl_, expectedShift, self->shift_);
}
EI_VERIFY(0x005E47E0, ConsoleKeyUpVerify, 7, s_consoleKeyUp);

// CInterface3D::CloseAll (005F74F0) / CloseAllPending (005F5AB0): ours only (they delete the
// windows). Afterwards no window is left, the five flags are clear and the screen flag is off;
// CloseAllPending does nothing unless +0x44 was set, and clears it. Reached by every change of
// screen (Activate 005F71C0 calls CloseAll through its address) and by a pending close-all on
// the next input message.
namespace {
bool AllClosed(const CInterface3D* ui) {
    return ui->stackBegin_ == ui->stackEnd_ && !ui->ctrlDown_ && !ui->altDown_ && !ui->shiftDown_ && !ui->rightDown_ &&
           !ui->doubleClick_ && !InterfaceHasScreen();
}
} // namespace

static void(__fastcall* s_closeAll)(CInterface3D*, void*);
static void __fastcall CloseAllVerify(CInterface3D* self, void*) {
    int windows = static_cast<int>(self->stackEnd_ - self->stackBegin_);
    self->CInterface3D::CloseAll();
    ei::verify::Check("CInterface3D::CloseAll", true, AllClosed(self), "windows %d left %d", windows,
                      static_cast<int>(self->stackEnd_ - self->stackBegin_));
}
EI_VERIFY(0x005F74F0, CloseAllVerify, 0, s_closeAll);

static void(__fastcall* s_closeAllPending)(CInterface3D*, void*);
static void __fastcall CloseAllPendingVerify(CInterface3D* self, void*) {
    bool pending = self->closeAll_;
    int windows = static_cast<int>(self->stackEnd_ - self->stackBegin_);
    self->CInterface3D::CloseAllPending();
    int left = static_cast<int>(self->stackEnd_ - self->stackBegin_);
    bool ok = !self->closeAll_ && (pending ? AllClosed(self) : left == windows);
    ei::verify::Check("CInterface3D::CloseAllPending", true, ok, "pending %d windows %d left %d", pending ? 1 : 0, windows,
                      left);
}
EI_VERIFY(0x005F5AB0, CloseAllPendingVerify, 0, s_closeAllPending);
