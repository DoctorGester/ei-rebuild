// Differential checks for the developer console (src/game/console*.cpp).
//
// Console output goes through the ostream 007C55A8 into its streambuf. To compare what two
// implementations print, the ostream's streambuf pointer is pointed at a capture streambuf for each
// run: a console streambuf (constructed by the game's code) whose sync stores the line instead of
// handing it to the console window. The original runs first, ours second; the outputs (and, where
// a command changes a setting, the setting after each run) are compared, and ours is then replayed
// into the real console. Only commands that can safely run twice run side by side; the others run
// ours only (inside a capture, replayed the same way) and are checked for consistency.
#include "verify.h"

#include "../../src/game/acks_db.h"
#include "../../src/game/console.h"
#include "../../src/game/world_glue.h"
#include "../../src/lib/crt.h"

#include "ei/rebirth.h"
#include "../../src/game/originals_library.h"

#include <windows.h>

#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace {
void* s_unused;

using namespace console;

std::string Text(const mfc::CString& s) { return std::string(static_cast<const char*>(s), Length(s)); }

std::string Joined(const CStringArray* args) {
    std::string s;
    for (int i = 0; i < args->GetSize(); ++i) s += (i ? " " : "") + Text((*args)[i]);
    return s;
}

// ---- Output capture -----------------------------------------------------------------------------

struct StreambufAt : ConsoleStreambuf {
    StreambufAt* ConstructAt(unsigned filter);        // 005DC200
    StreambufAt* DeletingDestructAt(unsigned flags);  // 005DC240
};
__declspec(naked) StreambufAt* StreambufAt::ConstructAt(unsigned) EI_ORIGINAL(0x005DC200)
__declspec(naked) StreambufAt* StreambufAt::DeletingDestructAt(unsigned) EI_ORIGINAL(0x005DC240)

using OverflowFn = int(__thiscall*)(ConsoleStreambuf*, int);
using SyncFn = int(__thiscall*)(ConsoleStreambuf*);

// The streambuf pointer (ios::bp) of the console ostream.
ConsoleStreambuf*& ConsoleBuffer() {
    char* os = reinterpret_cast<char*>(0x007C55A8);
    char* ios = os + (*reinterpret_cast<int**>(os))[1];
    return *reinterpret_cast<ConsoleStreambuf**>(ios + 4);
}

struct Capture;
std::vector<Capture*> g_captures;

struct Captured {
    std::vector<std::string> lines;   // each sync's text
    std::string pending;              // written but not synced
    bool operator==(const Captured& o) const { return lines == o.lines && pending == o.pending; }
    std::string Describe() const {
        std::string s;
        for (const std::string& l : lines) s += "[" + l + "]";
        if (!pending.empty()) s += " pending [" + pending + "]";
        return s;
    }
};

int __fastcall CaptureSync(ConsoleStreambuf* self, void*);

void* g_captureVtable[11];

struct Capture {
    StreambufAt* buffer = nullptr;
    ConsoleStreambuf* previous = nullptr;
    Captured out;

    Capture() {
        if (!g_captureVtable[0]) {
            std::memcpy(g_captureVtable, reinterpret_cast<void*>(kConsoleStreambufVtable), sizeof(g_captureVtable));
            g_captureVtable[1] = reinterpret_cast<void*>(&CaptureSync);
        }
        previous = ConsoleBuffer();
        if (!previous) return;
        buffer = static_cast<StreambufAt*>(gamecrt::operator_new(sizeof(ConsoleStreambuf)));
        buffer->ConstructAt(previous->filter_);
        buffer->vtable_ = g_captureVtable;
        ConsoleBuffer() = buffer;
        g_captures.push_back(this);
    }
    // Restores the console's streambuf and returns what was written.
    Captured Stop() {
        if (!buffer) return out;
        reinterpret_cast<OverflowFn>(buffer->vtable_[7])(buffer, -1);
        out.pending = Text(buffer->Line());
        ConsoleBuffer() = previous;
        g_captures.pop_back();
        buffer->vtable_ = reinterpret_cast<void**>(kConsoleStreambufVtable);
        buffer->DeletingDestructAt(1);
        buffer = nullptr;
        return out;
    }
    ~Capture() { Stop(); }
};

