#pragma once

// The game flow: the requests the main loop acts on (main menu, new game, load, zone changes), the saves, the
// multiplayer sessions' start and zone switches, and the quests and briefings of the zones.

#include "game/players/party.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

enum class ZoneMode;   // world.h
namespace net {
struct NetAddress;
class VariableMessage;
enum class PauseMode : std::uint8_t;   // net/net_session.h
}

struct GameFlow {
    // Saves the game into saves\<slot>\ (single player only): empties the folder, copies the visited zones' files
    // from saves\current, writes scenario.sav and, outside the cutscene zones, mission.sav. Without scenario.sav the
    // game goes back to the main menu. Returns 1 when saved.
    int SaveGame(const std::string& slot);
    // Fills the zone table once, while it is empty: for every "*.mq" file of the maps folder, its title is registered
    // as a quest folder (RegisterQuests); a zone the map database knows gets an entry {the title, the record's map
    // name, the allod}: 0 / 1 / 2 by the three map name lists, else 3.
    void BuildZoneTable();
    // Used each network step. The local player's "z.MPGame1" variable, set to 1 or removed: on a client unless the
    // client session's field is active; on the host when more than one player is in (zoneSwitchStarted) or unless the client's
    // field is active. Nothing without an active client session.
    void UpdateMultiplayerVariable();
    // The multiplayer start zone for the client session's game type: "bz2mpg".."bz4mpg" for
    // 1..3, else "bz1mpg".
    std::string MultiplayerZone();
    // Starts a multiplayer game: the party manager is reset, a single party record gets the unit id
    // 0x3B9AF5CE, the party outside a game is sent to the start zone with "z.MPGame1" = 1 and
    // redeployed; the flow loads that zone (state 4) and the client's game state request becomes kind 1.
    void BeginMultiplayerGame();
    // The local player has seen `briefing`: for a name ending in "_N" the zone's quests are loaded first
    // (with the name without "_N"), then BriefingVariable's variable is set to 1.
    void MarkBriefing(const char* briefing);
    // With an active client session: the local player's party is copied into the party outside a game,
    // redeployed by the party manager, its map transfer sent, then the flow's update.
    void ReturnLocalParty();
    // With an active client session: the flow's update, then an answered game state request (kind 3)
    // becomes a new one of kind 0.
    void UpdateAndAcknowledge();
    void RequestBriefing(std::string_view briefing);
    void ForgetBriefing(const char* briefing);
    void RecreateClientSession();
    // Leaves through exit `exit`: with a live client session the flow switches the multiplayer zone, otherwise the
    // local client takes the exit.
    void LeaveThroughExit(int exit);
    // Clears the sessions-open flag, releases both network sessions (server, then client), sets the state to 1.
    void CloseSessions();
    // A network zone that could not be entered (StartNetworkZone 0): the warning "Zone <name> cannot be entered", then
    // state 7, which ProcessRequest takes as CloseSessions and the main menu.
    void NetworkZoneNotEntered(std::string_view zone);
    // Used by the menus and the interface on leaving a network game. As CloseSessions without the
    // state: cleared, the server and the client session released.
    void DropSessions();
    // Used by the hero screens. DropSessions, then the resources go back to the local database
    // (UseResources(false)).
    void EndSession();
    // The client's variable `record` (an element of ClientSession::variables) changed: "q." variables
    // go to the quest log and the client world. Only the argument is read.
    static void OnVariableChanged(const net::VariableMessage* record);
    // Registers a quest folder once: the flow loads its quests, then with "<folder>\quest.reg" in the
    // texts the quest table reads it, the map database loads "<folder>\map.txt" and the folder joins the
    // list.
    void RegisterQuests(const std::string& folder);
    // The multiplayer zone switch, in two calls: the first (zoneSwitchStarted clear) resets the host's
    // connections, takes the party (player 0's records when its state has a party, else the outside
    // party) for multiplayerZone, asks for game state 3 and requests state 6 (host) or 5 (client, to its peer's
    // zone); the second (zoneSwitchStarted set) asks for state 1 and the start zone again, and the host drops its
    // client.
    void SwitchMultiplayerZone();
    // Hosts a multiplayer game: new server (game type) and client sessions, the client connects to
    // 127.0.0.1 with the password (20 bytes) as hello, then the server's name ("Server" when empty),
    // maxPlayers (also the local client's), the pause mode, the companions a player may add (ours), password,
    // listing (not `unlisted`) and masterKey are set (both between calls patched by Rebirth).
    void StartServer(std::string_view name, int maxPlayers, net::PauseMode pauseMode, int companions, int gameType,
                     std::string_view password, bool unlisted, int masterKey);
    void StartClient();
    void JoinServer(const net::NetAddress* address, std::string_view password);
    // The flow's requests: the state (state) the flow's update acts on next, the zone or save name
    // (requestTarget) and, for 5 and 6, a copy of a party (requestParty).
    void RequestState3();
    void RequestLoad(std::string_view name);                              // 2
    void RequestZoneChange(std::string_view zone);                        // 4
    void RequestPartyZone5(std::string_view zone, Party* party);
    void RequestPartyZone6(std::string_view zone, Party* party);
    // Starts `zone`: the current zone is torn down; mode None takes the mode from the zone record's kind
    // (0 Battle, 1 Edge, 2 Base), "endofgame" is EndOfGame, MainMenu loads the main menu zone (also when the
    // zone is unknown, after a warning and closing the network sessions); otherwise the party enters
    // the zone and the clock follows its "gtime". The mode becomes the campaign mode (the server world's
    // zoneMode). Rebirth's AllowScriptsInMainMenu sets the server's scriptsActive in the menu.
    void StartZone(std::string_view zone, ZoneMode mode);
    // A new game: "exec autoexec.cfg" on the console, the party manager's new game, saves\current
    // emptied, then the party's zone started (mode 0; a tool's own scene names it, app::g_toolScene). Rebirth's
    // AutoExec runs addon's AutoExec lines first.
    void StartNewGame();
    // Acts on the pending request (state, then cleared): the zone being left is saved, the interface
    // closes its windows, the zone is torn down and the resource database follows the network state;
    // then 1 main menu, 2 load the save named in requestTarget (two ticks first), 3 new game (StartNewGame), 4 the zone in requestTarget, 5 / 6 multiplayer zone
    // changes with the party copy in requestParty (6 whose zone cannot be entered: NetworkZoneNotEntered, then the
    // flow's update again), 7 CloseSessions first, then as 1. Ends with the network pump.
    void ProcessRequest();
    // The resource database follows `network`: "databaseLMP.res" in a network game, else "database.res".
    // When the name differs from the last one ("database.res" at first) the databases are switched and
    // the difficulty is saved and cleared (network) or taken back from it.
    void UseResources(bool network);
    // Each start of the game (app/main_loop.cpp, StartGame), which reads the databases from "database.res" and the map
    // database again: the zone table, the quest folders and the quest text files forgotten, the resources
    // "database.res".
    void Restart();
    // Loads the game saved in saves\<slot>\ (not in multiplayer, or kind 3): "exec autoexec.cfg",
    // saves\current emptied, scenario.sav read back (the party; by version the input, scripts and
    // globals), the visited zones of the current map copied back (the others marked not visited), then
    // the zone: from mission.sav (kinds 0, 2) or started afresh. Without scenario.sav the main menu
    // starts. Returns 1 when loaded. Rebirth's AutoExec runs addon's AutoExec lines first.
    int LoadGame(const std::string& slot);
    // After the sessions start: the party step (1), the variables of the party outside a game emptied,
    // its members reset, and the client's host record takes the hero's and party's
    // names (defaults "Human Hero", "Bug"). Rebirth's PersistentGlobalVars keeps the "gvar." variables.
    void AfterServerStart();
    // A multiplayer client's update from the server: the quest texts unloaded, the local player's
    // variables replaced ("constr.current" kept), the zone's quests, the game type's construction
    // variable and briefings (at least 1), the seen briefings, the quest folders of "q." variables, then
    // the server's variables applied in stamp order, and the multiplayer flag variable. Rebirth's
    // PersistentGlobalVars keeps the "gvar." ones.
    void UpdateFromServer();

