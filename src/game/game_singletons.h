#pragma once

// The game's global objects and tick counters, named once (they were per-file copies): the game flow,
// the interface manager, the client and server worlds and what they hold (the ticks, the managers, the
// mission script, the sector map), the scene renderer, its render device and lock, the cursor, the map database,
// the camera, the party manager and the folders.
// The global random generator is g_random.

#include <string>

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
class SceneRenderer;
class Camera;
struct PartyManager;

extern GameFlow g_gameFlow;   // world_game.cpp

// The interface manager (windows, the quest log, status text, the console window).
extern Interface3D g_interface;   // ui_interface_glue.cpp

extern WorldClient g_clientWorld;   // world_construct.cpp

extern WorldServer g_serverWorld;   // world_construct.cpp

// The scene renderer.
extern SceneRenderer g_sceneRenderer;   // renderer_scene.cpp

// The cursor. The map database. The server world's mission script and sector map. The camera.
extern Cursor g_cursor;   // ui_cursor_setup.cpp
extern MapDatabase g_mapDatabase;   // map_database.cpp
Script* TheMissionScript();
extern Camera g_camera;   // camera_object.cpp

// The party manager (the parties by key, the named records and the class templates).
extern PartyManager g_partyManager;   // party_manager.cpp

// The registry key of the options and the game's folders (game_folders.cpp): where the game was started,
// the disc's (the base folder), the text resources' and the data folder (with its trailing backslash; DataFolderText()
// is its characters for printf-style arguments).
extern std::string g_registryRoot;
extern std::string g_startFolder;
extern std::string g_discFolder;
extern std::string g_textFolder;
extern std::string g_dataFolder;
inline const char* DataFolderText() { return g_dataFolder.c_str(); }
