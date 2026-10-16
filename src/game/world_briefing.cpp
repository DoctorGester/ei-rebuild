// The briefing variables of the client (0057FC90, 0057FDE0, 0057FF40): which mercenary speaks a
// briefing (its "#show" lines in the briefing text) and the variable that marks the briefing as
// seen ("b.<speaker>.<briefing>" or "b.elder.<briefing>"). See world.h and docs/notes/world.md.
#include "object_tables.h"
#include "world.h"
#include "game_strings.h"

#include "buffered_file.h"
#include "console.h"
#include "item_db_text.h"
#include "net_classes.h"
#include "mod_folders.h"
#include "world_map_files.h"

#include "lib/crt.h"
#include "lib/mfc.h"
#include "lib/stlp.h"

#include "ei/hook.h"
#include "originals_engine.h"
#include "originals_library.h"
#include "originals_script.h"
#include "originals_world.h"
#include "originals_ui.h"
#include "game_flow_data.h"

#include <cstring>
#include <new>
#include <bit>

namespace {



// The briefing texts (pointer at 007AF788): 004DEFC0 (RET 4) opens a section by name (0 when
// there is none), 004DF030 (RET 4) finds the next line with a tag, 004DFCB0 (RET 8) splits the
// current line into words.
void* BriefingTexts() { return *reinterpret_cast<void**>(0x007AF788); }

// 005809D0 (RET 4). The flow loads the quests of a zone (not rewritten).
inline void __fastcall FlowLoadQuests(GameFlow*, void*, const char* a2) { FlowLoadQuestsOriginal(a2); }
// 00460BC0 (RET 8). Named float table Set.
void __fastcall VariableSet(void* a0, void*, const char* a2, float a3) { NamedTableSet(a0, nullptr, a2, std::bit_cast<std::uint32_t>(a3)); }
// Rewritten here, through their addresses.
__declspec(naked) mfc::CString* __stdcall BriefingSpeakerAt(void* /*out*/, const char*) EI_ORIGINAL(0x0057FC90)

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
        mfc::CString prefix(game::briefingwords::BriefingSection());
        mfc::CString section = prefix + briefing;
        missing = TextsOpenSection(texts, nullptr, section) == 0;
    }
    if (missing) return new (out) mfc::CString(game::EmptyText());
    // The first "#show" line whose second word is not "Hero" names the speaker.
    console::CStringArray words;
    while (TextFindLineOriginal(texts, nullptr, game::briefingwords::ShowTag())) {
        static_cast<CTextManager*>(texts)->Split(&words, 1);
        if (words.GetSize() >= 2 &&
            libcrt::Mbsicmp(reinterpret_cast<const unsigned char*>(static_cast<const char*>(words[1])),
                            reinterpret_cast<const unsigned char*>(game::briefingwords::Hero())) != 0)
            return new (out) mfc::CString(words[1]);
    }
    return new (out) mfc::CString(game::EmptyText());
}

