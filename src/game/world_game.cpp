// The game flow object (007AFE20; the state machine 0057C700 runs on it, Rebirth patches that one):
// saving a game into saves\<slot>, entering a zone. See docs/notes/world.md (seventh round).
#include "world.h"
#include "game_strings.h"

#include "console.h"

#include "core_maths.h"
#include "core_streams.h"
#include "net_link.h"
#include "object.h"
#include "object_tables.h"

#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "lib/stlp.h"

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

#include <cstring>
#include <new>
#include <bit>

namespace {

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(object) + offset);
}

const char* Text(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }

// ---- Data -------------------------------------------------------------------------------------

constexpr std::uintptr_t kSlotFolder = 0x007905E4;      // "%ssaves\\%s\\"
constexpr std::uintptr_t kCurrentFolder = 0x00790534;   // "%ssaves\\current\\"
constexpr std::uintptr_t kVisitedPrefix = 0x00790548;   // "zs.": the "zone visited" variable
constexpr std::uintptr_t kAlwaysCopied = 0x007905CC;    // "bz7g"
constexpr std::uintptr_t kSaveExtension = 0x0079052C;   // ".sav"
constexpr std::uintptr_t kScenarioFile = 0x007905D4;    // "scenario.sav"
constexpr std::uintptr_t kMissionFile = 0x007905C0;     // "mission.sav"
constexpr std::uintptr_t kMainMenu = 0x007904EC;        // "MainMenu"
constexpr std::uintptr_t kSaved = 0x00790604;           // "Saved "

constexpr unsigned kOpenWrite = netlink::FileStream::kOpenWrite;
constexpr unsigned kOpenRead = netlink::FileStream::kOpenRead;
constexpr std::uint32_t kStreamLimit = 2000000000;
constexpr unsigned kMagicScenario = 0xDEAD;
constexpr unsigned kMagicMission = 0x102;

int GameMode() { return *reinterpret_cast<const int*>(0x007B0098); }        // 1: single player
int Campaign() { return *reinterpret_cast<const int*>(0x007C2CC4); }        // 4: the party is player 0's
const char* GameFolder() { return *reinterpret_cast<char* const*>(0x007AF460); }
int SaveVersion() { return *reinterpret_cast<const int*>(0x00790260); }
std::uint8_t& ToMainMenu() { return *reinterpret_cast<std::uint8_t*>(0x007C7D94); }

void* const kParty = reinterpret_cast<void*>(0x007AA5A0);
void* const kMapDatabase = reinterpret_cast<void*>(0x0079BF00);
void* const kRenderer = reinterpret_cast<void*>(0x007C7BD8);
void* const kInput = reinterpret_cast<void*>(0x007C7D50);
void* const kScripts = reinterpret_cast<void*>(0x007AFDF8);
void* const kGlobals = reinterpret_cast<void*>(0x0079B2A8);
void* const kConsole = reinterpret_cast<void*>(0x007C55A8);
void* const kFlush = reinterpret_cast<void*>(0x00435000);

// The map database's zone records (0x5C bytes: zone name, map name, ...).
constexpr int kZoneRecordSize = 0x5C;
std::uint8_t* ZoneRecords() { return *reinterpret_cast<std::uint8_t**>(0x0079BF18); }
int ZoneRecordCount() { return *reinterpret_cast<const int*>(0x0079BF1C); }

// A CStream over a file (net_link.h).
using netlink::FileStream;

// ---- Callees not rewritten (EI_ORIGINAL forwarders; __thiscall called as __fastcall) ----------

// 005E5200 (RET 4). The renderer's busy state (3 while saving, 0 after).
void __fastcall RendererSetBusy(void* a0, void*, int a2) { CursorSetShapeOriginal(a0, nullptr, a2); }
// 0057AAB0. Creates the folder `path` (ECX) and deletes the files in it.
__declspec(naked) void __fastcall ClearSaveFolderAt(const char* /*path*/) EI_ORIGINAL(0x0057AAB0)
// 00594180 (RET 4). CWorldServer::GetPlayer (world_server.cpp), through its address.
// 004A8890 / 004A8880 (RET 4). The party (007AA5A0): take the records from / give them to a record
// builder (00661050 constructs one, 006611D0 destroys it).
__declspec(naked) void __fastcall PartyTake(void*, void*, void* /*records*/) EI_ORIGINAL(0x004A8890)
__declspec(naked) void __fastcall PartyStore(void*, void*, void* /*records*/) EI_ORIGINAL(0x004A8880)
void* __fastcall RecordsConstruct(void* a0, void*) { return PartyRecordsConstructOriginal(a0); }
void __fastcall RecordsDestruct(void* a0, void*) { PartyRecordsDestructOriginal(a0); }
// 0045E850 (RET 4). CMapDatabase::FindMap: the zone record named `name`, or null.
// 00460AF0 / 00460BC0. Named float table Get (ST0) / Set (RET 8).
double __fastcall VariableGet(void* a0, void*, const char* a2) { return (double)NamedTableGet(a0, nullptr, a2); }
void __fastcall VariableSet(void* a0, void*, const char* a2, float a3) { NamedTableSet(a0, nullptr, a2, std::bit_cast<std::uint32_t>(a3)); }
// 0057C460 (RET 8). Starts zone `zone` (mode 2: the main menu). Patched by Rebirth: original address.
__declspec(naked) void __fastcall StartZoneAt(GameFlow*, void*, const char*, int) EI_ORIGINAL(0x0057C460)
// The scenario.sav parts: the party (004A8560), the input state (005F85C0 save, 005F86D0), the
// scripts (004A54D0 reset, 004A5250 save into a stream), the globals (00410C10).
__declspec(naked) void __fastcall PartySave(void*, void*, CMemoryStream*) EI_ORIGINAL(0x004A8560)
__declspec(naked) void __fastcall InputSave(void*, void*, CMemoryStream*) EI_ORIGINAL(0x005F85C0)
__declspec(naked) void __fastcall InputAfterSave(void*, void*) EI_ORIGINAL(0x005F86D0)
void ScriptsReset(void* scripts) { reinterpret_cast<CMemoryStream*>(scripts)->Resize(0); }
__declspec(naked) void __fastcall StreamSaveScripts(CMemoryStream*, void*, void* /*scripts*/) EI_ORIGINAL(0x004A5250)
__declspec(naked) void __fastcall GlobalsSave(void*, void*, CMemoryStream*) EI_ORIGINAL(0x00410C10)
// 00579B80 (RET 8). Writes `data` to the file stream (ECX) under a header (magic in EDX, version).
__declspec(naked) void __fastcall WriteSaveFile(FileStream*, unsigned /*magic*/, int /*version*/, CMemoryStream*)
    EI_ORIGINAL(0x00579B80)
// 00597BD0. CWorldServer::SaveWorld (world_save.cpp), through its address.
__declspec(naked) void __fastcall SaveWorldAt(void*, void*, CMemoryStream*) EI_ORIGINAL(0x00597BD0)
// The console's ostream.

// The record builder of the party (0x70 bytes; the variables table at +0x54, the zone at +0x68).
constexpr std::size_t kRecordsSize = 0x70;
constexpr int kRecordsVariables = 0x54;
constexpr int kRecordsZone = 0x68;

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

struct LocalRecords {
    alignas(4) std::uint8_t bytes[kRecordsSize];
    LocalRecords() { RecordsConstruct(bytes, nullptr); }
    ~LocalRecords() { RecordsDestruct(bytes, nullptr); }
};

// Copies saves\current\<zone>.sav of every visited zone that has to be kept (the current map's
// zones and bz7g) into the slot folder; a zone whose file could not be copied, and every other
// zone, is marked not visited ("zs.<zone>" = 0).
void CopyVisitedZones(const mfc::CString& slotFolder, const mfc::CString& currentFolder, mfc::CString& zone,
                      const mfc::CString& currentMap, void* variables) {
    for (int i = 0; i < ZoneRecordCount(); ++i) {
        std::uint8_t* record = ZoneRecords() + i * kZoneRecordSize;
        const char* name = At<const char*>(record, 0);
        bool visited;
        {
            mfc::CString prefix(Text(kVisitedPrefix));
            mfc::CString variable = prefix + name;
            // The original tests C3 alone (equal or unordered): a NaN counts as not visited.
            double value = VariableGet(variables, nullptr, variable);
            visited = value < 0.0f || value > 0.0f;
        }
        bool keep = false;
        if (visited) {
            keep = gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(At<const char*>(record, 0)),
                                   reinterpret_cast<const unsigned char*>(Text(kAlwaysCopied))) == 0 ||
                   gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(At<const char*>(record, 4)),
                                   reinterpret_cast<const unsigned char*>(static_cast<const char*>(currentMap))) == 0;
        }
        if (keep) {
            zone = At<mfc::CString>(record, 0);
            bool failed;
            {
                mfc::CString slotPath = slotFolder + zone;
                mfc::CString slotFile = slotPath + Text(kSaveExtension);
                mfc::CString currentPath = currentFolder + zone;
                mfc::CString currentFile = currentPath + Text(kSaveExtension);
                failed = gameapi::CopyFileA(currentFile, slotFile, FALSE) == 0;
            }
            if (!failed) continue;
        }
        mfc::CString prefix(Text(kVisitedPrefix));
        mfc::CString variable = prefix + At<const char*>(record, 0);
        VariableSet(variables, nullptr, variable, 0.0f);
    }
}

} // namespace

