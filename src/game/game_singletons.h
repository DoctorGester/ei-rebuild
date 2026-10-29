#pragma once

// The game's global objects and tick counters, named once (they were per-file copies): the game flow,
// the interface manager, the client and server worlds and what they hold (the ticks, the managers, the
// mission script, the sector map), the scene renderer, its render device and lock, the cursor, the map database,
// the camera, the party manager and the folders.
// The global random generator is g_random.

#include <string>
#include "lib/static_object.h"


struct GameFlow;
class CInterface3D;
class CWorldClient;
class CWorldServer;
class CTextManager;
class CBaseManager;
struct CMapObjectManager;
class CTextureManager;
class CFigureManager;
class CParticleManager;
class CLightFactory;
class Terrain;
class CMaterialManager;
struct CSoundManager;
struct CMusicSystem;
class CTextureDevice;
class CCursor;
struct CMapDatabase;
class CScript;
class CSectorMap;
class CSceneRenderer;
class CCamera;
struct PartyManager;

extern GameFlow g_gameFlow;   // world_game.cpp

// The interface manager (windows, the quest log, status text, the console window).
extern CInterface3D g_interface;   // ui_small_members.cpp

extern CWorldClient g_clientWorld;   // world_construct.cpp

extern CWorldServer g_serverWorld;   // world_construct.cpp

// The scene renderer.
extern lib::StaticObject<CSceneRenderer> g_sceneRenderer;   // renderer_lifetime.cpp

// The cursor. The map database. The server world's mission script and sector map. The camera.
extern CCursor g_cursor;   // ui_cursor_setup.cpp
extern CMapDatabase g_mapDatabase;   // map_database.cpp
CScript* TheMissionScript();
extern lib::StaticObject<CCamera> g_camera;   // camera_object.cpp

// The party manager (the parties by key, the named records and the class templates).
extern PartyManager g_partyManager;   // app_lifetime.cpp

// The registry key of the options and the game's folders (game_folders.cpp): where the game was started,
// the disc's (the base folder), the text resources' and the data folder (with its trailing backslash; DataFolderText()
// is its characters for printf-style arguments).
extern std::string g_registryRoot;
extern std::string g_startFolder;
extern std::string g_discFolder;
extern std::string g_textFolder;
extern std::string g_dataFolder;
inline const char* DataFolderText() { return g_dataFolder.c_str(); }
