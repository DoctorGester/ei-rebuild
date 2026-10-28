// The game flow object (the state machine runs on it, Rebirth patches that one): saving a game into
// saves\<slot>, entering a zone. See docs/notes/world.md.
#include "camera.h"
#include "at_exit_functions.h"
#include "core_refcount.h"
#include "options.h"
#include "player_manager.h"
#include "random.h"
#include "item_db.h"
#include "item_db_text.h"
#include "net_coder.h"
#include "record_arrays.h"
#include "sound.h"
#include "net_session.h"
#include "ui.h"
#include "ui_screens.h"
#include "world.h"
#include "game_flow_data.h"
#include "game_slots.h"
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

#include "lib/static_object.h"
#include "lib/crt.h"
#include "lib/crt_iostream.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "lib/stlp.h"
#include "lib/mfc_array.h"

#include "ei/rebirth.h"
#include "party_manager.h"
#include "party_records.h"
#include "game_lists.h"
#include "record_vectors.h"
#include "game_arrays.h"
#include "library_containers.h"
#include "bink_video.h"
#include "game_data.h"
#include "lib/nan_audit.h"
#include "game_singletons.h"
#include "lib/str.h"

#include <cmath>
#include <cstring>
#include <memory>
#include <new>
#include <bit>
#include <string>

namespace {

using namespace flowdata;

constexpr unsigned kOpenWrite = netlink::FileStream::kOpenWrite;
constexpr unsigned kOpenRead = netlink::FileStream::kOpenRead;
constexpr unsigned kMagicScenario = 0xDEAD;
constexpr unsigned kMagicMission = 0x102;

int GameMode() { return TheServerWorld()->gameResult_; }        // 1: single player
int SaveVersion() { return kSaveVersion; }
bool& ToMainMenu() { return TheInterface()->closeAll_; }
// BeginMultiplayerGame sets it, a missing zone clears it.
bool& SessionsOpen() { return TheInterface()->disconnected_; }

libcrt::Ostream* const kConsole = &console::g_consoleOut;

MapRecord* ZoneRecords() { return TheMapDatabase()->maps.data; }
int ZoneRecordCount() { return TheMapDatabase()->maps.size; }
MapRecord* FindZone(const char* name) { return TheMapDatabase()->FindMap(name); }
MapRecord* FindZone(const std::string& name) { return FindZone(name.c_str()); }

using netlink::FileStream;

double VariableGet(NamedFloatTable* table, const char* name) { return table->Get(name); }
void VariableSet(NamedFloatTable* table, const char* name, float value) { table->Set(name, value); }
void ScriptsReset(CMemoryStream* scripts) { scripts->Resize(0); }

struct LocalRecords {
    PartyStorage storage;
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
            double value = VariableGet(variables, variable.c_str());
            EI_NAN_AUDIT(value);
            visited = value != 0.0f;
        }
        bool keep = false;
        if (visited) {
            keep = gamecrt::mbscmp(record->name,
                                   kAlwaysCopied) == 0 ||
                   gamecrt::mbscmp(record->allod,
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
        VariableSet(variables, variable.c_str(), 0.0f);
    }
}

} // namespace

// The game's save stream (core_streams.h), zero at the start: built by a static initialiser and destroyed
// by the exit function it registers (docs/notes/standalone.md, "The static initialisers' objects").
constinit lib::StaticObject<CMemoryStream> g_saveStream = /*007AFDF8*/ {};
CMemoryStream* GameSaveStream() { return &*g_saveStream; }

namespace {
void DestroySaveStream() { g_saveStream->~CMemoryStream(); }
} // namespace

void ConstructSaveStream() {
    new (&*g_saveStream) CMemoryStream();
    libcrt::AtExit(DestroySaveStream);
}

int GameFlow::SaveGame(const char* slot) {
    FileStream file;
    std::string zone;
    std::string slotFolder;
    std::string currentFolder;
    if (GameMode() != 1) return 0;

    TheCursor()->SetShape(3);
    slotFolder = str::Format(kSlotFolder, DataFolderText(), slot);
    currentFolder = str::Format(kCurrentFolder, DataFolderText());
    ClearSaveFolder(slotFolder.c_str());
    if (TheServerWorld()->sessionKind_ == 4) {
        if (CPlayer* player = TheServerWorld()->GetPlayer(0))
            ThePartyManager()->BeginRedeploy(player->Party());
    }
    LocalRecords records;
    ThePartyManager()->EndRedeploy(records.Party());
    std::string currentMap;
    currentMap = FindZone(records.Party()->zone_)->allod;

    CopyVisitedZones(slotFolder, currentFolder, zone, currentMap, &records.Party()->variables_);
    ThePartyManager()->BeginRedeploy(records.Party());

    bool opened;
    {
        std::string path = slotFolder + kScenarioFile;
        opened = file.Open(path, kOpenWrite, 0);
    }
    if (!opened) {
        ToMainMenu() = 1;
        StartZone(kMainMenu, 2);
        return 0;
    }

    // scenario.sav: the party, the input state, the scripts and the globals.
    CMemoryStream scenario;
    scenario.version_ = static_cast<std::uint32_t>(SaveVersion());
    ThePartyManager()->Save(&scenario);
    TheInterface()->SaveState(&scenario);
    ScriptsReset(GameSaveStream());
    TheInterface()->SaveWindowState();
    scenario.WriteSized(GameSaveStream());
    ScriptsReset(GameSaveStream());
    TheCamera()->Save(&scenario);
    SaveFileWrite(&file, kMagicScenario, SaveVersion(), &scenario);
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
        mission.version_ = static_cast<std::uint32_t>(SaveVersion());
        TheServerWorld()->SaveWorld(&mission);
        SaveFileWrite(&file, kMagicMission, SaveVersion(), &mission);
        file.Close();
    }

    TheCursor()->SetShape(0);
    kConsole->PutText(kSaved)->PutText(slot)->PutChar('\n')->Apply(libcrt::FlushManipulator);
    return 1;
}

namespace {

constexpr float kCatchUpRadius = 70.0f;
constexpr std::uint32_t kStartTime = 0x40800000;        // 4.0f
constexpr int kMaxCatchUpTicks = 1000;

const std::string& MoviesFolder() { return TheDiscFolder(); }

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
    if (!SaveFileRead(&data, kMagicMission, SaveVersion(), kSaveOldestVersion, &file)) {
        game::FatalError(kCorruptSave, fileName.c_str());
        failed = true;
        return 0;
    }
    int result = TheServerWorld()->LoadSave(&data, mapOverride);
    file.Close();

