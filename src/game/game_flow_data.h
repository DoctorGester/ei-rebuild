#pragma once

// Data of the exe the game flow's rewritten code (world_game.cpp) reads by address: strings,
// float constants (read from memory as the original does, rewrite guide rule 9) and vtables.
// Names are provisional.

#include <cstdint>

namespace flowdata {

// ---- SaveGame (0057B3B0) -----------------------------------------------------------------------
constexpr const char* kSlotFolder = "%ssaves\\%s\\";
constexpr const char* kCurrentFolder = "%ssaves\\current\\";
constexpr const char* kCurrentFiles = "%ssaves\\current\\*.*";
constexpr const char* kVisitedPrefix = "zs.";   // the "zone visited" variable
constexpr const char* kAlwaysCopied = "bz7g";
constexpr const char* kSaveExtension = ".sav";
constexpr const char* kScenarioFile = "scenario.sav";
constexpr const char* kMissionFile = "mission.sav";
constexpr const char* kMainMenu = "MainMenu";
constexpr const char* kSaved = "Saved ";

// ---- EnterZone ---------------------------------------------------------------------------------
constexpr const char* kMovieVariable = "movie";
constexpr const char* kMovieZone = "bz3g";       // no movie there
constexpr const char* kMovieFile = "Movies\\dcity.bik";
constexpr const char* kFrozenZone = "gz6g";
constexpr const char* kFrozenVariable = "z.gz6g.frozen";
constexpr const char* kFrozenMap = "zone6_2.mpr";
constexpr const char* kThawedMap = "zone6.mpr";
constexpr const char* kCorruptSave = "Save file %s is corrupt";
constexpr const char* kLeftPrefix = "zt.";      // the time the zone was left
constexpr const char* kGTime = "gtime";
constexpr const char* kMapExtension = ".mpr";
constexpr const char* kMobExtension = ".mob";
constexpr std::uintptr_t kHalf = std::uintptr_t(0x0073B86C);            // 0.5f

// ---- StartNetworkZone --------------------------------------------------------------------------
constexpr const char* kNetworkExitTarget = "MPGame1";   // where every exit leads
constexpr std::uintptr_t kExitArrayVtable = std::uintptr_t(0x0073C400);      // CArray<ZoneExit> (after 0056FFE0)
constexpr std::uintptr_t kExitArrayBaseVtable = std::uintptr_t(0x0073C3F4);

// ---- LeaveZone ---------------------------------------------------------------------------------
constexpr const char* kCantWrite = "Can`t write to %s";

// ---- The multiplayer flag and game --------------------------------------------------------------
constexpr const char* kMultiplayerVariable = "z.MPGame1";
// By the client session's game type.
constexpr const char* kMultiplayerZones[] = {"bz1mpg", "bz2mpg", "bz3mpg", "bz4mpg"};

// ---- ReadAIGraph -------------------------------------------------------------------------------
constexpr const char* kGraphExtension = ".grp";

// ---- StartServer / AcceptPlayers ---------------------------------------------------------------
constexpr const char* kLocalHost = "127.0.0.1";
constexpr const char* kServerName = "Server";
constexpr std::uintptr_t kNetLogStream = std::uintptr_t(0x007C56B0);  // the network log's ostream
constexpr const char* kAcceptedPlayer = "Accepted player ";
constexpr std::uintptr_t kSessionListVtable = std::uintptr_t(0x0073FA4C);

// ---- ClearSaveFolder ---------------------------------------------------------------------------
constexpr const char* kAllFiles = "*.*";

// ---- StartZone ---------------------------------------------------------------------------------
constexpr const char* kEndOfGame = "endofgame";
constexpr const char* kZoneNotFound = "Zone %s not found";
constexpr const char* kMenuMap = "ZoneMainMenuNew.mpr";
constexpr const char* kMenuMob = "ZoneMainMenuNew.mob";

// ---- New game, requests, LoadGame ---------------------------------------------------------------
constexpr const char* kExecAutoexec = "exec autoexec.cfg";
constexpr const char* kResourcesSingle = "database.res";
constexpr const char* kResourcesNetwork = "databaseLMP.res";
constexpr const char* kAutoplay = "autoplay.apl";
constexpr const char* kLoadedText = "Loaded ";

// ---- After starting the sessions ---------------------------------------------------------------
constexpr const char* kDefaultHeroName = "Human Hero";
constexpr const char* kDefaultPartyName = "Bug";

// ---- The client's update from the server -------------------------------------------------------
constexpr const char* kConstructionVariable = "constr.current";
constexpr const char* kBriefingSuffixQuest = "q";
constexpr const char* kBriefingSuffixK = "k";
constexpr const char* kQuestVariablePrefix = "q.";
constexpr const char* kDot = ".";
// By construction type.
constexpr const char* kTypeVariables[4] = {"b.smith.constr_1", "b.Shopper.constr_3", "b.kuzn.constr_4",
                                           "b.golem.constr_5"};
constexpr const char* kTypeBriefings[4] = {"intro_1", "intro_2", "intro_3", "intro_4"};

// ---- The saved multiplayer parties -------------------------------------------------------------
constexpr const char* kPartyFilePattern = "\\*.mp";
constexpr std::uintptr_t kRecordsVtable = std::uintptr_t(0x00743000);

} // namespace flowdata
