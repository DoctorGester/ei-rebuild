// Checks for the game flow (src/game/world_game.cpp). Saving writes files and changes the party
// and script state, so only ours runs and the result is checked.
#include "verify.h"

#include "../../src/game/unit_db.h"
#include "../../src/game/world.h"

#include "ei/rebirth.h"

#include <cstdio>
#include <cstring>

#include <windows.h>

namespace {
void* s_unused;

// GetFileAttributesA through game.exe's import slot (0073B2F8), which harness runs redirect to
// harness_saves like every save path the game touches; the DLL's own import is not redirected.
DWORD GameFileAttributes(const char* path) {
    using Fn = DWORD(WINAPI*)(LPCSTR);
    return (*reinterpret_cast<Fn const*>(0x0073B2F8))(path);
}
}

// 0057B3B0: a save that reports success has its scenario.sav; only single player saves.
static int __fastcall SaveGameVerify(GameFlow* self, void*, const char* slot) {
    int mode = *reinterpret_cast<const int*>(0x007B0098);
    int result = self->SaveGame(slot);
    char path[MAX_PATH];
    std::snprintf(path, sizeof(path), "%ssaves\\%s\\scenario.sav", *reinterpret_cast<char* const*>(0x007AF460), slot);
    bool exists = GameFileAttributes(path) != INVALID_FILE_ATTRIBUTES;
    bool ok = result == 0 || (result == 1 && mode == 1 && exists);
    ei::verify::Check("GameFlow::SaveGame", true, ok, "%s -> %d (mode %d, scenario.sav %d)", slot, result, mode, exists);
    return result;
}
EI_VERIFY(0x0057B3B0, SaveGameVerify, 0, s_unused);

int __stdcall EnterZone(std::uint8_t* records);   // world_game.cpp

// 0057A060: 0 or 1 (a restored zone gives LoadSave's 1, which is 1 unless the AI graph is missing).
static int __stdcall EnterZoneVerify(std::uint8_t* records) {
    char zone[64] = "";
    std::snprintf(zone, sizeof(zone), "%s", *reinterpret_cast<const char* const*>(records + 0x68));
    int result = EnterZone(records);
    auto* world = reinterpret_cast<CWorldServer*>(0x007AFF60);
    bool ok = result == 0 || result == 1;
    ei::verify::Check("EnterZone", true, ok, "%s -> %d, map %s", zone, result, world->mapName_ ? world->mapName_ : "");
    return result;
}
EI_VERIFY(0x0057A060, EnterZoneVerify, 0, s_unused);

using ZoneNameFn = mfc::CString*(__fastcall*)(CWorldServer*, void*, mfc::CString*);
static ZoneNameFn s_originalZoneName;

// 0057AA80: the original copies into a scratch CString; both share the zone name's buffer.
static mfc::CString* __fastcall ZoneNameVerify(CWorldServer* self, void*, mfc::CString* out) {
    alignas(4) unsigned char storage[sizeof(mfc::CString)];
    auto* scratch = reinterpret_cast<mfc::CString*>(storage);
    mfc::CString* expected = s_originalZoneName(self, nullptr, scratch);
    mfc::CString* result = self->ZoneNameInto(out);
    const char* a = *scratch;
    const char* b = *out;
    ei::verify::Check("CWorldServer::ZoneNameInto", reinterpret_cast<std::uintptr_t>(a), reinterpret_cast<std::uintptr_t>(b),
                      "%s", b);
    ei::verify::Check("CWorldServer::ZoneNameInto(result)", expected == scratch, result == out, "");
    scratch->~CString();
    return result;
}
EI_VERIFY(0x0057AA80, ZoneNameVerify, 6, s_originalZoneName);

int __stdcall LeaveZone(std::uint8_t* records);   // world_game.cpp

// 0057A780: a reported save has its saves\current\<zone>.sav.
static int __stdcall LeaveZoneVerify(std::uint8_t* records) {
    auto* world = reinterpret_cast<CWorldServer*>(0x007AFF60);
    char path[MAX_PATH];
    std::snprintf(path, sizeof(path), "%ssaves\\current\\%s.sav", *reinterpret_cast<char* const*>(0x007AF460),
                  world->zoneName_ ? world->zoneName_ : "");
    int result = LeaveZone(records);
    bool exists = GameFileAttributes(path) != INVALID_FILE_ATTRIBUTES;
    ei::verify::Check("LeaveZone", true, result == 0 || exists, "%s -> %d", path, result);
    return result;
}
EI_VERIFY(0x0057A780, LeaveZoneVerify, 0, s_unused);

