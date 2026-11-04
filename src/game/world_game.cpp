// The game flow object (the state machine runs on it, Rebirth patches that one): saving a game into
// saves\<slot>, entering a zone. See docs/notes/world.md.
#include "bink_video.h"
#include "camera.h"
#include "console.h"
#include "core_maths.h"
#include "core_refcount.h"
#include "core_streams.h"
#include "game_flow_data.h"
#include "game_singletons.h"
#include "game_strings.h"
#include "item_db.h"
#include "text_manager.h"
#include "mob_script_decoder.h"
#include "mod_folders.h"
#include "net_classes.h"
#include "net_coder.h"
#include "net_connection.h"
#include "net_link.h"
#include "net_messages.h"
#include "net_session.h"
#include "object.h"
#include "object_tables.h"
#include "options.h"
#include "party.h"
#include "party_manager.h"
#include "party_records.h"
#include "player_client.h"
#include "player_manager.h"
#include "player_messages.h"
#include "random.h"
#include "save_versions.h"
#include "sound.h"
#include "ui.h"
#include "ui_cursor.h"
#include "ui_screens.h"
#include "unit.h"
#include "unit_db.h"
#include "world.h"
#include "world_map_files.h"

#include "lib/calendar.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "platform/platform.h"
#include "lib/str.h"
#include "lib/text_case.h"


#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>
#include "lib/rounding.h"

namespace {

using namespace flowdata;

constexpr unsigned kMagicScenario = 0xDEAD;
constexpr unsigned kMagicMission = 0x102;

MapRecord* ZoneRecords() { return g_mapDatabase.maps.data(); }
int ZoneRecordCount() { return static_cast<int>(g_mapDatabase.maps.size()); }

using netlink::FileStream;

void ScriptsReset(MemoryStream* scripts) { scripts->Resize(0); }

// Copies <zone>.sav of every visited zone that has to be kept (the current map's zones and bz7g) from
// folder `from` into folder `to` (saving: saves\current into the slot; loading: back); a zone whose file
// could not be copied, and every other zone, is marked not visited ("zs.<zone>" = 0).
void CopyVisitedZones(const std::string& from, const std::string& to, const std::string& currentMap,
                      NamedFloatTable* variables) {
    for (int i = 0; i < ZoneRecordCount(); ++i) {
        MapRecord* record = ZoneRecords() + i;
        std::string variable = std::string(kVisitedPrefix) + record->name;
        bool visited = variables->Get(variable) != 0.0f;
        if (visited) {
            bool keep = str::Compare(record->name, kAlwaysCopied) == 0 || str::Compare(record->allod, currentMap) == 0;
            if (keep) {
                std::string fromFile = from + record->name + kSaveExtension;
                std::string toFile = to + record->name + kSaveExtension;
                if (platform::CopyFileOver(fromFile.c_str(), toFile.c_str())) continue;
            }
        }
        variables->Set(variable, 0.0f);
    }
}

constexpr float kCatchUpRadius = 70.0f;
constexpr float kStartTime = 4.0f;
constexpr int kMaxCatchUpTicks = 1000;


// The zone was saved before: read saves\current\<zone>.sav and restore it, then let the world catch up with
// the time spent away (one tick per elapsed unit, at most 1000, waking the units around the entrance).
// Returns LoadSave's result, 0 when the file cannot be opened or is corrupt.
int RestoreZone(Party* records, MapRecord* zoneRecord, const std::string& zone, std::string& folder,
                const char* mapOverride, FileStream& file, bool& failed) {
    NamedFloatTable* variables = &records->variables;
    folder = str::Format(kCurrentFolder, DataFolderText());
    std::string fileName = zone + kSaveExtension;
    bool opened = file.Open(folder + fileName, platform::FileMode::Read);
    if (!opened) {
        failed = true;
        return 0;
    }
    MemoryStream data;
    if (!SaveFileRead(&data, kMagicMission, &file)) {
        game::FatalError(kCorruptSave, fileName);
        failed = true;
        return 0;
    }
    int result = g_serverWorld.LoadSave(&data, mapOverride);
    file.Close();

    int entrance = records->entrance;
    if (entrance >= 0 && entrance < std::ssize(zoneRecord->exits)) {
        float now = variables->Get(kGTime);
        float left = variables->Get(std::string(kLeftPrefix) + zone);
        int ticks = lib::Truncate((now - left) * 100.0f);
        if (ticks >= kMaxCatchUpTicks) ticks = kMaxCatchUpTicks;
        else if (ticks <= 0) return result;
        // The centre of the exit's deploy rectangle.
        const ZoneExit* exit = &zoneRecord->exits[entrance];
        Vector3 point;
        point.x = (exit->deploy[2] + exit->deploy[0]) * 0.5f;
        point.y = (exit->deploy[3] + exit->deploy[1]) * 0.5f;
        point.z = 0.0f;
        for (int i = ticks; i != 0; --i) {
            g_serverWorld.WakeUnitsAround(point, kCatchUpRadius);
            g_serverWorld.Tick(static_cast<char>(1));
        }
    }
    return result;
}

constexpr int kHostPort = 8000;
constexpr int kServerMessageZone = 8;



// Tells the clients of the game server where the game is: the host's address and the zone.
void AnnounceHost(const char* zone) {
    char host[100];
    if (!platform::net::HostName(host, sizeof(host))) return;
    net::NetAddress address{};
    address.Parse(host, kHostPort);
    std::string formatted = address.ToString();
    net::HostMessage* record = net::NewCounted<net::HostMessage>();
    ShareAssignOwned(net::g_gameServer->hostInfo, record);
    net::HostMessage* shared = net::g_gameServer->hostInfo.Get();
    shared->address = formatted;
    shared = net::g_gameServer->hostInfo.Get();
    shared->zone.assign(zone);
    net::g_gameServer->AnnounceToGuests(kServerMessageZone, zone);
}

// The local client's player record (ClientSession::player, a LoginMessage): its state is the
// game state request to the server (a change goes out with a new version).
int ServerPlayerCount() { return static_cast<int>(g_serverWorld.players->objects.size()); }

constexpr std::uint32_t kMultiplayerHeroId = 0x3B9AF5CE;   // the only record's unit id


constexpr std::uint32_t kGraphChunk = 0x31415926;         // the AI graph chunk in a .mob file

constexpr std::uint32_t kScriptsChunk = 0xA000;          // the .mob file's scripts chunk
constexpr std::uint32_t kScriptEncoded = 0xACCEECCB;     // in it: the script text, packed and XORed
constexpr std::uint32_t kScriptPlain = 0xACCEECCA;       // in it: the script text as is

constexpr int kServerDropClient = 9;

// The client player's new status, a new version.
void ReportStatus(net::PlayerStatus kind) {
    net::g_gameClient->player.Get()->state = kind;
    ++net::g_gameClient->player.Get()->version;
}

constexpr std::size_t kPasswordSize = 0x14;

// The local client joins the server at `address` with the password (zero padded to 20 bytes) as its hello.
void JoinWithPassword(const net::NetAddress& address, const char* password) {
    GrowingBuffer hello;
    char secret[kPasswordSize] = {};
    #pragma warning(suppress : 4996)   // strncpy: the copy padded with zeros
    if (password) std::strncpy(secret, password, kPasswordSize);
    hello.BufferWrite(secret, static_cast<int>(kPasswordSize));
    net::g_gameClient->Join(const_cast<net::NetAddress*>(&address), &hello);
}

// The zone table: a vector of {first, second, id} entries (docs/notes/standalone.md, "The static
// initialisers' objects").
struct ZoneEntry {
    std::string first;
    std::string second;
    int id;
};
std::vector<ZoneEntry> g_zoneTable;
bool SameText(std::string_view a, std::string_view b) {
    return str::Compare(a, b) == 0;
}

// `value` mod 24 (exact).
float HoursOfDay(float value) { return std::fmod(value, 24.0f); }

constexpr int kStateMainMenu = 1, kStateLoad = 2, kStateNewGame = 3, kStateZone = 4, kStatePartyZone = 5,
              kStateNetworkZone = 6;

const char* ResourcesPath() { return options::g_options.resourcesPath.c_str(); }

unsigned g_serverStamp = /*007AFED0*/ {};   // the newest variable stamp seen
unsigned g_appliedStamp = /*007AFED4*/ {};   // the stamp applied up to

// The variable setter with the value as its bits.
void VariableSetBits(NamedFloatTable* table, std::string_view name, std::uint32_t bits) { table->Set(name, std::bit_cast<float>(bits)); }
// The value's bits.
std::uint32_t ValueBits(const net::VariableMessage* record) {
    std::uint32_t bits;
    bits = std::bit_cast<std::uint32_t>(record->value);
    return bits;
}

constexpr const char* kQuestFilePattern = "*.mq";
// The map names of the allods (Gipat, Ingos, Suslanger), each list ended by an empty name.
constexpr const char* kAllodMaps[3][10] = /*00790270*/ {
    {"zone2", "zone3", "zone3obr", "zone4", "zone5", "zone6", "zone7", "zone8", "zone9", ""},
    {"zone12", "zone13", "", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"zone17", "zone18", "", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}
};
constexpr int kGipat = 0, kIngos = 1, kSuslanger = 2;

int AllodOfMap(std::string_view map) {
    if (NameInList(kAllodMaps[kGipat], map)) return kGipat;
    if (NameInList(kAllodMaps[kIngos], map)) return kIngos;
    return NameInList(kAllodMaps[kSuslanger], map) ? kSuslanger : 3;
}

} // namespace

