// The briefing variables of the client (0057FC90, 0057FDE0, 0057FF40): which mercenary speaks a
// briefing (its "#show" lines in the briefing text) and the variable that marks the briefing as
// seen ("b.<speaker>.<briefing>" or "b.elder.<briefing>"). See world.h and docs/notes/world.md.
#include "world.h"
#include "game_strings.h"

#include "console.h"

#include "lib/crt.h"
#include "lib/mfc.h"
#include "lib/stlp.h"

#include "ei/hook.h"
#include "originals_engine.h"
#include "originals_library.h"
#include "originals_script.h"
#include "originals_world.h"

#include <cstring>
#include <new>
#include <bit>

namespace {

const char* Text(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }

constexpr std::uintptr_t kBriefingSection = 0x007906E4;   // "briefing "
constexpr std::uintptr_t kShowTag = 0x007906DC;           // "#show"
constexpr std::uintptr_t kHero = 0x0078B0F4;              // "Hero"
constexpr std::uintptr_t kElderPrefix = 0x007906F0;       // "b.elder."
constexpr std::uintptr_t kVariablePrefix = 0x007906FC;    // "b."
constexpr std::uintptr_t kDot = 0x00783D78;               // "."

// The briefing texts (pointer at 007AF788): 004DEFC0 (RET 4) opens a section by name (0 when
// there is none), 004DF030 (RET 4) finds the next line with a tag, 004DFCB0 (RET 8) splits the
// current line into words.
void* BriefingTexts() { return *reinterpret_cast<void**>(0x007AF788); }
__declspec(naked) int __fastcall TextsFindTag(void*, void*, const char*) EI_ORIGINAL(0x004DF030)
__declspec(naked) int __fastcall TextsReadWords(void*, void*, console::CStringArray*, int) EI_ORIGINAL(0x004DFCB0)

// 005809D0 (RET 4). The flow loads the quests of a zone (not rewritten).
inline void __fastcall FlowLoadQuests(GameFlow*, void*, const char* a2) { FlowLoadQuestsOriginal(a2); }
// 00460BC0 (RET 8). Named float table Set.
void __fastcall VariableSet(void* a0, void*, const char* a2, float a3) { NamedTableSet(a0, nullptr, a2, std::bit_cast<std::uint32_t>(a3)); }
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
                           reinterpret_cast<const unsigned char*>(game::EmptyText())) == 0;
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
    if (missing) return new (out) mfc::CString(game::EmptyText());
    // The first "#show" line whose second word is not "Hero" names the speaker.
    console::CStringArray words;
    while (TextsFindTag(texts, nullptr, Text(kShowTag))) {
        TextsReadWords(texts, nullptr, &words, 1);
        if (words.GetSize() >= 2 &&
            gamecrt::mbsicmp(reinterpret_cast<const unsigned char*>(static_cast<const char*>(words[1])),
                             reinterpret_cast<const unsigned char*>(Text(kHero))) != 0)
            return new (out) mfc::CString(words[1]);
    }
    return new (out) mfc::CString(game::EmptyText());
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
            mfc::CString left = zone.Left(reinterpret_cast<const int*>(static_cast<const char*>(zone))[-2] - 2);
            zone = left;
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
// 004DEDD0 (RET 4). The briefing texts forget a loaded file.
// 0040C720. STLport string destructor.

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
    for (LoadedFile* file = files.start; file != files.finish; ++file) StlStringDestroy(file);
    files.finish = files.start;
}

// ---- Quest variables ------------------------------------------------------------------------------

namespace {

constexpr std::uintptr_t kQuestPrefix = 0x00783D7C;   // "q."
// 005F7FC0 (RET 8). The quest log (007C7D50) hears of a quest variable.
__declspec(naked) void __fastcall QuestLogUpdate(void*, void*, const char*, std::uint32_t) EI_ORIGINAL(0x005F7FC0)
// 00565380 (RET 8). The client world (007AF5B8) hears of a quest variable.
__declspec(naked) void __fastcall ClientQuestUpdate(void*, void*, const char*, std::uint32_t) EI_ORIGINAL(0x00565380)

} // namespace

void GameFlow::OnVariableChanged(const void* event) {
    // The event's record (+8): name at +0x0C, value at +0x18.
    const std::uint8_t* record = *reinterpret_cast<const std::uint8_t* const*>(static_cast<const std::uint8_t*>(event) + 8);
    mfc::CString name(*reinterpret_cast<const char* const*>(record + 0x0C));
    std::uint32_t value = *reinterpret_cast<const std::uint32_t*>(record + 0x18);
    bool quest;
    {
        mfc::CString prefix = name.Left(2);
        quest = gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(static_cast<const char*>(prefix)),
                                reinterpret_cast<const unsigned char*>(Text(kQuestPrefix))) == 0;
    }
    if (quest) {
        QuestLogUpdate(reinterpret_cast<void*>(0x007C7D50), nullptr, name, value);
        ClientQuestUpdate(reinterpret_cast<void*>(0x007AF5B8), nullptr, name, value);
    }
}

// ---- Quest registration, the session's map name ---------------------------------------------------