int GameFlow::SaveGame(const char* slot) {
    LocalFileStream file;
    mfc::CString zone;          // [0x14]
    mfc::CString slotFolder;    // [0x10]
    mfc::CString currentFolder; // [0x18]
    if (GameMode() != 1) return 0;

    RendererSetBusy(kRenderer, nullptr, 3);
    slotFolder.Format(Text(kSlotFolder), GameFolder(), slot);
    currentFolder.Format(Text(kCurrentFolder), GameFolder());
    ClearSaveFolderAt(slotFolder);
    if (Campaign() == 4) {
        if (WorldPlayer* player = static_cast<WorldPlayer*>(WorldGetPlayer(reinterpret_cast<void*>(0x007AFF60), nullptr, 0)))
            PartyTake(kParty, nullptr, reinterpret_cast<std::uint8_t*>(player) + 0x70);
    }
    LocalRecords records;
    PartyStore(kParty, nullptr, records.bytes);
    mfc::CString currentMap;    // [0x1C]
    currentMap = At<mfc::CString>(MapDatabaseFind(kMapDatabase, nullptr, At<const char*>(records.bytes, kRecordsZone)), 4);

    CopyVisitedZones(slotFolder, currentFolder, zone, currentMap, records.bytes + kRecordsVariables);
    PartyTake(kParty, nullptr, records.bytes);

    bool opened;
    {
        mfc::CString path = slotFolder + Text(kScenarioFile);
        opened = file.Open(path, kOpenWrite, 0);
    }
    if (!opened) {
        ToMainMenu() = 1;
        StartZoneAt(this, nullptr, Text(kMainMenu), 2);
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
    PartySave(kParty, nullptr, &scenario);
    InputSave(kInput, nullptr, &scenario);
    ScriptsReset(kScripts);
    InputAfterSave(kInput, nullptr);
    StreamSaveScripts(&scenario, nullptr, kScripts);
    ScriptsReset(kScripts);
    GlobalsSave(kGlobals, nullptr, &scenario);
    WriteSaveFile(&file.stream, kMagicScenario, SaveVersion(), &scenario);
    file.Close();

    // mission.sav: the world, except in zones without one (kind 1: cutscenes).
    int kind = At<int>(MapDatabaseFind(kMapDatabase, nullptr, At<const char*>(records.bytes, kRecordsZone)), 0x28);
    if (kind == 0 || kind == 2) {
        {
            mfc::CString path = slotFolder + Text(kMissionFile);
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
        SaveWorldAt(reinterpret_cast<void*>(0x007AFF60), nullptr, &mission);
        WriteSaveFile(&file.stream, kMagicMission, SaveVersion(), &mission);
        ei::SetVtable(&mission, kMemoryStreamVtable);
        if (mission.data_) gamecrt::operator_delete(mission.data_);
        file.Close();
    }

    RendererSetBusy(kRenderer, nullptr, 0);
    void* console = OstreamPutString(kConsole, nullptr, Text(kSaved));
    console = OstreamPutString(console, nullptr, slot);
    OstreamManipulate(OstreamPutChar(console, nullptr, '\n'), nullptr, kFlush);
    return 1;
}

// ---- EnterZone --------------------------------------------------------------------------------

namespace {

constexpr std::uintptr_t kMovieVariable = 0x00790598;   // "movie"
constexpr std::uintptr_t kMovieZone = 0x00790590;       // "bz3g": no movie there
constexpr std::uintptr_t kMovieFile = 0x0079057C;       // "Movies\\dcity.bik"
constexpr std::uintptr_t kFrozenZone = 0x00790574;      // "gz6g"
constexpr std::uintptr_t kFrozenVariable = 0x00790564;  // "z.gz6g.frozen"
constexpr std::uintptr_t kFrozenMap = 0x00790558;       // "zone6_2.mpr"
constexpr std::uintptr_t kThawedMap = 0x0079054C;       // "zone6.mpr"
constexpr std::uintptr_t kCorruptSave = 0x00790514;     // "Save file %s is corrupt"
constexpr std::uintptr_t kLeftPrefix = 0x00790510;      // "zt.": the time the zone was left
constexpr std::uintptr_t kGTime = 0x0073F798;           // "gtime"
constexpr std::uintptr_t kMapExtension = 0x007848EC;    // ".mpr"
constexpr std::uintptr_t kMobExtension = 0x007848F4;    // ".mob"
constexpr std::uintptr_t kTicksPerTime = 0x0073BAB0;    // world ticks per unit of "gtime"
constexpr std::uintptr_t kHalf = 0x0073B86C;            // 0.5f
constexpr std::uint32_t kCatchUpRadius = 0x428C0000;    // 70.0f
constexpr std::uint32_t kStartTime = 0x40800000;        // 4.0f
constexpr int kMaxCatchUpTicks = 1000;

float GameFloat(std::uintptr_t address) { return *reinterpret_cast<const volatile float*>(address); }

// The game's _ftol (006E853C): truncation, on the value as the FPU holds it.
__declspec(naked) int __cdecl GameFtol(double) {
    __asm {
        fld qword ptr [esp + 4]
        mov eax, 0x006E853C
        call eax
        ret
    }
}
CWorldServer* World() { return reinterpret_cast<CWorldServer*>(0x007AFF60); }
const mfc::CString& MoviesFolder() { return *reinterpret_cast<const mfc::CString*>(0x007AF458); }

// Zone record fields (CMapDatabase, 0x5C bytes).
constexpr int kZoneName = 0x00;
constexpr int kZoneMob = 0x08;          // CString: the .mob file without extension
constexpr int kZoneMap = 0x0C;          // CString: the .mpr map without extension ("" = none)
constexpr int kZoneKind = 0x28;         // 2: the zone has a script folder (00596350)
constexpr int kZoneExits = 0x48;        // ZoneExitArray (exits at +0x4C, count +0x50)

// 005DB3F0. Plays a movie file (ECX path, EDX 0, five flags).
// 00460DB0 (RET 4). Named float table Remove.
// 00579830 (RET 0xC). Reads a save file through the file stream into `out` (ECX), checking the
// header (magic in EDX, version, minimum version); false when it is corrupt.
__declspec(naked) bool __fastcall ReadSaveFile(CMemoryStream* /*out*/, unsigned /*magic*/, int /*version*/,
                                               int /*minimum*/, FileStream*) EI_ORIGINAL(0x00579830)
// 00596E70 / 0058D3D0 / 0058DCD0 / 005915E0. CWorldServer::LoadSave, WakeUnitsAround, Tick and
// SetZoneExits, through their addresses (rewritten in world_save / world_tick / world_zone).
__declspec(naked) int __fastcall LoadSaveAt(CWorldServer*, void*, CMemoryStream*, const char*) EI_ORIGINAL(0x00596E70)
__declspec(naked) void __fastcall WakeUnitsAroundAt(CWorldServer*, void*, const float*, std::uint32_t /*radius*/)
    EI_ORIGINAL(0x0058D3D0)
__declspec(naked) void __fastcall SetZoneExitsAt(CWorldServer*, void*, const void*) EI_ORIGINAL(0x005915E0)
// 00595480 (RET 0xC). CWorldServer::NewZone(map, mob, zone). Patched by Rebirth: original address.
__declspec(naked) int __fastcall NewZone(CWorldServer*, void*, const char*, const char*, const char*) EI_ORIGINAL(0x00595480)
// 0058EEA0 (RET 4). CWorldServer::SetTime.
void __fastcall WorldSetTime(CWorldServer* a0, void*, std::uint32_t a2) { WorldSetTimeOriginal((void*)a0, nullptr, a2); }
// 00596350 (RET 4). Loads the zone's scripts from a folder.
__declspec(naked) void __fastcall WorldLoadZoneScripts(CWorldServer*, void*, const char*) EI_ORIGINAL(0x00596350)
// 004BEB00. Resets the scripts (007C27D4) for a zone without its own.
__declspec(naked) void __fastcall ScriptsResetZone(void*, void*) EI_ORIGINAL(0x004BEB00)

// The zone was saved before: read saves\current\<zone>.sav and restore it, then let the world
// catch up with the time spent away (one tick per elapsed unit, at most 1000, waking the units
// around the entrance). Returns LoadSave's result, 0 when the file cannot be opened or is
// corrupt.
int RestoreZone(std::uint8_t* records, std::uint8_t* zoneRecord, const mfc::CString& zone, mfc::CString& folder,
                const char* mapOverride, LocalFileStream& file, bool& failed) {
    void* variables = records + kRecordsVariables;
    folder.Format(Text(kCurrentFolder), GameFolder());
    mfc::CString fileName = zone + Text(kSaveExtension);
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
    if (!ReadSaveFile(&data, kMagicMission, SaveVersion(), *reinterpret_cast<const int*>(0x00790268), &file.stream)) {
        game::FatalError(Text(kCorruptSave), static_cast<const char*>(fileName));
        failed = true;
        return 0;
    }
    int result = LoadSaveAt(World(), nullptr, &data, mapOverride);
    file.Close();

    int entrance = At<int>(records, 0x6C);
    if (entrance >= 0 && entrance < At<int>(zoneRecord, kZoneExits + 8)) {
        float now = StoreFloat(static_cast<float>(VariableGet(variables, nullptr, Text(kGTime))));
        float left;
        {
            mfc::CString prefix(Text(kLeftPrefix));
            mfc::CString variable = prefix + zone;
            left = StoreFloat(static_cast<float>(VariableGet(variables, nullptr, variable)));
        }
        int ticks = GameFtol((static_cast<double>(now) - left) * GameFloat(kTicksPerTime));
        if (ticks >= kMaxCatchUpTicks) ticks = kMaxCatchUpTicks;
        else if (ticks <= 0) return result;
        const std::uint8_t* exit = At<std::uint8_t*>(zoneRecord, kZoneExits + 4) + entrance * 0x3C;
        float point[3];
        point[0] = static_cast<float>((static_cast<double>(At<float>(const_cast<std::uint8_t*>(exit), 0xC)) +
                                       At<float>(const_cast<std::uint8_t*>(exit), 0x4)) * GameFloat(kHalf));
        point[1] = static_cast<float>((static_cast<double>(At<float>(const_cast<std::uint8_t*>(exit), 0x10)) +
                                       At<float>(const_cast<std::uint8_t*>(exit), 0x8)) * GameFloat(kHalf));
        point[2] = 0.0f;
        for (int i = ticks; i != 0; --i) {
            WakeUnitsAroundAt(World(), nullptr, point, kCatchUpRadius);
            TickAt(World(), nullptr, 1);
        }
    }
    return result;
}

} // namespace

// 0057A060 (__stdcall, RET 4). Enters the party's zone (records +0x68, entrance +0x6C): plays the
// pending "movie", restores the zone from saves\current when it was visited (zs.<zone>), otherwise
// loads it fresh (NewZone; gz6g picks its frozen or thawed map), then the zone's scripts and its
// exits. Returns 0 on failure, else the restore's result (1 for a fresh zone).
int __stdcall EnterZone(std::uint8_t* records) {
    LocalFileStream file;
    int result = 1;
    mfc::CString folder;                                                    // [0x14]
    mfc::CString zone(*reinterpret_cast<const mfc::CString*>(records + 0x68)); // [0x10]
    std::uint8_t* zoneRecord = MapDatabaseFind(kMapDatabase, nullptr, zone);
    if (!zoneRecord) return 0;

    void* variables = records + kRecordsVariables;
    if (GameFtol(VariableGet(variables, nullptr, Text(kMovieVariable))) == 1) {
        if (gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(static_cast<const char*>(zone)),
                            reinterpret_cast<const unsigned char*>(Text(kMovieZone))) != 0) {
            {
                mfc::CString movie = MoviesFolder() + Text(kMovieFile);
                PlayMovieFile(movie, 0, 1, 1, 1, 1, 1);
            }
            VariableRemove(variables, nullptr, Text(kMovieVariable));
        }
    } else {
        VariableRemove(variables, nullptr, Text(kMovieVariable));
    }

    if (gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(At<const char*>(zoneRecord, kZoneMap)),
                        reinterpret_cast<const unsigned char*>(game::EmptyText())) != 0) {
        const char* mapOverride = nullptr;
        if (gamecrt::mbsicmp(reinterpret_cast<const unsigned char*>(static_cast<const char*>(zone)),
                             reinterpret_cast<const unsigned char*>(Text(kFrozenZone))) == 0) {
            double frozen = VariableGet(variables, nullptr, Text(kFrozenVariable));
            mapOverride = Text(kFrozenMap);
            if (!(frozen < 0.0f || frozen > 0.0f)) mapOverride = Text(kThawedMap);   // C3: equal or unordered
        }
        bool visited;
        {
            mfc::CString prefix(Text(kVisitedPrefix));
            mfc::CString variable = prefix + zone;
            double value = VariableGet(variables, nullptr, variable);
            visited = value < 0.0f || value > 0.0f;
        }
        if (visited) {
            bool failed = false;
            result = RestoreZone(records, zoneRecord, zone, folder, mapOverride, file, failed);
            if (failed) return 0;
        } else {
            mfc::CString map = At<mfc::CString>(zoneRecord, kZoneMap) + Text(kMapExtension);
            if (mapOverride) map = mapOverride;
            bool loaded;
            {
                mfc::CString mob = At<mfc::CString>(zoneRecord, kZoneMob) + Text(kMobExtension);
                loaded = NewZone(World(), nullptr, map, mob, At<const char*>(zoneRecord, kZoneName)) != 0;
            }
            if (!loaded) return 0;
            zoneRecord = MapDatabaseFind(kMapDatabase, nullptr, zone);
            WorldSetTime(World(), nullptr, kStartTime);
        }
        if (At<int>(zoneRecord, kZoneKind) == 2) {
            mfc::CString scripts = At<mfc::CString>(zoneRecord, kZoneMob) + Text(kMobExtension);
            WorldLoadZoneScripts(World(), nullptr, scripts);
        } else {
            ScriptsResetZone(reinterpret_cast<void*>(0x007C27D4), nullptr);
        }
    }
    SetZoneExitsAt(World(), nullptr, zoneRecord + kZoneExits);
    return result;
}

// ---- StartNetworkZone -------------------------------------------------------------------------

namespace {

constexpr std::uintptr_t kNetworkExitTarget = 0x00790620;   // "MPGame1": where every exit leads
constexpr std::uintptr_t kExitArrayVtable = 0x0073C400;      // CArray<ZoneExit> (after 0056FFE0)
constexpr std::uintptr_t kExitArrayBaseVtable = 0x0073C3F4;
constexpr std::uintptr_t kHostRecordVtable = 0x0073BCA0;
constexpr int kZoneSetting2C = 0x2C;   // CWorldServer::SetUnknownDC
constexpr int kZoneSetting30 = 0x30;   // CWorldServer::SetUnknownE0
constexpr int kGameResultNetwork = 2;
constexpr int kHostPort = 8000;
constexpr int kServerMessageZone = 8;

int& CampaignMode() { return *reinterpret_cast<int*>(0x007C2CC4); }
int& SpectatorFlag() { return *reinterpret_cast<int*>(0x0079BE6C); }

// 005957E0. CWorldServer's zone teardown.
__declspec(naked) void __fastcall WorldTeardownZone(CWorldServer*, void*) EI_ORIGINAL(0x005957E0)
// 0056FFE0. CArray<ZoneExit> constructor (empty, base vtable 0073C3F4).
__declspec(naked) void __fastcall ExitArrayConstruct(void*, void*) EI_ORIGINAL(0x0056FFE0)
// 00584EA0 (RET 8). CArray<ZoneExit>::GetAt by value into `out`; returns out.
__declspec(naked) ZoneExit* __fastcall ExitArrayGetAt(const void*, void*, ZoneExit* /*out*/, int) EI_ORIGINAL(0x00584EA0)
// 00583B50 (RET 4). ZoneExit::operator=.
__declspec(naked) void __fastcall ZoneExitAssign(ZoneExit*, void*, const ZoneExit*) EI_ORIGINAL(0x00583B50)
// Rewritten elsewhere, through their addresses: SetUnknownDC / SetUnknownE0 / SetGameResult
// (world_server / world_tick), LoadMobFile (world_zone).
__declspec(naked) void __fastcall WorldSetUnknownDC(CWorldServer*, void*, int) EI_ORIGINAL(0x0058CC80)
__declspec(naked) void __fastcall WorldSetUnknownE0(CWorldServer*, void*, int) EI_ORIGINAL(0x0058CC70)
__declspec(naked) void __fastcall WorldSetGameResult(CWorldServer*, void*, int) EI_ORIGINAL(0x0058DC90)
__declspec(naked) int __fastcall WorldLoadMobFile(CWorldServer*, void*, const char*) EI_ORIGINAL(0x00595AB0)
// The host announcement of a network game: gethostname (WS2_32, through 006E6100), the address
// (00440440 builds it from the host and port, 00435010 formats it into an STLport string, 00425040
// destroys that), the server's host record (0x24 bytes: 004186E0, two strings 0041F460, vtable
// 0073BCA0; counted pointer at server + 0x25A0: 00413270 release, 00418BE0 share; 00426A80
// assigns a string from a range) and the message 0041A500(8, zone).
__declspec(naked) bool __fastcall AddressInit(void*, void*, const char* /*host*/, int /*port*/) EI_ORIGINAL(0x00440440)
__declspec(naked) const char* const* __fastcall AddressFormat(void*, void*, void* /*out string*/) EI_ORIGINAL(0x00435010)
__declspec(naked) void __fastcall HostRecordConstruct(void*, void*) EI_ORIGINAL(0x004186E0)
__declspec(naked) void __fastcall CountedDestroy(void* /*slot*/, void*) EI_ORIGINAL(0x00413270)
void __fastcall HostStringAssign(void* a0, void*, const char* a2, const char* a3) { StringAssignRangeOriginal(a0, nullptr, a2, a3); }
__declspec(naked) void __fastcall ServerSendText(void*, void*, int /*message*/, const char*) EI_ORIGINAL(0x0041A500)

std::uint8_t* GameServer() { return *reinterpret_cast<std::uint8_t**>(0x0079B5DC); }

// Tells the clients of the game server where the game is: the host's address and the zone.
void AnnounceHost(const char* zone) {
    char host[100];
    if (WsGetHostName(host, sizeof(host)) != 0) return;
    std::uint32_t address[4] = {0, 0, 0, 0};
    AddressInit(address, nullptr, host, kHostPort);
    alignas(4) std::uint8_t text[12];
    mfc::CString formatted(*AddressFormat(address, nullptr, text));
    StringBufferFree(text);
    void* memory = gamecrt::operator_new(0x24);
    At<std::uint32_t>(memory, 4) = 0;   // written before the null test
    void* record = nullptr;
    if (memory) {
        HostRecordConstruct(memory, nullptr);
        StringInitEmpty(static_cast<std::uint8_t*>(memory) + 0xC);
        StringInitEmpty(static_cast<std::uint8_t*>(memory) + 0x18);
        *static_cast<void**>(memory) = reinterpret_cast<void*>(kHostRecordVtable);
        record = memory;
    }
    void* slot = GameServer() + 0x25A0;
    if (record != *static_cast<void**>(slot)) {
        CountedDestroy(slot, nullptr);
        RefShare(slot, nullptr, record);
    }
    const char* text1 = formatted;
    std::uint8_t* shared = At<std::uint8_t*>(GameServer(), 0x25A0);
    HostStringAssign(shared + 0xC, nullptr, text1, text1 + std::strlen(text1));
    shared = At<std::uint8_t*>(GameServer(), 0x25A0);
    HostStringAssign(shared + 0x18, nullptr, zone, zone + std::strlen(zone));
    ServerSendText(GameServer(), nullptr, kServerMessageZone, zone);
}

} // namespace

