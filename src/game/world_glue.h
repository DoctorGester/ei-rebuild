#pragma once

// The game glue (00578000-00578E30): methods of two empty objects (007AFEA4 the game's, 007AFEA5
// the local player's) that the interface, the scripts and the console call instead of the worlds.
// None reads `this`, so they are plain functions here. world.h declares the ones rewritten
// earlier (GameTime, LocalPlayerData, ...). Names are provisional.

#include "lib/mfc.h"

// 00578DB0 / 00578DC0: the data folder CString 007AF460 / the base folder CString 007AF458.
const mfc::CString* GameDataPathString();
const mfc::CString* GameBasePathString();

// 0057FB90 (RET 4; the scripts' QuestComplete in a network game). With a live game server: its
// FinishMap for the map named `name` (the held map message dropped, "name_3" queued), 1 when it
// did; else 0.
int __stdcall FinishQuestMap(const char* name);

class CScript;            // script.h
struct NamedFloatTable;   // object_tables.h

// A script's last error as callers take it (00456360 copies it out of the script): the text, the
// code and the line.
struct ScriptErrorInfo {
    mfc::CString text;
    int code;
    int line;
};
static_assert(sizeof(ScriptErrorInfo) == 0xC);

// 005785D0 (RET 0xC; the console's script command 005DD800). Loads `text` into the command script
// (007C29CC; CScript::Load with the WorldScript started), runs it when it loaded and `run` is set
// (ScriptTick), and copies the script's last error into `error` when given. Returns Load's result.
int __stdcall RunScriptText(mfc::CString* text, char run, ScriptErrorInfo* error);

// 00578C40 (RET 8; the quest and briefing screens). Player zero's variables whose names start with
// `prefix` copied into `out` (NamedFloatTable::CopyPrefixed): the local player's as the client sees
// them in a network game, else the server's player 0; `out` is emptied when there is no such player.
void __stdcall PlayerZeroVariables(const char* prefix, NamedFloatTable* out);

// 0055EA10 (the application's start-up 00456440). The start folder CString 007AF454 set to the
// current directory with a backslash appended.
void __cdecl StoreStartFolder();

// ---- A-W13: the rest of the glue. `this` (007AFEA4..007AFEA6) is never read. ---------------------
#include <cstdint>

class CEngineObject;
struct ClientTexts;

// The server world's (007AFF60).
// 00578560 (RET 4). Player 0's connection, when there is one, sends "constr.current" = `value`.
void __stdcall GameSetConstructionVariable(std::uint32_t value);
// 005786A0 (RET 8). CWorldServer::SetWeather.
void __stdcall GameSetWeather(int weather, float strength);
// 005786E0. The server world's time of day in hours (fmod(tick * secondsPerTick + base, 24)), in ST0.
double GameServerTimeOfDay();
// 00578700 (RET 4). The players' times move on by `offset` (CWorldServer::UpdatePlayerTimes)
// unless it is negative or a NaN.
void __stdcall GamePassTime(float offset);
// 00578730 (RET 8; the client's state block 00567AB0). CWorldServer::EnsureZone.
void __stdcall GameEnsureZone(const char* map, const char* mob);
// 00578750 (the client's state block 00567AB0 hands it to 00667B40). The object at 007C2D60.
void* GameObject7C2D60();
// 00578760 (RET 8; the briefing screen). The server unit with id `id` moves to height `z` (its
// sector map entry follows, part 2 marked changed) unless it is there already (or NaN).
void __stdcall GameSetUnitHeight(int id, float z);
// 00578D00 / 00578D10 (RET 4). The server world's pause flag / CWorldServer::SetPaused.
std::uint8_t GameServerPaused();
void __stdcall GameSetServerPaused(int paused);