int __stdcall StartNetworkZone(const char* zone);   // world_game.cpp

// 0057BDF0: on success every exit of the world leads to "MPGame1".
static int __stdcall StartNetworkZoneVerify(const char* zone) {
    int result = StartNetworkZone(zone);
    auto* world = reinterpret_cast<CWorldServer*>(0x007AFF60);
    bool ok = result == 0;
    if (result == 1) {
        ok = true;
        for (int i = 0; i < world->exitCount_ && ok; ++i) ok = std::strcmp(world->exits_[i].target, "MPGame1") == 0;
    }
    ei::verify::Check("StartNetworkZone", true, ok, "%s -> %d, %d exits", zone ? zone : "", result, world->exitCount_);
    return result;
}
EI_VERIFY(0x0057BDF0, StartNetworkZoneVerify, 0, s_unused);

// 0057C460: tears down and loads a zone, so only ours runs. Afterwards the campaign mode is the one
// asked for (8 for "endofgame", 2 for the menu or an unknown zone; mode 0 resolves to 0, 1, 3 or
// 4), and in the menu the server's +0x160 is set when AllowScriptsInMainMenu is on.
static void __fastcall StartZoneVerify(GameFlow* self, void*, const char* zone, int mode) {
    auto* world = reinterpret_cast<CWorldServer*>(0x007AFF60);
    int before160 = world->unknown160_;
    self->StartZone(zone, mode);
    int campaign = *reinterpret_cast<const int*>(0x007C2CC4);
    char what[80] = "";
    if (mode != 0 && campaign != mode && campaign != 2 && campaign != 8)
        std::snprintf(what, sizeof(what), "campaign mode %d", campaign);
    else if (mode == 0 && campaign != 0 && campaign != 1 && campaign != 2 && campaign != 3 && campaign != 4)
        std::snprintf(what, sizeof(what), "campaign mode %d", campaign);
    else if (campaign == 2 && ei::rebirth::Enabled(ei::rebirth::Patch::AllowScriptsInMainMenu) && world->unknown160_ != 1)
        std::snprintf(what, sizeof(what), "+0x160 %d in the menu", world->unknown160_);
    ei::verify::Check("GameFlow::StartZone", true, what[0] == 0, "%s mode %d -> campaign %d, +0x160 %d -> %d: %s",
                      zone ? zone : "", mode, campaign, before160, world->unknown160_, what);
}
EI_VERIFY(0x0057C460, StartZoneVerify, 0, s_unused);

namespace {
using UpdateMultiplayerFn = void(__fastcall*)(GameFlow*, void*);
UpdateMultiplayerFn s_originalUpdateMultiplayer;

__declspec(naked) double __fastcall VariableGetAt(void*, void*, const char*) {
    __asm mov eax, 0x00460AF0
    __asm jmp eax
}

double MultiplayerFlag() {
    auto* view = WorldClient()->LocalView();
    auto* player = *reinterpret_cast<WorldPlayer**>(view + 0x1C);
    return VariableGetAt(player->variables, nullptr, "z.MPGame1");
}
} // namespace

// 0057F190: ours, then the original must leave the flag as ours set it.
static void __fastcall UpdateMultiplayerVerify(GameFlow* self, void*) {
    self->UpdateMultiplayerVariable();
    double ours = MultiplayerFlag();
    s_originalUpdateMultiplayer(self, nullptr);
    double original = MultiplayerFlag();
    ei::verify::Check("GameFlow::UpdateMultiplayerVariable", true, ours == original, "z.MPGame1 %g (original %g)", ours,
                      original);
}
EI_VERIFY(0x0057F190, UpdateMultiplayerVerify, 5, s_originalUpdateMultiplayer);

namespace {
using MultiplayerZoneFn = mfc::CString*(__fastcall*)(GameFlow*, void*, mfc::CString*);
MultiplayerZoneFn s_originalMultiplayerZone;
}

