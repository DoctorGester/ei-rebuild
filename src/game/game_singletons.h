#pragma once

// The game's global objects and tick counters, named once (they were per-file copies): the game flow,
// the interface manager, the client and server worlds and what they hold (the ticks, the managers, the
// mission script, the sector map), the scene renderer, its render device and lock, the cursor, the map database,
// the camera, the party manager and the folders. The shared empty string is mfc::impl::PchNil() (lib/mfc_string.h).
// The global random generator is IsaacRandom::Global().

#include "game_data.h"
#include "lib/mfc.h"
#include "lib/static_object.h"

struct GameFlow;
class CInterface3D;
class CWorldClient;
class CWorldServer;
class CTextManager;
class CBaseManager;
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

GameFlow* TheGameFlow();   // g_gameFlow

// The interface manager (windows, the quest log, status text, the console window).
CInterface3D* TheInterface();   // g_interface

// The world tick (the server world's), advanced once per simulation step, and the client's tick counter and the
// fraction of its current tick (the client world's), advanced each frame.
int CurrentWorldTick();
int CurrentClientTick();
float ClientTickFraction();

CWorldClient* TheClientWorld();   // g_clientWorld

CWorldServer* TheServerWorld();   // g_serverWorld

// The client world's managers: the text manager (the game's string tables), the object
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

// The scene renderer; its device object; the critical section held around the device's
// DirectDraw / Direct3D calls.
CSceneRenderer* TheSceneRenderer();   // g_sceneRenderer
CTextureDevice* TheRenderDevice();
_RTL_CRITICAL_SECTION* TheDeviceLock();   // g_deviceLock

// The cursor. The map database. The server world's mission script and sector map. The camera.
CCursor* TheCursor();   // g_cursor
CMapDatabase* TheMapDatabase();   // g_mapDatabase
CScript* TheMissionScript();
CSectorMap* TheSectorMap();
CCamera* TheCamera();   // g_camera

// The party manager (the parties by key, the named records and the class templates).
PartyManager* ThePartyManager();   // g_partyManager

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