// 0057BDF0 (__stdcall, RET 4). Starts network zone `zone` (a zone record of kind 0): tears the
// current zone down, restores saves\current\<zone>.sav when there is one (else loads the zone
// fresh at time 4), copies its exits with every target set to "MPGame1", applies its two settings,
// marks the game a network one and, on a running game server, announces the host and loads the
// zone's .mob. Returns 1, 0 on failure.
int __stdcall StartNetworkZone(const char* zone) {
    ToMainMenu() = 1;
    std::uint8_t* zoneRecord = MapDatabaseFind(kMapDatabase, nullptr, zone);
    if (!zoneRecord || At<int>(zoneRecord, kZoneKind) != 0) return 0;
    WorldTeardownZone(World(), nullptr);

    mfc::CString folder;   // [0x14]
    LocalFileStream file;  // [0x20]
    folder.Format(Text(kCurrentFolder), GameFolder());
    bool opened;
    {
        mfc::CString path = folder + zone;
        mfc::CString fileName = path + Text(kSaveExtension);
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
        bool loaded = ReadSaveFile(&data, kMagicMission, SaveVersion(), *reinterpret_cast<const int*>(0x00790268),
                                   &file.stream) &&
                      LoadSaveAt(World(), nullptr, &data, nullptr) != 0;
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
            mfc::CString mob = At<mfc::CString>(zoneRecord, kZoneMob) + Text(kMobExtension);
            mfc::CString map = At<mfc::CString>(zoneRecord, kZoneMap) + Text(kMapExtension);
            loaded = NewZone(World(), nullptr, map, mob, zone) != 0;
        }
        if (!loaded) return 0;
        WorldSetTime(World(), nullptr, kStartTime);
    }

    // The zone's exits, all leading to "MPGame1".
    ZoneExitArray exits;
    ExitArrayConstruct(&exits, nullptr);
    exits.vtable = reinterpret_cast<void*>(kExitArrayVtable);
    const auto* source = reinterpret_cast<const ZoneExitArray*>(zoneRecord + kZoneExits);
    exits.SetSize(source->size, -1);
    for (int i = 0; i < source->size; ++i) {
        alignas(4) std::uint8_t copy[sizeof(ZoneExit)];
        ZoneExit* exit = ExitArrayGetAt(source, nullptr, reinterpret_cast<ZoneExit*>(copy), i);
        ZoneExitAssign(&exits.data[i], nullptr, exit);
        reinterpret_cast<mfc::CString*>(&reinterpret_cast<ZoneExit*>(copy)->target)->~CString();
    }
    for (int i = 0; i < exits.size; ++i)
        *reinterpret_cast<mfc::CString*>(&exits.data[i].target) = Text(kNetworkExitTarget);
    SetZoneExitsAt(World(), nullptr, &exits);
    WorldSetUnknownDC(World(), nullptr, At<int>(zoneRecord, kZoneSetting2C));
    WorldSetUnknownE0(World(), nullptr, At<int>(zoneRecord, kZoneSetting30));
    CampaignMode() = 1;
    WorldSetGameResult(World(), nullptr, kGameResultNetwork);
    SpectatorFlag() = 1;

    std::uint8_t* server = GameServer();
    if (server && !(At<std::uint32_t>(server, 4) & 0x80000000u)) {
        AnnounceHost(zone);
        mfc::CString name(zone);
        mfc::CString mob = name + Text(kMobExtension);
        WorldLoadMobFile(World(), nullptr, mob);
    }

    exits.vtable = reinterpret_cast<void*>(kExitArrayBaseVtable);
    if (exits.data) {
        for (int i = 0; i < exits.size; ++i)
            reinterpret_cast<mfc::CString*>(&exits.data[i].target)->~CString();
        gamecrt::operator_delete(exits.data);
    }
    return 1;
}

// ---- LeaveZone --------------------------------------------------------------------------------

namespace {

constexpr std::uintptr_t kCantWrite = 0x007905A0;   // "Can`t write to %s"

// 00410860 (RET 4). The globals (0079B2A8) note the campaign's progress.
__declspec(naked) void __fastcall GlobalsSetStage(void*, void*, int) EI_ORIGINAL(0x00410860)
// 005956E0. CWorldServer: prepares the zone for saving.
__declspec(naked) void __fastcall WorldBeforeSave(CWorldServer*, void*) EI_ORIGINAL(0x005956E0)
// 0057AA80 (RET 4). CWorldServer::ZoneNameInto (rewritten below), through its address.
__declspec(naked) mfc::CString* __fastcall ZoneNameAt(CWorldServer*, void*, mfc::CString*) EI_ORIGINAL(0x0057AA80)
// 00460BC0 (RET 8). Named float table Set.
void __fastcall VariableSetAt(void* a0, void*, const char* a2, float a3) { NamedTableSet(a0, nullptr, a2, std::bit_cast<std::uint32_t>(a3)); }

} // namespace

mfc::CString* CWorldServer::ZoneNameInto(mfc::CString* out) {
    new (out) mfc::CString(*reinterpret_cast<const mfc::CString*>(&zoneName_));
    return out;
}

// 0057A060's counterpart (__stdcall, RET 4; from the state machine): saves the current zone into
// saves\current\<zone>.sav (SaveWorld, header 0x102) and records in the party's variables that
// it was visited (zs.<zone> = 1) and when it was left (zt.<zone> = gtime). Returns 1, 0 when the
// file cannot be written (after FatalError).
int __stdcall LeaveZone(std::uint8_t* records) {
    LocalFileStream file;
    mfc::CString path;   // [0x04]
    alignas(4) unsigned char zoneStorage[sizeof(mfc::CString)];
    mfc::CString& zone = *ZoneNameAt(World(), nullptr, reinterpret_cast<mfc::CString*>(zoneStorage));   // [0x08]
    struct ZoneOwner {
        mfc::CString* s;
        ~ZoneOwner() { s->~CString(); }
    } zoneOwner{&zone};
    // `path` must be destroyed after `zone`, as the original does: it is declared first.

    if (Campaign() == 1) GlobalsSetStage(kGlobals, nullptr, 7);
    else if (Campaign() == 3) GlobalsSetStage(kGlobals, nullptr, 6);
    path.Format(Text(kCurrentFolder), GameFolder());
    {
        mfc::CString fileName = zone + Text(kSaveExtension);
        path += fileName;
    }
    if (!file.Open(path, kOpenWrite, 0)) {
        game::FatalError(Text(kCantWrite), static_cast<const char*>(path));
        return 0;
    }
    WorldBeforeSave(World(), nullptr);
    CMemoryStream data;
    std::memset(&data, 0, sizeof(data));
    data.version_ = kStreamLimit;
    data.ok_ = 1;
    ei::SetVtable(&data, kMemoryStreamVtable);
    data.data_ = nullptr;
    data.Reset();
    data.version_ = static_cast<std::uint32_t>(SaveVersion());
    SaveWorldAt(World(), nullptr, &data);
    WriteSaveFile(&file.stream, kMagicMission, SaveVersion(), &data);
    file.Close();

    void* variables = records + kRecordsVariables;
    {
        mfc::CString prefix(Text(kVisitedPrefix));
        mfc::CString variable = prefix + static_cast<const char*>(zone);
        VariableSetAt(variables, nullptr, variable, 1.0f);
    }
    float now = StoreFloat(static_cast<float>(VariableGet(variables, nullptr, Text(kGTime))));
    {
        mfc::CString prefix(Text(kLeftPrefix));
        mfc::CString variable = prefix + zone;
        VariableSetAt(variables, nullptr, variable, now);
    }
    ei::SetVtable(&data, kMemoryStreamVtable);
    if (data.data_) gamecrt::operator_delete(data.data_);
    ei::SetVtable(&data, kStreamVtable);
    return 1;
}

// ---- The multiplayer flag -----------------------------------------------------------------------

namespace {

constexpr std::uintptr_t kMultiplayerVariable = 0x007906C8;   // "z.MPGame1"
constexpr std::uint32_t kSessionInactive = 0x80000000u;       // net_session.h

std::uint8_t* ClientSession() { return *reinterpret_cast<std::uint8_t**>(0x0079B5D8); }
std::uint8_t* ServerSession() { return *reinterpret_cast<std::uint8_t**>(0x0079B5DC); }
bool Active(const std::uint8_t* session) {
    return session && !(*reinterpret_cast<const std::uint32_t*>(session + 4) & kSessionInactive);
}
// The server world's player list (007AFF60 + 0x38); +0x2C is its player count.
int ServerPlayerCount() { return At<int>(*reinterpret_cast<void**>(0x007AFF98), 0x2C); }

} // namespace

void GameFlow::UpdateMultiplayerVariable() {
    auto* player = *reinterpret_cast<WorldPlayer**>(WorldClient()->LocalView() + 0x1C);
    void* variables = player->variables;
    if (!Active(ClientSession())) return;
    auto set = [variables](bool on) {
        if (on) VariableSetAt(variables, nullptr, Text(kMultiplayerVariable), 1.0f);
        else VariableRemove(variables, nullptr, Text(kMultiplayerVariable));
    };
    if (!Active(ServerSession())) {
        // A client: set while the client has no peer (+0x25EC).
        set(!Active(At<std::uint8_t*>(ClientSession(), 0x25EC)));
        return;
    }
    // The host: with +0x80, set while more than one player is in; otherwise while the client
    // has no peer at +0x25F4.
    if (At<char>(this, 0x80)) set(ServerPlayerCount() > 1);
    else set(!Active(At<std::uint8_t*>(ClientSession(), 0x25F4)));
}

// ---- Starting a multiplayer game ------------------------------------------------------------------

namespace {

// The multiplayer start zones by the client session's game type (+0x261C): 1..3 -> bz2mpg ..
// bz4mpg, anything else bz1mpg.
constexpr std::uintptr_t kMultiplayerZones[] = {0x00790690, 0x007906A8, 0x007906A0, 0x00790698};

// The party manager (007AA5A0): 004A88C0 resets it, 004A8890 (RET 4) begins a redeploy of a
// party.
__declspec(naked) void __fastcall PartyManagerReset(void*, void*) EI_ORIGINAL(0x004A88C0)
__declspec(naked) void __fastcall PartyManagerBeginRedeploy(void*, void*, void* /*party*/) EI_ORIGINAL(0x004A8890)
__declspec(naked) mfc::CString* __fastcall MultiplayerZoneAt(GameFlow*, void*, void* /*out*/) EI_ORIGINAL(0x0057E6B0)

void* PartyManager() { return reinterpret_cast<void*>(0x007AA5A0); }
// The party outside a game (007C82B0, laid out like WorldPlayer + 0x70): records +0x30, count
// +0x34, variables +0x54, target zone +0x68 (a CString).
std::uint8_t* GlobalParty() { return reinterpret_cast<std::uint8_t*>(0x007C82B0); }
constexpr std::uint32_t kMultiplayerHeroId = 0x3B9AF5CE;   // the only record's unit id

} // namespace

mfc::CString* GameFlow::MultiplayerZoneInto(mfc::CString* out) {
    unsigned type = At<unsigned>(ClientSession(), 0x261C);
    return new (out) mfc::CString(Text(kMultiplayerZones[type <= 3 ? type : 0]));
}

void GameFlow::BeginMultiplayerGame() {
    PartyManagerReset(PartyManager(), nullptr);
    if (At<int>(GlobalParty(), 0x34) == 1) At<std::uint32_t>(At<std::uint8_t*>(GlobalParty(), 0x30), 0x1C) = kMultiplayerHeroId;
    {
        alignas(mfc::CString) std::uint8_t storage[sizeof(mfc::CString)];
        mfc::CString* zone = MultiplayerZoneAt(this, nullptr, storage);
        At<mfc::CString>(GlobalParty(), 0x68) = *zone;
        zone->~CString();
    }
    VariableSetAt(GlobalParty() + 0x54, nullptr, Text(kMultiplayerVariable), 1.0f);
    PartyManagerBeginRedeploy(PartyManager(), nullptr, GlobalParty());
    {
        alignas(mfc::CString) std::uint8_t storage[sizeof(mfc::CString)];
        mfc::CString* zone = MultiplayerZoneAt(this, nullptr, storage);
        At<int>(this, 0) = 4;   // the state: load the zone
        At<mfc::CString>(this, 4) = static_cast<const char*>(*zone);
        At<int>(this, 0x78) = 0;
        zone->~CString();
    }
    At<char>(this, 0x80) = 0;
    // The client's game state request (+0x2600): kind 1, a new request.
    At<int>(At<std::uint8_t*>(ClientSession(), 0x2600), 0x28) = 1;
    ++At<int>(At<std::uint8_t*>(ClientSession(), 0x2600), 8);
    *reinterpret_cast<char*>(0x007C7DD8) = 1;
}

// ---- Small glue of the flow -----------------------------------------------------------------------

namespace {

// 00661300 (RET 4). Party::CopyFrom. 00662AC0 (RET 4): the party sends its map transfer.
__declspec(naked) void __fastcall PartySendTransfer(void*, void*, int) EI_ORIGINAL(0x00662AC0)
// 005800C0. The flow's update (patched by Rebirth: original address).
__declspec(naked) void __fastcall FlowUpdate(GameFlow*, void*) EI_ORIGINAL(0x005800C0)
// 0041D260 (RET 8). A session command (0 zone, 1 briefing, 2 kick, 3 ban).
__declspec(naked) void __fastcall SessionCommand(void*, void*, int, const char*) EI_ORIGINAL(0x0041D260)

constexpr int kCommandBriefing = 1;
constexpr int kRequestDone = 3;   // the client's game state request: answered

} // namespace

void GameFlow::ReturnLocalParty() {
    if (!Active(ClientSession())) return;
    auto* player = *reinterpret_cast<std::uint8_t**>(WorldClient()->LocalView() + 0x1C);
    void* party = player + 0x70;
    PartyCopyFrom(GlobalParty(), nullptr, party);
    PartyManagerBeginRedeploy(PartyManager(), nullptr, party);
    PartySendTransfer(GlobalParty(), nullptr, *reinterpret_cast<int*>(0x007C82A0));
    FlowUpdate(this, nullptr);
}

void GameFlow::UpdateAndAcknowledge() {
    if (!Active(ClientSession())) return;
    FlowUpdate(this, nullptr);
    std::uint8_t* request = At<std::uint8_t*>(ClientSession(), 0x2600);
    if (At<int>(request, 0x28) != kRequestDone) return;
    At<int>(request, 0x28) = 0;
    ++At<int>(At<std::uint8_t*>(ClientSession(), 0x2600), 8);
}

void GameFlow::RequestBriefing(const char* briefing) {
    std::uint8_t* client = ClientSession();
    if (Active(client)) SessionCommand(client, nullptr, kCommandBriefing, briefing);
}