// The game's save stream (core_streams.h).
MemoryStream g_saveStream;

int GameFlow::SaveGame(const std::string& slot) {
    FileStream file;
    std::string slotFolder;
    std::string currentFolder;
    if (g_serverWorld.gameState != GameState::Playing) return 0;

    g_cursor.SetShape(CursorShape::Wait);
    slotFolder = str::Format(kSlotFolder, DataFolderText(), slot);
    currentFolder = str::Format(kCurrentFolder, DataFolderText());
    ClearSaveFolder(slotFolder);
    if (g_serverWorld.zoneMode == ZoneMode::Edge) {
        if (Player* player = g_serverWorld.GetPlayer(0))
            g_partyManager.BeginRedeploy(player->GetParty());
    }
    Party records;
    g_partyManager.EndRedeploy(&records);
    std::string currentMap;
    currentMap = g_mapDatabase.FindMap(records.zone)->allod;

    CopyVisitedZones(currentFolder, slotFolder, currentMap, &records.variables);
    g_partyManager.BeginRedeploy(&records);

    bool opened = file.Open(slotFolder + kScenarioFile, platform::FileMode::Create);
    if (!opened) {
        StartZone(kMainMenu, ZoneMode::MainMenu);
        return 0;
    }

    // scenario.sav: the party, the input state, the scripts and the globals.
    MemoryStream scenario;
    scenario.version = static_cast<std::uint32_t>(kSaveVersion);
    g_partyManager.Save(&scenario);
    g_interface.SaveState(&scenario);
    ScriptsReset(&g_saveStream);
    g_interface.SaveWindowState();
    scenario.WriteSized(&g_saveStream);
    ScriptsReset(&g_saveStream);
    g_camera.Save(&scenario);
    SaveFileWrite(&file, kMagicScenario, &scenario);
    file.Close();

    // mission.sav: the world, except in zones without one (kind 1: cutscenes).
    int kind = g_mapDatabase.FindMap(records.zone)->kind;
    if (kind == 0 || kind == 2) {
        opened = file.Open(slotFolder + kMissionFile, platform::FileMode::Create);
        if (!opened) return 0;
        MemoryStream mission;
        mission.version = static_cast<std::uint32_t>(kSaveVersion);
        g_serverWorld.SaveWorld(&mission);
        SaveFileWrite(&file, kMagicMission, &mission);
        file.Close();
    }

    g_cursor.SetShape(CursorShape::Default);
    console::EndLine(console::g_consoleOut << kSaved << slot);
    return 1;
}

