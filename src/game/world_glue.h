#pragma once

// The game glue: methods of two empty objects (the game's, the local player's) that the interface, the scripts
// and the console call instead of the worlds. None reads `this`, so they are plain functions here. world.h
// declares the others (GameTime, LocalPlayerData, ...).

#include "lib/mfc.h"

// The data folder CString / the base folder CString.
const mfc::CString* GameDataPathString();
const mfc::CString* GameBasePathString();

// The scripts' QuestComplete in a network game. With a live game server: its FinishMap for the map named
// `name` (the held map message dropped, "name_3" queued), 1 when it did; else 0.
int __stdcall FinishQuestMap(const char* name);

struct NamedFloatTable;

// A script's last error as callers take it (copied out of the script): the text, the code and the line.
struct ScriptErrorInfo {
    std::string text;
    int code;
    int line;
};

// The console's script command. Loads `text` into the command script (CScript::Load with the WorldScript
// started), runs it when it loaded and `run` is set (ScriptTick), and copies the script's last error into
// `error` when given. Returns Load's result.
int __stdcall RunScriptText(const std::string& text, char run, ScriptErrorInfo* error);

// For the quest and briefing screens: player zero's variables whose names start with `prefix` copied into
// `out` (NamedFloatTable::CopyPrefixed): the local player's as the client sees them in a network game, else
// the server's player 0; `out` is emptied when there is no such player.
void __stdcall PlayerZeroVariables(const char* prefix, NamedFloatTable* out);

// Called at the application's start-up. The start folder CString set to the current directory with a
// backslash appended.
void __cdecl StoreStartFolder();

// ---- The rest of the glue. `this` is never read. ---------------------
#include <cstdint>
#include <string>

class CPlayerClient;
class CUnit;
struct PartyUnitRecord;
class CTextManager;

// The server world's.
// Player 0's connection, when there is one, sends "constr.current" = `value`.
void __stdcall GameSetConstructionVariable(std::uint32_t value);
void __stdcall GameSetWeather(int weather, float strength);
// The server world's time of day in hours (fmod(tick * secondsPerTick + base, 24)).
double GameServerTimeOfDay();
// The players' times move on by `offset` (CWorldServer::UpdatePlayerTimes) unless it is negative or a NaN.
void __stdcall GamePassTime(float offset);
void __stdcall GameEnsureZone(const char* map, const char* mob);
// The memory stream of the local server's pending messages (the client's state block hands it over).
class CMemoryStream;
CMemoryStream* GameObject7C2D60();
// The server unit with id `id` moves to height `z` (its sector map entry follows, part 2 marked changed)
// unless it is there already (or NaN).
void __stdcall GameSetUnitHeight(int id, float z);
// The server world's pause flag / CWorldServer::SetPaused.
std::uint8_t GameServerPaused();
void __stdcall GameSetServerPaused(int paused);

// Recording stops: CameraCloseFile, the client world's flag 0x10 off.
void GameStopCameraRecording();
// The client world's flag 0x10 on, then CameraOpenFile(name); its result.
int __stdcall GameStartCameraRecording(const char* name);
// Loads the path file (with `flag`); when that works the camera plays it (flag 0x20) from now: the time offset
// is the client time (tick + fraction) less the first key's, the key index 0. Returns 1 when loaded.
int __stdcall GamePlayCameraPath(const char* name, int flag);
// Loads the path file and jumps to its first key (PlaybackStart).
int __stdcall GameShowCameraPath(const char* name);
// Whether a path is playing (flag 0x20, not 0 / 1).
std::uint32_t GameCameraPathPlaying();

// The client world's.
int GameRender();
void __stdcall GameStopSounds(int stopStream);
// The terrain's ambience stops (StopAmbience) / is switched on (flag 8).
void GameStopAmbience();
void GameStartAmbience();
// The sound manager is asked for the music stream `name` (null: the default), mode 4; RequestStream's result.
int __stdcall GamePlayMusic(const char* name);
CUnit* __stdcall GameFindClientUnit(int id);
void __stdcall GameCreatePartyUnits(const mfc::CArray<PartyUnitRecord*>* party, int allArmour);
void __stdcall GameRemoveUnitsOf(const mfc::CArray<PartyUnitRecord*>* records);
int __stdcall GameFindExitAt(const float* point, int* index);
int __stdcall GamePartyInExit(const mfc::CArray<PartyUnitRecord*>* records, int exit, mfc::CArray<PartyUnitRecord*>* inExit);
const char* __stdcall GameExitTarget(int index);
// Leaves through exit `exit`: with a live client session the flow switches the
// multiplayer zone, otherwise the local client takes the exit.
void __stdcall GameLeaveThroughExit(int exit);
// The interface's close-all flag set, then the local client's SendLeave04(value) / Rejoin.
void __stdcall GameSendLeave04(int value);
void GameRejoin();
void __stdcall GameLocalPlayerVariables(const char* prefix, NamedFloatTable* out);
CPlayerClient* GameLocalView();
int __stdcall GameSendFlag10(int value);
std::uint8_t GameClientPaused();
CTextManager* GameClientTexts();
