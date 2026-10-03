// The briefing variables of the client (0057FC90, 0057FDE0, 0057FF40): which mercenary speaks a
// briefing (its "#show" lines in the briefing text) and the variable that marks the briefing as
// seen ("b.<speaker>.<briefing>" or "b.elder.<briefing>"). See world.h and docs/notes/world.md
// (seventh round).
#include "world.h"

#include "console.h"

#include "lib/crt.h"
#include "lib/mfc.h"

#include "ei/hook.h"

#include <cstring>
#include <new>

namespace {

const char* Text(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }

constexpr std::uintptr_t kBriefingSection = 0x007906E4;   // "briefing "
constexpr std::uintptr_t kShowTag = 0x007906DC;           // "#show"
constexpr std::uintptr_t kHero = 0x0078B0F4;              // "Hero"
constexpr std::uintptr_t kElderPrefix = 0x007906F0;       // "b.elder."
constexpr std::uintptr_t kVariablePrefix = 0x007906FC;    // "b."
constexpr std::uintptr_t kDot = 0x00783D78;               // "."
constexpr std::uintptr_t kEmpty = 0x0079B298;             // ""

// The briefing texts (pointer at 007AF788): 004DEFC0 (RET 4) opens a section by name (0 when
// there is none), 004DF030 (RET 4) finds the next line with a tag, 004DFCB0 (RET 8) splits the
// current line into words.
void* BriefingTexts() { return *reinterpret_cast<void**>(0x007AF788); }
__declspec(naked) int __fastcall TextsOpenSection(void*, void*, const char*) EI_ORIGINAL(0x004DEFC0)
__declspec(naked) int __fastcall TextsFindTag(void*, void*, const char*) EI_ORIGINAL(0x004DF030)
__declspec(naked) void __fastcall TextsReadWords(void*, void*, console::CStringArray*, int) EI_ORIGINAL(0x004DFCB0)

// 006FDAE9 (RET 8). CString::Left(count), returned into `out`.
__declspec(naked) mfc::CString* __fastcall StringLeft(const mfc::CString*, void*, void* /*out*/, int) EI_ORIGINAL(0x006FDAE9)
// 005809D0 (RET 4). The flow loads the quests of a zone (not rewritten).
__declspec(naked) void __fastcall FlowLoadQuests(GameFlow*, void*, const char*) EI_ORIGINAL(0x005809D0)
// 00460BC0 (RET 8). Named float table Set.
__declspec(naked) void __fastcall VariableSet(void*, void*, const char*, float) EI_ORIGINAL(0x00460BC0)
// Rewritten here, through their addresses.
__declspec(naked) mfc::CString* __stdcall BriefingSpeakerAt(void* /*out*/, const char*) EI_ORIGINAL(0x0057FC90)
__declspec(naked) mfc::CString* __stdcall BriefingVariableAt(void* /*out*/, const char*) EI_ORIGINAL(0x0057FDE0)

// A CString constructed by a callee into raw storage, destroyed explicitly.
struct StringSlot {
    alignas(mfc::CString) std::uint8_t storage[sizeof(mfc::CString)];
    void* Raw() { return storage; }
    mfc::CString& Get() { return *reinterpret_cast<mfc::CString*>(storage); }
};

bool IsEmpty(const mfc::CString& s) {
    return gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(static_cast<const char*>(s)),
                           reinterpret_cast<const unsigned char*>(Text(kEmpty))) == 0;
}

} // namespace

mfc::CString* __stdcall BriefingSpeakerInto(mfc::CString* out, const char* briefing) {
    void* texts = BriefingTexts();
    bool missing;
    {
        mfc::CString prefix(Text(kBriefingSection));
        mfc::CString section = prefix + briefing;
        missing = TextsOpenSection(texts, nullptr, section) == 0;
    }
    if (missing) return new (out) mfc::CString(Text(kEmpty));
    // The first "#show" line whose second word is not "Hero" names the speaker.
    console::CStringArray words;
    while (TextsFindTag(texts, nullptr, Text(kShowTag))) {
        TextsReadWords(texts, nullptr, &words, 1);
        if (words.GetSize() >= 2 &&
            gamecrt::mbsicmp(reinterpret_cast<const unsigned char*>(static_cast<const char*>(words[1])),
                             reinterpret_cast<const unsigned char*>(Text(kHero))) != 0)
            return new (out) mfc::CString(words[1]);
    }
    return new (out) mfc::CString(Text(kEmpty));
}

