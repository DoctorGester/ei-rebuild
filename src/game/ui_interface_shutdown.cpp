// The interface manager's end (CInterface3D): the input reset after the windows close (005F8C50),
// the shutdown from the main loop's exit (005F4F90; it writes config\tutorial.ini), the destructor
// body of the global interface run at exit (005F4790) and the console window's destructor body
// (005E3DF0). See ui.h and docs/notes/ui.md, section 3.
#include "ui.h"

#include "console.h"
#include "game_strings.h"
#include "ui_cursor.h"
#include "ui_gdi.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "originals_engine.h"
#include "originals_ui.h"

namespace {
constexpr int kCursorWait = 3;

// A surface the interface owns: its destructor body, then the memory.
void DeleteSurface(CUiSurface* surface) {
    if (!surface) return;
    surface->ReleaseContents();
    gamecrt::operator_delete(surface);
}
} // namespace

void CInterface3D::ResetInput() {
    ctrlDown_ = false;
    altDown_ = false;
    shiftDown_ = 0;
    rightDown_ = false;
    doubleClick_ = false;
    if (stackBegin_ != stackEnd_) stackEnd_[-1]->Input()->Deactivate();
}
EI_REPLACE_METHOD(0x005F8C50, CInterface3D::ResetInput);

void CInterface3D::Shutdown() {
    GameCursor()->SetShape(kCursorWait);
    while (stackBegin_ != stackEnd_) {
        stackEnd_[-1]->Input()->Deactivate();
        stackEnd_[-1]->CloseWindow();
        if (CUiWindow* top = stackEnd_[-1]) top->DeleteWindow();
        --stackEnd_;
    }
    ResetInput();
    PumpMessagesOriginal();
    InterfaceHasScreen() = false;

    console::CStdioFile file;
    bool opened;
    {
        mfc::CString path = GameDataPath() + game::TutorialIniText();
        opened = file.Open(path, console::CStdioFile::modeCreate | console::CStdioFile::modeWrite, nullptr) != 0;
    }
    if (opened) {
        mfc::CString line;
        using Topics = stlp::map<mfc::CString, int>;
        for (Topics::Node* n = helpTopics_.begin(); n != helpTopics_.end(); n = Topics::Next(n)) {
            line.Format(game::TutorialLineFormat(), static_cast<const char*>(n->first), n->second);
            file.WriteString(line);
        }
        file.Close();
    }

    DeleteSurface(fpsSurface_);
    fpsSurface_ = nullptr;
    // The queued window is deleted but its pointer kept (the original does not clear it).
    if (queuedWindow_) queuedWindow_->DeleteWindow();
    DeleteSurface(backdrop_);
    backdrop_ = nullptr;
    DeleteSurface(secondBackdrop_);
    secondBackdrop_ = nullptr;
    if (block5C_) gamecrt::operator_delete(block5C_);
    block5C_ = nullptr;
    DebugPanelReleaseOriginal(DebugInfoPanel());
    if (root_) {
        root_->DeleteWindow();
        root_ = nullptr;
    }
    if (console_) {
        console_->DestroyBody();
        gamecrt::operator_delete(console_);
        console_ = nullptr;
    }
    SaveKeyBindingsOriginal();
    for (ui_gdi::GameFont*& font : fonts_) {
        if (font) font->Delete();
        font = nullptr;
    }
    // The fade's destructor body (005E69A0) is empty; the pointer is not cleared.
    if (fade_) gamecrt::operator_delete(fade_);
}
EI_REPLACE_METHOD(0x005F4F90, CInterface3D::Shutdown);

void CInterface3D::DestroyBody() {
    savedText_.~CString();
    messageText_.~CString();
    helpTopics_.Destroy();
    messages_.ClearWith([](mfc::CString* text) { text->~CString(); });
    stlp::deallocate(messages_.head, sizeof(stlp::list<mfc::CString>::Node));
    if (stackBegin_) {
        stlp::deallocate(stackBegin_, static_cast<std::size_t>(stackCapacity_ - stackBegin_) * sizeof(CUiWindow*));
    }
}
EI_REPLACE_METHOD(0x005F4790, CInterface3D::DestroyBody);

void console::ConsoleWindow::DestroyBody() {
    DeleteSurface(static_cast<CUiSurface*>(surface_));
    surface_ = nullptr;
    if (font_) font_->Delete();
    font_ = nullptr;
    // The members in reverse order: the lines, the input text, the command history.
    this->~ConsoleWindow();
}
EI_REPLACE_METHOD(0x005E3DF0, console::ConsoleWindow::DestroyBody);