// Enters the party's zone: plays the pending "movie", restores the zone from saves\current when it was
// visited (zs.<zone>), otherwise loads it fresh (NewZone; gz6g picks its frozen or thawed map), then the
// zone's scripts and its exits. Returns 0 on failure, else the restore's result (1 for a fresh zone).
int EnterZone(Party* records) {
    FileStream file;
    int result = 1;
    std::string folder;
    std::string zone(records->zone);
    MapRecord* zoneRecord = g_mapDatabase.FindMap(zone);
    if (!zoneRecord) return 0;

    NamedFloatTable* variables = &records->variables;
    if (lib::Truncate(variables->Get(kMovieVariable)) == 1) {
        if (str::Compare(zone,
                            kMovieZone) != 0) {
            PlayMovie(g_dataFolder + kMovieFile, 0, 1, 1, 1, 1);
            variables->Remove(kMovieVariable);
        }
    } else {
        variables->Remove(kMovieVariable);
    }

    if (str::Compare(zoneRecord->map,
                        game::kEmptyText) != 0) {
        const char* mapOverride = nullptr;
        if (text::CompareIgnoringCase(zone,
                            kFrozenZone) == 0) {
            float frozen = variables->Get(kFrozenVariable);
            mapOverride = kFrozenMap;
            if (frozen == 0.0f) mapOverride = kThawedMap;
        }
        bool visited = variables->Get(std::string(kVisitedPrefix) + zone) != 0.0f;
        if (visited) {
            bool failed = false;
            result = RestoreZone(records, zoneRecord, zone, folder, mapOverride, file, failed);
            if (failed) return 0;
        } else {
            std::string map = zoneRecord->map + kMapExtension;
            if (mapOverride) map = mapOverride;
            bool loaded = g_serverWorld.NewZone(map, zoneRecord->mob + kMobExtension, zoneRecord->name) != 0;
            if (!loaded) return 0;
            zoneRecord = g_mapDatabase.FindMap(zone);
            g_serverWorld.SetTime(kStartTime);
        }
        if (zoneRecord->kind == 2) {
            std::string scripts = zoneRecord->mob + kMobExtension;
            g_serverWorld.LoadZoneScripts(scripts);
        } else {
            ScriptResetZone(g_serverWorld.MainScript());   // the scripts reset for a zone without its own
        }
    }
    g_serverWorld.SetZoneExits(&zoneRecord->exits);
    return result;
}

// Starts network zone `zone` (a zone record of kind 0): tears the current zone down, restores
// saves\current\<zone>.sav when there is one (else loads the zone fresh at time 4), copies its exits with
// every target set to "MPGame1", applies its two settings, marks the game a network one and, on a running
// game server, announces the host and loads the zone's .mob. Returns 1, 0 on failure.
int StartNetworkZone(const std::string& zone) {
    g_interface.closeAll = 1;
    MapRecord* zoneRecord = g_mapDatabase.FindMap(zone);
    if (!zoneRecord || zoneRecord->kind != 0) return 0;
    g_serverWorld.TeardownZone();

    std::string folder;
    FileStream file;
    folder = str::Format(kCurrentFolder, DataFolderText());
    bool opened = file.Open(folder + zone + kSaveExtension, platform::FileMode::Read);
    if (opened) {
        MemoryStream data;
        bool loaded = SaveFileRead(&data, kMagicMission, &file) &&
                      g_serverWorld.LoadSave(&data, nullptr) != 0;
        if (loaded) file.Close();
        if (!loaded) return 0;
    } else {
        bool loaded = g_serverWorld.NewZone(zoneRecord->map + kMapExtension, zoneRecord->mob + kMobExtension, zone) != 0;
        if (!loaded) return 0;
        g_serverWorld.SetTime(kStartTime);
    }

    // The zone's exits, all leading to "MPGame1".
    std::vector<ZoneExit> exits = zoneRecord->exits;
    for (ZoneExit& exit : exits) exit.target = kNetworkExitTarget;
    g_serverWorld.SetZoneExits(&exits);
    g_serverWorld.SetClimate(zoneRecord->climate);
    g_serverWorld.SetCave(zoneRecord->cave);
    g_serverWorld.zoneMode = ZoneMode::Battle;
    g_serverWorld.SetGameState(GameState::Network);
    options::g_options.drawVisibleOnly = 1;

    if (IsLive(net::g_gameServer)) {
        AnnounceHost(zone.c_str());
        std::string name = zone;
        std::string mob = name + kMobExtension;
        g_serverWorld.LoadMobFile(mob);
    }
    return 1;
}

// The counterpart of the zone load: saves the current zone into saves\current\<zone>.sav (SaveWorld, header
// 0x102) and records in the party's variables that it was visited (zs.<zone> = 1) and when it was left
// (zt.<zone> = gtime). Returns 1, 0 when the file cannot be written (after FatalError).
int LeaveZone(Party* records) {
    FileStream file;
    std::string path;
    std::string zone = g_serverWorld.zoneName;

    if (g_serverWorld.zoneMode == ZoneMode::Battle) g_camera.SaveView(7);
    else if (g_serverWorld.zoneMode == ZoneMode::Base) g_camera.SaveView(6);
    path = str::Format(kCurrentFolder, DataFolderText());
    path += zone + kSaveExtension;
    if (!file.Open(path, platform::FileMode::Create)) {
        game::FatalError(kCantWrite, path);
        return 0;
    }
    g_serverWorld.PrepareSave();
    MemoryStream data;
    data.version = static_cast<std::uint32_t>(kSaveVersion);
    g_serverWorld.SaveWorld(&data);
    SaveFileWrite(&file, kMagicMission, &data);
    file.Close();

    NamedFloatTable* variables = &records->variables;
    variables->Set(std::string(kVisitedPrefix) + zone, 1.0f);
    float now = variables->Get(kGTime);
    variables->Set(std::string(kLeftPrefix) + zone, now);
    return 1;
}