int __fastcall CaptureSync(ConsoleStreambuf* self, void*) {
    reinterpret_cast<OverflowFn>(self->vtable_[7])(self, -1);
    for (Capture* c : g_captures)
        if (c->buffer == self) c->out.lines.push_back(Text(self->Line()));
    self->Line() = "";
    return 0;
}

// Writes captured output to the console's current streambuf as it was written: each synced text
// through its sync (after the characters already pending), the rest left pending.
void Replay(const Captured& captured) {
    ConsoleStreambuf* target = ConsoleBuffer();
    if (!target) return;
    for (const std::string& line : captured.lines) {
        reinterpret_cast<OverflowFn>(target->vtable_[7])(target, -1);
        Ops(target->Line()).AppendText(line.c_str());
        reinterpret_cast<SyncFn>(target->vtable_[1])(target);
    }
    if (!captured.pending.empty()) {
        reinterpret_cast<OverflowFn>(target->vtable_[7])(target, -1);
        Ops(target->Line()).AppendText(captured.pending.c_str());
    }
}

// ---- Command handlers ---------------------------------------------------------------------------

using Handler = void(__fastcall*)(CStringArray*);

// Original, then ours, each with its output captured; `state` describes the setting the command
// changes (compared after each run), `reset` puts the setting back between the runs.
void SideBySide(const char* name, Handler original, Handler ours, CStringArray* args,
                const std::function<std::string()>& state = {}, const std::function<void()>& reset = {}) {
    Capture first;
    original(args);
    Captured expected = first.Stop();
    std::string expectedState = state ? state() : std::string();
    if (reset) reset();
    Capture second;
    ours(args);
    Captured actual = second.Stop();
    std::string actualState = state ? state() : std::string();
    ei::verify::Check(name, true, expected == actual && expectedState == actualState, "'%s': %s %s vs %s %s",
                      Joined(args).c_str(), expected.Describe().c_str(), expectedState.c_str(),
                      actual.Describe().c_str(), actualState.c_str());
    Replay(actual);
}

// Ours only, output captured and replayed; `check` gets the output.
void OursOnly(const char* name, Handler ours, CStringArray* args, const std::function<bool(const Captured&)>& check) {
    Capture capture;
    ours(args);
    Captured out = capture.Stop();
    ei::verify::Check(name, true, check(out), "'%s': %s", Joined(args).c_str(), out.Describe().c_str());
    Replay(out);
}

bool PrintedOnly(const Captured& out, std::uintptr_t text) {
    return out.pending.empty() && out.lines.size() == 1 && out.lines[0] == std::string(GameText(text)) + "\n";
}

std::string Int(int v) { return std::to_string(v); }
int& At(std::uintptr_t address) { return *reinterpret_cast<int*>(address); }
char* WindowPtr() { return *reinterpret_cast<char**>(0x007C7D50 + 0x14); }

