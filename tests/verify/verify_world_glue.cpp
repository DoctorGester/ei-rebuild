// Checks for the rest of the game glue (src/game/world_glue.cpp, A-W13): small functions of the
// empty glue objects that hand a call on to the worlds, the camera, the terrain or the sound
// manager. The getters and lookups run both versions and compare; the ones that send, load or
// change something run ours only and check the state they leave (or are only logged when the work
// is the callee's, which has its own check). The interface, the briefing and the scripts call them
// (`newgame`, `world_load`); the camera path and network ones have no scenario.
#include "verify.h"

#include "../../src/game/camera.h"
#include "../../src/game/object_tables.h"
#include "../../src/game/terrain.h"
#include "../../src/game/unit.h"
#include "../../src/game/world.h"
#include "../../src/game/world_glue.h"

#include <cstdint>
#include <cstring>

namespace {
void* s_unused;

char ZoneRequested() { return *reinterpret_cast<char*>(0x007C7D94); }
const char* Text(const char* text) { return text ? text : "(null)"; }
} // namespace

// ---- Both versions run ---------------------------------------------------------------------------

// 005786E0: FILD [imm32] (6 bytes). Compared as a float.
static double(__cdecl* s_serverTimeOfDay)();
static double __cdecl ServerTimeOfDayVerify() {
    float expected = static_cast<float>(s_serverTimeOfDay());
    double result = GameServerTimeOfDay();
    ei::verify::CheckNear("GameServerTimeOfDay", expected, static_cast<float>(result), "");
    return result;
}
EI_VERIFY(0x005786E0, ServerTimeOfDayVerify, 6, s_serverTimeOfDay);

// 00578750: MOV EAX,imm32 (5 bytes).
static void*(__cdecl* s_object7C2D60)();
static void* __cdecl Object7C2D60Verify() {
    void* expected = s_object7C2D60();
    void* result = GameObject7C2D60();
    ei::verify::Check("GameObject7C2D60", expected, result, "");
    return result;
}
EI_VERIFY(0x00578750, Object7C2D60Verify, 5, s_object7C2D60);

// 00578D00 / 00578D50: MOV AL,[imm32] (5 bytes).
static std::uint8_t(__cdecl* s_serverPaused)();
static std::uint8_t __cdecl ServerPausedVerify() {
    std::uint8_t expected = s_serverPaused();
    std::uint8_t result = GameServerPaused();
    ei::verify::Check("GameServerPaused", expected, result, "");
    return result;
}
EI_VERIFY(0x00578D00, ServerPausedVerify, 5, s_serverPaused);

static std::uint8_t(__cdecl* s_clientPaused)();
static std::uint8_t __cdecl ClientPausedVerify() {
    std::uint8_t expected = s_clientPaused();
    std::uint8_t result = GameClientPaused();
    ei::verify::Check("GameClientPaused", expected, result, "");
    return result;
}
EI_VERIFY(0x00578D50, ClientPausedVerify, 5, s_clientPaused);

// 005788B0 / 00578E20: MOV EAX,[imm32] (5 bytes).
static std::uint32_t(__cdecl* s_cameraPathPlaying)();
static std::uint32_t __cdecl CameraPathPlayingVerify() {
    std::uint32_t expected = s_cameraPathPlaying();
    std::uint32_t result = GameCameraPathPlaying();
    ei::verify::Check("GameCameraPathPlaying", expected, result, "");
    return result;
}
EI_VERIFY(0x005788B0, CameraPathPlayingVerify, 5, s_cameraPathPlaying);

static ClientTexts*(__cdecl* s_clientTexts)();
static ClientTexts* __cdecl ClientTextsVerify() {
    ClientTexts* expected = s_clientTexts();
    ClientTexts* result = GameClientTexts();
    ei::verify::Check("GameClientTexts", static_cast<void*>(expected), static_cast<void*>(result), "");
    return result;
}
EI_VERIFY(0x00578E20, ClientTextsVerify, 5, s_clientTexts);

// 00578BC0: MOV ECX,imm32 (5 bytes).
static std::uint8_t*(__cdecl* s_localView)();
static std::uint8_t* __cdecl LocalViewVerify() {
    std::uint8_t* expected = s_localView();
    std::uint8_t* result = GameLocalView();
    ei::verify::Check("GameLocalView", static_cast<void*>(expected), static_cast<void*>(result), "");
    return result;
}
EI_VERIFY(0x00578BC0, LocalViewVerify, 5, s_localView);