    int entrance = records->entrance_;
    if (entrance >= 0 && entrance < zoneRecord->exits.size) {
        float now = static_cast<float>(VariableGet(variables, kGTime));
        float left;
        {
            std::string prefix = kLeftPrefix;
            std::string variable = prefix + zone;
            left = static_cast<float>(VariableGet(variables, variable.c_str()));
        }
        int ticks = static_cast<int>(gamecrt::Ftol((static_cast<double>(now) - left) * 100.0f /*0073BAB0*/));
        if (ticks >= kMaxCatchUpTicks) ticks = kMaxCatchUpTicks;
        else if (ticks <= 0) return result;
        // The centre of the exit's deploy rectangle.
        const ZoneExit* exit = zoneRecord->exits.data + entrance;
        Vector3 point;
        point.x = static_cast<float>((static_cast<double>(exit->deploy[2]) + exit->deploy[0]) * 0.5f /*0073B86C*/);
        point.y = static_cast<float>((static_cast<double>(exit->deploy[3]) + exit->deploy[1]) * 0.5f /*0073B86C*/);
        point.z = 0.0f;
        for (int i = ticks; i != 0; --i) {
            TheServerWorld()->WakeUnitsAround(&point, kCatchUpRadius);
            TheServerWorld()->Tick(static_cast<char>(1));
        }
    }
    return result;
}

} // namespace

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
    if (static_cast<int>(gamecrt::Ftol(VariableGet(variables, kMovieVariable))) == 1) {
        if (gamecrt::mbscmp(zone.c_str(),
                            kMovieZone) != 0) {
            {
                std::string movie = MoviesFolder() + kMovieFile;
                PlayMovie(movie.c_str(), 0, 1, 1, 1, 1, 1);
            }
            variables->Remove(kMovieVariable);
        }
    } else {
        variables->Remove(kMovieVariable);
    }

    if (gamecrt::mbscmp(zoneRecord->map,
                        game::EmptyText()) != 0) {
        const char* mapOverride = nullptr;
        if (gamecrt::mbsicmp(zone.c_str(),
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
            double value = VariableGet(variables, variable.c_str());
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
                loaded = TheServerWorld()->NewZone(map.c_str(), mob.c_str(), zoneRecord->name.c_str()) != 0;
            }
            if (!loaded) return 0;
            zoneRecord = FindZone(zone);
            TheServerWorld()->SetTime(std::bit_cast<float>(kStartTime));
        }
        if (zoneRecord->kind == 2) {
            std::string scripts = zoneRecord->mob + kMobExtension;
            TheServerWorld()->LoadZoneScripts(scripts.c_str());
        } else {
            ScriptResetZone(TheServerWorld()->MainScript());   // the scripts reset for a zone without its own
        }
    }
    TheServerWorld()->SetZoneExits(&zoneRecord->exits);
    return result;
}

namespace {

constexpr int kGameResultNetwork = 2;
constexpr int kHostPort = 8000;
constexpr int kServerMessageZone = 8;

int& SpectatorFlag() { return options::TheOptions()->drawVisibleOnly_; }


bool Active(const CRefCounted* object) { return object && !(object->flags7_ & CRefCounted::kDetached); }

// Tells the clients of the game server where the game is: the host's address and the zone.
void AnnounceHost(const char* zone) {
    char host[100];
    if (libcrt::imports::GetHostName(host, sizeof(host)) != 0) return;
    net::NetAddress address{};
    address.Parse(host, kHostPort);
    std::string formatted = address.ToString();
    net::HostMessage* record = net::NewCounted<net::HostMessage>();
    RefPtr& slot = net::GameServer()->hostInfo_;
    if (record != slot.ptr) {
        slot.Destroy();
        slot.Share(record);
    }
    net::HostMessage* shared = net::GameServer()->hostInfo_.Get();
    shared->address_ = formatted;
    shared = net::GameServer()->hostInfo_.Get();
    shared->zone_.assign(zone);
    net::GameServer()->AnnounceToGuests(kServerMessageZone, zone);
}

} // namespace

// Starts network zone `zone` (a zone record of kind 0): tears the current zone down, restores
// saves\current\<zone>.sav when there is one (else loads the zone fresh at time 4), copies its exits with
// every target set to "MPGame1", applies its two settings, marks the game a network one and, on a running
// game server, announces the host and loads the zone's .mob. Returns 1, 0 on failure.
int StartNetworkZone(const char* zone) {
    ToMainMenu() = 1;
    MapRecord* zoneRecord = FindZone(zone);
    if (!zoneRecord || zoneRecord->kind != 0) return 0;
    TheServerWorld()->TeardownZone();

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
        bool loaded = SaveFileRead(&data, kMagicMission, SaveVersion(), kSaveOldestVersion,
                                   &file) &&
                      TheServerWorld()->LoadSave(&data, nullptr) != 0;
        if (loaded) file.Close();
        if (!loaded) return 0;
    } else {
        bool loaded;
        {
            std::string mob = zoneRecord->mob + kMobExtension;
            std::string map = zoneRecord->map + kMapExtension;
            loaded = TheServerWorld()->NewZone(map.c_str(), mob.c_str(), zone) != 0;
        }
        if (!loaded) return 0;
        TheServerWorld()->SetTime(std::bit_cast<float>(kStartTime));
    }

    // The zone's exits, all leading to "MPGame1".
    ZoneExits exits;
    mfc::impl::ConstructArrayOf<mfc::impl::ExitArrayClass>(exits);
    exits.vtable = mfc::impl::MapRecordItemsClass::Vtable();
    const ZoneExits* source = &zoneRecord->exits;
    exits.SetSize(source->size);
    for (int i = 0; i < source->size; ++i) {
        lib::InPlace<ZoneExit> copy;
        ZoneExit* exit = source->CopyAt(copy.Get(), i);
        exits.data[i].Assign(exit);
        std::destroy_at(&exit->target);
    }
    for (int i = 0; i < exits.size; ++i) exits.data[i].target = kNetworkExitTarget;
    TheServerWorld()->SetZoneExits(&exits);
    TheServerWorld()->SetClimate(zoneRecord->setting2C);
    TheServerWorld()->SetCave(zoneRecord->setting30);
    TheServerWorld()->sessionKind_ = 1;
    TheServerWorld()->SetGameResult(kGameResultNetwork);
    SpectatorFlag() = 1;

    if (Active(net::GameServer())) {
        AnnounceHost(zone);
        std::string name = zone;
        std::string mob = name + kMobExtension;
        TheServerWorld()->LoadMobFile(mob.c_str());
    }

    exits.vtable = mfc::impl::ExitArrayClass::Vtable();
    if (exits.data) {
        for (int i = 0; i < exits.size; ++i) std::destroy_at(&exits.data[i].target);
        gamecrt::operator_delete(exits.data);
    }
    return 1;
}

namespace {


} // namespace

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
    TheServerWorld()->ZoneNameInto(&zone);
    // `path` must be destroyed after `zone`: it is declared first.

    if (TheServerWorld()->sessionKind_ == 1) TheCamera()->SaveView(7);
    else if (TheServerWorld()->sessionKind_ == 3) TheCamera()->SaveView(6);
    path = str::Format(kCurrentFolder, DataFolderText());
    {
        std::string fileName = zone + kSaveExtension;
        path += fileName;
    }
    if (!file.Open(path, kOpenWrite, 0)) {
        game::FatalError(kCantWrite, path.c_str());
        return 0;
    }
    TheServerWorld()->PrepareSave();
    CMemoryStream data;
    data.version_ = static_cast<std::uint32_t>(SaveVersion());
    TheServerWorld()->SaveWorld(&data);
    SaveFileWrite(&file, kMagicMission, SaveVersion(), &data);
    file.Close();

    NamedFloatTable* variables = &records->variables_;
    {
        std::string prefix = kVisitedPrefix;
        std::string variable = prefix + zone.c_str();
        VariableSet(variables, variable.c_str(), 1.0f);
    }
    float now = static_cast<float>(VariableGet(variables, kGTime));
    {
        std::string prefix = kLeftPrefix;
        std::string variable = prefix + zone;
        VariableSet(variables, variable.c_str(), now);
    }
    return 1;
}

