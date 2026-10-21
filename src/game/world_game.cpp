// The game flow object (007AFE20; the state machine 0057C700 runs on it, Rebirth patches that one):
// saving a game into saves\<slot>, entering a zone. See docs/notes/world.md.
#include "camera.h"
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

#include "lib/crt.h"
#include "lib/crt_iostream.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "lib/stlp.h"
#include "lib/mfc_array.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "ei/vcall.h"
#include "originals_core.h"
#include "originals_engine.h"
#include "originals_gameplay.h"
#include "originals_library.h"
#include "originals_script.h"
#include "originals_ui.h"
#include "originals_world.h"
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

#include <cstring>
#include <new>
#include <bit>

namespace {

using namespace flowdata;


// ---- Data -------------------------------------------------------------------------------------


constexpr unsigned kOpenWrite = netlink::FileStream::kOpenWrite;
constexpr unsigned kOpenRead = netlink::FileStream::kOpenRead;
constexpr std::uint32_t kStreamLimit = 2000000000;
constexpr unsigned kMagicScenario = 0xDEAD;
constexpr unsigned kMagicMission = 0x102;

int GameMode() { return ei::GameData<const int>(0x007B0098); }        // 1: single player
int Campaign() { return ei::GameData<const int>(0x007C2CC4); }        // 4: the party is player 0's
int SaveVersion() { return ei::GameData<const int>(0x00790260); }
std::uint8_t& ToMainMenu() { return ei::GameData<std::uint8_t>(0x007C7D94); }
// 007C7DD8: the network sessions are open (BeginMultiplayerGame sets it, a missing zone clears it).
std::uint8_t& SessionsOpen() { return ei::GameData<std::uint8_t>(0x007C7DD8); }

void* const kScripts = ei::GamePointer<void>(0x007AFDF8);
libcrt::Ostream* const kConsole = ei::GamePointer<libcrt::Ostream>(0x007C55A8);

// The map database's zone records (MapRecord, object_tables.h).
MapRecord* ZoneRecords() { return TheMapDatabase()->maps.data; }
int ZoneRecordCount() { return TheMapDatabase()->maps.size; }
// CMapDatabase::FindMap (0045E850).
MapRecord* FindZone(const char* name) { return TheMapDatabase()->FindMap(name); }

// A CStream over a file (net_link.h).
using netlink::FileStream;

// ---- Callees not rewritten (EI_ORIGINAL forwarders; __thiscall called as __fastcall) ----------

// The mouse cursor's busy shape (3 while saving, 0 after): TheCursor()->SetShape (005E5200).
// 00594180 (RET 4). CWorldServer::GetPlayer (world_server.cpp), through its address.
// The party (007AA5A0) takes the records from / gives them to a record builder (00661050 constructs one, 006611D0
// destroys it): PartyManagerBeginRedeploy / PartyManagerEndRedeploy.
// 0045E850 (RET 4). CMapDatabase::FindMap: the zone record named `name`, or null.
// 00460AF0 / 00460BC0. Named float table Get (ST0) / Set (RET 8).
double VariableGet(NamedFloatTable* table, const char* name) { return table->Get(name); }
void VariableSet(NamedFloatTable* table, const char* name, float value) { table->Set(name, value); }
// 0057C460 (RET 8). Starts zone `zone` (mode 2: the main menu). Patched by Rebirth: original address.
__declspec(naked) void __fastcall StartZoneAt(GameFlow*, void*, const char*, int) EI_ORIGINAL(0x0057C460)
// The scenario.sav parts: the party (004A8560), the input state (005F85C0 save), the
// scripts (004A54D0 reset, 004A5250 save into a stream), the globals (00410C10).
__declspec(naked) void __fastcall PartySave(void*, void*, CMemoryStream*) EI_ORIGINAL(0x004A8560)
void ScriptsReset(void* scripts) { reinterpret_cast<CMemoryStream*>(scripts)->Resize(0); }
// The console's ostream.

const mfc::CString& RecordName(MapRecord* record) { return record->name; }
const mfc::CString& RecordAllod(MapRecord* record) { return record->allod; }
const mfc::CString& RecordMob(MapRecord* record) { return record->mob; }
const mfc::CString& RecordMap(MapRecord* record) { return record->map; }

// A file stream local (zero-filled first, then built and torn down as the original inlines it).
struct LocalFileStream {
    FileStream stream;
    LocalFileStream() {
        std::memset(static_cast<void*>(&stream), 0, sizeof(stream));
        stream.Construct();
    }
    ~LocalFileStream() { stream.Destruct(); }
    LocalFileStream(const LocalFileStream&) = delete;
    LocalFileStream& operator=(const LocalFileStream&) = delete;

    // Opens `path` (`reading`: the stream's direction byte, 1 to read, 0 to write) through the
    // embedded CFile's virtual Open, not the stream's.
    bool Open(const char* path, unsigned flags, std::uint8_t reading) { return stream.Open(path, flags, reading); }
    void Close() { stream.Close(); }
};

// The record builder of the party (PartyRecords, unit.h), built and torn down by the originals
// (a union member, so the compiler neither constructs nor destroys it).
struct LocalRecords {
    union {
        PartyRecords party;
    };
    LocalRecords() { reinterpret_cast<CParty*>(&party)->Construct(); }
    ~LocalRecords() { PartyRecordsDestructOriginal(&party); }
    LocalRecords(const LocalRecords&) = delete;
    LocalRecords& operator=(const LocalRecords&) = delete;
};

// Copies saves\current\<zone>.sav of every visited zone that has to be kept (the current map's
// zones and bz7g) into the slot folder; a zone whose file could not be copied, and every other
// zone, is marked not visited ("zs.<zone>" = 0).
void CopyVisitedZones(const mfc::CString& slotFolder, const mfc::CString& currentFolder, mfc::CString& zone,
                      const mfc::CString& currentMap, NamedFloatTable* variables) {
    for (int i = 0; i < ZoneRecordCount(); ++i) {
        MapRecord* record = ZoneRecords() + i;
        const char* name = record->name;
        bool visited;
        {
            mfc::CString prefix(kVisitedPrefix);
            mfc::CString variable = prefix + name;
            double value = VariableGet(variables, variable);
            EI_NAN_AUDIT(value);
            visited = value != 0.0f;
        }
        bool keep = false;
        if (visited) {
            keep = gamecrt::mbscmp(record->name,
                                   kAlwaysCopied) == 0 ||
                   gamecrt::mbscmp(record->allod,
                                   static_cast<const char*>(currentMap)) == 0;
        }
        if (keep) {
            zone = RecordName(record);
            bool failed;
            {
                mfc::CString slotPath = slotFolder + zone;
                mfc::CString slotFile = slotPath + kSaveExtension;
                mfc::CString currentPath = currentFolder + zone;
                mfc::CString currentFile = currentPath + kSaveExtension;
                failed = gameapi::CopyFileA(currentFile, slotFile, FALSE) == 0;
            }
            if (!failed) continue;
        }
        mfc::CString prefix(kVisitedPrefix);
        mfc::CString variable = prefix + record->name;
        VariableSet(variables, variable, 0.0f);
    }
}

} // namespace

// The game's save stream (declared in core_streams.h).
CMemoryStream* GameSaveStream() { return ei::GamePointer<CMemoryStream>(0x007AFDF8); }

int GameFlow::SaveGame(const char* slot) {
    LocalFileStream file;
    mfc::CString zone;          // [0x14]
    mfc::CString slotFolder;    // [0x10]
    mfc::CString currentFolder; // [0x18]
    if (GameMode() != 1) return 0;

    TheCursor()->SetShape(3);
    slotFolder.Format(kSlotFolder, DataFolderText(), slot);
    currentFolder.Format(kCurrentFolder, DataFolderText());
    ClearSaveFolder(slotFolder);
    if (Campaign() == 4) {
        if (WorldPlayer* player = static_cast<WorldPlayer*>(TheServerWorld()->GetPlayer(0)))
            ThePartyManager()->BeginRedeploy(player->Party());
    }
    LocalRecords records;
    ThePartyManager()->EndRedeploy(&records.party);
    mfc::CString currentMap;    // [0x1C]
    currentMap = RecordAllod(FindZone(records.party.zone));

    CopyVisitedZones(slotFolder, currentFolder, zone, currentMap, &records.party.variables);
    ThePartyManager()->BeginRedeploy(&records.party);

    bool opened;
    {
        mfc::CString path = slotFolder + kScenarioFile;
        opened = file.Open(path, kOpenWrite, 0);
    }
    if (!opened) {
        ToMainMenu() = 1;
        StartZoneAt(this, nullptr, kMainMenu, 2);
        return 0;
    }

    // scenario.sav: the party, the input state, the scripts and the globals.
    CMemoryStream scenario;
    std::memset(&scenario, 0, sizeof(scenario));
    ei::SetVtable(&scenario, kMemoryStreamVtable);
    scenario.data_ = nullptr;
    scenario.capacity_ = 0;
    scenario.position_ = 0;
    scenario.size_ = 0;
    scenario.ok_ = 1;
    scenario.bitMask_ = 0x80;
    scenario.bitsIndex_ = -1;
    scenario.version_ = static_cast<std::uint32_t>(SaveVersion());
    struct ScenarioOwner {   // frees the buffer on the way out (CMemoryStream's inlined destructor)
        CMemoryStream* s;
        ~ScenarioOwner() {
            ei::SetVtable(s, kMemoryStreamVtable);
            if (s->data_) gamecrt::operator_delete(s->data_);
            ei::SetVtable(s, kStreamVtable);
        }
    } scenarioOwner{&scenario};
    PartySave(ThePartyManager(), nullptr, &scenario);
    TheInterface()->SaveState(&scenario);
    ScriptsReset(kScripts);
    TheInterface()->SaveWindowState();
    static_cast<CStream*>(&scenario)->WriteSized(static_cast<CStream*>(kScripts));
    ScriptsReset(kScripts);
    TheCamera()->SaveState(&scenario);
    SaveFileWrite(&file.stream, kMagicScenario, SaveVersion(), &scenario);
    file.Close();

    // mission.sav: the world, except in zones without one (kind 1: cutscenes).
    int kind = FindZone(records.party.zone)->kind;
    if (kind == 0 || kind == 2) {
        {
            mfc::CString path = slotFolder + kMissionFile;
            opened = file.Open(path, kOpenWrite, 0);
        }
        if (!opened) return 0;
        CMemoryStream mission;
        std::memset(&mission, 0, sizeof(mission));
        mission.version_ = kStreamLimit;
        mission.ok_ = 1;
        ei::SetVtable(&mission, kMemoryStreamVtable);
        mission.data_ = nullptr;
        mission.Reset();
        mission.version_ = static_cast<std::uint32_t>(SaveVersion());
        TheServerWorld()->SaveWorld(&mission);
        SaveFileWrite(&file.stream, kMagicMission, SaveVersion(), &mission);
        ei::SetVtable(&mission, kMemoryStreamVtable);
        if (mission.data_) gamecrt::operator_delete(mission.data_);
        file.Close();
    }

    TheCursor()->SetShape(0);
    kConsole->PutText(kSaved)->PutText(slot)->PutChar('\n')->Apply(libcrt::FlushManipulator);
    return 1;
}

// ---- EnterZone --------------------------------------------------------------------------------

