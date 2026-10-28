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

#include "game_flow_data.h"
#include "game_data.h"
#include "game_singletons.h"
#include "lib/str.h"

#include <cstring>
#include <memory>
#include <new>
#include <bit>
#include <string>

namespace {



bool IsEmpty(const std::string& s) {
    return gamecrt::mbscmp(s,
                           game::EmptyText()) == 0;
}

} // namespace

mfc::CString* __stdcall BriefingSpeakerInto(mfc::CString* out, const char* briefing) {
    CTextManager* texts = TheTextManager();
    bool missing;
    {
        std::string prefix = game::briefingwords::BriefingSection();
        std::string section = prefix + briefing;
        missing = texts->CTextManager::Open(section.c_str()) == 0;
    }
    if (missing) return std::construct_at(out, game::EmptyText());
    // The first "#show" line whose second word is not "Hero" names the speaker.
    std::vector<std::string> words;
    while (texts->FindLine(game::briefingwords::ShowTag())) {
        texts->Split(&words, 1);
        if (words.size() >= 2 &&
            gamecrt::mbsicmp(words[1].c_str(),
                            game::briefingwords::Hero()) != 0)
            return std::construct_at(out, words[1]);
    }
    return std::construct_at(out, game::EmptyText());
}

mfc::CString* __stdcall BriefingVariableInto(mfc::CString* out, const char* briefing) {
    mfc::CString speaker(mfc::CString::kUnset);   // BriefingSpeakerInto constructs it
    BriefingSpeakerInto(&speaker, briefing);
    if (!IsEmpty(speaker.GetString())) {
        std::string prefix = game::briefingwords::VariablePrefix();
        std::string withSpeaker = prefix + speaker;
        std::string withDot = withSpeaker + flowdata::kDot;
        mfc::CString variable = withDot + briefing;
        speaker = variable;
    } else {
        std::string prefix = game::briefingwords::ElderPrefix();
        mfc::CString variable = prefix + briefing;
        speaker = variable;
    }
    std::construct_at(out, speaker);
    return out;
}

void GameFlow::MarkBriefing(const char* briefing) {
    CPlayer* player = TheClientWorld()->LocalView()->player_;
    // "<zone>_N": the zone's quests are loaded first.
    std::size_t length = std::strlen(briefing);
    if (static_cast<int>(length) > 2 && briefing[length - 2] == '_') {
        std::string zone = briefing;
        {
            std::string left = str::Left(zone, static_cast<int>(zone.size()) - 2);
            zone = left;
        }
        LoadZoneQuests(zone.c_str());
    }
    mfc::CString variable(mfc::CString::kUnset);   // BriefingVariableInto constructs it
    BriefingVariableInto(&variable, briefing);
    player->Party()->variables_.Set(variable, 1.0f);
}

// ---- Forgetting briefings, unloading quest texts --------------------------------------------------

namespace {

using LoadedFiles = stlp::vector<std::string>;
LoadedFiles& QuestFiles() { return g_questFiles; }

} // namespace

// The quest text files (world.h) and the quest folders (QuestFolders, below): empty at the start, as the static
// initialisers leave them; the exit functions they register destroy them
// (docs/notes/standalone.md, "The static initialisers' objects").
constinit stlp::vector<std::string> g_questFiles = /*007AFDD0*/ {};
constinit stlp::vector<std::string> g_questFolders = /*007AFDC0*/ {};

namespace {
using QuestStrings = stlp::VectorAbi<std::string, stlp::ObjectElement<std::string>>;
} // namespace

void __cdecl DestroyQuestFiles() { QuestStrings::Destruct(&g_questFiles); }
void __cdecl DestroyQuestFolders() { QuestStrings::Destruct(&g_questFolders); }

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
    for (std::string* file = files.begin(); file != files.end(); ++file) texts->RemoveSource(file->data());
    // erase(begin(), end()) moves nothing and destroys them all.
    files.clear();
}

// ---- The quest file of a zone ---------------------------------------------------

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
    BufferedFile source;   // constructed and destroyed in place
    source.Open(path, kReadBinary, kReadOnlyFlags);
    net::BufferUpdateHigh(&source);
    const std::int32_t size = static_cast<std::int32_t>(source.high - source.begin) + source.base;
    data->Seek(0);
    std::uint8_t* to = data->BeginWrite(size);
    source.ReadBytes(to, static_cast<unsigned>(size));
    data->cur = to + size;
    data->Commit();
    std::string listed(name);
    net::GameServer()->ShareFile(&listed, data);
}

