// The briefing variables of the client: which mercenary speaks a
// briefing (its "#show" lines in the briefing text) and the variable that marks the briefing as
// seen ("b.<speaker>.<briefing>" or "b.elder.<briefing>"). See world.h and docs/notes/world.md.
#include "object_tables.h"
#include "player_client.h"
#include "ui.h"
#include "party.h"
#include "world.h"
#include "game_strings.h"

#include "buffered_file.h"
#include "lib/static_object.h"
#include "console.h"
#include "item_db_text.h"
#include "net_classes.h"
#include "net_messages.h"   // MapMessage
#include "quest_records.h"
#include "mod_folders.h"
#include "world_map_files.h"

#include "lib/crt.h"
#include "lib/mfc.h"
#include "lib/stlp.h"

#include "ei/hook.h"
#include "game_flow_data.h"
#include "game_data.h"
#include "game_singletons.h"

#include <cstring>
#include <new>
#include <bit>

namespace {



// The briefing texts (pointer at 007AF788): 004DEFC0 (RET 4) opens a section by name (0 when
// there is none), 004DF030 (RET 4) finds the next line with a tag, 004DFCB0 (RET 8) splits the
// current line into words.

bool IsEmpty(const mfc::CString& s) {
    return gamecrt::mbscmp(s.GetString(),
                           game::EmptyText()) == 0;
}

} // namespace

mfc::CString* __stdcall BriefingSpeakerInto(mfc::CString* out, const char* briefing) {
    CTextManager* texts = TheTextManager();
    bool missing;
    {
        mfc::CString prefix(game::briefingwords::BriefingSection());
        mfc::CString section = prefix + briefing;
        missing = texts->CTextManager::Open(section) == 0;
    }
    if (missing) return new (out) mfc::CString(game::EmptyText());
    // The first "#show" line whose second word is not "Hero" names the speaker.
    console::CStringArray words;
    while (texts->FindLine(game::briefingwords::ShowTag())) {
        texts->Split(&words, 1);
        if (words.GetSize() >= 2 &&
            gamecrt::mbsicmp(words[1].GetString(),
                            game::briefingwords::Hero()) != 0)
            return new (out) mfc::CString(words[1]);
    }
    return new (out) mfc::CString(game::EmptyText());
}

mfc::CString* __stdcall BriefingVariableInto(mfc::CString* out, const char* briefing) {
    mfc::CString speaker(mfc::CString::kUnset);   // BriefingSpeakerInto constructs it
    BriefingSpeakerInto(&speaker, briefing);
    if (!IsEmpty(speaker)) {
        mfc::CString prefix(game::briefingwords::VariablePrefix());
        mfc::CString withSpeaker = prefix + speaker;
        mfc::CString withDot = withSpeaker + flowdata::kDot;
        mfc::CString variable = withDot + briefing;
        speaker = variable;
    } else {
        mfc::CString prefix(game::briefingwords::ElderPrefix());
        mfc::CString variable = prefix + briefing;
        speaker = variable;
    }
    new (out) mfc::CString(speaker);
    return out;
}

void GameFlow::MarkBriefing(const char* briefing) {
    CPlayer* player = TheClientWorld()->LocalView()->player_;
    // "<zone>_N": the zone's quests are loaded first.
    std::size_t length = std::strlen(briefing);
    if (static_cast<int>(length) > 2 && briefing[length - 2] == '_') {
        mfc::CString zone(briefing);
        {
            mfc::CString left = zone.Left(zone.GetLength() - 2);
            zone = left;
        }
        LoadZoneQuests(zone);
    }
    mfc::CString variable(mfc::CString::kUnset);   // BriefingVariableInto constructs it
    BriefingVariableInto(&variable, briefing);
    player->Party()->variables_.Set(variable, 1.0f);
}

// ---- Forgetting briefings, unloading quest texts --------------------------------------------------

namespace {

// The briefing texts forget a loaded file.
// STLport string destructor.

using LoadedFiles = stlp::vector<stlp::string>;
LoadedFiles& QuestFiles() { return g_questFiles; }

} // namespace

