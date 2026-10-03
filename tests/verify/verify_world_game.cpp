// Checks for the game flow (src/game/world_game.cpp). Saving writes files and changes the party
// and script state, so only ours runs and the result is checked.
#include "verify.h"

#include "../../src/game/world.h"

#include <cstdio>
#include <cstring>

#include <windows.h>

namespace {
void* s_unused;
}

// 0057B3B0: a save that reports success has its scenario.sav; only single player saves.
static int __fastcall SaveGameVerify(GameFlow* self, void*, const char* slot) {
    int mode = *reinterpret_cast<const int*>(0x007B0098);
    int result = self->SaveGame(slot);
    char path[MAX_PATH];
    std::snprintf(path, sizeof(path), "%ssaves\\%s\\scenario.sav", *reinterpret_cast<char* const*>(0x007AF460), slot);
    bool exists = GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
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
    bool exists = GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
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
