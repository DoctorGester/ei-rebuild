// The destructors the compiler wrote for the world's classes: the bodies the exe's vtables reach through the
// scalar deleting destructors, the members destroyed last to first. The worlds' own teardown (Destroy) is not
// repeated: EmptyWorlds (app_main.cpp) has emptied both before their destructors run, which also keeps them from
// touching the camera and the scene renderer, plain globals that may be destroyed first. The containers are std's.
// See docs/notes/standalone.md.
#include "effects.h"
#include "ground_print.h"
#include "model.h"
#include "net_link.h"
#include "registry.h"
#include "script.h"
#include "sector_map.h"
#include "spell.h"
#include "terrain.h"
#include "texture.h"
#include "world.h"
#include "world_data.h"

#include <memory>

CWorld::~CWorld() {
    DestroyOwned(netSession);
    DestroyOwned(worldHandle);
    figureLoader.Reset();
}

CParticleManager::CParticleManager() {
    maxHeldId = 0;
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
    DestroyOwned(handle);
    scorchMarks.clear();
    footprints.clear();
    bloodPrints.clear();
    // The second grid still holds its objects: released here, as the original's destructor does.
    secondGrid.Free();
    objectGrid.Free();
    dynamicObjects.clear();
    exitMarkers.clear();
    exits.clear();
}

CWorldServer::~CWorldServer() {
    sharedObjects.clear();
    ReleaseAndClear(counted);
    projectiles.clear();
    diplomacy.Reset();
    exits.clear();
}