// The quest text files (world.h) and the quest folders (QuestFolders, below): empty at the start, as the static
// initialisers 00580980 and 00581050 leave them; the exit functions they register destroy them
// (docs/notes/standalone.md, "The static initialisers' objects").
constinit stlp::vector<stlp::string> g_questFiles = /*007AFDD0*/ {};
constinit stlp::vector<stlp::string> g_questFolders = /*007AFDC0*/ {};

namespace {
using QuestStrings = stlp::VectorAbi<stlp::string, stlp::StringElement>;
void __cdecl DestroyQuestFiles() { QuestStrings::Destruct(&g_questFiles); }
void __cdecl DestroyQuestFolders() { QuestStrings::Destruct(&g_questFolders); }
} // namespace

EI_REPLACE(0x005809C0, DestroyQuestFiles);
EI_REPLACE(0x00581090, DestroyQuestFolders);

void GameFlow::ForgetBriefing(const char* briefing) {
    CPlayer* player = TheClientWorld()->LocalView()->player_;
    mfc::CString variable(mfc::CString::kUnset);   // BriefingVariableInto constructs it
    BriefingVariableInto(&variable, briefing);
    player->Party()->variables_.Remove(variable);
}

void UnloadQuestTexts() {
    CTextManager* texts = TheTextManager();
    if (!texts) return;
    LoadedFiles& files = QuestFiles();
    for (stlp::string* file = files.begin(); file != files.end(); ++file) texts->RemoveSource(file->data());
    // The original's inlined erase(begin(), end()) moves nothing and destroys them all.
    files.clear();
}

// ---- The quest file of a zone (005809D0, A-W15) ---------------------------------------------------

namespace {

constexpr const char* kQuestFileSuffix = ".mq";
constexpr const char* kReadBinary = "rb";
// BufferedFile::Open's flags for a file that is only read.
constexpr std::uint32_t kReadOnlyFlags = 2;

// Whether a game server runs (bit 31 of its count word marks one shut down).
bool ServerRuns() {
    const net::ServerSession* server = net::GameServer();
    return server && !(server->flags7_ & CRefCounted::kDetached);
}

// The game server lists the file at `path` under `name` for its clients: the whole file, read
// into a buffer through a buffered file.
void ShareQuestFile(const char* path, const char* name) {
    GrowingBuffer local;
    GameBuffer* data = &local;
    BufferedFile source;   // the original's inlined constructor and destructor
    source.Open(path, kReadBinary, kReadOnlyFlags);
    net::BufferUpdateHigh(&source);
    const std::int32_t size = static_cast<std::int32_t>(source.high - source.begin) + source.base;
    data->Seek(0);
    std::uint8_t* to = data->BeginWrite(size);
    source.ReadBytes(to, static_cast<unsigned>(size));
    data->cur = to + size;
    data->Commit();
    stlp::string listed(name);
    net::GameServer()->ShareFile(&listed, data);
}

// The path of the maps folder's file `file` (LoadZoneQuests's).
stlp::string MapFilePath(const mfc::CString& file) {
    mfc::CString folder;
    modfolders::SelectMaps(file, modfolders::MapsSite::ZoneQuestsPath);
    MapsFolder(&folder);
    mfc::CString full = folder + file;
    return stlp::string(full.GetString());
}

} // namespace

