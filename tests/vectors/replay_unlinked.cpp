// Functions the game sources linked into ei_vectors refer to but no replay reaches: their files would
// pull in the world, the renderer and the terrain. Each stops the replay naming itself (as an
// EI_ORIGINAL forwarder without a rewrite does, hook_stub.cpp).
#include "../../src/game/console.h"
#include "../../src/game/figure_mesh.h"
#include "../../src/game/game_arrays.h"
#include "../../src/game/map_spec.h"
#include "../../src/game/material.h"
#include "../../src/game/net_delta.h"
#include "../../src/game/scene_loader.h"
#include "../../src/game/script.h"
#include "../../src/game/sector_map.h"
#include "../../src/game/terrain_sector.h"
#include "../../src/game/unit.h"
#include "../../src/lib/crt.h"
#include "../../src/lib/mfc_exception.h"
#include "../../src/lib/mfc_resource.h"

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
void __fastcall zone::ExitDestruct(MapExit*) EI_UNLINKED("zone::ExitDestruct")
void __fastcall zone::ZoneRecordDestruct(MapRecord*) EI_UNLINKED("zone::ZoneRecordDestruct")

// game_arrays.cpp's and game_lists.cpp's script effects (script_effects.cpp needs the world).
void ScriptPointLight::ScriptPointLightDestruct() EI_UNLINKED("ScriptPointLight::ScriptPointLightDestruct")
void ScriptParticleSource::ScriptParticleSourceDestruct() EI_UNLINKED("ScriptParticleSource::ScriptParticleSourceDestruct")
void ScriptFxSource::ScriptFxSourceDestruct() EI_UNLINKED("ScriptFxSource::ScriptFxSourceDestruct")
void ScriptLightning::ScriptLightningDestruct() EI_UNLINKED("ScriptLightning::ScriptLightningDestruct")

// record_arrays.cpp's other element types.
CFigureMesh* CFigureMesh::ConstructBase() EI_UNLINKED("CFigureMesh::ConstructBase")
CMaterial* CMaterial::Construct() EI_UNLINKED("CMaterial::Construct")
void SectorElements::Construct(Sector*, int) EI_UNLINKED("SectorElements::Construct")
void terrain::RecordElements::Construct(terrain::SectorVertex*, int) EI_UNLINKED("terrain::RecordElements::Construct")
void terrain::RecordElements::Destruct(terrain::SectorVertex*, int) EI_UNLINKED("terrain::RecordElements::Destruct")
void __stdcall scene::FaceGroup::ConstructElements(scene::FaceGroup*, int) EI_UNLINKED("scene::FaceGroup::ConstructElements")
void scene::FaceGroup::Destruct() EI_UNLINKED("scene::FaceGroup::Destruct")
void __fastcall scene::FaceGroupArrayDestruct(mfc::CArray<scene::FaceGroup>*) EI_UNLINKED("scene::FaceGroupArrayDestruct")
void scene::SceneMesh::Construct() EI_UNLINKED("scene::SceneMesh::Construct")
void scene::SceneNode::Construct() EI_UNLINKED("scene::SceneNode::Construct")
void scene::SceneNode::Destruct() EI_UNLINKED("scene::SceneNode::Destruct")
DeltaField* DeltaField::Construct() EI_UNLINKED("DeltaField::Construct")
void DeltaField::Destruct() EI_UNLINKED("DeltaField::Destruct")
mapspec::Behaviour* __fastcall mapspec::BehaviourConstruct(mapspec::Behaviour*) EI_UNLINKED("mapspec::BehaviourConstruct")
void __fastcall mapspec::BehaviourDestruct(mapspec::Behaviour*) EI_UNLINKED("mapspec::BehaviourDestruct")

// script_runtime_helpers.cpp's AI map and console helpers.
bool CSectorMap::Add(CEngineObject*) EI_UNLINKED("CSectorMap::Add")
bool CSectorMap::Remove(CEngineObject*) EI_UNLINKED("CSectorMap::Remove")
void CSectorMap::FindUnitsInRadius(const Vector3*, mfc::CTypedPtrList<CEngineObject>*, float)
    EI_UNLINKED("CSectorMap::FindUnitsInRadius")
console::GameOStream& console::GameOStream::operator<<(double) EI_UNLINKED("console::GameOStream::operator<<(double)")

// The library's exceptions and resources (stlp_string.cpp, mfc_archive.cpp, mfc_string.cpp).
libcrt::Exception* libcrt::Exception::Construct() EI_UNLINKED("libcrt::Exception::Construct")
void __stdcall libcrt::CxxThrowException(void*, const void*) EI_UNLINKED("libcrt::CxxThrowException")
void __stdcall mfc::impl::ThrowArchiveException(int, const char*) EI_UNLINKED("mfc::impl::ThrowArchiveException")
int __stdcall mfc::impl::AfxLoadString(unsigned, char*, unsigned) EI_UNLINKED("mfc::impl::AfxLoadString")
