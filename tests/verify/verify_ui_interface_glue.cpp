// Checks for the interface manager's small services (ui_interface_glue.cpp). All ours only: they
// play or stop sounds, change the console window, the debug panel and the top window. Scenarios:
// every screen with button sounds (PlaySound; ui_camp, ui_screens ...), the global map's and the
// multiplayer hero screen's sound stops (StopSound), console (its "console", "history",
// "debuginfo", "fps" and "show fps" commands and every console line: ConsoleAddText), every zone
// whose scripts change quest variables (OnQuestVariable; zone1's intro sets "q.gz1g..." ones).
#include "verify.h"

#include "../../src/game/console.h"
#include "../../src/game/ui.h"

#include <cstdio>
#include <cstring>

namespace {
void* s_unused;
} // namespace

static int __fastcall PlaySoundVerify(CInterface3D* self, void*, const char* name, int loop) {
    int handle = self->CInterface3D::PlaySound(name, loop);
    ei::verify::Check("CInterface3D::PlaySound", true, true, "");
    return handle;
}
EI_VERIFY(0x005F7E60, PlaySoundVerify, 0, s_unused);

static void __fastcall StopSoundVerify(CInterface3D* self, void*, int handle) {
    self->CInterface3D::StopSound(handle);
    ei::verify::Check("CInterface3D::StopSound", true, true, "");
}
EI_VERIFY(0x005F7E90, StopSoundVerify, 0, s_unused);

// The console keeps at most its history's lines.
static void __fastcall ConsoleAddTextVerify(CInterface3D* self, void*, const char* text) {
    self->CInterface3D::ConsoleAddText(text);
    console::ConsoleWindow* window = self->console_;
    bool ok = !window || window->history_ <= 0 || window->lines_.GetSize() <= window->history_;
    ei::verify::Check("CInterface3D::ConsoleAddText", true, ok, "lines %d history %d", window ? window->lines_.GetSize() : -1,
                      window ? window->history_ : -1);
}
EI_VERIFY(0x005F7EB0, ConsoleAddTextVerify, 0, s_unused);

// The console's height is clamped to at least 69 pixels and its line count follows from it.
static void __fastcall SetConsoleRectVerify(CInterface3D* self, void*, const int* rect) {
    self->CInterface3D::SetConsoleRect(rect);
    console::ConsoleWindow* window = self->console_;
    bool ok = !window || (window->height_ >= 0x45 && window->width_ >= 0x4C &&
                          window->visibleLines_ == (window->height_ - 0x18) / 15 - 1);
    ei::verify::Check("CInterface3D::SetConsoleRect", true, ok, "size %d x %d lines %d", window ? window->width_ : -1,
                      window ? window->height_ : -1, window ? window->visibleLines_ : -1);
}
EI_VERIFY(0x005F7ED0, SetConsoleRectVerify, 0, s_unused);

static void __fastcall SetConsoleHistoryVerify(CInterface3D* self, void*, int lines) {
    self->CInterface3D::SetConsoleHistory(lines);
    console::ConsoleWindow* window = self->console_;
    bool ok = !window || (window->history_ == lines && (lines < 0 || window->lines_.GetSize() <= lines));
    ei::verify::Check("CInterface3D::SetConsoleHistory", true, ok, "lines %d", lines);
}
EI_VERIFY(0x005F7EF0, SetConsoleHistoryVerify, 0, s_unused);

static void __fastcall SetDebugInfoRectVerify(CInterface3D* self, void*, const RECT* rect) {
    self->CInterface3D::SetDebugInfoRect(rect);
    ei::verify::Check("CInterface3D::SetDebugInfoRect", true, true, "");
}
EI_VERIFY(0x005F7F10, SetDebugInfoRectVerify, 0, s_unused);

// A surface exactly while shown (allocation failures aside).
static void __fastcall ShowFpsVerify(CInterface3D* self, void*, int on) {
    self->CInterface3D::ShowFps(on);
    bool ok = (self->fpsSurface_ != nullptr) == (static_cast<std::uint8_t>(on) != 0);
    ei::verify::Check("CInterface3D::ShowFps", true, ok, "on %d surface %p", on & 0xFF, static_cast<void*>(self->fpsSurface_));
}
EI_VERIFY(0x005F5700, ShowFpsVerify, 0, s_unused);

// The current quest changes only at 1 (or NaN), to a '.' part of the name after the second.
static void __fastcall OnQuestVariableVerify(CInterface3D* self, void*, const char* name, float value) {
    char before[128] = {};
    std::snprintf(before, sizeof(before), "%s", static_cast<const char*>(self->savedText_));
    self->CInterface3D::OnQuestVariable(name, value);
    const char* after = self->savedText_;
    bool changed = std::strcmp(before, after) != 0;
    bool atOne = !(value < 1.0f) && !(value > 1.0f);
    bool ok = !changed || (atOne && name && *after && std::strstr(name, after) != nullptr);
    ei::verify::Check("CInterface3D::OnQuestVariable", true, ok, "name %s value %g quest '%s' -> '%s'", name ? name : "",
                      value, before, after);
}
EI_VERIFY(0x005F7FC0, OnQuestVariableVerify, 0, s_unused);
