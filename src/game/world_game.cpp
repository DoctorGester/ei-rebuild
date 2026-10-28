// The game flow object (the state machine runs on it, Rebirth patches that one): saving a game into
// saves\<slot>, entering a zone. See docs/notes/world.md.
#include "camera.h"
#include "core_refcount.h"
#include "options.h"
#include "player_manager.h"
#include "random.h"
#include "item_db.h"
#include "item_db_text.h"
#include "net_coder.h"
#include "sound.h"
#include "net_session.h"
#include "ui.h"
#include "ui_screens.h"
#include "world.h"
#include "game_flow_data.h"
#include "game_strings.h"

#include "console.h"

#include "core_maths.h"
#include "core_streams.h"
#include "mob_script_decoder.h"
#include "net_classes.h"
#include "net_connection.h"
#include "net_link.h"
#include "net_messages.h"
#include "object.h"
#include "object_tables.h"
#include "party.h"
#include "player_client.h"
#include "ui_cursor.h"
#include "player_messages.h"
#include "unit.h"
#include "unit_db.h"
#include "mod_folders.h"
#include "world_map_files.h"

#include "lib/file_io.h"
#include "lib/containers.h"
#include "lib/static_object.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include "lib/text_case.h"

#include "ei/rebirth.h"
#include "party_manager.h"
#include "party_records.h"
#include "bink_video.h"
#include "game_data.h"
#include "lib/nan_audit.h"
#include "game_singletons.h"
#include "lib/str.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <list>
#include <bit>
#include <vector>