bool __stdcall LoadZoneQuests(const char* zone) {
    auto* texts = TheTextManager();
    if (!texts) return false;
    mfc::CString file = mfc::CString(zone) + kQuestFileSuffix;
    bool found;
    {
        mfc::CFileFind find;
        mfc::CString folder;
        modfolders::SelectMaps(file, modfolders::MapsSite::ZoneQuests);
        MapsFolder(&folder);
        mfc::CString pattern = folder + file.GetString();
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
EI_REWRITTEN(0x005809D0, LoadZoneQuests);

// ---- Quest variables ------------------------------------------------------------------------------

void __stdcall GameFlow::OnVariableChanged(const stlp::list<RefPtr>::Node* node) {
    const net::VariableMessage* record = stlp::CountedRefListOf<net::VariableMessage>::At(node);
    mfc::CString name(record->name_.data());
    const float value = record->value_;
    bool quest;
    {
        mfc::CString prefix = name.Left(2);
        quest = gamecrt::mbscmp(prefix.GetString(),
                                flowdata::kQuestVariablePrefix) == 0;
    }
    if (quest) {
        TheInterface()->OnQuestVariable(name, value);
        TheClientWorld()->OnQuestVariable(name, value);
    }
}

// ---- Quest registration, the session's map name ---------------------------------------------------

namespace {



// The quest folders registered so far.
using StringVector = stlp::vector<stlp::string>;
StringVector& QuestFolders() { return g_questFolders; }

bool Equal(const stlp::string& s, const char* text) {
    std::size_t length = std::strlen(text);
    return s.size() == length && std::memcmp(s.data(), text, length) == 0;
}

// A session or message still in use (not shut down by its owner).
bool Active(const CRefCounted* object) { return object && !(object->flags7_ & CRefCounted::kDetached); }

} // namespace

void GameFlow::RegisterQuests(const char* folder) {
    CTextManager* texts = TheTextManager();
    if (!texts) return;
    if (!LoadZoneQuests(folder)) return;
    StringVector& folders = QuestFolders();
    for (stlp::string* s = folders.begin(); s != folders.end(); ++s)
        if (Equal(*s, folder)) return;
    bool registered;
    {
        mfc::CString base(folder);
        mfc::CString path = base + game::briefingwords::QuestRegistry();
        // (004DEFC0 in the original; the registry is binary, read as it is: CTextManager::OpenData.)
        registered = texts->OpenData(path) != 0;
    }
    if (!registered) return;
    // The quest table reads from the texts' cursor (the section just opened).
    TheQuestDatabase().LoadRegistry(texts->cursor_);   // 006AE8E0 (its result not read)
    {
        mfc::CString base(folder);
        mfc::CString path = base + game::briefingwords::MapList();
        TheMapDatabase()->Load(path);
    }
    // push_back(folder)
    stlp::string name(folder);
    if (folders.Finish() != folders.EndOfStorage()) {
        new (folders.Finish()) stlp::string(name);
        ++folders.Finish();
    } else {
        folders.InsertOverflow<stlp::StringElement>(folders.Finish(), name, 1);
    }
}

mfc::CString* __stdcall SessionMapNameInto(mfc::CString* out) {
    mfc::CString name;
    // The peer's map message: the client's, else the server's (the map's name its text).
    const net::MapMessage* peer = nullptr;
    const net::ClientSession* client = net::GameClient();
    const net::ServerSession* server = net::GameServer();
    if (Active(client) && Active(client->map_))
        peer = client->map_;
    else if (Active(server) && Active(server->map_.Get()))
        peer = server->map_.Get();
    if (!peer) return new (out) mfc::CString(game::EmptyText());
    name = peer->text_.data();
    CTextManager* texts = TheTextManager();
    int found;
    {
        mfc::CString prefix(game::briefingwords::QuestSection());
        mfc::CString section = prefix + name;
        found = texts->CTextManager::Open(section);
    }
    if (found) texts->ReadLine(&name, 1);
    return new (out) mfc::CString(name);
}

EI_REWRITTEN(0x00580840, GameFlow::OnVariableChanged);
EI_REWRITTEN(0x005810A0, GameFlow::RegisterQuests);
EI_REWRITTEN(0x005814C0, SessionMapNameInto);
EI_REWRITTEN(0x0057FC90, BriefingSpeakerInto);
EI_REWRITTEN(0x00580040, GameFlow::ForgetBriefing);
EI_REWRITTEN(0x00580F10, UnloadQuestTexts);
EI_REWRITTEN(0x0057FDE0, BriefingVariableInto);
EI_REWRITTEN(0x0057FF40, GameFlow::MarkBriefing);
