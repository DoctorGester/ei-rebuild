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

#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/file_io.h"
#include "lib/gameapi.h"
#include "lib/nan_audit.h"
#include "lib/str.h"
#include "lib/text_case.h"
#include "platform/clock.h"
#include "platform/net.h"

#include "ei/rebirth.h"

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

namespace {

using namespace flowdata;

constexpr unsigned kOpenWrite = netlink::FileStream::kOpenWrite;
constexpr unsigned kOpenRead = netlink::FileStream::kOpenRead;
constexpr unsigned kMagicScenario = 0xDEAD;
constexpr unsigned kMagicMission = 0x102;

MapRecord* ZoneRecords() { return g_mapDatabase.maps.data(); }
int ZoneRecordCount() { return static_cast<int>(g_mapDatabase.maps.size()); }
MapRecord* FindZone(std::string_view name) { return g_mapDatabase.FindMap(name); }

using netlink::FileStream;

float VariableGet(NamedFloatTable* table, std::string_view name) { return table->Get(name); }
// The game's _ftol: the value truncated through 64 bits, the low half (0 for a NaN).
int Truncate(float value) { return static_cast<int>(static_cast<std::int64_t>(value)); }
void VariableSet(NamedFloatTable* table, std::string_view name, float value) { table->Set(name, value); }
void ScriptsReset(MemoryStream* scripts) { scripts->Resize(0); }

// Copies saves\current\<zone>.sav of every visited zone that has to be kept (the current map's
// zones and bz7g) into the slot folder; a zone whose file could not be copied, and every other
// zone, is marked not visited ("zs.<zone>" = 0).
void CopyVisitedZones(const std::string& slotFolder, const std::string& currentFolder, std::string& zone,
                      const std::string& currentMap, NamedFloatTable* variables) {
    for (int i = 0; i < ZoneRecordCount(); ++i) {
        MapRecord* record = ZoneRecords() + i;
        const std::string& name = record->name;
        bool visited;
        {
            std::string prefix = kVisitedPrefix;
            std::string variable = prefix + name;
            float value = VariableGet(variables, variable);
            EI_NAN_AUDIT(value);
            visited = value != 0.0f;
        }
        bool keep = false;
        if (visited) {
            keep = str::Compare(record->name,
                                   kAlwaysCopied) == 0 ||
                   str::Compare(record->allod,
                                   currentMap) == 0;
        }
        if (keep) {
            zone = record->name;
            bool failed;
            {
                std::string slotPath = slotFolder + zone;
                std::string slotFile = slotPath + kSaveExtension;
                std::string currentPath = currentFolder + zone;
                std::string currentFile = currentPath + kSaveExtension;
                failed = gameapi::CopyFileA(currentFile.c_str(), slotFile.c_str(), FALSE) == 0;
            }
            if (!failed) continue;
        }
        std::string prefix = kVisitedPrefix;
        std::string variable = prefix + record->name;
        VariableSet(variables, variable, 0.0f);
    }
}

constexpr float kCatchUpRadius = 70.0f;
constexpr float kStartTime = 4.0f;
constexpr int kMaxCatchUpTicks = 1000;


// The zone was saved before: read saves\current\<zone>.sav and restore it, then let the world catch up with
// the time spent away (one tick per elapsed unit, at most 1000, waking the units around the entrance).
// Returns LoadSave's result, 0 when the file cannot be opened or is corrupt.
int RestoreZone(CParty* records, MapRecord* zoneRecord, const std::string& zone, std::string& folder,
                const char* mapOverride, FileStream& file, bool& failed) {
    NamedFloatTable* variables = &records->variables;
    folder = str::Format(kCurrentFolder, DataFolderText());
    std::string fileName = zone + kSaveExtension;
    bool opened;
    {
        std::string path = folder + fileName;
        opened = file.Open(path, kOpenRead, 1);
    }
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
        float now = VariableGet(variables, kGTime);
        float left;
        {
            std::string prefix = kLeftPrefix;
            std::string variable = prefix + zone;
            left = VariableGet(variables, variable);
        }
        int ticks = Truncate((now - left) * 100.0f);
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

constexpr int kGameResultNetwork = 2;
constexpr int kHostPort = 8000;
constexpr int kServerMessageZone = 8;


bool Active(const RefCounted* object) { return object && !(object->flags7 & RefCounted::kDetached); }

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

CParty* GlobalParty() { return g_savedParties.outsideGame.Party(); }
constexpr std::uint32_t kMultiplayerHeroId = 0x3B9AF5CE;   // the only record's unit id

constexpr int kCommandBriefing = 1;
constexpr int kRequestDone = 3;   // the client's game state request: answered

constexpr std::uint32_t kGraphChunk = 0x31415926;         // the AI graph chunk in a .mob file

constexpr std::uint32_t kScriptsChunk = 0xA000;          // the .mob file's scripts chunk
constexpr std::uint32_t kScriptEncoded = 0xACCEECCB;     // in it: the script text, packed and XORed
constexpr std::uint32_t kScriptPlain = 0xACCEECCA;       // in it: the script text as is

constexpr int kServerDropClient = 9;

// The client's game state request: `kind`, a new version.
void RequestKind(int kind) {
    net::g_gameClient->player.Get()->state = kind;
    ++net::g_gameClient->player.Get()->version;
}

constexpr std::size_t kPasswordSize = 0x14;

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

constexpr unsigned kMagicMissionFile = 0x102;

// StartZone's modes (also stored as the campaign mode, the server world's sessionKind).
constexpr int kModeFromRecord = 0;   // by the zone record's kind: 0 -> 1, 1 -> 4, 2 -> 3
constexpr int kModeMainMenu = 2;
constexpr int kModeEndOfGame = 8;    // the zone "endofgame"
// SetGameResult's values here.
constexpr int kResultPlaying = 1;
constexpr int kResultMainMenu = 4;

inline PlayerClient* ClientWorldLocalView(WorldClient* client) { return client->LocalView(); }

// `value` mod 24 (exact).
float HoursOfDay(float value) { return std::fmod(value, 24.0f); }

constexpr int kStateMainMenu = 1, kStateLoad = 2, kStateNewGame = 3, kStateZone = 4, kStatePartyZone = 5,
              kStateNetworkZone = 6;

const char* ResourcesPath() { return options::g_options.resourcesPath.c_str(); }

unsigned g_serverStamp = /*007AFED0*/ {};   // the newest variable stamp seen
unsigned g_appliedStamp = /*007AFED4*/ {};   // the stamp applied up to

// The variable setter with the value as the raw dword that is pushed (no FPU load on the way).
void VariableSetBits(NamedFloatTable* table, std::string_view name, std::uint32_t bits) { table->Set(name, std::bit_cast<float>(bits)); }
// The value's bits, moved as the dword that is pushed (no FPU load).
std::uint32_t ValueBits(const net::VariableMessage* record) {
    std::uint32_t bits;
    bits = std::bit_cast<std::uint32_t>(record->value);
    return bits;
}

constexpr const char* kQuestFilePattern = "*.mq";
// The map names of the allods (Gipat, Ingos, Suslanger, and a fourth list nothing reads), each list ended by an
// empty name.
constexpr const char* kAllodMaps[4][10] = /*00790270*/ {
    {"zone2", "zone3", "zone3obr", "zone4", "zone5", "zone6", "zone7", "zone8", "zone9", ""},
    {"zone12", "zone13", "", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"zone17", "zone18", "", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"zone19", "", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}
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
    std::string zone;
    std::string slotFolder;
    std::string currentFolder;
    if (g_serverWorld.gameResult != 1) return 0;

    g_cursor.SetShape(3);
    slotFolder = str::Format(kSlotFolder, DataFolderText(), slot);
    currentFolder = str::Format(kCurrentFolder, DataFolderText());
    ClearSaveFolder(slotFolder);
    if (g_serverWorld.sessionKind == 4) {
        if (CPlayer* player = g_serverWorld.GetPlayer(0))
            g_partyManager.BeginRedeploy(player->Party());
    }
    CParty records;
    g_partyManager.EndRedeploy(&records);
    std::string currentMap;
    currentMap = FindZone(records.zone)->allod;

    CopyVisitedZones(slotFolder, currentFolder, zone, currentMap, &records.variables);
    g_partyManager.BeginRedeploy(&records);

    bool opened;
    {
        std::string path = slotFolder + kScenarioFile;
        opened = file.Open(path, kOpenWrite, 0);
    }
    if (!opened) {
        g_interface.closeAll = 1;
        StartZone(kMainMenu, 2);
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
    int kind = FindZone(records.zone)->kind;
    if (kind == 0 || kind == 2) {
        {
            std::string path = slotFolder + kMissionFile;
            opened = file.Open(path, kOpenWrite, 0);
        }
        if (!opened) return 0;
        MemoryStream mission;
        mission.version = static_cast<std::uint32_t>(kSaveVersion);
        g_serverWorld.SaveWorld(&mission);
        SaveFileWrite(&file, kMagicMission, &mission);
        file.Close();
    }

    g_cursor.SetShape(0);
    console::EndLine(console::g_consoleOut << kSaved << slot);
    return 1;
}

// Enters the party's zone: plays the pending "movie", restores the zone from saves\current when it was
// visited (zs.<zone>), otherwise loads it fresh (NewZone; gz6g picks its frozen or thawed map), then the
// zone's scripts and its exits. Returns 0 on failure, else the restore's result (1 for a fresh zone).
int EnterZone(CParty* records) {
    FileStream file;
    int result = 1;
    std::string folder;
    std::string zone(records->zone);
    MapRecord* zoneRecord = FindZone(zone);
    if (!zoneRecord) return 0;

    NamedFloatTable* variables = &records->variables;
    if (Truncate(VariableGet(variables, kMovieVariable)) == 1) {
        if (str::Compare(zone,
                            kMovieZone) != 0) {
            {
                std::string movie = g_discFolder + kMovieFile;
                PlayMovie(movie, 0, 1, 1, 1, 1);
            }
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
            float frozen = VariableGet(variables, kFrozenVariable);
            mapOverride = kFrozenMap;
            EI_NAN_AUDIT(frozen);
            if (frozen == 0.0f) mapOverride = kThawedMap;
        }
        bool visited;
        {
            std::string prefix = kVisitedPrefix;
            std::string variable = prefix + zone;
            float value = VariableGet(variables, variable);
            EI_NAN_AUDIT(value);
            visited = value != 0.0f;
        }
        if (visited) {
            bool failed = false;
            result = RestoreZone(records, zoneRecord, zone, folder, mapOverride, file, failed);
            if (failed) return 0;
        } else {
            std::string map = zoneRecord->map + kMapExtension;
            if (mapOverride) map = mapOverride;
            bool loaded;
            {
                std::string mob = zoneRecord->mob + kMobExtension;
                loaded = g_serverWorld.NewZone(map, mob, zoneRecord->name) != 0;
            }
            if (!loaded) return 0;
            zoneRecord = FindZone(zone);
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
    MapRecord* zoneRecord = FindZone(zone);
    if (!zoneRecord || zoneRecord->kind != 0) return 0;
    g_serverWorld.TeardownZone();

    std::string folder;
    FileStream file;
    folder = str::Format(kCurrentFolder, DataFolderText());
    bool opened;
    {
        std::string path = folder + zone;
        std::string fileName = path + kSaveExtension;
        opened = file.Open(fileName, kOpenRead, 1);
    }
    if (opened) {
        MemoryStream data;
        bool loaded = SaveFileRead(&data, kMagicMission, &file) &&
                      g_serverWorld.LoadSave(&data, nullptr) != 0;
        if (loaded) file.Close();
        if (!loaded) return 0;
    } else {
        bool loaded;
        {
            std::string mob = zoneRecord->mob + kMobExtension;
            std::string map = zoneRecord->map + kMapExtension;
            loaded = g_serverWorld.NewZone(map, mob, zone) != 0;
        }
        if (!loaded) return 0;
        g_serverWorld.SetTime(kStartTime);
    }

    // The zone's exits, all leading to "MPGame1".
    std::vector<ZoneExit> exits = zoneRecord->exits;
    for (ZoneExit& exit : exits) exit.target = kNetworkExitTarget;
    g_serverWorld.SetZoneExits(&exits);
    g_serverWorld.SetClimate(zoneRecord->climate);
    g_serverWorld.SetCave(zoneRecord->cave);
    g_serverWorld.sessionKind = 1;
    g_serverWorld.SetGameResult(kGameResultNetwork);
    options::g_options.drawVisibleOnly = 1;

    if (Active(net::g_gameServer)) {
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
int LeaveZone(CParty* records) {
    FileStream file;
    std::string path;
    std::string zone = g_serverWorld.zoneName;

    if (g_serverWorld.sessionKind == 1) g_camera.SaveView(7);
    else if (g_serverWorld.sessionKind == 3) g_camera.SaveView(6);
    path = str::Format(kCurrentFolder, DataFolderText());
    {
        std::string fileName = zone + kSaveExtension;
        path += fileName;
    }
    if (!file.Open(path, kOpenWrite, 0)) {
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
    {
        std::string prefix = kVisitedPrefix;
        std::string variable = prefix + zone;
        VariableSet(variables, variable, 1.0f);
    }
    float now = VariableGet(variables, kGTime);
    {
        std::string prefix = kLeftPrefix;
        std::string variable = prefix + zone;
        VariableSet(variables, variable, now);
    }
    return 1;
}

// ---- The multiplayer flag -----------------------------------------------------------------------

void GameFlow::UpdateMultiplayerVariable() {
    CPlayer* player = g_clientWorld.LocalView()->player;
    NamedFloatTable* variables = &player->Party()->variables;
    if (!Active(net::g_gameClient)) return;
    auto set = [variables](bool on) {
        if (on) VariableSet(variables, kMultiplayerVariable, 1.0f);
        else variables->Remove(kMultiplayerVariable);
    };
    if (!Active(net::g_gameServer)) {
        set(!Active(net::g_gameClient->info));
        return;
    }
    if (zoneSwitchStarted) set(ServerPlayerCount() > 1);
    else set(!Active(net::g_gameClient->map));
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
    VariableSet(&GlobalParty()->variables, kMultiplayerVariable, 1.0f);
    g_partyManager.BeginRedeploy(GlobalParty());
    state = 4;    // load the zone
    requestTarget = MultiplayerZone();
    requestArg = 0;
    zoneSwitchStarted = 0;
    net::g_gameClient->player.Get()->state = 1;
    ++net::g_gameClient->player.Get()->version;
    g_interface.disconnected = 1;
}

// ---- Small glue of the flow -----------------------------------------------------------------------

void GameFlow::ReturnLocalParty() {
    if (!Active(net::g_gameClient)) return;
    CPlayer* player = g_clientWorld.LocalView()->player;
    CParty* party = player->Party();
    GlobalParty()->Assign(party);
    g_partyManager.BeginRedeploy(party);
    // The global party record written into the heroes' folder.
    g_savedParties.outsideGame.WriteFile(g_savedParties.folder);
    UpdateFromServer();
}

void GameFlow::UpdateAndAcknowledge() {
    if (!Active(net::g_gameClient)) return;
    UpdateFromServer();
    if (net::g_gameClient->player.Get()->state != kRequestDone) return;
    net::g_gameClient->player.Get()->state = 0;
    ++net::g_gameClient->player.Get()->version;
}

void GameFlow::RequestBriefing(std::string_view briefing) {
    net::ClientSession* client = net::g_gameClient;
    if (Active(client)) client->SendCommand(kCommandBriefing, briefing);
}

void GameFlow::RecreateClientSession() {
    net::g_gameClient.Reset();
    net::g_gameClient.Assign(net::NewCounted<net::ClientSession>());
}

int GameFlow::SaveGameThunk(const std::string& slot) { return SaveGame(slot); }

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
    g_interface.disconnected = 0;
    net::g_gameServer.Reset();
    net::g_gameClient.Reset();
    state = 1;
}

// The flow's requests: the state its update acts on next, the zone or save name, and a party for states 5
// and 6.
void GameFlow::RequestState3() { state = 3; }

void GameFlow::RequestLoad(std::string_view name, char flag) {
    state = 2;
    requestTarget = name;
    requestArg = flag != 0;
}

void GameFlow::RequestZoneChange(std::string_view zone, int entrance) {
    state = 4;
    requestTarget = zone;
    requestArg = entrance;
}

void GameFlow::RequestPartyZone5(const char* zone, CParty* party) {
    state = 5;
    requestTarget = zone;
    RequestParty()->Assign(party);
}

void GameFlow::RequestPartyZone6(const char* zone, CParty* party) {
    state = 6;
    requestTarget = zone;
    RequestParty()->Assign(party);
}

void GameFlow::DisconnectClient() { g_clientWorld.DisconnectLocalPlayer(); }

int WorldServer::ReadAIGraph(std::string_view map, std::string_view mob, ByteStream* out) {
    FileStream mobFile;
    int found = 0;
    // The graph chunk of the .mob file.
    {
        std::string path = MapPath(mob);
        mobFile.Open(path, kOpenRead, 1);
    }
    if (mobFile.FindTagged(kGraphChunk, out)) found = 1;
    mobFile.Close();
    if (found) return found;
    // Otherwise <map without extension>.grp.
    std::string base = MapPath(map);
    int dot = str::ReverseFind(base, '.');
    if (dot > 0) {
        std::string cut = str::Left(base, dot);
        base = cut;
    }
    {
        FileStream graph;
        bool opened;
        {
            std::string path = base + kGraphExtension;
            opened = graph.Open(path, kOpenRead, 1);
        }
        if (opened) {
            out->CopyFrom(&graph);
            found = 1;
            graph.Close();
        }
    }
    return found;
}

void WorldServer::LoadZoneScripts(std::string_view mob) {
    FileStream file;
    std::string script;
    MemoryStream scripts;
    MemoryStream text;
    MobScriptDecoder decoder;
    {
        std::string path = MapPath(mob);
        file.Open(path, kOpenRead, 1);
    }
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
        RequestKind(1);
        bool host = Active(net::g_gameServer);
        {
            g_interface.closeAll = 1;
            g_clientWorld.LocalView()->RequestZone(MultiplayerZone(), 0);
        }
        if (host) {
            // The host sends command 9 with the announced zone and drops the announcement.
            net::HostMessage* announced = net::g_gameServer->hostInfo.Get();
            if (Active(announced)) net::g_gameServer->AnnounceToGuests(kServerDropClient, announced->zone.data());
            DestroyOwned(net::g_gameServer->hostInfo);
        }
        zoneSwitchStarted = 0;
        return;
    }

    // Every connection of the host drops what it has read of the incoming reliable stream
    // (flags 6, high and the cursor back to the start).
    net::ServerSession* server = net::g_gameServer;
    if (Active(server)) {
        for (auto& connection : server->clients) {
            GameBuffer& in = connection.Get()->reliableIn;
            std::uint8_t* begin = in.begin;
            in.flags = 6;
            in.high = begin;
            in.cur = begin;
        }
    }
    CParty records;
    CPlayer* player = g_serverWorld.GetPlayer(0);
    if (player && player->Server()->partyDeployed) {
        g_serverWorld.SyncPartyRecords(player);
        records.Assign(player->Party());
        g_serverWorld.ClearPlayerItems(player);
    } else {
        records.Assign(GlobalParty());
    }
    records.zone = multiplayerZone;
    RequestKind(3);
    if (Active(net::g_gameServer)) {
        state = 6;
        requestTarget = multiplayerZone;
        RequestParty()->Assign(&records);
    } else {
        net::HostMessage* host = net::g_gameClient->info;
        if (Active(host)) records.zone = host->zone.data();
        host = net::g_gameClient->info;
        if (Active(host)) {
            state = 5;
            requestTarget = host->address.data();
            RequestParty()->Assign(&records);
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
    // The local client connects to the local server with the password as its hello.
    net::NetAddress address = {};
    address.Parse(kLocalHost, net::g_gamePort);
    {
        GrowingBuffer hello;
        char secret[kPasswordSize] = {};
        #pragma warning(suppress : 4996)   // strncpy: the copy padded with zeros
        if (password) std::strncpy(secret, password, kPasswordSize);
        hello.BufferWrite(secret, static_cast<int>(kPasswordSize));
        net::g_gameClient->Join(&address, &hello);
    }
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
    GrowingBuffer hello;
    char secret[kPasswordSize] = {};
    #pragma warning(suppress : 4996)   // strncpy: the copy padded with zeros
    if (password) std::strncpy(secret, password, kPasswordSize);
    hello.BufferWrite(secret, static_cast<int>(kPasswordSize));
    net::g_gameClient->Join(const_cast<net::NetAddress*>(address), &hello);
}

void WorldServer::AcceptNewPlayers(PlayerSource* lobby) {
    std::vector<HeldRef<netlink::NetLink>> joined;
    if (gameResult == 0) return;
    lobby->TakeNew(&joined);
    for (netlink::NetLink* connection : joined) {
        CPlayer* player = players->CreateSessionPlayer();
        player->Connect(connection);
        player->dirty = kPlayerDirtyAll;
        player->sessionKind = sessionKind;
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
    if (!SaveFileRead(&mission, kMagicMissionFile, file))
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
    gameapi::CreateDirectoryA(path.c_str(), nullptr);
    lib::FileFind finder;
    int found;
    {
        std::string pattern = path + kAllFiles;
        found = finder.FindFile(pattern);
    }
    while (found) {
        found = finder.FindNext();
        std::string file(finder.GetFilePath());
        gameapi::DeleteFileA(file.c_str());
    }
    finder.Close();
}

bool NameInList(const char* const* list, std::string_view name) {
    for (; *list[0] != 0; ++list)
        if (text::CompareIgnoringCase(*list, name) == 0) return true;
    return false;
}

void GameFlow::StartZone(std::string_view zone, int mode) {
    g_cursor.SetShape(3);
    MapRecord* zoneRecord = nullptr;
    g_interface.closeAll = 1;
    if (text::CompareIgnoringCase(zone, kEndOfGame) == 0) {
        mode = kModeEndOfGame;
    } else {
        zoneRecord = FindZone(zone);
    }
    g_serverWorld.TeardownZone();

    bool found;
    if (mode == kModeFromRecord) {
        found = zoneRecord != nullptr;
        if (found) {
            switch (zoneRecord->kind) {
            case 0: mode = 1; break;
            case 1: mode = 4; break;
            case 2: mode = 3; break;
            default: break;
            }
        }
    } else {
        found = mode == kModeEndOfGame || mode == kModeMainMenu || zoneRecord != nullptr;
    }
    if (!found) {
        // An unknown zone: the error, the network sessions closed, the main menu instead.
        game::FatalError(kZoneNotFound, std::string(zone));
        if (Active(net::g_gameClient)) {
            g_interface.disconnected = 0;
            net::g_gameServer.Reset();
            net::g_gameClient.Reset();
        }
        mode = kModeMainMenu;
    }

    g_serverWorld.SetTimeSpeed(static_cast<float>(options::g_options.defaultTimeScale));
    int result;
    if (mode == kModeMainMenu) {
        g_serverWorld.NewZone(kMenuMap, kMenuMob, std::nullopt);
        // The menu shows the local time of day.
        const platform::DateTime now = platform::LocalTime();
        int minute = now.minute;
        int hour = now.hour;
        g_serverWorld.SetTime(static_cast<float>(minute) * 0.016666668f + static_cast<float>(hour));
        result = kResultMainMenu;
        // Rebirth's AllowScriptsInMainMenu: the first script command queue runs in the menu too.
        if (ei::rebirth::Enabled(ei::rebirth::Patch::AllowScriptsInMainMenu)) g_serverWorld.scriptsActive = 1;
    } else if (mode == kModeEndOfGame) {
        result = kResultPlaying;
    } else {
        // The party enters the zone, then the clock is set from its "gtime".
        CParty records;
        g_partyManager.EndRedeploy(&records);
        EnterZone(&records);
        g_partyManager.BeginRedeploy(&records);
        g_serverWorld.SetTime(HoursOfDay(VariableGet(&records.variables, kGTime)));
        if (zoneRecord) {
            g_serverWorld.SetClimate(zoneRecord->climate);
            g_serverWorld.SetCave(zoneRecord->cave);
        }
        result = kResultPlaying;
    }

    g_serverWorld.sessionKind = mode;
    if (mode == 1) options::g_options.drawVisibleOnly = mode;
    g_serverWorld.SetGameResult(result);
    // The server world's player source handed to the client world.
    g_clientWorld.SetPlayer(g_serverWorld.worldHandle.Get());
    ClientWorldLocalView(&g_clientWorld)->sendParty = 0;
}

void GameFlow::StartNewGame() {
    // Rebirth's AutoExec #2 (the call redirected): addon's AutoExec lines first.
    if (ei::rebirth::Enabled(ei::rebirth::Patch::AutoExec)) console::RunAddonAutoExec();
    console::Execute(kExecAutoexec);
    g_interface.closeAll = 1;
    PartyManagerNewGame(&g_partyManager);
    std::string folder;
    folder = str::Format(kCurrentFolder, DataFolderText());
    ClearSaveFolder(folder);
    CParty records;
    g_partyManager.EndRedeploy(&records);
    StartZone(records.zone, 0);
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
    int& currentState = state;
    if (currentState == 0) return;
    // The zone being left is saved (single player campaigns 1 and 3, or the host in campaign 1).
    if (g_serverWorld.gameResult == 1 && (g_serverWorld.sessionKind == 1 || g_serverWorld.sessionKind == 3) && currentState == kStateZone) {
        CParty records;
        g_partyManager.EndRedeploy(&records);
        ::LeaveZone(&records);
        g_partyManager.BeginRedeploy(&records);
    }
    if (g_serverWorld.gameResult == 2 && g_serverWorld.sessionKind == 1 && currentState == kStateZone) {
        CParty records;
        ::LeaveZone(&records);
    }
    g_interface.CloseAll();
    g_serverWorld.TeardownZone();
    UseResources(Active(net::g_gameClient));

    auto* clientWorld = &g_clientWorld;
    switch (currentState) {
    case kStateMainMenu:
        g_interface.closeAll = 1;
        StartZone(kMainMenu, 2);
        break;
    case kStateLoad:
        clientWorld->SetPlayer(g_serverWorld.worldHandle.Get());
        ClientWorldLocalView(clientWorld)->sendParty = 0;
        g_serverWorld.SetGameResult(1);
        clientWorld->RunTick();
        g_serverWorld.Update();
        clientWorld->RunTick();
        g_serverWorld.Update();
        LoadGame(requestTarget);
        if (requestArg) {
            const char* autoplay = kAutoplay;
            if (autoplay) {
                ClientWorldLocalView(clientWorld)->StartRecording(autoplay);
                g_serverWorld.sendsStartPosition = 1;
            } else {
                ClientWorldLocalView(clientWorld)->StopRecording();
                g_serverWorld.sendsStartPosition = 0;
            }
        }
        break;
    case kStateNewGame:
        // The same steps as the matching start function (inlined here), Rebirth's AutoExec #3 included.
        StartNewGame();
        break;
    case kStateZone:
        StartZone(requestTarget, 0);
        break;
    case kStatePartyZone: {
        CPlayer* player = ClientWorldLocalView(clientWorld)->player;
        player->AssignParty(RequestParty());
        g_serverWorld.TeardownZone();
        if (!clientWorld->JoinGameClient()) {
            if (Active(net::g_gameClient)) {
                BeginMultiplayerGame();
            } else {
                g_interface.closeAll = 1;
                StartZone(kMainMenu, 2);
            }
        }
        ClientWorldLocalView(clientWorld)->sendParty = 1;
        break;
    }
    case kStateNetworkZone: {
        StartNetworkZone(requestTarget);
        CPlayer* player = ClientWorldLocalView(clientWorld)->player;
        player->AssignParty(RequestParty());
        clientWorld->SetPlayer(g_serverWorld.worldHandle.Get());
        ClientWorldLocalView(clientWorld)->sendParty = 1;
        break;
    }
    default:
        break;
    }
    currentState = 0;
    net::Pump();
}

int GameFlow::LoadGame(const std::string& slot) {
    if (g_serverWorld.gameResult == 2 || g_serverWorld.gameResult == 3) return 0;
    // Rebirth's AutoExec #1 (the call redirected): addon's AutoExec lines first.
    if (ei::rebirth::Enabled(ei::rebirth::Patch::AutoExec)) console::RunAddonAutoExec();
    console::Execute(kExecAutoexec);
    g_cursor.SetShape(3);
    g_interface.closeAll = 1;
    FileStream file;
    std::string zone;
    std::string slotFolder;
    std::string currentFolder;
    slotFolder = str::Format(kSlotFolder, DataFolderText(), slot);
    currentFolder = str::Format(kCurrentFolder, DataFolderText());
    ClearSaveFolder(currentFolder);
    bool opened;
    {
        std::string path = slotFolder + kScenarioFile;
        opened = file.Open(path, kOpenRead, 1);
    }
    if (!opened) {
        g_interface.closeAll = 1;
        StartZone(kMainMenu, 2);
        return 0;
    }

    // scenario.sav: the party, then (by version) the input, the scripts and the globals.
    MemoryStream scenario;
    if (!SaveFileRead(&scenario, kMagicScenario, &file)) {
        g_interface.closeAll = 1;
        StartZone(kMainMenu, 2);
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
    CParty records;
    g_partyManager.EndRedeploy(&records);
    std::string currentMap;
    currentMap = FindZone(records.zone)->allod;
    NamedFloatTable* variables = &records.variables;
    for (int i = 0; i < ZoneRecordCount(); ++i) {
        MapRecord* record = ZoneRecords() + i;
        bool visited;
        {
            std::string prefix = kVisitedPrefix;
            std::string variable = prefix + record->name;
            float value = VariableGet(variables, variable);
            EI_NAN_AUDIT(value);
            visited = value != 0.0f;
        }
        bool restored = false;
        if (visited) {
            bool keep = str::Compare(record->name,
                                        kAlwaysCopied) == 0 ||
                        str::Compare(record->allod,
                                        currentMap) == 0;
            if (keep) {
                zone = record->name;
                std::string currentPath = currentFolder + zone;
                std::string currentFile = currentPath + kSaveExtension;
                std::string slotPath = slotFolder + zone;
                std::string slotFile = slotPath + kSaveExtension;
                restored = gameapi::CopyFileA(slotFile.c_str(), currentFile.c_str(), FALSE) != 0;
                if (restored) continue;
            }
        }
        std::string prefix = kVisitedPrefix;
        std::string variable = prefix + record->name;
        VariableSet(variables, variable, 0.0f);
    }
    g_partyManager.BeginRedeploy(&records);

    // The zone: kinds 0 and 2 come back from mission.sav, the others start afresh.
    MapRecord* zoneRecord = FindZone(records.zone);
    int kind = zoneRecord->kind;
    if (kind != 0 && kind != 2) {
        StartZone(records.zone, 0);
    } else {
        {
            std::string path = slotFolder + kMissionFile;
            opened = file.Open(path, kOpenRead, 1);
        }
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
    if (Active(net::g_gameServer) && !Active(net::g_gameClient) && zone) StartNetworkZone(zone);
}

int CurrentSaveSize() {
    lib::FileFind find;
    std::string pattern;
    pattern = str::Format(kCurrentFiles, DataFolderText());
    int total = 1000000;
    if (find.FindFile(pattern)) {
        int more;
        do {
            more = find.FindNext();
            total += static_cast<int>(find.GetLength());
        } while (more);
    }
    return total;
}

void GameFlow::AfterServerStart() {
    UseResources(true);
// The variables of the party outside a game start empty. Rebirth's PersistentGlobalVars keeps the "gvar."
// ones.
    NamedFloatTable* variables = &GlobalParty()->variables;
    if (ei::rebirth::Enabled(ei::rebirth::Patch::PersistentGlobalVars))
        ClearVariablesKeepingGlobals(variables);
    else
        variables->clear();
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
    net::g_gameClient->player.Get()->state = 1;
}

void GameFlow::UpdateFromServer() {
    if (!Active(net::g_gameClient)) return;
    if (TextManager* texts = g_clientWorld.texts) {
        for (const auto& file : g_questFiles) texts->RemoveSource(file);
        g_questFiles.clear();
    }
    auto* clientWorld = &g_clientWorld;
    CPlayer* player = ClientWorldLocalView(clientWorld)->player;
    NamedFloatTable* variables = &player->Party()->variables;
// The local player's variables are replaced by the server's; "constr.current" survives. Rebirth's
// PersistentGlobalVars keeps the "gvar." ones.
    float construction = VariableGet(variables, kConstructionVariable);
    NamedFloatTable unusedCopy(*variables);
    if (ei::rebirth::Enabled(ei::rebirth::Patch::PersistentGlobalVars))
        ClearVariablesKeepingGlobals(variables);
    else
        variables->clear();
    VariableSet(variables, kConstructionVariable, construction);
    if (net::MapMessage* map = net::g_gameClient->map) {
        if (Active(map)) LoadZoneQuests(map->text.data());
    }

    // The game type's construction variable and briefings, at least 1.
    std::string briefing;
    unsigned type = net::g_gameClient->gameType;
    if (type <= 3) {
        VariableSet(variables, kTypeVariables[type], 1.0f);
        briefing = kTypeBriefings[type];
    }
    std::string questVariable = BriefingVariable(briefing + kBriefingSuffixQuest);
    std::string kVariable = BriefingVariable(briefing + kBriefingSuffixK);
    std::string* const names[2] = {&questVariable, &kVariable};
    for (std::string* name : names) {
        float value = VariableGet(variables, (*name));
        // The larger of the value and 1.0 (a NaN is kept).
        std::uint32_t bits;
        bits = std::bit_cast<std::uint32_t>(value);
        bool keep = (bits & 0x7F800000u) == 0x7F800000u && (bits & 0x7FFFFFu) != 0 ? true : 1.0f < value;
        VariableSet(variables, (*name), keep ? value : 1.0f);
    }

    // The briefings the server says were seen.
    for (const auto& zone : net::g_gameClient->zones) MarkBriefing(zone.Get()->text.data());
    // The quest variables ("q.<folder>.<name>") register their quest folders.
    for (const auto& variable : net::g_gameClient->variables) {
        std::string name = variable.Get()->name.data();
        bool quest;
        {
            std::string prefix = str::Left(name, 2);
            quest = str::Compare(prefix,
                                    kQuestVariablePrefix) == 0;
        }
        if (!quest) continue;
        std::string rest = str::Right(name, static_cast<int>(name.size()) - 2);
        int dot = str::Find(rest, kDot);
        if (dot > 0) {
            std::string folder = str::Left(rest, dot);
            RegisterQuests(folder);
        }
    }

    // The server's variables, by stamp.
    if (ClientWorldLocalView(clientWorld)->player->sessionKind == 1) {
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
            std::string name = record->name.data();
            VariableSetBits(variables, record->name.data(), ValueBits(record));
        }
        g_appliedStamp = 0;
    }
    UpdateMultiplayerVariable();
}

void SavedParties::Reload() {
    entries.clear();

    lib::FileFind finder;
    if (!finder.FindFile(folder + kPartyFilePattern)) return;
    bool more;
    do {
        more = finder.FindNext();
        MpHeroRecord read;
        MpHeroRecord* entry = &read;
        entry->file = finder.GetFileName();
        if (entry->ReadFile(folder)) {
            NamedFloatTable* variables = &entry->Party()->variables;
            if (ei::rebirth::Enabled(ei::rebirth::Patch::PersistentGlobalVars)) {
                // Rebirth's PersistentGlobalVars: the clear is skipped and the table is cleared
                // keeping its "gvar." entries.
                ClearVariablesKeepingGlobals(variables);
            } else {
                variables->clear();
            }
            if (std::ssize(entry->Party()->members) == 1) {
                ReserveOne();
                entries.push_back(read);
            }
        }
    } while (more);
}

void GameFlow::BuildZoneTable() {
    std::vector<ZoneEntry>& table = g_zoneTable;
    if (table.end() != table.begin()) return;
    lib::FileFind find;
    int more;
    {
        modfolders::SelectMaps(kQuestFilePattern, modfolders::MapsSite::ZoneTable);
        std::string pattern = MapsFolder() + kQuestFilePattern;
        more = find.FindFile(pattern);
    }
    while (more) {
        more = find.FindNext();
        std::string name = find.GetTitle();
        str::MakeLower(name);
        RegisterQuests(name);
        UnloadQuestTexts();
        std::string map;
        if (MapRecord* record = FindZone(name)) {
            map = record->map;
            const int allod = AllodOfMap(map);
            ZoneEntry& entry = table.emplace_back();
            entry.first = name;
            entry.second = map;
            entry.id = allod;
        }
    }
    find.Close();
}