namespace {

constexpr float kCatchUpRadius = 70.0f;
constexpr std::uint32_t kStartTime = 0x40800000;        // 4.0f
constexpr int kMaxCatchUpTicks = 1000;


const mfc::CString& MoviesFolder() { return TheDiscFolder(); }


// 005DB3F0. Plays a movie file (ECX path, EDX 0, five flags).
// 00460DB0 (RET 4). Named float table Remove.
// 00579830 (RET 0xC). Reads a save file through the file stream into `out` (ECX), checking the
// header (magic in EDX, version, minimum version); false when it is corrupt.
// 0058D3D0 / 0058DCD0 / 005915E0. CWorldServer::WakeUnitsAround, Tick and SetZoneExits, through their
// addresses (rewritten in world_tick / world_zone).
// 00595480 (RET 0xC). CWorldServer::NewZone(map, mob, zone). Patched by Rebirth: original address.

// The zone was saved before: read saves\current\<zone>.sav and restore it, then let the world
// catch up with the time spent away (one tick per elapsed unit, at most 1000, waking the units
// around the entrance). Returns LoadSave's result, 0 when the file cannot be opened or is
// corrupt.
int RestoreZone(PartyRecords* records, MapRecord* zoneRecord, const mfc::CString& zone, mfc::CString& folder,
                const char* mapOverride, LocalFileStream& file, bool& failed) {
    NamedFloatTable* variables = &records->variables;
    folder.Format(kCurrentFolder, DataFolderText());
    mfc::CString fileName = zone + kSaveExtension;
    bool opened;
    {
        mfc::CString path = folder + fileName;
        opened = file.Open(path, kOpenRead, 1);
    }
    if (!opened) {
        failed = true;
        return 0;
    }
    CMemoryStream data;
    std::memset(&data, 0, sizeof(data));
    data.version_ = kStreamLimit;
    data.ok_ = 1;
    ei::SetVtable(&data, kMemoryStreamVtable);
    data.data_ = nullptr;
    data.Reset();
    struct DataOwner {
        CMemoryStream* s;
        ~DataOwner() {
            ei::SetVtable(s, kMemoryStreamVtable);
            if (s->data_) gamecrt::operator_delete(s->data_);
            ei::SetVtable(s, kStreamVtable);
        }
    } owner{&data};
    if (!SaveFileRead(&data, kMagicMission, SaveVersion(), ei::GameData<const int>(0x00790268), &file.stream)) {
        game::FatalError(kCorruptSave, static_cast<const char*>(fileName));
        failed = true;
        return 0;
    }
    int result = TheServerWorld()->LoadSave(&data, mapOverride);   // 00596E70
    file.Close();

    int entrance = records->entrance;
    if (entrance >= 0 && entrance < zoneRecord->exits.size) {
        float now = static_cast<float>(VariableGet(variables, kGTime));
        float left;
        {
            mfc::CString prefix(kLeftPrefix);
            mfc::CString variable = prefix + zone;
            left = static_cast<float>(VariableGet(variables, variable));
        }
        int ticks = static_cast<int>(gamecrt::Ftol((static_cast<double>(now) - left) * 100.0f /*0073BAB0*/));
        if (ticks >= kMaxCatchUpTicks) ticks = kMaxCatchUpTicks;
        else if (ticks <= 0) return result;
        // The centre of the exit's deploy rectangle.
        const MapExit* exit = zoneRecord->exits.data + entrance;
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

// 0057A060 (__stdcall, RET 4). Enters the party's zone (records +0x68, entrance +0x6C): plays the
// pending "movie", restores the zone from saves\current when it was visited (zs.<zone>), otherwise
// loads it fresh (NewZone; gz6g picks its frozen or thawed map), then the zone's scripts and its
// exits. Returns 0 on failure, else the restore's result (1 for a fresh zone).
int __stdcall EnterZone(std::uint8_t* recordsBytes) {
    auto* records = reinterpret_cast<PartyRecords*>(recordsBytes);
    LocalFileStream file;
    int result = 1;
    mfc::CString folder;                                // [0x14]
    mfc::CString zone(records->zone);                   // [0x10]
    MapRecord* zoneRecord = FindZone(zone);
    if (!zoneRecord) return 0;

    NamedFloatTable* variables = &records->variables;
    if (static_cast<int>(gamecrt::Ftol(VariableGet(variables, kMovieVariable))) == 1) {
        if (gamecrt::mbscmp(static_cast<const char*>(zone),
                            kMovieZone) != 0) {
            {
                mfc::CString movie = MoviesFolder() + kMovieFile;
                PlayMovie(movie, 0, 1, 1, 1, 1, 1);
            }
            variables->Remove(kMovieVariable);
        }
    } else {
        variables->Remove(kMovieVariable);
    }

    if (gamecrt::mbscmp(zoneRecord->map,
                        game::EmptyText()) != 0) {
        const char* mapOverride = nullptr;
        if (gamecrt::mbsicmp(static_cast<const char*>(zone),
                            kFrozenZone) == 0) {
            double frozen = VariableGet(variables, kFrozenVariable);
            mapOverride = kFrozenMap;
            EI_NAN_AUDIT(frozen);
            if (frozen == 0.0f) mapOverride = kThawedMap;
        }
        bool visited;
        {
            mfc::CString prefix(kVisitedPrefix);
            mfc::CString variable = prefix + zone;
            double value = VariableGet(variables, variable);
            EI_NAN_AUDIT(value);
            visited = value != 0.0f;
        }
        if (visited) {
            bool failed = false;
            result = RestoreZone(records, zoneRecord, zone, folder, mapOverride, file, failed);
            if (failed) return 0;
        } else {
            mfc::CString map = RecordMap(zoneRecord) + kMapExtension;
            if (mapOverride) map = mapOverride;
            bool loaded;
            {
                mfc::CString mob = RecordMob(zoneRecord) + kMobExtension;
                loaded = WorldNewZoneOriginal((void*)TheServerWorld(), nullptr, map, mob, zoneRecord->name) != 0;
            }
            if (!loaded) return 0;
            zoneRecord = FindZone(zone);
            TheServerWorld()->SetTimeImpl(std::bit_cast<float>(kStartTime));
        }
        if (zoneRecord->kind == 2) {
            mfc::CString scripts = RecordMob(zoneRecord) + kMobExtension;
            TheServerWorld()->LoadZoneScripts(scripts);
        } else {
            ScriptResetZone(TheServerWorld()->MainScript());   // 004BEB00: the scripts reset for a zone without its own
        }
    }
    TheServerWorld()->SetZoneExits(reinterpret_cast<const ZoneExitArray*>(&zoneRecord->exits));
    return result;
}

// ---- StartNetworkZone -------------------------------------------------------------------------

namespace {

constexpr int kGameResultNetwork = 2;
constexpr int kHostPort = 8000;
constexpr int kServerMessageZone = 8;

int& CampaignMode() { return ei::GameData<int>(0x007C2CC4); }
int& SpectatorFlag() { return ei::GameData<int>(0x0079BE6C); }

// 0056FFE0. CArray<ZoneExit> constructor (empty, base vtable 0073C3F4).
// The host announcement of a network game: gethostname (WS2_32, through 006E6100), the address
// (00440440 builds it from the host and port, 00435010 formats it into an STLport string, 00425040
// destroys that), the server's host record (0x24 bytes: 004186E0, two strings 0041F460, vtable
// 0073BCA0; counted pointer at server + 0x25A0: 00413270 release, 00418BE0 share; 00426A80
// assigns a string from a range) and the message 0041A500(8, zone).

// A session or a replicated record is usable: there, and not detached by its owner.
bool Active(const CRefCounted* object) { return object && !(object->flags7_ & CRefCounted::kDetached); }

// Tells the clients of the game server where the game is: the host's address and the zone.
void AnnounceHost(const char* zone) {
    char host[100];
    if (libcrt::imports::GetHostName(host, sizeof(host)) != 0) return;
    net::NetAddress address{};
    address.Parse(host, kHostPort);   // 00440440
    stlp::string text;
    mfc::CString formatted(address.ToString(&text)->data());   // 00435010
    // (the count cleared before the null test)
    auto* memory = static_cast<net::HostMessage*>(net::NewRefObject(sizeof(net::HostMessage)));
    net::HostMessage* record = nullptr;
    if (memory) {
        net::MessageConstructBase(memory);   // 004186E0
        stlp::StringElement::Default(&memory->address_);
        stlp::StringElement::Default(&memory->zone_);
        ei::SetVtable(memory, net::HostMessage::kVtableAddress);
        record = memory;
    }
    RefPtr& slot = net::GameServer()->hostInfo_;
    if (record != slot.ptr) {
        slot.Destroy();
        slot.Share(record);
    }
    const char* text1 = formatted;
    auto* shared = static_cast<net::HostMessage*>(net::GameServer()->hostInfo_.ptr);
    shared->address_.Assign(text1);
    shared = static_cast<net::HostMessage*>(net::GameServer()->hostInfo_.ptr);
    shared->zone_.Assign(zone);
    net::GameServer()->AnnounceToGuests(kServerMessageZone, zone);
}

} // namespace

// 0057BDF0 (__stdcall, RET 4). Starts network zone `zone` (a zone record of kind 0): tears the
// current zone down, restores saves\current\<zone>.sav when there is one (else loads the zone
// fresh at time 4), copies its exits with every target set to "MPGame1", applies its two settings,
// marks the game a network one and, on a running game server, announces the host and loads the
// zone's .mob. Returns 1, 0 on failure.
int __stdcall StartNetworkZone(const char* zone) {
    ToMainMenu() = 1;
    MapRecord* zoneRecord = FindZone(zone);
    if (!zoneRecord || zoneRecord->kind != 0) return 0;
    TheServerWorld()->TeardownZone();

    mfc::CString folder;   // [0x14]
    LocalFileStream file;  // [0x20]
    folder.Format(kCurrentFolder, DataFolderText());
    bool opened;
    {
        mfc::CString path = folder + zone;
        mfc::CString fileName = path + kSaveExtension;
        opened = file.Open(fileName, kOpenRead, 1);
    }
    if (opened) {
        CMemoryStream data;
        std::memset(&data, 0, sizeof(data));
        data.version_ = kStreamLimit;
        data.ok_ = 1;
        ei::SetVtable(&data, kMemoryStreamVtable);
        data.data_ = nullptr;
        data.Reset();
        bool loaded = SaveFileRead(&data, kMagicMission, SaveVersion(), ei::GameData<const int>(0x00790268),
                                   &file.stream) &&
                      TheServerWorld()->LoadSave(&data, nullptr) != 0;
        if (loaded) file.Close();
        ei::SetVtable(&data, kMemoryStreamVtable);
        if (data.data_) gamecrt::operator_delete(data.data_);
        if (!loaded) {
            ei::SetVtable(&data, kStreamVtable);
            return 0;
        }
    } else {
        bool loaded;
        {
            mfc::CString mob = RecordMob(zoneRecord) + kMobExtension;
            mfc::CString map = RecordMap(zoneRecord) + kMapExtension;
            loaded = WorldNewZoneOriginal((void*)TheServerWorld(), nullptr, map, mob, zone) != 0;
        }
        if (!loaded) return 0;
        TheServerWorld()->SetTimeImpl(std::bit_cast<float>(kStartTime));
    }

    // The zone's exits, all leading to "MPGame1".
    ZoneExitArray exits;
    mfc::impl::ConstructArrayOf<mfc::impl::ExitArrayClass>(exits);
    exits.vtable = ei::GamePointer<void>(kExitArrayVtable);
    const auto* source = reinterpret_cast<const ZoneExitArray*>(&zoneRecord->exits);
    gamearrays::ExitsSetSize(&exits, source->size);
    for (int i = 0; i < source->size; ++i) {
        alignas(ZoneExit) std::uint8_t copy[sizeof(ZoneExit)];
        ZoneExit* exit = source->CopyAt(reinterpret_cast<ZoneExit*>(copy), i);
        exits.data[i].Assign(exit);
        exit->target.~CString();
    }
    for (int i = 0; i < exits.size; ++i) exits.data[i].target = kNetworkExitTarget;
    TheServerWorld()->SetZoneExits(&exits);
    TheServerWorld()->SetClimate(zoneRecord->setting2C);
    TheServerWorld()->SetCave(zoneRecord->setting30);
    CampaignMode() = 1;
    TheServerWorld()->SetGameResult(kGameResultNetwork);
    SpectatorFlag() = 1;

    if (Active(net::GameServer())) {
        AnnounceHost(zone);
        mfc::CString name(zone);
        mfc::CString mob = name + kMobExtension;
        TheServerWorld()->LoadMobFile(mob);
    }

    exits.vtable = ei::GamePointer<void>(kExitArrayBaseVtable);
    if (exits.data) {
        for (int i = 0; i < exits.size; ++i) exits.data[i].target.~CString();
        gamecrt::operator_delete(exits.data);
    }
    return 1;
}

// ---- LeaveZone --------------------------------------------------------------------------------

namespace {


// 00410860 (RET 4). The globals (0079B2A8) note the campaign's progress.
// 00460BC0 (RET 8). Named float table Set.

} // namespace

mfc::CString* CWorldServer::ZoneNameInto(mfc::CString* out) {
    new (out) mfc::CString(zoneName_);
    return out;
}

// 0057A060's counterpart (__stdcall, RET 4; from the state machine): saves the current zone into
// saves\current\<zone>.sav (SaveWorld, header 0x102) and records in the party's variables that
// it was visited (zs.<zone> = 1) and when it was left (zt.<zone> = gtime). Returns 1, 0 when the
// file cannot be written (after FatalError).
int __stdcall LeaveZone(std::uint8_t* recordsBytes) {
    auto* records = reinterpret_cast<PartyRecords*>(recordsBytes);
    LocalFileStream file;
    mfc::CString path;   // [0x04]
    mfc::CString zone(mfc::CString::kUnset);   // [0x08]
    TheServerWorld()->ZoneNameInto(&zone);
    // `path` must be destroyed after `zone`, as the original does: it is declared first.

    if (Campaign() == 1) TheCamera()->SaveView(7);
    else if (Campaign() == 3) TheCamera()->SaveView(6);
    path.Format(kCurrentFolder, DataFolderText());
    {
        mfc::CString fileName = zone + kSaveExtension;
        path += fileName;
    }
    if (!file.Open(path, kOpenWrite, 0)) {
        game::FatalError(kCantWrite, static_cast<const char*>(path));
        return 0;
    }
    TheServerWorld()->PrepareSave();
    CMemoryStream data;
    std::memset(&data, 0, sizeof(data));
    data.version_ = kStreamLimit;
    data.ok_ = 1;
    ei::SetVtable(&data, kMemoryStreamVtable);
    data.data_ = nullptr;
    data.Reset();
    data.version_ = static_cast<std::uint32_t>(SaveVersion());
    TheServerWorld()->SaveWorld(&data);
    SaveFileWrite(&file.stream, kMagicMission, SaveVersion(), &data);
    file.Close();

    NamedFloatTable* variables = &records->variables;
    {
        mfc::CString prefix(kVisitedPrefix);
        mfc::CString variable = prefix + static_cast<const char*>(zone);
        VariableSet(variables, variable, 1.0f);
    }
    float now = static_cast<float>(VariableGet(variables, kGTime));
    {
        mfc::CString prefix(kLeftPrefix);
        mfc::CString variable = prefix + zone;
        VariableSet(variables, variable, now);
    }
    ei::SetVtable(&data, kMemoryStreamVtable);
    if (data.data_) gamecrt::operator_delete(data.data_);
    ei::SetVtable(&data, kStreamVtable);
    return 1;
}

// ---- The multiplayer flag -----------------------------------------------------------------------

namespace {

// The local client's player record (ClientSession::player_, a LoginMessage): its state_ is the
// game state request to the server (a change goes out with a new version_).
net::LoginMessage* LocalLogin() { return static_cast<net::LoginMessage*>(net::GameClient()->player_.ptr); }
// The server world's player list (a CPlayerManager): the number of players registered.
int ServerPlayerCount() { return static_cast<int>(TheServerWorld()->players_->objects_.Size()); }

} // namespace

void GameFlow::UpdateMultiplayerVariable() {
    WorldPlayer* player = reinterpret_cast<LocalViewData*>(TheClientWorld()->LocalView())->player;
    NamedFloatTable* variables = &player->variables;
    if (!Active(net::GameClient())) return;
    auto set = [variables](bool on) {
        if (on) VariableSet(variables, kMultiplayerVariable, 1.0f);
        else variables->Remove(kMultiplayerVariable);
    };
    if (!Active(net::GameServer())) {
        // A client: set while it has no host announcement (info_).
        set(!Active(net::GameClient()->info_));
        return;
    }
    // The host: with +0x80, set while more than one player is in; otherwise while the client
    // has no map message (map_).
    if (flag80_) set(ServerPlayerCount() > 1);
    else set(!Active(net::GameClient()->map_));
}

// ---- Starting a multiplayer game ------------------------------------------------------------------

namespace {

// The multiplayer start zones by the client session's game type (gameType_): 1..3 -> bz2mpg ..
// bz4mpg, anything else bz1mpg.

// The party manager (007AA5A0): 004A88C0 resets it (PartyManagerNewGameFour), 004A8890 (originals_world.h)
// begins a redeploy of a party.

// The party outside a game (007C82B0, the party records as WorldPlayer::Party has them).
PartyRecords* GlobalParty() { return ei::GamePointer<PartyRecords>(0x007C82B0); }
// A CParty seen as its records (PartyRecords) or through an untyped pointer; Assign / CopyConstruct
// (00661300 / 006613C0) read the source non-const.
CParty* AsParty(PartyRecords* records) { return reinterpret_cast<CParty*>(records); }
CParty* AsParty(const void* party) { return static_cast<CParty*>(const_cast<void*>(party)); }
constexpr std::uint32_t kMultiplayerHeroId = 0x3B9AF5CE;   // the only record's unit id

} // namespace

mfc::CString* GameFlow::MultiplayerZoneInto(mfc::CString* out) {
    unsigned type = net::GameClient()->gameType_;
    return new (out) mfc::CString(kMultiplayerZones[type <= 3 ? type : 0]);
}

void GameFlow::BeginMultiplayerGame() {
    PartyManagerNewGameFour(ThePartyManager());
    if (GlobalParty()->count == 1) GlobalParty()->records->unitId = kMultiplayerHeroId;
    {
        mfc::CString zone(mfc::CString::kUnset);
        GlobalParty()->zone = *MultiplayerZoneInto(&zone);
    }
    VariableSet(&GlobalParty()->variables, kMultiplayerVariable, 1.0f);
    ThePartyManager()->BeginRedeploy(GlobalParty());
    {
        mfc::CString zone(mfc::CString::kUnset);
        MultiplayerZoneInto(&zone);
        state_ = 4;   // the state: load the zone
        requestTarget_ = static_cast<const char*>(zone);
        requestArg_ = 0;
    }
    flag80_ = 0;
    // The client's game state request: 1, a new version.
    LocalLogin()->state_ = 1;
    ++LocalLogin()->version_;
    SessionsOpen() = 1;
}

// ---- Small glue of the flow -----------------------------------------------------------------------

namespace {

// 00661300 (RET 4). Party::CopyFrom. 00662AC0 (RET 4): the party sends its map transfer.

constexpr int kCommandBriefing = 1;
constexpr int kRequestDone = 3;   // the client's game state request: answered

} // namespace

void GameFlow::ReturnLocalParty() {
    if (!Active(net::GameClient())) return;
    WorldPlayer* player = reinterpret_cast<LocalViewData*>(TheClientWorld()->LocalView())->player;
    CParty* party = player->Party();
    AsParty(GlobalParty())->Assign(party);
    ThePartyManager()->BeginRedeploy(party);
    // 00662AC0: the global party record (007C82B0) written into the heroes' folder (007C82A0).
    reinterpret_cast<MpHeroRecord*>(GlobalParty())->WriteFile(ei::GameData<const char* const>(0x007C82A0));
    FlowUpdateFromServerOriginal(this);
}

void GameFlow::UpdateAndAcknowledge() {
    if (!Active(net::GameClient())) return;
    FlowUpdateFromServerOriginal(this);
    if (LocalLogin()->state_ != kRequestDone) return;
    LocalLogin()->state_ = 0;
    ++LocalLogin()->version_;
}

void GameFlow::RequestBriefing(const char* briefing) {
    net::ClientSession* client = net::GameClient();
    if (Active(client)) client->SendCommand(kCommandBriefing, briefing);
}

namespace {

// The last reference deletes the session (slot 0, the deleting destructor, with 1).
void ReleaseSession(CRefCounted* session) {
    if (session && --session->refs_ == 0) delete session;
}

} // namespace

void GameFlow::RecreateClientSession() {
    net::ClientSession*& global = net::GameClient();
    net::ClientSession* old = global;
    global = nullptr;
    ReleaseSession(old);
    void* memory = net::NewRefObject(sizeof(net::ClientSession));
    net::ClientSession* session = memory ? static_cast<net::ClientSession*>(memory)->Construct() : nullptr;
    net::ClientSession* previous = global;
    global = session;
    if (session) ++session->refs_;
    ReleaseSession(previous);
}

namespace {

__declspec(naked) int __fastcall SaveGameAt(GameFlow*, void*, const char*) EI_ORIGINAL(0x0057B3B0)

} // namespace

int GameFlow::SaveGameThunk(const char* slot) { return SaveGameAt(this, nullptr, slot); }

int __stdcall RequestZone(const char* zone, int entrance) {
    ei::GameData<char>(0x007C7D94) = 1;
    // 0066B1B0: the local view asks to go to a zone (name, entrance).
    return reinterpret_cast<CPlayerClient*>(TheClientWorld()->LocalView())->RequestZone(zone, entrance);
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
    state_ = 1;   // the state
}

// The flow's requests: the state (+0) its update (005800C0) acts on next, the zone or save name
// (+4, a CString), +0x78, and a party (+8) for states 5 and 6.
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

void GameFlow::RequestPartyZone5(const char* zone, const void* party) {
    state_ = 5;
    requestTarget_ = zone;
    AsParty(RequestParty())->Assign(AsParty(party));
}

void GameFlow::RequestPartyZone6(const char* zone, const void* party) {
    state_ = 6;
    requestTarget_ = zone;
    AsParty(RequestParty())->Assign(AsParty(party));
}

void GameFlow::DisconnectClient() { TheClientWorld()->DisconnectLocalPlayer(); }

// ---- ReadAIGraph ----------------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kGraphChunk = 0x31415926;         // the AI graph chunk in a .mob file

} // namespace

int CWorldServer::ReadAIGraph(const char* map, const char* mob, void* out) {
    LocalFileStream mobFile;
    int found = 0;
    if (mob) {
        // The graph chunk of the .mob file.
        {
            alignas(mfc::CString) std::uint8_t storage[sizeof(mfc::CString)];
            mfc::CString* path = (mfc::CString*)ServerMapPathOriginal((void*)this, nullptr, storage, mob);
            mobFile.Open(*path, kOpenRead, 1);
            path->~CString();
        }
        if (mobFile.stream.FindTagged(kGraphChunk, static_cast<CStream*>(out))) found = 1;
        mobFile.Close();
        if (found) return found;
    }
    // Otherwise <map without extension>.grp.
    alignas(mfc::CString) std::uint8_t storage[sizeof(mfc::CString)];
    mfc::CString* base = (mfc::CString*)ServerMapPathOriginal((void*)this, nullptr, storage, map);
    int dot = base->ReverseFind('.');
    if (dot > 0) {
        mfc::CString cut = base->Left(dot);
        *base = cut;
    }
    {
        LocalFileStream graph;
        bool opened;
        {
            mfc::CString path = *base + kGraphExtension;
            opened = graph.Open(path, kOpenRead, 1);
        }
        if (opened) {
            static_cast<CStream*>(out)->CopyFrom(&graph.stream);
            found = 1;
            graph.Close();
        }
    }
    base->~CString();
    return found;
}

// ---- LoadZoneScripts ------------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kScriptsChunk = 0xA000;          // the .mob file's scripts chunk
constexpr std::uint32_t kScriptEncoded = 0xACCEECCB;     // in it: the script text, packed and XORed
constexpr std::uint32_t kScriptPlain = 0xACCEECCA;       // in it: the script text as is

// The decoder (mob_script_decoder.h): 00598940 constructs an empty vector<char> (allocator by
// pointer); 0042D2C0 / 00598D80 free them (FreeStorage); 004D8E10 (RET 4) decodes `input` into `text` and the
// CString, called through its address so its verify wrapper sees the zone's scripts.
using CharVector = stlp::vector<char>;
// 004BE6E0 / 004D23A0 (RET 8). A script command queue: reset, add a line.

// A CMemoryStream on the stack, as the original builds it (limit, ok, no data, Reset) and frees it.
struct LocalMemoryStream {
    CMemoryStream s;
    LocalMemoryStream() {
        std::memset(&s, 0, sizeof(s));
        s.version_ = kStreamLimit;
        s.ok_ = 1;
        ei::SetVtable(&s, kMemoryStreamVtable);
        s.data_ = nullptr;
        s.Reset();
    }
    ~LocalMemoryStream() {
        ei::SetVtable(&s, kMemoryStreamVtable);
        if (s.data_) gamecrt::operator_delete(s.data_);
        ei::SetVtable(&s, kStreamVtable);
    }
    LocalMemoryStream(const LocalMemoryStream&) = delete;
    LocalMemoryStream& operator=(const LocalMemoryStream&) = delete;
};

} // namespace

void CWorldServer::LoadZoneScripts(const char* mob) {
    LocalFileStream file;
    mfc::CString script;
    LocalMemoryStream scripts;
    LocalMemoryStream text;
    MobScriptDecoder decoder;
    decoder.text.ConstructEmpty();   // 00598940
    decoder.input.ConstructEmpty();
    {
        alignas(mfc::CString) std::uint8_t storage[sizeof(mfc::CString)];
        mfc::CString* path = (mfc::CString*)ServerMapPathOriginal((void*)this, nullptr, storage, mob);
        file.Open(*path, kOpenRead, 1);
        path->~CString();
    }
    file.stream.FindTagged(kScriptsChunk, &scripts.s);
    if (scripts.s.FindTagged(kScriptEncoded, &text.s)) {
        const std::uint8_t* data = text.s.data_;
        std::uint32_t size = text.s.size_;
        char zero = 0;
        decoder.input.Resize<stlp::PlainElement<char>>(size, zero);
        std::memmove(decoder.input.begin(), data, size);
        decoder.Decode(&script);
    } else if (scripts.s.FindTagged(kScriptPlain, &text.s)) {
        text.s.SeekImpl(text.s.size_);
        char zero = 0;
        text.s.Write(&zero, 1u);
        script = reinterpret_cast<const char*>(text.s.data_);
    }
    ScriptReset(MainScript());
    ScriptLoadLine(commandsA_, nullptr, &script, 1);
    decoder.input.FreeStorage();
    decoder.text.FreeStorage();
}

// ---- SwitchMultiplayerZone ------------------------------------------------------------------------

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
            mfc::CString zone(mfc::CString::kUnset);
            MultiplayerZoneInto(&zone);
            ToMainMenu() = 1;
            reinterpret_cast<CPlayerClient*>(TheClientWorld()->LocalView())->RequestZone(zone, 0);   // 0066B1B0
        }
        if (host) {
            // The host sends command 9 with the announced zone and drops the announcement.
            auto* announced = static_cast<net::HostMessage*>(net::GameServer()->hostInfo_.ptr);
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
    WorldPlayer* player = static_cast<WorldPlayer*>(TheServerWorld()->GetPlayer(0));
    if (player && player->server->flag70_) {
        TheServerWorld()->SyncPartyRecords(player);
        AsParty(&records.party)->Assign(player->Party());
        TheServerWorld()->ClearPlayerItems(player);
    } else {
        AsParty(&records.party)->Assign(AsParty(GlobalParty()));
    }
    records.party.zone = zone7C_;
    RequestKind(3);
    if (Active(net::GameServer())) {
        state_ = 6;
        requestTarget_ = zone7C_;
        AsParty(RequestParty())->Assign(AsParty(&records.party));
    } else {
        auto* host = static_cast<net::HostMessage*>(net::GameClient()->info_);
        if (Active(host)) records.party.zone = host->zone_.data();
        host = static_cast<net::HostMessage*>(net::GameClient()->info_);
        if (Active(host)) {
            state_ = 5;
            requestTarget_ = host->address_.data();
            AsParty(RequestParty())->Assign(AsParty(&records.party));
        }
    }
    flag80_ = 1;
}