#define SIDE_BY_SIDE(addr, Name, stolen, ...)                                                  \
    Handler s_##Name;                                                                          \
    void __fastcall Name##Verify(CStringArray* args) {                                         \
        SideBySide(#Name, s_##Name, &commands::Name, args, ##__VA_ARGS__);                     \
    }                                                                                          \
    EI_VERIFY(addr, Name##Verify, stolen, s_##Name);

// 005DF5E0: PUSH imm32 (5).
SIDE_BY_SIDE(0x005DF5E0, Help, 5)
// 005DD920: MOV EAX,FS:[0] (6). The debug info rectangle is set twice to the same value.
SIDE_BY_SIDE(0x005DD920, DebugInfo, 6)
// 005DDBE0 (6). The console rectangle is compared after each run.
SIDE_BY_SIDE(0x005DDBE0, ConsolePos, 6, [] {
    char* w = WindowPtr();
    return w ? Int(At(reinterpret_cast<std::uintptr_t>(w) + 0x48)) + "," + Int(At(reinterpret_cast<std::uintptr_t>(w) + 0x4C)) + "," +
                   Int(At(reinterpret_cast<std::uintptr_t>(w) + 0x50)) + "," + Int(At(reinterpret_cast<std::uintptr_t>(w) + 0x54))
             : std::string("no window");
})
// 005DDF80: PUSH ECX; MOV EAX,[ECX+8]; TEST EAX,EAX (6). The history depth is compared.
SIDE_BY_SIDE(0x005DDF80, History, 6, [] {
    char* w = WindowPtr();
    return w ? Int(At(reinterpret_cast<std::uintptr_t>(w) + 0x6C)) : std::string("no window");
})
// 005DE460 (6). The debug display flags are compared.
SIDE_BY_SIDE(0x005DE460, Show, 6, [] { return Int(At(0x007AF5B8 + 0x88)); })
// 005DEEE0: SUB ESP,8; PUSH ESI; PUSH EDI (5).
SIDE_BY_SIDE(0x005DEEE0, LastFps, 5)
// 005DEA00: PUSH EBP; MOV EBP,ESP; AND ESP,-8 (6). Writes the same file twice.
SIDE_BY_SIDE(0x005DEA00, ListVar, 6)
// 005DEC90 (6). Sets the same variables twice.
SIDE_BY_SIDE(0x005DEC90, LoadVar, 6)
// 005E00D0 (6). Pure.
SIDE_BY_SIDE(0x005E00D0, Days, 6)
// 005E0830 (6).
SIDE_BY_SIDE(0x005E0830, Fps, 6)

// 005DDFE0 (6): the filter mask is compared, and restored before ours runs.
Handler s_filter;
void __fastcall FilterVerify(CStringArray* args) {
    unsigned before = FilterMask();
    SideBySide("Filter", s_filter, &commands::Filter, args, [] { return Int(static_cast<int>(FilterMask())); },
               [before] { FilterMask() = before; });
}
EI_VERIFY(0x005DDFE0, FilterVerify, 6, s_filter);

// 005E0990 / 005E0A10: PUSH ECX; CMP DWORD [ECX+8],1 (5): the rate is compared and restored.
Handler s_rate;
void __fastcall RateVerify(CStringArray* args) {
    int before = At(0x00783E34);
    SideBySide("Rate", s_rate, &commands::Rate, args, [] { return Int(At(0x00783E34)); }, [before] { At(0x00783E34) = before; });
}
EI_VERIFY(0x005E0990, RateVerify, 5, s_rate);

Handler s_localRate;
void __fastcall LocalRateVerify(CStringArray* args) {
    int before = At(0x00783E38);
    SideBySide("LocalRate", s_localRate, &commands::LocalRate, args, [] { return Int(At(0x00783E38)); },
               [before] { At(0x00783E38) = before; });
}
EI_VERIFY(0x005E0A10, LocalRateVerify, 5, s_localRate);

// 005DF9B0 (6): "net time" (and the usage message) side by side, comparing the setting; the other
// subcommands change network state and run ours only.
Handler s_netCommand;
void __fastcall NetVerify(CStringArray* args) {
    if (args->GetSize() != 2 || gamecrt::mbsicmp(reinterpret_cast<const unsigned char*>(static_cast<const char*>((*args)[0])),
                                                 reinterpret_cast<const unsigned char*>("time")) == 0) {
        int before = At(0x0079BC60);
        SideBySide("Net", s_netCommand, &commands::Net, args, [] { return Int(At(0x0079BC60)); }, [before] { At(0x0079BC60) = before; });
        return;
    }
    OursOnly("Net", &commands::Net, args, [](const Captured& out) { return out.lines.empty() && out.pending.empty(); });
}
EI_VERIFY(0x005DF9B0, NetVerify, 6, s_netCommand);

// 005DEF50: ours only (runs other commands). Checks the usage / "Cannot open file" message against
// whether the file exists.
void __fastcall ExecVerify(CStringArray* args) {
    if (args->GetSize() == 0) {
        OursOnly("Exec", &commands::Exec, args, [](const Captured& out) { return PrintedOnly(out, 0x00792720); });
        return;
    }
    std::string name = Text((*args)[0]);
    while (!name.empty() && name.front() == '"') name.erase(0, 1);
    while (!name.empty() && name.back() == '"') name.pop_back();
    DWORD attributes = GetFileAttributesA(name.c_str());
    bool exists = attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
    OursOnly("Exec", &commands::Exec, args, [&](const Captured& out) {
        bool failed = false;
        for (const std::string& l : out.lines) failed |= l.rfind(GameText(0x0079266C), 0) == 0;
        return failed != exists;
    });
}
EI_VERIFY(0x005DEF50, ExecVerify, 0, s_unused);

// 005DF0F0: ours only. "give <n> money <m>" must add m to the player's money.
void __fastcall GiveVerify(CStringArray* args) {
    char* player = nullptr;
    int before = 0;
    bool money = args->GetSize() > 2 && std::strcmp((*args)[1], "money") == 0;
    if (money) {
        using GetPlayerFn = char*(__thiscall*)(void*, int);
        player = reinterpret_cast<GetPlayerFn>(0x00594180)(reinterpret_cast<void*>(0x007AFF60), std::atoi((*args)[0]));
        money = player && *reinterpret_cast<void**>(player + 0x20);
        if (money) before = *reinterpret_cast<int*>(player + 0xC0);
    }
    OursOnly("Give", &commands::Give, args, [&](const Captured&) {
        return !money || *reinterpret_cast<int*>(player + 0xC0) - before == std::atoi((*args)[2]);
    });
}
EI_VERIFY(0x005DF0F0, GiveVerify, 0, s_unused);

// 005DF5B0: ours only; prints "Looting..." first.
void __fastcall LootAllVerify(CStringArray* args) {
    OursOnly("LootAll", &commands::LootAll, args, [](const Captured& out) {
        return !out.lines.empty() && out.lines[0] == std::string(GameText(0x00792838)) + "\n";
    });
}
EI_VERIFY(0x005DF5B0, LootAllVerify, 0, s_unused);

// 005DF8E0: ours only (starts a fade); prints "Ok" or the usage.
void __fastcall FadeOutVerify(CStringArray* args) {
    bool usage = args->GetSize() <= 0;
    OursOnly("FadeOut", &commands::FadeOut, args,
             [usage](const Captured& out) { return PrintedOnly(out, usage ? 0x00792B40 : 0x00792B54); });
}
EI_VERIFY(0x005DF8E0, FadeOutVerify, 0, s_unused);

// 005E0570 / 005E0590 / 005E06E0: ours only (network); they print nothing.
bool Silent(const Captured& out) { return out.lines.empty() && out.pending.empty(); }
void __fastcall PartyResendVerify(CStringArray* args) { OursOnly("PartyResend", &commands::PartyResend, args, Silent); }
EI_VERIFY(0x005E0570, PartyResendVerify, 0, s_unused);
void __fastcall KickVerify(CStringArray* args) { OursOnly("Kick", &commands::Kick, args, Silent); }
EI_VERIFY(0x005E0590, KickVerify, 0, s_unused);
void __fastcall BanVerify(CStringArray* args) { OursOnly("Ban", &commands::Ban, args, Silent); }
EI_VERIFY(0x005E06E0, BanVerify, 0, s_unused);

// ---- Executor, tokenizer, script lines ----------------------------------------------------------

// Read-only lookup in a command map (the map's own operator[] inserts).
bool FindCommand(CommandMap* map, const char* key, CommandHandler* handler) {
    auto less = [](const char* a, const char* b) {
        return gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(a), reinterpret_cast<const unsigned char*>(b)) < 0;
    };
    CommandNode* header = map->end();
    CommandNode* y = header;
    for (CommandNode* x = header->parent; x;) {
        if (!less(x->first, key)) {
            y = x;
            x = x->left;
        } else {
            x = x->right;
        }
    }
    if (y == header || less(key, y->first)) return false;
    *handler = y->second;
    return true;
}

