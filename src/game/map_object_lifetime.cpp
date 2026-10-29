// Constructors and destructors of the map object family: CObject3D, CMapObject, CUnit, their
// server / client companions and the server-side unit controller. See docs/notes/map_objects.md ("Lifetime").
#include "ai_map.h"        // AIPath
#include "ai_motion.h"     // UnitMotion
#include "combat.h"
#include "core_refcount.h"
#include "core_streams.h"
#include "core_transform.h"
#include "effects.h"       // CParticleSource
#include "engine_object.h"
#include "link_figure.h"     // CLinkMesh: the figure
#include "map_object.h"
#include "map_object_figures.h"
#include "net_delta.h"     // DeltaState
#include "spell.h"         // CLightFactory
#include "terrain.h"
#include "unit.h"
#include "unit_ai.h"       // AIBrain

#include "lib/crt.h"
#include <bit>
#include <cstdint>
#include <cstring>

#include "lib/vecmath.h"
#include "game_singletons.h"
#include "world.h"

#include <new>

namespace {

constexpr float kOne = 1.0f;
constexpr float kTenth = 0.1f;
constexpr float kThreeQuarters = 0.75f;
constexpr float kHalf = 0.5f;

// A float stored as the dword the original moves.
void SetBits(float& field, std::uint32_t bits) { field = std::bit_cast<float>(bits); }

} // namespace

void CObject3D::ClearBounds() {
    std::memset(&bounds, 0, sizeof(bounds));
    eyeHeight = 0;
}

CObject3D::CObject3D() {
    localRotation_.SetIdentity();
    worldRotation_.SetIdentity();
    ClearBounds();
    attached = nullptr;
    animation = nullptr;
    scale = {};
    defaultScale = kDefaultFigureScale;
    // The original leaves the view position as the heap held it, and the draw sorts read it for an object listed
    // only for its shadow before it was ever in view (a NaN there made the draw order irreflexive). It starts at
    // zero here, and in the original through a harness patch (harness_leftovers.cpp).
    worldPosition_ = {};
}

// The name is destroyed after the body, then CEngineObject's part.
CObject3D::~CObject3D() {
    Destroy3D();
    figure.Reset();
    lib::FreeStorage(parts);
}

DeltaState::DeltaState() { Reset(); }

DeltaState::~DeltaState() = default;

void DeltaState::SetFieldCount(int count) { fields_.resize(count); }

CMapObject::CMapObject() {
    companion = nullptr;
    packedVitals.Reset();
    sectorCellX = -1;
    sectorCellY = -1;
    animationSpeed = kOne;
}

CMapObject::~CMapObject() = default;

CUnit::CUnit() {
    // The unit's motion: two arrays, then the step speeds and the segment part.
    UnitMotion* m = &motion;
    // (The segment part's constructor only returns it.)
    std::memset(&m->curveX, 0, sizeof(m->curveX));
    std::memset(&m->curveY, 0, sizeof(m->curveY));
    m->segment = -1;
    m->Clear();
    m->speedScale = kTenth;
    m->baseTurnRate = kTenth;
    controller = nullptr;
    player = nullptr;
    Reset();
}

void CUnit::Reset() {
    scriptName.clear();
    secondPrototype = -1;
    heightOffset = 0.0f;
    sizeClass = 3;
    unitRadius = kThreeQuarters;
    standingHeight = kHalf;
    relationsA = 0;
    relationsB = 0xFFFFFFFFu;
    side = 0;
    if (controller) {
        ServerController()->unit = nullptr;
        controller->Release();
    }
    animationEntry = -1;
    secondEntry = -1;
    controller = nullptr;
    player = nullptr;
    actionSound = 0;
    animationStart = 0;
    motionMode = 0;
    pathTimeShift = 0;
    moveMode = 2;
    moveMode2 = 2;
    pendingWeapon = -1;
    dead = 0;
    obstacle = 0;
    activeWeapon = 0xFFFFFFFFu;
    params.Reset();
    std::memset(weapons, 0, sizeof(weapons));
    std::memset(quickItems, 0, sizeof(quickItems));
    std::memset(armor, 0, sizeof(armor));
    std::memset(spells, 0, sizeof(spells));
    carried.clear();
    altPassCosts = 0;
    MapObject()->netState_.Reset();
    fightAtWill = 1;
}

CUnit::~CUnit() {
    // The release-from-the-player virtual, called directly like the original (through the vtable's memory, not
    // the object's).
    ReleaseUnit();
    DestroyMapObject();
    Reset();
}

CMapObjectServer::CMapObjectServer() {
    vitals.Reset();
}

CMapObjectServer::~CMapObjectServer() {
    vitals.Reset();
    strings.clear();
    owner = nullptr;
}

CMapObjectClientSpecific::CMapObjectClientSpecific() {
    lightStateA = 0;
    lightStateB = 0;
}

CMapObjectClientSpecific::~CMapObjectClientSpecific() {
    lightStateA = 0;
    lightStateB = 0;
    particles.clear();
    if (light) {
        light->flags_ &= ~LightFlag::Held;
        if (CLight* shown = light; shown && !(shown->flags_ & LightFlag::Hidden) && !(shown->flags_ & LightFlag::Held)) {
            shown->flags_ |= LightFlag::Hidden;
            g_clientWorld.terrain_->RemoveLight(light);
            if (CUnit* unit = Unit()) {
                if (CObject3DSpecific* half = unit->Object3D()->attached) half->SetLit(0);
            }
        }
        int id = light->id_;
        g_clientWorld.lightFactory_->litIds_.remove(id);
        light.Reset();
    }
    owner = nullptr;
}

// --- The server unit controller ---------------------------------------------------------------

// ResetController empties the brain (Init) and releases every reference the controller holds.
CUnitServer::~CUnitServer() { ResetController(); }