// ---- StartServer ----------------------------------------------------------------------------------

namespace {

constexpr std::size_t kPasswordSize = 0x14;

// 0057F560 / 0057EB10 (patched by Rebirth): the flow before / after starting the sessions.
__declspec(naked) void __fastcall FlowBeforeServer(GameFlow*, void*) EI_ORIGINAL(0x0057F560)
__declspec(naked) void __fastcall FlowAfterServer(GameFlow*, void*) EI_ORIGINAL(0x0057EB10)
// 00419CD0 (RET 4). A session reference (0079B5DC / 0079B5D8) assigned.
// 00440440 (RET 8). A 16-byte network address from a host name and a port.
__declspec(naked) bool __fastcall AddressSet(void*, void*, const char*, int) EI_ORIGINAL(0x00440440)

} // namespace

void GameFlow::StartServer(const char* name, int value25E0, int gameType, const char* password, char unlisted,
                           int value25FC) {
    FlowBeforeServer(this, nullptr);
    net::NetLog() = ei::GamePointer<net::LogStream>(kNetLogStream);
    {
        void* memory = net::NewRefObject(sizeof(net::ServerSession));
        net::ServerSession* server = memory ? static_cast<net::ServerSession*>(memory)->Construct(gameType) : nullptr;
        RefPtrAssign(&net::GameServer(), nullptr, server);
    }
    {
        void* memory = net::NewRefObject(sizeof(net::ClientSession));
        net::ClientSession* client = memory ? static_cast<net::ClientSession*>(memory)->Construct() : nullptr;
        RefPtrAssign(&net::GameClient(), nullptr, client);
    }
    // The local client connects to the local server with the password as its hello.
    alignas(4) std::uint8_t address[16] = {};
    AddressSet(address, nullptr, kLocalHost, ei::GameData<const int>(0x00783E30));
    {
        GameBuffer hello;
        static_cast<GameBuffer*>(&hello)->ConstructGrowing();
        char secret[kPasswordSize] = {};
        if (password) gamecrt::strncpy(secret, password, kPasswordSize);
        hello.BufferWrite(secret, static_cast<int>(kPasswordSize));
        static_cast<net::ClientBase*>(net::GameClient())->Join(reinterpret_cast<const net::NetAddress*>(address), &hello);
        GrowingBufferDestruct(&hello);
    }
    LocalLogin()->host_ = 1;
    const char* shown = name && *name ? name : kServerName;
    net::GameServer()->name_.Assign(shown);
    net::GameServer()->maxPlayers_ = value25E0;
    if (password && *password) net::GameServer()->SetPassword(password);
    net::GameServer()->SetPublic(unlisted == 0);
    net::GameClient()->gameType_ = gameType;
    net::GameServer()->masterKey_ = value25FC;
    FlowAfterServer(this, nullptr);
}

