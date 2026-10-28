// Functions the game sources linked into ei_vectors refer to but no replay reaches: their files would
// pull in the world, the renderer and the terrain. Each stops the replay naming itself.
#include "../../src/game/ai_map.h"
#include "../../src/game/ai_shape_cache.h"
#include "../../src/game/base_manager.h"
#include "../../src/game/console.h"
#include "../../src/game/core_streams.h"
#include "../../src/game/figure_mesh.h"
#include "../../src/game/game_arrays.h"
#include "../../src/game/map_spec.h"
#include "../../src/game/material.h"
#include "../../src/game/net_delta.h"
#include "../../src/game/record_arrays.h"
#include "../../src/game/scene_loader.h"
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

// game_arrays.cpp's other element types.
void EffectParticle::ReleaseSource() EI_UNLINKED("EffectParticle::ReleaseSource")
void aligned::ArrayAt8Free(void*) EI_UNLINKED("aligned::ArrayAt8Free")
void ScriptQuestObjectiveDestruct(ScriptQuestObjective*) EI_UNLINKED("ScriptQuestObjectiveDestruct")

// game_arrays.cpp's and game_lists.cpp's script effects (script_effects.cpp needs the world).
void ScriptPointLight::ScriptPointLightDestruct() EI_UNLINKED("ScriptPointLight::ScriptPointLightDestruct")
void ScriptParticleSource::ScriptParticleSourceDestruct() EI_UNLINKED("ScriptParticleSource::ScriptParticleSourceDestruct")
void ScriptFxSource::ScriptFxSourceDestruct() EI_UNLINKED("ScriptFxSource::ScriptFxSourceDestruct")
void ScriptLightning::ScriptLightningDestruct() EI_UNLINKED("ScriptLightning::ScriptLightningDestruct")

// record_arrays.cpp's other element types.
void scene::InitFigurePart(CPlainFigureMesh*) EI_UNLINKED("scene::InitFigurePart")
void scene::InitHelper(CMaterial*) EI_UNLINKED("scene::InitHelper")
void DeltaFieldElements::Construct(DeltaField*, int) EI_UNLINKED("DeltaFieldElements::Construct")
void DeltaFieldElements::Destruct(DeltaField*, int) EI_UNLINKED("DeltaFieldElements::Destruct")
void SectorElements::Construct(Sector*, int) EI_UNLINKED("SectorElements::Construct")
void terrain::RecordElements::Construct(terrain::SectorVertex*, int) EI_UNLINKED("terrain::RecordElements::Construct")
void terrain::RecordElements::Destruct(terrain::SectorVertex*, int) EI_UNLINKED("terrain::RecordElements::Destruct")
void scene::FaceGroup::ConstructElements(scene::FaceGroup*, int) EI_UNLINKED("scene::FaceGroup::ConstructElements")
void scene::FaceGroup::Destruct() EI_UNLINKED("scene::FaceGroup::Destruct")
void scene::FaceGroupArrayDestruct(scene::FaceGroupArray*) EI_UNLINKED("scene::FaceGroupArrayDestruct")
void scene::InitSceneMesh(scene::SceneMesh*) EI_UNLINKED("scene::InitSceneMesh")
void scene::SceneNode::Construct() EI_UNLINKED("scene::SceneNode::Construct")
void scene::SceneNode::Destruct() EI_UNLINKED("scene::SceneNode::Destruct")

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
void ScriptPointLight::ScriptPointLightLoad(ScriptStream*) EI_UNLINKED("ScriptPointLight::ScriptPointLightLoad")
void ScriptFxSource::ScriptFxSourceLoad(ScriptStream*) EI_UNLINKED("ScriptFxSource::ScriptFxSourceLoad")
void ScriptLightningElementsDestruct(ScriptLightning*, int) EI_UNLINKED("ScriptLightningElementsDestruct")

// The game CRT's ports (crt.cpp, crt_x87.cpp read the game CRT's data)
// that no replay reaches: the node allocator (stlp.cpp), qsort (script_group.cpp), srand (random.cpp), acos
// (quaternion.cpp).
void* libcrt::NodeAllocate(std::size_t) EI_UNLINKED("libcrt::NodeAllocate")
void libcrt::NodeDeallocate(void*, std::size_t) EI_UNLINKED("libcrt::NodeDeallocate")
void libcrt::Qsort(void*, std::size_t, std::size_t, int(__cdecl*)(const void*, const void*)) EI_UNLINKED("libcrt::Qsort")
void libcrt::Srand(unsigned) EI_UNLINKED("libcrt::Srand")
double libcrt::CiAcos(double) EI_UNLINKED("libcrt::CiAcos")

// The library's exceptions and resources (mfc_archive.cpp).
int mfc::impl::CException::ReportError(unsigned, unsigned) EI_UNLINKED("mfc::impl::CException::ReportError")
void mfc::impl::ThrowArchiveException(int, const char*) EI_UNLINKED("mfc::impl::ThrowArchiveException")
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
