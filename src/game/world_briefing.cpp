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

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include "game_flow_data.h"
#include "game_singletons.h"
#include "lib/str.h"

#include <algorithm>
#include <memory>
#include <new>
#include <bit>
#include <vector>

namespace {



bool IsEmpty(const std::string& s) {
    return str::Compare(s,
                           game::kEmptyText) == 0;
}


constexpr const char* kQuestFileSuffix = ".mq";
constexpr const char* kReadBinary = "rb";
// BufferedFile::Open's flags for a file that is only read.
constexpr std::uint32_t kReadOnlyFlags = 2;

// Whether a game server runs (bit 31 of its count word marks one shut down).
bool ServerRuns() {
    const net::ServerSession* server = net::g_gameServer;
    return server && !(server->flags7_ & CRefCounted::kDetached);
}

// The game server lists the file at `path` under `name` for its clients: the whole file, read
// into a buffer through a buffered file.
void ShareQuestFile(const std::string& path, const std::string& name) {
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
    std::string listed = name;
    net::g_gameServer->ShareFile(&listed, data);
}

// The path of the maps folder's file `file` (LoadZoneQuests's).
std::string MapFilePath(const std::string& file) {
    std::string folder;
    modfolders::SelectMaps(file, modfolders::MapsSite::ZoneQuestsPath);
    MapsFolder(&folder);
    std::string full = folder + file;
    return std::string(full);
}

} // namespace

std::string* BriefingSpeakerInto(std::string* out, const std::string& briefing) {
    CTextManager* texts = g_clientWorld->texts_;
    bool missing;
    {
        std::string prefix = game::briefingwords::BriefingSection();
        std::string section = prefix + briefing;
        missing = texts->CTextManager::Open(section) == 0;
    }
    if (missing) {
        *out = game::kEmptyText;
        return out;
    }
    // The first "#show" line whose second word is not "Hero" names the speaker.
    std::vector<std::string> words;
    while (texts->FindLine(game::briefingwords::ShowTag())) {
        texts->Split(&words, 1);
        if (words.size() >= 2 &&
            text::CompareIgnoringCase(words[1],
                            game::briefingwords::Hero()) != 0) {
            *out = words[1];
            return out;
        }
    }
    *out = game::kEmptyText;
    return out;
}

std::string* BriefingVariableInto(std::string* out, const std::string& briefing) {
    std::string speaker;
    BriefingSpeakerInto(&speaker, briefing);
    if (!IsEmpty(speaker)) {
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
    CPlayer* player = g_clientWorld->LocalView()->player_;
    // "<zone>_N": the zone's quests are loaded first.
    std::size_t length = std::strlen(briefing);
    if (static_cast<int>(length) > 2 && briefing[length - 2] == '_') {
        std::string zone = briefing;
        {
            std::string left = str::Left(zone, static_cast<int>(zone.size()) - 2);
            zone = left;
        }
        LoadZoneQuests(zone);
    }
    std::string variable;
    BriefingVariableInto(&variable, briefing);
    player->Party()->variables_.Set(variable, 1.0f);
}

// ---- Forgetting briefings, unloading quest texts --------------------------------------------------

// The quest text files (world.h) and the quest folders (g_questFolders, below): empty at the start, as the static
// initialisers leave them; the exit functions they register destroy them
// (docs/notes/standalone.md, "The static initialisers' objects").
constinit std::vector<std::string> g_questFiles = /*007AFDD0*/ {};
constinit std::vector<std::string> g_questFolders = /*007AFDC0*/ {};

void DestroyQuestFiles() { lib::FreeStorage(g_questFiles); }
void DestroyQuestFolders() { lib::FreeStorage(g_questFolders); }

void GameFlow::ForgetBriefing(const char* briefing) {
    CPlayer* player = g_clientWorld->LocalView()->player_;
    std::string variable;
    BriefingVariableInto(&variable, briefing);
    player->Party()->variables_.Remove(variable);
}

void UnloadQuestTexts() {
    CTextManager* texts = g_clientWorld->texts_;
    if (!texts) return;
    std::vector<std::string>& files = g_questFiles;
    for (auto& file : files) texts->RemoveSource(file);
    // erase(begin(), end()) moves nothing and destroys them all.
    files.clear();
}

// ---- The quest file of a zone ---------------------------------------------------

bool LoadZoneQuests(std::string_view zone) {
    auto* texts = g_clientWorld->texts_;
    if (!texts) return false;
    std::string file(zone);
    file += kQuestFileSuffix;
    bool found;
    {
        lib::FileFind find;
        std::string folder;
        modfolders::SelectMaps(file, modfolders::MapsSite::ZoneQuests);
        MapsFolder(&folder);
        std::string pattern = folder + file;
        found = find.FindFile(pattern) != 0;
    }
    if (!found) return false;
    std::string path = MapFilePath(file);
    if (ServerRuns()) ShareQuestFile(path, file);
    // A file already loaded is not loaded again.
    std::vector<std::string>& files = g_questFiles;
    if (std::find(files.begin(), files.end(), path) == files.end()) {
        files.push_back(path);
        texts->AddSource(path);
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
        g_interface->OnQuestVariable(name, value);
        g_clientWorld->OnQuestVariable(name, value);
    }
}

// ---- Quest registration, the session's map name ---------------------------------------------------

// A second block: it uses g_questFolders (defined above).
namespace {



// A session or message still in use (not shut down by its owner).
bool Active(const CRefCounted* object) { return object && !(object->flags7_ & CRefCounted::kDetached); }

} // namespace

void GameFlow::RegisterQuests(const std::string& folder) {
    CTextManager* texts = g_clientWorld->texts_;
    if (!texts) return;
    if (!LoadZoneQuests(folder)) return;
    std::vector<std::string>& folders = g_questFolders;
    for (std::string* s = folders.data(); s != folders.data() + folders.size(); ++s)
        if (*s == folder) return;
    bool registered;
    {
        std::string base = folder;
        std::string path = base + game::briefingwords::QuestRegistry();
        // (The registry is binary, read as it is: CTextManager::OpenData.)
        registered = texts->OpenData(path) != 0;
    }
    if (!registered) return;
    // The quest table reads from the texts' cursor (the section just opened).
    g_questDatabase->LoadRegistry(texts->cursor_);   // (its result is not read)
    {
        std::string base = folder;
        std::string path = base + game::briefingwords::MapList();
        g_mapDatabase->Load(path.c_str());
    }
    std::string name(folder);
    folders.push_back(name);
}

std::string* SessionMapNameInto(std::string* out) {
    std::string name;
    // The peer's map message: the client's, else the server's (the map's name its text).
    const net::MapMessage* peer = nullptr;
    const net::ClientSession* client = net::g_gameClient;
    const net::ServerSession* server = net::g_gameServer;
    if (Active(client) && Active(client->map_))
        peer = client->map_;
    else if (Active(server) && Active(server->map_.Get()))
        peer = server->map_.Get();
    if (!peer) {
        *out = game::kEmptyText;
        return out;
    }
    name = peer->text_.data();
    CTextManager* texts = g_clientWorld->texts_;
    int found;
    {
        std::string prefix = game::briefingwords::QuestSection();
        std::string section = prefix + name;
        found = texts->CTextManager::Open(section);
    }
    if (found) texts->ReadLine(&name, 1);
    *out = name; return out;
}