// ---- The multiplayer flag -----------------------------------------------------------------------

namespace {

// The local client's player record (ClientSession::player_, a LoginMessage): its state_ is the
// game state request to the server (a change goes out with a new version_).
net::LoginMessage* LocalLogin() { return net::GameClient()->player_.Get(); }
int ServerPlayerCount() { return static_cast<int>(TheServerWorld()->players_->objects_.Size()); }

} // namespace

void GameFlow::UpdateMultiplayerVariable() {
    CPlayer* player = TheClientWorld()->LocalView()->player_;
    NamedFloatTable* variables = &player->Party()->variables_;
    if (!Active(net::GameClient())) return;
    auto set = [variables](bool on) {
        if (on) VariableSet(variables, kMultiplayerVariable, 1.0f);
        else variables->Remove(kMultiplayerVariable);
    };
    if (!Active(net::GameServer())) {
        set(!Active(net::GameClient()->info_));
        return;
    }
    if (flag80_) set(ServerPlayerCount() > 1);
    else set(!Active(net::GameClient()->map_));
}

// ---- Starting a multiplayer game ------------------------------------------------------------------

namespace {


CParty* GlobalParty() { return TheSavedParties().OutsideGame()->Party(); }
constexpr std::uint32_t kMultiplayerHeroId = 0x3B9AF5CE;   // the only record's unit id

} // namespace

std::string* GameFlow::MultiplayerZoneInto(std::string* out) {
    unsigned type = net::GameClient()->gameType_;
    *out = kMultiplayerZones[type <= 3 ? type : 0]; return out;
}

void GameFlow::BeginMultiplayerGame() {
    PartyManagerNewGameFour(ThePartyManager());
    if (GlobalParty()->members_.size == 1) GlobalParty()->members_.data->unitId = kMultiplayerHeroId;
    {
        std::string zone;
        GlobalParty()->zone_ = *MultiplayerZoneInto(&zone);
    }
    VariableSet(&GlobalParty()->variables_, kMultiplayerVariable, 1.0f);
    ThePartyManager()->BeginRedeploy(GlobalParty());
    {
        std::string zone;
        MultiplayerZoneInto(&zone);
        state_ = 4;   // load the zone
        requestTarget_ = zone.c_str();
        requestArg_ = 0;
    }
    flag80_ = 0;
    LocalLogin()->state_ = 1;
    ++LocalLogin()->version_;
    SessionsOpen() = 1;
}

// ---- Small glue of the flow -----------------------------------------------------------------------

namespace {

constexpr int kCommandBriefing = 1;
constexpr int kRequestDone = 3;   // the client's game state request: answered

} // namespace

void GameFlow::ReturnLocalParty() {
    if (!Active(net::GameClient())) return;
    CPlayer* player = TheClientWorld()->LocalView()->player_;
    CParty* party = player->Party();
    GlobalParty()->Assign(party);
    ThePartyManager()->BeginRedeploy(party);
    // The global party record written into the heroes' folder.
    TheSavedParties().OutsideGame()->WriteFile(TheSavedParties().folder_.c_str());
    UpdateFromServer();
}

void GameFlow::UpdateAndAcknowledge() {
    if (!Active(net::GameClient())) return;
    UpdateFromServer();
    if (LocalLogin()->state_ != kRequestDone) return;
    LocalLogin()->state_ = 0;
    ++LocalLogin()->version_;
}

void GameFlow::RequestBriefing(const char* briefing) {
    net::ClientSession* client = net::GameClient();
    if (Active(client)) client->SendCommand(kCommandBriefing, briefing);
}

namespace {

void ReleaseSession(CRefCounted* session) {
    if (session && --session->refs_ == 0) delete session;
}

} // namespace

void GameFlow::RecreateClientSession() {
    net::ClientSession*& global = net::GameClient();
    net::ClientSession* old = global;
    global = nullptr;
    ReleaseSession(old);
    net::ClientSession* session = net::NewCounted<net::ClientSession>();
    net::ClientSession* previous = global;
    global = session;
    if (session) ++session->refs_;
    ReleaseSession(previous);
}

namespace {

} // namespace

int GameFlow::SaveGameThunk(const char* slot) { return SaveGame(slot); }

int RequestZone(const char* zone, int entrance) {
    TheInterface()->closeAll_ = true;
    // The local view asks to go to a zone (name, entrance).
    return TheClientWorld()->LocalView()->RequestZone(zone, entrance);
}

void GameFlow::DropSessions() {
    SessionsOpen() = 0;
    net::ServerSession*& server = net::GameServer();
    net::ServerSession* oldServer = server;
    server = nullptr;
    ReleaseSession(oldServer);
    net::ClientSession*& client = net::GameClient();
    net::ClientSession* oldClient = client;
    client = nullptr;
    ReleaseSession(oldClient);
}

void GameFlow::EndSession() {
    DropSessions();
    UseResources(false);
}