std::string CheatWord() {
    std::string word;
    for (const char* p = GameText(kCheatWordXor66); *p; ++p) word += static_cast<char>(*p ^ 0x66);
    return word;
}

// 005DC620: ours only. Afterwards every command is registered with its handler, and the cheat flag
// changed only for the cheat word (the first token). With Rebirth's AddNewConsoleCommands, a line
// with a token also registers addon's commands, whose "help" replaces the game's (an empty line
// returns before that point, after the game's registrations).
void __fastcall ExecuteVerify(const char* text) {
    std::string line = text ? text : "";
    bool cheatBefore = CheatEnabled();
    Execute(text);
    std::size_t start = line.find_first_not_of(' ');
    bool addon = ei::rebirth::Enabled(ei::rebirth::Patch::AddNewConsoleCommands) && start != std::string::npos;
    bool registered = true;
    for (const CommandRegistration& r : kCommandRegistrations) {
        bool replaced = false;
        for (const AddonCommandRegistration& a : kAddonCommandRegistrations)
            replaced |= addon && a.cheat == r.cheat && std::strcmp(a.name, GameText(r.name)) == 0;
        if (replaced) continue;
        CommandHandler handler = nullptr;
        registered &= FindCommand(r.cheat ? CheatCommands() : PublicCommands(), GameText(r.name), &handler) &&
                      reinterpret_cast<std::uintptr_t>(handler) == r.handler;
    }
    for (const AddonCommandRegistration& a : kAddonCommandRegistrations) {
        if (!addon) break;
        CommandHandler handler = nullptr;
        registered &= FindCommand(a.cheat ? CheatCommands() : PublicCommands(), a.name, &handler) &&
                      handler == a.handler;
    }
    std::string first = start == std::string::npos ? "" : line.substr(start, line.find(' ', start) - start);
    for (char& c : first) c = static_cast<char>(gamecrt::tolower(static_cast<unsigned char>(c)));
    bool cheatOk = CheatEnabled() == cheatBefore || (!cheatBefore && first == CheatWord());
    ei::verify::Check("Execute", true, registered && cheatOk, "'%s': registered %d, cheat %d -> %d",
                      line.c_str(), registered, cheatBefore, CheatEnabled());
}
EI_VERIFY(0x005DC620, ExecuteVerify, 0, s_unused);

