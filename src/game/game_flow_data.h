#pragma once

// Data of the exe the game flow's rewritten code (world_game.cpp) reads by address: strings,
// float constants (read from memory as the original does, rewrite guide rule 9) and vtables.
// Names are provisional.

#include <cstdint>

namespace flowdata {

// ---- SaveGame (0057B3B0) -----------------------------------------------------------------------
constexpr std::uintptr_t kSlotFolder = std::uintptr_t(0x007905E4);      // "%ssaves\\%s\\"
constexpr std::uintptr_t kCurrentFolder = std::uintptr_t(0x00790534);   // "%ssaves\\current\\"
constexpr std::uintptr_t kCurrentFiles = std::uintptr_t(0x0079060C);    // "%ssaves\\current\\*.*"
constexpr std::uintptr_t kVisitedPrefix = std::uintptr_t(0x00790548);   // "zs.": the "zone visited" variable
constexpr std::uintptr_t kAlwaysCopied = std::uintptr_t(0x007905CC);    // "bz7g"
constexpr std::uintptr_t kSaveExtension = std::uintptr_t(0x0079052C);   // ".sav"
constexpr std::uintptr_t kScenarioFile = std::uintptr_t(0x007905D4);    // "scenario.sav"
constexpr std::uintptr_t kMissionFile = std::uintptr_t(0x007905C0);     // "mission.sav"
constexpr std::uintptr_t kMainMenu = std::uintptr_t(0x007904EC);        // "MainMenu"
constexpr std::uintptr_t kSaved = std::uintptr_t(0x00790604);           // "Saved "

// ---- EnterZone ---------------------------------------------------------------------------------
constexpr std::uintptr_t kMovieVariable = std::uintptr_t(0x00790598);   // "movie"
constexpr std::uintptr_t kMovieZone = std::uintptr_t(0x00790590);       // "bz3g": no movie there
constexpr std::uintptr_t kMovieFile = std::uintptr_t(0x0079057C);       // "Movies\\dcity.bik"
constexpr std::uintptr_t kFrozenZone = std::uintptr_t(0x00790574);      // "gz6g"
constexpr std::uintptr_t kFrozenVariable = std::uintptr_t(0x00790564);  // "z.gz6g.frozen"
constexpr std::uintptr_t kFrozenMap = std::uintptr_t(0x00790558);       // "zone6_2.mpr"
constexpr std::uintptr_t kThawedMap = std::uintptr_t(0x0079054C);       // "zone6.mpr"
constexpr std::uintptr_t kCorruptSave = std::uintptr_t(0x00790514);     // "Save file %s is corrupt"
constexpr std::uintptr_t kLeftPrefix = std::uintptr_t(0x00790510);      // "zt.": the time the zone was left
constexpr std::uintptr_t kGTime = std::uintptr_t(0x0073F798);           // "gtime"
constexpr std::uintptr_t kMapExtension = std::uintptr_t(0x007848EC);    // ".mpr"
constexpr std::uintptr_t kMobExtension = std::uintptr_t(0x007848F4);    // ".mob"
constexpr std::uintptr_t kTicksPerTime = std::uintptr_t(0x0073BAB0);    // world ticks per unit of "gtime"
constexpr std::uintptr_t kHalf = std::uintptr_t(0x0073B86C);            // 0.5f

// ---- StartNetworkZone --------------------------------------------------------------------------
constexpr std::uintptr_t kNetworkExitTarget = std::uintptr_t(0x00790620);   // "MPGame1": where every exit leads
constexpr std::uintptr_t kExitArrayVtable = std::uintptr_t(0x0073C400);      // CArray<ZoneExit> (after 0056FFE0)
constexpr std::uintptr_t kExitArrayBaseVtable = std::uintptr_t(0x0073C3F4);

// ---- LeaveZone ---------------------------------------------------------------------------------
constexpr std::uintptr_t kCantWrite = std::uintptr_t(0x007905A0);   // "Can`t write to %s"

// ---- The multiplayer flag and game --------------------------------------------------------------
constexpr std::uintptr_t kMultiplayerVariable = std::uintptr_t(0x007906C8);   // "z.MPGame1"
// "bz1mpg".."bz4mpg" by the client session's game type.
constexpr std::uintptr_t kMultiplayerZones[] = {std::uintptr_t(0x00790690), std::uintptr_t(0x007906A8),
                                                std::uintptr_t(0x007906A0), std::uintptr_t(0x00790698)};

// ---- ReadAIGraph -------------------------------------------------------------------------------
constexpr std::uintptr_t kGraphExtension = std::uintptr_t(0x00790BB0);   // ".grp"

// ---- StartServer / AcceptPlayers ---------------------------------------------------------------
constexpr std::uintptr_t kLocalHost = std::uintptr_t(0x007906B8);     // "127.0.0.1"
constexpr std::uintptr_t kServerName = std::uintptr_t(0x007906B0);    // "Server"
constexpr std::uintptr_t kNetLogStream = std::uintptr_t(0x007C56B0);  // the network log's ostream
constexpr std::uintptr_t kAcceptedPlayer = std::uintptr_t(0x007908AC);   // "Accepted player "
constexpr std::uintptr_t kSessionListVtable = std::uintptr_t(0x0073FA4C);

// ---- ClearSaveFolder ---------------------------------------------------------------------------
constexpr std::uintptr_t kAllFiles = std::uintptr_t(0x007905B4);   // "*.*"

// ---- StartZone ---------------------------------------------------------------------------------
constexpr std::uintptr_t kEndOfGame = std::uintptr_t(0x00790664);       // "endofgame"
constexpr std::uintptr_t kZoneNotFound = std::uintptr_t(0x00790650);    // "Zone %s not found"
constexpr std::uintptr_t kMenuMap = std::uintptr_t(0x00790628);         // "ZoneMainMenuNew.mpr"
constexpr std::uintptr_t kMenuMob = std::uintptr_t(0x0079063C);         // "ZoneMainMenuNew.mob"
constexpr std::uintptr_t kMinuteHours = std::uintptr_t(0x0073F8A0);     // 1/60 (float)

// ---- New game, requests, LoadGame ---------------------------------------------------------------
constexpr std::uintptr_t kExecAutoexec = std::uintptr_t(0x007905F0);   // "exec autoexec.cfg"
constexpr std::uintptr_t kResourcesSingle = std::uintptr_t(0x00784818);   // "database.res"
constexpr std::uintptr_t kResourcesNetwork = std::uintptr_t(0x00790680);  // "databaseLMP.res"
constexpr std::uintptr_t kAutoplay = std::uintptr_t(0x00790670);          // "autoplay.apl"
constexpr std::uintptr_t kLoadedText = std::uintptr_t(0x007905B8);   // "Loaded "

// ---- After starting the sessions ---------------------------------------------------------------
constexpr std::uintptr_t kDefaultHeroName = std::uintptr_t(0x0078B0FC);   // "Human Hero"
constexpr std::uintptr_t kDefaultPartyName = std::uintptr_t(0x007906C4);  // "Bug"

// ---- The client's update from the server -------------------------------------------------------
constexpr std::uintptr_t kConstructionVariable = std::uintptr_t(0x00790384);   // "constr.current"
constexpr std::uintptr_t kBriefingSuffixQuest = std::uintptr_t(0x00790704);    // "q"
constexpr std::uintptr_t kBriefingSuffixK = std::uintptr_t(0x00790700);        // "k"
constexpr std::uintptr_t kQuestVariablePrefix = std::uintptr_t(0x00783D7C);    // "q."
constexpr std::uintptr_t kDot = std::uintptr_t(0x00783D78);                    // "."
// "b.smith.constr_1"... and "intro_1".."intro_4" by construction type.
constexpr std::uintptr_t kTypeVariables[4] = {std::uintptr_t(0x00790760), std::uintptr_t(0x00790744),
                                              std::uintptr_t(0x0079072C), std::uintptr_t(0x00790710)};
constexpr std::uintptr_t kTypeBriefings[4] = {std::uintptr_t(0x00790758), std::uintptr_t(0x0079073C),
                                              std::uintptr_t(0x00790724), std::uintptr_t(0x00790708)};

// ---- The saved multiplayer parties -------------------------------------------------------------
constexpr std::uintptr_t kPartyFilePattern = std::uintptr_t(0x00794C88);   // "\*.mp"
constexpr std::uintptr_t kRecordsVtable = std::uintptr_t(0x00743000);

} // namespace flowdata
