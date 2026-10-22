// Functions the game sources linked into ei_vectors refer to but no replay reaches: their files would
// pull in the world, the renderer and the terrain. Each stops the replay naming itself (as an
// EI_ORIGINAL forwarder without a rewrite does, hook_stub.cpp).
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
void __fastcall aligned::ArrayAt8Free(void*) EI_UNLINKED("aligned::ArrayAt8Free")
void __fastcall aligned::ArrayAt4Free(void*) EI_UNLINKED("aligned::ArrayAt4Free")
void __fastcall ScriptQuestObjectiveDestruct(ScriptQuestObjective*) EI_UNLINKED("ScriptQuestObjectiveDestruct")
void __fastcall zone::ExitDestruct(ZoneExit*) EI_UNLINKED("zone::ExitDestruct")
void __fastcall zone::ZoneRecordDestruct(MapRecord*) EI_UNLINKED("zone::ZoneRecordDestruct")

// game_arrays.cpp's and game_lists.cpp's script effects (script_effects.cpp needs the world).
void ScriptPointLight::ScriptPointLightDestruct() EI_UNLINKED("ScriptPointLight::ScriptPointLightDestruct")
void ScriptParticleSource::ScriptParticleSourceDestruct() EI_UNLINKED("ScriptParticleSource::ScriptParticleSourceDestruct")
void ScriptFxSource::ScriptFxSourceDestruct() EI_UNLINKED("ScriptFxSource::ScriptFxSourceDestruct")
void ScriptLightning::ScriptLightningDestruct() EI_UNLINKED("ScriptLightning::ScriptLightningDestruct")

// record_arrays.cpp's other element types.
void scene::InitFigurePart(CPlainFigureMesh*) EI_UNLINKED("scene::InitFigurePart")
CMaterial* CMaterial::Construct() EI_UNLINKED("CMaterial::Construct")
void recordarrays::InitDeltaField(DeltaField*) EI_UNLINKED("recordarrays::InitDeltaField")
void recordarrays::DestroyDeltaField(DeltaField*) EI_UNLINKED("recordarrays::DestroyDeltaField")
void SectorElements::Construct(Sector*, int) EI_UNLINKED("SectorElements::Construct")
void terrain::RecordElements::Construct(terrain::SectorVertex*, int) EI_UNLINKED("terrain::RecordElements::Construct")
void terrain::RecordElements::Destruct(terrain::SectorVertex*, int) EI_UNLINKED("terrain::RecordElements::Destruct")
void __stdcall scene::FaceGroup::ConstructElements(scene::FaceGroup*, int) EI_UNLINKED("scene::FaceGroup::ConstructElements")
void scene::FaceGroup::Destruct() EI_UNLINKED("scene::FaceGroup::Destruct")
void __fastcall scene::FaceGroupArrayDestruct(scene::FaceGroupArray*) EI_UNLINKED("scene::FaceGroupArrayDestruct")
void scene::InitSceneMesh(scene::SceneMesh*) EI_UNLINKED("scene::InitSceneMesh")
void scene::SceneNode::Construct() EI_UNLINKED("scene::SceneNode::Construct")
void scene::SceneNode::Destruct() EI_UNLINKED("scene::SceneNode::Destruct")
mapspec::Behaviour* __fastcall mapspec::BehaviourConstruct(mapspec::Behaviour*) EI_UNLINKED("mapspec::BehaviourConstruct")
void __fastcall mapspec::BehaviourDestruct(mapspec::Behaviour*) EI_UNLINKED("mapspec::BehaviourDestruct")

// script_runtime_helpers.cpp's AI map and console helpers (CSectorMap::Add and Remove: replay_path.cpp).
void CSectorMap::FindUnitsInRadius(const Vector3*, mfc::CTypedPtrList<CUnit>*, float)
    EI_UNLINKED("CSectorMap::FindUnitsInRadius")
// ai_movement.cpp's shape cache and passability marks (ai_shape_cache.cpp, ai_refresh.cpp).
CRefObject* UnitShapeCache::Get(void*) EI_UNLINKED("UnitShapeCache::Get")
std::uint32_t CAIMap::CellMark(void*, int, int, int, int) EI_UNLINKED("CAIMap::CellMark")
console::GameOStream& console::GameOStream::operator<<(double) EI_UNLINKED("console::GameOStream::operator<<(double)")

// The script loaders' and destructors' callees outside the interpreter: the object registry
// (base_manager.cpp), the streams (core_streams.cpp), the world's timed objects (world_server.cpp) and
// the script effects (script_effects.cpp needs the world).
CEngineObject* CBaseManager::FindObject(unsigned) const EI_UNLINKED("CBaseManager::FindObject")
mfc::CString* CStream::ReadString(void*) EI_UNLINKED("CStream::ReadString")
void CWorldServer::RemoveTimedObject(unsigned) EI_UNLINKED("CWorldServer::RemoveTimedObject")
void ScriptPointLight::ScriptPointLightLoad(ScriptStream*) EI_UNLINKED("ScriptPointLight::ScriptPointLightLoad")
void ScriptFxSource::ScriptFxSourceLoad(ScriptStream*) EI_UNLINKED("ScriptFxSource::ScriptFxSourceLoad")
void __stdcall ScriptLightningElementsDestruct(ScriptLightning*, int) EI_UNLINKED("ScriptLightningElementsDestruct")

