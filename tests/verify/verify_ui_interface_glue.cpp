// Checks for the interface manager's small services (ui_interface_glue.cpp). All ours only: they
// play or stop sounds, change the console window, the debug panel and the top window. Scenarios:
// every screen with button sounds (PlaySound; ui_camp, ui_screens ...), the global map's and the
// multiplayer hero screen's sound stops (StopSound), console (its "console", "history",
// "debuginfo", "fps" and "show fps" commands and every console line: ConsoleAddText), every zone
// whose scripts change quest variables (OnQuestVariable; zone1's intro sets "q.gz1g..." ones).
#include "verify.h"

#include "../../src/game/console.h"
#include "../../src/game/ui.h"

#include "slot_recorder.h"

#include <cstdint>
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

// The status line's surface exists only while it is shown. The save / load window's saves and
// loads (ui_saveload, ui_saveload_delete), the hero screen and the layouts' notices.
static void __fastcall ShowStatusTextVerify(CInterface3D* self, void*, const char* text) {
    self->CInterface3D::ShowStatusText(text);
    ei::verify::Check("CInterface3D::ShowStatusText", true, self->statusSurface_ == nullptr, "text %s", text ? text : "");
}
EI_VERIFY(0x005F75B0, ShowStatusTextVerify, 0, s_unused);

// --- 005F7F30-005F8310, 005F86D0: the news slots --------------------------------------------------
// Side by side on the call: the top window (the bottom one for 005F86D0) gets a stand-in vtable
// whose news slot records the call and forwards it; ours runs once; the call must be that slot
// with the arguments passed on (none without a window). Scenarios: items picked up and units
// joining or leaving the party (items, ui_hud, every new game: the party joins), speech (combat:
// units shout), saves (ui_saveload: 005F86D0), the game lost (hero_death). The players, chat and
// exchange news (005F82D0, 005F82F0, 005F8310) need a multiplayer client: no scenario.
namespace {
template <int Slot, int Args, typename Run>
void CheckNews(const char* name, CInterface3D* self, bool bottom, std::uintptr_t a, std::uintptr_t b, std::uintptr_t c,
               Run run) {
    CUiWindow* target = self->stackBegin_ == self->stackEnd_ ? nullptr
                        : bottom                             ? self->stackBegin_[0]
                                                             : self->stackEnd_[-1];
    if (!target) {
        run();
        ei::verify::Check(name, true, true, "");
        return;
    }
    ei::verify::SlotRecorder recorder(target);
    recorder.Record<Slot, Args>();
    run();
    const ei::verify::SlotCall& call = ei::verify::SlotRecorder::Call();
    bool ok = call.calls == 1 && call.slot == Slot && (Args < 1 || call.args[0] == a) && (Args < 2 || call.args[1] == b) &&
              (Args < 3 || call.args[2] == c);
    ei::verify::Check(name, true, ok, "calls %d slot %d args %x %x %x", call.calls, call.slot, call.args[0], call.args[1],
                      call.args[2]);
}
std::uintptr_t U(const void* p) { return reinterpret_cast<std::uintptr_t>(p); }
std::uintptr_t U(int v) { return static_cast<std::uintptr_t>(v); }
} // namespace

static void __fastcall OnItemsPickedVerify(CInterface3D* self, void*, void* items, int money, int crippled) {
    CheckNews<28, 3>("CInterface3D::OnItemsPicked", self, false, U(items), U(money), U(crippled),
                     [&] { self->CInterface3D::OnItemsPicked(items, money, crippled); });
}
EI_VERIFY(0x005F7F30, OnItemsPickedVerify, 0, s_unused);

static void __fastcall OnUnitJoinedVerify(CInterface3D* self, void*, void* unit) {
    CheckNews<29, 1>("CInterface3D::OnUnitJoined", self, false, U(unit), 0, 0,
                     [&] { self->CInterface3D::OnUnitJoined(unit); });
}
EI_VERIFY(0x005F7F60, OnUnitJoinedVerify, 0, s_unused);

static void __fastcall OnUnitLeftVerify(CInterface3D* self, void*, void* unit) {
    CheckNews<30, 1>("CInterface3D::OnUnitLeft", self, false, U(unit), 0, 0, [&] { self->CInterface3D::OnUnitLeft(unit); });
}
EI_VERIFY(0x005F7F80, OnUnitLeftVerify, 0, s_unused);

static void __fastcall OnGameLostVerify(CInterface3D* self, void*) {
    CheckNews<31, 1>("CInterface3D::OnGameLost", self, false, 0, 0, 0, [&] { self->CInterface3D::OnGameLost(); });
}
EI_VERIFY(0x005F7FA0, OnGameLostVerify, 0, s_unused);

static void __fastcall OnUnitSpeechVerify(CInterface3D* self, void*, int id, unsigned event) {
    CheckNews<32, 3>("CInterface3D::OnUnitSpeech", self, false, U(id), event, 0,
                     [&] { self->CInterface3D::OnUnitSpeech(id, event); });
}
EI_VERIFY(0x005F82A0, OnUnitSpeechVerify, 0, s_unused);

static void __fastcall OnPlayersChangedVerify(CInterface3D* self, void*, int added) {
    CheckNews<36, 1>("CInterface3D::OnPlayersChanged", self, false, U(added), 0, 0,
                     [&] { self->CInterface3D::OnPlayersChanged(added); });
}
EI_VERIFY(0x005F82D0, OnPlayersChangedVerify, 0, s_unused);

static void __fastcall OnExchangeDoneVerify(CInterface3D* self, void*, void* partyB, void* partyA, void* partyC) {
    CheckNews<37, 3>("CInterface3D::OnExchangeDone", self, false, U(partyB), U(partyA), U(partyC),
                     [&] { self->CInterface3D::OnExchangeDone(partyB, partyA, partyC); });
}
EI_VERIFY(0x005F8310, OnExchangeDoneVerify, 0, s_unused);

static void __fastcall SaveWindowStateVerify(CInterface3D* self, void*) {
    CheckNews<40, 0>("CInterface3D::SaveWindowState", self, true, 0, 0, 0,
                     [&] { self->CInterface3D::SaveWindowState(); });
}
EI_VERIFY(0x005F86D0, SaveWindowStateVerify, 0, s_unused);

// 005F82F0: ours only (the chat overlay's AddLine is checked by its own wrapper).
static void __fastcall OnChatLineVerify(CInterface3D* self, void*, const char* text, std::uint32_t number) {
    self->CInterface3D::OnChatLine(text, number);
    ei::verify::Check("CInterface3D::OnChatLine", true, true, "");
}
EI_VERIFY(0x005F82F0, OnChatLineVerify, 0, s_unused);