// 0057E6B0: the same name as the original's.
static mfc::CString* __fastcall MultiplayerZoneVerify(GameFlow* self, void*, mfc::CString* out) {
    alignas(mfc::CString) std::uint8_t storage[sizeof(mfc::CString)];
    auto* original = s_originalMultiplayerZone(self, nullptr, reinterpret_cast<mfc::CString*>(storage));
    mfc::CString* result = self->MultiplayerZoneInto(out);
    bool ok = result == out && std::strcmp(*result, *original) == 0;
    ei::verify::Check("GameFlow::MultiplayerZoneInto", true, ok, "%s (original %s)", static_cast<const char*>(*result),
                      static_cast<const char*>(*original));
    original->~CString();
    return result;
}
EI_VERIFY(0x0057E6B0, MultiplayerZoneVerify, 6, s_originalMultiplayerZone);

// 0057F280: afterwards the flow loads the start zone (state 4) and the party outside a game is
// sent there.
static void __fastcall BeginMultiplayerVerify(GameFlow* self, void*) {
    self->BeginMultiplayerGame();
    auto* raw = reinterpret_cast<std::uint8_t*>(self);
    const char* zone = *reinterpret_cast<const char**>(raw + 4);
    const char* partyZone = *reinterpret_cast<const char**>(0x007C8318);
    bool ok = *reinterpret_cast<int*>(raw) == 4 && raw[0x80] == 0 && *reinterpret_cast<char*>(0x007C7DD8) == 1 &&
              std::strcmp(zone, partyZone) == 0;
    ei::verify::Check("GameFlow::BeginMultiplayerGame", true, ok, "zone %s, party zone %s", zone, partyZone);
}
EI_VERIFY(0x0057F280, BeginMultiplayerVerify, 0, s_unused);

namespace {
using NameInListFn = bool(__fastcall*)(const char* const*, const char*);
NameInListFn s_originalNameInList;

std::uint8_t* ActiveClient() {
    auto* client = *reinterpret_cast<std::uint8_t**>(0x0079B5D8);
    return client && !(*reinterpret_cast<std::uint32_t*>(client + 4) & 0x80000000u) ? client : nullptr;
}
} // namespace

// 0057F820: pure, compared with the original.
static bool __fastcall NameInListVerify(const char* const* list, const char* name) {
    bool expected = s_originalNameInList(list, name);
    bool result = NameInList(list, name);
    ei::verify::Check("NameInList", expected, result, "%s", name ? name : "");
    return result;
}
EI_VERIFY(0x0057F820, NameInListVerify, 6, s_originalNameInList);

// 0057AAB0: afterwards the folder exists (the files' deletion is through the game's imports).
static void __fastcall ClearSaveFolderVerify(const char* path) {
    ClearSaveFolder(path);
    DWORD attributes = GameFileAttributes(path);
    bool ok = attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
    ei::verify::Check("ClearSaveFolder", true, ok, "%s", path ? path : "");
}
EI_VERIFY(0x0057AAB0, ClearSaveFolderVerify, 0, s_unused);

// 0057EE10: the pending flag (+0x80) flips.
static void __fastcall SwitchMultiplayerZoneVerify(GameFlow* self, void*) {
    char before = reinterpret_cast<char*>(self)[0x80];
    self->SwitchMultiplayerZone();
    char after = reinterpret_cast<char*>(self)[0x80];
    ei::verify::Check("GameFlow::SwitchMultiplayerZone", true, (before != 0) != (after != 0), "+0x80 %d -> %d", before, after);
}
EI_VERIFY(0x0057EE10, SwitchMultiplayerZoneVerify, 0, s_unused);

// 0057E740: afterwards both sessions exist and the client has the game type.
static void __fastcall StartServerVerify(GameFlow* self, void*, const char* name, int value25E0, int gameType,
                                         const char* password, char unlisted, int value25FC) {
    self->StartServer(name, value25E0, gameType, password, unlisted, value25FC);
    auto* server = *reinterpret_cast<std::uint8_t**>(0x0079B5DC);
    auto* client = *reinterpret_cast<std::uint8_t**>(0x0079B5D8);
    bool ok = server && client && *reinterpret_cast<int*>(client + 0x261C) == gameType &&
              *reinterpret_cast<int*>(server + 0x25FC) == value25FC;
    ei::verify::Check("GameFlow::StartServer", true, ok, "%s type %d", name ? name : "", gameType);
}
EI_VERIFY(0x0057E740, StartServerVerify, 0, s_unused);

// A-W13. 0057EA50: afterwards there is no server session and a client session. 0057E970 sends the
// join request: the client session is still there. Both ours only (sessions are created and a
// connection is opened); reached from the multiplayer connect screens only.
static void __fastcall StartClientVerify(GameFlow* self, void*) {
    self->StartClient();
    auto* server = *reinterpret_cast<std::uint8_t**>(0x0079B5DC);
    auto* client = *reinterpret_cast<std::uint8_t**>(0x0079B5D8);
    ei::verify::Check("GameFlow::StartClient", true, server == nullptr && client != nullptr, "%p %p", server, client);
}
EI_VERIFY(0x0057EA50, StartClientVerify, 0, s_unused);