// The game CRT's ports (crt.cpp, crt_file.cpp, crt_x87.cpp read the game CRT's data)
// that no replay reaches: the node allocator (stlp.cpp), the multibyte string functions CString uses
// (mfc_string.cpp), qsort (script_group.cpp), srand (random.cpp), acos (quaternion.cpp),
// fwrite (crt_format.cpp); replay_crt.cpp has the ones the replays call.
void* __fastcall libcrt::NodeAllocate(std::size_t) EI_UNLINKED("libcrt::NodeAllocate")
void __fastcall libcrt::NodeDeallocate(void*, std::size_t) EI_UNLINKED("libcrt::NodeDeallocate")
unsigned char* __cdecl libcrt::Mbslwr(unsigned char*) EI_UNLINKED("libcrt::Mbslwr")
void __cdecl libcrt::Qsort(void*, std::size_t, std::size_t, int(__cdecl*)(const void*, const void*)) EI_UNLINKED("libcrt::Qsort")
void __cdecl libcrt::Srand(unsigned) EI_UNLINKED("libcrt::Srand")
double __cdecl libcrt::CiAcos(double) EI_UNLINKED("libcrt::CiAcos")
std::size_t __cdecl libcrt::Fwrite(const void*, std::size_t, std::size_t, gamecrt::CrtFile*) EI_UNLINKED("libcrt::Fwrite")

// The library's exceptions and resources (stlp_string.cpp, mfc_archive.cpp, mfc_string.cpp, mfc.cpp's
// TRY link, mfc_file.cpp).
libcrt::Exception* libcrt::Exception::Construct() EI_UNLINKED("libcrt::Exception::Construct")
libcrt::Exception* libcrt::Exception::DeletingDestruct(unsigned) EI_UNLINKED("libcrt::Exception::DeletingDestruct")
void* mfc::impl::CFileException::Vtable() EI_UNLINKED("mfc::impl::CFileException::Vtable")
void __stdcall libcrt::CxxThrowException(void*, const void*) EI_UNLINKED("libcrt::CxxThrowException")
void __stdcall mfc::impl::ThrowArchiveException(int, const char*) EI_UNLINKED("mfc::impl::ThrowArchiveException")
void __stdcall mfc::impl::AfxClassInit(mfc::impl::CRuntimeClass*) EI_UNLINKED("mfc::impl::AfxClassInit")
int __stdcall mfc::impl::AfxLoadString(unsigned, char*, unsigned) EI_UNLINKED("mfc::impl::AfxLoadString")
mfc::ExceptionLink* __fastcall mfc::ConstructExceptionLink(mfc::ExceptionLink*) EI_UNLINKED("mfc::ConstructExceptionLink")
void __cdecl mfc::TryCleanup() EI_UNLINKED("mfc::TryCleanup")
int __stdcall mfc::impl::OsErrorToException(long) EI_UNLINKED("mfc::impl::OsErrorToException")
void __stdcall mfc::impl::ThrowFileException(int, long, const char*) EI_UNLINKED("mfc::impl::ThrowFileException")
void __stdcall mfc::impl::ThrowOsError(long, const char*) EI_UNLINKED("mfc::impl::ThrowOsError")
void __stdcall mfc::impl::ThrowNotSupported() EI_UNLINKED("mfc::impl::ThrowNotSupported")
void mfc::impl::CFileException::InitEmpty() EI_UNLINKED("mfc::impl::CFileException::InitEmpty")
// mfc_file.cpp's CFileFind times (mfc_time.cpp) and CStdioFile streams (crt_file.cpp).
mfc::impl::CTime* mfc::impl::CTime::ConstructFileTime(const FILETIME&, int) EI_UNLINKED("mfc::impl::CTime::ConstructFileTime")
int __cdecl libcrt::Fclose(gamecrt::CrtFile*) EI_UNLINKED("libcrt::Fclose")
int __cdecl libcrt::Fseek(gamecrt::CrtFile*, long, int) EI_UNLINKED("libcrt::Fseek")
long __cdecl libcrt::Ftell(gamecrt::CrtFile*) EI_UNLINKED("libcrt::Ftell")
std::size_t __cdecl libcrt::Fread(void*, std::size_t, std::size_t, gamecrt::CrtFile*) EI_UNLINKED("libcrt::Fread")

// world_construct.cpp (the server world, g_serverWorld) is not linked: the scripts' replays see a zero world, as they
// saw the stand-in's zero bytes before C18t named it (no fixture fills it).
CWorldServer* TheServerWorld() {
    alignas(8) static unsigned char world[sizeof(CWorldServer)] = {};
    return reinterpret_cast<CWorldServer*>(world);
}
