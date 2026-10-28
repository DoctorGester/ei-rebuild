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

#include "game_singletons.h"

UiInputSide* CInterface3D::MouseTarget() const {
    if (mouseCapture_) return static_cast<UiInputSide*>(mouseCapture_);
    if (stack_.begin() != stack_.end()) return stack_.end()[-1]->Input();
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

void CInterface3D::OnLeftUp(int buttons, int x, int y) {
    Send(this, UiInputSide::kEventLeftUp, buttons, x, y);
    doubleClick_ = false;
}

void CInterface3D::OnMouseMove(int buttons, int x, int y) { Send(this, UiInputSide::kEventMouseMove, buttons, x, y); }

void CInterface3D::OnRightDown(int buttons, int x, int y) {
    rightDown_ = true;
    Send(this, UiInputSide::kEventRightDown, buttons, x, y);
}

void CInterface3D::OnRightUp(int buttons, int x, int y) {
    rightDown_ = false;
    Send(this, UiInputSide::kEventRightUp, buttons, x, y);
}

void CInterface3D::OnLeftDouble(int buttons, int x, int y) {
    doubleClick_ = true;
    Send(this, UiInputSide::kEventLeftDouble, buttons, x, y);
}

void CInterface3D::OnRightDouble(int buttons, int x, int y) {
    rightDown_ = true;
    Send(this, UiInputSide::kEventRightDouble, buttons, x, y);
}

int CInterface3D::OnWheel(int flags, int delta, int x, int y) {
    UiInputSide* target = MouseTarget();
    return target ? target->Wheel(flags, delta, x, y) : 0;
}

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
    if ((IsMultiplayerClient() & 0xFF) && root_->chatOpen_) {   // the low byte
        root_->Input()->Event(UiInputSide::kEventChar, key, repeat, flags);
        return;
    }
    if (stack_.Start() != stack_.Finish()) stack_.Finish()[-1]->Input()->Event(UiInputSide::kEventChar, key, repeat, flags);
}

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
    if (stack_.Start() != stack_.Finish()) stack_.Finish()[-1]->Input()->Event(UiInputSide::kEventKeyUp, key, repeat, flags);
}

void console::ConsoleWindow::OnKeyUp(int key, int, int) {
    if (key == VK_SHIFT)
        shift_ = false;
    else if (key == VK_CONTROL)
        ctrl_ = false;
}

// --- Every window closed --------------------------------------------------

void CInterface3D::CloseAll() {
    TheCursor()->SetShape(kCursorWait);
    while (stack_.Start() != stack_.Finish()) {
        stack_.Finish()[-1]->Input()->Deactivate();
        ScreenOf(stack_.Finish()[-1])->CloseScreen();
        delete stack_.Finish()[-1];
        --stack_.Finish();
    }
    ctrlDown_ = false;
    altDown_ = false;
    shiftDown_ = 0;
    rightDown_ = false;
    doubleClick_ = false;
    // No top window to deactivate here (the original deactivates it, but none can exist any more).
    if (stack_.Start() != stack_.Finish()) stack_.Finish()[-1]->Input()->Deactivate();
    app::PumpMessages();
    app::g_interfaceHasScreen = 0;
}

void CInterface3D::CloseAllPending() {
    if (!closeAll_) return;
    closeAll_ = false;
    CloseAll();
}
