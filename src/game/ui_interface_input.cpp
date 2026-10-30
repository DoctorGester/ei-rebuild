// The interface's input dispatch (Interface3D): the input dispatcher closes the
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
#include "platform/keys.h"

UiInputSide* Interface3D::MouseTarget() const {
    if (mouseCapture) return static_cast<UiInputSide*>(mouseCapture);
    if (stack.begin() != stack.end()) return stack.end()[-1]->Input();
    return nullptr;
}

namespace {
void Send(const Interface3D* ui, int slot, int buttons, int x, int y) {
    if (UiInputSide* target = ui->MouseTarget()) target->Event(slot, buttons, x, y);
}

constexpr int kScanMask = 0x1FF;
constexpr int kScanConsoleKey = 0x29;   // the key under Esc (the console's)
constexpr int kCursorWait = 3;

} // namespace

void Interface3D::OnLeftDown(int buttons, int x, int y) {
    doubleClick = false;
    Send(this, UiInputSide::kEventLeftDown, buttons, x, y);
}

void Interface3D::OnLeftUp(int buttons, int x, int y) {
    Send(this, UiInputSide::kEventLeftUp, buttons, x, y);
    doubleClick = false;
}

void Interface3D::OnMouseMove(int buttons, int x, int y) { Send(this, UiInputSide::kEventMouseMove, buttons, x, y); }

void Interface3D::OnRightDown(int buttons, int x, int y) {
    rightDown = true;
    Send(this, UiInputSide::kEventRightDown, buttons, x, y);
}

void Interface3D::OnRightUp(int buttons, int x, int y) {
    rightDown = false;
    Send(this, UiInputSide::kEventRightUp, buttons, x, y);
}

void Interface3D::OnLeftDouble(int buttons, int x, int y) {
    doubleClick = true;
    Send(this, UiInputSide::kEventLeftDouble, buttons, x, y);
}

void Interface3D::OnRightDouble(int buttons, int x, int y) {
    rightDown = true;
    Send(this, UiInputSide::kEventRightDouble, buttons, x, y);
}

void Interface3D::OnWheel(int flags, int delta, int x, int y) {
    if (UiInputSide* target = MouseTarget()) target->Wheel(flags, delta, x, y);
}

// --- Keys ------------------------------------------------------------------

void Interface3D::OnChar(int key, int repeat, int flags) {
    if ((flags & kScanMask) == kScanConsoleKey) return;
    if (console->shown) {
        static_cast<console::ConsoleWindow*>(console)->OnChar(key, repeat, flags);
        return;
    }
    if (IsMultiplayerClient() && root->chatOpen) {
        root->Input()->Event(UiInputSide::kEventChar, key, repeat, flags);
        return;
    }
    if (!stack.empty()) stack.back()->Input()->Event(UiInputSide::kEventChar, key, repeat, flags);
}

void Interface3D::OnKeyUp(int key, int repeat, int flags) {
    if (key == platform::kKeyShift)
        shiftDown = 0;
    else if (key == platform::kKeyControl)
        ctrlDown = false;
    else if (key == platform::kKeyMenu)
        altDown = false;
    if (console->shown) {
        console->OnKeyUp(key, repeat, flags);
        return;
    }
    if (!stack.empty()) stack.back()->Input()->Event(UiInputSide::kEventKeyUp, key, repeat, flags);
}

void console::ConsoleWindow::OnKeyUp(int key, int, int) {
    if (key == platform::kKeyShift)
        shift = false;
    else if (key == platform::kKeyControl)
        ctrl = false;
}

// --- Every window closed --------------------------------------------------

void Interface3D::CloseAll() {
    g_cursor.SetShape(kCursorWait);
    while (!stack.empty()) {
        stack.back()->Input()->Deactivate();
        ScreenOf(stack.back())->CloseScreen();
        delete stack.back();
        stack.pop_back();
    }
    ctrlDown = false;
    altDown = false;
    shiftDown = 0;
    rightDown = false;
    doubleClick = false;
    // No top window to deactivate here (the original deactivates it, but none can exist any more).
    if (!stack.empty()) stack.back()->Input()->Deactivate();
    app::PumpMessages();
    app::g_interfaceHasScreen = 0;
}

void Interface3D::CloseAllPending() {
    if (!closeAll) return;
    closeAll = false;
    CloseAll();
}