void GameFlow::StartClient() {
    net::NetLog() = ei::GamePointer<net::LogStream>(kNetLogStream);
    RefPtrAssign(&net::GameServer(), nullptr, nullptr);
    void* memory = net::NewRefObject(sizeof(net::ClientSession));
    net::ClientSession* client = memory ? static_cast<net::ClientSession*>(memory)->Construct() : nullptr;
    RefPtrAssign(&net::GameClient(), nullptr, client);
    FlowAfterServer(this, nullptr);
}
EI_REPLACE_METHOD(0x0057EA50, GameFlow::StartClient);

void GameFlow::JoinServer(const net::NetAddress* address, const char* password) {
    GameBuffer hello;
    static_cast<GameBuffer*>(&hello)->ConstructGrowing();
    char secret[kPasswordSize] = {};
    if (password) gamecrt::strncpy(secret, password, kPasswordSize);
    hello.BufferWrite(secret, static_cast<int>(kPasswordSize));
    static_cast<net::ClientBase*>(net::GameClient())->Join(const_cast<net::NetAddress*>(address), &hello);
    GrowingBufferDestruct(&hello);
}
EI_REPLACE_METHOD(0x0057E970, GameFlow::JoinServer);

// ---- AcceptPlayers --------------------------------------------------------------------------------

namespace {

libcrt::Ostream* const kNetLog = ei::GamePointer<libcrt::Ostream>(0x007C56B0);   // the network log's ostream

// 0066C180. The player list makes a new player. 00665990 (CPlayer::Connect): the player takes a connection.

} // namespace

