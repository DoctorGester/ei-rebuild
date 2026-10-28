#pragma once

// The game glue: methods of two empty objects (the game's, the local player's) that the interface, the scripts
// and the console call instead of the worlds. None reads `this`, so they are plain functions here. world.h
// declares the others (GameTime, LocalPlayerData, ...).

#include <cstdint>
#include <string>

// The scripts' QuestComplete in a network game. With a live game server: its FinishMap for the map named
// `name` (the held map message dropped, "name_3" queued), 1 when it did; else 0.
int FinishQuestMap(const char* name);

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
int RunScriptText(const std::string& text, char run, ScriptErrorInfo* error);

// For the quest and briefing screens: player zero's variables whose names start with `prefix` copied into
// `out` (NamedFloatTable::CopyPrefixed): the local player's as the client sees them in a network game, else
// the server's player 0; `out` is emptied when there is no such player.
void PlayerZeroVariables(const char* prefix, NamedFloatTable* out);

// Called at the application's start-up. The start folder string set to the current directory with a
// backslash appended.
void StoreStartFolder();

// ---- The rest of the glue. `this` is never read. ---------------------
#include <vector>

class CPlayerClient;
class CUnit;
struct PartyUnitRecord;
class CTextManager;

// The server world's.
// Player 0's connection, when there is one, sends "constr.current" = `value`.
void GameSetConstructionVariable(std::uint32_t value);
void GameSetWeather(int weather, float strength);
// The server world's time of day in hours (fmod(tick * secondsPerTick + base, 24)).
double GameServerTimeOfDay();
// The players' times move on by `offset` (CWorldServer::UpdatePlayerTimes) unless it is negative or a NaN.
void GamePassTime(float offset);
void GameEnsureZone(std::string_view map, std::string_view mob);
// The memory stream of the local server's pending messages (the client's state block hands it over).
class CMemoryStream;
// The server unit with id `id` moves to height `z` (its sector map entry follows, part 2 marked changed)
// unless it is there already (or NaN).
void GameSetUnitHeight(int id, float z);
// The server world's pause flag / CWorldServer::SetPaused.
void GameSetServerPaused(int paused);

// Recording stops: CameraCloseFile, the client world's flag 0x10 off.
void GameStopCameraRecording();
// The client world's flag 0x10 on, then CameraOpenFile(name); its result.
int GameStartCameraRecording(const char* name);
// Loads the path file (with `flag`); when that works the camera plays it (flag 0x20) from now: the time offset
// is the client time (tick + fraction) less the first key's, the key index 0. Returns 1 when loaded.
int GamePlayCameraPath(const char* name, int flag);
// Loads the path file and jumps to its first key (PlaybackStart).
int GameShowCameraPath(const char* name);
// Whether a path is playing (flag 0x20, not 0 / 1).
std::uint32_t GameCameraPathPlaying();

// The client world's.
int GameRender();
void GameStopSounds(int stopStream);
// The terrain's ambience stops (StopAmbience) / is switched on (flag 8).
void GameStopAmbience();
void GameStartAmbience();
// The sound manager is asked for the music stream `name` (null: the default), mode 4; RequestStream's result.
int GamePlayMusic(const char* name);
CUnit* GameFindClientUnit(int id);
void GameCreatePartyUnits(const std::vector<PartyUnitRecord*>* party, int allArmour);
void GameRemoveUnitsOf(const std::vector<PartyUnitRecord*>* records);
int GameFindExitAt(const float* point, int* index);
int GamePartyInExit(const std::vector<PartyUnitRecord*>* records, int exit, std::vector<PartyUnitRecord*>* inExit);
const char* GameExitTarget(int index);
// Leaves through exit `exit`: with a live client session the flow switches the
// multiplayer zone, otherwise the local client takes the exit.
void GameLeaveThroughExit(int exit);
// The interface's close-all flag set, then the local client's SendLeave04(value) / Rejoin.
void GameSendLeave04(int value);
void GameRejoin();
void GameLocalPlayerVariables(std::string_view prefix, NamedFloatTable* out);
CPlayerClient* GameLocalView();
int GameSendFlag10(int value);
