// The destructors the compiler wrote for the world's classes: the bodies the exe's vtables reach through the
// scalar deleting destructors, the members destroyed last to first after the class's own teardown. The
// containers are the library's (lib/mfc.h CArray / CList) and std's. See docs/notes/standalone.md.
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
    // The second grid still holds its objects: released here, as the original's destructor does.
    secondGrid_.Free();
    objectGrid_.Free();
    dynamicObjects_.clear();
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