// 004DC200: PUSH -1; PUSH imm32 (7). The original splits into a scratch array.
int(__fastcall* s_tokenize)(const char*, CStringArray*);
int __fastcall TokenizeVerify(const char* text, CStringArray* out) {
    CStringArray scratch;
    int expected = s_tokenize(text, &scratch);
    int actual = Tokenize(text, out);
    bool same = expected == actual && scratch.GetSize() == out->GetSize() && scratch.maxSize == out->maxSize &&
                scratch.growBy == out->growBy;
    for (int i = 0; same && i < out->GetSize(); ++i) same = Text(scratch[i]) == Text((*out)[i]);
    ei::verify::Check("Tokenize", true, same, "'%s': %d tokens (max %d) vs %d (max %d): %s",
                      text, scratch.GetSize(), scratch.maxSize, out->GetSize(), out->maxSize, Joined(out).c_str());
    return expected;
}
EI_VERIFY(0x004DC200, TokenizeVerify, 7, s_tokenize);

// 005DD800: ours only (loads and runs a script). The mission script (007C29CC) counts each file it
// loads (+0x1AC); a failed load does not count and leaves its message in +0x1EC. So: printed
// "Script error: <message>" exactly when the count did not change, with the loader's message.
void __fastcall RunScriptLineVerify(const mfc::CString* text, const CStringArray* args, const char* prefix,
                                    const char* suffix) {
    char* script = reinterpret_cast<char*>(0x007C29CC);
    int filesBefore = *reinterpret_cast<int*>(script + 0x1AC);
    Capture capture;
    RunScriptLine(text, args, prefix, suffix);
    Captured out = capture.Stop();
    int files = *reinterpret_cast<int*>(script + 0x1AC);
    bool ok;
    if (files == filesBefore)
        ok = out.lines.size() == 1 && out.pending.empty() &&
             out.lines[0] == std::string(GameText(0x007924CC)) + Text(*reinterpret_cast<mfc::CString*>(script + 0x1EC)) + "\n";
    else
        ok = files > filesBefore;
    ei::verify::Check("RunScriptLine", true, ok, "'%s%s %s%s': files %d -> %d, output %s", prefix,
                      static_cast<const char*>(*text), Joined(args).c_str(), suffix, filesBefore, files,
                      out.Describe().c_str());
    Replay(out);
}
EI_VERIFY(0x005DD800, RunScriptLineVerify, 0, s_unused);