mfc::CString* __stdcall BriefingVariableInto(mfc::CString* out, const char* briefing) {
    StringSlot speaker;
    BriefingSpeakerAt(speaker.Raw(), briefing);
    if (!IsEmpty(speaker.Get())) {
        mfc::CString prefix(game::briefingwords::VariablePrefix());
        mfc::CString withSpeaker = prefix + speaker.Get();
        mfc::CString withDot = withSpeaker + game::Text(flowdata::kDot);
        mfc::CString variable = withDot + briefing;
        speaker.Get() = variable;
    } else {
        mfc::CString prefix(game::briefingwords::ElderPrefix());
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
    BriefingVariableInto(reinterpret_cast<mfc::CString*>(variable.Raw()), briefing);
    VariableSet(player->variables, nullptr, variable.Get(), 1.0f);
    variable.Get().~CString();
}

// ---- Forgetting briefings, unloading quest texts --------------------------------------------------

namespace {

// 00460DB0 (RET 4). Named float table Remove.
// 004DEDD0 (RET 4). The briefing texts forget a loaded file.
// 0040C720. STLport string destructor.

// The quest text files loaded so far (007AFDD0: an STLport vector of strings, the files' paths).
using LoadedFiles = stlp::vector<stlp::string>;
LoadedFiles& QuestFiles() { return *reinterpret_cast<LoadedFiles*>(0x007AFDD0); }

} // namespace

void GameFlow::ForgetBriefing(const char* briefing) {
    auto* player = *reinterpret_cast<WorldPlayer**>(WorldClient()->LocalView() + 0x1C);
    StringSlot variable;
    BriefingVariableInto(reinterpret_cast<mfc::CString*>(variable.Raw()), briefing);
    VariableRemove(player->variables, nullptr, variable.Get());
    variable.Get().~CString();
}

void UnloadQuestTexts() {
    void* texts = BriefingTexts();
    if (!texts) return;
    LoadedFiles& files = QuestFiles();
    for (stlp::string* file = files.begin(); file != files.end(); ++file) static_cast<CTextManager*>(texts)->RemoveSource(file->data());
    // The original's inlined erase(begin(), end()) moves nothing and destroys them all.
    files.clear();
}

// ---- The quest file of a zone (005809D0, A-W15) ---------------------------------------------------

namespace {

constexpr std::uintptr_t kQuestFileSuffix = std::uintptr_t(0x00790774);   // ".mq"
constexpr std::uintptr_t kReadBinary = std::uintptr_t(0x0078E998);        // "rb"
// BufferedFile::Open's flags for a file that is only read.
constexpr std::uint32_t kReadOnlyFlags = 2;

// Whether a game server runs (bit 31 of its count word marks one shut down).
bool ServerRuns() {
    const net::ServerSession* server = net::GameServer();
    return server && !(server->flags7_ & CRefCounted::kDetached);
}

// A buffered file on the stack, as the original's inlined constructor and destructor build it.
struct LocalFile {
    LocalFile() {
        file.Construct();
        ei::SetVtable(&file, BufferedFile::kFileVtableAddress);
        file.file = nullptr;
    }
    ~LocalFile() { file.DestructFile(); }
    BufferedFile file;
};

// A growing buffer on the stack. Raw storage: the game's constructor makes the object, and its
// slots are called through the vtable that constructor stores.
struct LocalBuffer {
    LocalBuffer() { Get()->ConstructGrowing(); }
    ~LocalBuffer() { Get()->DestructGrowing(); }
    GameBuffer* Get() { return reinterpret_cast<GameBuffer*>(storage); }
    alignas(GameBuffer) std::uint8_t storage[sizeof(GameBuffer)];
};

// The game server lists the file at `path` under `name` for its clients: the whole file, read
// into a buffer through a buffered file.
void ShareQuestFile(const char* path, const char* name) {
    LocalBuffer local;
    GameBuffer* data = local.Get();
    LocalFile source;
    source.file.Open(path, game::Text(kReadBinary), kReadOnlyFlags);
    net::BufferUpdateHigh(&source.file);
    const std::int32_t size = static_cast<std::int32_t>(source.file.high - source.file.begin) + source.file.base;
    data->Seek(0);
    std::uint8_t* to = data->BeginWrite(size);
    source.file.ReadBytes(to, static_cast<unsigned>(size));
    data->cur = to + size;
    data->Commit();
    stlp::string listed(name);
    net::GameServer()->ShareFile(reinterpret_cast<const net::StringRep*>(&listed), data);
}

// The path of the maps folder's file `file` (LoadZoneQuests's).
stlp::string MapFilePath(const mfc::CString& file) {
    mfc::CString folder;
    modfolders::SelectMaps(file, modfolders::MapsSite::ZoneQuestsPath);
    MapsFolderAt(&folder);
    mfc::CString full = folder + file;
    return stlp::string(static_cast<const char*>(full));
}

} // namespace

bool __stdcall LoadZoneQuests(const char* zone) {
    auto* texts = static_cast<CTextManager*>(BriefingTexts());
    if (!texts) return false;
    mfc::CString file = mfc::CString(zone) + game::Text(kQuestFileSuffix);
    bool found;
    {
        mfc::CFileFind find;
        mfc::CString folder;
        modfolders::SelectMaps(file, modfolders::MapsSite::ZoneQuests);
        MapsFolderAt(&folder);
        mfc::CString pattern = folder + static_cast<const char*>(file);
        found = find.FindFile(pattern, 0) != 0;
    }
    if (!found) return false;
    stlp::string path = MapFilePath(file);
    if (ServerRuns()) ShareQuestFile(path.data(), file);
    // A file already loaded is not loaded again.
    LoadedFiles& files = QuestFiles();
    stlp::string* listed = files.begin();
    while (listed != files.end() && !(*listed == path)) ++listed;
    if (listed == files.end()) {
        files.push_back(path);
        texts->AddSource(path.data());
    }
    return true;
}
EI_REPLACE(0x005809D0, LoadZoneQuests);

// ---- Quest variables ------------------------------------------------------------------------------

namespace {


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
                                reinterpret_cast<const unsigned char*>(game::Text(flowdata::kQuestVariablePrefix))) == 0;
    }
    if (quest) {
        QuestLogUpdateOriginal(reinterpret_cast<void*>(0x007C7D50), nullptr, name, value);
        ClientQuestUpdateOriginal(reinterpret_cast<void*>(0x007AF5B8), nullptr, name, value);
    }
}

// ---- Quest registration, the session's map name ---------------------------------------------------

namespace {


// 005809D0 (RET 4, AL; patched by Rebirth). The flow loads the quests of a folder.
inline bool __fastcall FlowLoadQuestFolder(GameFlow*, void*, const char* a2) { return FlowLoadQuestsOriginal(a2); }
// 006AE8E0 (RET 4). The quest table (007C87A8) reads the texts' current file (+0x20).
__declspec(naked) void __fastcall QuestTableLoad(void*, void*, void*) EI_ORIGINAL(0x006AE8E0)
// 004DF8D0 (RET 8). The texts read the current line into a CString.
void __fastcall TextsReadLine(void* a0, void*, mfc::CString* a2, int a3) { static_cast<CTextManager*>(a0)->ReadLine(a2, a3); }

// The quest folders registered so far (007AFDC0: an STLport vector of strings).
using StringVector = stlp::vector<stlp::string>;
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
    for (stlp::string* s = folders.begin(); s != folders.end(); ++s)
        if (Equal(*s, folder)) return;
    bool registered;
    {
        mfc::CString base(folder);
        mfc::CString path = base + game::briefingwords::QuestRegistry();
        registered = TextsOpenSection(texts, nullptr, path) != 0;
    }
    if (!registered) return;
    // The quest table reads from the texts' cursor (the section just opened).
    QuestTableLoad(reinterpret_cast<void*>(0x007C87A8), nullptr,
                   const_cast<char*>(static_cast<CTextManager*>(texts)->cursor_));
    {
        mfc::CString base(folder);
        mfc::CString path = base + game::briefingwords::MapList();
        static_cast<CMapDatabase*>(reinterpret_cast<void*>(0x0079BF00))->Load(path);
    }
    // push_back(folder)
    stlp::string name(folder);
    if (folders.Finish() != folders.EndOfStorage()) {
        new (folders.Finish()) stlp::string(name);
        ++folders.Finish();
    } else {
        StringVectorInsertOverflowOriginal(&folders, nullptr, folders.Finish(), &name, 1);
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
        mfc::CString prefix(game::briefingwords::QuestSection());
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
