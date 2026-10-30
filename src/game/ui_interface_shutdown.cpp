// The interface manager's end (Interface3D): the input reset after the windows close, the shutdown from the
// main loop's exit (it writes config\tutorial.ini), the destructor body of the global interface run at exit
// and the console window's destructor body. See ui.h and docs/notes/ui.md, section 3.
#include "app.h"
#include "ui.h"

#include "console.h"
#include "game_strings.h"
#include "ui_cursor.h"

#include "platform/file.h"
#include "lib/crt.h"
#include "game_singletons.h"
#include "lib/str.h"

#include <list>
#include <memory>
#include <string>

namespace {
constexpr int kCursorWait = 3;

// A surface the interface owns: its destructor body, then the memory.
void DeleteSurface(UiSurface* surface) {
    if (!surface) return;
    delete surface;
}
} // namespace

void Interface3D::ResetInput() {
    ctrlDown = false;
    altDown = false;
    shiftDown = 0;
    rightDown = false;
    doubleClick = false;
    if (!stack.empty()) stack.back()->Input()->Deactivate();
}

void Interface3D::Shutdown() {
    g_cursor.SetShape(kCursorWait);
    while (!stack.empty()) {
        stack.back()->Input()->Deactivate();
        ScreenOf(stack.back())->CloseScreen();
        delete stack.back();
        stack.pop_back();
    }
    ResetInput();
    app::PumpMessages();
    app::g_interfaceHasScreen = 0;

    platform::TextFile file;
    bool opened;
    {
        std::string path = g_dataFolder + game::TutorialIniText();
        opened = file.Open(path, "w") != 0;
    }
    if (opened) {
        std::string line;
        for (const auto& [topic, page] : helpTopics) {
            line = str::Format(game::TutorialLineFormat(), topic, page);
            file.WriteString(line);
        }
        file.Close();
    }

    DeleteSurface(fpsSurface);
    fpsSurface = nullptr;
    // The queued window is deleted but its pointer kept (like the original).
    delete queuedWindow;
    DeleteSurface(backdrop);
    backdrop = nullptr;
    DeleteSurface(secondBackdrop);
    secondBackdrop = nullptr;
    screenCopy = {};
    if (root) {
        delete root;
        root = nullptr;
    }
    if (console) {
        delete console;
        console = nullptr;
    }
    keybind::SaveKeyBindings();
    for (UiFont& font : fonts) font = UiFont{};
    delete fade;    // (the pointer is not cleared)
}

console::ConsoleWindow::~ConsoleWindow() {
    DeleteSurface(surface);
    surface = nullptr;
    font = UiFont{};
}