// The camera's (the client world's, 007AF7C8).
// 005787C0. The recorded path stops playing (flag 0x20 off).
void GameStopCameraPath();
// 005787D0. Recording stops: CameraCloseFile, the client world's flag 0x10 off.
void GameStopCameraRecording();
// 005787F0 (RET 4). The client world's flag 0x10 on, then CameraOpenFile(name); its result.
int __stdcall GameStartCameraRecording(const char* name);
// 00578820 (RET 8; the script's camera command, the briefing). Loads the path file (0040ECD0 with
// `flag`); when that works the camera plays it (flag 0x20) from now: the time offset is the client
// time (tick + fraction) less the first key's, the key index 0. Returns 1 when loaded.
int __stdcall GamePlayCameraPath(const char* name, int flag);
// 00578880 (RET 4). Loads the path file and jumps to its first key (PlaybackStart).
int __stdcall GameShowCameraPath(const char* name);
// 005788B0. Whether a path is playing (flag 0x20, not 0 / 1).
std::uint32_t GameCameraPathPlaying();

// The client world's (007AF5B8).
// 00578910. CWorldClient::Render (its result).
int GameRender();
// 00578920 (RET 4). CSoundManager::StopAll(stopStream).
void __stdcall GameStopSounds(int stopStream);
// 00578940 / 00578950. The terrain's ambience stops (StopAmbience) / is switched on (flag 8).
void GameStopAmbience();
void GameStartAmbience();
// 00578960 (RET 4). The sound manager is asked for the music stream `name` (null: the default
// 0079BECC), mode 4; RequestStream's result.
int __stdcall GamePlayMusic(const char* name);
// 00578980 (RET 4). CWorld::FindUnit on the client world.
CEngineObject* __stdcall GameFindClientUnit(int id);
// 005789C0 (RET 8) / 005789E0 (RET 4). CWorldClient::CreatePartyUnits / RemoveUnitsOf.
void __stdcall GameCreatePartyUnits(const mfc::CArray<std::uint8_t*>* party, int allArmour);
void __stdcall GameRemoveUnitsOf(const mfc::CArray<std::uint8_t*>* records);
// 00578A00 (RET 8) / 00578A20 (RET 0xC) / 00578A40 (RET 4). CWorldClient::FindExitAt / PartyInExit /
// ExitTarget.
int __stdcall GameFindExitAt(const float* point, int* index);
int __stdcall GamePartyInExit(const mfc::CArray<std::uint8_t*>* records, int exit, mfc::CArray<std::uint8_t*>* inExit);
const char* __stdcall GameExitTarget(int index);
// 00578AC0 (RET 4). Leaves through exit `exit`: with a live client session the flow switches the
// multiplayer zone, otherwise the local client takes the exit.
void __stdcall GameLeaveThroughExit(int exit);
// 00578B00 (RET 4) / 00578B20. The interface's close-all flag (007C7D94, CInterface3D +0x44) set, then the local client's SendLeave04(value) / Rejoin.
void __stdcall GameSendLeave04(int value);
void GameRejoin();
// 005793E0 (RET 4; the single-player start). The server's zone torn down, the client world's local
// session given the loopback pair for `player` (CWorldClient::SetPlayer), then the local view's
// +0x84 set and its party-taken flag cleared.
void __stdcall GameStartLocalSession(std::uint8_t* player);
// 00578B70 (RET 8). The local player's variables whose names start with `prefix`, into `out`.
void __stdcall GameLocalPlayerVariables(const char* prefix, NamedFloatTable* out);
// 00578BC0. CWorldClient::LocalView.
std::uint8_t* GameLocalView();
// 00578D30 (RET 4). CWorldClient::SendFlag10 (its result).
int __stdcall GameSendFlag10(int value);
// 00578D50. The client world's pause flag.
std::uint8_t GameClientPaused();
// 00578D60 / 00578D70 (RET 4). The map edge fade (0079BD34), in ST0 / stored as it comes.
double GameMapEdgeFade();
void __stdcall GameSetMapEdgeFade(float value);
// 00578E20. The client world's text resources (+0x1D0).
ClientTexts* GameClientTexts();