namespace {

// 0041BC40. The game client session's constructor (0x2700 bytes).
__declspec(naked) std::uint8_t* __fastcall ClientSessionConstruct(void*, void*) EI_ORIGINAL(0x0041BC40)
constexpr std::size_t kClientSessionSize = 0x2700;

// The sessions' reference count is the low word at +4; the last reference deletes (slot 0 with 1).
void ReleaseSession(std::uint8_t* session) {
    if (session && --At<std::uint16_t>(session, 4) == 0) ei::VCall<void*>(session, 0, 1);
}

} // namespace

void GameFlow::RecreateClientSession() {
    auto& global = *reinterpret_cast<std::uint8_t**>(0x0079B5D8);
    std::uint8_t* old = global;
    global = nullptr;
    ReleaseSession(old);
    auto* memory = static_cast<std::uint8_t*>(gamecrt::operator_new(kClientSessionSize));
    At<std::uint32_t>(memory, 4) = 0;
    std::uint8_t* session = memory ? ClientSessionConstruct(memory, nullptr) : nullptr;
    std::uint8_t* previous = global;
    global = session;
    if (session) ++At<std::uint16_t>(session, 4);
    ReleaseSession(previous);
}

namespace {

__declspec(naked) int __fastcall SaveGameAt(GameFlow*, void*, const char*) EI_ORIGINAL(0x0057B3B0)
// 0066B1B0 (RET 8). The local view asks to go to a zone (name, entrance).
__declspec(naked) int __fastcall ViewRequestZone(void*, void*, const char*, int) EI_ORIGINAL(0x0066B1B0)

} // namespace

int GameFlow::SaveGameThunk(const char* slot) { return SaveGameAt(this, nullptr, slot); }

int __stdcall RequestZone(const char* zone, int entrance) {
    *reinterpret_cast<char*>(0x007C7D94) = 1;
    return ViewRequestZone(WorldClient()->LocalView(), nullptr, zone, entrance);
}

void GameFlow::CloseSessions() {
    *reinterpret_cast<char*>(0x007C7DD8) = 0;
    auto& server = *reinterpret_cast<std::uint8_t**>(0x0079B5DC);
    std::uint8_t* old = server;
    server = nullptr;
    ReleaseSession(old);
    auto& client = *reinterpret_cast<std::uint8_t**>(0x0079B5D8);
    old = client;
    client = nullptr;
    ReleaseSession(old);
    At<int>(this, 0) = 1;   // the state
}

// The flow's requests: the state (+0) its update (005800C0) acts on next, the zone or save name
// (+4, a CString), +0x78, and a party (+8) for states 5 and 6.
void GameFlow::RequestState3() { At<int>(this, 0) = 3; }

void GameFlow::RequestLoad(const char* name, char flag) {
    At<int>(this, 0) = 2;
    At<mfc::CString>(this, 4) = name;
    At<int>(this, 0x78) = flag != 0;
}

void GameFlow::RequestZoneChange(const char* zone, int entrance) {
    At<int>(this, 0) = 4;
    At<mfc::CString>(this, 4) = zone;
    At<int>(this, 0x78) = entrance;
}

void GameFlow::RequestPartyZone5(const char* zone, const void* party) {
    At<int>(this, 0) = 5;
    At<mfc::CString>(this, 4) = zone;
    PartyCopyFrom(reinterpret_cast<std::uint8_t*>(this) + 8, nullptr, party);
}

void GameFlow::RequestPartyZone6(const char* zone, const void* party) {
    At<int>(this, 0) = 6;
    At<mfc::CString>(this, 4) = zone;
    PartyCopyFrom(reinterpret_cast<std::uint8_t*>(this) + 8, nullptr, party);
}

// ---- ReadAIGraph ----------------------------------------------------------------------------------

namespace {

constexpr std::uintptr_t kGraphExtension = 0x00790BB0;   // ".grp"
constexpr std::uint32_t kGraphChunk = 0x31415926;         // the AI graph chunk in a .mob file

// 00595BA0 (RET 8, patched by Rebirth). The full path of a map file, constructed into `out`.
mfc::CString* __fastcall MapPathInto(CWorldServer* a0, void*, void* a2, const char* a3) { return (mfc::CString*)ServerMapPathOriginal((void*)a0, nullptr, a2, a3); }
// 004A4A90 (RET 8, AL). Finds the chunk `id` in a stream and reads it into `out`.
__declspec(naked) bool __fastcall StreamReadChunk(void*, void*, std::uint32_t, void* /*out*/) EI_ORIGINAL(0x004A4A90)
// 006FDBB7 (RET 4). CString::ReverseFind(char).
__declspec(naked) int __fastcall StringReverseFind(const mfc::CString*, void*, char) EI_ORIGINAL(0x006FDBB7)

} // namespace

int CWorldServer::ReadAIGraph(const char* map, const char* mob, void* out) {
    LocalFileStream mobFile;
    int found = 0;
    if (mob) {
        // The graph chunk of the .mob file.
        {
            alignas(mfc::CString) std::uint8_t storage[sizeof(mfc::CString)];
            mfc::CString* path = MapPathInto(this, nullptr, storage, mob);
            mobFile.Open(*path, kOpenRead, 1);
            path->~CString();
        }
        if (StreamReadChunk(&mobFile.stream, nullptr, kGraphChunk, out)) found = 1;
        mobFile.Close();
        if (found) return found;
    }
    // Otherwise <map without extension>.grp.
    alignas(mfc::CString) std::uint8_t storage[sizeof(mfc::CString)];
    mfc::CString* base = MapPathInto(this, nullptr, storage, map);
    int dot = StringReverseFind(base, nullptr, '.');
    if (dot > 0) {
        mfc::CString cut = base->Left(dot);
        *base = cut;
    }
    {
        LocalFileStream graph;
        bool opened;
        {
            mfc::CString path = *base + Text(kGraphExtension);
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
constexpr int kSlotStreamWrite = 2;

// A pair of STLport vector<char> (0x18 bytes): the decoded text, then the encoded bytes.
// 00598940 constructs an empty vector (allocator by pointer); 0042D2C0 / 00598D80 free them;
// 004D8EB0 (RET 8) resizes one, filling with *value; 004D8E10 (RET 4) decodes `in` into `out`
// and the CString.
struct CharVector {
    char* begin;
    char* end;
    char* capacity;
};
struct ScriptDecoder {
    CharVector out;
    CharVector in;
};
__declspec(naked) void __fastcall CharVectorConstruct(CharVector*, void*, const char* /*allocator*/) EI_ORIGINAL(0x00598940)
__declspec(naked) void __fastcall DecodedFree(CharVector*, void*) EI_ORIGINAL(0x0042D2C0)
__declspec(naked) void __fastcall EncodedFree(CharVector*, void*) EI_ORIGINAL(0x00598D80)
__declspec(naked) void __fastcall CharVectorResize(CharVector*, void*, unsigned, const char*) EI_ORIGINAL(0x004D8EB0)
__declspec(naked) void __fastcall ScriptDecode(ScriptDecoder*, void*, mfc::CString*) EI_ORIGINAL(0x004D8E10)
// 004BE6E0 / 004D23A0 (RET 8). A script command queue: reset, add a line.
int __fastcall QueueAdd(void* a0, void*, const mfc::CString* a2, int a3) { return ScriptLoadLine(a0, nullptr, a2, a3); }

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
    ScriptDecoder decoder;
    char allocator = 0;
    CharVectorConstruct(&decoder.out, nullptr, &allocator);
    CharVectorConstruct(&decoder.in, nullptr, &allocator);
    {
        alignas(mfc::CString) std::uint8_t storage[sizeof(mfc::CString)];
        mfc::CString* path = MapPathInto(this, nullptr, storage, mob);
        file.Open(*path, kOpenRead, 1);
        path->~CString();
    }
    StreamReadChunk(&file.stream, nullptr, kScriptsChunk, &scripts.s);
    if (StreamReadChunk(&scripts.s, nullptr, kScriptEncoded, &text.s)) {
        const std::uint8_t* data = text.s.data_;
        std::uint32_t size = text.s.size_;
        char zero = 0;
        CharVectorResize(&decoder.in, nullptr, size, &zero);
        std::memmove(decoder.in.begin, data, size);
        ScriptDecode(&decoder, nullptr, &script);
    } else if (StreamReadChunk(&scripts.s, nullptr, kScriptPlain, &text.s)) {
        text.s.Seek(text.s.size_);
        char zero = 0;
        ei::VCall<void>(&text.s, kSlotStreamWrite, &zero, 1u);
        script = reinterpret_cast<const char*>(text.s.data_);
    }
    QueueReset(commandsA_, nullptr);
    QueueAdd(commandsA_, nullptr, &script, 1);
    EncodedFree(&decoder.in, nullptr);
    DecodedFree(&decoder.out, nullptr);
}

// ---- SwitchMultiplayerZone ------------------------------------------------------------------------

namespace {

// 0041A500 (RET 8). A server command with an argument (9: drop a client).
__declspec(naked) void __fastcall ServerCommand(void*, void*, int, const char*) EI_ORIGINAL(0x0041A500)
// 00413270 / 00418BE0 (RET 4). The session handle at the server's +0x25A0: close / assign.
__declspec(naked) void __fastcall HandleClose(void*, void*) EI_ORIGINAL(0x00413270)
// 00590C70 / 00591070 (RET 4). CWorldServer: sync a player's party records / after the copy.
__declspec(naked) void __fastcall SyncPartyRecordsAt(void*, void*, WorldPlayer*) EI_ORIGINAL(0x00590C70)
__declspec(naked) void __fastcall AfterPartyCopy(void*, void*, WorldPlayer*) EI_ORIGINAL(0x00591070)
__declspec(naked) int __fastcall ViewRequestZoneAt(void*, void*, const char*, int) EI_ORIGINAL(0x0066B1B0)

constexpr int kServerDropClient = 9;

// The client's game state request (+0x2600): `kind`, a new request.
void RequestKind(int kind) {
    At<int>(At<std::uint8_t*>(ClientSession(), 0x2600), 0x28) = kind;
    ++At<int>(At<std::uint8_t*>(ClientSession(), 0x2600), 8);
}

} // namespace

void GameFlow::SwitchMultiplayerZone() {
    if (At<char>(this, 0x80)) {
        // A switch is pending: ask for the start zone again.
        RequestKind(1);
        std::uint8_t* server = ServerSession();
        bool host = server && !(At<std::uint32_t>(server, 4) & kSessionInactive);
        {
            alignas(mfc::CString) std::uint8_t storage[sizeof(mfc::CString)];
            mfc::CString* zone = MultiplayerZoneAt(this, nullptr, storage);
            ToMainMenu() = 1;
            ViewRequestZoneAt(WorldClient()->LocalView(), nullptr, *zone, 0);
            zone->~CString();
        }
        if (host) {
            // The host drops its client (+0x25A0) and closes the handle.
            std::uint8_t* client = At<std::uint8_t*>(ServerSession(), 0x25A0);
            if (Active(client)) ServerCommand(ServerSession(), nullptr, kServerDropClient, At<const char*>(client, 0x18));
            void* handle = ServerSession() + 0x25A0;
            if (*static_cast<void**>(handle)) {
                HandleClose(handle, nullptr);
                RefShare(handle, nullptr, nullptr);
            }
        }
        At<char>(this, 0x80) = 0;
        return;
    }

    // Every connection of the host (a list at +0x28) is reset to state 6.
    std::uint8_t* server = ServerSession();
    if (server && !(At<std::uint32_t>(server, 4) & kSessionInactive)) {
        auto* head = reinterpret_cast<std::uint8_t**>(server + 0x28);
        for (auto* node = *reinterpret_cast<std::uint8_t**>(*head); node != *head; node = *reinterpret_cast<std::uint8_t**>(node)) {
            std::uint8_t* connection = At<std::uint8_t*>(node, 8);
            std::uint32_t value = At<std::uint32_t>(connection, 0x114C);
            At<int>(connection, 0x1160) = 6;
            At<std::uint32_t>(connection, 0x1158) = value;
            At<std::uint32_t>(connection, 0x1150) = value;
        }
    }
    LocalRecords records;
    WorldPlayer* player = static_cast<WorldPlayer*>(WorldGetPlayer(reinterpret_cast<void*>(0x007AFF60), nullptr, 0));
    if (player && At<char>(At<void*>(player, 0x20), 0x70)) {
        SyncPartyRecordsAt(reinterpret_cast<void*>(0x007AFF60), nullptr, player);
        PartyCopyFrom(records.bytes, nullptr, reinterpret_cast<std::uint8_t*>(player) + 0x70);
        AfterPartyCopy(reinterpret_cast<void*>(0x007AFF60), nullptr, player);
    } else {
        PartyCopyFrom(records.bytes, nullptr, GlobalParty());
    }
    At<mfc::CString>(records.bytes, kRecordsZone) = At<mfc::CString>(this, 0x7C);
    RequestKind(3);
    if (Active(ServerSession())) {
        At<int>(this, 0) = 6;
        At<mfc::CString>(this, 4) = At<const char*>(this, 0x7C);
        PartyCopyFrom(reinterpret_cast<std::uint8_t*>(this) + 8, nullptr, records.bytes);
    } else {
        std::uint8_t* peer = At<std::uint8_t*>(ClientSession(), 0x25EC);
        if (Active(peer)) At<mfc::CString>(records.bytes, kRecordsZone) = At<const char*>(peer, 0x18);
        peer = At<std::uint8_t*>(ClientSession(), 0x25EC);
        if (Active(peer)) {
            At<int>(this, 0) = 5;
            At<mfc::CString>(this, 4) = At<const char*>(peer, 0x0C);
            PartyCopyFrom(reinterpret_cast<std::uint8_t*>(this) + 8, nullptr, records.bytes);
        }
    }
    At<char>(this, 0x80) = 1;
}

// ---- StartServer ----------------------------------------------------------------------------------

namespace {

constexpr std::uintptr_t kLocalHost = 0x007906B8;     // "127.0.0.1"
constexpr std::uintptr_t kServerName = 0x007906B0;    // "Server"
constexpr std::size_t kServerSessionSize = 0x2600;
constexpr std::size_t kPasswordSize = 0x14;

// 0057F560 / 0057EB10 (patched by Rebirth): the flow before / after starting the sessions.
__declspec(naked) void __fastcall FlowBeforeServer(GameFlow*, void*) EI_ORIGINAL(0x0057F560)
__declspec(naked) void __fastcall FlowAfterServer(GameFlow*, void*) EI_ORIGINAL(0x0057EB10)
// 00412D80 (RET 4). The game server session's constructor (game type).
__declspec(naked) std::uint8_t* __fastcall ServerSessionConstruct(void*, void*, int) EI_ORIGINAL(0x00412D80)
// 00419CD0 (RET 4). A session reference (0079B5DC / 0079B5D8) assigned.
__declspec(naked) void __fastcall SessionRefAssign(void*, void*, void*) EI_ORIGINAL(0x00419CD0)
// 00440440 (RET 8). A 16-byte network address from a host name and a port.
__declspec(naked) bool __fastcall AddressSet(void*, void*, const char*, int) EI_ORIGINAL(0x00440440)
// 00436C60 (RET 8). A buffer takes bytes.
__declspec(naked) void __fastcall BufferPut(GameBuffer*, void*, const void*, int) EI_ORIGINAL(0x00436C60)
// 00442980 (RET 8). The client connects to an address with a hello buffer.
__declspec(naked) void __fastcall ClientConnect(void*, void*, void*, GameBuffer*) EI_ORIGINAL(0x00442980)
// 00426A80 (RET 8). STLport string assign [first, last).
void __fastcall StringAssignRange(void* a0, void*, const char* a2, const char* a3) { StringAssignRangeOriginal(a0, nullptr, a2, a3); }
// 0041A140 (RET 4) / 00417E10 (RET 4). The server's password / whether it is listed.
__declspec(naked) void __fastcall ServerSetPassword(void*, void*, const char*) EI_ORIGINAL(0x0041A140)
__declspec(naked) void __fastcall ServerSetPublic(void*, void*, int) EI_ORIGINAL(0x00417E10)
__declspec(naked) std::uint8_t* __fastcall ClientSessionNew(void*, void*) EI_ORIGINAL(0x0041BC40)
// 006E7E80. The CRT's strncpy.

} // namespace

void GameFlow::StartServer(const char* name, int value25E0, int gameType, const char* password, char unlisted,
                           int value25FC) {
    FlowBeforeServer(this, nullptr);
    *reinterpret_cast<std::uintptr_t*>(0x0079B91C) = 0x007C56B0;
    {
        auto* memory = static_cast<std::uint8_t*>(gamecrt::operator_new(kServerSessionSize));
        At<std::uint32_t>(memory, 4) = 0;
        std::uint8_t* server = memory ? ServerSessionConstruct(memory, nullptr, gameType) : nullptr;
        SessionRefAssign(reinterpret_cast<void*>(0x0079B5DC), nullptr, server);
    }
    {
        auto* memory = static_cast<std::uint8_t*>(gamecrt::operator_new(kClientSessionSize));
        At<std::uint32_t>(memory, 4) = 0;
        std::uint8_t* client = memory ? ClientSessionNew(memory, nullptr) : nullptr;
        SessionRefAssign(reinterpret_cast<void*>(0x0079B5D8), nullptr, client);
    }
    // The local client connects to the local server with the password as its hello.
    alignas(4) std::uint8_t address[16] = {};
    AddressSet(address, nullptr, Text(kLocalHost), *reinterpret_cast<const int*>(0x00783E30));
    {
        GameBuffer hello;
        GrowingBufferConstruct(&hello);
        char secret[kPasswordSize] = {};
        if (password) GameStrncpy(secret, password, kPasswordSize);
        BufferPut(&hello, nullptr, secret, static_cast<int>(kPasswordSize));
        ClientConnect(ClientSession(), nullptr, address, &hello);
        GrowingBufferDestruct(&hello);
    }
    At<std::uint8_t>(At<std::uint8_t*>(ClientSession(), 0x2600), 0x24) = 1;
    const char* shown = name && *name ? name : Text(kServerName);
    StringAssignRange(ServerSession() + 0x25C8, nullptr, shown, shown + std::strlen(shown));
    At<int>(ServerSession(), 0x25E0) = value25E0;
    if (password && *password) ServerSetPassword(ServerSession(), nullptr, password);
    ServerSetPublic(ServerSession(), nullptr, unlisted == 0);
    At<int>(ClientSession(), 0x261C) = gameType;
    At<int>(ServerSession(), 0x25FC) = value25FC;
    FlowAfterServer(this, nullptr);
}

// ---- AcceptPlayers --------------------------------------------------------------------------------

namespace {

constexpr std::uintptr_t kAcceptedPlayer = 0x007908AC;   // "Accepted player "
constexpr std::uintptr_t kSessionListVtable = 0x0073FA4C;
constexpr int kSlotLobbyTakeNew = 6;                      // the lobby's slot 6 (+0x18): new connections into a list
void* const kNetLog = reinterpret_cast<void*>(0x007C56B0);   // the network log's ostream

// 00598EF0. The connection list's destructor.
__declspec(naked) void __fastcall SessionListDestruct(void*, void*) EI_ORIGINAL(0x00598EF0)
// 0066C180. The player list makes a new player. 00665990 (RET 4): the player takes a connection.
WorldPlayer* __fastcall PlayersAdd(void* a0, void*) { return (WorldPlayer*)PlayerCreateOriginal(a0); }
__declspec(naked) void __fastcall PlayerAttach(WorldPlayer*, void*, void*) EI_ORIGINAL(0x00665990)
// 006FC9C6 (RET 4). ostream << long.
inline void* __fastcall OstreamPutLong(void* s, void*, long v) { return OstreamPutLongOriginal(s, nullptr, v); }

} // namespace

void CWorldServer::AcceptNewPlayers(void* lobby) {
    mfc::CTypedPtrList<std::uint8_t> joined;
    joined.vtable = reinterpret_cast<void*>(kSessionListVtable);
    joined.head = nullptr;
    joined.tail = nullptr;
    joined.count = 0;
    joined.free = nullptr;
    joined.blocks = nullptr;
    joined.blockSize = 10;
    if (gameResult_ == 0) {
        SessionListDestruct(&joined, nullptr);
        return;
    }
    ei::VCall<void>(lobby, kSlotLobbyTakeNew, &joined);
    for (auto* node = joined.head; node;) {
        auto* next = node->next;
        std::uint8_t* connection = node->data;
        WorldPlayer* player = PlayersAdd(players_, nullptr);
        PlayerAttach(player, nullptr, connection);
        player->dirty = 0xFFFFFFFFu;
        At<int>(player, 0x100) = unknown12D64_;
        std::memcpy(At<std::uint8_t*>(player, 0x20) + 0x38, startPosition_, 12);
        int id = player->id;
        void* log = OstreamPutLong(OstreamPutString(kNetLog, nullptr, Text(kAcceptedPlayer)), nullptr, id);
        OstreamManipulate(OstreamPutChar(log, nullptr, '\n'), nullptr, kFlush);
        node = next;
    }
    // The list's inlined destructor: every reference released, then the blocks.
    joined.vtable = reinterpret_cast<void*>(kSessionListVtable);
    for (auto* node = joined.head; node; node = node->next) ReleaseSession(node->data);
    joined.count = 0;
    joined.free = nullptr;
    joined.tail = nullptr;
    joined.head = nullptr;
    mfc::PlexFreeDataChain(joined.blocks);
}

// ---- PickZones ------------------------------------------------------------------------------------

namespace {

// The zone table (007AFDE0: a vector of 12-byte {first, second, id}, two CStrings and an int).
struct ZoneEntry {
    const char* first;
    const char* second;
    int id;
};
struct ZoneTable {
    ZoneEntry* start;
    ZoneEntry* finish;
    ZoneEntry* end;
};
struct OutStrings {
    stlp::string* start;
    stlp::string* finish;
    stlp::string* end;
};
// 004DCE90 (RET 0xC). vector<string>::_M_insert_overflow(pos, value, count).
void __fastcall OutInsertOverflow(OutStrings* a0, void*, stlp::string* a2, const stlp::string* a3, unsigned a4) { StringVectorInsertOverflowOriginal((void*)a0, nullptr, (void*)a2, (const void*)a3, a4); }
// 004667F0. IsaacRandom::Next on the game's generator (007A9D40).
__declspec(naked) std::uint32_t __fastcall RandomNext(void*, void*) EI_ORIGINAL(0x004667F0)

bool SameText(const char* a, const char* b) {
    return gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(a), reinterpret_cast<const unsigned char*>(b)) == 0;
}

// A list of entry pointers on the game's heap (the original keeps STLport lists of copies).
struct EntryList {
    const ZoneEntry** items = nullptr;
    int count = 0;
    explicit EntryList(int capacity) {
        items = static_cast<const ZoneEntry**>(gamecrt::operator_new(static_cast<std::size_t>(capacity > 0 ? capacity : 1) * 4));
    }
    ~EntryList() { gamecrt::operator_delete(items); }
    EntryList(const EntryList&) = delete;
    EntryList& operator=(const EntryList&) = delete;
    void RemoveAt(int i) {
        for (int k = i + 1; k < count; ++k) items[k - 1] = items[k];
        --count;
    }
};

} // namespace

void __stdcall PickZones(void* outArg, int id, const char* exclude) {
    auto* out = static_cast<OutStrings*>(outArg);
    ZoneTable& table = *reinterpret_cast<ZoneTable*>(0x007AFDE0);
    int total = static_cast<int>(table.finish - table.start);
    EntryList pending(total);
    EntryList group(total);
    for (ZoneEntry* e = table.start; e != table.finish; ++e)
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
        unsigned pick = RandomNext(reinterpret_cast<void*>(0x007A9D40), nullptr) % static_cast<unsigned>(group.count);
        stlp::string name(group.items[pick]->first);
        if (out->finish != out->end) {
            new (out->finish) stlp::string(name);
            ++out->finish;
        } else {
            OutInsertOverflow(out, nullptr, out->finish, &name, 1);
        }
    }
}

