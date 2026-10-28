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
struct _RTL_CRITICAL_SECTION;   // CRITICAL_SECTION: the render device's lock

extern lib::StaticObject<GameFlow> g_gameFlow;   // world_game.cpp

// The interface manager (windows, the quest log, status text, the console window).
extern lib::StaticObject<CInterface3D> g_interface;   // ui_small_members.cpp

// The world tick (the server world's), advanced once per simulation step, and the client's tick counter and the
// fraction of its current tick (the client world's), advanced each frame.
int CurrentWorldTick();
int CurrentClientTick();
float ClientTickFraction();

extern lib::StaticObject<CWorldClient> g_clientWorld;   // world_construct.cpp

extern lib::StaticObject<CWorldServer> g_serverWorld;   // world_construct.cpp

// The client world's managers: the text manager (the game's string tables), the object
// manager (finds and creates engine objects by id), the figure manager (models and sprites), the texture, material,
// sound and particle managers, the light factory, the music system and the terrain (also the point lights' manager).
CTextManager* TheTextManager();
CMapObjectManager* TheObjectManager();
CFigureManager* TheFigureManager();
CTextureManager* TheTextureManager();
CMaterialManager* TheMaterialManager();
CSoundManager* TheSoundManager();
CMusicSystem* TheMusicSystem();
CParticleManager* TheParticleManager();
CLightFactory* TheLightFactory();
Terrain* TheTerrain();

// The scene renderer; its device object; the critical section held around the device's
// DirectDraw / Direct3D calls.
extern lib::StaticObject<CSceneRenderer> g_sceneRenderer;   // renderer_lifetime.cpp
CTextureDevice* TheRenderDevice();
extern _RTL_CRITICAL_SECTION g_deviceLock;   // renderer_lifetime.cpp

// The cursor. The map database. The server world's mission script and sector map. The camera.
extern lib::StaticObject<CCursor> g_cursor;   // ui_cursor_setup.cpp
extern lib::StaticObject<CMapDatabase> g_mapDatabase;   // map_database.cpp
CScript* TheMissionScript();
CSectorMap* TheSectorMap();
extern lib::StaticObject<CCamera> g_camera;   // camera_object.cpp

// The party manager (the parties by key, the named records and the class templates).
extern lib::StaticObject<PartyManager> g_partyManager;   // app_lifetime.cpp

// The registry key of the options and the game's folders (game_folders.cpp): where the game was started,
// the disc's (the base folder), the text resources' and the data folder (with its trailing backslash; DataFolderText()
// is its characters for printf-style arguments).
extern lib::StaticObject<std::string> g_registryRoot;
extern lib::StaticObject<std::string> g_startFolder;
extern lib::StaticObject<std::string> g_discFolder;
extern lib::StaticObject<std::string> g_textFolder;
extern lib::StaticObject<std::string> g_dataFolder;
inline const char* DataFolderText() { return g_dataFolder->c_str(); }