static void __fastcall JoinServerVerify(GameFlow* self, void*, const net::NetAddress* address, const char* password) {
    self->JoinServer(address, password);
    ei::verify::Check("GameFlow::JoinServer", true, *reinterpret_cast<void**>(0x0079B5D8) != nullptr, "%p", address);
}
EI_VERIFY(0x0057E970, JoinServerVerify, 0, s_unused);

// 0058DAF0: no players are lost.
static void __fastcall AcceptPlayersVerify(CWorldServer* self, void*, void* lobby) {
    auto count = [self] {
        int n = 0;
        PlayerIterator it;
        for (WorldPlayer* p = it.First(self->players_); p; p = it.Next()) ++n;
        return n;
    };
    int before = count();
    self->AcceptNewPlayers(lobby);
    int after = count();
    ei::verify::Check("CWorldServer::AcceptNewPlayers", true, after >= before, "players %d -> %d", before, after);
}
EI_VERIFY(0x0058DAF0, AcceptPlayersVerify, 0, s_unused);

// 0057F850: draws random numbers, so only ours runs; the output only grows.
static void __stdcall PickZonesVerify(void* out, int id, const char* exclude) {
    auto* v = static_cast<std::uint8_t**>(out);
    int before = static_cast<int>((v[1] - v[0]) / 12);
    PickZones(out, id, exclude);
    int after = static_cast<int>((v[1] - v[0]) / 12);
    ei::verify::Check("PickZones", true, after >= before, "id %d: %d -> %d names", id, before, after);
}
EI_VERIFY(0x0057F850, PickZonesVerify, 0, s_unused);

// 00579E60: LoadSave has its own check; logged.
static int __stdcall LoadMissionVerify(void* file) {
    int result = LoadMission(file);
    ei::verify::Check("LoadMission", true, true, "-> %d", result);
    return result;
}
EI_VERIFY(0x00579E60, LoadMissionVerify, 0, s_unused);

// 0057F150: afterwards an active client's request is not left answered (kind 3).
static void __fastcall UpdateAndAcknowledgeVerify(GameFlow* self, void*) {
    self->UpdateAndAcknowledge();
    std::uint8_t* client = ActiveClient();
    int kind = client ? *reinterpret_cast<int*>(*reinterpret_cast<std::uint8_t**>(client + 0x2600) + 0x28) : -1;
    ei::verify::Check("GameFlow::UpdateAndAcknowledge", true, kind != 3, "request kind %d", kind);
}
EI_VERIFY(0x0057F150, UpdateAndAcknowledgeVerify, 0, s_unused);

// 0057F0F0 / 0057FB60: network and party side effects through original code; logged.
static void __fastcall ReturnLocalPartyVerify(GameFlow* self, void*) {
    bool client = ActiveClient() != nullptr;
    self->ReturnLocalParty();
    ei::verify::Check("GameFlow::ReturnLocalParty", true, true, "client %d", client);
}
EI_VERIFY(0x0057F0F0, ReturnLocalPartyVerify, 0, s_unused);

static void __fastcall RequestBriefingVerify(GameFlow* self, void*, const char* briefing) {
    self->RequestBriefing(briefing);
    ei::verify::Check("GameFlow::RequestBriefing", true, true, "%s", briefing ? briefing : "");
}
EI_VERIFY(0x0057FB60, RequestBriefingVerify, 0, s_unused);

// 0057F390: afterwards a new client session is held (at least one reference).
static void __fastcall RecreateClientSessionVerify(GameFlow* self, void*) {
    auto* before = *reinterpret_cast<std::uint8_t**>(0x0079B5D8);
    self->RecreateClientSession();
    auto* session = *reinterpret_cast<std::uint8_t**>(0x0079B5D8);
    int refs = session ? *reinterpret_cast<std::uint16_t*>(session + 4) : -1;
    ei::verify::Check("GameFlow::RecreateClientSession", true, session && refs >= 1, "%p -> %p, references %d", before,
                      session, refs);
}
EI_VERIFY(0x0057F390, RecreateClientSessionVerify, 0, s_unused);