// ---- LoadMission ----------------------------------------------------------------------------------

namespace {

constexpr unsigned kMagicMissionFile = 0x102;
constexpr int kVersionGlobalsInMission = 0x52;   // below: the globals (0079B2A8) come from mission.sav
constexpr int kVersionInputInMission = 0x47;     // below: the input state (007C7D50) too

// 00579830 (RET 0xC). SaveFileRead through its address.
__declspec(naked) bool __fastcall SaveFileReadAt(void*, unsigned, int, int, void*) EI_ORIGINAL(0x00579830)
// 00410AB0 (RET 4) / 005F8420 (RET 4). The globals / the input state read from a stream.
__declspec(naked) void __fastcall GlobalsLoad(void*, void*, CMemoryStream*) EI_ORIGINAL(0x00410AB0)
__declspec(naked) void __fastcall InputLoad(void*, void*, CMemoryStream*) EI_ORIGINAL(0x005F8420)
// 004A51C0 (RET 4). A memory stream copies its remaining data into another.
void __fastcall StreamCopyRest(CMemoryStream* a0, void*, void* a2) { StreamReadSizedOriginal((void*)a0, nullptr, a2); }
// 00596E70 (RET 8). CWorldServer::LoadSave through its address.
__declspec(naked) int __fastcall LoadSaveAt(void*, void*, CMemoryStream*, const char*) EI_ORIGINAL(0x00596E70)

} // namespace

int __stdcall LoadMission(void* file) {
    LoadingStepOriginal(*reinterpret_cast<void**>(0x007AF784), nullptr, 1);
    LocalMemoryStream mission;
    if (!SaveFileReadAt(&mission.s, kMagicMissionFile, *reinterpret_cast<const int*>(0x00790260),
                        *reinterpret_cast<const int*>(0x00790268), file))
        return 0;
    int version = static_cast<int>(mission.s.version_);
    if (version < kVersionGlobalsInMission) GlobalsLoad(reinterpret_cast<void*>(0x0079B2A8), nullptr, &mission.s);
    if (static_cast<int>(mission.s.version_) < kVersionInputInMission) {
        // Old saves: the input state, then the rest goes to the stream at 007AFDF8.
        reinterpret_cast<CMemoryStream*>(0x007AFDF8)->Resize(0);
        InputLoad(reinterpret_cast<void*>(0x007C7D50), nullptr, &mission.s);
        StreamCopyRest(&mission.s, nullptr, reinterpret_cast<void*>(0x007AFDF8));
        *reinterpret_cast<std::uint32_t*>(0x007AFE00) = mission.s.version_;
    }
    return LoadSaveAt(reinterpret_cast<void*>(0x007AFF60), nullptr, &mission.s, nullptr);
}

// ---- ClearSaveFolder ------------------------------------------------------------------------------

namespace {

// MFC CFileFind (0x1C bytes) members, and the Win32 calls through game.exe's import slots (harness
// runs redirect those to harness_saves).
__declspec(naked) void __fastcall FinderConstruct(void*, void*) EI_ORIGINAL(0x006E6370)
__declspec(naked) void __fastcall FinderDestruct(void*, void*) EI_ORIGINAL(0x006E63AD)
__declspec(naked) int __fastcall FinderFindFile(void*, void*, const char*, unsigned long) EI_ORIGINAL(0x006E6433)
__declspec(naked) int __fastcall FinderNext(void*, void*) EI_ORIGINAL(0x006E6653)
__declspec(naked) mfc::CString* __fastcall FinderPath(void*, void*, void* /*out*/) EI_ORIGINAL(0x006E6722)
__declspec(naked) void __fastcall FinderClose(void*, void*) EI_ORIGINAL(0x006E63E9)
constexpr std::uintptr_t kAllFiles = 0x007905B4;   // "*.*"
using CreateDirectoryFn = BOOL(__stdcall*)(const char*, void*);
using DeleteFileFn = BOOL(__stdcall*)(const char*);
CreateDirectoryFn ImportCreateDirectory() { return *reinterpret_cast<CreateDirectoryFn const*>(0x0073B1B8); }
DeleteFileFn ImportDeleteFile() { return *reinterpret_cast<DeleteFileFn const*>(0x0073B20C); }

} // namespace

void __fastcall ClearSaveFolder(const char* path) {
    ImportCreateDirectory()(path, nullptr);
    alignas(4) std::uint8_t finder[0x1C];
    FinderConstruct(finder, nullptr);
    int found;
    {
        mfc::CString folder(path);
        mfc::CString pattern = folder + Text(kAllFiles);
        found = FinderFindFile(finder, nullptr, pattern, 0);
    }
    while (found) {
        found = FinderNext(finder, nullptr);
        alignas(mfc::CString) std::uint8_t name[sizeof(mfc::CString)];
        mfc::CString* file = FinderPath(finder, nullptr, name);
        ImportDeleteFile()(*file);
        file->~CString();
    }
    FinderClose(finder, nullptr);
    FinderDestruct(finder, nullptr);
}

bool __fastcall NameInList(const char* const* list, const char* name) {
    for (; *list[0] != 0; ++list)
        if (gamecrt::stricmp(*list, name) == 0) return true;
    return false;
}