// ---- Streambuf ----------------------------------------------------------------------------------

// 005DC150: ours only. A shown category moves the put area and `c` into the line and resets the
// put area; a hidden one changes nothing. Returns 0.
int __fastcall OverflowVerify(ConsoleStreambuf* self, void*, int c) {
    bool shown = self->filter_ == 0 || (FilterMask() & self->filter_) != 0;
    std::string before = Text(self->Line());
    char* pbase = self->pbase_;
    char* pptr = self->pptr_;
    char* epptr = self->epptr_;
    std::string expected = before;
    if (shown) {
        if (pptr > pbase) expected.append(pbase, pptr - pbase);
        if (c != -1) expected.push_back(static_cast<char>(c));
    }
    int result = self->Overflow(c);
    bool ok = result == 0 && Text(self->Line()) == expected &&
              (shown ? self->pbase_ == self->base_ && self->pptr_ == self->base_ && self->epptr_ == self->ebuf_
                     : self->pbase_ == pbase && self->pptr_ == pptr && self->epptr_ == epptr);
    ei::verify::Check("ConsoleStreambuf::Overflow", true, ok, "filter %X, %d pending, c %d", self->filter_,
                      static_cast<int>(pptr - pbase), c);
    return result;
}
EI_VERIFY(0x005DC150, OverflowVerify, 0, s_unused);

// 005DC1B0: ours only. Returns 0 with the line emptied.
int __fastcall SyncVerify(ConsoleStreambuf* self, void*) {
    int result = self->Sync();
    ei::verify::Check("ConsoleStreambuf::Sync", true, result == 0 && Length(self->Line()) == 0, "filter %X",
                      self->filter_);
    return result;
}
EI_VERIFY(0x005DC1B0, SyncVerify, 0, s_unused);

// 005DC200: ours only (runs at static initialisation, before hooks are installed; the capture
// streambufs above use it). Filter, empty line, vtable and put area over the buffer.
ConsoleStreambuf* __fastcall ConstructVerify(ConsoleStreambuf* self, void*, unsigned filter) {
    ConsoleStreambuf* result = self->Construct(filter);
    bool ok = result == self && self->filter_ == filter && Length(self->Line()) == 0 &&
              self->vtable_ == reinterpret_cast<void**>(kConsoleStreambufVtable) && self->base_ == self->buffer_ &&
              self->ebuf_ == self->buffer_ + sizeof(self->buffer_);
    ei::verify::Check("ConsoleStreambuf::Construct", true, ok, "filter %X", filter);
    return result;
}
EI_VERIFY(0x005DC200, ConstructVerify, 0, s_unused);

// 005DC260 / 005DC240: ours only (at exit, and for the capture streambufs): a console streambuf.
void __fastcall DestructVerify(ConsoleStreambuf* self, void*) {
    bool ok = self->vtable_ == reinterpret_cast<void**>(kConsoleStreambufVtable);
    self->Destruct();
    ei::verify::Check("ConsoleStreambuf::Destruct", true, ok, "%p", self);
}
EI_VERIFY(0x005DC260, DestructVerify, 0, s_unused);