// ---- The multiplayer flag -----------------------------------------------------------------------

void GameFlow::UpdateMultiplayerVariable() {
    Player* player = g_clientWorld.LocalView()->player;
    NamedFloatTable* variables = &player->GetParty()->variables;
    if (!IsLive(net::g_gameClient)) return;
    auto set = [variables](bool on) {
        if (on) variables->Set(kMultiplayerVariable, 1.0f);
        else variables->Remove(kMultiplayerVariable);
    };
    if (!IsLive(net::g_gameServer)) {
        set(!IsLive(net::g_gameClient->info));
        return;
    }
    if (zoneSwitchStarted) set(ServerPlayerCount() > 1);
    else set(!IsLive(net::g_gameClient->map));
}

// ---- Starting a multiplayer game ------------------------------------------------------------------

std::string GameFlow::MultiplayerZone() {
    unsigned type = net::g_gameClient->gameType;
    return kMultiplayerZones[type <= 3 ? type : 0];
}

void GameFlow::BeginMultiplayerGame() {
    PartyManagerNewGameFour(&g_partyManager);
    if (std::ssize(GlobalParty()->members) == 1) GlobalParty()->members.data()->unitId = kMultiplayerHeroId;
    GlobalParty()->zone = MultiplayerZone();
    GlobalParty()->variables.Set(kMultiplayerVariable, 1.0f);
    g_partyManager.BeginRedeploy(GlobalParty());
    state = kStateZone;
    requestTarget = MultiplayerZone();
    zoneSwitchStarted = 0;
    ReportStatus(net::PlayerStatus::InBase);
    g_interface.disconnected = 1;
}

// ---- Small glue of the flow -----------------------------------------------------------------------

void GameFlow::ReturnLocalParty() {
    if (!IsLive(net::g_gameClient)) return;
    Player* player = g_clientWorld.LocalView()->player;
    Party* party = player->GetParty();
    GlobalParty()->Assign(party);
    g_partyManager.BeginRedeploy(party);
    // The global party record written into the heroes' folder.
    g_savedParties.outsideGame.WriteFile(g_savedParties.folder);
    UpdateFromServer();
}

void GameFlow::UpdateAndAcknowledge() {
    if (!IsLive(net::g_gameClient)) return;
    UpdateFromServer();
    if (net::g_gameClient->player.Get()->state != net::PlayerStatus::Connecting) return;
    ReportStatus(net::PlayerStatus::InZone);
}

void GameFlow::RequestBriefing(std::string_view briefing) {
    net::ClientSession* client = net::g_gameClient;
    if (IsLive(client)) client->SendCommand(net::CommandKind::Zone, briefing);
}

void GameFlow::RecreateClientSession() {
    net::g_gameClient.Reset();
    net::g_gameClient.Assign(net::NewCounted<net::ClientSession>());
}

int RequestZone(std::string_view zone, int entrance) {
    g_interface.closeAll = true;
    // The local view asks to go to a zone (name, entrance).
    return g_clientWorld.LocalView()->RequestZone(zone, entrance);
}

void GameFlow::DropSessions() {
    g_interface.disconnected = 0;
    net::g_gameServer.Reset();
    net::g_gameClient.Reset();
}

void GameFlow::EndSession() {
    DropSessions();
    UseResources(false);
}

void GameFlow::CloseSessions() {
    DropSessions();
    state = kStateMainMenu;
}

// The flow's requests: the state its update acts on next, the zone or save name, and a party for states 5
// and 6.
void GameFlow::RequestState3() { state = kStateNewGame; }

void GameFlow::RequestLoad(std::string_view name) {
    state = kStateLoad;
    requestTarget = name;
}

void GameFlow::RequestZoneChange(std::string_view zone) {
    state = kStateZone;
    requestTarget = zone;
}

void GameFlow::RequestPartyZone5(const char* zone, Party* party) {
    state = kStatePartyZone;
    requestTarget = zone;
    RequestParty()->Assign(party);
}

void GameFlow::RequestPartyZone6(const char* zone, Party* party) {
    state = kStateNetworkZone;
    requestTarget = zone;
    RequestParty()->Assign(party);
}

// Leaves through exit `exit`: with a live client session the flow switches the multiplayer zone, otherwise the
// local client takes the exit.
void GameFlow::LeaveThroughExit(int exit) {
    if (IsMultiplayerClient())
        SwitchMultiplayerZone();
    else
        g_clientWorld.LocalView()->TakeExit(exit);
}

int WorldServer::ReadAIGraph(std::string_view map, std::string_view mob, ByteStream* out) {
    FileStream mobFile;
    int found = 0;
    // The graph chunk of the .mob file.
    mobFile.Open(MapPath(mob), platform::FileMode::Read);
    if (mobFile.FindTagged(kGraphChunk, out)) found = 1;
    mobFile.Close();
    if (found) return found;
    // Otherwise <map without extension>.grp.
    std::string base = MapPath(map);
    int dot = str::ReverseFind(base, '.');
    if (dot > 0) base = str::Left(base, dot);
    FileStream graph;
    if (graph.Open(base + kGraphExtension, platform::FileMode::Read)) {
        out->CopyFrom(&graph);
        found = 1;
        graph.Close();
    }
    return found;
}

void WorldServer::SetConstructionVariable(float value) {
    if (Player* player = GetPlayer(0)) player->Server()->SetVariable(kConstructionVariable, value);
}

int WorldServer::RunCommandScript(const std::string& text, bool run, ScriptErrorInfo* error) {
    Script* script = SecondScript();
    std::string source = text;   // Script::Load preprocesses its text in place
    int ok = script->Load(&source, 1);
    if (ok && run) ScriptTick(script);
    if (error) script->GetError(error);
    return ok;
}