// ---- StartZone --------------------------------------------------------------------------------

namespace {

constexpr std::uintptr_t kEndOfGame = 0x00790664;       // "endofgame"
constexpr std::uintptr_t kZoneNotFound = 0x00790650;    // "Zone %s not found"
constexpr std::uintptr_t kMenuMap = 0x00790628;         // "ZoneMainMenuNew.mpr"
constexpr std::uintptr_t kMenuMob = 0x0079063C;         // "ZoneMainMenuNew.mob"
constexpr std::uintptr_t kMinuteHours = 0x0073F8A0;     // 1/60 (float)

// StartZone's modes (also stored as the campaign mode, 007C2CC4).
constexpr int kModeFromRecord = 0;   // by the zone record's kind: 0 -> 1, 1 -> 4, 2 -> 3
constexpr int kModeMainMenu = 2;
constexpr int kModeEndOfGame = 8;    // the zone "endofgame"
// SetGameResult's values here.
constexpr int kResultPlaying = 1;
constexpr int kResultMainMenu = 4;

int TimeSpeedSetting() { return *reinterpret_cast<const int*>(0x0079BE5C); }
std::uint8_t& SessionsOpen() { return *reinterpret_cast<std::uint8_t*>(0x007C7DD8); }
void* const kClientWorld = reinterpret_cast<void*>(0x007AF5B8);

// 0058EEE0 / 0058EEA0 (RET 4). CWorldServer::SetTimeSpeed / SetTime.
__declspec(naked) void __fastcall WorldSetTimeSpeedF(CWorldServer*, void*, float) EI_ORIGINAL(0x0058EEE0)
void __fastcall WorldSetTimeF(CWorldServer* a0, void*, float a2) { WorldSetTimeOriginal((void*)a0, nullptr, std::bit_cast<std::uint32_t>(a2)); }
// 00566CE0 (RET 4) / 005699F0. The client world: the local player / its local view.
__declspec(naked) std::uint8_t* __fastcall ClientWorldLocalView(void*) EI_ORIGINAL(0x005699F0)
// 0057A060 (__stdcall; the original passes the flow in ECX too). EnterZone above, through its address.
__declspec(naked) int __stdcall EnterZoneAt(std::uint8_t*) EI_ORIGINAL(0x0057A060)

// `value` mod 24 through the game's _CIfmod (006E95AA), stored as float.
float HoursOfDay(double value) {
    float result;
    __asm {
        fld value
        mov eax, 0x0073D108   ; 24.0 (double)
        fld qword ptr [eax]
        mov eax, 0x006E95AA
        call eax
        fstp result
    }
    return result;
}

} // namespace