ConsoleStreambuf* __fastcall DeletingDestructVerify(ConsoleStreambuf* self, void*, unsigned flags) {
    bool ok = self->vtable_ == reinterpret_cast<void**>(kConsoleStreambufVtable);
    ConsoleStreambuf* result = self->DeletingDestruct(flags);
    ei::verify::Check("ConsoleStreambuf::DeletingDestruct", true, ok && result == self, "%p", self);
    return result;
}
EI_VERIFY(0x005DC240, DeletingDestructVerify, 0, s_unused);

// ---- Console window -----------------------------------------------------------------------------

struct LinesAt : CStringArray {
    void CopyFrom(const CStringArray& from);   // 006FEB61 CStringArray::Copy
};
void LinesAt::CopyFrom(const CStringArray& a0) { StringArrayCopyOriginal((void*)this, nullptr, &a0); }

std::string WindowText(const ConsoleWindow* w) {
    std::string s = "scroll " + Int(w->scroll_) + " history " + Int(w->history_) + " lines";
    for (int i = 0; i < w->lines_.GetSize(); ++i) s += " [" + Text(w->lines_[i]) + "]";
    return s;
}

// Runs the original on the window, records it, puts the lines and scroll position back, runs ours
// and compares. Both redraw.
void CompareWindow(const char* name, ConsoleWindow* w, const std::function<void()>& original,
                   const std::function<void()>& ours, const char* argument) {
    CStringArray saved;
    static_cast<LinesAt&>(saved).CopyFrom(w->lines_);
    int scroll = w->scroll_;
    int history = w->history_;
    original();
    std::string expected = WindowText(w);
    static_cast<LinesAt&>(w->lines_).CopyFrom(saved);
    w->scroll_ = scroll;
    w->history_ = history;
    ours();
    std::string actual = WindowText(w);
    ei::verify::Check(name, true, expected == actual, "'%s': %s vs %s", argument, expected.c_str(), actual.c_str());
}

// 005E4090: PUSH -1; PUSH imm32 (7).
void(__thiscall* s_addText)(ConsoleWindow*, const char*);
void __fastcall AddTextVerify(ConsoleWindow* self, void*, const char* text) {
    std::string copy = text ? text : "";
    CompareWindow("ConsoleWindow::AddText", self, [&] { s_addText(self, text); }, [&] { self->AddText(text); }, copy.c_str());
}
EI_VERIFY(0x005E4090, AddTextVerify, 7, s_addText);

// 005E3F90: PUSH ESI; MOV ESI,ECX; MOV ECX,[ESP+8] (7).
void(__thiscall* s_setHistory)(ConsoleWindow*, int);
void __fastcall SetHistoryVerify(ConsoleWindow* self, void*, int lines) {
    CompareWindow("ConsoleWindow::SetHistory", self, [&] { s_setHistory(self, lines); }, [&] { self->SetHistory(lines); },
                  Int(lines).c_str());
}
EI_VERIFY(0x005E3F90, SetHistoryVerify, 7, s_setHistory);

// 005E3E90: PUSH ECX; PUSH EBX; PUSH EBP; PUSH ESI; MOV EDX,[ESP+14] (8). Every field it sets is
// derived from the argument and the screen size, so ours simply runs again (a shown window gets
// its surface recreated twice).
void(__thiscall* s_setRect)(ConsoleWindow*, const int*);
void __fastcall SetRectVerify(ConsoleWindow* self, void*, const int* rect) {
    auto fields = [self] {
        std::string s = Int(self->width_) + "x" + Int(self->height_) + " lines " + Int(self->visibleLines_);
        for (int v : self->rect_) s += " " + Int(v);
        for (int v : self->screenRect_) s += " " + Int(v);
        return s;
    };
    int copy[4] = {rect[0], rect[1], rect[2], rect[3]};
    s_setRect(self, copy);
    std::string expected = fields();
    self->SetRect(copy);
    std::string actual = fields();
    ei::verify::Check("ConsoleWindow::SetRect", true, expected == actual, "%d,%d,%d,%d: %s vs %s", rect[0], rect[1],
                      rect[2], rect[3], expected.c_str(), actual.c_str());
}
EI_VERIFY(0x005E3E90, SetRectVerify, 8, s_setRect);