void WorldServer::LoadZoneScripts(std::string_view mob) {
    FileStream file;
    std::string script;
    MemoryStream scripts;
    MemoryStream text;
    MobScriptDecoder decoder;
    file.Open(MapPath(mob), platform::FileMode::Read);
    file.FindTagged(kScriptsChunk, &scripts);
    if (scripts.FindTagged(kScriptEncoded, &text)) {
        const std::uint8_t* data = text.data.data();
        std::uint32_t size = text.size;
        char zero = 0;
        decoder.input.resize(size, zero);
        std::memmove(decoder.input.data(), data, size);
        script = decoder.Decode();
    } else if (scripts.FindTagged(kScriptPlain, &text)) {
        text.Seek(text.size);
        char zero = 0;
        text.WriteValue(zero);
        script = reinterpret_cast<const char*>(text.data.data());
    }
    ScriptReset(MainScript());
    MainScript()->Load(&script, 1);
}

void GameFlow::SwitchMultiplayerZone() {
    if (zoneSwitchStarted) {
        // A switch is pending: ask for the start zone again.
        ReportStatus(net::PlayerStatus::InBase);
        bool host = IsLive(net::g_gameServer);
        RequestZone(MultiplayerZone(), 0);
        if (host) {
            // The host sends command 9 with the announced zone and drops the announcement.
            net::HostMessage* announced = net::g_gameServer->hostInfo.Get();
            if (IsLive(announced)) net::g_gameServer->AnnounceToGuests(kServerDropClient, announced->zone.data());
            DestroyOwned(net::g_gameServer->hostInfo);
        }
        zoneSwitchStarted = 0;
        return;
    }

    // Every connection of the host drops what it has read of the incoming reliable stream
    // (flags 6, high and the cursor back to the start).
    net::ServerSession* server = net::g_gameServer;
    if (IsLive(server)) {
        for (auto& connection : server->clients) {
            GameBuffer& in = connection.Get()->reliableIn;
            std::uint8_t* begin = in.begin;
            in.flags = 6;
            in.high = begin;
            in.cur = begin;
        }
    }
    Party records;
    Player* player = g_serverWorld.GetPlayer(0);
    if (player && player->Server()->partyDeployed) {
        g_serverWorld.SyncPartyRecords(player);
        records.Assign(player->GetParty());
        g_serverWorld.ClearPlayerItems(player);
    } else {
        records.Assign(GlobalParty());
    }
    records.zone = multiplayerZone;
    ReportStatus(net::PlayerStatus::Connecting);
    if (IsLive(net::g_gameServer)) {
        RequestPartyZone6(multiplayerZone.c_str(), &records);
    } else {
        net::HostMessage* host = net::g_gameClient->info;
        if (IsLive(host)) {
            records.zone = host->zone.data();
            RequestPartyZone5(host->address.data(), &records);
        }
    }
    zoneSwitchStarted = 1;
}

void GameFlow::StartServer(const char* name, int maxPlayers, int gameType, const char* password, bool unlisted,
                           int masterKey) {
    BuildZoneTable();
    net::g_netLog = &console::g_netLogOut;
    net::g_gameServer.Assign(net::NewCounted<net::ServerSession>(gameType));
    net::g_gameClient.Assign(net::NewCounted<net::ClientSession>());
    // The local client connects to the local server.
    net::NetAddress address = {};
    address.Parse(kLocalHost, net::g_gamePort);
    JoinWithPassword(address, password);
    net::g_gameClient->player.Get()->host = 1;
    const char* shown = name && *name ? name : kServerName;
    net::g_gameServer->name.assign(shown);
    net::g_gameServer->maxPlayers = maxPlayers;
    if (password && *password) net::g_gameServer->SetPassword(password);
    net::g_gameServer->SetPublic(!unlisted);
    net::g_gameClient->gameType = gameType;
    net::g_gameServer->masterKey = masterKey;
    AfterServerStart();
}

void GameFlow::StartClient() {
    net::g_netLog = &console::g_netLogOut;
    net::g_gameServer.Reset();
    net::g_gameClient.Assign(net::NewCounted<net::ClientSession>());
    AfterServerStart();
}

void GameFlow::JoinServer(const net::NetAddress* address, const char* password) {
    JoinWithPassword(*address, password);
}

void WorldServer::AcceptNewPlayers(PlayerSource* lobby) {
    std::vector<HeldRef<netlink::NetLink>> joined;
    if (gameState == GameState::None) return;
    lobby->TakeNew(&joined);
    for (netlink::NetLink* connection : joined) {
        Player* player = players->CreateSessionPlayer();
        player->Connect(connection);
        player->dirty = kPlayerDirtyAll;
        player->sessionKind = static_cast<int>(zoneMode);
        player->Server()->lastPosition = startPosition;
        int playerId = player->playerId;
        console::EndLine(console::g_netLogOut << kAcceptedPlayer << playerId);
    }
}

std::vector<std::string> PickZones(int id, const char* exclude) {
    std::vector<std::string> out;
    // The entry lists (the game keeps STLport lists of copies).
    std::vector<const ZoneEntry*> pending;
    for (const ZoneEntry& e : g_zoneTable)
        if (e.id == id) pending.push_back(&e);
    std::vector<const ZoneEntry*> group;
    while (!pending.empty()) {
        // The group of the first pending entry's second name, in table order.
        const std::string& key = pending[0]->second;
        group.clear();
        for (auto it = pending.begin(); it != pending.end();) {
            if (SameText((*it)->second, key)) {
                group.push_back(*it);
                it = pending.erase(it);
            } else {
                ++it;
            }
        }
        // With a choice, never the excluded first name.
        if (group.size() > 1) std::erase_if(group, [exclude](const ZoneEntry* e) { return SameText(e->first, exclude); });
        unsigned pick = g_random.Next() % static_cast<unsigned>(group.size());
        out.push_back(group[pick]->first);
    }
    return out;
}

