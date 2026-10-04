// The game flow object (007AFE20; the state machine 0057C700 runs on it, Rebirth patches that one):
// saving a game into saves\<slot>, entering a zone. See docs/notes/world.md (seventh round).
#include "world.h"

#include "core_maths.h"
#include "core_streams.h"
#include "object.h"

#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"

#include "ei/hook.h"
#include "ei/vcall.h"

#include <cstring>
#include <new>

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

constexpr std::uintptr_t kFileStreamVtable = 0x0073C8D8;   // a CStream over an MFC CFile (+0x0C)
constexpr unsigned kOpenWrite = 0x1001;                    // CFile::modeCreate | modeWrite
constexpr unsigned kOpenRead = 0;
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

// A CStream writing to a file (0x1C bytes: CStream header, then an MFC CFile at +0x0C).
struct FileStream {
    void* vtable;
    std::uint8_t unknown_04;
    std::uint8_t ok;              // +0x05
    std::uint8_t unknown_06[2];
    std::uint32_t limit;          // +0x08
    alignas(4) std::uint8_t file[sizeof(mfc::CFile)];   // +0x0C
};
static_assert(sizeof(FileStream) == 0x1C);

// ---- Callees not rewritten (EI_ORIGINAL forwarders; __thiscall called as __fastcall) ----------

// 005E5200 (RET 4). The renderer's busy state (3 while saving, 0 after).
__declspec(naked) void __fastcall RendererSetBusy(void*, void*, int) EI_ORIGINAL(0x005E5200)
// 0057AAB0. Creates the folder `path` (ECX) and deletes the files in it.
__declspec(naked) void __fastcall ClearSaveFolderAt(const char* /*path*/) EI_ORIGINAL(0x0057AAB0)
// 00594180 (RET 4). CWorldServer::GetPlayer (world_server.cpp), through its address.
__declspec(naked) WorldPlayer* __fastcall WorldGetPlayer(void*, void*, int) EI_ORIGINAL(0x00594180)
// 004A8890 / 004A8880 (RET 4). The party (007AA5A0): take the records from / give them to a record
// builder (00661050 constructs one, 006611D0 destroys it).
__declspec(naked) void __fastcall PartyTake(void*, void*, void* /*records*/) EI_ORIGINAL(0x004A8890)
__declspec(naked) void __fastcall PartyStore(void*, void*, void* /*records*/) EI_ORIGINAL(0x004A8880)
__declspec(naked) void __fastcall RecordsConstruct(void*, void*) EI_ORIGINAL(0x00661050)
__declspec(naked) void __fastcall RecordsDestruct(void*, void*) EI_ORIGINAL(0x006611D0)
// 0045E850 (RET 4). CMapDatabase::FindMap: the zone record named `name`, or null.
__declspec(naked) std::uint8_t* __fastcall MapDatabaseFind(void*, void*, const char*) EI_ORIGINAL(0x0045E850)
// 00460AF0 / 00460BC0. Named float table Get (ST0) / Set (RET 8).
__declspec(naked) double __fastcall VariableGet(void*, void*, const char*) EI_ORIGINAL(0x00460AF0)
__declspec(naked) void __fastcall VariableSet(void*, void*, const char*, float) EI_ORIGINAL(0x00460BC0)
// 0057C460 (RET 8). Starts zone `zone` (mode 2: the main menu). Patched by Rebirth: original address.
__declspec(naked) void __fastcall StartZone(GameFlow*, void*, const char*, int) EI_ORIGINAL(0x0057C460)
// The scenario.sav parts: the party (004A8560), the input state (005F85C0 save, 005F86D0), the
// scripts (004A54D0 reset, 004A5250 save into a stream), the globals (00410C10).
__declspec(naked) void __fastcall PartySave(void*, void*, CMemoryStream*) EI_ORIGINAL(0x004A8560)
__declspec(naked) void __fastcall InputSave(void*, void*, CMemoryStream*) EI_ORIGINAL(0x005F85C0)
__declspec(naked) void __fastcall InputAfterSave(void*, void*) EI_ORIGINAL(0x005F86D0)
__declspec(naked) void __fastcall ScriptsReset(void*, void*, int) EI_ORIGINAL(0x004A54D0)
__declspec(naked) void __fastcall StreamSaveScripts(CMemoryStream*, void*, void* /*scripts*/) EI_ORIGINAL(0x004A5250)
__declspec(naked) void __fastcall GlobalsSave(void*, void*, CMemoryStream*) EI_ORIGINAL(0x00410C10)
// 00579B80 (RET 8). Writes `data` to the file stream (ECX) under a header (magic in EDX, version).
__declspec(naked) void __fastcall WriteSaveFile(FileStream*, unsigned /*magic*/, int /*version*/, CMemoryStream*)
    EI_ORIGINAL(0x00579B80)