namespace {

constexpr std::uintptr_t kQuestRegistry = 0x00790784;   // "\quest.reg"
constexpr std::uintptr_t kMapList = 0x00790778;         // "\map.txt"
constexpr std::uintptr_t kQuestSection = 0x00790790;    // "quest "

// 005809D0 (RET 4, AL; patched by Rebirth). The flow loads the quests of a folder.
inline bool __fastcall FlowLoadQuestFolder(GameFlow*, void*, const char* a2) { return FlowLoadQuestsOriginal(a2); }
// 006AE8E0 (RET 4). The quest table (007C87A8) reads the texts' current file (+0x20).
__declspec(naked) void __fastcall QuestTableLoad(void*, void*, void*) EI_ORIGINAL(0x006AE8E0)
// 0045EA20 (RET 4). CMapDatabase::Load (0079BF00).
__declspec(naked) void __fastcall MapDatabaseLoad(void*, void*, const char*) EI_ORIGINAL(0x0045EA20)
// 004DCE90 (RET 0xC). vector<string>::_M_insert_overflow(pos, value, count).
void __fastcall StringVectorInsertOverflow(void* a0, void*, stlp::string* a2, const stlp::string* a3, unsigned a4) { StringVectorInsertOverflowOriginal(a0, nullptr, (void*)a2, (const void*)a3, a4); }
// 004DF8D0 (RET 8). The texts read the current line into a CString.
void __fastcall TextsReadLine(void* a0, void*, mfc::CString* a2, int a3) { TextsReadLineOriginal(a0, nullptr, a2, a3); }

// The quest folders registered so far (007AFDC0: an STLport vector of strings).
struct StringVector {
    stlp::string* start;
    stlp::string* finish;
    stlp::string* end;
};
StringVector& QuestFolders() { return *reinterpret_cast<StringVector*>(0x007AFDC0); }

bool Equal(const stlp::string& s, const char* text) {
    std::size_t length = std::strlen(text);
    return s.size() == length && std::memcmp(s.data(), text, length) == 0;
}

bool ActiveSession(const std::uint8_t* s) { return s && !(*reinterpret_cast<const std::uint32_t*>(s + 4) & 0x80000000u); }

} // namespace

void GameFlow::RegisterQuests(const char* folder) {
    void* texts = BriefingTexts();
    if (!texts) return;
    if (!FlowLoadQuestFolder(this, nullptr, folder)) return;
    StringVector& folders = QuestFolders();
    for (stlp::string* s = folders.start; s != folders.finish; ++s)
        if (Equal(*s, folder)) return;
    bool registered;
    {
        mfc::CString base(folder);
        mfc::CString path = base + Text(kQuestRegistry);
        registered = TextsOpenSection(texts, nullptr, path) != 0;
    }
    if (!registered) return;
    QuestTableLoad(reinterpret_cast<void*>(0x007C87A8), nullptr, *reinterpret_cast<void**>(static_cast<std::uint8_t*>(texts) + 0x20));
    {
        mfc::CString base(folder);
        mfc::CString path = base + Text(kMapList);
        MapDatabaseLoad(reinterpret_cast<void*>(0x0079BF00), nullptr, path);
    }
    // push_back(folder)
    stlp::string name(folder);
    if (folders.finish != folders.end) {
        new (folders.finish) stlp::string(name);
        ++folders.finish;
    } else {
        StringVectorInsertOverflow(&folders, nullptr, folders.finish, &name, 1);
    }
}

mfc::CString* __stdcall SessionMapNameInto(mfc::CString* out) {
    mfc::CString name;
    // The peer's map: the client's +0x25F4, else the server's +0x25A8 (name at +0x0C).
    const std::uint8_t* peer = nullptr;
    auto* client = *reinterpret_cast<std::uint8_t* const*>(0x0079B5D8);
    auto* server = *reinterpret_cast<std::uint8_t* const*>(0x0079B5DC);
    if (ActiveSession(client) && ActiveSession(*reinterpret_cast<std::uint8_t* const*>(client + 0x25F4)))
        peer = *reinterpret_cast<std::uint8_t* const*>(client + 0x25F4);
    else if (ActiveSession(server) && ActiveSession(*reinterpret_cast<std::uint8_t* const*>(server + 0x25A8)))
        peer = *reinterpret_cast<std::uint8_t* const*>(server + 0x25A8);
    if (!peer) return new (out) mfc::CString(game::EmptyText());
    name = *reinterpret_cast<const char* const*>(peer + 0x0C);
    void* texts = BriefingTexts();
    int found;
    {
        mfc::CString prefix(Text(kQuestSection));
        mfc::CString section = prefix + name;
        found = TextsOpenSection(texts, nullptr, section);
    }
    if (found) TextsReadLine(texts, nullptr, &name, 1);
    return new (out) mfc::CString(name);
}

EI_REPLACE_METHOD(0x00580840, GameFlow::OnVariableChanged);
EI_REPLACE_METHOD(0x005810A0, GameFlow::RegisterQuests);
EI_REPLACE(0x005814C0, SessionMapNameInto);
EI_REPLACE(0x0057FC90, BriefingSpeakerInto);
EI_REPLACE_METHOD(0x00580040, GameFlow::ForgetBriefing);
EI_REPLACE(0x00580F10, UnloadQuestTexts);
EI_REPLACE(0x0057FDE0, BriefingVariableInto);
EI_REPLACE_METHOD(0x0057FF40, GameFlow::MarkBriefing);