int LoadMission(ByteStream* file) {
    g_clientWorld.sounds->StopAll(1);
    MemoryStream mission;
    if (!SaveFileRead(&mission, kMagicMission, file))
        return 0;
    int version = static_cast<int>(mission.version);
    if (version < kVersionScenarioGlobals) g_camera.Load(&mission);
    if (static_cast<int>(mission.version) < kVersionScenarioInput) {
        // Old saves: the input state, then the rest goes to the save stream.
        g_saveStream.Resize(0);
        g_interface.LoadState(&mission);
        mission.ReadSized(&g_saveStream);
        g_saveStream.version = mission.version;
    }
    return g_serverWorld.LoadSave(&mission, nullptr);
}

void ClearSaveFolder(const std::string& path) {
    platform::CreateFolder(path);
    for (const platform::FolderEntry& entry : platform::FolderEntries(path + kAllFiles)) platform::RemoveFile(entry.path);
}

bool NameInList(const char* const* list, std::string_view name) {
    for (; *list[0] != 0; ++list)
        if (text::CompareIgnoringCase(*list, name) == 0) return true;
    return false;
}

void GameFlow::StartZone(std::string_view zone, ZoneMode mode) {
    g_cursor.SetShape(CursorShape::Wait);
    MapRecord* zoneRecord = nullptr;
    g_interface.closeAll = 1;
    if (text::CompareIgnoringCase(zone, kEndOfGame) == 0) {
        mode = ZoneMode::EndOfGame;
    } else {
        zoneRecord = g_mapDatabase.FindMap(zone);
    }
    g_serverWorld.TeardownZone();

    bool found;
    if (mode == ZoneMode::None) {
        found = zoneRecord != nullptr;
        if (found) {
            switch (zoneRecord->kind) {
            case 0: mode = ZoneMode::Battle; break;
            case 1: mode = ZoneMode::Edge; break;
            case 2: mode = ZoneMode::Base; break;
            default: break;
            }
        }
    } else {
        found = mode == ZoneMode::EndOfGame || mode == ZoneMode::MainMenu || zoneRecord != nullptr;
    }
    if (!found) {
        // An unknown zone: the error, the network sessions closed, the main menu instead.
        game::FatalError(kZoneNotFound, std::string(zone));
        if (IsLive(net::g_gameClient)) DropSessions();
        mode = ZoneMode::MainMenu;
    }

    g_serverWorld.SetTimeSpeed(static_cast<float>(options::kDefaultTimeScale));
    GameState result;
    if (mode == ZoneMode::MainMenu) {
        g_serverWorld.NewZone(kMenuMap, kMenuMob, std::nullopt);
        // The menu shows the local time of day.
        const lib::DateTime now = lib::LocalTime();
        int minute = now.minute;
        int hour = now.hour;
        g_serverWorld.SetTime(static_cast<float>(minute) * 0.016666668f + static_cast<float>(hour));
        result = GameState::MainMenu;
        // The first script command queue runs in the menu too (Rebirth's AllowScriptsInMainMenu).
        g_serverWorld.scriptsActive = 1;
    } else if (mode == ZoneMode::EndOfGame) {
        result = GameState::Playing;
    } else {
        // The party enters the zone, then the clock is set from its "gtime".
        Party records;
        g_partyManager.EndRedeploy(&records);
        EnterZone(&records);
        g_partyManager.BeginRedeploy(&records);
        g_serverWorld.SetTime(HoursOfDay(records.variables.Get(kGTime)));
        if (zoneRecord) {
            g_serverWorld.SetClimate(zoneRecord->climate);
            g_serverWorld.SetCave(zoneRecord->cave);
        }
        result = GameState::Playing;
    }

    g_serverWorld.zoneMode = mode;
    if (mode == ZoneMode::Battle) options::g_options.drawVisibleOnly = 1;
    g_serverWorld.SetGameState(result);
    // The server world's player source handed to the client world.
    g_clientWorld.SetPlayer(g_serverWorld.worldHandle.Get());
    g_clientWorld.LocalView()->sendParty = 0;
}

void GameFlow::StartNewGame() {
    console::RunAddonAutoExec();   // the settings' AutoExec lines first (Rebirth's AutoExec)
    console::Execute(kExecAutoexec);
    g_interface.closeAll = 1;
    PartyManagerNewGame(&g_partyManager);
    std::string folder;
    folder = str::Format(kCurrentFolder, DataFolderText());
    ClearSaveFolder(folder);
    Party records;
    g_partyManager.EndRedeploy(&records);
    StartZone(records.zone, ZoneMode::None);
}

// ---- The flow's requests -------------------------------------------------------------

void SwitchResourceDatabases(const std::string& name) {
    UnitDatabase& units = g_unitDatabase;
    units = UnitDatabase();
    units.Load(ResourcesPath(), name);
    units.CountSoundFiles(g_clientWorld.sounds);
    g_skillDatabase = SkillDatabase();
    g_skillDatabase.Load(ResourcesPath(), name);
    g_spellDatabase = itemdb::SpellDatabase();
    g_spellDatabase.Load(ResourcesPath(), name);
    g_itemDatabase = itemdb::ItemDatabase();
    g_itemDatabase.Load(ResourcesPath(), name);
}

void GameFlow::UseResources(bool network) {
    static std::string lastResources = kResourcesSingle;   // the resources in use
    std::string wanted = network ? kResourcesNetwork : kResourcesSingle;
    if (str::Compare(wanted, lastResources) == 0)
        return;
    lastResources = wanted;
    SwitchResourceDatabases(wanted);
    if (network) {
        int value = options::g_options.difficulty;
        options::g_options.difficulty = 0;
        options::g_options.difficultyStored = value;
    } else {
        options::g_options.difficulty = options::g_options.difficultyStored;
    }
}