void CWorldServer::AcceptNewPlayers(void* lobby) {
    mfc::CTypedPtrList<netlink::NetLink> joined;
    joined.vtable = ei::GamePointer<void>(kSessionListVtable);
    joined.head = nullptr;
    joined.tail = nullptr;
    joined.count = 0;
    joined.free = nullptr;
    joined.blocks = nullptr;
    joined.blockSize = 10;
    if (gameResult_ == 0) {
        mfc::impl::ListAs<gamelists::Refs>(&joined)->RemoveAll();   // 00598EF0: the connections released
        return;
    }
    static_cast<CPlayerSource*>(lobby)->TakeNew(&joined);   // the new connections
    for (auto* node = joined.head; node;) {
        auto* next = node->next;
        netlink::NetLink* connection = node->data;
        WorldPlayer* player = static_cast<WorldPlayer*>(PlayerCreateOriginal(players_));
        reinterpret_cast<CPlayer*>(player)->Connect(connection);
        player->dirty = 0xFFFFFFFFu;
        player->sessionKind = sessionKind_;
        std::memcpy(&player->server->lastPosition_, &startPosition_, 12);
        int id = player->id;
        kNetLog->PutText(kAcceptedPlayer)->PutInt(id)->PutChar('\n')->Apply(libcrt::FlushManipulator);
        node = next;
    }
    // The list's inlined destructor: every reference released, then the blocks.
    joined.vtable = ei::GamePointer<void>(kSessionListVtable);
    for (auto* node = joined.head; node; node = node->next) ReleaseSession(node->data);
    joined.count = 0;
    joined.free = nullptr;
    joined.tail = nullptr;
    joined.head = nullptr;
    mfc::PlexFreeDataChain(joined.blocks);
}

// ---- PickZones ------------------------------------------------------------------------------------

