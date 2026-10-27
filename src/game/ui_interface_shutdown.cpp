// The interface manager's end (CInterface3D): the input reset after the windows close, the shutdown from the
// main loop's exit (it writes config\tutorial.ini), the destructor body of the global interface run at exit
// and the console window's destructor body. See ui.h and docs/notes/ui.md, section 3.
#include "app.h"
#include "ui.h"

#include "console.h"
#include "game_strings.h"
#include "ui_cursor.h"
#include "ui_gdi.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "game_singletons.h"
#include "lib/str.h"

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
    if (stack_.Start() != stack_.Finish()) stack_.Finish()[-1]->Input()->Deactivate();
}
EI_REWRITTEN(0x005F8C50, CInterface3D::ResetInput);

void CInterface3D::Shutdown() {
    TheCursor()->SetShape(kCursorWait);
    while (stack_.Start() != stack_.Finish()) {
        stack_.Finish()[-1]->Input()->Deactivate();
        ScreenOf(stack_.Finish()[-1])->CloseScreen();
        delete stack_.Finish()[-1];
        --stack_.Finish();
    }
    ResetInput();
    app::PumpMessages();
    app::g_interfaceHasScreen = 0;

    console::CStdioFile file;
    bool opened;
    {
        std::string path = (TheDataFolder() + game::TutorialIniText()).GetString();
        opened = file.Open(path, console::CStdioFile::modeCreate | console::CStdioFile::modeWrite, nullptr) != 0;
    }
    if (opened) {
        std::string line;
        using Topics = decltype(helpTopics_);
        for (Topics::Node* n = helpTopics_.begin(); n != helpTopics_.end(); n = Topics::Next(n)) {
            line = str::Format(game::TutorialLineFormat(), n->first.GetString(), n->second);
            file.WriteString(line.c_str());
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
    if (block5C_) gamecrt::operator_delete(block5C_);
    block5C_ = nullptr;
    DebugInfoPanel()->Release();
    if (root_) {
        delete root_;
        root_ = nullptr;
    }
    if (console_) {
        console_->DestroyBody();
        gamecrt::operator_delete(console_);
        console_ = nullptr;
    }
    keybind::SaveKeyBindings();
    for (mfc::impl::CFont*& font : fonts_) {
        delete font;
        font = nullptr;
    }
    // The fade's destructor body is empty; the pointer is not cleared.
    if (fade_) gamecrt::operator_delete(fade_);
}
EI_REWRITTEN(0x005F4F90, CInterface3D::Shutdown);

void CInterface3D::DestroyBody() {
    std::destroy_at(&savedText_);
    std::destroy_at(&messageText_);
    helpTopics_.Destroy();
    messages_.ClearWith([](mfc::CString* text) { std::destroy_at(text); });
    stlp::deallocate(messages_.head, sizeof(stlp::list<mfc::CString>::Node));
    if (stack_.Start()) {
        stlp::deallocate(stack_.Start(), static_cast<std::size_t>(stack_.EndOfStorage() - stack_.Start()) * sizeof(CUiWindow*));
    }
}
EI_REWRITTEN(0x005F4790, CInterface3D::DestroyBody);

void console::ConsoleWindow::DestroyBody() {
    DeleteSurface(surface_);
    surface_ = nullptr;
    delete font_;
    font_ = nullptr;
    // The members in reverse order: the lines, the input text, the command history.
    this->~ConsoleWindow();
}
EI_REWRITTEN(0x005E3DF0, console::ConsoleWindow::DestroyBody);