// ---- generateacks (scenario console: `console generateacks`) -----------------------------------

// A stand-in file for the line writers: only slot 21 (WriteString), which collects the text.
struct LineSink {
    void** vtable;
    std::vector<std::string> lines;

    static void __fastcall Write(LineSink* self, void*, const char* text) { self->lines.emplace_back(text); }
    LineSink() {
        static void* slots[22];
        slots[21] = reinterpret_cast<void*>(&Write);
        vtable = slots;
    }
    CStdioFileVirtuals* File() { return reinterpret_cast<CStdioFileVirtuals*>(this); }
};

// 005DFC20 / 005DFCE0: PUSH -1; PUSH imm32 (7). The original and ours each write into a stand-in
// file; the lines are compared and ours' then written to the real file.
template <typename List>
void CompareLines(const char* name, void(__fastcall* original)(CStdioFileVirtuals*, const List*),
                  void(__fastcall* ours)(CStdioFileVirtuals*, const List*), CStdioFileVirtuals* file, const List* list) {
    LineSink expected, actual;
    original(expected.File(), list);
    ours(actual.File(), list);
    ei::verify::Check(name, true, expected.lines == actual.lines, "%u vs %u lines",
                      static_cast<unsigned>(expected.lines.size()), static_cast<unsigned>(actual.lines.size()));
    for (const std::string& line : actual.lines) file->WriteString(line.c_str());
}

void(__fastcall* s_speechLines)(CStdioFileVirtuals*, const acks::SpeechList*);
void __fastcall SpeechLinesVerify(CStdioFileVirtuals* file, const acks::SpeechList* list) {
    CompareLines("WriteSpeechLines", s_speechLines, &commands::WriteSpeechLines, file, list);
}
EI_VERIFY(0x005DFC20, SpeechLinesVerify, 7, s_speechLines);

void(__fastcall* s_environmentLines)(CStdioFileVirtuals*, const stlp::vector<acks::EnvironmentSpeech>*);
void __fastcall EnvironmentLinesVerify(CStdioFileVirtuals* file, const stlp::vector<acks::EnvironmentSpeech>* list) {
    CompareLines("WriteEnvironmentSpeechLines", s_environmentLines, &commands::WriteEnvironmentSpeechLines, file, list);
}
EI_VERIFY(0x005DFCE0, EnvironmentLinesVerify, 7, s_environmentLines);

// 005DFDA0: PUSH -1; PUSH imm32 (7). Side by side: the original writes scenack.txt, the file is
// summarised (length and a hash of its bytes) and emptied, ours writes it again.
std::string AcksFilePath() { return Text(*GameDataPathString()) + GameText(0x00792C00); }
std::string AcksFileSummary() {
    mfc::CFile file;
    if (!file.Open(AcksFilePath().c_str(), mfc::CFile::modeRead, nullptr)) return "no file";
    std::string bytes(file.GetLength(), '\0');
    unsigned read = bytes.empty() ? 0 : file.Read(bytes.data(), static_cast<unsigned>(bytes.size()));
    file.Close();
    std::uint32_t hash = 2166136261u;
    for (unsigned i = 0; i < read; ++i) hash = (hash ^ static_cast<unsigned char>(bytes[i])) * 16777619u;
    return Int(static_cast<int>(read)) + " bytes, hash " + Int(static_cast<int>(hash));
}
Handler s_generateAcks;
void __fastcall GenerateAcksVerify(CStringArray* args) {
    SideBySide("GenerateAcks", s_generateAcks, &commands::GenerateAcks, args, &AcksFileSummary, [] {
        CStdioFile file;
        if (file.Open(AcksFilePath().c_str(), CStdioFile::modeCreate | CStdioFile::modeWrite, nullptr)) file.Close();
    });
}
EI_VERIFY(0x005DFDA0, GenerateAcksVerify, 7, s_generateAcks);

} // namespace