// 00597BD0. CWorldServer::SaveWorld (world_save.cpp), through its address.
__declspec(naked) void __fastcall SaveWorldAt(void*, void*, CMemoryStream*) EI_ORIGINAL(0x00597BD0)
// The console's ostream.
__declspec(naked) void* __fastcall OstreamPutString(void*, void*, const char*) EI_ORIGINAL(0x006FC31D)
__declspec(naked) void* __fastcall OstreamPutChar(void*, void*, char) EI_ORIGINAL(0x006FC0B0)
__declspec(naked) void* __fastcall OstreamManipulate(void*, void*, void*) EI_ORIGINAL(0x00434FE0)

constexpr int kSlotFileOpen = 8;     // CFile slot 8 (+0x20): Open(path, flags, error), on the stream's CFile (+0x0C)
constexpr int kSlotFileClose = 19;    // CFile slot 19 (+0x4C): Close

// The record builder of the party (0x70 bytes; the variables table at +0x54, the zone at +0x68).
constexpr std::size_t kRecordsSize = 0x70;
constexpr int kRecordsVariables = 0x54;
constexpr int kRecordsZone = 0x68;

struct LocalFileStream {
    FileStream stream;
    LocalFileStream() {
        std::memset(&stream, 0, sizeof(stream));
        stream.vtable = reinterpret_cast<void*>(kStreamVtable);
        stream.limit = kStreamLimit;
        stream.ok = 1;
        new (stream.file) mfc::CFile();
        stream.vtable = reinterpret_cast<void*>(kFileStreamVtable);
    }
    ~LocalFileStream() {
        stream.vtable = reinterpret_cast<void*>(kFileStreamVtable);
        reinterpret_cast<mfc::CFile*>(stream.file)->~CFile();
    }
    LocalFileStream(const LocalFileStream&) = delete;
    LocalFileStream& operator=(const LocalFileStream&) = delete;

    // Opens `path` (`reading`: the stream's +4 byte, 1 to read, 0 to write). The original calls
    // the embedded CFile's virtual Open (its vtable at stream +0x0C, slot 8), not the stream's.
    bool Open(const char* path, unsigned flags, std::uint8_t reading) {
        stream.unknown_04 = reading;
        stream.ok = ei::VCall<int>(stream.file, kSlotFileOpen, path, flags, nullptr) != 0;
        return stream.ok != 0;
    }
    void Close() { ei::VCall<void>(stream.file, kSlotFileClose); }
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
        if (WorldPlayer* player = WorldGetPlayer(reinterpret_cast<void*>(0x007AFF60), nullptr, 0))
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
        StartZone(this, nullptr, Text(kMainMenu), 2);
        return 0;
    }

    // scenario.sav: the party, the input state, the scripts and the globals.
    CMemoryStream scenario;
    std::memset(&scenario, 0, sizeof(scenario));
    scenario.vtable_ = reinterpret_cast<void*>(kMemoryStreamVtable);
    scenario.data_ = nullptr;
    scenario.unknown_1C = 0;
    scenario.position_ = 0;
    scenario.size_ = 0;
    scenario.ok_ = 1;
    scenario.growBy_ = 0x80;
    scenario.unknown_14 = -1;
    scenario.unknown_8 = static_cast<std::uint32_t>(SaveVersion());
    struct ScenarioOwner {   // frees the buffer on the way out (CMemoryStream's inlined destructor)
        CMemoryStream* s;
        ~ScenarioOwner() {
            s->vtable_ = reinterpret_cast<void*>(kMemoryStreamVtable);
            if (s->data_) gamecrt::operator_delete(s->data_);
            s->vtable_ = reinterpret_cast<void*>(kStreamVtable);
        }
    } scenarioOwner{&scenario};
    PartySave(kParty, nullptr, &scenario);
    InputSave(kInput, nullptr, &scenario);
    ScriptsReset(kScripts, nullptr, 0);
    InputAfterSave(kInput, nullptr);
    StreamSaveScripts(&scenario, nullptr, kScripts);
    ScriptsReset(kScripts, nullptr, 0);
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
        mission.unknown_8 = kStreamLimit;
        mission.ok_ = 1;
        mission.vtable_ = reinterpret_cast<void*>(kMemoryStreamVtable);
        mission.data_ = nullptr;
        mission.Reset();
        mission.unknown_8 = static_cast<std::uint32_t>(SaveVersion());
        SaveWorldAt(reinterpret_cast<void*>(0x007AFF60), nullptr, &mission);
        WriteSaveFile(&file.stream, kMagicMission, SaveVersion(), &mission);
        mission.vtable_ = reinterpret_cast<void*>(kMemoryStreamVtable);
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
const char* EmptyText() { return reinterpret_cast<const char*>(0x0079B298); }

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
__declspec(naked) void __fastcall PlayMovieFile(const char*, int, int, int, int, int, int) EI_ORIGINAL(0x005DB3F0)
// 00460DB0 (RET 4). Named float table Remove.
__declspec(naked) void __fastcall VariableRemove(void*, void*, const char*) EI_ORIGINAL(0x00460DB0)
// 00579830 (RET 0xC). Reads a save file through the file stream into `out` (ECX), checking the
// header (magic in EDX, version, minimum version); false when it is corrupt.
__declspec(naked) bool __fastcall ReadSaveFile(CMemoryStream* /*out*/, unsigned /*magic*/, int /*version*/,
                                               int /*minimum*/, FileStream*) EI_ORIGINAL(0x00579830)
