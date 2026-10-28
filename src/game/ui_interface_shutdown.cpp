// The interface manager's end (CInterface3D): the input reset after the windows close, the shutdown from the
// main loop's exit (it writes config\tutorial.ini), the destructor body of the global interface run at exit
// and the console window's destructor body. See ui.h and docs/notes/ui.md, section 3.
#include "app.h"
#include "ui.h"

#include "console.h"
#include "game_strings.h"
#include "ui_cursor.h"
#include "ui_gdi.h"

#include "lib/file_io.h"
#include "lib/crt.h"
#include "game_singletons.h"
#include "lib/str.h"

#include <list>
#include <memory>
#include <string>

namespace {
constexpr int kCursorWait = 3;

// A surface the interface owns: its destructor body, then the memory.
void DeleteSurface(CUiSurface* surface) {
    if (!surface) return;
    delete surface;
}
} // namespace

void CInterface3D::ResetInput() {
    ctrlDown_ = false;
    altDown_ = false;
    shiftDown_ = 0;
    rightDown_ = false;
    doubleClick_ = false;
    if (!stack_.empty()) stack_.back()->Input()->Deactivate();
}

void CInterface3D::Shutdown() {
    g_cursor->SetShape(kCursorWait);
    while (!stack_.empty()) {
        stack_.back()->Input()->Deactivate();
        ScreenOf(stack_.back())->CloseScreen();
        delete stack_.back();
        stack_.pop_back();
    }
    ResetInput();
    app::PumpMessages();
    app::g_interfaceHasScreen = 0;

    lib::TextFile file;
    bool opened;
    {
        std::string path = *g_dataFolder + game::TutorialIniText();
        opened = file.Open(path, "w") != 0;
    }
    if (opened) {
        std::string line;
        for (const auto& [topic, page] : helpTopics_) {
            line = str::Format(game::TutorialLineFormat(), topic.c_str(), page);
            file.WriteString(line);
        }
        file.Close();
    }

    DeleteSurface(fpsSurface_);
    fpsSurface_ = nullptr;
    // The queued window is deleted but its pointer kept (like the original).
    delete queuedWindow_;
    DeleteSurface(backdrop_);
    backdrop_ = nullptr;
    DeleteSurface(secondBackdrop_);
    secondBackdrop_ = nullptr;
    if (block5C_) delete[] block5C_;
    block5C_ = nullptr;
    strmap::g_debugPanel->Release();
    if (root_) {
        delete root_;
        root_ = nullptr;
    }
    if (console_) {
        delete console_;
        console_ = nullptr;
    }
    keybind::SaveKeyBindings();
    for (gdi::Font& font : fonts_) font.reset();
    delete fade_;   // (the pointer is not cleared)
}

void CInterface3D::DestroyBody() {
    std::destroy_at(&savedText_);
    std::destroy_at(&messageText_);
    std::destroy_at(&helpTopics_);
    std::destroy_at(&messages_);
    std::destroy_at(&stack_);
}

console::ConsoleWindow::~ConsoleWindow() {
    DeleteSurface(surface_);
    surface_ = nullptr;
    font_.reset();
}