namespace {

using namespace flowdata;

constexpr unsigned kOpenWrite = netlink::FileStream::kOpenWrite;
constexpr unsigned kOpenRead = netlink::FileStream::kOpenRead;
constexpr unsigned kMagicScenario = 0xDEAD;
constexpr unsigned kMagicMission = 0x102;

// BeginMultiplayerGame sets it, a missing zone clears it.

MapRecord* ZoneRecords() { return g_mapDatabase->maps.data(); }
int ZoneRecordCount() { return static_cast<int>(g_mapDatabase->maps.size()); }
MapRecord* FindZone(std::string_view name) { return g_mapDatabase->FindMap(name); }

using netlink::FileStream;

double VariableGet(NamedFloatTable* table, std::string_view name) { return table->Get(name); }
void VariableSet(NamedFloatTable* table, std::string_view name, float value) { table->Set(name, value); }
void ScriptsReset(CMemoryStream* scripts) { scripts->Resize(0); }

struct LocalRecords {
    lib::InPlace<CParty> storage;
    CParty* Party() { return storage.Get(); }
    LocalRecords() { new (Party()) CParty(); }
    ~LocalRecords() { Party()->~CParty(); }
    LocalRecords(const LocalRecords&) = delete;
    LocalRecords& operator=(const LocalRecords&) = delete;
};

// Copies saves\current\<zone>.sav of every visited zone that has to be kept (the current map's
// zones and bz7g) into the slot folder; a zone whose file could not be copied, and every other
// zone, is marked not visited ("zs.<zone>" = 0).
void CopyVisitedZones(const std::string& slotFolder, const std::string& currentFolder, std::string& zone,
                      const std::string& currentMap, NamedFloatTable* variables) {
    for (int i = 0; i < ZoneRecordCount(); ++i) {
        MapRecord* record = ZoneRecords() + i;
        const char* name = record->name.c_str();
        bool visited;
        {
            std::string prefix = kVisitedPrefix;
            std::string variable = prefix + name;
            double value = VariableGet(variables, variable);
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
constexpr std::uint32_t kStartTime = 0x40800000;        // 4.0f
constexpr int kMaxCatchUpTicks = 1000;


// The zone was saved before: read saves\current\<zone>.sav and restore it, then let the world catch up with
// the time spent away (one tick per elapsed unit, at most 1000, waking the units around the entrance).
// Returns LoadSave's result, 0 when the file cannot be opened or is corrupt.
int RestoreZone(CParty* records, MapRecord* zoneRecord, const std::string& zone, std::string& folder,
                const char* mapOverride, FileStream& file, bool& failed) {
    NamedFloatTable* variables = &records->variables_;
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
    CMemoryStream data;
    if (!SaveFileRead(&data, kMagicMission, kSaveVersion, kSaveOldestVersion, &file)) {
        game::FatalError(kCorruptSave, fileName);
        failed = true;
        return 0;
    }
    int result = g_serverWorld->LoadSave(&data, mapOverride);
    file.Close();

    int entrance = records->entrance_;
    if (entrance >= 0 && entrance < std::ssize(zoneRecord->exits)) {
        float now = static_cast<float>(VariableGet(variables, kGTime));
        float left;
        {
            std::string prefix = kLeftPrefix;
            std::string variable = prefix + zone;
            left = static_cast<float>(VariableGet(variables, variable));
        }
        int ticks = static_cast<int>(libcrt::Ftol((static_cast<double>(now) - left) * 100.0f));
        if (ticks >= kMaxCatchUpTicks) ticks = kMaxCatchUpTicks;
        else if (ticks <= 0) return result;
        // The centre of the exit's deploy rectangle.
        const ZoneExit* exit = &zoneRecord->exits[entrance];
        Vector3 point;
        point.x = static_cast<float>((static_cast<double>(exit->deploy[2]) + exit->deploy[0]) * 0.5f);
        point.y = static_cast<float>((static_cast<double>(exit->deploy[3]) + exit->deploy[1]) * 0.5f);
        point.z = 0.0f;
        for (int i = ticks; i != 0; --i) {
            g_serverWorld->WakeUnitsAround(&point, kCatchUpRadius);
            g_serverWorld->Tick(static_cast<char>(1));
        }
    }
    return result;
}

constexpr int kGameResultNetwork = 2;
constexpr int kHostPort = 8000;
constexpr int kServerMessageZone = 8;


bool Active(const CRefCounted* object) { return object && !(object->flags7_ & CRefCounted::kDetached); }

// Tells the clients of the game server where the game is: the host's address and the zone.
void AnnounceHost(const char* zone) {
    char host[100];
    if (gameapi::gethostname(host, sizeof(host)) != 0) return;
    net::NetAddress address{};
    address.Parse(host, kHostPort);
    std::string formatted = address.ToString();
    net::HostMessage* record = net::NewCounted<net::HostMessage>();
    RefPtr& slot = net::g_gameServer->hostInfo_;
    if (record != slot.ptr) {
        slot.Destroy();
        slot.Share(record);
    }
    net::HostMessage* shared = net::g_gameServer->hostInfo_.Get();
    shared->address_ = formatted;
    shared = net::g_gameServer->hostInfo_.Get();
    shared->zone_.assign(zone);
    net::g_gameServer->AnnounceToGuests(kServerMessageZone, zone);
}

// The local client's player record (ClientSession::player_, a LoginMessage): its state_ is the
// game state request to the server (a change goes out with a new version_).
int ServerPlayerCount() { return static_cast<int>(g_serverWorld->players_->objects_.size()); }

CParty* GlobalParty() { return g_savedParties->outsideGame_.Party(); }
constexpr std::uint32_t kMultiplayerHeroId = 0x3B9AF5CE;   // the only record's unit id

constexpr int kCommandBriefing = 1;
constexpr int kRequestDone = 3;   // the client's game state request: answered

void ReleaseSession(CRefCounted* session) {
    if (session && --session->refs_ == 0) delete session;
}

constexpr std::uint32_t kGraphChunk = 0x31415926;         // the AI graph chunk in a .mob file

constexpr std::uint32_t kScriptsChunk = 0xA000;          // the .mob file's scripts chunk
constexpr std::uint32_t kScriptEncoded = 0xACCEECCB;     // in it: the script text, packed and XORed
constexpr std::uint32_t kScriptPlain = 0xACCEECCA;       // in it: the script text as is

constexpr int kServerDropClient = 9;

// The client's game state request: `kind`, a new version.
void RequestKind(int kind) {
    net::g_gameClient->player_.Get()->state_ = kind;
    ++net::g_gameClient->player_.Get()->version_;
}

constexpr std::size_t kPasswordSize = 0x14;

// The zone table: a vector of {first, second, id} entries (docs/notes/standalone.md, "The static
// initialisers' objects").
struct ZoneEntry {
    std::string first;
    std::string second;
    int id;
};
constinit std::vector<ZoneEntry> g_zoneTable = /*007AFDE0*/ {};
bool SameText(std::string_view a, std::string_view b) {
    return str::Compare(a, b) == 0;
}

// A list of entry pointers on the game's heap (the game keeps STLport lists of copies).
struct EntryPicks {
    const ZoneEntry** items = nullptr;
    int count = 0;
    explicit EntryPicks(int capacity) {
        items = new const ZoneEntry*[static_cast<std::size_t>(capacity > 0 ? capacity : 1)];
    }
    ~EntryPicks() { delete[] items; }
    EntryPicks(const EntryPicks&) = delete;
    EntryPicks& operator=(const EntryPicks&) = delete;
    void RemoveAt(int i) {
        for (int k = i + 1; k < count; ++k) items[k - 1] = items[k];
        --count;
    }
};

constexpr unsigned kMagicMissionFile = 0x102;
constexpr int kVersionGlobalsInMission = 0x52;   // below: the globals come from mission.sav
constexpr int kVersionInputInMission = 0x47;     // below: the input state too

// StartZone's modes (also stored as the campaign mode, the server world's sessionKind_).
constexpr int kModeFromRecord = 0;   // by the zone record's kind: 0 -> 1, 1 -> 4, 2 -> 3
constexpr int kModeMainMenu = 2;
constexpr int kModeEndOfGame = 8;    // the zone "endofgame"
// SetGameResult's values here.
constexpr int kResultPlaying = 1;
constexpr int kResultMainMenu = 4;

inline CPlayerClient* ClientWorldLocalView(CWorldClient* client) { return client->LocalView(); }

// `value` mod 24 through the game's _CIfmod, stored as float.
float HoursOfDay(double value) { return static_cast<float>(std::fmod(value, 24.0)); }

constexpr int kStateMainMenu = 1, kStateLoad = 2, kStateNewGame = 3, kStateZone = 4, kStatePartyZone = 5,
              kStateNetworkZone = 6;

// UseResources' function-local static: the resources in use (guard bit 0 of g_lastResourcesGuard),
// destroyed at exit.
constinit lib::StaticObject<std::string> g_lastResources = /*007AFDEC*/ {};
void DestroyLastResources() { g_lastResources.Destroy(); }
std::uint8_t g_lastResourcesGuard = /*007AFDF0*/ {};
const char* ResourcesPath() { return options::g_options->resourcesPath_.c_str(); }

constexpr int kScenarioScriptsVersion = 0x47;      // scenario.sav from this version holds the input and scripts
constexpr int kScenarioGlobalsVersion = 0x52;      // and from this one the globals
void StreamLoadScripts(CMemoryStream* a0, void*, CStream* a2) { a0->ReadSized(a2); }

void DestroyGameFlow() { g_gameFlow->Destruct(); }

unsigned g_serverStamp = /*007AFED0*/ {};   // the newest variable stamp seen
unsigned g_appliedStamp = /*007AFED4*/ {};   // the stamp applied up to

// The variable setter with the value as the raw dword that is pushed (no FPU load on the way).
void VariableSetBits(NamedFloatTable* table, std::string_view name, std::uint32_t bits) { table->Set(name, std::bit_cast<float>(bits)); }
// The value's bits, moved as the dword that is pushed (no FPU load).
std::uint32_t ValueBits(const net::VariableMessage* record) {
    std::uint32_t bits;
    std::memcpy(&bits, &record->value_, 4);
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

int AllodOfMap(const char* map) {
    if (NameInList(kAllodMaps[kGipat], map)) return kGipat;
    if (NameInList(kAllodMaps[kIngos], map)) return kIngos;
    return NameInList(kAllodMaps[kSuslanger], map) ? kSuslanger : 3;
}

} // namespace

// The game's save stream (core_streams.h), zero at the start: built by a static initialiser and destroyed
// by the exit function it registers (docs/notes/standalone.md, "The static initialisers' objects").
constinit lib::StaticObject<CMemoryStream> g_saveStream = /*007AFDF8*/ {};

// A second block: it uses g_saveStream (defined above).
namespace {
void DestroySaveStream() { g_saveStream->~CMemoryStream(); }
} // namespace

void ConstructSaveStream() {
    new (&*g_saveStream) CMemoryStream();
    std::atexit(DestroySaveStream);
}

int GameFlow::SaveGame(const char* slot) {
    FileStream file;
    std::string zone;
    std::string slotFolder;
    std::string currentFolder;
    if (g_serverWorld->gameResult_ != 1) return 0;

    g_cursor->SetShape(3);
    slotFolder = str::Format(kSlotFolder, DataFolderText(), slot);
    currentFolder = str::Format(kCurrentFolder, DataFolderText());
    ClearSaveFolder(slotFolder);
    if (g_serverWorld->sessionKind_ == 4) {
        if (CPlayer* player = g_serverWorld->GetPlayer(0))
            g_partyManager->BeginRedeploy(player->Party());
    }
    LocalRecords records;
    g_partyManager->EndRedeploy(records.Party());
    std::string currentMap;
    currentMap = FindZone(records.Party()->zone_)->allod;

    CopyVisitedZones(slotFolder, currentFolder, zone, currentMap, &records.Party()->variables_);
    g_partyManager->BeginRedeploy(records.Party());

    bool opened;
    {
        std::string path = slotFolder + kScenarioFile;
        opened = file.Open(path, kOpenWrite, 0);
    }
    if (!opened) {
        g_interface->closeAll_ = 1;
        StartZone(kMainMenu, 2);
        return 0;
    }

    // scenario.sav: the party, the input state, the scripts and the globals.
    CMemoryStream scenario;
    scenario.version_ = static_cast<std::uint32_t>(kSaveVersion);
    g_partyManager->Save(&scenario);
    g_interface->SaveState(&scenario);
    ScriptsReset(&g_saveStream.Get());
    g_interface->SaveWindowState();
    scenario.WriteSized(&g_saveStream.Get());
    ScriptsReset(&g_saveStream.Get());
    g_camera->Save(&scenario);
    SaveFileWrite(&file, kMagicScenario, kSaveVersion, &scenario);
    file.Close();

    // mission.sav: the world, except in zones without one (kind 1: cutscenes).
    int kind = FindZone(records.Party()->zone_)->kind;
    if (kind == 0 || kind == 2) {
        {
            std::string path = slotFolder + kMissionFile;
            opened = file.Open(path, kOpenWrite, 0);
        }
        if (!opened) return 0;
        CMemoryStream mission;
        mission.version_ = static_cast<std::uint32_t>(kSaveVersion);
        g_serverWorld->SaveWorld(&mission);
        SaveFileWrite(&file, kMagicMission, kSaveVersion, &mission);
        file.Close();
    }

    g_cursor->SetShape(0);
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
    std::string zone(records->zone_);
    MapRecord* zoneRecord = FindZone(zone);
    if (!zoneRecord) return 0;

    NamedFloatTable* variables = &records->variables_;
    if (static_cast<int>(libcrt::Ftol(VariableGet(variables, kMovieVariable))) == 1) {
        if (str::Compare(zone,
                            kMovieZone) != 0) {
            {
                std::string movie = *g_discFolder + kMovieFile;
                PlayMovie(movie, 0, 1, 1, 1, 1, 1);
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
            double frozen = VariableGet(variables, kFrozenVariable);
            mapOverride = kFrozenMap;
            EI_NAN_AUDIT(frozen);
            if (frozen == 0.0f) mapOverride = kThawedMap;
        }
        bool visited;
        {
            std::string prefix = kVisitedPrefix;
            std::string variable = prefix + zone;
            double value = VariableGet(variables, variable);
            EI_NAN_AUDIT(value);
            visited = value != 0.0f;
        }
        if (visited) {
            bool failed = false;
            result = RestoreZone(records, zoneRecord, zone.c_str(), folder, mapOverride, file, failed);
            if (failed) return 0;
        } else {
            std::string map = zoneRecord->map + kMapExtension;
            if (mapOverride) map = mapOverride;
            bool loaded;
            {
                std::string mob = zoneRecord->mob + kMobExtension;
                loaded = g_serverWorld->NewZone(map, mob, zoneRecord->name.c_str()) != 0;
            }
            if (!loaded) return 0;
            zoneRecord = FindZone(zone);
            g_serverWorld->SetTime(std::bit_cast<float>(kStartTime));
        }
        if (zoneRecord->kind == 2) {
            std::string scripts = zoneRecord->mob + kMobExtension;
            g_serverWorld->LoadZoneScripts(scripts.c_str());
        } else {
            ScriptResetZone(g_serverWorld->MainScript());   // the scripts reset for a zone without its own
        }
    }
    g_serverWorld->SetZoneExits(&zoneRecord->exits);
    return result;
}

// Starts network zone `zone` (a zone record of kind 0): tears the current zone down, restores
// saves\current\<zone>.sav when there is one (else loads the zone fresh at time 4), copies its exits with
// every target set to "MPGame1", applies its two settings, marks the game a network one and, on a running
// game server, announces the host and loads the zone's .mob. Returns 1, 0 on failure.
int StartNetworkZone(const char* zone) {
    g_interface->closeAll_ = 1;
    MapRecord* zoneRecord = FindZone(zone);
    if (!zoneRecord || zoneRecord->kind != 0) return 0;
    g_serverWorld->TeardownZone();

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
        CMemoryStream data;
        bool loaded = SaveFileRead(&data, kMagicMission, kSaveVersion, kSaveOldestVersion,
                                   &file) &&
                      g_serverWorld->LoadSave(&data, nullptr) != 0;
        if (loaded) file.Close();
        if (!loaded) return 0;
    } else {
        bool loaded;
        {
            std::string mob = zoneRecord->mob + kMobExtension;
            std::string map = zoneRecord->map + kMapExtension;
            loaded = g_serverWorld->NewZone(map, mob, zone) != 0;
        }
        if (!loaded) return 0;
        g_serverWorld->SetTime(std::bit_cast<float>(kStartTime));
    }

    // The zone's exits, all leading to "MPGame1".
    std::vector<ZoneExit> exits = zoneRecord->exits;
    for (ZoneExit& exit : exits) exit.target = kNetworkExitTarget;
    g_serverWorld->SetZoneExits(&exits);
    g_serverWorld->SetClimate(zoneRecord->climate);
    g_serverWorld->SetCave(zoneRecord->cave);
    g_serverWorld->sessionKind_ = 1;
    g_serverWorld->SetGameResult(kGameResultNetwork);
    options::g_options->drawVisibleOnly_ = 1;

    if (Active(net::g_gameServer)) {
        AnnounceHost(zone);
        std::string name = zone;
        std::string mob = name + kMobExtension;
        g_serverWorld->LoadMobFile(mob.c_str());
    }
    return 1;
}

std::string* CWorldServer::ZoneNameInto(std::string* out) {
    *out = zoneName_;
    return out;
}

// The counterpart of the zone load: saves the current zone into saves\current\<zone>.sav (SaveWorld, header
// 0x102) and records in the party's variables that it was visited (zs.<zone> = 1) and when it was left
// (zt.<zone> = gtime). Returns 1, 0 when the file cannot be written (after FatalError).
int LeaveZone(CParty* records) {
    FileStream file;
    std::string path;
    std::string zone;
    g_serverWorld->ZoneNameInto(&zone);
    // `path` must be destroyed after `zone`: it is declared first.

    if (g_serverWorld->sessionKind_ == 1) g_camera->SaveView(7);
    else if (g_serverWorld->sessionKind_ == 3) g_camera->SaveView(6);
    path = str::Format(kCurrentFolder, DataFolderText());
    {
        std::string fileName = zone + kSaveExtension;
        path += fileName;
    }
    if (!file.Open(path, kOpenWrite, 0)) {
        game::FatalError(kCantWrite, path);
        return 0;
    }
    g_serverWorld->PrepareSave();
    CMemoryStream data;
    data.version_ = static_cast<std::uint32_t>(kSaveVersion);
    g_serverWorld->SaveWorld(&data);
    SaveFileWrite(&file, kMagicMission, kSaveVersion, &data);
    file.Close();

    NamedFloatTable* variables = &records->variables_;
    {
        std::string prefix = kVisitedPrefix;
        std::string variable = prefix + zone.c_str();
        VariableSet(variables, variable, 1.0f);
    }
    float now = static_cast<float>(VariableGet(variables, kGTime));
    {
        std::string prefix = kLeftPrefix;
        std::string variable = prefix + zone;
        VariableSet(variables, variable, now);
    }
    return 1;
}

// ---- The multiplayer flag -----------------------------------------------------------------------

void GameFlow::UpdateMultiplayerVariable() {
    CPlayer* player = g_clientWorld->LocalView()->player_;
    NamedFloatTable* variables = &player->Party()->variables_;
    if (!Active(net::g_gameClient)) return;
    auto set = [variables](bool on) {
        if (on) VariableSet(variables, kMultiplayerVariable, 1.0f);
        else variables->Remove(kMultiplayerVariable);
    };
    if (!Active(net::g_gameServer)) {
        set(!Active(net::g_gameClient->info_));
        return;
    }
    if (zoneSwitchStarted_) set(ServerPlayerCount() > 1);
    else set(!Active(net::g_gameClient->map_));
}

// ---- Starting a multiplayer game ------------------------------------------------------------------

std::string* GameFlow::MultiplayerZoneInto(std::string* out) {
    unsigned type = net::g_gameClient->gameType_;
    *out = kMultiplayerZones[type <= 3 ? type : 0]; return out;
}

void GameFlow::BeginMultiplayerGame() {
    PartyManagerNewGameFour(&g_partyManager.Get());
    if (std::ssize(GlobalParty()->members_) == 1) GlobalParty()->members_.data()->unitId = kMultiplayerHeroId;
    {
        std::string zone;
        GlobalParty()->zone_ = *MultiplayerZoneInto(&zone);
    }
    VariableSet(&GlobalParty()->variables_, kMultiplayerVariable, 1.0f);
    g_partyManager->BeginRedeploy(GlobalParty());
    {
        std::string zone;
        MultiplayerZoneInto(&zone);
        state_ = 4;   // load the zone
        requestTarget_ = zone.c_str();
        requestArg_ = 0;
    }
    zoneSwitchStarted_ = 0;
    net::g_gameClient->player_.Get()->state_ = 1;
    ++net::g_gameClient->player_.Get()->version_;
    g_interface->disconnected_ = 1;
}

// ---- Small glue of the flow -----------------------------------------------------------------------

void GameFlow::ReturnLocalParty() {
    if (!Active(net::g_gameClient)) return;
    CPlayer* player = g_clientWorld->LocalView()->player_;
    CParty* party = player->Party();
    GlobalParty()->Assign(party);
    g_partyManager->BeginRedeploy(party);
    // The global party record written into the heroes' folder.
    g_savedParties->outsideGame_.WriteFile(g_savedParties->folder_.c_str());
    UpdateFromServer();
}

void GameFlow::UpdateAndAcknowledge() {
    if (!Active(net::g_gameClient)) return;
    UpdateFromServer();
    if (net::g_gameClient->player_.Get()->state_ != kRequestDone) return;
    net::g_gameClient->player_.Get()->state_ = 0;
    ++net::g_gameClient->player_.Get()->version_;
}

void GameFlow::RequestBriefing(const char* briefing) {
    net::ClientSession* client = net::g_gameClient;
    if (Active(client)) client->SendCommand(kCommandBriefing, briefing);
}

void GameFlow::RecreateClientSession() {
    net::ClientSession*& global = net::g_gameClient;
    net::ClientSession* old = global;
    global = nullptr;
    ReleaseSession(old);
    net::ClientSession* session = net::NewCounted<net::ClientSession>();
    net::ClientSession* previous = global;
    global = session;
    if (session) ++session->refs_;
    ReleaseSession(previous);
}

int GameFlow::SaveGameThunk(const char* slot) { return SaveGame(slot); }

int RequestZone(std::string_view zone, int entrance) {
    g_interface->closeAll_ = true;
    // The local view asks to go to a zone (name, entrance).
    return g_clientWorld->LocalView()->RequestZone(zone, entrance);
}

void GameFlow::DropSessions() {
    g_interface->disconnected_ = 0;
    net::ServerSession*& server = net::g_gameServer;
    net::ServerSession* oldServer = server;
    server = nullptr;
    ReleaseSession(oldServer);
    net::ClientSession*& client = net::g_gameClient;
    net::ClientSession* oldClient = client;
    client = nullptr;
    ReleaseSession(oldClient);
}

void GameFlow::EndSession() {
    DropSessions();
    UseResources(false);
}

void GameFlow::CloseSessions() {
    g_interface->disconnected_ = 0;
    net::ServerSession*& server = net::g_gameServer;
    net::ServerSession* oldServer = server;
    server = nullptr;
    ReleaseSession(oldServer);
    net::ClientSession*& client = net::g_gameClient;
    net::ClientSession* oldClient = client;
    client = nullptr;
    ReleaseSession(oldClient);
    state_ = 1;
}

// The flow's requests: the state its update acts on next, the zone or save name, and a party for states 5
// and 6.
void GameFlow::RequestState3() { state_ = 3; }

void GameFlow::RequestLoad(const char* name, char flag) {
    state_ = 2;
    requestTarget_ = name;
    requestArg_ = flag != 0;
}

void GameFlow::RequestZoneChange(std::string_view zone, int entrance) {
    state_ = 4;
    requestTarget_ = zone;
    requestArg_ = entrance;
}

void GameFlow::RequestPartyZone5(const char* zone, CParty* party) {
    state_ = 5;
    requestTarget_ = zone;
    RequestParty()->Assign(party);
}

void GameFlow::RequestPartyZone6(const char* zone, CParty* party) {
    state_ = 6;
    requestTarget_ = zone;
    RequestParty()->Assign(party);
}

void GameFlow::DisconnectClient() { g_clientWorld->DisconnectLocalPlayer(); }

int CWorldServer::ReadAIGraph(std::string_view map, std::string_view mob, CStream* out) {
    FileStream mobFile;
    int found = 0;
    // The graph chunk of the .mob file.
    {
        std::string path;
        MapPath(&path, mob);
        mobFile.Open(path, kOpenRead, 1);
    }
    if (mobFile.FindTagged(kGraphChunk, out)) found = 1;
    mobFile.Close();
    if (found) return found;
    // Otherwise <map without extension>.grp.
    std::string base;
    MapPath(&base, map);
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

void CWorldServer::LoadZoneScripts(const char* mob) {
    FileStream file;
    std::string script;
    CMemoryStream scripts;
    CMemoryStream text;
    MobScriptDecoder decoder;
    {
        std::string path;
        MapPath(&path, mob);
        file.Open(path, kOpenRead, 1);
    }
    file.FindTagged(kScriptsChunk, &scripts);
    if (scripts.FindTagged(kScriptEncoded, &text)) {
        const std::uint8_t* data = text.data_;
        std::uint32_t size = text.size_;
        char zero = 0;
        decoder.input.resize(size, zero);
        std::memmove(decoder.input.data(), data, size);
        decoder.Decode(&script);
    } else if (scripts.FindTagged(kScriptPlain, &text)) {
        text.Seek(text.size_);
        char zero = 0;
        text.Write(&zero, 1u);
        script = reinterpret_cast<const char*>(text.data_);
    }
    ScriptReset(MainScript());
    MainScript()->Load(&script, 1);
}

void GameFlow::SwitchMultiplayerZone() {
    if (zoneSwitchStarted_) {
        // A switch is pending: ask for the start zone again.
        RequestKind(1);
        bool host = Active(net::g_gameServer);
        {
            std::string zone;
            MultiplayerZoneInto(&zone);
            g_interface->closeAll_ = 1;
            g_clientWorld->LocalView()->RequestZone(zone, 0);
        }
        if (host) {
            // The host sends command 9 with the announced zone and drops the announcement.
            net::HostMessage* announced = net::g_gameServer->hostInfo_.Get();
            if (Active(announced)) net::g_gameServer->AnnounceToGuests(kServerDropClient, announced->zone_.data());
            RefPtr& handle = net::g_gameServer->hostInfo_;
            if (handle.ptr) {
                handle.Destroy();
                handle.Share(nullptr);
            }
        }
        zoneSwitchStarted_ = 0;
        return;
    }

    // Every connection of the host drops what it has read of the incoming reliable stream
    // (flags 6, high and the cursor back to the start).
    net::ServerSession* server = net::g_gameServer;
    if (Active(server)) {
        for (auto& connection : server->clients_) {
            GameBuffer& in = connection.Get()->reliableIn_;
            std::uint8_t* begin = in.begin;
            in.flags = 6;
            in.high = begin;
            in.cur = begin;
        }
    }
    LocalRecords records;
    CPlayer* player = g_serverWorld->GetPlayer(0);
    if (player && player->Server()->partyDeployed_) {
        g_serverWorld->SyncPartyRecords(player);
        records.Party()->Assign(player->Party());
        g_serverWorld->ClearPlayerItems(player);
    } else {
        records.Party()->Assign(GlobalParty());
    }
    records.Party()->zone_ = multiplayerZone_;
    RequestKind(3);
    if (Active(net::g_gameServer)) {
        state_ = 6;
        requestTarget_ = multiplayerZone_;
        RequestParty()->Assign(records.Party());
    } else {
        net::HostMessage* host = net::g_gameClient->info_;
        if (Active(host)) records.Party()->zone_ = host->zone_.data();
        host = net::g_gameClient->info_;
        if (Active(host)) {
            state_ = 5;
            requestTarget_ = host->address_.data();
            RequestParty()->Assign(records.Party());
        }
    }
    zoneSwitchStarted_ = 1;
}

void GameFlow::StartServer(const char* name, int maxPlayers, int gameType, const char* password, char unlisted,
                           int masterKey) {
    BuildZoneTable();
    net::g_netLog = &console::g_netLogOut;
    {
        net::ServerSession* server = net::NewCounted<net::ServerSession>(gameType);
        AssignCounted(net::g_gameServer, server);
    }
    {
        net::ClientSession* client = net::NewCounted<net::ClientSession>();
        AssignCounted(net::g_gameClient, client);
    }
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
    net::g_gameClient->player_.Get()->host_ = 1;
    const char* shown = name && *name ? name : kServerName;
    net::g_gameServer->name_.assign(shown);
    net::g_gameServer->maxPlayers_ = maxPlayers;
    if (password && *password) net::g_gameServer->SetPassword(password);
    net::g_gameServer->SetPublic(unlisted == 0);
    net::g_gameClient->gameType_ = gameType;
    net::g_gameServer->masterKey_ = masterKey;
    AfterServerStart();
}

void GameFlow::StartClient() {
    net::g_netLog = &console::g_netLogOut;
    AssignCounted(net::g_gameServer, static_cast<net::ServerSession*>(nullptr));
    net::ClientSession* client = net::NewCounted<net::ClientSession>();
    AssignCounted(net::g_gameClient, client);
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

void CWorldServer::AcceptNewPlayers(CPlayerSource* lobby) {
    std::list<netlink::NetLink*> joined;
    if (gameResult_ == 0) return;
    lobby->TakeNew(&joined);
    for (auto node = joined.begin(); node != joined.end();) {
        auto next = std::next(node);
        netlink::NetLink* connection = *node;
        CPlayer* player = players_->CreateSessionPlayer();
        player->Connect(connection);
        player->dirty_ = 0xFFFFFFFFu;
        player->sessionKind_ = sessionKind_;
        std::memcpy(&player->Server()->lastPosition_, &startPosition_, 12);
        int id = player->playerId_;
        console::EndLine(console::g_netLogOut << kAcceptedPlayer << id);
        node = next;
    }
    // The list's destructor: every reference released, then the blocks.
    for (auto node = joined.begin(); node != joined.end(); ++node) ReleaseSession(*node);
    joined.clear();
}

void DestroyZoneTable() { lib::FreeStorage(g_zoneTable); }

void PickZones(std::vector<std::string>* out, int id, const char* exclude) {
    const std::vector<ZoneEntry>& table = g_zoneTable;
    int total = static_cast<int>(table.size());
    EntryPicks pending(total);
    EntryPicks group(total);
    for (const ZoneEntry* e = table.data(); e != table.data() + table.size(); ++e)
        if (e->id == id) pending.items[pending.count++] = e;
    while (pending.count) {
        // The group of the first pending entry's second name, in table order.
        const char* key = pending.items[0]->second.c_str();
        group.count = 0;
        for (int i = 0; i < pending.count;) {
            if (SameText(pending.items[i]->second, key)) {
                group.items[group.count++] = pending.items[i];
                pending.RemoveAt(i);
            } else {
                ++i;
            }
        }
        // With a choice, never the excluded first name.
        if (group.count > 1)
            for (int i = 0; i < group.count;)
                if (SameText(group.items[i]->first, exclude)) group.RemoveAt(i);
                else ++i;
        unsigned pick = ((&g_random))->Next() % static_cast<unsigned>(group.count);
        std::string name(group.items[pick]->first.c_str());
        out->push_back(name);
    }
}

int LoadMission(CStream* file) {
    g_clientWorld->sounds_->StopAll(1);
    CMemoryStream mission;
    if (!SaveFileRead(&mission, kMagicMissionFile, kSaveVersion,
                      kSaveOldestVersion, file))
        return 0;
    int version = static_cast<int>(mission.version_);
    if (version < kVersionGlobalsInMission) g_camera->Load(&mission);
    if (static_cast<int>(mission.version_) < kVersionInputInMission) {
        // Old saves: the input state, then the rest goes to the save stream.
        g_saveStream->Resize(0);
        g_interface->LoadState(&mission);
        mission.ReadSized(&g_saveStream.Get());
        g_saveStream->version_ = mission.version_;
    }
    return g_serverWorld->LoadSave(&mission, nullptr);
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

bool NameInList(const char* const* list, const char* name) {
    for (; *list[0] != 0; ++list)
        if (text::CompareIgnoringCase(*list, name) == 0) return true;
    return false;
}

void GameFlow::StartZone(std::string_view zone, int mode) {
    g_cursor->SetShape(3);
    MapRecord* zoneRecord = nullptr;
    g_interface->closeAll_ = 1;
    if (text::CompareIgnoringCase(zone, kEndOfGame) == 0) {
        mode = kModeEndOfGame;
    } else {
        zoneRecord = FindZone(zone);
    }
    g_serverWorld->TeardownZone();

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
            net::ServerSession*& server = net::g_gameServer;
            net::ServerSession* oldServer = server;
            g_interface->disconnected_ = 0;
            server = nullptr;
            ReleaseSession(oldServer);
            net::ClientSession*& client = net::g_gameClient;
            net::ClientSession* oldClient = client;
            client = nullptr;
            ReleaseSession(oldClient);
        }
        mode = kModeMainMenu;
    }

    g_serverWorld->SetTimeSpeed(static_cast<float>(options::g_options->defaultTimeScale_));
    int result;
    if (mode == kModeMainMenu) {
        g_serverWorld->NewZone(kMenuMap, kMenuMob, std::nullopt);
        // The menu shows the local time of day.
        SYSTEMTIME now;
        gameapi::GetLocalTime(&now);
        int minute = now.wMinute;
        int hour = now.wHour;
        g_serverWorld->SetTime(static_cast<float>(minute) * 0.016666668f + static_cast<float>(hour));
        result = kResultMainMenu;
        // Rebirth's AllowScriptsInMainMenu: the first script command queue runs in the menu too.
        if (ei::rebirth::Enabled(ei::rebirth::Patch::AllowScriptsInMainMenu)) g_serverWorld->scriptsActive_ = 1;
    } else if (mode == kModeEndOfGame) {
        result = kResultPlaying;
    } else {
        // The party enters the zone, then the clock is set from its "gtime".
        LocalRecords records;
        g_partyManager->EndRedeploy(records.Party());
        EnterZone(records.Party());
        g_partyManager->BeginRedeploy(records.Party());
        g_serverWorld->SetTime(HoursOfDay(VariableGet(&records.Party()->variables_, kGTime)));
        if (zoneRecord) {
            g_serverWorld->SetClimate(zoneRecord->climate);
            g_serverWorld->SetCave(zoneRecord->cave);
        }
        result = kResultPlaying;
    }

    g_serverWorld->sessionKind_ = mode;
    if (mode == 1) options::g_options->drawVisibleOnly_ = mode;
    g_serverWorld->SetGameResult(result);
    // The server world's player source handed to the client world.
    g_clientWorld->SetPlayer(g_serverWorld->worldHandle_.Get());
    ClientWorldLocalView(&g_clientWorld.Get())->sendParty_ = 0;
}

void GameFlow::StartNewGame() {
    // Rebirth's AutoExec #2 (the call redirected): addon's AutoExec lines first.
    if (ei::rebirth::Enabled(ei::rebirth::Patch::AutoExec)) console::RunAddonAutoExec();
    console::Execute(kExecAutoexec);
    g_interface->closeAll_ = 1;
    PartyManagerNewGame(&g_partyManager.Get());
    std::string folder;
    folder = str::Format(kCurrentFolder, DataFolderText());
    ClearSaveFolder(folder);
    LocalRecords records;
    g_partyManager->EndRedeploy(records.Party());
    StartZone(records.Party()->zone_, 0);
}

// ---- The flow's requests -------------------------------------------------------------

void SwitchResourceDatabases(const char* name) {
    UnitDatabase* units = &g_unitDatabase.Get();
    units->Reset();
    units->Load(ResourcesPath(), name);
    units->CountSoundFiles(g_clientWorld->sounds_);
    g_skillDatabase->Reset();
    g_skillDatabase->Load(ResourcesPath(), name);
    g_spellDatabase->Reset();
    g_spellDatabase->Load(ResourcesPath(), name);
    g_itemDatabase->Reset();
    g_itemDatabase->Load(ResourcesPath(), name);
}

void GameFlow::UseResources(bool network) {
    if (!(g_lastResourcesGuard & 1)) {
        g_lastResourcesGuard |= 1;
        g_lastResources.Construct(kResourcesSingle);
        std::atexit(DestroyLastResources);
    }
    std::string wanted = network ? kResourcesNetwork : kResourcesSingle;
    if (str::Compare(wanted,
                        g_lastResources->c_str()) == 0)
        return;
    *g_lastResources = wanted;
    SwitchResourceDatabases(wanted.c_str());
    if (network) {
        int value = options::g_options->difficulty_;
        options::g_options->difficulty_ = 0;
        options::g_options->difficultyStored_ = value;
    } else {
        options::g_options->difficulty_ = options::g_options->difficultyStored_;
    }
}

void GameFlow::ProcessRequest() {
    int& state = state_;
    if (state == 0) return;
    // The zone being left is saved (single player campaigns 1 and 3, or the host in campaign 1).
    if (g_serverWorld->gameResult_ == 1 && (g_serverWorld->sessionKind_ == 1 || g_serverWorld->sessionKind_ == 3) && state == kStateZone) {
        LocalRecords records;
        g_partyManager->EndRedeploy(records.Party());
        ::LeaveZone(records.Party());
        g_partyManager->BeginRedeploy(records.Party());
    }
    if (g_serverWorld->gameResult_ == 2 && g_serverWorld->sessionKind_ == 1 && state == kStateZone) {
        LocalRecords records;
        ::LeaveZone(records.Party());
    }
    g_interface->CloseAll();
    g_serverWorld->TeardownZone();
    UseResources(Active(net::g_gameClient));

    auto* clientWorld = &g_clientWorld.Get();
    switch (state) {
    case kStateMainMenu:
        g_interface->closeAll_ = 1;
        StartZone(kMainMenu, 2);
        break;
    case kStateLoad:
        clientWorld->SetPlayer(g_serverWorld->worldHandle_.Get());
        ClientWorldLocalView(clientWorld)->sendParty_ = 0;
        g_serverWorld->SetGameResult(1);
        clientWorld->Segment();
        g_serverWorld->Update();
        clientWorld->Segment();
        g_serverWorld->Update();
        LoadGame(requestTarget_.c_str());
        if (requestArg_) {
            const char* autoplay = kAutoplay;
            if (autoplay) {
                ClientWorldLocalView(clientWorld)->StartRecording(autoplay);
                g_serverWorld->sendsStartPosition_ = 1;
            } else {
                ClientWorldLocalView(clientWorld)->StopRecording();
                g_serverWorld->sendsStartPosition_ = 0;
            }
        }
        break;
    case kStateNewGame:
        // The same steps as the matching start function (inlined here), Rebirth's AutoExec #3 included.
        StartNewGame();
        break;
    case kStateZone:
        StartZone(requestTarget_, 0);
        break;
    case kStatePartyZone: {
        const char* zone = requestTarget_.c_str();
        CPlayer* player = ClientWorldLocalView(clientWorld)->player_;
        player->AssignParty(RequestParty());
        g_serverWorld->TeardownZone();
        if (!clientWorld->JoinGameClient(zone)) {
            if (Active(net::g_gameClient)) {
                BeginMultiplayerGame();
            } else {
                g_interface->closeAll_ = 1;
                StartZone(kMainMenu, 2);
            }
        }
        ClientWorldLocalView(clientWorld)->sendParty_ = 1;
        break;
    }
    case kStateNetworkZone: {
        StartNetworkZone(requestTarget_.c_str());
        CPlayer* player = ClientWorldLocalView(clientWorld)->player_;
        player->AssignParty(RequestParty());
        clientWorld->SetPlayer(g_serverWorld->worldHandle_.Get());
        ClientWorldLocalView(clientWorld)->sendParty_ = 1;
        break;
    }
    default:
        break;
    }
    state = 0;
    net::Pump();
}

int GameFlow::LoadGame(const char* slot) {
    if (g_serverWorld->gameResult_ == 2 || g_serverWorld->gameResult_ == 3) return 0;
    // Rebirth's AutoExec #1 (the call redirected): addon's AutoExec lines first.
    if (ei::rebirth::Enabled(ei::rebirth::Patch::AutoExec)) console::RunAddonAutoExec();
    console::Execute(kExecAutoexec);
    g_cursor->SetShape(3);
    g_interface->closeAll_ = 1;
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
        g_interface->closeAll_ = 1;
        StartZone(kMainMenu, 2);
        return 0;
    }

    // scenario.sav: the party, then (by version) the input, the scripts and the globals.
    CMemoryStream scenario;
    if (!SaveFileRead(&scenario, kMagicScenario, kSaveVersion, kSaveOldestVersion, &file)) {
        g_interface->closeAll_ = 1;
        StartZone(kMainMenu, 2);
        return 0;
    }
    g_partyManager->Load(&scenario);
    int version = static_cast<int>(scenario.version_);
    if (version >= kScenarioScriptsVersion) {
        ScriptsReset(&g_saveStream.Get());
        g_interface->LoadState(&scenario);
        StreamLoadScripts(&scenario, nullptr, &g_saveStream.Get());
        version = static_cast<int>(scenario.version_);
        g_saveStream->version_ = static_cast<std::uint32_t>(version);
    }
    if (version >= kScenarioGlobalsVersion) g_camera->Load(&scenario);
    file.Close();

    // The visited zones of the current map (and bz7g) come back into saves\current; every other
    // zone, and one whose file could not be copied, is marked not visited.
    LocalRecords records;
    g_partyManager->EndRedeploy(records.Party());
    std::string currentMap;
    currentMap = FindZone(records.Party()->zone_)->allod;
    NamedFloatTable* variables = &records.Party()->variables_;
    for (int i = 0; i < ZoneRecordCount(); ++i) {
        MapRecord* record = ZoneRecords() + i;
        bool visited;
        {
            std::string prefix = kVisitedPrefix;
            std::string variable = prefix + record->name;
            double value = VariableGet(variables, variable);
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
    g_partyManager->BeginRedeploy(records.Party());

    // The zone: kinds 0 and 2 come back from mission.sav, the others start afresh.
    MapRecord* zoneRecord = FindZone(records.Party()->zone_);
    int kind = zoneRecord->kind;
    if (kind != 0 && kind != 2) {
        StartZone(records.Party()->zone_, 0);
    } else {
        {
            std::string path = slotFolder + kMissionFile;
            opened = file.Open(path, kOpenRead, 1);
        }
        if (!opened) return 0;
        LoadMission(&file);
        file.Close();
        g_serverWorld->SetZoneExits(&zoneRecord->exits);
    }
    console::EndLine(console::g_consoleOut << kLoadedText << slot);
    return 1;
}

GameFlow* GameFlow::Construct() {
    std::construct_at(&requestTarget_);
    new (RequestParty()) CParty();
    std::construct_at(&multiplayerZone_);
    state_ = 0;
    return this;
}

void GameFlow::Destruct() {
    std::destroy_at(&multiplayerZone_);
    RequestParty()->~CParty();
    std::destroy_at(&requestTarget_);
}

// The flow, zero at the start: built by a static initialiser and destroyed by the exit function it
// registers (docs/notes/standalone.md, "The static initialisers' objects").
constinit lib::StaticObject<GameFlow> g_gameFlow = /*007AFE20*/ {};

void ConstructGameFlow() {
    g_gameFlow->Construct();
    std::atexit(DestroyGameFlow);
}

void GameFlow::SetNetworkZone(const char* zone) {
    std::string folder;
    folder = str::Format(kCurrentFolder, DataFolderText());
    ClearSaveFolder(folder);
    multiplayerZone_ = zone ? zone : "";   // null (LeaveZone): no zone
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
    NamedFloatTable* variables = &GlobalParty()->variables_;
    if (ei::rebirth::Enabled(ei::rebirth::Patch::PersistentGlobalVars))
        ClearVariablesKeepingGlobals(variables);
    else
        variables->clear();
    GlobalParty()->RemoveQuestItems();
    // The client's player record gets the hero's and the party's names (or the defaults).
    if (std::ssize(GlobalParty()->members_) == 1) {
        const std::string* names = GlobalParty()->members_.data()->texts;
        net::g_gameClient->player_.Get()->text_.assign(names[5].c_str());
        net::g_gameClient->player_.Get()->name_.assign(names[0].c_str());
    } else {
        net::g_gameClient->player_.Get()->text_.assign(kDefaultHeroName);
        net::g_gameClient->player_.Get()->name_.assign(kDefaultPartyName);
    }
    net::g_gameClient->player_.Get()->state_ = 1;
}

void GameFlow::UpdateFromServer() {
    if (!Active(net::g_gameClient)) return;
    if (CTextManager* texts = g_clientWorld->texts_) {
        for (const std::string* file = g_questFiles.data(); file != g_questFiles.data() + g_questFiles.size(); ++file)
            texts->RemoveSource(*file);
        g_questFiles.clear();
    }
    auto* clientWorld = &g_clientWorld.Get();
    CPlayer* player = ClientWorldLocalView(clientWorld)->player_;
    NamedFloatTable* variables = &player->Party()->variables_;
// The local player's variables are replaced by the server's; "constr.current" survives. Rebirth's
// PersistentGlobalVars keeps the "gvar." ones.
    float construction = static_cast<float>(VariableGet(variables, kConstructionVariable));
    NamedFloatTable unusedCopy(*variables);
    if (ei::rebirth::Enabled(ei::rebirth::Patch::PersistentGlobalVars))
        ClearVariablesKeepingGlobals(variables);
    else
        variables->clear();
    VariableSet(variables, kConstructionVariable, construction);
    if (net::MapMessage* map = net::g_gameClient->map_) {
        if (Active(map)) LoadZoneQuests(map->text_.data());
    }

    // The game type's construction variable and briefings, at least 1.
    std::string briefing;
    unsigned type = net::g_gameClient->gameType_;
    if (type <= 3) {
        VariableSet(variables, kTypeVariables[type], 1.0f);
        briefing = kTypeBriefings[type];
    }
    std::string questVariable;
    std::string kVariable;
    BriefingVariableInto(&questVariable, (briefing + kBriefingSuffixQuest).c_str());
    BriefingVariableInto(&kVariable, (briefing + kBriefingSuffixK).c_str());
    std::string* const names[2] = {&questVariable, &kVariable};
    for (std::string* name : names) {
        float value = static_cast<float>(VariableGet(variables, (*name)));
        // The larger of the value and 1.0 (a NaN is kept).
        std::uint32_t bits;
        std::memcpy(&bits, &value, 4);
        bool keep = (bits & 0x7F800000u) == 0x7F800000u && (bits & 0x7FFFFFu) != 0 ? true : 1.0f < value;
        VariableSet(variables, (*name), keep ? value : 1.0f);
    }

    // The briefings the server says were seen.
    for (const auto& zone : net::g_gameClient->zones_) MarkBriefing(zone.Get()->text_.data());
    // The quest variables ("q.<folder>.<name>") register their quest folders.
    for (const auto& variable : net::g_gameClient->variables_) {
        std::string name = variable.Get()->name_.data();
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
            RegisterQuests(folder.c_str());
        }
    }

    // The server's variables, by stamp.
    if (ClientWorldLocalView(clientWorld)->player_->sessionKind_ == 1) {
        unsigned previous = g_appliedStamp;
        if (g_serverStamp > g_appliedStamp + 2) g_appliedStamp = g_serverStamp + 1;
        std::vector<const net::VariableMessage*> changed;
        for (const auto& variable : net::g_gameClient->variables_) {
            const unsigned stamp = variable.Get()->serial_;
            if (g_serverStamp <= stamp) g_serverStamp = stamp;
            if (stamp >= g_appliedStamp || stamp < previous) continue;
            changed.push_back(variable.Get());
        }
        // By stamp (the original's STLport sort). The server gives every set its own serial, so no two
        // variables share a stamp and any sort gives this order.
        std::stable_sort(changed.begin(), changed.end(),
                         [](const net::VariableMessage* a, const net::VariableMessage* b) { return a->serial_ < b->serial_; });
        for (const net::VariableMessage* record : changed) GameFlow::OnVariableChanged(record);
        bool settled;
        do {
            settled = true;
            for (const auto& variable : net::g_gameClient->variables_) {
                const net::VariableMessage* record = variable.Get();
                if (record->serial_ > g_appliedStamp) continue;
                if (record->serial_ == g_appliedStamp) {
                    GameFlow::OnVariableChanged(record);
                    ++g_appliedStamp;
                    settled = false;
                }
                VariableSetBits(variables, record->name_.data(), ValueBits(record));
            }
        } while (!settled);
    } else {
        for (const auto& variable : net::g_gameClient->variables_) {
            const net::VariableMessage* record = variable.Get();
            if (g_serverStamp <= record->serial_) g_serverStamp = record->serial_;
            std::string name = record->name_.data();
            VariableSetBits(variables, record->name_.data(), ValueBits(record));
        }
        g_appliedStamp = 0;
    }
    UpdateMultiplayerVariable();
}

void SavedParties::Reload() {
    entries_.clear();

    WIN32_FIND_DATAA found;
    HANDLE search;
    {
        std::string pattern = folder_ + kPartyFilePattern;
        search = gameapi::FindFirstFileA(pattern.c_str(), &found);
    }
    if (search == INVALID_HANDLE_VALUE) return;
    do {
        MpHeroRecord read;
        MpHeroRecord* entry = &read;
        entry->file = found.cFileName;
        if (entry->ReadFile(folder_.c_str())) {
            NamedFloatTable* variables = &entry->Party()->variables_;
            if (ei::rebirth::Enabled(ei::rebirth::Patch::PersistentGlobalVars)) {
                // Rebirth's PersistentGlobalVars: the clear is skipped and the table is cleared
                // keeping its "gvar." entries.
                ClearVariablesKeepingGlobals(variables);
            } else {
                variables->clear();
            }
            if (std::ssize(entry->Party()->members_) == 1) {
                ReserveOne();
                entries_.push_back(read);
            }
        }
    } while (gameapi::FindNextFileA(search, &found));
    gameapi::FindClose(search);
}

void GameFlow::BuildZoneTable() {
    std::vector<ZoneEntry>& table = g_zoneTable;
    if (table.end() != table.begin()) return;
    lib::FileFind find;
    int more;
    {
        std::string folder;
        modfolders::SelectMaps(kQuestFilePattern, modfolders::MapsSite::ZoneTable);
        MapsFolder(&folder);
        std::string pattern = folder + kQuestFilePattern;
        more = find.FindFile(pattern);
    }
    while (more) {
        more = find.FindNext();
        std::string name = find.GetTitle();
        str::MakeLower(name);
        RegisterQuests(name.c_str());
        UnloadQuestTexts();
        std::string map;
        if (MapRecord* record = FindZone(name)) {
            map = record->map;
            const int allod = AllodOfMap(map.c_str());
            ZoneEntry& entry = table.emplace_back();
            entry.first = name;
            entry.second = map;
            entry.id = allod;
        }
    }
    find.Close();
}