// 00581610: SaveGame's result (SaveGame has its own check).
static int __fastcall SaveGameThunkVerify(GameFlow* self, void*, const char* slot) {
    int result = self->SaveGameThunk(slot);
    ei::verify::Check("GameFlow::SaveGameThunk", true, result == 0 || result == 1, "%s -> %d", slot ? slot : "", result);
    return result;
}
EI_VERIFY(0x00581610, SaveGameThunkVerify, 0, s_unused);

// 00581630: through original code; logged.
static int __stdcall RequestZoneVerify(const char* zone, int entrance) {
    int result = RequestZone(zone, entrance);
    ei::verify::Check("RequestZone", true, true, "%s %d -> %d", zone ? zone : "", entrance, result);
    return result;
}
EI_VERIFY(0x00581630, RequestZoneVerify, 0, s_unused);

// 00581660: afterwards both sessions are gone and the state is 1.
static void __fastcall CloseSessionsVerify(GameFlow* self, void*) {
    self->CloseSessions();
    bool ok = *reinterpret_cast<void**>(0x0079B5DC) == nullptr && *reinterpret_cast<void**>(0x0079B5D8) == nullptr &&
              *reinterpret_cast<int*>(self) == 1 && *reinterpret_cast<char*>(0x007C7DD8) == 0;
    ei::verify::Check("GameFlow::CloseSessions", true, ok, "");
}
EI_VERIFY(0x00581660, CloseSessionsVerify, 0, s_unused);

namespace {
// The request fields after a request: state, name (+4), +0x78.
bool RequestIs(GameFlow* flow, int state, const char* name) {
    auto* raw = reinterpret_cast<std::uint8_t*>(flow);
    return *reinterpret_cast<int*>(raw) == state &&
           (!name || std::strcmp(*reinterpret_cast<const char**>(raw + 4), name) == 0);
}
int Field78(GameFlow* flow) { return *reinterpret_cast<int*>(reinterpret_cast<std::uint8_t*>(flow) + 0x78); }
} // namespace

// 005816C0 .. 00581760: afterwards the request holds what was asked.
static void __fastcall RequestState3Verify(GameFlow* self, void*) {
    self->RequestState3();
    ei::verify::Check("GameFlow::RequestState3", true, RequestIs(self, 3, nullptr), "");
}
EI_VERIFY(0x005816C0, RequestState3Verify, 0, s_unused);

static void __fastcall RequestLoadVerify(GameFlow* self, void*, const char* name, char flag) {
    self->RequestLoad(name, flag);
    ei::verify::Check("GameFlow::RequestLoad", true, RequestIs(self, 2, name) && Field78(self) == (flag != 0), "%s %d", name,
                      flag);
}
EI_VERIFY(0x005816D0, RequestLoadVerify, 0, s_unused);

static void __fastcall RequestZoneChangeVerify(GameFlow* self, void*, const char* zone, int entrance) {
    self->RequestZoneChange(zone, entrance);
    ei::verify::Check("GameFlow::RequestZoneChange", true, RequestIs(self, 4, zone) && Field78(self) == entrance, "%s %d",
                      zone, entrance);
}
EI_VERIFY(0x00581700, RequestZoneChangeVerify, 0, s_unused);

static void __fastcall RequestPartyZone5Verify(GameFlow* self, void*, const char* zone, const void* party) {
    self->RequestPartyZone5(zone, party);
    ei::verify::Check("GameFlow::RequestPartyZone5", true, RequestIs(self, 5, zone), "%s", zone);
}
EI_VERIFY(0x00581730, RequestPartyZone5Verify, 0, s_unused);

static void __fastcall RequestPartyZone6Verify(GameFlow* self, void*, const char* zone, const void* party) {
    self->RequestPartyZone6(zone, party);
    ei::verify::Check("GameFlow::RequestPartyZone6", true, RequestIs(self, 6, zone), "%s", zone);
}
EI_VERIFY(0x00581760, RequestPartyZone6Verify, 0, s_unused);

// 0057C700 acts on the pending request (zone changes, loading): ours only. The request is cleared.
static void __fastcall ProcessRequestVerify(GameFlow* self, void*) {
    int request = *reinterpret_cast<int*>(self);
    self->ProcessRequest();
    int after = *reinterpret_cast<int*>(self);
    ei::verify::Check("GameFlow::ProcessRequest", true, after == 0, "request %d -> %d", request, after);
}
EI_VERIFY(0x0057C700, ProcessRequestVerify, 0, s_unused);