    class Party* RequestParty() { return &requestParty; }

    // saves\current emptied, the multiplayer zone (multiplayerZone) named; a host without an active client
    // session starts it when there is a name (NetworkZoneNotEntered when it cannot be entered).
    void SetNetworkZone(const char* zone);

    int state = 0;                        // the pending request (1..7; ProcessRequest runs it)
    std::string requestTarget;            // the request's zone or save name
    Party requestParty;
    std::string multiplayerZone;          // the multiplayer zone (SwitchMultiplayerZone)
    char zoneSwitchStarted = 0;           // set by SwitchMultiplayerZone, cleared by the game's start
    bool networkResources = false;        // the databases in use are "databaseLMP.res" (UseResources)
};

extern GameFlow g_gameFlow;

void RequestZone(std::string_view zone, int entrance);

// With the briefing texts loaded: every quest text file loaded so far (g_questFiles) is unloaded and
// the list emptied.
void UnloadQuestTexts();
extern std::vector<std::string> g_questFiles;
// The quest folders registered (GameFlow::RegisterQuests).
extern std::vector<std::string> g_questFolders;

// Loads the quest texts of `zone`: false without the briefing texts or when the maps folder has no
// "<zone>.mq". A running game server first lists the file's bytes under "<zone>.mq" for its clients
// (ShareFile); then a path not yet among the quest files joins them and the briefing texts add it as a
// source.
bool LoadZoneQuests(std::string_view zone);

bool NameInList(const char* const* list, std::string_view name);

void ClearSaveFolder(const std::string& path);

// For the zone table entries with `id`, grouped by their second name in order of first appearance: one
// random entry per group (never the one whose first name is `exclude` when there is a choice; one
// IsaacRandom draw per group) has its first name returned.
std::vector<std::string> PickZones(int id, const char* exclude);

// The mercenary who speaks `briefing` (the second word of the first "#show" line of
// section "briefing <name>" that is not "Hero"), or "". The briefing's
// variable, "b.<speaker>.<briefing>" or, without a speaker, "b.elder.<briefing>".
std::string BriefingSpeakerName(const std::string& briefing);
std::string BriefingVariable(const std::string& briefing);
// The multiplayer peer's map (the client's session field or the server's), replaced by the first line of its "quest
// <name>" section when there is one; "" without a peer.
std::string SessionMapName();
// Chooses the loading movie for zone `zone` (may be null) by its allod in the map database:
// progres2.bik for gipat, progres3.bik for ingos, progres4.bik for suslanger, else progres.bik
// (Rebirth's FixProgressNames: progres2.bik for gipat2, progres5.bik for jigran); the zones gz1g and
// bz7g (case-insensitive) take progres1.bik.
void SelectLoadingMovie(std::optional<std::string_view> zone);

// The save screens: 1000000 plus the sizes (low dwords) of the files in saves\current.
int CurrentSaveSize();

// Every resource database (units with their sound file counts, skills, spell tables, items) emptied, then
// loaded from `name`.
void SwitchResourceDatabases(const std::string& name);