namespace {

// The zone table: a vector of 12-byte {first, second, id}, two CStrings and an int. Empty at the start, as the static
// initialiser 0057F460 leaves it; the exit function it registers destroys it (docs/notes/standalone.md, "The static
// initialisers' objects").
using ZoneEntry = recordvec::ZoneTableEntry;
using ZoneTable = stlp::vector<ZoneEntry>;
constinit stlp::vector<ZoneEntry> g_zoneTable = /*007AFDE0*/ {};
void __cdecl DestroyZoneTable() { recordvec::ZoneTableVector::Destruct(&g_zoneTable); }   // 0057F4A0
using OutStrings = stlp::vector<stlp::string>;

bool SameText(const char* a, const char* b) {
    return gamecrt::mbscmp(a, b) == 0;
}

// A list of entry pointers on the game's heap (the original keeps STLport lists of copies).
struct EntryPicks {
    const ZoneEntry** items = nullptr;
    int count = 0;
    explicit EntryPicks(int capacity) {
        items = static_cast<const ZoneEntry**>(gamecrt::operator_new(static_cast<std::size_t>(capacity > 0 ? capacity : 1) * 4));
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

void __stdcall PickZones(void* outArg, int id, const char* exclude) {
    auto* out = static_cast<OutStrings*>(outArg);
    const ZoneTable& table = g_zoneTable;
    int total = static_cast<int>(table.size());
    EntryPicks pending(total);
    EntryPicks group(total);
    for (ZoneEntry* e = table.begin(); e != table.end(); ++e)
        if (e->id == id) pending.items[pending.count++] = e;
    while (pending.count) {
        // The group of the first pending entry's second name, in table order.
        const char* key = pending.items[0]->second;
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
        unsigned pick = RandomNext((&IsaacRandom::Global())) % static_cast<unsigned>(group.count);
        stlp::string name(group.items[pick]->first);
        if (out->Finish() != out->EndOfStorage()) {
            new (out->Finish()) stlp::string(name);
            ++out->Finish();
        } else {
            out->InsertOverflow<stlp::StringElement>(out->Finish(), name, 1);   // 004DCE90
        }
    }
}

// ---- LoadMission ----------------------------------------------------------------------------------

namespace {

constexpr unsigned kMagicMissionFile = 0x102;
constexpr int kVersionGlobalsInMission = 0x52;   // below: the globals (0079B2A8) come from mission.sav
constexpr int kVersionInputInMission = 0x47;     // below: the input state (007C7D50) too

} // namespace

int __stdcall LoadMission(void* file) {
    TheSoundManager()->StopAll(1);
    LocalMemoryStream mission;
    if (!SaveFileRead(&mission.s, kMagicMissionFile, ei::GameData<const int>(0x00790260),
                      ei::GameData<const int>(0x00790268), file))
        return 0;
    int version = static_cast<int>(mission.s.version_);
    if (version < kVersionGlobalsInMission) TheCamera()->LoadState(&mission.s);
    if (static_cast<int>(mission.s.version_) < kVersionInputInMission) {
        // Old saves: the input state, then the rest goes to the stream at 007AFDF8.
        GameSaveStream()->Resize(0);
        TheInterface()->LoadState(&mission.s);
        static_cast<CStream*>(&mission.s)->ReadSized(static_cast<CStream*>(GameSaveStream()));
        ei::GameData<std::uint32_t>(0x007AFE00) = mission.s.version_;
    }
    return TheServerWorld()->LoadSave(&mission.s, nullptr);   // 00596E70
}

// ---- ClearSaveFolder ------------------------------------------------------------------------------

namespace {

// MFC CFileFind (0x1C bytes) members, and the Win32 calls through game.exe's import slots (harness
// runs redirect those to harness_saves).
using CreateDirectoryFn = BOOL(__stdcall*)(const char*, void*);
using DeleteFileFn = BOOL(__stdcall*)(const char*);
CreateDirectoryFn ImportCreateDirectory() { return ei::GameData<CreateDirectoryFn const>(0x0073B1B8); }
DeleteFileFn ImportDeleteFile() { return ei::GameData<DeleteFileFn const>(0x0073B20C); }

} // namespace

void __fastcall ClearSaveFolder(const char* path) {
    ImportCreateDirectory()(path, nullptr);
    mfc::CFileFind finder;
    int found;
    {
        mfc::CString folder(path);
        mfc::CString pattern = folder + kAllFiles;
        found = finder.FindFile(pattern, 0);
    }
    while (found) {
        found = finder.FindNext();
        mfc::CString file(static_cast<const mfc::CFileFind*>(&finder)->GetFilePath());
        ImportDeleteFile()(file);
    }
    finder.Close();
}

bool __fastcall NameInList(const char* const* list, const char* name) {
    for (; *list[0] != 0; ++list)
        if (libcrt::Stricmp(*list, name) == 0) return true;
    return false;
}

// ---- StartZone --------------------------------------------------------------------------------

namespace {


// StartZone's modes (also stored as the campaign mode, 007C2CC4).
constexpr int kModeFromRecord = 0;   // by the zone record's kind: 0 -> 1, 1 -> 4, 2 -> 3
constexpr int kModeMainMenu = 2;
constexpr int kModeEndOfGame = 8;    // the zone "endofgame"
// SetGameResult's values here.
constexpr int kResultPlaying = 1;
constexpr int kResultMainMenu = 4;


// 00566CE0 (RET 4) / 005699F0. The client world: the local player / its local view.
inline LocalViewData* ClientWorldLocalView(void* client) { return reinterpret_cast<LocalViewData*>(static_cast<CWorldClient*>(client)->LocalView()); }
// 0057A060 (__stdcall; the original passes the flow in ECX too). EnterZone above, through its address.
__declspec(naked) int __stdcall EnterZoneAt(PartyRecords*) EI_ORIGINAL(0x0057A060)

// `value` mod 24 through the game's _CIfmod (006E95AA), stored as float.
float HoursOfDay(double value) { return static_cast<float>(libcrt::CiFmod(value, 24.0)); }

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

    TheServerWorld()->SetTimeSpeedImpl(static_cast<float>(TimeSpeedSetting()));
    int result;
    if (mode == kModeMainMenu) {
        WorldNewZoneOriginal((void*)TheServerWorld(), nullptr, kMenuMap, kMenuMob, nullptr);
        // The menu shows the local time of day.
        SYSTEMTIME now;
        gameapi::GetLocalTime(&now);
        int minute = now.wMinute;
        int hour = now.wHour;
        TheServerWorld()->SetTimeImpl(static_cast<float>(minute) * 0.016666668f /*0073F8A0*/ + static_cast<float>(hour));
        result = kResultMainMenu;
        // Rebirth's AllowScriptsInMainMenu (a JMP at 0057C69F to a stub): the first script command
        // queue runs in the menu too.
        if (ei::rebirth::Enabled(ei::rebirth::Patch::AllowScriptsInMainMenu)) TheServerWorld()->scriptsActive_ = 1;
    } else if (mode == kModeEndOfGame) {
        result = kResultPlaying;
    } else {
        // The party enters the zone, then the clock is set from its "gtime".
        LocalRecords records;
        ThePartyManager()->EndRedeploy(&records.party);
        EnterZoneAt(&records.party);
        ThePartyManager()->BeginRedeploy(&records.party);
        TheServerWorld()->SetTimeImpl(HoursOfDay(VariableGet(&records.party.variables, kGTime)));
        if (zoneRecord) {
            TheServerWorld()->SetClimate(zoneRecord->setting2C);
            TheServerWorld()->SetCave(zoneRecord->setting30);
        }
        result = kResultPlaying;
    }

    CampaignMode() = mode;
    if (mode == 1) SpectatorFlag() = mode;
    TheServerWorld()->SetGameResult(result);
    // The server world's counted helper (+0x5C, a player source) handed to the client world.
    TheClientWorld()->SetPlayer(reinterpret_cast<std::uint8_t*>(TheServerWorld()->shared5C_.ptr));
    ClientWorldLocalView(TheClientWorld())->sendParty = 0;
}

// ---- New game (0057BD10) ------------------------------------------------------------------------

namespace {

// 005DC620 / 004A88A0 through their addresses: the console executor, the party manager's new game.
__declspec(naked) void __fastcall PartyNewGameAt(void*) EI_ORIGINAL(0x004A88A0)

} // namespace

void GameFlow::StartNewGame() {
    // Rebirth's AutoExec #2 (the CALL at 0057BD30 redirected): addon's AutoExec lines first.
    if (ei::rebirth::Enabled(ei::rebirth::Patch::AutoExec)) console::RunAddonAutoExec();
    ExecuteAt(kExecAutoexec);
    ToMainMenu() = 1;
    PartyNewGameAt(ThePartyManager());
    mfc::CString folder;
    folder.Format(kCurrentFolder, DataFolderText());
    ClearSaveFolder(folder);
    LocalRecords records;
    ThePartyManager()->EndRedeploy(&records.party);
    StartZoneAt(this, nullptr, records.party.zone, 0);
}

// ---- The flow's requests (0057C700) -------------------------------------------------------------

namespace {

constexpr int kStateMainMenu = 1, kStateLoad = 2, kStateNewGame = 3, kStateZone = 4, kStatePartyZone = 5,
              kStateNetworkZone = 6;

__declspec(naked) int __stdcall LeaveZoneAt(PartyRecords*) EI_ORIGINAL(0x0057A780)
__declspec(naked) int __stdcall StartNetworkZoneAt(const char*) EI_ORIGINAL(0x0057BDF0)
__declspec(naked) void __fastcall LoadGameAt(GameFlow*, void*, const char*) EI_ORIGINAL(0x0057ABA0)
// 005F74F0 (the interface 007C7D50): closes its windows for the zone change.
void __fastcall InterfaceCloseAll(void* a0) { static_cast<CInterface3D*>(a0)->CloseAll(); }
// The client world (007AF5B8): its tick (005641F0), a zone check (00566D80, AL); the server's
// update (0058DFF0); the network pump (004122D0).
int __fastcall ClientSegmentAt(void* client) { return ClientSegmentOriginal(client); }
int __fastcall ServerUpdateAt(CWorldServer* server) { return ServerUpdateOriginal(server); }
// The local view: autoplay on (00669710, name) / off (00669750); the local player takes the
// flow's party copy (00665CC0, +8).

mfc::CString& LastResources() { return ei::GameData<mfc::CString>(0x007AFDEC); }
std::uint8_t& LastResourcesGuard() { return ei::GameData<std::uint8_t>(0x007AFDF0); }
int& SavedSpectator() { return ei::GameData<int>(0x0079BEC4); }
int& Spectator0() { return ei::GameData<int>(0x0079BEC0); }
std::uint8_t& AutoplayOn() { return ei::GameData<std::uint8_t>(0x007B007C); }
// The resource directory (the CString 0079BC94), the sound system (007AF784) and the spell tables /
// item database globals the switch reloads.
const char* ResourcesPath() { return ei::GameData<const char* const>(0x0079BC94); }
UnitDatabase* UnitDatabaseObject() { return ei::GamePointer<UnitDatabase>(0x007AF358); }
void* SpellTablesObject() { return ei::GamePointer<void>(0x007C8510); }
void* ItemDatabaseObject() { return ei::GamePointer<void>(0x007C86D8); }

} // namespace

void __fastcall SwitchResourceDatabases(const char* name) {
    UnitDatabase* units = UnitDatabaseObject();
    units->Reset();
    UnitDatabaseLoadOriginal(units, nullptr, ResourcesPath(), name);
    units->CountSoundFiles(TheSoundManager());
    SkillDb()->Reset();
    SkillDatabaseLoadOriginal(SkillDb(), nullptr, ResourcesPath(), name);
    static_cast<itemdb::SpellDatabase*>(SpellTablesObject())->Reset();
    SpellTablesLoadOriginal(SpellTablesObject(), nullptr, ResourcesPath(), name);
    static_cast<itemdb::ItemDatabase*>(ItemDatabaseObject())->Reset();
    ItemDatabaseLoadOriginal(ItemDatabaseObject(), nullptr, ResourcesPath(), name);
}
EI_REPLACE(0x0057CB50, SwitchResourceDatabases);

void GameFlow::UseResources(bool network) {
    if (!(LastResourcesGuard() & 1)) {
        LastResourcesGuard() |= 1;
        new (&LastResources()) mfc::CString(kResourcesSingle);
        GameAtExit(reinterpret_cast<void(__cdecl*)()>(0x0057E6A0));
    }
    mfc::CString wanted(network ? kResourcesNetwork : kResourcesSingle);
    if (gamecrt::mbscmp(static_cast<const char*>(wanted),
                        static_cast<const char*>(LastResources())) == 0)
        return;
    LastResources() = wanted;
    SwitchResourceDatabases(wanted);
    if (network) {
        int value = Spectator0();
        Spectator0() = 0;
        SavedSpectator() = value;
    } else {
        Spectator0() = SavedSpectator();
    }
}
EI_REPLACE_METHOD(0x0057DEE0, GameFlow::UseResources);

void GameFlow::ProcessRequest() {
    int& state = state_;
    if (state == 0) return;
    // The zone being left is saved (single player campaigns 1 and 3, or the host in campaign 1).
    if (GameMode() == 1 && (Campaign() == 1 || Campaign() == 3) && state == kStateZone) {
        LocalRecords records;
        ThePartyManager()->EndRedeploy(&records.party);
        LeaveZoneAt(&records.party);
        ThePartyManager()->BeginRedeploy(&records.party);
    }
    if (GameMode() == 2 && Campaign() == 1 && state == kStateZone) {
        LocalRecords records;
        LeaveZoneAt(&records.party);
    }
    InterfaceCloseAll(TheInterface());
    TheServerWorld()->TeardownZone();
    // (0057DEE0's body is inlined here in the original, with the databases' switch 0057CB50 out of line.)
    UseResources(Active(net::GameClient()));

    auto* clientWorld = TheClientWorld();
    switch (state) {
    case kStateMainMenu:
        ToMainMenu() = 1;
        StartZoneAt(this, nullptr, kMainMenu, 2);
        break;
    case kStateLoad:
        static_cast<CWorldClient*>(clientWorld)->SetPlayer(ei::GameData<std::uint8_t*>(0x007AFFBC));
        ClientWorldLocalView(clientWorld)->sendParty = 0;
        TheServerWorld()->SetGameResult(1);
        ClientSegmentAt(clientWorld);
        ServerUpdateAt(TheServerWorld());
        ClientSegmentAt(clientWorld);
        ServerUpdateAt(TheServerWorld());
        LoadGameAt(this, nullptr, requestTarget_);
        if (requestArg_) {
            const char* autoplay = kAutoplay;
            if (autoplay) {
                reinterpret_cast<CPlayerClient*>(ClientWorldLocalView(clientWorld))->StartRecording(autoplay);
                AutoplayOn() = 1;
            } else {
                reinterpret_cast<CPlayerClient*>(ClientWorldLocalView(clientWorld))->StopRecording();
                AutoplayOn() = 0;
            }
        }
        break;
    case kStateNewGame:
        // The same steps as 0057BD10 (inlined here in the original), Rebirth's AutoExec #3 (the
        // CALL at 0057C9A3) included.
        StartNewGame();
        break;
    case kStateZone:
        StartZoneAt(this, nullptr, requestTarget_, 0);
        break;
    case kStatePartyZone: {
        const char* zone = requestTarget_;
        WorldPlayer* player = ClientWorldLocalView(clientWorld)->player;
        reinterpret_cast<CPlayer*>(player)->AssignParty(reinterpret_cast<CParty*>(RequestParty()));
        TheServerWorld()->TeardownZone();
        if (!static_cast<CWorldClient*>(clientWorld)->JoinGameClient(zone)) {
            if (Active(net::GameClient())) {
                BeginMultiplayerGame();
            } else {
                ToMainMenu() = 1;
                StartZoneAt(this, nullptr, kMainMenu, 2);
            }
        }
        ClientWorldLocalView(clientWorld)->sendParty = 1;
        break;
    }
    case kStateNetworkZone: {
        StartNetworkZoneAt(requestTarget_);
        WorldPlayer* player = ClientWorldLocalView(clientWorld)->player;
        reinterpret_cast<CPlayer*>(player)->AssignParty(reinterpret_cast<CParty*>(RequestParty()));
        static_cast<CWorldClient*>(clientWorld)->SetPlayer(ei::GameData<std::uint8_t*>(0x007AFFBC));
        ClientWorldLocalView(clientWorld)->sendParty = 1;
        break;
    }
    default:
        break;
    }
    state = 0;
    net::Pump();
}

// ---- LoadGame (0057ABA0) ------------------------------------------------------------------------

namespace {

constexpr int kScenarioScriptsVersion = 0x47;      // scenario.sav from this version holds the input and scripts
constexpr int kScenarioGlobalsVersion = 0x52;      // and from this one the globals
// The scenario.sav parts read back: the party (004A8310), the input state (005F85C0's reader
// 005F8420), the scripts (004A51C0, a stream method), the globals (00410AB0); the world from
// mission.sav (00579E60, a flow method, from the file stream).
__declspec(naked) void __fastcall PartyLoad(void*, void*, CMemoryStream*) EI_ORIGINAL(0x004A8310)
void __fastcall StreamLoadScripts(CMemoryStream* a0, void*, void* a2) { static_cast<CStream*>(a0)->ReadSized(static_cast<CStream*>(a2)); }
int& LoadedScriptsVersion() { return ei::GameData<int>(0x007AFE00); }

} // namespace

int GameFlow::LoadGame(const char* slot) {
    if (GameMode() == 2 || GameMode() == 3) return 0;
    // Rebirth's AutoExec #1 (the CALL at 0057ABE1 redirected): addon's AutoExec lines first.
    if (ei::rebirth::Enabled(ei::rebirth::Patch::AutoExec)) console::RunAddonAutoExec();
    ExecuteAt(kExecAutoexec);
    TheCursor()->SetShape(3);
    ToMainMenu() = 1;
    LocalFileStream file;
    mfc::CString zone;          // [0x14]
    mfc::CString slotFolder;    // [0x10]
    mfc::CString currentFolder; // [0x18]
    slotFolder.Format(kSlotFolder, DataFolderText(), slot);
    currentFolder.Format(kCurrentFolder, DataFolderText());
    ClearSaveFolder(currentFolder);
    bool opened;
    {
        mfc::CString path = slotFolder + kScenarioFile;
        opened = file.Open(path, kOpenRead, 1);
    }
    if (!opened) {
        ToMainMenu() = 1;
        StartZoneAt(this, nullptr, kMainMenu, 2);
        return 0;
    }

    // scenario.sav: the party, then (by version) the input, the scripts and the globals.
    CMemoryStream scenario;
    std::memset(&scenario, 0, sizeof(scenario));
    scenario.version_ = kStreamLimit;
    scenario.ok_ = 1;
    ei::SetVtable(&scenario, kMemoryStreamVtable);
    scenario.data_ = nullptr;
    scenario.Reset();
    struct ScenarioOwner {   // the buffer freed on the way out (the inlined destructor)
        CMemoryStream* s;
        ~ScenarioOwner() {
            ei::SetVtable(s, kMemoryStreamVtable);
            if (s->data_) gamecrt::operator_delete(s->data_);
            ei::SetVtable(s, kStreamVtable);
        }
    } scenarioOwner{&scenario};
    if (!SaveFileRead(&scenario, kMagicScenario, SaveVersion(), ei::GameData<const int>(0x00790268), &file.stream)) {
        ToMainMenu() = 1;
        StartZoneAt(this, nullptr, kMainMenu, 2);
        return 0;
    }
    PartyLoad(ThePartyManager(), nullptr, &scenario);
    int version = static_cast<int>(scenario.version_);
    if (version >= kScenarioScriptsVersion) {
        ScriptsReset(kScripts);
        TheInterface()->LoadState(&scenario);
        StreamLoadScripts(&scenario, nullptr, kScripts);
        version = static_cast<int>(scenario.version_);
        LoadedScriptsVersion() = version;
    }
    if (version >= kScenarioGlobalsVersion) TheCamera()->LoadState(&scenario);
    file.Close();

    // The visited zones of the current map (and bz7g) come back into saves\current; every other
    // zone, and one whose file could not be copied, is marked not visited.
    LocalRecords records;
    ThePartyManager()->EndRedeploy(&records.party);
    mfc::CString currentMap;    // [0x1C]
    currentMap = RecordAllod(FindZone(records.party.zone));
    NamedFloatTable* variables = &records.party.variables;
    for (int i = 0; i < ZoneRecordCount(); ++i) {
        MapRecord* record = ZoneRecords() + i;
        bool visited;
        {
            mfc::CString prefix(kVisitedPrefix);
            mfc::CString variable = prefix + record->name;
            double value = VariableGet(variables, variable);
            EI_NAN_AUDIT(value);
            visited = value != 0.0f;
        }
        bool restored = false;
        if (visited) {
            bool keep = gamecrt::mbscmp(record->name,
                                        kAlwaysCopied) == 0 ||
                        gamecrt::mbscmp(record->allod,
                                        static_cast<const char*>(currentMap)) == 0;
            if (keep) {
                zone = RecordName(record);
                mfc::CString currentPath = currentFolder + zone;
                mfc::CString currentFile = currentPath + kSaveExtension;
                mfc::CString slotPath = slotFolder + zone;
                mfc::CString slotFile = slotPath + kSaveExtension;
                restored = gameapi::CopyFileA(slotFile, currentFile, FALSE) != 0;
                if (restored) continue;
            }
        }
        mfc::CString prefix(kVisitedPrefix);
        mfc::CString variable = prefix + record->name;
        VariableSet(variables, variable, 0.0f);
    }
    ThePartyManager()->BeginRedeploy(&records.party);

    // The zone: kinds 0 and 2 come back from mission.sav, the others start afresh.
    MapRecord* zoneRecord = FindZone(records.party.zone);
    int kind = zoneRecord->kind;
    if (kind != 0 && kind != 2) {
        StartZoneAt(this, nullptr, records.party.zone, 0);
    } else {
        {
            mfc::CString path = slotFolder + kMissionFile;
            opened = file.Open(path, kOpenRead, 1);
        }
        if (!opened) return 0;
        LoadMission(&file.stream);
        file.Close();
        TheServerWorld()->SetZoneExits(reinterpret_cast<const ZoneExitArray*>(&zoneRecord->exits));
    }
    kConsole->PutText(kLoadedText)->PutText(slot)->PutChar('\n')->Apply(libcrt::FlushManipulator);
    return 1;
}

// ---- The flow's object, the network zone, the current save's size (A-W13) -----------------------

GameFlow* GameFlow::Construct() {
    new (&requestTarget_) mfc::CString();
    RequestPartyObject()->Construct();
    new (&zone7C_) mfc::CString();
    state_ = 0;
    return this;
}
EI_REPLACE_METHOD(0x00578460, GameFlow::Construct);

void GameFlow::Destruct() {
    zone7C_.~CString();
    RequestPartyObject()->DestructBody();
    requestTarget_.~CString();
}
EI_REPLACE_METHOD(0x005784D0, GameFlow::Destruct);

void GameFlow::SetNetworkZone(const char* zone) {
    mfc::CString folder;
    folder.Format(kCurrentFolder, DataFolderText());
    ClearSaveFolder(folder);
    zone7C_ = zone;
    if (Active(net::GameServer()) && !Active(net::GameClient()) && zone) StartNetworkZoneAt(zone);
}
EI_REPLACE_METHOD(0x0057ED60, GameFlow::SetNetworkZone);

int CurrentSaveSize() {
    mfc::CFileFind find;
    mfc::CString pattern;
    pattern.Format(kCurrentFiles, DataFolderText());
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
EI_REPLACE(0x0057BC30, CurrentSaveSize);

// ---- After starting the sessions (0057EB10) -----------------------------------------------------

namespace {

} // namespace

void GameFlow::AfterServerStart() {
    UseResources(true);
    // The variables of the party outside a game (007C82B0 + 0x54) start empty. Rebirth's
    // PersistentGlobalVars (the CALL at 0057EB25 redirected) keeps the "gvar." ones.
    NamedFloatTable* variables = &GlobalParty()->variables;
    if (ei::rebirth::Enabled(ei::rebirth::Patch::PersistentGlobalVars))
        ClearVariablesKeepingGlobals(variables);
    else
        variables->Clear();
    PartyResetItems(GlobalParty());
    // The client's player record gets the hero's and the party's names (or the defaults).
    if (GlobalParty()->count == 1) {
        const mfc::CString* names = GlobalParty()->records->texts;
        LocalLogin()->text_.Assign(names[5]);
        LocalLogin()->name_.Assign(names[0]);
    } else {
        LocalLogin()->text_.Assign(kDefaultHeroName);
        LocalLogin()->name_.Assign(kDefaultPartyName);
    }
    LocalLogin()->state_ = 1;
}

// ---- The client's update from the server (005800C0) ---------------------------------------------

namespace {

// The game types' (ClientSession::gameType_, 0..3) variable set to 1 and briefing name.

unsigned& ServerStamp() { return ei::GameData<unsigned>(0x007AFED0); }   // the newest variable stamp seen
unsigned& AppliedStamp() { return ei::GameData<unsigned>(0x007AFED4); }  // the stamp applied up to

// The quest text files (007AFDD0, a vector of strings) and the briefing texts (007AF788).
// 00460BC0 with the value as the raw dword the original pushes (no FPU load on the way).
void VariableSetBits(NamedFloatTable* table, const char* name, std::uint32_t bits) { table->Set(name, std::bit_cast<float>(bits)); }
// The flow's methods, through their addresses.
// 00580840 (GameFlow::OnVariableChanged): the original passes the variable node as ECX and as the
// event (the method reads only the event).
void VariableChanged(const void* node) { reinterpret_cast<GameFlow*>(const_cast<void*>(node))->OnVariableChanged(node); }
// The vector of changed variables (STLport vector<node*>): its allocator tag constructor
// (00584720), insert overflow (00587700: position, value, count) and storage release (00584A90:
// ECX storage, EDX capacity); the stamp sort (00589160 introsort with ECX first, EDX last, then a
// dummy, the depth limit and the comparator; 00589240 the final insertion sort; world_stamp_sort.cpp).
using NodeVector = stlp::vector<void*>;

// A local NamedFloatTable (00581790 an empty one of 100 buckets, 00580900 a copy), released the way
// the original's inlined destructor does.
struct LocalVariables {
    NamedFloatTable table;
    ~LocalVariables() { table.Destroy(); }
};

// The client's STLport lists of counted messages (ClientSession::zones_, variables_).
using VariableNode = net::RefNode;
using ZoneNode = net::RefNode;
// The server's variables (name_, value_ and the stamp serial_).
VariableNode* VariableList() { return net::GameClient()->variables_.End(); }
net::VariableMessage* NodeRecord(VariableNode* node) { return static_cast<net::VariableMessage*>(node->data.ptr); }
unsigned NodeStamp(VariableNode* node) { return NodeRecord(node)->serial_; }
// The value's bits, moved as the dword the original pushes (no FPU load).
std::uint32_t ValueBits(const net::VariableMessage* record) {
    std::uint32_t bits;
    std::memcpy(&bits, &record->value_, 4);
    return bits;
}

} // namespace

void GameFlow::UpdateFromServer() {
    if (!Active(net::GameClient())) return;
    if (void* texts = TheTextManager()) {
        for (const stlp::string* file = g_questFiles.begin(); file != g_questFiles.end(); ++file)
            static_cast<CTextManager*>(texts)->RemoveSource(file->data());
        stlp::VectorAbi<stlp::string, stlp::StringElement>::Clear(&g_questFiles);   // 00581860
    }
    auto* clientWorld = TheClientWorld();
    WorldPlayer* player = ClientWorldLocalView(clientWorld)->player;
    LocalVariables unusedEmpty;
    unusedEmpty.table.ConstructDefault();
    NamedFloatTable* variables = &player->variables;
    // The local player's variables are replaced by the server's; "constr.current" survives.
    // Rebirth's PersistentGlobalVars (the CALL at 00580177 redirected) keeps the "gvar." ones.
    float construction = static_cast<float>(VariableGet(variables, kConstructionVariable));
    LocalVariables unusedCopy;
    unusedCopy.table.CopyConstruct(*variables);
    if (ei::rebirth::Enabled(ei::rebirth::Patch::PersistentGlobalVars))
        ClearVariablesKeepingGlobals(variables);
    else
        variables->Clear();
    VariableSet(variables, kConstructionVariable, construction);
    if (auto* map = static_cast<net::MapMessage*>(net::GameClient()->map_)) {
        if (Active(map)) FlowLoadQuestsOriginal(map->text_.data());
    }

    // The game type's construction variable and briefings, at least 1.
    mfc::CString briefing;
    unsigned type = net::GameClient()->gameType_;
    if (type <= 3) {
        VariableSet(variables, kTypeVariables[type], 1.0f);
        briefing = kTypeBriefings[type];
    }
    mfc::CString questVariable(mfc::CString::kUnset);
    mfc::CString kVariable(mfc::CString::kUnset);
    BriefingVariableInto(&questVariable, briefing + kBriefingSuffixQuest);
    BriefingVariableInto(&kVariable, briefing + kBriefingSuffixK);
    mfc::CString* const names[2] = {&questVariable, &kVariable};
    for (mfc::CString* name : names) {
        float value = static_cast<float>(VariableGet(variables, *name));
        // FCOMP 1.0 with the value, C0 selecting it: the larger (a NaN kept).
        std::uint32_t bits;
        std::memcpy(&bits, &value, 4);
        bool keep = (bits & 0x7F800000u) == 0x7F800000u && (bits & 0x007FFFFFu) != 0 ? true : 1.0f < value;
        VariableSet(variables, *name, keep ? value : 1.0f);
    }

    // The briefings the server says were seen.
    for (ZoneNode* node = net::GameClient()->zones_.First(); node != net::GameClient()->zones_.End(); node = node->next)
        MarkBriefing(static_cast<net::ZoneMessage*>(node->data.ptr)->text_.data());
    // The quest variables ("q.<folder>.<name>") register their quest folders.
    for (VariableNode* node = VariableList()->next; node != VariableList(); node = node->next) {
        mfc::CString name(NodeRecord(node)->name_.data());
        bool quest;
        {
            mfc::CString prefix = name.Left(2);
            quest = gamecrt::mbscmp(static_cast<const char*>(prefix),
                                    kQuestVariablePrefix) == 0;
        }
        if (!quest) continue;
        mfc::CString rest = name.Right(console::Length(name) - 2);
        int dot = rest.Find(kDot);
        if (dot > 0) {
            mfc::CString folder = rest.Left(dot);
            RegisterQuests(folder);
        }
    }

    // The server's variables, by stamp.
    if (ClientWorldLocalView(clientWorld)->player->sessionKind == 1) {
        unsigned previous = AppliedStamp();
        if (ServerStamp() > AppliedStamp() + 2) AppliedStamp() = ServerStamp() + 1;
        NodeVector changed;
        changed.ConstructEmpty();   // 00584720
        for (VariableNode* node = VariableList()->next; node != VariableList(); node = node->next) {
            if (ServerStamp() <= NodeStamp(node)) ServerStamp() = NodeStamp(node);
            unsigned stamp = NodeStamp(node);
            if (stamp >= AppliedStamp() || stamp < previous) continue;
            if (changed.Finish() != changed.EndOfStorage()) {
                if (changed.Finish()) *changed.Finish() = node;
                ++changed.Finish();
            } else {
                void* value = node;
                changed.InsertOverflow<stlp::PlainElement<void*>>(changed.Finish(), value, 1);
            }
        }
        if (changed.begin() != changed.end()) {
            int depth = 0;
            for (int n = static_cast<int>(changed.size()); n != 1; n >>= 1) ++depth;
            StampIntroSort(changed.begin(), changed.end(), 0, depth * 2, variables);
            StampInsertionSort(changed.begin(), changed.end(), variables);
        }
        for (void** it = changed.begin(); it != changed.end(); ++it) VariableChanged(*it);
        bool settled;
        do {
            settled = true;
            for (VariableNode* node = VariableList()->next; node != VariableList(); node = node->next) {
                if (NodeStamp(node) > AppliedStamp()) continue;
                if (NodeStamp(node) == AppliedStamp()) {
                    VariableChanged(node);
                    ++AppliedStamp();
                    settled = false;
                }
                net::VariableMessage* record = NodeRecord(node);
                VariableSetBits(variables, record->name_.data(), ValueBits(record));
            }
        } while (!settled);
        stlp::deallocate(changed.begin(), static_cast<std::size_t>(changed.EndOfStorage() - changed.begin()) * 4);   // 00584A90
    } else {
        for (VariableNode* node = VariableList()->next; node != VariableList(); node = node->next) {
            if (ServerStamp() <= NodeStamp(node)) ServerStamp() = NodeStamp(node);
            net::VariableMessage* record = NodeRecord(node);
            mfc::CString name(record->name_.data());
            VariableSetBits(variables, record->name_.data(), ValueBits(record));
        }
        AppliedStamp() = 0;
    }
    UpdateMultiplayerVariable();
}

// ---- The saved multiplayer parties (00662FB0) ---------------------------------------------------

// An entry of SavedParties' vector: the party records and the name of their file.
struct SavedPartyEntry {
    PartyRecords records;           // +0x00
    mfc::CString fileName;          // +0x70
};
static_assert(offsetof(SavedPartyEntry, fileName) == 0x70);
static_assert(sizeof(SavedPartyEntry) == 0x74);

namespace {

// An entry is a multiplayer hero record (ui_screens.h): the vector's element copy loop is
// MpHeroRecord::CopyRange (00664410), its element destructor DeletingDestruct (00664AE0, flag 0);
// the records' copy constructor (006613C0) and the insert overflow (00619920) are in
// originals_gameplay.h.
MpHeroRecord* AsHero(SavedPartyEntry* entry) { return reinterpret_cast<MpHeroRecord*>(entry); }
// The records' destructor pieces, as 00662FB0 inlines it: 00661700 (Reset(1, 1)), 00661640, the
// zone name, the variables (00663330), the array at +0x40 (006643F0), 00664160 (+0x2C), the base
// (00455930).

void DestroyPartyEntry(SavedPartyEntry* entry) {
    PartyRecords* records = &entry->records;
    entry->fileName.~CString();
    ei::SetVtable(records, kRecordsVtable);
    PartyResetOriginal(records, nullptr, 1, 1);
    AsParty(records)->DestroyParty();   // 00661640
    records->zone.~CString();
    records->variables.Destroy();   // 00663330
    reinterpret_cast<lib::AlignedArray<std::uint8_t>*>(records->array40)->FreeData();   // 006643F0
    DestructPartyMemberArray(reinterpret_cast<PartyMemberArray*>(&records->recordArrayVtable));   // 00664160
    reinterpret_cast<CEngineObject*>(records)->DestructBase();
}

} // namespace

void SavedParties::Reload() {
    // Empty the list.
    SavedPartyEntry*& finish = finish_;
    SavedPartyEntry* end = reinterpret_cast<SavedPartyEntry*>(MpHeroRecord::CopyRange(AsHero(finish), AsHero(finish), AsHero(entries_)));
    for (SavedPartyEntry* entry = end; entry != finish; ++entry) AsHero(entry)->DeletingDestruct(0);
    finish = end;

    WIN32_FIND_DATAA found;
    HANDLE search;
    {
        mfc::CString pattern = folder_ + kPartyFilePattern;
        search = gameapi::FindFirstFileA(pattern, &found);
    }
    if (search == INVALID_HANDLE_VALUE) return;
    do {
        // The party records and the file name, side by side as the list's entries are.
        alignas(SavedPartyEntry) std::uint8_t storage[sizeof(SavedPartyEntry)];
        auto* entry = reinterpret_cast<SavedPartyEntry*>(storage);
        reinterpret_cast<CParty*>(&entry->records)->Construct();
        new (&entry->fileName) mfc::CString();
        entry->fileName = found.cFileName;
        if (reinterpret_cast<MpHeroRecord*>(&entry->records)->ReadFile(folder_)) {   // 00662DD0
            NamedFloatTable* variables = &entry->records.variables;
            if (ei::rebirth::Enabled(ei::rebirth::Patch::PersistentGlobalVars)) {
                // Rebirth's PersistentGlobalVars: the JNE at 00663083 skips the inlined clear and
                // the hook at 00663170 (whose instruction, count = 0, is dropped) clears the table
                // keeping its "gvar." entries.
                ClearVariablesKeepingGlobals(variables);
            } else {
                variables->Clear();   // inlined: each chain's nodes freed (004616A0), the count zeroed
            }
            if (entry->records.count == 1) {
                SavedPartyEntry* eos = eos_;
                if (finish != eos) {
                    if (finish) {
                        AsParty(&finish->records)->CopyConstruct(AsParty(&entry->records));
                        new (&finish->fileName) mfc::CString(entry->fileName);
                    }
                    ++finish;
                } else {
                    libcontainers::HeroRecordAbi::InsertOverflow(   // 00619920
                        reinterpret_cast<libcontainers::HeroRecords*>(&entries_), nullptr, reinterpret_cast<MpHeroRecord*>(finish),
                        reinterpret_cast<const MpHeroRecord*>(entry), 1);
                }
            }
        }
        DestroyPartyEntry(entry);
    } while (gameapi::FindNextFileA(search, &found));
    gameapi::FindClose(search);
}

// ---- BuildZoneTable (0057F560, A-W14) -----------------------------------------------------------

namespace {

constexpr const char* kQuestFilePattern = "*.mq";
// The map names of the three allods, each list ended by an empty name.
constexpr std::uintptr_t kGipatMaps = ei::DataId(0x00790270);       // zone2 .. zone9
constexpr std::uintptr_t kIngosMaps = ei::DataId(0x00790298);       // zone12, zone13
constexpr std::uintptr_t kSuslangerMaps = ei::DataId(0x007902C0);   // zone17, zone18
const char* const* NameList(std::uintptr_t address) { return ei::GamePointer<const char* const>(address); }

// 00587480. The zone table's _M_insert_overflow(pos, value, count) (recordvec::ZoneTableVector's).
void ZoneTableInsertOverflow(ZoneTable* table, ZoneEntry* pos, const ZoneEntry* value, unsigned count) {
    recordvec::ZoneTableVector::InsertOverflow(table, nullptr, pos, value, count);
}

// The allod of a map name: the first list that has it.
int AllodOfMap(const char* map) {
    if (NameInList(NameList(kGipatMaps), map)) return 0;
    if (NameInList(NameList(kIngosMaps), map)) return 1;
    return NameInList(NameList(kSuslangerMaps), map) ? 2 : 3;
}

// push_back of an entry with two empty names (its id is not set, as in the original).
void AppendEmptyEntry(ZoneTable& table) {
    if (table.Finish() != table.EndOfStorage()) {
        if (table.Finish()) {
            new (&table.Finish()->first) mfc::CString();
            new (&table.Finish()->second) mfc::CString();
        }
        ++table.Finish();
        return;
    }
    ZoneEntry empty{};   // destroyed from the last name
    ZoneTableInsertOverflow(&table, table.Finish(), &empty, 1);
}

} // namespace

void GameFlow::BuildZoneTable() {
    ZoneTable& table = g_zoneTable;
    if (table.end() != table.begin()) return;
    mfc::CFileFind find;
    int more;
    {
        mfc::CString folder;
        modfolders::SelectMaps(kQuestFilePattern, modfolders::MapsSite::ZoneTable);
        MapsFolderAt(&folder);
        mfc::CString pattern = folder + kQuestFilePattern;
        more = find.FindFile(pattern, 0);
    }
    while (more) {
        more = find.FindNext();
        mfc::CString name = find.GetTitle();
        name.MakeLower();
        RegisterQuests(name);
        UnloadQuestTexts();
        mfc::CString map;
        if (MapRecord* record = FindZone(name)) {
            map = RecordMap(record);
            const int allod = AllodOfMap(map);
            AppendEmptyEntry(table);
            ZoneEntry& entry = table.Finish()[-1];
            entry.first = name;
            entry.second = map;
            entry.id = allod;
        }
    }
    find.Close();
}
EI_REPLACE_METHOD(0x0057F560, GameFlow::BuildZoneTable);
EI_REPLACE(0x0057F4A0, DestroyZoneTable);

EI_REPLACE_METHOD(0x0057B3B0, GameFlow::SaveGame);
EI_REPLACE_METHOD(0x0057F0F0, GameFlow::ReturnLocalParty);
EI_REPLACE_METHOD(0x0057F150, GameFlow::UpdateAndAcknowledge);
EI_REPLACE_METHOD(0x0057FB60, GameFlow::RequestBriefing);
EI_REPLACE(0x0057F820, NameInList);
EI_REPLACE(0x0057AAB0, ClearSaveFolder);
EI_REPLACE_METHOD(0x0057EE10, GameFlow::SwitchMultiplayerZone);
EI_REPLACE_METHOD(0x0057E740, GameFlow::StartServer);
EI_REPLACE_METHOD(0x0058DAF0, CWorldServer::AcceptNewPlayers);
EI_REPLACE(0x0057F850, PickZones);
EI_REPLACE(0x00579E60, LoadMission);
EI_REPLACE_METHOD(0x00596140, CWorldServer::ReadAIGraph);
EI_REPLACE_METHOD(0x00596350, CWorldServer::LoadZoneScripts);
EI_REPLACE_METHOD(0x0057F390, GameFlow::RecreateClientSession);
EI_REPLACE_METHOD(0x00581610, GameFlow::SaveGameThunk);
EI_REPLACE(0x00581630, RequestZone);
EI_REPLACE_METHOD(0x00581660, GameFlow::CloseSessions);
EI_REPLACE_METHOD(0x0057ECA0, GameFlow::DropSessions);
EI_REPLACE_METHOD(0x0057ED00, GameFlow::EndSession);
EI_REPLACE_METHOD(0x005816C0, GameFlow::RequestState3);
EI_REPLACE_METHOD(0x005816D0, GameFlow::RequestLoad);
EI_REPLACE_METHOD(0x00581700, GameFlow::RequestZoneChange);
EI_REPLACE_METHOD(0x00581730, GameFlow::RequestPartyZone5);
EI_REPLACE_METHOD(0x00581760, GameFlow::RequestPartyZone6);
EI_REPLACE_METHOD(0x00581620, GameFlow::DisconnectClient);
EI_REPLACE_METHOD(0x0057F190, GameFlow::UpdateMultiplayerVariable);
EI_REPLACE_METHOD(0x0057E6B0, GameFlow::MultiplayerZoneInto);
EI_REPLACE_METHOD(0x0057F280, GameFlow::BeginMultiplayerGame);
EI_REPLACE_METHOD(0x0057AA80, CWorldServer::ZoneNameInto);
EI_REPLACE(0x0057A780, LeaveZone);
EI_REPLACE(0x0057A060, EnterZone);
EI_REPLACE(0x0057BDF0, StartNetworkZone);
EI_REPLACE_METHOD(0x0057C460, GameFlow::StartZone);
EI_REPLACE_METHOD(0x0057BD10, GameFlow::StartNewGame);
EI_REPLACE_METHOD(0x0057C700, GameFlow::ProcessRequest);
EI_REPLACE_METHOD(0x0057ABA0, GameFlow::LoadGame);
EI_REPLACE_METHOD(0x0057EB10, GameFlow::AfterServerStart);
EI_REPLACE_METHOD(0x005800C0, GameFlow::UpdateFromServer);
EI_REPLACE_METHOD(0x00662FB0, SavedParties::Reload);