// The path of the maps folder's file `file` (LoadZoneQuests's).
std::string MapFilePath(const std::string& file) {
    mfc::CString folder;
    modfolders::SelectMaps(file.c_str(), modfolders::MapsSite::ZoneQuestsPath);
    MapsFolder(&folder);
    std::string full = (folder + file).GetString();
    return std::string(full.c_str());
}

} // namespace

bool __stdcall LoadZoneQuests(const char* zone) {
    auto* texts = TheTextManager();
    if (!texts) return false;
    std::string file = (mfc::CString(zone) + kQuestFileSuffix).GetString();
    bool found;
    {
        mfc::CFileFind find;
        mfc::CString folder;
        modfolders::SelectMaps(file.c_str(), modfolders::MapsSite::ZoneQuests);
        MapsFolder(&folder);
        std::string pattern = folder.GetString() + file;
        found = find.FindFile(pattern, 0) != 0;
    }
    if (!found) return false;
    std::string path = MapFilePath(file);
    if (ServerRuns()) ShareQuestFile(path.data(), file.c_str());
    // A file already loaded is not loaded again.
    LoadedFiles& files = QuestFiles();
    std::string* listed = files.begin();
    while (listed != files.end() && !(*listed == path)) ++listed;
    if (listed == files.end()) {
        files.push_back(path);
        texts->AddSource(path.data());
    }
    return true;
}

// ---- Quest variables ------------------------------------------------------------------------------

void __stdcall GameFlow::OnVariableChanged(const stlp::list<RefPtr>::Node* node) {
    const net::VariableMessage* record = stlp::CountedRefListOf<net::VariableMessage>::At(node);
    std::string name = record->name_.data();
    const float value = record->value_;
    bool quest;
    {
        std::string prefix = str::Left(name, 2);
        quest = gamecrt::mbscmp(prefix,
                                flowdata::kQuestVariablePrefix) == 0;
    }
    if (quest) {
        TheInterface()->OnQuestVariable(name.c_str(), value);
        TheClientWorld()->OnQuestVariable(name.c_str(), value);
    }
}

// ---- Quest registration, the session's map name ---------------------------------------------------

namespace {



// The quest folders registered so far.
using StringVector = stlp::vector<std::string>;
StringVector& QuestFolders() { return g_questFolders; }

bool Equal(const std::string& s, const char* text) {
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
    for (std::string* s = folders.begin(); s != folders.end(); ++s)
        if (Equal(*s, folder)) return;
    bool registered;
    {
        std::string base = folder;
        std::string path = base + game::briefingwords::QuestRegistry();
        // (The registry is binary, read as it is: CTextManager::OpenData.)
        registered = texts->OpenData(path.c_str()) != 0;
    }
    if (!registered) return;
    // The quest table reads from the texts' cursor (the section just opened).
    TheQuestDatabase().LoadRegistry(texts->cursor_);   // (its result is not read)
    {
        std::string base = folder;
        std::string path = base + game::briefingwords::MapList();
        TheMapDatabase()->Load(path.c_str());
    }
    std::string name(folder);
    if (folders.Finish() != folders.EndOfStorage()) {
        new (folders.Finish()) std::string(name);
        ++folders.Finish();
    } else {
        folders.InsertOverflow<stlp::ObjectElement<std::string>>(folders.Finish(), name, 1);
    }
}

mfc::CString* __stdcall SessionMapNameInto(mfc::CString* out) {
    std::string name;
    // The peer's map message: the client's, else the server's (the map's name its text).
    const net::MapMessage* peer = nullptr;
    const net::ClientSession* client = net::GameClient();
    const net::ServerSession* server = net::GameServer();
    if (Active(client) && Active(client->map_))
        peer = client->map_;
    else if (Active(server) && Active(server->map_.Get()))
        peer = server->map_.Get();
    if (!peer) return std::construct_at(out, game::EmptyText());
    name = peer->text_.data();
    CTextManager* texts = TheTextManager();
    int found;
    {
        std::string prefix = game::briefingwords::QuestSection();
        std::string section = prefix + name;
        found = texts->CTextManager::Open(section.c_str());
    }
    if (found) texts->ReadLine(&name, 1);
    return std::construct_at(out, name);
}