// 00578D60: FLD [imm32] (6 bytes). The value is only loaded: the bits are compared.
static double(__cdecl* s_mapEdgeFade)();
static double __cdecl MapEdgeFadeVerify() {
    float expected = static_cast<float>(s_mapEdgeFade());
    double result = GameMapEdgeFade();
    float actual = static_cast<float>(result);
    ei::verify::Check("GameMapEdgeFade", 0, std::memcmp(&expected, &actual, sizeof(expected)), "%g vs %g", expected, actual);
    return result;
}
EI_VERIFY(0x00578D60, MapEdgeFadeVerify, 6, s_mapEdgeFade);

// 00578980: MOV EAX,[ESP+4]; MOV ECX,imm32 (9 bytes).
static CEngineObject*(__stdcall* s_findClientUnit)(int);
static CEngineObject* __stdcall FindClientUnitVerify(int id) {
    CEngineObject* expected = s_findClientUnit(id);
    CEngineObject* result = GameFindClientUnit(id);
    ei::verify::Check("GameFindClientUnit", static_cast<void*>(expected), static_cast<void*>(result), "id %d", id);
    return result;
}
EI_VERIFY(0x00578980, FindClientUnitVerify, 9, s_findClientUnit);

// 00578A00: MOV EAX,[ESP+8]; MOV ECX,[ESP+4] (8 bytes). The original writes a scratch index.
static int(__stdcall* s_findExitAt)(const float*, int*);
static int __stdcall FindExitAtVerify(const float* point, int* index) {
    int theirIndex = index ? *index : 0;
    int ourStart = theirIndex;
    int expected = s_findExitAt(point, &theirIndex);
    int result = GameFindExitAt(point, index);
    bool same = expected == result && (!index || *index == theirIndex);
    ei::verify::Check("GameFindExitAt", true, same, "%d / %d vs %d / %d (was %d)", expected, theirIndex, result,
                      index ? *index : 0, ourStart);
    return result;
}
EI_VERIFY(0x00578A00, FindExitAtVerify, 8, s_findExitAt);

// 00578A20: MOV EAX,[ESP+0xC]; MOV ECX,[ESP+8] (8 bytes). Both empty and refill `inExit`; the results
// and the listed count are compared.
static int(__stdcall* s_partyInExit)(const mfc::CArray<std::uint8_t*>*, int, mfc::CArray<std::uint8_t*>*);
static int __stdcall PartyInExitVerify(const mfc::CArray<std::uint8_t*>* records, int exit,
                                       mfc::CArray<std::uint8_t*>* inExit) {
    int expected = s_partyInExit(records, exit, inExit);
    int theirCount = inExit->size;
    int result = GamePartyInExit(records, exit, inExit);
    ei::verify::Check("GamePartyInExit", true, expected == result && theirCount == inExit->size, "exit %d: %d / %d vs %d / %d",
                      exit, expected, theirCount, result, inExit->size);
    return result;
}
EI_VERIFY(0x00578A20, PartyInExitVerify, 8, s_partyInExit);

// 00578A40: MOV EAX,[ESP+4]; MOV ECX,imm32 (9 bytes). 00578A60: MOV EAX,[ESP+4]; SUB ESP,0x3C (7
// bytes). The texts are compared by content (the original's comes from a copy of the exit).
static const char*(__stdcall* s_exitTarget)(int);
static const char* __stdcall ExitTargetVerify(int index) {
    const char* expected = s_exitTarget(index);
    const char* result = GameExitTarget(index);
    ei::verify::Check("GameExitTarget", 0, std::strcmp(Text(expected), Text(result)), "exit %d: \"%s\" vs \"%s\"", index,
                      Text(expected), Text(result));
    return result;
}
EI_VERIFY(0x00578A40, ExitTargetVerify, 9, s_exitTarget);

static const char*(__fastcall* s_clientExitTarget)(CWorldClient*, void*, int);
static const char* __fastcall ClientExitTargetVerify(CWorldClient* self, void*, int index) {
    const char* expected = s_clientExitTarget(self, nullptr, index);
    const char* result = self->ExitTarget(index);
    ei::verify::Check("CWorldClient::ExitTarget", 0, std::strcmp(Text(expected), Text(result)), "exit %d", index);
    return result;
}
EI_VERIFY(0x00578A60, ClientExitTargetVerify, 7, s_clientExitTarget);

// ---- Ours only -----------------------------------------------------------------------------------

static void __stdcall SetConstructionVariableVerify(std::uint32_t value) {
    GameSetConstructionVariable(value);
    ei::verify::Check("GameSetConstructionVariable", true, true, "%u", value);
}
EI_VERIFY(0x00578560, SetConstructionVariableVerify, 0, s_unused);

