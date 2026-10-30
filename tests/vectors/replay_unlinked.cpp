// Functions the game sources linked into ei_vectors refer to but no replay reaches: their files would
// pull in the world, the renderer and the terrain. Each stops the replay naming itself.
#include "../../src/game/ai_map.h"
#include "../../src/game/ai_shape_cache.h"
#include "../../src/game/base_manager.h"
#include "../../src/game/console.h"
#include "../../src/game/core_streams.h"
#include "../../src/game/figure_mesh.h"
#include "../../src/game/map_spec.h"
#include "../../src/game/material.h"
#include "../../src/game/net_delta.h"
#include "../../src/game/script.h"
#include "../../src/game/sector_map.h"
#include "../../src/game/terrain_sector.h"
#include "../../src/game/unit.h"
#include "../../src/game/world.h"
#include "../../src/lib/crt.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {
[[noreturn]] void Unlinked(const char* what) {
    std::fprintf(stderr, "ei_vectors: %s is not linked into the replay\n", what);
    std::fflush(stderr);
    std::exit(3);
}
} // namespace

#define EI_UNLINKED(what) \
    { Unlinked(what); }

// The script effects' drop (script_effects.cpp needs the world).
void ScriptDropEffect(EngineObject*) EI_UNLINKED("ScriptDropEffect")

// ai_map.cpp's AI map and console helpers (SectorMap::Add and Remove: replay_path.cpp).
std::vector<Unit*> SectorMap::FindUnitsInRadius(const Vector3&, float)
    EI_UNLINKED("CSectorMap::FindUnitsInRadius")
// ai_movement.cpp's shape cache and passability marks (ai_unit_shape.cpp, ai_refresh.cpp).
RefObject* UnitShapeCache::Get(void*) EI_UNLINKED("UnitShapeCache::Get")
RefObject* UnitShapeCache::CreateShape(const ShapeCacheHandle*) EI_UNLINKED("UnitShapeCache::CreateShape")
std::uint32_t AIMap::CellMark(CellMarkState*, int, int, int, int) EI_UNLINKED("CAIMap::CellMark")
console::Stream& console::Stream::operator<<(double) EI_UNLINKED("console::Stream::operator<<(double)")

// The script loaders' and destructors' callees outside the interpreter: the object registry
// (base_manager.cpp), the streams (core_streams.cpp), the world's timed objects (world_server.cpp) and
// the script effects (script_effects.cpp needs the world).
EngineObject* BaseManager::FindObject(unsigned) const EI_UNLINKED("CBaseManager::FindObject")
std::string ByteStream::ReadString() EI_UNLINKED("CStream::ReadString")
void ByteStream::WriteString(std::string_view) EI_UNLINKED("CStream::WriteString")
void WorldServer::RemoveTimedObject(unsigned) EI_UNLINKED("CWorldServer::RemoveTimedObject")
void ScriptPointLight::Load(ByteStream*) EI_UNLINKED("ScriptPointLight::Load")
void ScriptFxSource::Load(ByteStream*) EI_UNLINKED("ScriptFxSource::Load")

// The game CRT's port that no replay reaches (crt.cpp reads the game CRT's data): srand (random.cpp).
void libcrt::Srand(unsigned) EI_UNLINKED("libcrt::Srand")

// world_create.cpp (the server world, g_serverWorld) is not linked: the scripts' replays see a zero world, as they
// saw the stand-in's zero bytes before C18t named it (no fixture fills it). A constructed WorldServer would pull in
// its class's vtable and with it the whole world, so the linker resolves the name to zero bytes of its size instead.
extern "C" {
alignas(WorldServer) unsigned char ei_zero_server_world[sizeof(WorldServer)];
}
#pragma comment(linker, "/alternatename:?g_serverWorld@@3VWorldServer@@A=_ei_zero_server_world")

// The game CRT's rand (crt.cpp's port reads the image's per-thread seed) and the fatal error report (app.cpp).
int libcrt::Rand() EI_UNLINKED("libcrt::Rand")
int game::FatalError(const char*, ...) EI_UNLINKED("game::FatalError")

// The object lookup (world_server.cpp) and the sector map's Move (sector_map.cpp).
MapObject* WorldServer::FindObject(unsigned) EI_UNLINKED("CWorldServer::FindObject")
bool SectorMap::Move(MapObject*, const Vector3&) EI_UNLINKED("CSectorMap::Move")