void GameFlow::CloseSessions() {
    SessionsOpen() = 0;
    net::ServerSession*& server = net::GameServer();
    net::ServerSession* oldServer = server;
    server = nullptr;
    ReleaseSession(oldServer);
    net::ClientSession*& client = net::GameClient();
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

void GameFlow::RequestZoneChange(const char* zone, int entrance) {
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

void GameFlow::DisconnectClient() { TheClientWorld()->DisconnectLocalPlayer(); }

namespace {

constexpr std::uint32_t kGraphChunk = 0x31415926;         // the AI graph chunk in a .mob file

} // namespace

int CWorldServer::ReadAIGraph(const char* map, const char* mob, CStream* out) {
    FileStream mobFile;
    int found = 0;
    if (mob) {
        // The graph chunk of the .mob file.
        {
            std::string path;
            MapPath(&path, mob);
            mobFile.Open(path, kOpenRead, 1);
        }
        if (mobFile.FindTagged(kGraphChunk, out)) found = 1;
        mobFile.Close();
        if (found) return found;
    }
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

namespace {

constexpr std::uint32_t kScriptsChunk = 0xA000;          // the .mob file's scripts chunk
constexpr std::uint32_t kScriptEncoded = 0xACCEECCB;     // in it: the script text, packed and XORed
constexpr std::uint32_t kScriptPlain = 0xACCEECCA;       // in it: the script text as is

} // namespace

void CWorldServer::LoadZoneScripts(const char* mob) {
    FileStream file;
    std::string script;
    CMemoryStream scripts;
    CMemoryStream text;
    MobScriptDecoder decoder;
    decoder.text.ConstructEmpty();
    decoder.input.ConstructEmpty();
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
        decoder.input.Resize<stlp::PlainElement<char>>(size, zero);
        std::memmove(decoder.input.begin(), data, size);
        decoder.Decode(&script);
    } else if (scripts.FindTagged(kScriptPlain, &text)) {
        text.Seek(text.size_);
        char zero = 0;
        text.Write(&zero, 1u);
        script = reinterpret_cast<const char*>(text.data_);
    }
    ScriptReset(MainScript());
    MainScript()->Load(&script, 1);
    decoder.input.FreeStorage();
    decoder.text.FreeStorage();
}

namespace {

constexpr int kServerDropClient = 9;

// The client's game state request: `kind`, a new version.
void RequestKind(int kind) {
    LocalLogin()->state_ = kind;
    ++LocalLogin()->version_;
}

} // namespace

void GameFlow::SwitchMultiplayerZone() {
    if (flag80_) {
        // A switch is pending: ask for the start zone again.
        RequestKind(1);
        bool host = Active(net::GameServer());
        {
            std::string zone;
            MultiplayerZoneInto(&zone);
            ToMainMenu() = 1;
            TheClientWorld()->LocalView()->RequestZone(zone.c_str(), 0);
        }
        if (host) {
            // The host sends command 9 with the announced zone and drops the announcement.
            net::HostMessage* announced = net::GameServer()->hostInfo_.Get();
            if (Active(announced)) net::GameServer()->AnnounceToGuests(kServerDropClient, announced->zone_.data());
            RefPtr& handle = net::GameServer()->hostInfo_;
            if (handle.ptr) {
                handle.Destroy();
                handle.Share(nullptr);
            }
        }
        flag80_ = 0;
        return;
    }

    // Every connection of the host drops what it has read of the incoming reliable stream
    // (flags 6, high and the cursor back to the start).
    net::ServerSession* server = net::GameServer();
    if (Active(server)) {
        net::ClientNode* head = server->clients_;
        for (net::ClientNode* node = head->next; node != head; node = node->next) {
            GameBuffer& in = node->connection->reliableIn_;
            std::uint8_t* begin = in.begin;
            in.flags = 6;
            in.high = begin;
            in.cur = begin;
        }
    }
    LocalRecords records;
    CPlayer* player = TheServerWorld()->GetPlayer(0);
    if (player && player->Server()->flag70_) {
        TheServerWorld()->SyncPartyRecords(player);
        records.Party()->Assign(player->Party());
        TheServerWorld()->ClearPlayerItems(player);
    } else {
        records.Party()->Assign(GlobalParty());
    }
    records.Party()->zone_ = zone7C_;
    RequestKind(3);
    if (Active(net::GameServer())) {
        state_ = 6;
        requestTarget_ = zone7C_;
        RequestParty()->Assign(records.Party());
    } else {
        net::HostMessage* host = net::GameClient()->info_;
        if (Active(host)) records.Party()->zone_ = host->zone_.data();
        host = net::GameClient()->info_;
        if (Active(host)) {
            state_ = 5;
            requestTarget_ = host->address_.data();
            RequestParty()->Assign(records.Party());
        }
    }
    flag80_ = 1;
}

namespace {

constexpr std::size_t kPasswordSize = 0x14;

} // namespace

void GameFlow::StartServer(const char* name, int value25E0, int gameType, const char* password, char unlisted,
                           int value25FC) {
    BuildZoneTable();
    net::g_netLog = console::StreamAs<net::LogStream>(console::g_netLogOut);
    {
        net::ServerSession* server = net::NewCounted<net::ServerSession>(gameType);
        AssignCounted(net::GameServer(), server);
    }
    {
        net::ClientSession* client = net::NewCounted<net::ClientSession>();
        AssignCounted(net::GameClient(), client);
    }
    // The local client connects to the local server with the password as its hello.
    net::NetAddress address = {};
    address.Parse(kLocalHost, net::g_gamePort);
    {
        GrowingBuffer hello;
        char secret[kPasswordSize] = {};
        if (password) gamecrt::strncpy(secret, password, kPasswordSize);
        hello.BufferWrite(secret, static_cast<int>(kPasswordSize));
        net::GameClient()->Join(&address, &hello);
    }
    LocalLogin()->host_ = 1;
    const char* shown = name && *name ? name : kServerName;
    net::GameServer()->name_.assign(shown);
    net::GameServer()->maxPlayers_ = value25E0;
    if (password && *password) net::GameServer()->SetPassword(password);
    net::GameServer()->SetPublic(unlisted == 0);
    net::GameClient()->gameType_ = gameType;
    net::GameServer()->masterKey_ = value25FC;
    AfterServerStart();
}

void GameFlow::StartClient() {
    net::g_netLog = console::StreamAs<net::LogStream>(console::g_netLogOut);
    AssignCounted(net::GameServer(), static_cast<net::ServerSession*>(nullptr));
    net::ClientSession* client = net::NewCounted<net::ClientSession>();
    AssignCounted(net::GameClient(), client);
    AfterServerStart();
}

void GameFlow::JoinServer(const net::NetAddress* address, const char* password) {
    GrowingBuffer hello;
    char secret[kPasswordSize] = {};
    if (password) gamecrt::strncpy(secret, password, kPasswordSize);
    hello.BufferWrite(secret, static_cast<int>(kPasswordSize));
    net::GameClient()->Join(const_cast<net::NetAddress*>(address), &hello);
}

namespace {

libcrt::Ostream* const kNetLog = &console::g_netLogOut;

} // namespace

void CWorldServer::AcceptNewPlayers(CPlayerSource* lobby) {
    mfc::CTypedPtrList<netlink::NetLink> joined;
    joined.vtable = gameslots::RefListAClass::Vtable();
    joined.head = nullptr;
    joined.tail = nullptr;
    joined.count = 0;
    joined.free = nullptr;
    joined.blocks = nullptr;
    joined.blockSize = 10;
    if (gameResult_ == 0) {
        mfc::impl::ListAs<gamelists::Refs>(&joined)->RemoveAll();
        return;
    }
    lobby->TakeNew(&joined);
    for (auto* node = joined.head; node;) {
        auto* next = node->next;
        netlink::NetLink* connection = node->data;
        CPlayer* player = players_->CreateSessionPlayer();
        player->Connect(connection);
        player->dirty_ = 0xFFFFFFFFu;
        player->sessionKind100_ = sessionKind_;
        std::memcpy(&player->Server()->lastPosition_, &startPosition_, 12);
        int id = player->playerId_;
        kNetLog->PutText(kAcceptedPlayer)->PutInt(id)->PutChar('\n')->Apply(libcrt::FlushManipulator);
        node = next;
    }
    // The list's inlined destructor: every reference released, then the blocks.
    joined.vtable = gameslots::RefListAClass::Vtable();
    for (auto* node = joined.head; node; node = node->next) ReleaseSession(node->data);
    joined.count = 0;
    joined.free = nullptr;
    joined.tail = nullptr;
    joined.head = nullptr;
    mfc::PlexFreeDataChain(joined.blocks);
}

namespace {

// The zone table: a vector of {first, second, id} entries (docs/notes/standalone.md, "The static
// initialisers' objects").
using ZoneEntry = recordvec::ZoneTableEntry;
using ZoneTable = stlp::vector<ZoneEntry>;
constinit stlp::vector<ZoneEntry> g_zoneTable = /*007AFDE0*/ {};
bool SameText(std::string_view a, std::string_view b) {
    return gamecrt::mbscmp(a, b) == 0;
}

// A list of entry pointers on the game's heap (the game keeps STLport lists of copies).
struct EntryPicks {
    const ZoneEntry** items = nullptr;
    int count = 0;
    explicit EntryPicks(int capacity) {
        items = gamecrt::operator_new_array<const ZoneEntry*>(static_cast<std::size_t>(capacity > 0 ? capacity : 1));
    }
    ~EntryPicks() { gamecrt::operator_delete(items); }
    EntryPicks(const EntryPicks&) = delete;
    EntryPicks& operator=(const EntryPicks&) = delete;
    void RemoveAt(int i) {
        for (int k = i + 1; k < count; ++k) items[k - 1] = items[k];
        --count;
    }
};

} // namespace

void DestroyZoneTable() { recordvec::ZoneTableVector::Destruct(&g_zoneTable); }

void PickZones(stlp::vector<std::string>* out, int id, const char* exclude) {
    const ZoneTable& table = g_zoneTable;
    int total = static_cast<int>(table.size());
    EntryPicks pending(total);
    EntryPicks group(total);
    for (ZoneEntry* e = table.begin(); e != table.end(); ++e)
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
        unsigned pick = ((&IsaacRandom::Global()))->Next() % static_cast<unsigned>(group.count);
        std::string name(group.items[pick]->first.c_str());
        if (out->Finish() != out->EndOfStorage()) {
            new (out->Finish()) std::string(name);
            ++out->Finish();
        } else {
            out->InsertOverflow<stlp::ObjectElement<std::string>>(out->Finish(), name, 1);
        }
    }
}

namespace {

constexpr unsigned kMagicMissionFile = 0x102;
constexpr int kVersionGlobalsInMission = 0x52;   // below: the globals come from mission.sav
constexpr int kVersionInputInMission = 0x47;     // below: the input state too

} // namespace

int LoadMission(CStream* file) {
    TheSoundManager()->StopAll(1);
    CMemoryStream mission;
    if (!SaveFileRead(&mission, kMagicMissionFile, kSaveVersion,
                      kSaveOldestVersion, file))
        return 0;
    int version = static_cast<int>(mission.version_);
    if (version < kVersionGlobalsInMission) TheCamera()->Load(&mission);
    if (static_cast<int>(mission.version_) < kVersionInputInMission) {
        // Old saves: the input state, then the rest goes to the save stream.
        GameSaveStream()->Resize(0);
        TheInterface()->LoadState(&mission);
        mission.ReadSized(GameSaveStream());
        GameSaveStream()->version_ = mission.version_;
    }
    return TheServerWorld()->LoadSave(&mission, nullptr);
}

namespace {


} // namespace

void ClearSaveFolder(const char* path) {
    gameapi::CreateDirectoryA(path, nullptr);
    mfc::CFileFind finder;
    int found;
    {
        std::string folder(path);
        std::string pattern = folder + kAllFiles;
        found = finder.FindFile(pattern, 0);
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
        if (libcrt::Stricmp(*list, name) == 0) return true;
    return false;
}

namespace {

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

} // namespace

void GameFlow::StartZone(const char* zone, int mode) {
    TheCursor()->SetShape(3);
    MapRecord* zoneRecord = nullptr;
    ToMainMenu() = 1;
    if (libcrt::Stricmp(zone, kEndOfGame) == 0) {
        mode = kModeEndOfGame;
    } else {
        zoneRecord = FindZone(zone);
    }
    TheServerWorld()->TeardownZone();

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
        game::FatalError(kZoneNotFound, zone);
        if (Active(net::GameClient())) {
            net::ServerSession*& server = net::GameServer();
            net::ServerSession* oldServer = server;
            SessionsOpen() = 0;
            server = nullptr;
            ReleaseSession(oldServer);
            net::ClientSession*& client = net::GameClient();
            net::ClientSession* oldClient = client;
            client = nullptr;
            ReleaseSession(oldClient);
        }
        mode = kModeMainMenu;
    }

    TheServerWorld()->SetTimeSpeed(static_cast<float>(TimeSpeedSetting()));
    int result;
    if (mode == kModeMainMenu) {
        TheServerWorld()->NewZone(kMenuMap, kMenuMob, nullptr);
        // The menu shows the local time of day.
        SYSTEMTIME now;
        gameapi::GetLocalTime(&now);
        int minute = now.wMinute;
        int hour = now.wHour;
        TheServerWorld()->SetTime(static_cast<float>(minute) * 0.016666668f /*0073F8A0*/ + static_cast<float>(hour));
        result = kResultMainMenu;
        // Rebirth's AllowScriptsInMainMenu: the first script command queue runs in the menu too.
        if (ei::rebirth::Enabled(ei::rebirth::Patch::AllowScriptsInMainMenu)) TheServerWorld()->scriptsActive_ = 1;
    } else if (mode == kModeEndOfGame) {
        result = kResultPlaying;
    } else {
        // The party enters the zone, then the clock is set from its "gtime".
        LocalRecords records;
        ThePartyManager()->EndRedeploy(records.Party());
        EnterZone(records.Party());
        ThePartyManager()->BeginRedeploy(records.Party());
        TheServerWorld()->SetTime(HoursOfDay(VariableGet(&records.Party()->variables_, kGTime)));
        if (zoneRecord) {
            TheServerWorld()->SetClimate(zoneRecord->setting2C);
            TheServerWorld()->SetCave(zoneRecord->setting30);
        }
        result = kResultPlaying;
    }

    TheServerWorld()->sessionKind_ = mode;
    if (mode == 1) SpectatorFlag() = mode;
    TheServerWorld()->SetGameResult(result);
    // The server world's player source handed to the client world.
    TheClientWorld()->SetPlayer(TheServerWorld()->shared5C_.Get());
    ClientWorldLocalView(TheClientWorld())->sendParty_ = 0;
}

namespace {

} // namespace

void GameFlow::StartNewGame() {
    // Rebirth's AutoExec #2 (the call redirected): addon's AutoExec lines first.
    if (ei::rebirth::Enabled(ei::rebirth::Patch::AutoExec)) console::RunAddonAutoExec();
    console::Execute(kExecAutoexec);
    ToMainMenu() = 1;
    PartyManagerNewGame(ThePartyManager());
    std::string folder;
    folder = str::Format(kCurrentFolder, DataFolderText());
    ClearSaveFolder(folder.c_str());
    LocalRecords records;
    ThePartyManager()->EndRedeploy(records.Party());
    StartZone(records.Party()->zone_.c_str(), 0);
}

// ---- The flow's requests -------------------------------------------------------------

namespace {

constexpr int kStateMainMenu = 1, kStateLoad = 2, kStateNewGame = 3, kStateZone = 4, kStatePartyZone = 5,
              kStateNetworkZone = 6;

// UseResources' function-local static: the resources in use (guard bit 0 of g_lastResourcesGuard),
// destroyed at exit.
constinit lib::StaticObject<std::string> g_lastResources = /*007AFDEC*/ {};
std::string& LastResources() { return *g_lastResources; }
void DestroyLastResources() { g_lastResources.Destroy(); }
std::uint8_t g_lastResourcesGuard = /*007AFDF0*/ {};
std::uint8_t& LastResourcesGuard() { return g_lastResourcesGuard; }
int& SavedSpectator() { return options::TheOptions()->difficultyStored_; }
int& Spectator0() { return options::TheOptions()->difficulty_; }
std::uint8_t& AutoplayOn() { return TheServerWorld()->sendsStartPosition_; }
const char* ResourcesPath() { return options::TheOptions()->resourcesPath_.c_str(); }
UnitDatabase* UnitDatabaseObject() { return &unitdb::Database(); }
itemdb::SpellDatabase* SpellTablesObject() { return &itemdb::TheSpellDatabase(); }
itemdb::ItemDatabase* ItemDatabaseObject() { return &itemdb::TheItemDatabase(); }

} // namespace

void SwitchResourceDatabases(const char* name) {
    UnitDatabase* units = UnitDatabaseObject();
    units->Reset();
    units->Load(ResourcesPath(), name);
    units->CountSoundFiles(TheSoundManager());
    SkillDb()->Reset();
    SkillDb()->Load(ResourcesPath(), name);
    SpellTablesObject()->Reset();
    SpellTablesObject()->Load(ResourcesPath(), name);
    ItemDatabaseObject()->Reset();
    ItemDatabaseObject()->Load(ResourcesPath(), name);
}

void GameFlow::UseResources(bool network) {
    if (!(LastResourcesGuard() & 1)) {
        LastResourcesGuard() |= 1;
        g_lastResources.Construct(kResourcesSingle);
        libcrt::AtExit(DestroyLastResources);
    }
    std::string wanted = network ? kResourcesNetwork : kResourcesSingle;
    if (gamecrt::mbscmp(wanted,
                        LastResources().c_str()) == 0)
        return;
    LastResources() = wanted;
    SwitchResourceDatabases(wanted.c_str());
    if (network) {
        int value = Spectator0();
        Spectator0() = 0;
        SavedSpectator() = value;
    } else {
        Spectator0() = SavedSpectator();
    }
}

void GameFlow::ProcessRequest() {
    int& state = state_;
    if (state == 0) return;
    // The zone being left is saved (single player campaigns 1 and 3, or the host in campaign 1).
    if (GameMode() == 1 && (TheServerWorld()->sessionKind_ == 1 || TheServerWorld()->sessionKind_ == 3) && state == kStateZone) {
        LocalRecords records;
        ThePartyManager()->EndRedeploy(records.Party());
        ::LeaveZone(records.Party());
        ThePartyManager()->BeginRedeploy(records.Party());
    }
    if (GameMode() == 2 && TheServerWorld()->sessionKind_ == 1 && state == kStateZone) {
        LocalRecords records;
        ::LeaveZone(records.Party());
    }
    TheInterface()->CloseAll();
    TheServerWorld()->TeardownZone();
    UseResources(Active(net::GameClient()));

    auto* clientWorld = TheClientWorld();
    switch (state) {
    case kStateMainMenu:
        ToMainMenu() = 1;
        StartZone(kMainMenu, 2);
        break;
    case kStateLoad:
        clientWorld->SetPlayer(TheServerWorld()->shared5C_.Get());
        ClientWorldLocalView(clientWorld)->sendParty_ = 0;
        TheServerWorld()->SetGameResult(1);
        clientWorld->Segment();
        TheServerWorld()->Update();
        clientWorld->Segment();
        TheServerWorld()->Update();
        LoadGame(requestTarget_.c_str());
        if (requestArg_) {
            const char* autoplay = kAutoplay;
            if (autoplay) {
                ClientWorldLocalView(clientWorld)->StartRecording(autoplay);
                AutoplayOn() = 1;
            } else {
                ClientWorldLocalView(clientWorld)->StopRecording();
                AutoplayOn() = 0;
            }
        }
        break;
    case kStateNewGame:
        // The same steps as the matching start function (inlined here), Rebirth's AutoExec #3 included.
        StartNewGame();
        break;
    case kStateZone:
        StartZone(requestTarget_.c_str(), 0);
        break;
    case kStatePartyZone: {
        const char* zone = requestTarget_.c_str();
        CPlayer* player = ClientWorldLocalView(clientWorld)->player_;
        player->AssignParty(RequestParty());
        TheServerWorld()->TeardownZone();
        if (!clientWorld->JoinGameClient(zone)) {
            if (Active(net::GameClient())) {
                BeginMultiplayerGame();
            } else {
                ToMainMenu() = 1;
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
        clientWorld->SetPlayer(TheServerWorld()->shared5C_.Get());
        ClientWorldLocalView(clientWorld)->sendParty_ = 1;
        break;
    }
    default:
        break;
    }
    state = 0;
    net::Pump();
}

namespace {

constexpr int kScenarioScriptsVersion = 0x47;      // scenario.sav from this version holds the input and scripts
constexpr int kScenarioGlobalsVersion = 0x52;      // and from this one the globals
void StreamLoadScripts(CMemoryStream* a0, void*, CStream* a2) { a0->ReadSized(a2); }

} // namespace

int GameFlow::LoadGame(const char* slot) {
    if (GameMode() == 2 || GameMode() == 3) return 0;
    // Rebirth's AutoExec #1 (the call redirected): addon's AutoExec lines first.
    if (ei::rebirth::Enabled(ei::rebirth::Patch::AutoExec)) console::RunAddonAutoExec();
    console::Execute(kExecAutoexec);
    TheCursor()->SetShape(3);
    ToMainMenu() = 1;
    FileStream file;
    std::string zone;
    std::string slotFolder;
    std::string currentFolder;
    slotFolder = str::Format(kSlotFolder, DataFolderText(), slot);
    currentFolder = str::Format(kCurrentFolder, DataFolderText());
    ClearSaveFolder(currentFolder.c_str());
    bool opened;
    {
        std::string path = slotFolder + kScenarioFile;
        opened = file.Open(path, kOpenRead, 1);
    }
    if (!opened) {
        ToMainMenu() = 1;
        StartZone(kMainMenu, 2);
        return 0;
    }

    // scenario.sav: the party, then (by version) the input, the scripts and the globals.
    CMemoryStream scenario;
    if (!SaveFileRead(&scenario, kMagicScenario, SaveVersion(), kSaveOldestVersion, &file)) {
        ToMainMenu() = 1;
        StartZone(kMainMenu, 2);
        return 0;
    }
    ThePartyManager()->Load(&scenario);
    int version = static_cast<int>(scenario.version_);
    if (version >= kScenarioScriptsVersion) {
        ScriptsReset(GameSaveStream());
        TheInterface()->LoadState(&scenario);
        StreamLoadScripts(&scenario, nullptr, GameSaveStream());
        version = static_cast<int>(scenario.version_);
        GameSaveStream()->version_ = static_cast<std::uint32_t>(version);
    }
    if (version >= kScenarioGlobalsVersion) TheCamera()->Load(&scenario);
    file.Close();

    // The visited zones of the current map (and bz7g) come back into saves\current; every other
    // zone, and one whose file could not be copied, is marked not visited.
    LocalRecords records;
    ThePartyManager()->EndRedeploy(records.Party());
    std::string currentMap;
    currentMap = FindZone(records.Party()->zone_)->allod;
    NamedFloatTable* variables = &records.Party()->variables_;
    for (int i = 0; i < ZoneRecordCount(); ++i) {
        MapRecord* record = ZoneRecords() + i;
        bool visited;
        {
            std::string prefix = kVisitedPrefix;
            std::string variable = prefix + record->name;
            double value = VariableGet(variables, variable.c_str());
            EI_NAN_AUDIT(value);
            visited = value != 0.0f;
        }
        bool restored = false;
        if (visited) {
            bool keep = gamecrt::mbscmp(record->name,
                                        kAlwaysCopied) == 0 ||
                        gamecrt::mbscmp(record->allod,
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
        VariableSet(variables, variable.c_str(), 0.0f);
    }
    ThePartyManager()->BeginRedeploy(records.Party());

    // The zone: kinds 0 and 2 come back from mission.sav, the others start afresh.
    MapRecord* zoneRecord = FindZone(records.Party()->zone_);
    int kind = zoneRecord->kind;
    if (kind != 0 && kind != 2) {
        StartZone(records.Party()->zone_.c_str(), 0);
    } else {
        {
            std::string path = slotFolder + kMissionFile;
            opened = file.Open(path, kOpenRead, 1);
        }
        if (!opened) return 0;
        LoadMission(&file);
        file.Close();
        TheServerWorld()->SetZoneExits(&zoneRecord->exits);
    }
    kConsole->PutText(kLoadedText)->PutText(slot)->PutChar('\n')->Apply(libcrt::FlushManipulator);
    return 1;
}

GameFlow* GameFlow::Construct() {
    std::construct_at(&requestTarget_);
    new (RequestParty()) CParty();
    std::construct_at(&zone7C_);
    state_ = 0;
    return this;
}

void GameFlow::Destruct() {
    std::destroy_at(&zone7C_);
    RequestParty()->~CParty();
    std::destroy_at(&requestTarget_);
}

// The flow, zero at the start: built by a static initialiser and destroyed by the exit function it
// registers (docs/notes/standalone.md, "The static initialisers' objects").
constinit lib::StaticObject<GameFlow> g_gameFlow = /*007AFE20*/ {};
GameFlow* TheGameFlow() { return &*g_gameFlow; }

namespace {
void DestroyGameFlow() { g_gameFlow->Destruct(); }
} // namespace

void ConstructGameFlow() {
    g_gameFlow->Construct();
    libcrt::AtExit(DestroyGameFlow);
}

void GameFlow::SetNetworkZone(const char* zone) {
    std::string folder;
    folder = str::Format(kCurrentFolder, DataFolderText());
    ClearSaveFolder(folder.c_str());
    zone7C_ = zone ? zone : "";   // null (LeaveZone): no zone
    if (Active(net::GameServer()) && !Active(net::GameClient()) && zone) StartNetworkZone(zone);
}

int CurrentSaveSize() {
    mfc::CFileFind find;
    std::string pattern;
    pattern = str::Format(kCurrentFiles, DataFolderText());
    int total = 1000000;
    if (find.FindFile(pattern, 0)) {
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
        variables->Clear();
    GlobalParty()->RemoveQuestItems();
    // The client's player record gets the hero's and the party's names (or the defaults).
    if (GlobalParty()->members_.size == 1) {
        const std::string* names = GlobalParty()->members_.data->texts;
        LocalLogin()->text_.assign(names[5].c_str());
        LocalLogin()->name_.assign(names[0].c_str());
    } else {
        LocalLogin()->text_.assign(kDefaultHeroName);
        LocalLogin()->name_.assign(kDefaultPartyName);
    }
    LocalLogin()->state_ = 1;
}

namespace {


unsigned g_serverStamp = /*007AFED0*/ {};
unsigned& ServerStamp() { return g_serverStamp; }   // the newest variable stamp seen
unsigned g_appliedStamp = /*007AFED4*/ {};
unsigned& AppliedStamp() { return g_appliedStamp; }  // the stamp applied up to

// The variable setter with the value as the raw dword that is pushed (no FPU load on the way).
void VariableSetBits(NamedFloatTable* table, const char* name, std::uint32_t bits) { table->Set(name, std::bit_cast<float>(bits)); }
// The client's STLport lists of counted messages (ClientSession::zones_, variables_).
using VariableNode = net::RefNode;
using ZoneNode = net::RefNode;
// The vector of changed variables (STLport vector<node*>) and the stamp sort (introsort,
// world_stamp_sort.cpp).

// A local NamedFloatTable (an empty one of 100 buckets, and a copy), released the way the inlined
// destructor does.
struct LocalVariables {
    NamedFloatTable table;
    ~LocalVariables() { table.Destroy(); }
};

VariableNode* VariableList() { return net::GameClient()->variables_.End(); }
net::VariableMessage* NodeRecord(VariableNode* node) { return net::GameClient()->variables_.At(node); }
unsigned NodeStamp(VariableNode* node) { return NodeRecord(node)->serial_; }
// The value's bits, moved as the dword that is pushed (no FPU load).
std::uint32_t ValueBits(const net::VariableMessage* record) {
    std::uint32_t bits;
    std::memcpy(&bits, &record->value_, 4);
    return bits;
}

} // namespace

void GameFlow::UpdateFromServer() {
    if (!Active(net::GameClient())) return;
    if (CTextManager* texts = TheTextManager()) {
        for (const std::string* file = g_questFiles.begin(); file != g_questFiles.end(); ++file)
            texts->RemoveSource(file->data());
        g_questFiles.EraseRange<stlp::ObjectElement<std::string>>(g_questFiles.begin(), g_questFiles.end());
    }
    auto* clientWorld = TheClientWorld();
    CPlayer* player = ClientWorldLocalView(clientWorld)->player_;
    LocalVariables unusedEmpty;
    unusedEmpty.table.ConstructDefault();
    NamedFloatTable* variables = &player->Party()->variables_;
// The local player's variables are replaced by the server's; "constr.current" survives. Rebirth's
// PersistentGlobalVars keeps the "gvar." ones.
    float construction = static_cast<float>(VariableGet(variables, kConstructionVariable));
    LocalVariables unusedCopy;
    unusedCopy.table.CopyConstruct(*variables);
    if (ei::rebirth::Enabled(ei::rebirth::Patch::PersistentGlobalVars))
        ClearVariablesKeepingGlobals(variables);
    else
        variables->Clear();
    VariableSet(variables, kConstructionVariable, construction);
    if (net::MapMessage* map = net::GameClient()->map_) {
        if (Active(map)) LoadZoneQuests(map->text_.data());
    }

    // The game type's construction variable and briefings, at least 1.
    std::string briefing;
    unsigned type = net::GameClient()->gameType_;
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
        float value = static_cast<float>(VariableGet(variables, (*name).c_str()));
        // The larger of the value and 1.0 (a NaN is kept).
        std::uint32_t bits;
        std::memcpy(&bits, &value, 4);
        bool keep = (bits & 0x7F800000u) == 0x7F800000u && (bits & 0x7FFFFFu) != 0 ? true : 1.0f < value;
        VariableSet(variables, (*name).c_str(), keep ? value : 1.0f);
    }

    // The briefings the server says were seen.
    for (ZoneNode* node = net::GameClient()->zones_.First(); node != net::GameClient()->zones_.End(); node = node->next)
        MarkBriefing(net::GameClient()->zones_.At(node)->text_.data());
    // The quest variables ("q.<folder>.<name>") register their quest folders.
    for (VariableNode* node = VariableList()->next; node != VariableList(); node = node->next) {
        std::string name = NodeRecord(node)->name_.data();
        bool quest;
        {
            std::string prefix = str::Left(name, 2);
            quest = gamecrt::mbscmp(prefix,
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
    if (ClientWorldLocalView(clientWorld)->player_->sessionKind100_ == 1) {
        unsigned previous = AppliedStamp();
        if (ServerStamp() > AppliedStamp() + 2) AppliedStamp() = ServerStamp() + 1;
        stlp::vector<VariableNode*> changed;
        changed.ConstructEmpty();
        for (VariableNode* node = VariableList()->next; node != VariableList(); node = node->next) {
            if (ServerStamp() <= NodeStamp(node)) ServerStamp() = NodeStamp(node);
            unsigned stamp = NodeStamp(node);
            if (stamp >= AppliedStamp() || stamp < previous) continue;
            if (changed.Finish() != changed.EndOfStorage()) {
                if (changed.Finish()) *changed.Finish() = node;
                ++changed.Finish();
            } else {
                VariableNode* value = node;
                changed.InsertOverflow<stlp::PlainElement<VariableNode*>>(changed.Finish(), value, 1);
            }
        }
        if (changed.begin() != changed.end()) {
            int depth = 0;
            for (int n = static_cast<int>(changed.size()); n != 1; n >>= 1) ++depth;
            StampIntroSort(changed.begin(), changed.end(), 0, depth * 2, variables);
            StampInsertionSort(changed.begin(), changed.end(), variables);
        }
        for (VariableNode** it = changed.begin(); it != changed.end(); ++it) GameFlow::OnVariableChanged(*it);
        bool settled;
        do {
            settled = true;
            for (VariableNode* node = VariableList()->next; node != VariableList(); node = node->next) {
                if (NodeStamp(node) > AppliedStamp()) continue;
                if (NodeStamp(node) == AppliedStamp()) {
                    GameFlow::OnVariableChanged(node);
                    ++AppliedStamp();
                    settled = false;
                }
                net::VariableMessage* record = NodeRecord(node);
                VariableSetBits(variables, record->name_.data(), ValueBits(record));
            }
        } while (!settled);
        stlp::deallocate(changed.begin(), static_cast<std::size_t>(changed.EndOfStorage() - changed.begin()) * 4);
    } else {
        for (VariableNode* node = VariableList()->next; node != VariableList(); node = node->next) {
            if (ServerStamp() <= NodeStamp(node)) ServerStamp() = NodeStamp(node);
            net::VariableMessage* record = NodeRecord(node);
            std::string name = record->name_.data();
            VariableSetBits(variables, record->name_.data(), ValueBits(record));
        }
        AppliedStamp() = 0;
    }
    UpdateMultiplayerVariable();
}

namespace {

// An entry is a multiplayer hero record (ui_screens.h); the entry's destructor is inlined here: the file
// name, then the party's.
void DestroyPartyEntry(MpHeroRecord* entry) {
    std::destroy_at(&entry->file);
    entry->Party()->~CParty();
}

} // namespace

void SavedParties::Reload() {
    MpHeroRecord*& finish = entries_.Finish();
    MpHeroRecord* end = MpHeroRecord::CopyRange(finish, finish, entries_.begin());
    for (MpHeroRecord* entry = end; entry != finish; ++entry) entry->DeletingDestruct(0);
    finish = end;

    WIN32_FIND_DATAA found;
    HANDLE search;
    {
        std::string pattern = folder_ + kPartyFilePattern;
        search = gameapi::FindFirstFileA(pattern.c_str(), &found);
    }
    if (search == INVALID_HANDLE_VALUE) return;
    do {
        // The party records and the file name, side by side as the list's entries are.
        lib::InPlace<MpHeroRecord> storage;
        auto* entry = storage.Get();
        new (entry->Party()) CParty();
        std::construct_at(&entry->file);
        entry->file = found.cFileName;
        if (entry->ReadFile(folder_.c_str())) {
            NamedFloatTable* variables = &entry->Party()->variables_;
            if (ei::rebirth::Enabled(ei::rebirth::Patch::PersistentGlobalVars)) {
                // Rebirth's PersistentGlobalVars: the inlined clear is skipped and the table is cleared
                // keeping its "gvar." entries.
                ClearVariablesKeepingGlobals(variables);
            } else {
                variables->Clear();
            }
            if (entry->Party()->members_.size == 1) {
                MpHeroRecord* eos = entries_.EndOfStorage();
                if (finish != eos) {
                    if (finish) {
                        new (finish->Party()) CParty(*entry->Party());
                        std::construct_at(&finish->file, entry->file);
                    }
                    ++finish;
                } else {
                    entries_.InsertOverflow<libcontainers::HeroRecordElement>(finish, *entry, 1);
                }
            }
        }
        DestroyPartyEntry(entry);
    } while (gameapi::FindNextFileA(search, &found));
    gameapi::FindClose(search);
}

namespace {

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

// push_back of an entry with two empty names (its id is not set).
void AppendEmptyEntry(ZoneTable& table) {
    if (table.Finish() != table.EndOfStorage()) {
        if (table.Finish()) {
            std::construct_at(&table.Finish()->first);
            std::construct_at(&table.Finish()->second);
        }
        ++table.Finish();
        return;
    }
    ZoneEntry empty{};
    table.InsertOverflow<recordvec::ZoneTableVector::Element>(table.Finish(), empty, 1);
}

} // namespace

void GameFlow::BuildZoneTable() {
    ZoneTable& table = g_zoneTable;
    if (table.end() != table.begin()) return;
    mfc::CFileFind find;
    int more;
    {
        std::string folder;
        modfolders::SelectMaps(kQuestFilePattern, modfolders::MapsSite::ZoneTable);
        MapsFolder(&folder);
        std::string pattern = folder + kQuestFilePattern;
        more = find.FindFile(pattern, 0);
    }
    while (more) {
        more = find.FindNext();
        std::string name = find.GetTitle().GetString();
        str::MakeLower(name);
        RegisterQuests(name.c_str());
        UnloadQuestTexts();
        std::string map;
        if (MapRecord* record = FindZone(name)) {
            map = record->map;
            const int allod = AllodOfMap(map.c_str());
            AppendEmptyEntry(table);
            ZoneEntry& entry = table.Finish()[-1];
            entry.first = name;
            entry.second = map;
            entry.id = allod;
        }
    }
    find.Close();
}
