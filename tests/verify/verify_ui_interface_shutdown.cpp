// Checks for the interface's end (ui_interface_shutdown.cpp). All ours only: each closes windows,
// writes a file or frees memory, so the original cannot run as well.
//   005F8C50 ResetInput: the five flags clear. Reached by every change of screen (Activate) and
//     every close-all (Update 005F57C0), so by every scenario.
//   005F4F90 Shutdown: no window left, flags and screen flag clear, every pointer it frees cleared;
//     config\tutorial.ini read back has one "topic count" line per help topic, the first equal to
//     the map's first. Reached when a game exits through the main loop (every scenario that quits).
//   005F4790 DestroyBody: runs from the exit's static destructors; the map header and the message
//     list's sentinel are back on the node allocator's free lists.
//   005E3DF0 ConsoleWindow::DestroyBody: surface and font cleared. Reached by Shutdown.
#include "verify.h"

#include "../../src/game/console.h"
#include "../../src/game/game_strings.h"
#include "../../src/game/ui.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

bool InputReset(const CInterface3D* ui) {
    return !ui->ctrlDown_ && !ui->altDown_ && !ui->shiftDown_ && !ui->rightDown_ && !ui->doubleClick_;
}

// Whether `node` is among the first few entries of the node allocator's free list for its size.
bool OnFreeList(const void* node, std::size_t bytes) {
    void* const* list = reinterpret_cast<void* const*>(0x0079B224) + (bytes - 1) / 8;
    const void* entry = *list;
    for (int i = 0; i < 4 && entry; ++i, entry = *static_cast<void* const*>(entry)) {
        if (entry == node) return true;
    }
    return false;
}

} // namespace

static void(__fastcall* s_resetInput)(CInterface3D*, void*);
static void __fastcall ResetInputVerify(CInterface3D* self, void*) {
    self->CInterface3D::ResetInput();
    ei::verify::Check("CInterface3D::ResetInput", true, InputReset(self), "flags %d %d %d %d %d", self->ctrlDown_ ? 1 : 0,
                      self->shiftDown_, self->altDown_ ? 1 : 0, self->doubleClick_ ? 1 : 0, self->rightDown_ ? 1 : 0);
}
EI_VERIFY(0x005F8C50, ResetInputVerify, 0, s_resetInput);

static void(__fastcall* s_shutdown)(CInterface3D*, void*);
static void __fastcall ShutdownVerify(CInterface3D* self, void*) {
    std::size_t topics = self->helpTopics_.size();
    char first[64] = {};
    if (topics) std::snprintf(first, sizeof(first), "%s", static_cast<const char*>(self->helpTopics_.begin()->first));
    self->CInterface3D::Shutdown();
    bool closed = self->stackBegin_ == self->stackEnd_ && InputReset(self) && !InterfaceHasScreen();
    bool freed = !self->surface20_ && !self->backdrop_ && !self->secondBackdrop_ && !self->block5C_ && !self->root_ &&
                 !self->console_ && !self->fonts_[0] && !self->fonts_[1] && !self->fonts_[2];
    ei::verify::Check("CInterface3D::Shutdown", true, closed && freed, "closed %d freed %d", closed ? 1 : 0, freed ? 1 : 0);

    // The file read back through the game's own CStdioFile (the harness redirects the path).
    console::CStdioFile file;
    mfc::CString path = GameDataPath() + game::TutorialIniText();
    if (!file.Open(path, console::CStdioFile::typeText, nullptr)) {
        ei::verify::Check("CInterface3D::Shutdown tutorial.ini", true, false, "not readable, %u topics",
                          static_cast<unsigned>(topics));
        return;
    }
    mfc::CString line;
    std::size_t lines = 0;
    bool firstOk = topics == 0;
    while (file.ReadString(line)) {
        if (lines++ == 0 && topics) {
            std::size_t n = std::strlen(first);
            firstOk = std::strncmp(line, first, n) == 0 && static_cast<const char*>(line)[n] == ' ';
        }
    }
    file.Close();
    ei::verify::Check("CInterface3D::Shutdown tutorial.ini", true, lines == topics && firstOk,
                      "lines %u topics %u first '%s' ok %d", static_cast<unsigned>(lines), static_cast<unsigned>(topics),
                      first, firstOk ? 1 : 0);
}
EI_VERIFY(0x005F4F90, ShutdownVerify, 0, s_shutdown);

static void(__fastcall* s_destroyBody)(CInterface3D*, void*);
static void __fastcall DestroyBodyVerify(CInterface3D* self, void*) {
    const void* header = self->helpTopics_.end();
    const void* sentinel = self->messages_.head;
    self->CInterface3D::DestroyBody();
    bool mapFreed = OnFreeList(header, sizeof(stlp::map<mfc::CString, int>::Node));
    bool listFreed = OnFreeList(sentinel, sizeof(stlp::list<mfc::CString>::Node));
    ei::verify::Check("CInterface3D::DestroyBody", true, mapFreed && listFreed, "map %d list %d", mapFreed ? 1 : 0,
                      listFreed ? 1 : 0);
}
EI_VERIFY(0x005F4790, DestroyBodyVerify, 0, s_destroyBody);

static void(__fastcall* s_consoleDestroy)(console::ConsoleWindow*, void*);
static void __fastcall ConsoleDestroyVerify(console::ConsoleWindow* self, void*) {
    self->console::ConsoleWindow::DestroyBody();
    ei::verify::Check("ConsoleWindow::DestroyBody", true, !self->surface_ && !self->font_, "surface %p font %p",
                      self->surface_, static_cast<void*>(self->font_));
}
EI_VERIFY(0x005E3DF0, ConsoleDestroyVerify, 0, s_consoleDestroy);
