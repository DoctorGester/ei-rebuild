#pragma once

// The game's global objects and tick counters, named once (they were per-file copies): the game flow,
// the interface manager, the client and server worlds and what they hold (the ticks, the managers, the
// mission script, the sector map), the scene renderer, its render device and lock, the cursor, the map database,
// the camera, the party manager and the folders. The shared empty string is mfc::impl::PchNil() (lib/mfc_string.h).
// The global random generator is IsaacRandom::Global() (random.h).

#include "game_data.h"
#include "lib/mfc.h"
#include "lib/static_object.h"

struct GameFlow;      // world.h
class CInterface3D;   // ui.h
class CWorldClient;   // world.h
class CWorldServer;   // world.h
class CTextManager;   // item_db_text.h
class CBaseManager;   // base_manager.h
class CTextureManager;   // texture.h
class CFigureManager;   // figure_manager.h
class CParticleManager;   // effects.h
class CLightFactory;   // spell.h
class Terrain;   // terrain.h
class CMaterialManager;   // material.h
struct CSoundManager;   // sound.h
struct CMusicSystem;   // sound.h
class CTextureDevice;   // texture.h
class CCursor;   // ui_cursor.h
struct CMapDatabase;   // object_tables.h
class CScript;   // script.h
class CSectorMap;   // sector_map.h
class CSceneRenderer;   // model.h
class CCamera;   // camera.h
struct PartyManager;   // party_manager.h
struct _RTL_CRITICAL_SECTION;   // CRITICAL_SECTION: the render device's lock

// The game flow object (its state machine, sessions and requests).
GameFlow* TheGameFlow();   // g_gameFlow (world_game.cpp)

// The interface manager (windows, the quest log, status text, the console window).
CInterface3D* TheInterface();   // g_interface (ui_small_members.cpp)

// The world tick (the server world's), advanced once per simulation step, and the client's tick counter and the
// fraction of its current tick (the client world's), advanced each frame (world_singletons.cpp).
int CurrentWorldTick();
int CurrentClientTick();
float ClientTickFraction();

// The client world (the local player's view of the map and its objects).
CWorldClient* TheClientWorld();   // g_clientWorld (world_construct.cpp)

// The server world (the simulation: objects, players, timed objects).
CWorldServer* TheServerWorld();   // g_serverWorld (world_construct.cpp)

// The client world's managers (world_singletons.cpp): the text manager (the game's string tables), the object
// manager (finds and creates engine objects by id), the figure manager (models and sprites), the texture, material,
// sound and particle managers, the light factory, the music system and the terrain (also the point lights' manager).
CTextManager* TheTextManager();
CBaseManager* TheObjectManager();
CFigureManager* TheFigureManager();
CTextureManager* TheTextureManager();
CMaterialManager* TheMaterialManager();
CSoundManager* TheSoundManager();
CMusicSystem* TheMusicSystem();
CParticleManager* TheParticleManager();
CLightFactory* TheLightFactory();
Terrain* TheTerrain();

// The scene renderer; its device object (world_singletons.cpp); the critical section held around the device's
// DirectDraw / Direct3D calls.
CSceneRenderer* TheSceneRenderer();   // g_sceneRenderer (renderer_lifetime.cpp)
CTextureDevice* TheRenderDevice();
_RTL_CRITICAL_SECTION* TheDeviceLock();   // g_deviceLock (renderer_lifetime.cpp)

// The cursor. The map database. The server world's mission script and sector map (world_singletons.cpp). The
// camera.
CCursor* TheCursor();   // g_cursor (ui_cursor_setup.cpp)
CMapDatabase* TheMapDatabase();   // g_mapDatabase (map_database.cpp)
CScript* TheMissionScript();
CSectorMap* TheSectorMap();
CCamera* TheCamera();   // g_camera (camera_object.cpp)

// The party manager (the parties by key, the named records and the class templates).
PartyManager* ThePartyManager();   // g_partyManager (app_lifetime.cpp)

// The registry key of the options and the game's folders (CStrings; game_folders.cpp): where the game was started,
// the disc's (the base folder), the text resources' and the data folder (with its trailing backslash; DataFolderText()
// is its characters for printf-style arguments).
extern lib::StaticObject<mfc::CString> g_registryRoot;
extern lib::StaticObject<mfc::CString> g_startFolder;
extern lib::StaticObject<mfc::CString> g_discFolder;
extern lib::StaticObject<mfc::CString> g_textFolder;
extern lib::StaticObject<mfc::CString> g_dataFolder;
inline mfc::CString& TheRegistryRoot() { return *g_registryRoot; }
inline mfc::CString& TheStartFolder() { return *g_startFolder; }
inline mfc::CString& TheDiscFolder() { return *g_discFolder; }
inline mfc::CString& TheTextFolder() { return *g_textFolder; }
inline mfc::CString& TheDataFolder() { return *g_dataFolder; }
inline const char* DataFolderText() { return TheDataFolder(); }