// 0057ABA0 loads a saved game: ours only. In single player it returns 0 or 1, in multiplayer 0;
// a loaded game has its scenario.sav.
static int __fastcall LoadGameVerify(GameFlow* self, void*, const char* slot) {
    int mode = *reinterpret_cast<const int*>(0x007B0098);
    int result = self->LoadGame(slot);
    char path[MAX_PATH];
    std::snprintf(path, sizeof(path), "%ssaves\\%s\\scenario.sav", *reinterpret_cast<char* const*>(0x007AF460), slot);
    bool exists = GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
    bool ok = (mode == 2 || mode == 3) ? result == 0 : (result == 0 || (result == 1 && exists));
    ei::verify::Check("GameFlow::LoadGame", true, ok, "%s -> %d (mode %d, scenario.sav %d)", slot, result, mode, exists);
    return result;
}
EI_VERIFY(0x0057ABA0, LoadGameVerify, 0, s_unused);

// A-W13. 0057CB50 reloads the four resource databases (files read, every table rebuilt): ours only;
// "database.res" and "databaseLMP.res" both hold unit models and skills. Reached when the database
// changes: entering or leaving a network game (the multiplayer hero screen, back to the main menu).
static void __fastcall SwitchResourceDatabasesVerify(const char* name) {
    SwitchResourceDatabases(name);
    const auto* units = reinterpret_cast<const UnitDatabase*>(0x007AF358);
    bool ok = units->modelsBegin != units->modelsEnd && SkillDb()->skills.size() != 0;
    ei::verify::Check("SwitchResourceDatabases", true, ok, "%s", name);
}
EI_VERIFY(0x0057CB50, SwitchResourceDatabasesVerify, 0, s_unused);

// 0057DEE0: ours only (it may switch the databases). Afterwards the last database name (007AFDEC) is
// the one asked for. The main menu's Create, the multiplayer hero screen and every request.
static void __fastcall UseResourcesVerify(GameFlow* self, void*, bool network) {
    self->UseResources(network);
    const char* last = *reinterpret_cast<const char* const*>(0x007AFDEC);
    const char* wanted = network ? "databaseLMP.res" : "database.res";
    ei::verify::Check("GameFlow::UseResources", true, std::strcmp(last, wanted) == 0, "%s, wanted %s", last, wanted);
}
EI_VERIFY(0x0057DEE0, UseResourcesVerify, 0, s_unused);

// A-W13. 0057BC30 only lists files: PUSH -1; PUSH 0x7237D0 (7 bytes); both run and the sizes are
// compared. The save screens.
static int(__cdecl* s_currentSaveSize)();
static int __cdecl CurrentSaveSizeVerify() {
    int expected = s_currentSaveSize();
    ei::verify::Check("CurrentSaveSize", expected, CurrentSaveSize(), "bytes");
    return expected;
}
EI_VERIFY(0x0057BC30, CurrentSaveSizeVerify, 7, s_currentSaveSize);

// 0057ED60 empties saves\current and may start the zone: ours only; the multiplayer zone (+0x7C)
// is then the name given (empty for none). The session messages of a network game.
static void __fastcall SetNetworkZoneVerify(GameFlow* self, void*, const char* zone) {
    self->SetNetworkZone(zone);
    ei::verify::Check("GameFlow::SetNetworkZone", true, std::strcmp(self->zone7C_, zone ? zone : "") == 0, "%s",
                      zone ? zone : "(null)");
}
EI_VERIFY(0x0057ED60, SetNetworkZoneVerify, 0, s_unused);

// 00578460 / 005784D0: the flow's constructor and destructor bodies (the static initialiser of
// 007AFE20 and its atexit function): ours only. The constructor leaves no request and empty names.
static GameFlow* __fastcall FlowConstructVerify(GameFlow* self) {
    GameFlow* result = self->Construct();
    bool ok = result == self && self->state_ == 0 && self->requestTarget_[0] == 0 && self->zone7C_[0] == 0;
    ei::verify::Check("GameFlow::Construct", true, ok, "%p", self);
    return result;
}
EI_VERIFY(0x00578460, FlowConstructVerify, 0, s_unused);

static void __fastcall FlowDestructVerify(GameFlow* self) {
    self->Destruct();
    ei::verify::Check("GameFlow::Destruct", true, true, "%p", self);
}
EI_VERIFY(0x005784D0, FlowDestructVerify, 0, s_unused);
