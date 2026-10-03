// The game flow object (007AFE20; the state machine 0057C700 runs on it, Rebirth patches that one):
// saving a game into saves\<slot>. See docs/notes/world.md (seventh round).
#include "world.h"

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
__declspec(naked) void __fastcall ClearSaveFolder(const char* /*path*/) EI_ORIGINAL(0x0057AAB0)
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

constexpr int kSlotStreamOpen = 8;    // the file stream's slot 8 (+0x20): Open(path, flags, error)
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

    bool Open(const char* path) {
        stream.unknown_04 = 0;
        stream.ok = ei::VCall<int>(&stream, kSlotStreamOpen, path, kOpenWrite, 0) != 0;
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
    ClearSaveFolder(slotFolder);
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
        opened = file.Open(path);
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
            opened = file.Open(path);
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

EI_REPLACE_METHOD(0x0057B3B0, GameFlow::SaveGame);