void GameFlow::ProcessRequest() {
    if (state == 0) return;
    // The zone being left is saved (single player campaigns 1 and 3, or the host in campaign 1).
    if (g_serverWorld.gameState == GameState::Playing && IsSinglePlayerMode(g_serverWorld.zoneMode) && state == kStateZone) {
        Party records;
        g_partyManager.EndRedeploy(&records);
        ::LeaveZone(&records);
        g_partyManager.BeginRedeploy(&records);
    }
    if (g_serverWorld.gameState == GameState::Network && g_serverWorld.zoneMode == ZoneMode::Battle && state == kStateZone) {
        Party records;
        ::LeaveZone(&records);
    }
    g_interface.CloseAll();
    g_serverWorld.TeardownZone();
    UseResources(IsLive(net::g_gameClient));

    switch (state) {
    case kStateMainMenu:
        StartZone(kMainMenu, ZoneMode::MainMenu);
        break;
    case kStateLoad:
        g_clientWorld.SetPlayer(g_serverWorld.worldHandle.Get());
        g_clientWorld.LocalView()->sendParty = 0;
        g_serverWorld.SetGameState(GameState::Playing);
        g_clientWorld.RunTick();
        g_serverWorld.Update();
        g_clientWorld.RunTick();
        g_serverWorld.Update();
        LoadGame(requestTarget);
        break;
    case kStateNewGame:
        // The same steps as the matching start function, Rebirth's AutoExec #3 included.
        StartNewGame();
        break;
    case kStateZone:
        StartZone(requestTarget, ZoneMode::None);
        break;
    case kStatePartyZone: {
        Player* player = g_clientWorld.LocalView()->player;
        player->AssignParty(RequestParty());
        g_serverWorld.TeardownZone();
        if (!g_clientWorld.JoinGameClient()) {
            if (IsLive(net::g_gameClient)) {
                BeginMultiplayerGame();
            } else {
                StartZone(kMainMenu, ZoneMode::MainMenu);
            }
        }
        g_clientWorld.LocalView()->sendParty = 1;
        break;
    }
    case kStateNetworkZone: {
        StartNetworkZone(requestTarget);
        Player* player = g_clientWorld.LocalView()->player;
        player->AssignParty(RequestParty());
        g_clientWorld.SetPlayer(g_serverWorld.worldHandle.Get());
        g_clientWorld.LocalView()->sendParty = 1;
        break;
    }
    default:
        break;
    }
    state = 0;
    net::Pump();
}

int GameFlow::LoadGame(const std::string& slot) {
    if (g_serverWorld.gameState == GameState::Network || g_serverWorld.gameState == GameState::Network3) return 0;
    console::RunAddonAutoExec();   // the settings' AutoExec lines first (Rebirth's AutoExec)
    console::Execute(kExecAutoexec);
    g_cursor.SetShape(CursorShape::Wait);
    g_interface.closeAll = 1;
    FileStream file;
    std::string slotFolder;
    std::string currentFolder;
    slotFolder = str::Format(kSlotFolder, DataFolderText(), slot);
    currentFolder = str::Format(kCurrentFolder, DataFolderText());
    ClearSaveFolder(currentFolder);
    bool opened = file.Open(slotFolder + kScenarioFile, platform::FileMode::Read);
    if (!opened) {
        StartZone(kMainMenu, ZoneMode::MainMenu);
        return 0;
    }

    // scenario.sav: the party, then (by version) the input, the scripts and the globals.
    MemoryStream scenario;
    if (!SaveFileRead(&scenario, kMagicScenario, &file)) {
        StartZone(kMainMenu, ZoneMode::MainMenu);
        return 0;
    }
    g_partyManager.Load(&scenario);
    int version = static_cast<int>(scenario.version);
    if (version >= kVersionScenarioInput) {
        ScriptsReset(&g_saveStream);
        g_interface.LoadState(&scenario);
        (&scenario)->ReadSized(&g_saveStream);
        version = static_cast<int>(scenario.version);
        g_saveStream.version = static_cast<std::uint32_t>(version);
    }
    if (version >= kVersionScenarioGlobals) g_camera.Load(&scenario);
    file.Close();

    // The visited zones of the current map (and bz7g) come back into saves\current; every other
    // zone, and one whose file could not be copied, is marked not visited.
    Party records;
    g_partyManager.EndRedeploy(&records);
    std::string currentMap;
    currentMap = g_mapDatabase.FindMap(records.zone)->allod;
    CopyVisitedZones(slotFolder, currentFolder, currentMap, &records.variables);
    g_partyManager.BeginRedeploy(&records);

    // The zone: kinds 0 and 2 come back from mission.sav, the others start afresh.
    MapRecord* zoneRecord = g_mapDatabase.FindMap(records.zone);
    int kind = zoneRecord->kind;
    if (kind != 0 && kind != 2) {
        StartZone(records.zone, ZoneMode::None);
    } else {
        opened = file.Open(slotFolder + kMissionFile, platform::FileMode::Read);
        if (!opened) return 0;
        LoadMission(&file);
        file.Close();
        g_serverWorld.SetZoneExits(&zoneRecord->exits);
    }
    console::EndLine(console::g_consoleOut << kLoadedText << slot);
    return 1;
}

// The flow.
GameFlow g_gameFlow;

void GameFlow::SetNetworkZone(const char* zone) {
    std::string folder;
    folder = str::Format(kCurrentFolder, DataFolderText());
    ClearSaveFolder(folder);
    multiplayerZone = zone ? zone : "";    // null (LeaveZone): no zone
    if (IsLive(net::g_gameServer) && !IsLive(net::g_gameClient) && zone) StartNetworkZone(zone);
}

int CurrentSaveSize() {
    int total = 1000000;
    for (const platform::FolderEntry& entry : platform::FolderEntries(str::Format(kCurrentFiles, DataFolderText())))
        total += static_cast<int>(entry.size);
    return total;
}

void GameFlow::AfterServerStart() {
    UseResources(true);
// The variables of the party outside a game start empty but the "gvar." ones (Rebirth's PersistentGlobalVars).
    ClearVariablesKeepingGlobals(&GlobalParty()->variables);
    GlobalParty()->RemoveQuestItems();
    // The client's player record gets the hero's and the party's names (or the defaults).
    if (std::ssize(GlobalParty()->members) == 1) {
        const std::string* names = GlobalParty()->members.data()->texts;
        net::g_gameClient->player.Get()->text.assign(names[5]);
        net::g_gameClient->player.Get()->name.assign(names[0]);
    } else {
        net::g_gameClient->player.Get()->text.assign(kDefaultHeroName);
        net::g_gameClient->player.Get()->name.assign(kDefaultPartyName);
    }
    net::g_gameClient->player.Get()->state = net::PlayerStatus::InBase;
}

