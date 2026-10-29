// The destructors the compiler wrote for the world's classes: the bodies the exe's vtables reach through the
// scalar deleting destructors, the members destroyed last to first after the class's own teardown. The
// containers are std's. See docs/notes/standalone.md.
#include "effects.h"
#include "ground_print.h"
#include "model.h"
#include "registry.h"
#include "script.h"
#include "sector_map.h"
#include "spell.h"
#include "terrain.h"
#include "world.h"
#include "world_data.h"

#include <memory>

CWorld::~CWorld() {
    CWorld::Destroy();
    netSession_.Destroy();
    worldHandle_.Destroy();
    figureLoader_.Reset();
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
    exitMarkers_.clear();
    exits_.clear();
}

CWorldServer::~CWorldServer() {
    CWorldServer::Destroy();
    sharedObjects_.clear();
    ReleaseAndClear(counted_);
    projectiles_.clear();
    diplomacy_.Reset();
    std::destroy_at(SecondScript());
    std::destroy_at(MainScript());
    Messages()->Destroy();
    exits_.clear();
}
