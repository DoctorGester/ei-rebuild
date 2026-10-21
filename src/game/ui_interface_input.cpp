// The interface's input dispatch (CInterface3D): the input dispatcher (00457970) closes the
// windows when asked (005F5AB0), then hands each mouse message here (005F76A0-005F7880; on to the
// input side that took the mouse or the top window's) and each typed character and key release
// (005F7B40 / 005F7BE0; on to the console, the chat line or the top window). See ui.h and
// docs/notes/ui.md ("Container input dispatch").
#include "ui.h"

#include "app.h"
#include "console.h"
#include "ui_cursor.h"
#include "world.h"

#include "ei/hook.h"
#include "originals_engine.h"
#include "originals_ui.h"
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
    Send(this, UiInputSide::kLeftDown, buttons, x, y);
}

void CInterface3D::OnLeftUp(int buttons, int x, int y) {
    Send(this, UiInputSide::kLeftUp, buttons, x, y);
    doubleClick_ = false;
}

void CInterface3D::OnMouseMove(int buttons, int x, int y) { Send(this, UiInputSide::kMouseMove, buttons, x, y); }

void CInterface3D::OnRightDown(int buttons, int x, int y) {
    rightDown_ = true;
    Send(this, UiInputSide::kRightDown, buttons, x, y);
}

void CInterface3D::OnRightUp(int buttons, int x, int y) {
    rightDown_ = false;
    Send(this, UiInputSide::kRightUp, buttons, x, y);
}

void CInterface3D::OnLeftDouble(int buttons, int x, int y) {
    doubleClick_ = true;
    Send(this, UiInputSide::kLeftDouble, buttons, x, y);
}

void CInterface3D::OnRightDouble(int buttons, int x, int y) {
    rightDown_ = true;
    Send(this, UiInputSide::kRightDouble, buttons, x, y);
}

int CInterface3D::OnWheel(int flags, int delta, int x, int y) {
    UiInputSide* target = MouseTarget();
    return target ? target->Wheel(flags, delta, x, y) : 0;
}

// --- 005F7B40 / 005F7BE0: keys ------------------------------------------------------------------

namespace {
constexpr int kScanMask = 0x1FF;
constexpr int kScanConsoleKey = 0x29;   // the key under Esc (the console's)
constexpr int kCursorWait = 3;
} // namespace

void CInterface3D::OnChar(int key, int repeat, int flags) {
    if ((flags & kScanMask) == kScanConsoleKey) return;
    if (console_->shown_) {
        ConsoleOnCharOriginal(console_, nullptr, key, repeat, flags);
        return;
    }
    if ((IsMultiplayerClient() & 0xFF) && root_->chatOpen_) {   // TEST AL,AL
        root_->Input()->Event(UiInputSide::kChar, key, repeat, flags);
        return;
    }
    if (stackBegin_ != stackEnd_) stackEnd_[-1]->Input()->Event(UiInputSide::kChar, key, repeat, flags);
}
EI_REPLACE_METHOD(0x005F7B40, CInterface3D::OnChar);

void CInterface3D::OnKeyUp(int key, int repeat, int flags) {
    if (key == VK_SHIFT)
        shiftDown_ = 0;
    else if (key == VK_CONTROL)
        ctrlDown_ = false;
    else if (key == VK_MENU)
        altDown_ = false;
    if (console_->shown_) {
        static_cast<console::ConsoleWindow*>(console_)->OnKeyUp(key, repeat, flags);
        return;
    }
    if (stackBegin_ != stackEnd_) stackEnd_[-1]->Input()->Event(UiInputSide::kKeyUp, key, repeat, flags);
}

void console::ConsoleWindow::OnKeyUp(int key, int, int) {
    if (key == VK_SHIFT)
        shift_ = false;
    else if (key == VK_CONTROL)
        ctrl_ = false;
}

// --- 005F74F0 / 005F5AB0: every window closed --------------------------------------------------

void CInterface3D::CloseAll() {
    TheCursor()->SetShape(kCursorWait);
    while (stackBegin_ != stackEnd_) {
        stackEnd_[-1]->Input()->Deactivate();
        stackEnd_[-1]->CloseWindow();
        if (CUiWindow* top = stackEnd_[-1]) top->DeleteWindow();
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
    InterfaceHasScreen() = false;
}

void CInterface3D::CloseAllPending() {
    if (!closeAll_) return;
    closeAll_ = false;
    CloseAll();
}