// 00596E70 / 0058D3D0 / 0058DCD0 / 005915E0. CWorldServer::LoadSave, WakeUnitsAround, Tick and
// SetZoneExits, through their addresses (rewritten in world_save / world_tick / world_zone).
__declspec(naked) int __fastcall LoadSaveAt(CWorldServer*, void*, CMemoryStream*, const char*) EI_ORIGINAL(0x00596E70)
__declspec(naked) void __fastcall WakeUnitsAroundAt(CWorldServer*, void*, const float*, std::uint32_t /*radius*/)
    EI_ORIGINAL(0x0058D3D0)
__declspec(naked) void __fastcall TickAt(CWorldServer*, void*, int /*force*/) EI_ORIGINAL(0x0058DCD0)
__declspec(naked) void __fastcall SetZoneExitsAt(CWorldServer*, void*, const void*) EI_ORIGINAL(0x005915E0)
// 00595480 (RET 0xC). CWorldServer::NewZone(map, mob, zone). Patched by Rebirth: original address.
__declspec(naked) int __fastcall NewZone(CWorldServer*, void*, const char*, const char*, const char*) EI_ORIGINAL(0x00595480)
// 0058EEA0 (RET 4). CWorldServer::SetTime.
__declspec(naked) void __fastcall WorldSetTime(CWorldServer*, void*, std::uint32_t /*float bits*/) EI_ORIGINAL(0x0058EEA0)
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
    data.unknown_8 = kStreamLimit;
    data.ok_ = 1;
    data.vtable_ = reinterpret_cast<void*>(kMemoryStreamVtable);
    data.data_ = nullptr;
    data.Reset();
    struct DataOwner {
        CMemoryStream* s;
        ~DataOwner() {
            s->vtable_ = reinterpret_cast<void*>(kMemoryStreamVtable);
            if (s->data_) gamecrt::operator_delete(s->data_);
            s->vtable_ = reinterpret_cast<void*>(kStreamVtable);
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
                        reinterpret_cast<const unsigned char*>(EmptyText())) != 0) {
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
__declspec(naked) int __stdcall GetHostName(char*, int) EI_ORIGINAL(0x006E6100)
__declspec(naked) void __fastcall AddressInit(void*, void*, const char* /*host*/, int /*port*/) EI_ORIGINAL(0x00440440)
__declspec(naked) const char* const* __fastcall AddressFormat(void*, void*, void* /*out string*/) EI_ORIGINAL(0x00435010)
__declspec(naked) void __fastcall RawStringDestruct(void*, void*) EI_ORIGINAL(0x00425040)
__declspec(naked) void __fastcall HostRecordConstruct(void*, void*) EI_ORIGINAL(0x004186E0)
__declspec(naked) void __fastcall HostStringConstruct(void*, void*) EI_ORIGINAL(0x0041F460)
__declspec(naked) void __fastcall CountedDestroy(void* /*slot*/, void*) EI_ORIGINAL(0x00413270)
__declspec(naked) void __fastcall CountedShare(void* /*slot*/, void*, void*) EI_ORIGINAL(0x00418BE0)
__declspec(naked) void __fastcall HostStringAssign(void*, void*, const char* /*first*/, const char* /*last*/)
    EI_ORIGINAL(0x00426A80)
__declspec(naked) void __fastcall ServerSendText(void*, void*, int /*message*/, const char*) EI_ORIGINAL(0x0041A500)

std::uint8_t* GameServer() { return *reinterpret_cast<std::uint8_t**>(0x0079B5DC); }

// Tells the clients of the game server where the game is: the host's address and the zone.
void AnnounceHost(const char* zone) {
    char host[100];
    if (GetHostName(host, sizeof(host)) != 0) return;
    std::uint32_t address[4] = {0, 0, 0, 0};
    AddressInit(address, nullptr, host, kHostPort);
    alignas(4) std::uint8_t text[12];
    mfc::CString formatted(*AddressFormat(address, nullptr, text));
    RawStringDestruct(text, nullptr);
    void* memory = gamecrt::operator_new(0x24);
    At<std::uint32_t>(memory, 4) = 0;   // written before the null test
    void* record = nullptr;
    if (memory) {
        HostRecordConstruct(memory, nullptr);
        HostStringConstruct(static_cast<std::uint8_t*>(memory) + 0xC, nullptr);
        HostStringConstruct(static_cast<std::uint8_t*>(memory) + 0x18, nullptr);
        *static_cast<void**>(memory) = reinterpret_cast<void*>(kHostRecordVtable);
        record = memory;
    }
    void* slot = GameServer() + 0x25A0;
    if (record != *static_cast<void**>(slot)) {
        CountedDestroy(slot, nullptr);
        CountedShare(slot, nullptr, record);
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
        data.unknown_8 = kStreamLimit;
        data.ok_ = 1;
        data.vtable_ = reinterpret_cast<void*>(kMemoryStreamVtable);
        data.data_ = nullptr;
        data.Reset();
        bool loaded = ReadSaveFile(&data, kMagicMission, SaveVersion(), *reinterpret_cast<const int*>(0x00790268),
                                   &file.stream) &&
                      LoadSaveAt(World(), nullptr, &data, nullptr) != 0;
        if (loaded) file.Close();
        data.vtable_ = reinterpret_cast<void*>(kMemoryStreamVtable);
        if (data.data_) gamecrt::operator_delete(data.data_);
        if (!loaded) {
            data.vtable_ = reinterpret_cast<void*>(kStreamVtable);
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
__declspec(naked) void __fastcall VariableSetAt(void*, void*, const char*, float) EI_ORIGINAL(0x00460BC0)

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
    data.unknown_8 = kStreamLimit;
    data.ok_ = 1;
    data.vtable_ = reinterpret_cast<void*>(kMemoryStreamVtable);
    data.data_ = nullptr;
    data.Reset();
    data.unknown_8 = static_cast<std::uint32_t>(SaveVersion());
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
    data.vtable_ = reinterpret_cast<void*>(kMemoryStreamVtable);
    if (data.data_) gamecrt::operator_delete(data.data_);
    data.vtable_ = reinterpret_cast<void*>(kStreamVtable);
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
__declspec(naked) void __fastcall PartyCopyFrom(void*, void*, const void*) EI_ORIGINAL(0x00661300)
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
constexpr int kSlotStreamCopyFrom = 7;                    // memory stream slot 7 (+0x1C): read a whole stream

// 00595BA0 (RET 8, patched by Rebirth). The full path of a map file, constructed into `out`.
__declspec(naked) mfc::CString* __fastcall MapPathInto(CWorldServer*, void*, void* /*out*/, const char*) EI_ORIGINAL(0x00595BA0)
// 004A4A90 (RET 8, AL). Finds the chunk `id` in a stream and reads it into `out`.
__declspec(naked) bool __fastcall StreamReadChunk(void*, void*, std::uint32_t, void* /*out*/) EI_ORIGINAL(0x004A4A90)
// 006FDBB7 (RET 4). CString::ReverseFind(char).
__declspec(naked) int __fastcall StringReverseFind(const mfc::CString*, void*, char) EI_ORIGINAL(0x006FDBB7)
__declspec(naked) mfc::CString* __fastcall StringLeftInto(const mfc::CString*, void*, void* /*out*/, int) EI_ORIGINAL(0x006FDAE9)

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
        alignas(mfc::CString) std::uint8_t left[sizeof(mfc::CString)];
        mfc::CString* cut = StringLeftInto(base, nullptr, left, dot);
        *base = *cut;
        cut->~CString();
    }
    {
        LocalFileStream graph;
        bool opened;
        {
            mfc::CString path = *base + Text(kGraphExtension);
            opened = graph.Open(path, kOpenRead, 1);
        }
        if (opened) {
            ei::VCall<void>(out, kSlotStreamCopyFrom, &graph.stream);
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
// 004A5490 (RET 4). CMemoryStream::Seek. 00468C20: Reset.
__declspec(naked) void __fastcall MemorySeek(CMemoryStream*, void*, std::uint32_t) EI_ORIGINAL(0x004A5490)
__declspec(naked) void __fastcall MemoryReset(CMemoryStream*, void*) EI_ORIGINAL(0x00468C20)
// 004BE6E0 / 004D23A0 (RET 8). A script command queue: reset, add a line.
__declspec(naked) void __fastcall QueueReset(void*, void*) EI_ORIGINAL(0x004BE6E0)
__declspec(naked) void __fastcall QueueAdd(void*, void*, const mfc::CString*, int) EI_ORIGINAL(0x004D23A0)

// A CMemoryStream on the stack, as the original builds it (limit, ok, no data, Reset) and frees it.
struct LocalMemoryStream {
    CMemoryStream s;
    LocalMemoryStream() {
        std::memset(&s, 0, sizeof(s));
        s.unknown_8 = kStreamLimit;
        s.ok_ = 1;
        s.vtable_ = reinterpret_cast<void*>(kMemoryStreamVtable);
        s.data_ = nullptr;
        MemoryReset(&s, nullptr);
    }
    ~LocalMemoryStream() {
        s.vtable_ = reinterpret_cast<void*>(kMemoryStreamVtable);
        if (s.data_) gamecrt::operator_delete(s.data_);
        s.vtable_ = reinterpret_cast<void*>(kStreamVtable);
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
        MemorySeek(&text.s, nullptr, text.s.size_);
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
__declspec(naked) void __fastcall HandleAssign(void*, void*, void*) EI_ORIGINAL(0x00418BE0)
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
                HandleAssign(handle, nullptr, nullptr);
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
    WorldPlayer* player = WorldGetPlayer(reinterpret_cast<void*>(0x007AFF60), nullptr, 0);
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
__declspec(naked) void __fastcall AddressSet(void*, void*, const char*, int) EI_ORIGINAL(0x00440440)
// 00436C60 (RET 8). A buffer takes bytes.
__declspec(naked) void __fastcall BufferPut(GameBuffer*, void*, const void*, int) EI_ORIGINAL(0x00436C60)
// 00442980 (RET 8). The client connects to an address with a hello buffer.
__declspec(naked) void __fastcall ClientConnect(void*, void*, void*, GameBuffer*) EI_ORIGINAL(0x00442980)
// 00426A80 (RET 8). STLport string assign [first, last).
__declspec(naked) void __fastcall StringAssignRange(void*, void*, const char*, const char*) EI_ORIGINAL(0x00426A80)
// 0041A140 (RET 4) / 00417E10 (RET 4). The server's password / whether it is listed.
__declspec(naked) void __fastcall ServerSetPassword(void*, void*, const char*) EI_ORIGINAL(0x0041A140)
__declspec(naked) void __fastcall ServerSetPublic(void*, void*, int) EI_ORIGINAL(0x00417E10)
__declspec(naked) void __fastcall GrowingBufferConstruct(GameBuffer*, void*) EI_ORIGINAL(0x004DA7F0)
__declspec(naked) void __fastcall GrowingBufferDestruct(GameBuffer*, void*) EI_ORIGINAL(0x004DA910)
__declspec(naked) std::uint8_t* __fastcall ClientSessionNew(void*, void*) EI_ORIGINAL(0x0041BC40)
// 006E7E80. The CRT's strncpy.
__declspec(naked) char* __cdecl GameStrncpy(char*, const char*, std::size_t) EI_ORIGINAL(0x006E7E80)

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
        GrowingBufferConstruct(&hello, nullptr);
        char secret[kPasswordSize] = {};
        if (password) GameStrncpy(secret, password, kPasswordSize);
        BufferPut(&hello, nullptr, secret, static_cast<int>(kPasswordSize));
        ClientConnect(ClientSession(), nullptr, address, &hello);
        GrowingBufferDestruct(&hello, nullptr);
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

EI_REPLACE_METHOD(0x0057B3B0, GameFlow::SaveGame);
EI_REPLACE_METHOD(0x0057F0F0, GameFlow::ReturnLocalParty);
EI_REPLACE_METHOD(0x0057F150, GameFlow::UpdateAndAcknowledge);
EI_REPLACE_METHOD(0x0057FB60, GameFlow::RequestBriefing);
EI_REPLACE(0x0057F820, NameInList);
EI_REPLACE(0x0057AAB0, ClearSaveFolder);
EI_REPLACE_METHOD(0x0057EE10, GameFlow::SwitchMultiplayerZone);
EI_REPLACE_METHOD(0x0057E740, GameFlow::StartServer);
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