void GameFlow::UpdateFromServer() {
    if (!IsLive(net::g_gameClient)) return;
    if (TextManager* texts = g_clientWorld.texts) {
        for (const auto& file : g_questFiles) texts->RemoveSource(file);
        g_questFiles.clear();
    }
    Player* player = g_clientWorld.LocalView()->player;
    NamedFloatTable* variables = &player->GetParty()->variables;
// The local player's variables are replaced by the server's; "constr.current" and the "gvar." ones survive (Rebirth's
// PersistentGlobalVars).
    float construction = variables->Get(kConstructionVariable);
    ClearVariablesKeepingGlobals(variables);
    variables->Set(kConstructionVariable, construction);
    if (net::MapMessage* map = net::g_gameClient->map) {
        if (IsLive(map)) LoadZoneQuests(map->text.data());
    }

    // The game type's construction variable and briefings, at least 1.
    std::string briefing;
    unsigned type = net::g_gameClient->gameType;
    if (type <= 3) {
        variables->Set(kTypeVariables[type], 1.0f);
        briefing = kTypeBriefings[type];
    }
    std::string questVariable = BriefingVariable(briefing + kBriefingSuffixQuest);
    std::string kVariable = BriefingVariable(briefing + kBriefingSuffixK);
    std::string* const names[2] = {&questVariable, &kVariable};
    for (std::string* name : names) {
        float value = variables->Get(*name);
        // The larger of the value and 1.0 (a NaN is kept).
        std::uint32_t bits;
        bits = std::bit_cast<std::uint32_t>(value);
        bool keep = (bits & 0x7F800000u) == 0x7F800000u && (bits & 0x7FFFFFu) != 0 ? true : 1.0f < value;
        variables->Set(*name, keep ? value : 1.0f);
    }

    // The briefings the server says were seen.
    for (const auto& zone : net::g_gameClient->zones) MarkBriefing(zone.Get()->text.data());
    // The quest variables ("q.<folder>.<name>") register their quest folders.
    for (const auto& variable : net::g_gameClient->variables) {
        std::string name = variable.Get()->name.data();
        bool quest = str::Compare(str::Left(name, 2), kQuestVariablePrefix) == 0;
        if (!quest) continue;
        std::string rest = str::Right(name, static_cast<int>(name.size()) - 2);
        int dot = str::Find(rest, kDot);
        if (dot > 0) {
            std::string folder = str::Left(rest, dot);
            RegisterQuests(folder);
        }
    }

    // The server's variables, by stamp.
    if (g_clientWorld.LocalView()->player->sessionKind == 1) {
        unsigned previous = g_appliedStamp;
        if (g_serverStamp > g_appliedStamp + 2) g_appliedStamp = g_serverStamp + 1;
        std::vector<const net::VariableMessage*> changed;
        for (const auto& variable : net::g_gameClient->variables) {
            const unsigned stamp = variable.Get()->serial;
            if (g_serverStamp <= stamp) g_serverStamp = stamp;
            if (stamp >= g_appliedStamp || stamp < previous) continue;
            changed.push_back(variable.Get());
        }
        // By stamp (the original's STLport sort). The server gives every set its own serial, so no two
        // variables share a stamp and any sort gives this order.
        std::stable_sort(changed.begin(), changed.end(),
                         [](const net::VariableMessage* a, const net::VariableMessage* b) { return a->serial < b->serial; });
        for (const net::VariableMessage* record : changed) GameFlow::OnVariableChanged(record);
        bool settled;
        do {
            settled = true;
            for (const auto& variable : net::g_gameClient->variables) {
                const net::VariableMessage* record = variable.Get();
                if (record->serial > g_appliedStamp) continue;
                if (record->serial == g_appliedStamp) {
                    GameFlow::OnVariableChanged(record);
                    ++g_appliedStamp;
                    settled = false;
                }
                VariableSetBits(variables, record->name.data(), ValueBits(record));
            }
        } while (!settled);
    } else {
        for (const auto& variable : net::g_gameClient->variables) {
            const net::VariableMessage* record = variable.Get();
            if (g_serverStamp <= record->serial) g_serverStamp = record->serial;
            VariableSetBits(variables, record->name.data(), ValueBits(record));
        }
        g_appliedStamp = 0;
    }
    UpdateMultiplayerVariable();
}

void SavedParties::Reload() {
    entries.clear();

    for (const platform::FolderEntry& found : platform::FolderEntries(folder + kPartyFilePattern)) {
        MpHeroRecord read;
        MpHeroRecord* entry = &read;
        entry->file = found.name;
        if (entry->ReadFile(folder)) {
            ClearVariablesKeepingGlobals(&entry->GetParty()->variables);   // but "gvar." (Rebirth's PersistentGlobalVars)
            if (std::ssize(entry->GetParty()->members) == 1) {
                ReserveOne();
                entries.push_back(read);
            }
        }
    }
}

void GameFlow::BuildZoneTable() {
    std::vector<ZoneEntry>& table = g_zoneTable;
    if (!table.empty()) return;
    modfolders::SelectMaps(kQuestFilePattern, modfolders::MapsSite::ZoneTable);
    for (const platform::FolderEntry& found : platform::FolderEntries(MapsFolder() + kQuestFilePattern)) {
        std::string name = found.Title();
        str::MakeLower(name);
        RegisterQuests(name);
        UnloadQuestTexts();
        std::string map;
        if (MapRecord* record = g_mapDatabase.FindMap(name)) {
            map = record->map;
            const int allod = AllodOfMap(map);
            ZoneEntry& entry = table.emplace_back();
            entry.first = name;
            entry.second = map;
            entry.id = allod;
        }
    }
}