static void __stdcall SetUnknownE4E8Verify(int e4, float e8) {
    GameSetUnknownE4E8(e4, e8);
    ei::verify::Check("GameSetUnknownE4E8", true, true, "%d %g", e4, e8);
}
EI_VERIFY(0x005786A0, SetUnknownE4E8Verify, 0, s_unused);

static void __stdcall PassTimeVerify(float offset) {
    GamePassTime(offset);
    ei::verify::Check("GamePassTime", true, true, "%g", offset);
}
EI_VERIFY(0x00578700, PassTimeVerify, 0, s_unused);

static void __stdcall EnsureZoneGlueVerify(const char* map, const char* mob) {
    GameEnsureZone(map, mob);
    ei::verify::Check("GameEnsureZone", true, true, "%s %s", Text(map), Text(mob));
}
EI_VERIFY(0x00578730, EnsureZoneGlueVerify, 0, s_unused);

// Afterwards the unit, when there is one, stands at the height (a NaN height changes nothing).
static void __stdcall SetUnitHeightVerify(int id, float z) {
    GameSetUnitHeight(id, z);
    auto* unit = static_cast<CUnit*>(WorldServer()->FindUnit(id));
    bool ok = !unit || !(z == z) || unit->position.z == z;
    ei::verify::Check("GameSetUnitHeight", true, ok, "id %d z %g", id, z);
}
EI_VERIFY(0x00578760, SetUnitHeightVerify, 0, s_unused);

static void __stdcall SetServerPausedVerify(int paused) {
    GameSetServerPaused(paused);
    ei::verify::Check("GameSetServerPaused", static_cast<std::uint8_t>(paused), WorldServer()->paused_, "");
}
EI_VERIFY(0x00578D10, SetServerPausedVerify, 0, s_unused);

static void __cdecl StopCameraPathVerify() {
    GameStopCameraPath();
    ei::verify::Check("GameStopCameraPath", 0u, WorldClient()->camera_->flags_ & CCamera::kPlaying, "");
}
EI_VERIFY(0x005787C0, StopCameraPathVerify, 0, s_unused);

static void __cdecl StopCameraRecordingVerify() {
    GameStopCameraRecording();
    bool ok = !(WorldClient()->camera_->flags_ & CCamera::kRecording) && !(WorldClient()->flags_ & 0x10);
    ei::verify::Check("GameStopCameraRecording", true, ok, "");
}
EI_VERIFY(0x005787D0, StopCameraRecordingVerify, 0, s_unused);

static int __stdcall StartCameraRecordingVerify(const char* name) {
    int result = GameStartCameraRecording(name);
    bool ok = (WorldClient()->flags_ & 0x10) != 0 && (result == 0 || result == 1);
    ei::verify::Check("GameStartCameraRecording", true, ok, "%s -> %d", Text(name), result);
    return result;
}
EI_VERIFY(0x005787F0, StartCameraRecordingVerify, 0, s_unused);

static int __stdcall PlayCameraPathVerify(const char* name, int flag) {
    int result = GamePlayCameraPath(name, flag);
    CCamera* camera = WorldClient()->camera_;
    bool ok = result == 0 || (result == 1 && (camera->flags_ & CCamera::kPlaying) && camera->keyIndex_ == 0);
    ei::verify::Check("GamePlayCameraPath", true, ok, "%s %d -> %d", Text(name), flag, result);
    return result;
}
EI_VERIFY(0x00578820, PlayCameraPathVerify, 0, s_unused);

static int __stdcall ShowCameraPathVerify(const char* name) {
    int result = GameShowCameraPath(name);
    ei::verify::Check("GameShowCameraPath", true, true, "%s -> %d", Text(name), result);
    return result;
}
EI_VERIFY(0x00578880, ShowCameraPathVerify, 0, s_unused);

static int __cdecl RenderVerify() {
    int result = GameRender();
    ei::verify::Check("GameRender", true, true, "");
    return result;
}
EI_VERIFY(0x00578910, RenderVerify, 0, s_unused);

static void __stdcall StopSoundsVerify(int stopStream) {
    GameStopSounds(stopStream);
    ei::verify::Check("GameStopSounds", true, true, "%d", stopStream);
}
EI_VERIFY(0x00578920, StopSoundsVerify, 0, s_unused);

static void __cdecl StopAmbienceVerify() {
    GameStopAmbience();
    ei::verify::Check("GameStopAmbience", 0u, WorldClient()->terrain_->landscapeFlags & 8u, "");
}
EI_VERIFY(0x00578940, StopAmbienceVerify, 0, s_unused);

static void __cdecl StartAmbienceVerify() {
    GameStartAmbience();
    ei::verify::Check("GameStartAmbience", 8u, WorldClient()->terrain_->landscapeFlags & 8u, "");
}
EI_VERIFY(0x00578950, StartAmbienceVerify, 0, s_unused);

