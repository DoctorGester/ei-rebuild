#pragma once

// The game's global objects and tick counters, named once (they were per-file copies): the game flow (007AFE20),
// the interface manager (007C7D50), the client and server worlds and what they hold (the ticks, the managers, the
// mission script, the sector map), the scene renderer, its render device and lock, the cursor, the map database,
// the camera, the party manager and the folders. The shared empty string (00796E48) is mfc::impl::PchNil() (lib/mfc_string.h).
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

// 007AFE20: the game flow object (its state machine, sessions and requests).
GameFlow* TheGameFlow();   // g_gameFlow (world_game.cpp)

// 007C7D50: the interface manager (windows, the quest log, status text, the console window).
CInterface3D* TheInterface();   // g_interface (ui_small_members.cpp)

// The world tick (the server world's), advanced once per simulation step, and the client's tick counter and the
// fraction of its current tick (the client world's), advanced each frame (world_singletons.cpp).
int CurrentWorldTick();
int CurrentClientTick();
float ClientTickFraction();

// 007AF5B8: the client world (the local player's view of the map and its objects).
inline CWorldClient* TheClientWorld() { return ei::GamePointer<CWorldClient>(0x007AF5B8); }

// 007AFF60: the server world (the simulation: objects, players, timed objects).
inline CWorldServer* TheServerWorld() { return ei::GamePointer<CWorldServer>(0x007AFF60); }

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

// 007AF060: the scene renderer; its device object (world_singletons.cpp); 007AEE78: the critical section held
// around the device's DirectDraw / Direct3D calls.
inline CSceneRenderer* TheSceneRenderer() { return ei::GamePointer<CSceneRenderer>(0x007AF060); }
CTextureDevice* TheRenderDevice();
inline _RTL_CRITICAL_SECTION* TheDeviceLock() { return ei::GamePointer<_RTL_CRITICAL_SECTION>(0x007AEE78); }

// 007C7BD8: the cursor. 0079BF00: the map database. The server world's mission script and sector map
// (world_singletons.cpp). 0079B2A8: the camera.
CCursor* TheCursor();   // g_cursor (ui_cursor_setup.cpp)
inline CMapDatabase* TheMapDatabase() { return ei::GamePointer<CMapDatabase>(0x0079BF00); }
CScript* TheMissionScript();
CSectorMap* TheSectorMap();
inline CCamera* TheCamera() { return ei::GamePointer<CCamera>(0x0079B2A8); }

// 007AA5A0: the party manager (the parties by key, the named records and the class templates).
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
