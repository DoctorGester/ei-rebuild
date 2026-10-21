#pragma once

// The game's global objects and tick counters, named once (they were per-file copies): the game flow (007AFE20),
// the interface manager (007C7D50), the world tick (007AFFC4), the client and server worlds, the managers
// (texts, objects, textures, sounds, music), the render device and its lock, the cursor, the map database, the
// mission script, the sector map, the scene renderer, the camera and the client tick (007AF61C).
// The global random generator is IsaacRandom::Global() (random.h).

#include "game_data.h"

struct GameFlow;      // world.h
class CInterface3D;   // ui.h
class CWorldClient;   // world.h
class CWorldServer;   // world.h
class CTextManager;   // item_db_text.h
class CBaseManager;   // base_manager.h
class CTextureManager;   // texture.h
struct CSoundManager;   // sound.h
struct CMusicSystem;   // sound.h
class CTextureDevice;   // texture.h
class CCursor;   // ui_cursor.h
struct CMapDatabase;   // object_tables.h
class CScript;   // script.h
class CSectorMap;   // sector_map.h
class CSceneRenderer;   // model.h
class CCamera;   // camera.h
struct _RTL_CRITICAL_SECTION;   // CRITICAL_SECTION: the render device's lock

// 007AFE20: the game flow object (its state machine, sessions and requests).
inline GameFlow* TheGameFlow() { return ei::GamePointer<GameFlow>(0x007AFE20); }

// 007C7D50: the interface manager (windows, the quest log, status text, the console window).
inline CInterface3D* TheInterface() { return ei::GamePointer<CInterface3D>(0x007C7D50); }

// 007AFFC4: the world tick, advanced once per simulation step.
inline int CurrentWorldTick() { return ei::GameData<const int>(0x007AFFC4); }

// 007AF61C: the client's tick counter, advanced by the client world each frame.
inline int CurrentClientTick() { return ei::GameData<const volatile int>(0x007AF61C); }

// 007AF5B8: the client world (the local player's view of the map and its objects).
inline CWorldClient* TheClientWorld() { return ei::GamePointer<CWorldClient>(0x007AF5B8); }

// 007AFF60: the server world (the simulation: objects, players, timed objects).
inline CWorldServer* TheServerWorld() { return ei::GamePointer<CWorldServer>(0x007AFF60); }

// 007AF788: the text manager (the game's string tables). 007AF5F8: the object manager (finds and creates engine
// objects by id). 007AF744: the texture manager. 007AF784: the sound manager. 007AF760: the music system.
inline CTextManager* TheTextManager() { return ei::GameData<CTextManager* const>(0x007AF788); }
inline CBaseManager* TheObjectManager() { return ei::GameData<CBaseManager* const>(0x007AF5F8); }
inline CTextureManager* TheTextureManager() { return ei::GameData<CTextureManager* const>(0x007AF744); }
inline CSoundManager* TheSoundManager() { return ei::GameData<CSoundManager* const>(0x007AF784); }
inline CMusicSystem* TheMusicSystem() { return ei::GameData<CMusicSystem* const>(0x007AF760); }

// 007AF080: the render device object; 007AEE78: the critical section held around its DirectDraw / Direct3D calls;
// 007AF060: the scene renderer.
inline CTextureDevice* TheRenderDevice() { return ei::GameData<CTextureDevice* const>(0x007AF080); }
inline _RTL_CRITICAL_SECTION* TheDeviceLock() { return ei::GamePointer<_RTL_CRITICAL_SECTION>(0x007AEE78); }
inline CSceneRenderer* TheSceneRenderer() { return ei::GamePointer<CSceneRenderer>(0x007AF060); }

// 007C7BD8: the cursor. 0079BF00: the map database. 007C27D4: the mission script. 007B01F4: the sector map.
// 0079B2A8: the camera.
inline CCursor* TheCursor() { return ei::GamePointer<CCursor>(0x007C7BD8); }
inline CMapDatabase* TheMapDatabase() { return ei::GamePointer<CMapDatabase>(0x0079BF00); }
inline CScript* TheMissionScript() { return ei::GamePointer<CScript>(0x007C27D4); }
inline CSectorMap* TheSectorMap() { return ei::GamePointer<CSectorMap>(0x007B01F4); }
inline CCamera* TheCamera() { return ei::GamePointer<CCamera>(0x0079B2A8); }