static int __stdcall PlayMusicVerify(const char* name) {
    int result = GamePlayMusic(name);
    ei::verify::Check("GamePlayMusic", true, true, "%s -> %d", Text(name), result);
    return result;
}
EI_VERIFY(0x00578960, PlayMusicVerify, 0, s_unused);

static void __stdcall CreatePartyUnitsVerify(const mfc::CArray<std::uint8_t*>* party, int allArmour) {
    GameCreatePartyUnits(party, allArmour);
    ei::verify::Check("GameCreatePartyUnits", true, true, "%d records", party->size);
}
EI_VERIFY(0x005789C0, CreatePartyUnitsVerify, 0, s_unused);

static void __stdcall RemoveUnitsOfGlueVerify(const mfc::CArray<std::uint8_t*>* records) {
    GameRemoveUnitsOf(records);
    ei::verify::Check("GameRemoveUnitsOf", true, true, "%d records", records->size);
}
EI_VERIFY(0x005789E0, RemoveUnitsOfGlueVerify, 0, s_unused);

// 00567400: afterwards the client world has none of the records' units.
static void __fastcall RemoveUnitsOfVerify(CWorldClient* self, void*, const RecordPtrArray* records) {
    self->RemoveUnitsOf(records);
    bool gone = true;
    for (int i = 0; i < records->size; ++i)
        gone = gone && !self->FindUnit(static_cast<int>(reinterpret_cast<PartyUnitRecord*>(records->data[i])->unitId));
    ei::verify::Check("CWorldClient::RemoveUnitsOf", true, gone, "%d records", records->size);
}
EI_VERIFY(0x00567400, RemoveUnitsOfVerify, 0, s_unused);

static void __stdcall LeaveThroughExitVerify(int exit) {
    GameLeaveThroughExit(exit);
    ei::verify::Check("GameLeaveThroughExit", true, true, "exit %d", exit);
}
EI_VERIFY(0x00578AC0, LeaveThroughExitVerify, 0, s_unused);

static void __stdcall SendLeave04Verify(int value) {
    GameSendLeave04(value);
    ei::verify::Check("GameSendLeave04", 1, static_cast<int>(ZoneRequested()), "%d", value);
}
EI_VERIFY(0x00578B00, SendLeave04Verify, 0, s_unused);

static void __cdecl RejoinVerify() {
    GameRejoin();
    ei::verify::Check("GameRejoin", 1, static_cast<int>(ZoneRequested()), "");
}
EI_VERIFY(0x00578B20, RejoinVerify, 0, s_unused);

static void __stdcall LocalPlayerVariablesVerify(const char* prefix, NamedFloatTable* out) {
    GameLocalPlayerVariables(prefix, out);
    ei::verify::Check("LocalPlayerVariables", true, true, "%s", Text(prefix));
}
EI_VERIFY(0x00578B70, LocalPlayerVariablesVerify, 0, s_unused);

static int __stdcall SendFlag10GlueVerify(int value) {
    int result = GameSendFlag10(value);
    ei::verify::Check("GameSendFlag10", true, true, "%d", value);
    return result;
}
EI_VERIFY(0x00578D30, SendFlag10GlueVerify, 0, s_unused);

static int __fastcall SendFlag10Verify(CWorldClient* self, void*, int value) {
    int result = self->SendFlag10(value);
    ei::verify::Check("CWorldClient::SendFlag10", true, true, "%d", value);
    return result;
}
EI_VERIFY(0x00566330, SendFlag10Verify, 0, s_unused);

// The value is stored as it comes: the bits are compared.
static void __stdcall SetMapEdgeFadeVerify(float value) {
    GameSetMapEdgeFade(value);
    ei::verify::Check("GameSetMapEdgeFade", 0, std::memcmp(&value, reinterpret_cast<const void*>(0x0079BD34), 4), "%g", value);
}
EI_VERIFY(0x00578D70, SetMapEdgeFadeVerify, 0, s_unused);

// 005793E0: ours only (the zone is torn down and the session reconnected). Afterwards the local
// view's two flags are 1 and 0. Every single-player game start (`newgame`, `world_load`).
static void __stdcall StartLocalSessionVerify(std::uint8_t* player) {
    GameStartLocalSession(player);
    auto* view = reinterpret_cast<LocalViewData*>(WorldClient()->LocalView());
    ei::verify::Check("GameStartLocalSession", true, view->unknown_84 == 1 && view->partyTaken == 0, "");
}
EI_VERIFY(0x005793E0, StartLocalSessionVerify, 0, s_unused);
