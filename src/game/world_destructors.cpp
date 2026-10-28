// The destructors the compiler wrote for the world's classes: the bodies the exe's vtables reach through the
// scalar deleting destructors, the members destroyed last to first after the class's own teardown. The
// containers are the library's (lib/mfc.h CArray / CList, lib/containers.h AlignedArray, lib/stlp.h
// hash_map). See docs/notes/standalone.md.
#include "effects.h"
#include "game_arrays.h"
#include "game_lists.h"
#include "ground_print.h"
#include "model.h"
#include "registry.h"
#include "script.h"
#include "sector_map.h"
#include "spell.h"
#include "terrain.h"
#include "world.h"
#include "world_data.h"
#include "world_deleting_destructors.h"

#include "lib/crt.h"
#include "game_data.h"
#include "lib/mfc_slots.h"
#include "game/record_arrays.h"
#include "game/game_slots.h"

#include <memory>

void RefPtrRelease(RefPtr* self);

namespace {

// A sector grid's storage as the client's destructor frees it after setting the grid to 1 x 1: the
// cells' lists (their vector deleting destructor), then the row pointers.
void FreeGrid(SectorGrid& grid) {
    if (grid.cells_) grid.cells_->VectorDelete(3);
    gamecrt::operator_delete(grid.rows_);
}

// A script container destroyed by its instantiation's destructor (`Abi` over the element's policy,
// `Class` the vtable it writes back).
template <typename Abi, typename Class, typename Container>
void DestroyScriptList(Container& list) {
    Abi::template Destruct<Class>(reinterpret_cast<typename Abi::List*>(&list));
}
template <typename Abi, typename Class, typename Container>
void DestroyScriptArray(Container& array) {
    Abi::template Destruct<Class>(reinterpret_cast<typename Abi::Array*>(&array));
}

template <typename T>
T* DeleteIf(T* self, unsigned flags) {
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}

} // namespace

// (The server's second script inlines it): the reset, the error text,
// then the containers last to first (the variables through ScriptVarsDestruct).
void CScript::Destruct() {
    ScriptReset(this);
    std::destroy_at(&errorText_);
    DestroyScriptList<gamelists::ObjectRefs, gameslots::ObjectRefListClass>(tempObjects_);
    DestroyScriptList<gamelists::Groups, gameslots::GroupListClass>(tempGroups_);
    DestroyScriptArray<gamearrays::Areas, mfc::impl::ScriptAreaArrayClass>(areas_);
    DestroyScriptList<gamelists::Decls, gameslots::DeclListClass>(worldBlocks_);
    DestroyScriptList<gamelists::Runs, gameslots::RunListClass>(runs_);
    DestroyScriptList<gamelists::Definitions, gameslots::DefinitionListClass>(instances_);
    ScriptVarsDestruct(&globals_);
    DestroyScriptArray<gamearrays::Definitions, recordarrays::DefinitionArrayClass>(definitions_);
    DestroyScriptList<gamelists::Decls, gameslots::DeclListClass>(blocks_);
    DestroyScriptList<gamelists::Lightnings, gameslots::LightningListClass>(lightning_);
    DestroyScriptList<gamelists::FxSources, gameslots::FxSourceListClass>(fxSources_);
    DestroyScriptList<gamelists::ParticleSources, gameslots::ParticleSourceListClass>(particles_);
    DestroyScriptArray<gamearrays::PointLights, gamearrays::PointLightArrayClass>(pointLights_);
}

CWorld::~CWorld() {
    CWorld::Destroy();
    netSession_.Destroy();
    shared5C_.Destroy();
    if (CLinkFigureLoader* loader = figureLoader_)
        if (--loader->refCount_ == 0) delete loader;
}

CParticleManager::CParticleManager() {
    maxHeldId_ = 0;
    ParticleManagerClearQuietStart(this);
}

CParticleManager::~CParticleManager() {
    CParticleManager::Destroy();
    freeParticles.clear();
}

Sector::~Sector() {
    Clear();
    units.clear();
    objects.clear();
}

CWorldClient::~CWorldClient() {
    CWorldClient::Destroy();
    handle_.Destroy();
    scorchMarks_.clear();
    footprints_.clear();
    bloodPrints_.clear();
    secondGrid_.Resize(1, 1);
    FreeGrid(secondGrid_);
    objectGrid_.Resize(1, 1);
    FreeGrid(objectGrid_);
    dynamicObjects_.clear();
    objects_.Destroy();
    DestroySectorList(&heroes_);
    countedA0_.clear();
    exits_.clear();
}

CWorldServer::~CWorldServer() {
    CWorldServer::Destroy();
    sharedObjects_.clear();
    ReleaseAndClear(counted_);
    for (CSpellEffect* missile : projectiles_) ReleaseCounted(missile);
    projectiles_.clear();
    diplomacy_.Reset();
    SecondScript()->Destruct();
    MainScript()->Destruct();
    Messages()->Destroy();
    exits_.clear();
}
