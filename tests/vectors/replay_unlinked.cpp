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
#include "../../src/lib/mfc_exception.h"
#include "../../src/lib/mfc_resource.h"
#include "../../src/lib/mfc_state.h"
#include "../../src/lib/mfc_time.h"

#include <cstdio>
#include <cstdlib>
#include <list>

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
void ScriptDropEffect(CEngineObject*) EI_UNLINKED("ScriptDropEffect")

// script_runtime_helpers.cpp's AI map and console helpers (CSectorMap::Add and Remove: replay_path.cpp).
void CSectorMap::FindUnitsInRadius(const Vector3*, std::list<CUnit*>*, float)
    EI_UNLINKED("CSectorMap::FindUnitsInRadius")
// ai_movement.cpp's shape cache and passability marks (ai_shape_cache.cpp, ai_refresh.cpp).
CRefObject* UnitShapeCache::Get(void*) EI_UNLINKED("UnitShapeCache::Get")
std::uint32_t CAIMap::CellMark(void*, int, int, int, int) EI_UNLINKED("CAIMap::CellMark")
console::Stream& console::Stream::operator<<(double) EI_UNLINKED("console::Stream::operator<<(double)")

// The script loaders' and destructors' callees outside the interpreter: the object registry
// (base_manager.cpp), the streams (core_streams.cpp), the world's timed objects (world_server.cpp) and
// the script effects (script_effects.cpp needs the world).
CEngineObject* CBaseManager::FindObject(unsigned) const EI_UNLINKED("CBaseManager::FindObject")
std::string* CStream::ReadString(std::string*) EI_UNLINKED("CStream::ReadString")
void CStream::WriteString(std::string_view) EI_UNLINKED("CStream::WriteString")
void CWorldServer::RemoveTimedObject(unsigned) EI_UNLINKED("CWorldServer::RemoveTimedObject")
void ScriptPointLight::Load(ScriptStream*) EI_UNLINKED("ScriptPointLight::Load")
void ScriptFxSource::Load(ScriptStream*) EI_UNLINKED("ScriptFxSource::Load")

// The game CRT's ports (crt.cpp, crt_x87.cpp read the game CRT's data)
// that no replay reaches: srand (random.cpp), acos (quaternion.cpp).
void libcrt::Srand(unsigned) EI_UNLINKED("libcrt::Srand")
double libcrt::CiAcos(double) EI_UNLINKED("libcrt::CiAcos")

// The library's exceptions, runtime classes and resources (mfc_exception.cpp, mfc_state.cpp, mfc_resource.cpp).
int mfc::impl::CException::ReportError(unsigned, unsigned) EI_UNLINKED("mfc::impl::CException::ReportError")
void mfc::impl::AfxClassInit(mfc::impl::CRuntimeClass*) EI_UNLINKED("mfc::impl::AfxClassInit")
int mfc::impl::AfxLoadString(unsigned, char*, unsigned) EI_UNLINKED("mfc::impl::AfxLoadString")

// world_construct.cpp (the server world, g_serverWorld) is not linked: the scripts' replays see a zero world, as they
// saw the stand-in's zero bytes before C18t named it (no fixture fills it).
CWorldServer* TheServerWorld() {
    alignas(8) static unsigned char world[sizeof(CWorldServer)] = {};
    return reinterpret_cast<CWorldServer*>(world);
}

// The game CRT's rand (crt.cpp's port reads the image's per-thread seed) and the fatal error report (app.cpp).
int libcrt::Rand() EI_UNLINKED("libcrt::Rand")
int game::FatalError(const char*, ...) EI_UNLINKED("game::FatalError")

// The object lookup (world_server.cpp) and the sector map's Move (sector_map.cpp).
CMapObject* CWorldServer::FindObject(unsigned) EI_UNLINKED("CWorldServer::FindObject")
bool CSectorMap::Move(CMapObject*, const Vector3*) EI_UNLINKED("CSectorMap::Move")