mfc::CString* __stdcall BriefingVariableInto(mfc::CString* out, const char* briefing) {
    StringSlot speaker;
    BriefingSpeakerAt(speaker.Raw(), briefing);
    if (!IsEmpty(speaker.Get())) {
        mfc::CString prefix(Text(kVariablePrefix));
        mfc::CString withSpeaker = prefix + speaker.Get();
        mfc::CString withDot = withSpeaker + Text(kDot);
        mfc::CString variable = withDot + briefing;
        speaker.Get() = variable;
    } else {
        mfc::CString prefix(Text(kElderPrefix));
        mfc::CString variable = prefix + briefing;
        speaker.Get() = variable;
    }
    new (out) mfc::CString(speaker.Get());
    speaker.Get().~CString();
    return out;
}

void GameFlow::MarkBriefing(const char* briefing) {
    auto* player = *reinterpret_cast<WorldPlayer**>(WorldClient()->LocalView() + 0x1C);
    // "<zone>_N": the zone's quests are loaded first.
    std::size_t length = std::strlen(briefing);
    if (static_cast<int>(length) > 2 && briefing[length - 2] == '_') {
        mfc::CString zone(briefing);
        {
            StringSlot left;
            StringLeft(&zone, nullptr, left.Raw(), reinterpret_cast<const int*>(static_cast<const char*>(zone))[-2] - 2);
            zone = left.Get();
            left.Get().~CString();
        }
        FlowLoadQuests(this, nullptr, zone);
    }
    StringSlot variable;
    BriefingVariableAt(variable.Raw(), briefing);
    VariableSet(player->variables, nullptr, variable.Get(), 1.0f);
    variable.Get().~CString();
}

// ---- Forgetting briefings, unloading quest texts --------------------------------------------------

namespace {

// 00460DB0 (RET 4). Named float table Remove.
__declspec(naked) void __fastcall VariableRemove(void*, void*, const char*) EI_ORIGINAL(0x00460DB0)
// 004DEDD0 (RET 4). The briefing texts forget a loaded file.
__declspec(naked) void __fastcall TextsUnload(void*, void*, const char*) EI_ORIGINAL(0x004DEDD0)
// 0040C720. STLport string destructor.
__declspec(naked) void __fastcall RawStringDestroy(void*, void*) EI_ORIGINAL(0x0040C720)

// The quest text files loaded so far (007AFDD0: an STLport vector of 12-byte strings).
struct LoadedFile {
    const char* start;
    const char* finish;
    const char* end;
};
struct LoadedFiles {
    LoadedFile* start;
    LoadedFile* finish;
    LoadedFile* end;
};
LoadedFiles& QuestFiles() { return *reinterpret_cast<LoadedFiles*>(0x007AFDD0); }

} // namespace

void GameFlow::ForgetBriefing(const char* briefing) {
    auto* player = *reinterpret_cast<WorldPlayer**>(WorldClient()->LocalView() + 0x1C);
    StringSlot variable;
    BriefingVariableAt(variable.Raw(), briefing);
    VariableRemove(player->variables, nullptr, variable.Get());
    variable.Get().~CString();
}

void UnloadQuestTexts() {
    void* texts = BriefingTexts();
    if (!texts) return;
    LoadedFiles& files = QuestFiles();
    for (LoadedFile* file = files.start; file != files.finish; ++file) TextsUnload(texts, nullptr, file->start);
    // clear(): the original's inlined erase(begin(), end()) moves nothing and destroys them all.
    for (LoadedFile* file = files.start; file != files.finish; ++file) RawStringDestroy(file, nullptr);
    files.finish = files.start;
}

EI_REPLACE(0x0057FC90, BriefingSpeakerInto);
EI_REPLACE_METHOD(0x00580040, GameFlow::ForgetBriefing);
EI_REPLACE(0x00580F10, UnloadQuestTexts);
EI_REPLACE(0x0057FDE0, BriefingVariableInto);
EI_REPLACE_METHOD(0x0057FF40, GameFlow::MarkBriefing);