void GameFlow::StartZone(const char* zone, int mode) {
    RendererSetBusy(kRenderer, nullptr, 3);
    std::uint8_t* zoneRecord = nullptr;
    ToMainMenu() = 1;
    if (gamecrt::stricmp(zone, Text(kEndOfGame)) == 0) {
        mode = kModeEndOfGame;
    } else {
        zoneRecord = MapDatabaseFind(kMapDatabase, nullptr, zone);
    }
    WorldTeardownZone(World(), nullptr);

    bool found;
    if (mode == kModeFromRecord) {
        found = zoneRecord != nullptr;
        if (found) {
            switch (At<int>(zoneRecord, kZoneKind)) {
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
        game::FatalError(Text(kZoneNotFound), zone);
        std::uint8_t* client = ClientSession();
        if (client && !(At<std::uint32_t>(client, 4) & kSessionInactive)) {
            auto& server = *reinterpret_cast<std::uint8_t**>(0x0079B5DC);
            std::uint8_t* old = server;
            SessionsOpen() = 0;
            server = nullptr;
            ReleaseSession(old);
            auto& clientSlot = *reinterpret_cast<std::uint8_t**>(0x0079B5D8);
            old = clientSlot;
            clientSlot = nullptr;
            ReleaseSession(old);
        }
        mode = kModeMainMenu;
    }

    WorldSetTimeSpeedF(World(), nullptr, static_cast<float>(TimeSpeedSetting()));
    int result;
    if (mode == kModeMainMenu) {
        NewZone(World(), nullptr, Text(kMenuMap), Text(kMenuMob), nullptr);
        // The menu shows the local time of day.
        SYSTEMTIME now;
        gameapi::GetLocalTime(&now);
        int minute = now.wMinute;
        int hour = now.wHour;
        WorldSetTimeF(World(), nullptr, static_cast<float>(minute) * GameFloat(kMinuteHours) + static_cast<float>(hour));
        result = kResultMainMenu;
        // Rebirth's AllowScriptsInMainMenu (a JMP at 0057C69F to a stub): the first script command
        // queue runs in the menu too.
        if (ei::rebirth::Enabled(ei::rebirth::Patch::AllowScriptsInMainMenu)) World()->unknown160_ = 1;
    } else if (mode == kModeEndOfGame) {
        result = kResultPlaying;
    } else {
        // The party enters the zone, then the clock is set from its "gtime".
        LocalRecords records;
        PartyStore(kParty, nullptr, records.bytes);
        EnterZoneAt(records.bytes);
        PartyTake(kParty, nullptr, records.bytes);
        WorldSetTimeF(World(), nullptr,
                      HoursOfDay(VariableGet(records.bytes + kRecordsVariables, nullptr, Text(kGTime))));
        if (zoneRecord) {
            WorldSetUnknownDC(World(), nullptr, At<int>(zoneRecord, kZoneSetting2C));
            WorldSetUnknownE0(World(), nullptr, At<int>(zoneRecord, kZoneSetting30));
        }
        result = kResultPlaying;
    }

    CampaignMode() = mode;
    if (mode == 1) SpectatorFlag() = mode;
    WorldSetGameResult(World(), nullptr, result);
    ClientSetPlayer(kClientWorld, nullptr, At<int>(World(), 0x5C));
    ClientWorldLocalView(kClientWorld)[0x85] = 0;
}

// ---- New game (0057BD10) ------------------------------------------------------------------------

namespace {

constexpr std::uintptr_t kExecAutoexec = 0x007905F0;   // "exec autoexec.cfg"
// 005DC620 / 004A88A0 through their addresses: the console executor, the party manager's new game.
__declspec(naked) void __fastcall PartyNewGameAt(void*) EI_ORIGINAL(0x004A88A0)

} // namespace

void GameFlow::StartNewGame() {
    // Rebirth's AutoExec #2 (the CALL at 0057BD30 redirected): addon's AutoExec lines first.
    if (ei::rebirth::Enabled(ei::rebirth::Patch::AutoExec)) console::RunAddonAutoExec();
    ExecuteAt(Text(kExecAutoexec));
    ToMainMenu() = 1;
    PartyNewGameAt(kParty);
    mfc::CString folder;
    folder.Format(Text(kCurrentFolder), GameFolder());
    ClearSaveFolderAt(folder);
    LocalRecords records;
    PartyStore(kParty, nullptr, records.bytes);
    StartZoneAt(this, nullptr, At<const char*>(records.bytes, kRecordsZone), 0);
}

// ---- The flow's requests (0057C700) -------------------------------------------------------------

namespace {

constexpr std::uintptr_t kResourcesSingle = 0x00784818;   // "database.res"
constexpr std::uintptr_t kResourcesNetwork = 0x00790680;  // "databaseLMP.res"
constexpr std::uintptr_t kAutoplay = 0x00790670;          // "autoplay.apl"
constexpr int kStateMainMenu = 1, kStateLoad = 2, kStateNewGame = 3, kStateZone = 4, kStatePartyZone = 5,
              kStateNetworkZone = 6;

__declspec(naked) int __stdcall LeaveZoneAt(std::uint8_t*) EI_ORIGINAL(0x0057A780)
__declspec(naked) int __stdcall StartNetworkZoneAt(const char*) EI_ORIGINAL(0x0057BDF0)
__declspec(naked) void __fastcall LoadGameAt(GameFlow*, void*, const char*) EI_ORIGINAL(0x0057ABA0)
__declspec(naked) void __fastcall BeginMultiplayerGameAt(GameFlow*, void*) EI_ORIGINAL(0x0057F280)
// 005F74F0 (the interface 007C7D50): closes its windows for the zone change.
__declspec(naked) void __fastcall InterfaceCloseAll(void*) EI_ORIGINAL(0x005F74F0)
// 0057CB50 (ECX name): switches the resource database.
__declspec(naked) void __fastcall SwitchResources(const char*) EI_ORIGINAL(0x0057CB50)
// The client world (007AF5B8): its tick (005641F0), a zone check (00566D80, AL); the server's
// update (0058DFF0); the network pump (004122D0).
__declspec(naked) int __fastcall ClientSegmentAt(void*) EI_ORIGINAL(0x005641F0)
__declspec(naked) bool __fastcall ClientHasZone(void*, void*, const char*) EI_ORIGINAL(0x00566D80)
__declspec(naked) int __fastcall ServerUpdateAt(CWorldServer*) EI_ORIGINAL(0x0058DFF0)
// The local view: autoplay on (00669710, name) / off (00669750); the local player takes the
// flow's party copy (00665CC0, +8).
__declspec(naked) void __fastcall ViewAutoplayOn(void*, void*, const char*) EI_ORIGINAL(0x00669710)
__declspec(naked) void __fastcall ViewAutoplayOff(void*) EI_ORIGINAL(0x00669750)
__declspec(naked) void __fastcall PlayerTakeParty(void*, void*, void*) EI_ORIGINAL(0x00665CC0)

mfc::CString& LastResources() { return *reinterpret_cast<mfc::CString*>(0x007AFDEC); }
std::uint8_t& LastResourcesGuard() { return *reinterpret_cast<std::uint8_t*>(0x007AFDF0); }
int& SavedSpectator() { return *reinterpret_cast<int*>(0x0079BEC4); }
int& Spectator0() { return *reinterpret_cast<int*>(0x0079BEC0); }
std::uint8_t& AutoplayOn() { return *reinterpret_cast<std::uint8_t*>(0x007B007C); }

} // namespace

void GameFlow::ProcessRequest() {
    int& state = At<int>(this, 0);
    if (state == 0) return;
    // The zone being left is saved (single player campaigns 1 and 3, or the host in campaign 1).
    if (GameMode() == 1 && (Campaign() == 1 || Campaign() == 3) && state == kStateZone) {
        LocalRecords records;
        PartyStore(kParty, nullptr, records.bytes);
        LeaveZoneAt(records.bytes);
        PartyTake(kParty, nullptr, records.bytes);
    }
    if (GameMode() == 2 && Campaign() == 1 && state == kStateZone) {
        LocalRecords records;
        LeaveZoneAt(records.bytes);
    }
    InterfaceCloseAll(reinterpret_cast<void*>(0x007C7D50));
    WorldTeardownZone(World(), nullptr);
    std::uint8_t* client = ClientSession();
    bool network = client && !(At<std::uint32_t>(client, 4) & kSessionInactive);
    if (!(LastResourcesGuard() & 1)) {
        LastResourcesGuard() |= 1;
        new (&LastResources()) mfc::CString(Text(kResourcesSingle));
        GameAtExit(reinterpret_cast<void(__cdecl*)()>(0x0057E6A0));
    }
    {
        mfc::CString wanted(Text(network ? kResourcesNetwork : kResourcesSingle));
        if (gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(static_cast<const char*>(wanted)),
                            reinterpret_cast<const unsigned char*>(static_cast<const char*>(LastResources()))) != 0) {
            LastResources() = wanted;
            SwitchResources(wanted);
            if (network) {
                int value = Spectator0();
                Spectator0() = 0;
                SavedSpectator() = value;
            } else {
                Spectator0() = SavedSpectator();
            }
        }
    }

    auto* clientWorld = reinterpret_cast<void*>(0x007AF5B8);
    switch (state) {
    case kStateMainMenu:
        ToMainMenu() = 1;
        StartZoneAt(this, nullptr, Text(kMainMenu), 2);
        break;
    case kStateLoad:
        ClientSetPlayer(clientWorld, nullptr, *reinterpret_cast<int*>(0x007AFFBC));
        ClientWorldLocalView(clientWorld)[0x85] = 0;
        WorldSetGameResult(World(), nullptr, 1);
        ClientSegmentAt(clientWorld);
        ServerUpdateAt(World());
        ClientSegmentAt(clientWorld);
        ServerUpdateAt(World());
        LoadGameAt(this, nullptr, At<const char*>(this, 4));
        if (At<int>(this, 0x78)) {
            const char* autoplay = Text(kAutoplay);
            if (autoplay) {
                ViewAutoplayOn(ClientWorldLocalView(clientWorld), nullptr, autoplay);
                AutoplayOn() = 1;
            } else {
                ViewAutoplayOff(ClientWorldLocalView(clientWorld));
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
        StartZoneAt(this, nullptr, At<const char*>(this, 4), 0);
        break;
    case kStatePartyZone: {
        const char* zone = At<const char*>(this, 4);
        void* player = At<void*>(ClientWorldLocalView(clientWorld), 0x1C);
        PlayerTakeParty(player, nullptr, reinterpret_cast<std::uint8_t*>(this) + 8);
        WorldTeardownZone(World(), nullptr);
        if (!ClientHasZone(clientWorld, nullptr, zone)) {
            std::uint8_t* session = ClientSession();
            if (session && !(At<std::uint32_t>(session, 4) & kSessionInactive)) {
                BeginMultiplayerGameAt(this, nullptr);
            } else {
                ToMainMenu() = 1;
                StartZoneAt(this, nullptr, Text(kMainMenu), 2);
            }
        }
        ClientWorldLocalView(clientWorld)[0x85] = 1;
        break;
    }
    case kStateNetworkZone: {
        StartNetworkZoneAt(At<const char*>(this, 4));
        void* player = At<void*>(ClientWorldLocalView(clientWorld), 0x1C);
        PlayerTakeParty(player, nullptr, reinterpret_cast<std::uint8_t*>(this) + 8);
        ClientSetPlayer(clientWorld, nullptr, *reinterpret_cast<int*>(0x007AFFBC));
        ClientWorldLocalView(clientWorld)[0x85] = 1;
        break;
    }
    default:
        break;
    }
    state = 0;
    NetworkTimers();
}

// ---- LoadGame (0057ABA0) ------------------------------------------------------------------------

namespace {

constexpr std::uintptr_t kLoadedText = 0x007905B8;   // "Loaded "
constexpr int kScenarioScriptsVersion = 0x47;      // scenario.sav from this version holds the input and scripts
constexpr int kScenarioGlobalsVersion = 0x52;      // and from this one the globals
// The scenario.sav parts read back: the party (004A8310), the input state (005F85C0's reader
// 005F8420), the scripts (004A51C0, a stream method), the globals (00410AB0); the world from
// mission.sav (00579E60, a flow method, from the file stream).
__declspec(naked) void __fastcall PartyLoad(void*, void*, CMemoryStream*) EI_ORIGINAL(0x004A8310)
void __fastcall StreamLoadScripts(CMemoryStream* a0, void*, void* a2) { StreamReadSizedOriginal((void*)a0, nullptr, a2); }
__declspec(naked) void __fastcall FlowLoadMission(GameFlow*, void*, FileStream*) EI_ORIGINAL(0x00579E60)
int& LoadedScriptsVersion() { return *reinterpret_cast<int*>(0x007AFE00); }

} // namespace

int GameFlow::LoadGame(const char* slot) {
    if (GameMode() == 2 || GameMode() == 3) return 0;
    // Rebirth's AutoExec #1 (the CALL at 0057ABE1 redirected): addon's AutoExec lines first.
    if (ei::rebirth::Enabled(ei::rebirth::Patch::AutoExec)) console::RunAddonAutoExec();
    ExecuteAt(Text(kExecAutoexec));
    RendererSetBusy(kRenderer, nullptr, 3);
    ToMainMenu() = 1;
    LocalFileStream file;
    mfc::CString zone;          // [0x14]
    mfc::CString slotFolder;    // [0x10]
    mfc::CString currentFolder; // [0x18]
    slotFolder.Format(Text(kSlotFolder), GameFolder(), slot);
    currentFolder.Format(Text(kCurrentFolder), GameFolder());
    ClearSaveFolderAt(currentFolder);
    bool opened;
    {
        mfc::CString path = slotFolder + Text(kScenarioFile);
        opened = file.Open(path, kOpenRead, 1);
    }
    if (!opened) {
        ToMainMenu() = 1;
        StartZoneAt(this, nullptr, Text(kMainMenu), 2);
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
    if (!ReadSaveFile(&scenario, kMagicScenario, SaveVersion(), *reinterpret_cast<const int*>(0x00790268), &file.stream)) {
        ToMainMenu() = 1;
        StartZoneAt(this, nullptr, Text(kMainMenu), 2);
        return 0;
    }
    PartyLoad(kParty, nullptr, &scenario);
    int version = static_cast<int>(scenario.version_);
    if (version >= kScenarioScriptsVersion) {
        ScriptsReset(kScripts);
        InputLoad(kInput, nullptr, &scenario);
        StreamLoadScripts(&scenario, nullptr, kScripts);
        version = static_cast<int>(scenario.version_);
        LoadedScriptsVersion() = version;
    }
    if (version >= kScenarioGlobalsVersion) GlobalsLoad(kGlobals, nullptr, &scenario);
    file.Close();

    // The visited zones of the current map (and bz7g) come back into saves\current; every other
    // zone, and one whose file could not be copied, is marked not visited.
    LocalRecords records;
    PartyStore(kParty, nullptr, records.bytes);
    mfc::CString currentMap;    // [0x1C]
    currentMap = At<mfc::CString>(MapDatabaseFind(kMapDatabase, nullptr, At<const char*>(records.bytes, kRecordsZone)), 4);
    void* variables = records.bytes + kRecordsVariables;
    for (int i = 0; i < ZoneRecordCount(); ++i) {
        std::uint8_t* record = ZoneRecords() + i * kZoneRecordSize;
        bool visited;
        {
            mfc::CString prefix(Text(kVisitedPrefix));
            mfc::CString variable = prefix + At<const char*>(record, 0);
            // FCOMP with 0 and C3: zero or NaN counts as not visited.
            double value = VariableGet(variables, nullptr, variable);
            visited = value < 0.0f || value > 0.0f;
        }
        bool restored = false;
        if (visited) {
            bool keep = gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(At<const char*>(record, 0)),
                                        reinterpret_cast<const unsigned char*>(Text(kAlwaysCopied))) == 0 ||
                        gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(At<const char*>(record, 4)),
                                        reinterpret_cast<const unsigned char*>(static_cast<const char*>(currentMap))) == 0;
            if (keep) {
                zone = At<mfc::CString>(record, 0);
                mfc::CString currentPath = currentFolder + zone;
                mfc::CString currentFile = currentPath + Text(kSaveExtension);
                mfc::CString slotPath = slotFolder + zone;
                mfc::CString slotFile = slotPath + Text(kSaveExtension);
                restored = gameapi::CopyFileA(slotFile, currentFile, FALSE) != 0;
                if (restored) continue;
            }
        }
        mfc::CString prefix(Text(kVisitedPrefix));
        mfc::CString variable = prefix + At<const char*>(record, 0);
        VariableSet(variables, nullptr, variable, 0.0f);
    }
    PartyTake(kParty, nullptr, records.bytes);

    // The zone: kinds 0 and 2 come back from mission.sav, the others start afresh.
    std::uint8_t* zoneRecord = MapDatabaseFind(kMapDatabase, nullptr, At<const char*>(records.bytes, kRecordsZone));
    int kind = At<int>(zoneRecord, kZoneKind);
    if (kind != 0 && kind != 2) {
        StartZoneAt(this, nullptr, At<const char*>(records.bytes, kRecordsZone), 0);
    } else {
        {
            mfc::CString path = slotFolder + Text(kMissionFile);
            opened = file.Open(path, kOpenRead, 1);
        }
        if (!opened) return 0;
        FlowLoadMission(this, nullptr, &file.stream);
        file.Close();
        SetZoneExitsAt(World(), nullptr, zoneRecord + kZoneExits);
    }
    void* console = OstreamPutString(kConsole, nullptr, Text(kLoadedText));
    console = OstreamPutString(console, nullptr, slot);
    OstreamManipulate(OstreamPutChar(console, nullptr, '\n'), nullptr, kFlush);
    return 1;
}

// ---- After starting the sessions (0057EB10) -----------------------------------------------------

namespace {

constexpr std::uintptr_t kDefaultHeroName = 0x0078B0FC;   // "Human Hero"
constexpr std::uintptr_t kDefaultPartyName = 0x007906C4;  // "Bug"
// 0057DEE0 (RET 4). The flow's party setup step (1 here).
__declspec(naked) void __fastcall FlowPartyStep(GameFlow*, void*, int) EI_ORIGINAL(0x0057DEE0)
// 00461540. The named float table's clear, through its address.
__declspec(naked) void __fastcall VariablesClear(NamedFloatTable*) EI_ORIGINAL(0x00461540)
// 00661FB0. The party outside a game (007C82B0) resets its members.
// 00426A80 (RET 8). STLport string assign(first, last) (the second call is inlined in the
// original, the same algorithm).
void __fastcall RangeAssign(void* a0, void*, const char* a2, const char* a3) { StringAssignRangeOriginal(a0, nullptr, a2, a3); }

std::uint8_t* HostRecord() { return *reinterpret_cast<std::uint8_t**>(ClientSession() + 0x2600); }
void AssignText(std::uint8_t* field, const char* text) { RangeAssign(field, nullptr, text, text + std::strlen(text)); }

} // namespace

void GameFlow::AfterServerStart() {
    FlowPartyStep(this, nullptr, 1);
    // The variables of the party outside a game (007C82B0 + 0x54) start empty. Rebirth's
    // PersistentGlobalVars (the CALL at 0057EB25 redirected) keeps the "gvar." ones.
    auto* variables = reinterpret_cast<NamedFloatTable*>(0x007C8304);
    if (ei::rebirth::Enabled(ei::rebirth::Patch::PersistentGlobalVars))
        ClearVariablesKeepingGlobals(variables);
    else
        VariablesClear(variables);
    PartyResetItems(reinterpret_cast<void*>(0x007C82B0));
    // The client's host record gets the hero's and the party's names (or the defaults).
    if (*reinterpret_cast<int*>(0x007C82E4) == 1) {
        char** names = *reinterpret_cast<char***>(0x007C82E0);
        AssignText(HostRecord() + 0x18, names[5]);
        AssignText(HostRecord() + 0xC, names[0]);
    } else {
        AssignText(HostRecord() + 0x18, Text(kDefaultHeroName));
        AssignText(HostRecord() + 0xC, Text(kDefaultPartyName));
    }
    At<int>(HostRecord(), 0x28) = 1;
}

// ---- The client's update from the server (005800C0) ---------------------------------------------

namespace {

constexpr std::uintptr_t kConstructionVariable = 0x00790384;   // "constr.current"
constexpr std::uintptr_t kBriefingSuffixQuest = 0x00790704;    // "q"
constexpr std::uintptr_t kBriefingSuffixK = 0x00790700;        // "k"
constexpr std::uintptr_t kQuestVariablePrefix = 0x00783D7C;    // "q."
constexpr std::uintptr_t kDot = 0x00783D78;                    // "."
// The game types' (client session +0x261C, 0..3) variable set to 1 and briefing name.
constexpr std::uintptr_t kTypeVariables[4] = {0x00790760, 0x00790744, 0x0079072C, 0x00790710};   // "b.smith.constr_1"...
constexpr std::uintptr_t kTypeBriefings[4] = {0x00790758, 0x0079073C, 0x00790724, 0x00790708};   // "intro_1".."intro_4"

unsigned& ServerStamp() { return *reinterpret_cast<unsigned*>(0x007AFED0); }   // the newest variable stamp seen
unsigned& AppliedStamp() { return *reinterpret_cast<unsigned*>(0x007AFED4); }  // the stamp applied up to

// The quest text files (007AFDD0, 12-byte strings) and the briefing texts (007AF788).
__declspec(naked) void __fastcall QuestFilesClear(void*) EI_ORIGINAL(0x00581860)
// Local NamedFloatTable copies: 00581790 an empty one (100 buckets), 00580900 a copy.
__declspec(naked) void __fastcall VariablesConstruct(NamedFloatTable*) EI_ORIGINAL(0x00581790)
__declspec(naked) void __fastcall VariablesCopy(NamedFloatTable*, void*, const NamedFloatTable*) EI_ORIGINAL(0x00580900)
__declspec(naked) void __fastcall VariablesClearAt(NamedFloatTable*) EI_ORIGINAL(0x00461540)
// 00460BC0 with the value as the raw dword the original pushes (no FPU load on the way).
void __fastcall VariableSetBits(void* a0, void*, const char* a2, std::uint32_t a3) { NamedTableSet(a0, nullptr, a2, a3); }
// The flow's methods, through their addresses.
inline void __fastcall FlowLoadZoneQuests(GameFlow*, void*, const char* a2) { FlowLoadQuestsOriginal(a2); }
__declspec(naked) void __fastcall FlowMarkBriefing(GameFlow*, void*, const char*) EI_ORIGINAL(0x0057FF40)
__declspec(naked) void __fastcall FlowRegisterQuests(GameFlow*, void*, const char*) EI_ORIGINAL(0x005810A0)
__declspec(naked) void __fastcall FlowVariableChanged(void*, void*, const void*) EI_ORIGINAL(0x00580840)
__declspec(naked) void __fastcall FlowUpdateMultiplayerVariable(GameFlow*, void*) EI_ORIGINAL(0x0057F190)
__declspec(naked) mfc::CString* __stdcall BriefingVariableAt(mfc::CString*, const char*) EI_ORIGINAL(0x0057FDE0)
// CString members.
struct TextOpsAt : mfc::CString {
    int Find(const char* text) const;      // 006FDBD9
};
__declspec(naked) int TextOpsAt::Find(const char*) const EI_ORIGINAL(0x006FDBD9)
const TextOpsAt& Ops(const mfc::CString& s) { return static_cast<const TextOpsAt&>(s); }
// The vector of changed variables (STLport vector<node*>): its allocator tag constructor
// (00584720), insert overflow (00587700: position, value, count) and storage release (00584A90:
// ECX storage, EDX capacity); the stamp sort (00589160 introsort with ECX first, EDX last, then a
// dummy, the depth limit and the comparator; 00589240 the final insertion sort).
struct NodeVector {
    void** start;
    void** finish;
    void** eos;
};
__declspec(naked) void __fastcall NodeVectorConstruct(NodeVector*, void*, const void*) EI_ORIGINAL(0x00584720)
__declspec(naked) void __fastcall NodeVectorInsertOverflow(NodeVector*, void*, void**, void* const*, unsigned)
    EI_ORIGINAL(0x00587700)
__declspec(naked) void __fastcall NodeVectorFree(void**, unsigned) EI_ORIGINAL(0x00584A90)
__declspec(naked) void __fastcall StampIntroSort(void**, void**, int, int, const void*) EI_ORIGINAL(0x00589160)
__declspec(naked) void __fastcall StampInsertionSort(void**, void**, const void*) EI_ORIGINAL(0x00589240)

// A local NamedFloatTable released the way the original's inlined destructor does.
struct LocalVariables {
    NamedFloatTable table;
    ~LocalVariables() {
        std::size_t n = static_cast<std::size_t>(table.finish - table.buckets);
        for (std::size_t b = 0; b < n; ++b) {
            NamedFloatTable::Node* node = table.buckets[b];
            while (node) {
                NamedFloatTable::Node* next = node->next;
                reinterpret_cast<mfc::CString*>(&node->key)->~CString();
                stlp::deallocate(node, sizeof(NamedFloatTable::Node));
                node = next;
            }
            table.buckets[b] = nullptr;
        }
        table.count = 0;
        if (table.buckets)
            stlp::deallocate(table.buckets, static_cast<std::size_t>(table.endOfStorage - table.buckets) * sizeof(void*));
    }
};

// The server's variable list (client session +0x25FC, a list with a sentinel head; node +8 the
// record: name +0x0C, value +0x18, stamp +0x1C).
std::uint8_t* VariableList() { return At<std::uint8_t*>(ClientSession(), 0x25FC); }
std::uint8_t* NodeRecord(std::uint8_t* node) { return At<std::uint8_t*>(node, 8); }
unsigned NodeStamp(std::uint8_t* node) { return At<unsigned>(NodeRecord(node), 0x1C); }

} // namespace

void GameFlow::UpdateFromServer() {
    std::uint8_t* client = ClientSession();
    if (!client || (At<std::uint32_t>(client, 4) & kSessionInactive)) return;
    if (void* texts = *reinterpret_cast<void**>(0x007AF788)) {
        auto* files = reinterpret_cast<std::uint8_t**>(0x007AFDD0);
        for (std::uint8_t* file = files[0]; file != files[1]; file += 0xC)
            TextsUnload(texts, nullptr, *reinterpret_cast<const char**>(file));
        QuestFilesClear(files);
    }
    auto* clientWorld = reinterpret_cast<void*>(0x007AF5B8);
    std::uint8_t* player = At<std::uint8_t*>(ClientWorldLocalView(clientWorld), 0x1C);
    LocalVariables unusedEmpty;
    VariablesConstruct(&unusedEmpty.table);
    auto* variables = reinterpret_cast<NamedFloatTable*>(player + 0xC4);
    // The local player's variables are replaced by the server's; "constr.current" survives.
    // Rebirth's PersistentGlobalVars (the CALL at 00580177 redirected) keeps the "gvar." ones.
    float construction = static_cast<float>(VariableGet(variables, nullptr, Text(kConstructionVariable)));
    LocalVariables unusedCopy;
    VariablesCopy(&unusedCopy.table, nullptr, variables);
    if (ei::rebirth::Enabled(ei::rebirth::Patch::PersistentGlobalVars))
        ClearVariablesKeepingGlobals(variables);
    else
        VariablesClearAt(variables);
    VariableSet(variables, nullptr, Text(kConstructionVariable), construction);
    if (std::uint8_t* zone = At<std::uint8_t*>(ClientSession(), 0x25F4)) {
        if (!(At<std::uint32_t>(zone, 4) & kSessionInactive)) FlowLoadZoneQuests(this, nullptr, At<const char*>(zone, 0xC));
    }

    // The game type's construction variable and briefings, at least 1.
    mfc::CString briefing;
    unsigned type = At<unsigned>(ClientSession(), 0x261C);
    if (type <= 3) {
        VariableSet(variables, nullptr, Text(kTypeVariables[type]), 1.0f);
        briefing = Text(kTypeBriefings[type]);
    }
    alignas(mfc::CString) std::uint8_t questSlot[sizeof(mfc::CString)];
    alignas(mfc::CString) std::uint8_t kSlot[sizeof(mfc::CString)];
    BriefingVariableAt(reinterpret_cast<mfc::CString*>(questSlot), briefing + Text(kBriefingSuffixQuest));
    BriefingVariableAt(reinterpret_cast<mfc::CString*>(kSlot), briefing + Text(kBriefingSuffixK));
    mfc::CString& questVariable = *reinterpret_cast<mfc::CString*>(questSlot);
    mfc::CString& kVariable = *reinterpret_cast<mfc::CString*>(kSlot);
    mfc::CString* const names[2] = {&questVariable, &kVariable};
    for (mfc::CString* name : names) {
        float value = static_cast<float>(VariableGet(variables, nullptr, *name));
        // FCOMP 1.0 with the value, C0 selecting it: the larger (a NaN kept).
        std::uint32_t bits;
        std::memcpy(&bits, &value, 4);
        bool keep = (bits & 0x7F800000u) == 0x7F800000u && (bits & 0x007FFFFFu) != 0 ? true : 1.0f < value;
        VariableSet(variables, nullptr, *name, keep ? value : 1.0f);
    }

    // The briefings the server says were seen.
    for (std::uint8_t* node = *At<std::uint8_t**>(ClientSession(), 0x25F8); node != At<std::uint8_t*>(ClientSession(), 0x25F8);
         node = *reinterpret_cast<std::uint8_t**>(node))
        FlowMarkBriefing(this, nullptr, At<const char*>(At<std::uint8_t*>(node, 8), 0xC));
    // The quest variables ("q.<folder>.<name>") register their quest folders.
    for (std::uint8_t* node = *reinterpret_cast<std::uint8_t**>(VariableList()); node != VariableList();
         node = *reinterpret_cast<std::uint8_t**>(node)) {
        mfc::CString name(At<const char*>(NodeRecord(node), 0xC));
        bool quest;
        {
            mfc::CString prefix = name.Left(2);
            quest = gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(static_cast<const char*>(prefix)),
                                    reinterpret_cast<const unsigned char*>(Text(kQuestVariablePrefix))) == 0;
        }
        if (!quest) continue;
        mfc::CString rest = Ops(name).Mid(console::Length(name) - 2);
        int dot = Ops(rest).Find(Text(kDot));
        if (dot > 0) {
            mfc::CString folder = rest.Left(dot);
            FlowRegisterQuests(this, nullptr, folder);
        }
    }

    // The server's variables, by stamp.
    if (At<int>(At<std::uint8_t*>(ClientWorldLocalView(clientWorld), 0x1C), 0x100) == 1) {
        unsigned previous = AppliedStamp();
        if (ServerStamp() > AppliedStamp() + 2) AppliedStamp() = ServerStamp() + 1;
        NodeVector changed;
        char allocator;
        NodeVectorConstruct(&changed, nullptr, &allocator);
        for (std::uint8_t* node = *reinterpret_cast<std::uint8_t**>(VariableList()); node != VariableList();
             node = *reinterpret_cast<std::uint8_t**>(node)) {
            if (ServerStamp() <= NodeStamp(node)) ServerStamp() = NodeStamp(node);
            unsigned stamp = NodeStamp(node);
            if (stamp >= AppliedStamp() || stamp < previous) continue;
            if (changed.finish != changed.eos) {
                if (changed.finish) *changed.finish = node;
                ++changed.finish;
            } else {
                void* value = node;
                NodeVectorInsertOverflow(&changed, nullptr, changed.finish, &value, 1);
            }
        }
        if (changed.start != changed.finish) {
            int depth = 0;
            for (int n = static_cast<int>(changed.finish - changed.start); n != 1; n >>= 1) ++depth;
            StampIntroSort(changed.start, changed.finish, 0, depth * 2, variables);
            StampInsertionSort(changed.start, changed.finish, variables);
        }
        for (void** it = changed.start; it != changed.finish; ++it) FlowVariableChanged(*it, nullptr, *it);
        bool settled;
        do {
            settled = true;
            for (std::uint8_t* node = *reinterpret_cast<std::uint8_t**>(VariableList()); node != VariableList();
                 node = *reinterpret_cast<std::uint8_t**>(node)) {
                if (NodeStamp(node) > AppliedStamp()) continue;
                if (NodeStamp(node) == AppliedStamp()) {
                    FlowVariableChanged(node, nullptr, node);
                    ++AppliedStamp();
                    settled = false;
                }
                std::uint8_t* record = NodeRecord(node);
                VariableSetBits(variables, nullptr, At<const char*>(record, 0xC), At<std::uint32_t>(record, 0x18));
            }
        } while (!settled);
        NodeVectorFree(changed.start, static_cast<unsigned>(changed.eos - changed.start));
    } else {
        for (std::uint8_t* node = *reinterpret_cast<std::uint8_t**>(VariableList()); node != VariableList();
             node = *reinterpret_cast<std::uint8_t**>(node)) {
            if (ServerStamp() <= NodeStamp(node)) ServerStamp() = NodeStamp(node);
            std::uint8_t* record = NodeRecord(node);
            mfc::CString name(At<const char*>(record, 0xC));
            VariableSetBits(variables, nullptr, At<const char*>(record, 0xC), At<std::uint32_t>(record, 0x18));
        }
        AppliedStamp() = 0;
    }
    FlowUpdateMultiplayerVariable(this, nullptr);
    kVariable.~CString();
    questVariable.~CString();
}

// ---- The saved multiplayer parties (00662FB0) ---------------------------------------------------

namespace {

constexpr std::uintptr_t kPartyFilePattern = 0x00794C88;   // "\*.mp"
constexpr std::size_t kPartyEntrySize = 0x74;              // the party records (0x70) and the file name
// The vector's element copy loop (ECX first, EDX last, destination on the stack; returns the end),
// element destructor (00664AE0, flag 0), the records' copy constructor (006613C0) and the
// insert overflow (00619920: position, value, count).
__declspec(naked) std::uint8_t* __fastcall PartyEntriesCopy(std::uint8_t*, std::uint8_t*, std::uint8_t*) EI_ORIGINAL(0x00664410)
__declspec(naked) void __fastcall PartyEntryDestroy(std::uint8_t*, void*, int) EI_ORIGINAL(0x00664AE0)
__declspec(naked) void __fastcall PartyRecordsCopy(std::uint8_t*, void*, const std::uint8_t*) EI_ORIGINAL(0x006613C0)
__declspec(naked) void __fastcall PartyEntriesInsertOverflow(void*, void*, std::uint8_t*, const std::uint8_t*, unsigned)
    EI_ORIGINAL(0x00619920)
// 00662DD0 (RET 4): the records read a party file from `folder` (their +0x68 names the file).
__declspec(naked) bool __fastcall PartyRecordsLoad(void*, void*, const char*) EI_ORIGINAL(0x00662DD0)
// The records' destructor pieces, as 00662FB0 inlines it: 00661700 (Reset(1, 1)), 00661640, the
// zone name, the variables (00663330), the array at +0x40 (006643F0), 00664160 (+0x2C), the base
// (00455930).
inline void __fastcall RecordsPart661700(void* a0, void*, int a2, int a3) { PartyResetOriginal((void*)a0, nullptr, a2, a3); }
__declspec(naked) void __fastcall RecordsPart661640(void*) EI_ORIGINAL(0x00661640)
__declspec(naked) void __fastcall RecordsVariablesDestroy(void*) EI_ORIGINAL(0x00663330)
__declspec(naked) void __fastcall RecordsArrayDestroy(void*) EI_ORIGINAL(0x006643F0)
__declspec(naked) void __fastcall RecordsPart664160(void*) EI_ORIGINAL(0x00664160)
__declspec(naked) void __fastcall RecordsBaseDestroy(void*) EI_ORIGINAL(0x00455930)
// 004616A0 (RET 4): the variables table frees one node (what the original's inlined clear does).
__declspec(naked) void __fastcall VariablesFreeNode(NamedFloatTable*, void*, NamedFloatTable::Node*) EI_ORIGINAL(0x004616A0)
constexpr std::uintptr_t kRecordsVtable = 0x00743000;

void DestroyPartyEntry(std::uint8_t* entry) {
    reinterpret_cast<mfc::CString*>(entry + 0x70)->~CString();
    *reinterpret_cast<std::uintptr_t*>(entry) = kRecordsVtable;
    RecordsPart661700(entry, nullptr, 1, 1);
    RecordsPart661640(entry);
    reinterpret_cast<mfc::CString*>(entry + 0x68)->~CString();
    RecordsVariablesDestroy(entry + 0x54);
    RecordsArrayDestroy(entry + 0x40);
    RecordsPart664160(entry + 0x2C);
    RecordsBaseDestroy(entry);
}

} // namespace

void SavedPartyList::Reload() {
    auto* self = reinterpret_cast<std::uint8_t*>(this);
    // Empty the list (+4: an STLport vector of 0x74-byte entries).
    auto*& start = *reinterpret_cast<std::uint8_t**>(self + 4);
    auto*& finish = *reinterpret_cast<std::uint8_t**>(self + 8);
    std::uint8_t* end = PartyEntriesCopy(finish, finish, start);
    for (std::uint8_t* entry = end; entry != finish; entry += kPartyEntrySize) PartyEntryDestroy(entry, nullptr, 0);
    finish = end;

    WIN32_FIND_DATAA found;
    HANDLE search;
    {
        mfc::CString pattern = *reinterpret_cast<const mfc::CString*>(self) + reinterpret_cast<const char*>(kPartyFilePattern);
        search = gameapi::FindFirstFileA(pattern, &found);
    }
    if (search == INVALID_HANDLE_VALUE) return;
    do {
        // The party records and the file name, side by side as the list's entries are.
        alignas(4) std::uint8_t entry[kPartyEntrySize];
        RecordsConstruct(entry, nullptr);
        new (entry + 0x70) mfc::CString();
        *reinterpret_cast<mfc::CString*>(entry + 0x70) = found.cFileName;
        if (PartyRecordsLoad(entry, nullptr, *reinterpret_cast<const char* const*>(self))) {
            auto* variables = reinterpret_cast<NamedFloatTable*>(entry + 0x54);
            if (ei::rebirth::Enabled(ei::rebirth::Patch::PersistentGlobalVars)) {
                // Rebirth's PersistentGlobalVars: the JNE at 00663083 skips the inlined clear and
                // the hook at 00663170 (whose instruction, count = 0, is dropped) clears the table
                // keeping its "gvar." entries.
                ClearVariablesKeepingGlobals(variables);
            } else {
                for (std::size_t b = 0; b < static_cast<std::size_t>(variables->finish - variables->buckets); ++b) {
                    NamedFloatTable::Node* node = variables->buckets[b];
                    while (node) {
                        NamedFloatTable::Node* next = node->next;
                        VariablesFreeNode(variables, nullptr, node);
                        node = next;
                    }
                    variables->buckets[b] = nullptr;
                }
                variables->count = 0;
            }
            if (*reinterpret_cast<int*>(entry + 0x34) == 1) {
                auto* eos = *reinterpret_cast<std::uint8_t**>(self + 0xC);
                if (finish != eos) {
                    if (finish) {
                        PartyRecordsCopy(finish, nullptr, entry);
                        new (finish + 0x70) mfc::CString(*reinterpret_cast<const mfc::CString*>(entry + 0x70));
                    }
                    finish += kPartyEntrySize;
                } else {
                    PartyEntriesInsertOverflow(self + 4, nullptr, finish, entry, 1);
                }
            }
        }
        DestroyPartyEntry(entry);
    } while (gameapi::FindNextFileA(search, &found));
    gameapi::FindClose(search);
}

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
EI_REPLACE_METHOD(0x005816C0, GameFlow::RequestState3);
EI_REPLACE_METHOD(0x005816D0, GameFlow::RequestLoad);
EI_REPLACE_METHOD(0x00581700, GameFlow::RequestZoneChange);
EI_REPLACE_METHOD(0x00581730, GameFlow::RequestPartyZone5);
EI_REPLACE_METHOD(0x00581760, GameFlow::RequestPartyZone6);
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
EI_REPLACE_METHOD(0x00662FB0, SavedPartyList::Reload);
EI_REBIRTH_IMPLEMENTS(0x00662FB0, ei::rebirth::Patch::PersistentGlobalVars);
EI_REBIRTH_IMPLEMENTS(0x005800C0, ei::rebirth::Patch::PersistentGlobalVars);
EI_REBIRTH_IMPLEMENTS(0x0057EB10, ei::rebirth::Patch::PersistentGlobalVars);
EI_REBIRTH_IMPLEMENTS(0x0057ABA0, ei::rebirth::Patch::AutoExec);
EI_REBIRTH_IMPLEMENTS(0x0057C700, ei::rebirth::Patch::AutoExec);
EI_REBIRTH_IMPLEMENTS(0x0057BD10, ei::rebirth::Patch::AutoExec);
EI_REBIRTH_IMPLEMENTS(0x0057C460, ei::rebirth::Patch::AllowScriptsInMainMenu);
