#pragma once

// The game's global objects, named once (they were per-file copies): the game flow, the interface manager, the client
// and server worlds and what they hold (the managers, the mission script, the sector map), the cursor, the map
// database and the party manager. The engine's (the scene renderer, the camera, the folders) are in
// engine/core/engine_globals.h, included here. The global random generator is g_random.

#include "engine/core/engine_globals.h"

struct GameFlow;
class Interface3D;
class WorldClient;
class WorldServer;
class TextManager;
class BaseManager;
struct MapObjectManager;
class TextureManager;
class FigureManager;
class ParticleManager;
class LightFactory;
namespace terrain { class Terrain; }
class MaterialManager;
struct SoundManager;
struct MusicSystem;
class TextureDevice;
class Cursor;
struct MapDatabase;
class Script;
class SectorMap;
struct PartyManager;

extern GameFlow g_gameFlow;   // app/game_flow.cpp

// The interface manager (windows, the quest log, status text, the console window).
extern Interface3D g_interface;   // ui_interface_input.cpp

extern WorldClient g_clientWorld;   // world_create.cpp

extern WorldServer g_serverWorld;   // world_create.cpp

// The cursor. The map database. The server world's mission script and sector map.
extern Cursor g_cursor;   // ui_cursor.cpp
extern MapDatabase g_mapDatabase;   // map_database.cpp
Script* TheMissionScript();

// The party manager (the parties by key, the named records and the class templates).
extern PartyManager g_partyManager;   // party_manager.cpp
