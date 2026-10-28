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
#include "lib/file_io.h"
#include "lib/containers.h"
#include "console.h"
#include "item_db_text.h"
#include "lib/text_case.h"
#include "net_classes.h"
#include "net_messages.h"   // MapMessage
#include "quest_records.h"
#include "mod_folders.h"
#include "world_map_files.h"

#include "lib/mfc.h"

#include "game_flow_data.h"
#include "game_data.h"
#include "game_singletons.h"
#include "lib/str.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <new>
#include <bit>
#include <string>
#include <vector>

namespace {



bool IsEmpty(const std::string& s) {
    return str::Compare(s,
                           game::EmptyText()) == 0;
}

} // namespace

std::string* BriefingSpeakerInto(std::string* out, const char* briefing) {
    CTextManager* texts = TheTextManager();
    bool missing;
    {
        std::string prefix = game::briefingwords::BriefingSection();
        std::string section = prefix + briefing;
        missing = texts->CTextManager::Open(section.c_str()) == 0;
    }
    if (missing) {
        *out = game::EmptyText();
        return out;
    }
    // The first "#show" line whose second word is not "Hero" names the speaker.
    std::vector<std::string> words;
    while (texts->FindLine(game::briefingwords::ShowTag())) {
        texts->Split(&words, 1);
        if (words.size() >= 2 &&
            text::CompareIgnoringCase(words[1].c_str(),
                            game::briefingwords::Hero()) != 0) {
            *out = words[1];
            return out;
        }
    }
    *out = game::EmptyText();
    return out;
}

std::string* BriefingVariableInto(std::string* out, const char* briefing) {
    std::string speaker;
    BriefingSpeakerInto(&speaker, briefing);
    if (!IsEmpty(speaker.c_str())) {
        std::string prefix = game::briefingwords::VariablePrefix();
        std::string withSpeaker = prefix + speaker;
        std::string withDot = withSpeaker + flowdata::kDot;
        std::string variable = withDot + briefing;
        speaker = variable;
    } else {
        std::string prefix = game::briefingwords::ElderPrefix();
        std::string variable = prefix + briefing;
        speaker = variable;
    }
    *out = speaker;
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
    std::string variable;
    BriefingVariableInto(&variable, briefing);
    player->Party()->variables_.Set(variable.c_str(), 1.0f);
}

// ---- Forgetting briefings, unloading quest texts --------------------------------------------------

namespace {

using LoadedFiles = std::vector<std::string>;
LoadedFiles& QuestFiles() { return g_questFiles; }

} // namespace

// The quest text files (world.h) and the quest folders (QuestFolders, below): empty at the start, as the static
// initialisers leave them; the exit functions they register destroy them
// (docs/notes/standalone.md, "The static initialisers' objects").
constinit std::vector<std::string> g_questFiles = /*007AFDD0*/ {};
constinit std::vector<std::string> g_questFolders = /*007AFDC0*/ {};

void DestroyQuestFiles() { lib::FreeStorage(g_questFiles); }
void DestroyQuestFolders() { lib::FreeStorage(g_questFolders); }

void GameFlow::ForgetBriefing(const char* briefing) {
    CPlayer* player = TheClientWorld()->LocalView()->player_;
    std::string variable;
    BriefingVariableInto(&variable, briefing);
    player->Party()->variables_.Remove(variable.c_str());
}

void UnloadQuestTexts() {
    CTextManager* texts = TheTextManager();
    if (!texts) return;
    LoadedFiles& files = QuestFiles();
    for (std::string* file = files.data(); file != files.data() + files.size(); ++file) texts->RemoveSource(file->data());
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
    std::string folder;
    modfolders::SelectMaps(file.c_str(), modfolders::MapsSite::ZoneQuestsPath);
    MapsFolder(&folder);
    std::string full = folder + file;
    return std::string(full.c_str());
}

} // namespace

bool LoadZoneQuests(const char* zone) {
    auto* texts = TheTextManager();
    if (!texts) return false;
    std::string file = std::string(zone) + kQuestFileSuffix;
    bool found;
    {
        lib::FileFind find;
        std::string folder;
        modfolders::SelectMaps(file.c_str(), modfolders::MapsSite::ZoneQuests);
        MapsFolder(&folder);
        std::string pattern = folder.c_str() + file;
        found = find.FindFile(pattern) != 0;
    }
    if (!found) return false;
    std::string path = MapFilePath(file);
    if (ServerRuns()) ShareQuestFile(path.data(), file.c_str());
    // A file already loaded is not loaded again.
    LoadedFiles& files = QuestFiles();
    if (std::find(files.begin(), files.end(), path) == files.end()) {
        files.push_back(path);
        texts->AddSource(path.data());
    }
    return true;
}

// ---- Quest variables ------------------------------------------------------------------------------

void GameFlow::OnVariableChanged(const net::VariableMessage* record) {
    std::string name = record->name_.data();
    const float value = record->value_;
    bool quest;
    {
        std::string prefix = str::Left(name, 2);
        quest = str::Compare(prefix,
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
using StringVector = std::vector<std::string>;
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
    for (std::string* s = folders.data(); s != folders.data() + folders.size(); ++s)
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
    folders.push_back(name);
}

std::string* SessionMapNameInto(std::string* out) {
    std::string name;
    // The peer's map message: the client's, else the server's (the map's name its text).
    const net::MapMessage* peer = nullptr;
    const net::ClientSession* client = net::GameClient();
    const net::ServerSession* server = net::GameServer();
    if (Active(client) && Active(client->map_))
        peer = client->map_;
    else if (Active(server) && Active(server->map_.Get()))
        peer = server->map_.Get();
    if (!peer) {
        *out = game::EmptyText();
        return out;
    }
    name = peer->text_.data();
    CTextManager* texts = TheTextManager();
    int found;
    {
        std::string prefix = game::briefingwords::QuestSection();
        std::string section = prefix + name;
        found = texts->CTextManager::Open(section.c_str());
    }
    if (found) texts->ReadLine(&name, 1);
    *out = name; return out;
}
