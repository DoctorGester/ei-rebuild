// The briefing variables of the client: which mercenary speaks a
// briefing (its "#show" lines in the briefing text) and the variable that marks the briefing as
// seen ("b.<speaker>.<briefing>" or "b.elder.<briefing>"). See world.h and docs/notes/world.md.
#include "game/console/console.h"
#include "engine/data/resource.h"
#include "game/app/game_flow.h"
#include "game/app/singletons.h"
#include "engine/data/text_manager.h"
#include "engine/mods/mod_folders.h"
#include "game/net/net_session.h"
#include "game/net/net_messages.h"   // MapMessage
#include "game/map/map_database.h"
#include "game/players/party.h"
#include "game/players/player_client.h"
#include "game/world/quest_records.h"
#include "game/ui/ui.h"
#include "game/world/world.h"
#include "engine/mods/maps_folder.h"
#include "game/players/player.h"

#include "lib/containers.h"
#include "platform/platform.h"
#include "lib/str.h"
#include "lib/text_case.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>

namespace {


constexpr const char* kQuestFileSuffix = ".mq";

// The game server lists the file at `path` under `name` for its clients: the whole file (none of it when it does not
// open) in a buffer.
void ShareQuestFile(const std::string& path, const std::string& name) {
    std::string bytes;
    platform::ReadWholeFile(path, &bytes);
    GrowingBuffer data;
    data.Write(bytes.data(), static_cast<std::uint32_t>(bytes.size()));
    net::g_gameServer->ShareFile(name, &data);
}

// The path of the maps folder's file `file` (LoadZoneQuests's).
std::string MapFilePath(const std::string& file) {
    modfolders::SelectMaps(file, modfolders::MapsSite::ZoneQuestsPath);
    return MapsFolder() + file;
}

} // namespace

std::string BriefingSpeakerName(const std::string& briefing) {
    TextManager* texts = g_clientWorld.texts.get();
    bool missing = texts->TextManager::Open("briefing " + briefing) == 0;
    if (missing) return std::string();
    // The first "#show" line whose second word is not "Hero" names the speaker.
    std::vector<std::string> words;
    while (texts->FindLine("#show")) {
        words = texts->Split(true);
        if (words.size() >= 2 &&
            text::CompareIgnoringCase(words[1],
                            "Hero") != 0) {
            return words[1];
        }
    }
    return std::string();
}

std::string BriefingVariable(const std::string& briefing) {
    std::string speaker = BriefingSpeakerName(briefing);
    if (!speaker.empty()) {
        std::string prefix = "b.";
        return prefix + speaker + "." + briefing;
    }
    std::string prefix = "b.elder.";
    return prefix + briefing;
}

void GameFlow::MarkBriefing(const char* briefing) {
    Player* player = g_clientWorld.LocalView()->player;
    // "<zone>_N": the zone's quests are loaded first.
    std::size_t length = std::strlen(briefing);
    if (static_cast<int>(length) > 2 && briefing[length - 2] == '_') {
        std::string zone = briefing;
        zone = str::Left(zone, static_cast<int>(zone.size()) - 2);
        LoadZoneQuests(zone);
    }
    player->GetParty()->variables.Set(BriefingVariable(briefing), 1.0f);
}

// ---- Forgetting briefings, unloading quest texts --------------------------------------------------

// The quest text files (world.h) and the quest folders.
std::vector<std::string> g_questFiles;
std::vector<std::string> g_questFolders;

void GameFlow::ForgetBriefing(const char* briefing) {
    Player* player = g_clientWorld.LocalView()->player;
    player->GetParty()->variables.Remove(BriefingVariable(briefing));
}

void UnloadQuestTexts() {
    TextManager* texts = g_clientWorld.texts.get();
    if (!texts) return;
    std::vector<std::string>& files = g_questFiles;
    for (auto& file : files) texts->RemoveSource(file);
    // erase(begin(), end()) moves nothing and destroys them all.
    files.clear();
}

// ---- The quest file of a zone ---------------------------------------------------

bool LoadZoneQuests(std::string_view zone) {
    auto* texts = g_clientWorld.texts.get();
    if (!texts) return false;
    std::string file(zone);
    file += kQuestFileSuffix;
    modfolders::SelectMaps(file, modfolders::MapsSite::ZoneQuests);
    if (!ResourceExists(MapsFolder() + file)) return false;
    std::string path = MapFilePath(file);
    if (IsLive(net::g_gameServer)) ShareQuestFile(path, file);
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
    std::string name = record->name.data();
    const float value = record->value;
    bool quest = str::Compare(str::Left(name, 2), "q.") == 0;
    if (quest) {
        g_interface.OnQuestVariable(name, value);
        g_clientWorld.OnQuestVariable(name, value);
    }
}

// ---- Quest registration, the session's map name ---------------------------------------------------

void GameFlow::RegisterQuests(const std::string& folder) {
    TextManager* texts = g_clientWorld.texts.get();
    if (!texts) return;
    if (!LoadZoneQuests(folder)) return;
    std::vector<std::string>& folders = g_questFolders;
    for (std::string* s = folders.data(); s != folders.data() + folders.size(); ++s)
        if (*s == folder) return;
    // (The registry is binary, read as it is: TextManager::OpenData.)
    bool registered = texts->OpenData(folder + "\\quest.reg") != 0;
    if (!registered) return;
    // The quest table reads from the texts' cursor (the section just opened).
    g_questDatabase.LoadRegistry(texts->cursor, texts->length);    // (its result is not read)
    g_mapDatabase.Load(folder + "\\map.txt");
    std::string name(folder);
    folders.push_back(name);
}

std::string SessionMapName() {
    std::string name;
    // The peer's map message: the client's, else the server's (the map's name its text).
    const net::MapMessage* peer = nullptr;
    const net::ClientSession* client = net::g_gameClient;
    const net::ServerSession* server = net::g_gameServer;
    if (IsLive(client) && IsLive(client->map))
        peer = client->map;
    else if (IsLive(server) && IsLive(server->map.Get()))
        peer = server->map.Get();
    if (!peer) return std::string();
    name = peer->text.data();
    TextManager* texts = g_clientWorld.texts.get();
    int found = texts->TextManager::Open("quest " + name);
    if (found) name = texts->ReadLine(true);
    return name;
}
