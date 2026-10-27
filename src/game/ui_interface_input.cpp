// The interface's input dispatch (CInterface3D): the input dispatcher closes the
// windows when asked, then hands each mouse message here (on to the
// input side that took the mouse or the top window's) and each typed character and key release
// (on to the console, the chat line or the top window). See ui.h and
// docs/notes/ui.md ("Container input dispatch").
#include "ui.h"

#include "app.h"
#include "console.h"
#include "ui_cursor.h"
#include "world.h"

#include "ei/hook.h"
#include "game_singletons.h"

UiInputSide* CInterface3D::MouseTarget() const {
    if (mouseCapture_) return static_cast<UiInputSide*>(mouseCapture_);
    if (stackBegin_ != stackEnd_) return stackEnd_[-1]->Input();
    return nullptr;
}

namespace {
void Send(const CInterface3D* ui, int slot, int buttons, int x, int y) {
    if (UiInputSide* target = ui->MouseTarget()) target->Event(slot, buttons, x, y);
}
} // namespace

void CInterface3D::OnLeftDown(int buttons, int x, int y) {
    doubleClick_ = false;
    Send(this, UiInputSide::kEventLeftDown, buttons, x, y);
}
EI_REWRITTEN(0x005F76A0, CInterface3D::OnLeftDown);

void CInterface3D::OnLeftUp(int buttons, int x, int y) {
    Send(this, UiInputSide::kEventLeftUp, buttons, x, y);
    doubleClick_ = false;
}
EI_REWRITTEN(0x005F76E0, CInterface3D::OnLeftUp);

void CInterface3D::OnMouseMove(int buttons, int x, int y) { Send(this, UiInputSide::kEventMouseMove, buttons, x, y); }
EI_REWRITTEN(0x005F7740, CInterface3D::OnMouseMove);

void CInterface3D::OnRightDown(int buttons, int x, int y) {
    rightDown_ = true;
    Send(this, UiInputSide::kEventRightDown, buttons, x, y);
}
EI_REWRITTEN(0x005F7780, CInterface3D::OnRightDown);

void CInterface3D::OnRightUp(int buttons, int x, int y) {
    rightDown_ = false;
    Send(this, UiInputSide::kEventRightUp, buttons, x, y);
}
EI_REWRITTEN(0x005F77C0, CInterface3D::OnRightUp);

void CInterface3D::OnLeftDouble(int buttons, int x, int y) {
    doubleClick_ = true;
    Send(this, UiInputSide::kEventLeftDouble, buttons, x, y);
}
EI_REWRITTEN(0x005F7800, CInterface3D::OnLeftDouble);

void CInterface3D::OnRightDouble(int buttons, int x, int y) {
    rightDown_ = true;
    Send(this, UiInputSide::kEventRightDouble, buttons, x, y);
}
EI_REWRITTEN(0x005F7840, CInterface3D::OnRightDouble);

int CInterface3D::OnWheel(int flags, int delta, int x, int y) {
    UiInputSide* target = MouseTarget();
    return target ? target->Wheel(flags, delta, x, y) : 0;
}
EI_REWRITTEN(0x005F7880, CInterface3D::OnWheel);

// --- Keys ------------------------------------------------------------------

namespace {
constexpr int kScanMask = 0x1FF;
constexpr int kScanConsoleKey = 0x29;   // the key under Esc (the console's)
constexpr int kCursorWait = 3;
} // namespace

void CInterface3D::OnChar(int key, int repeat, int flags) {
    if ((flags & kScanMask) == kScanConsoleKey) return;
    if (console_->shown_) {
        static_cast<console::ConsoleWindow*>(console_)->OnChar(key, repeat, flags);
        return;
    }
    if ((IsMultiplayerClient() & 0xFF) && root_->chatOpen_) {   // TEST AL,AL
        root_->Input()->Event(UiInputSide::kEventChar, key, repeat, flags);
        return;
    }
    if (stackBegin_ != stackEnd_) stackEnd_[-1]->Input()->Event(UiInputSide::kEventChar, key, repeat, flags);
}
EI_REWRITTEN(0x005F7B40, CInterface3D::OnChar);

void CInterface3D::OnKeyUp(int key, int repeat, int flags) {
    if (key == VK_SHIFT)
        shiftDown_ = 0;
    else if (key == VK_CONTROL)
        ctrlDown_ = false;
    else if (key == VK_MENU)
        altDown_ = false;
    if (console_->shown_) {
        console_->OnKeyUp(key, repeat, flags);
        return;
    }
    if (stackBegin_ != stackEnd_) stackEnd_[-1]->Input()->Event(UiInputSide::kEventKeyUp, key, repeat, flags);
}
EI_REWRITTEN(0x005F7BE0, CInterface3D::OnKeyUp);

void console::ConsoleWindow::OnKeyUp(int key, int, int) {
    if (key == VK_SHIFT)
        shift_ = false;
    else if (key == VK_CONTROL)
        ctrl_ = false;
}
EI_REWRITTEN(0x005E47E0, console::ConsoleWindow::OnKeyUp);

// --- Every window closed --------------------------------------------------

void CInterface3D::CloseAll() {
    TheCursor()->SetShape(kCursorWait);
    while (stackBegin_ != stackEnd_) {
        stackEnd_[-1]->Input()->Deactivate();
        ScreenOf(stackEnd_[-1])->CloseScreen();
        delete stackEnd_[-1];
        --stackEnd_;
    }
    ctrlDown_ = false;
    altDown_ = false;
    shiftDown_ = 0;
    rightDown_ = false;
    doubleClick_ = false;
    // The original deactivates the top window here too, which cannot exist any more.
    if (stackBegin_ != stackEnd_) stackEnd_[-1]->Input()->Deactivate();
    app::PumpMessages();
    app::g_interfaceHasScreen = 0;
}
EI_REWRITTEN(0x005F74F0, CInterface3D::CloseAll);

void CInterface3D::CloseAllPending() {
    if (!closeAll_) return;
    closeAll_ = false;
    CloseAll();
}
EI_REWRITTEN(0x005F5AB0, CInterface3D::CloseAllPending);
