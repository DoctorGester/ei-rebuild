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
#include "game_lists.h"
#include "link_figure.h"     // CLinkMesh: the figure
#include "map_object.h"
#include "game_arrays.h"
#include "record_arrays.h"
#include "map_object_figures.h"
#include "net_delta.h"     // DeltaState
#include "spell.h"         // CLightFactory
#include "terrain.h"
#include "unit.h"
#include "unit_ai.h"       // AIBrain

#include "lib/crt.h"
#include "lib/mfc.h"

#include "ei/vcall.h"
#include "lib/stlp_list.h"
#include "lib/mfc_array.h"
#include "lib/mfc_list.h"
#include "game_data.h"
#include "lib/mfc_slots.h"
#include "game/game_slots.h"
#include "lib/vecmath.h"
#include "game_singletons.h"

#include <cstring>
#include <new>

namespace {
using ei::SetVtable;

constexpr std::uint32_t kOne = 0x3F800000;
constexpr std::uint32_t kTenth = 0x3DCCCCCD;          // 0.1
constexpr std::uint32_t kThreeQuarters = 0x3F400000;  // 0.75
constexpr std::uint32_t kHalf = 0x3F000000;           // 0.5


// A float stored as the dword the original moves.
void SetBits(float& field, std::uint32_t bits) { std::memcpy(&field, &bits, 4); }

void BrainObject8CDestroy(void* a0) { static_cast<CRefObject*>(a0)->Release(); }
void BrainArrayDestroy(void* a0) { static_cast<AIRef*>(a0)->Reset(); }
void Brain14Destroy(void* a0) { mfc::impl::DestructHeader<mfc::impl::AiLookArrayClass>(a0); }

// The engine object fields every constructor here sets (the base constructor inlined).
void InitEngineObject(CEngineObject* object) {
    object->refCount_ = 0;
    object->id_ = -1;
    object->prototypeId_ = 0;
    object->type_ = 0;
    object->flags_ = 0;
    object->owner_ = nullptr;
}

// A plain CArray member: its vtable, then its data freed.
template <typename Array>
void DestroyArray(Array& array, std::uintptr_t vtable) {
    SetVtable(&array, vtable);
    if (array.data) gamecrt::operator_delete(array.data);
}
template <typename Array>
void DestroyArray(Array& array, void* vtable) {
    SetVtable(&array, vtable);
    if (array.data) gamecrt::operator_delete(array.data);
}
void DestroyArray(AnimationSteps& array, void* vtable) {
    SetVtable(&array, vtable);
    if (array.data) gamecrt::operator_delete(array.data);
}
} // namespace


void CObject3D::ClearBounds() {
    std::memset(&bounds, 0, sizeof(bounds));
    std::memset(unused_150, 0, sizeof(unused_150));
    eyeHeight = 0;
}

CObject3D::CObject3D() { SetUpObject3D(); }

void CObject3D::SetUpObject3D() {
    InitEngineObject(this);
    localRotation_.SetIdentity();
    worldRotation_.SetIdentity();
    // The part list (an MFC CPtrList: head, tail, count, free list, blocks, block size 10).
    effectSources.vtable = mfc::impl::Object3DListClass::Vtable();
    effectSources.count = 0;
    effectSources.free = nullptr;
    effectSources.tail = nullptr;
    effectSources.head = nullptr;
    effectSources.blocks = nullptr;
    effectSources.blockSize = 10;
    std::memset(&parts, 0, sizeof(parts));
    std::memset(&pendingParts, 0, sizeof(pendingParts));
    figure = nullptr;
    ClearBounds();
    partBoxArrayVtable = mfc::impl::FrameArrayClass::Vtable();
    partBoxes = nullptr;
    partBoxGrowBy = 0;
    partBoxCapacity = 0;
    partBoxCount = 0;
    attached = nullptr;
    animation = nullptr;
    std::memset(&scale, 0, sizeof(scale));
    defaultScale = kDefaultFigureScale;
    // The original leaves the view position as the heap held it, and the draw sorts read it for an object listed
    // only for its shadow before it was ever in view (a NaN there made UnitOrderLess irreflexive). It starts at
    // zero here, and in the original through a harness patch (harness_leftovers.cpp).
    std::memset(&worldPosition_, 0, sizeof(worldPosition_));
}

// The name is destroyed after the body, then CEngineObject's part.
CObject3D::~CObject3D() { TearDownObject3D(); }

void CObject3D::TearDownObject3D() {
    Destroy3D();
    partBoxArrayVtable = mfc::impl::FrameArrayClass::Vtable();
    if (partBoxes) gamecrt::operator_delete(partBoxes);
    if (CLinkMesh* current = figure) {
        if (--current->refCount_ == 0) delete current;
    }
    if (pendingParts.data) gamecrt::operator_delete(pendingParts.data[-1]);
    if (parts.data) gamecrt::operator_delete(parts.data[-1]);
    // The part list: emptied, its blocks freed.
    effectSources.vtable = mfc::impl::Object3DListClass::Vtable();
    mfc::CPlex* blocks = effectSources.blocks;
    effectSources.count = 0;
    effectSources.free = nullptr;
    effectSources.tail = nullptr;
    effectSources.head = nullptr;
    mfc::PlexFreeDataChain(blocks);
    effectSources.blocks = nullptr;
}


CMapObject::CMapObject() { SetUpMapObject(); }

DeltaState::DeltaState() {
    fields_.Construct(recordarrays::DeltaFieldArrayClass::Vtable());
    Reset();
}

DeltaState::~DeltaState() {
    fields_.vtable = const_cast<void*>(recordarrays::DeltaFieldArrayClass::Vtable());
    fields_.FreeData();
}

void CMapObject::SetUpMapObject() {
    effects.Construct(mfc::impl::EffectArrayClass::Vtable());
    companion = nullptr;
    PackedVitals()->Reset();
    sectorCellX = -1;
    sectorCellY = -1;
    SetBits(animationSpeed, kOne);
}

// The second name is destroyed after the body (before the effects in the original), then the 3D object.
CMapObject::~CMapObject() {
    effects.vtable = const_cast<void*>(mfc::impl::EffectArrayClass::Vtable());
    effects.FreeData();
}

CUnit::CUnit() {
    // The unit's motion: two arrays, then the step speeds and the segment part.
    UnitMotion* m = Motion();
    m->points.data = nullptr;
    m->points.growBy = 0;
    m->points.maxSize = 0;
    m->points.size = 0;
    m->points.vtable = mfc::impl::MotionPointsClass::Vtable();
    m->waypoints.data = nullptr;
    m->waypoints.growBy = 0;
    m->waypoints.maxSize = 0;
    m->waypoints.size = 0;
    m->waypoints.vtable = mfc::impl::MotionWaypointArrayClass::Vtable();
    mfc::impl::ConstructArrayOf<mfc::impl::FloatArrayClass>(m->stepSpeeds);
    SetVtable(&m->stepSpeeds, mfc::impl::ArgFloatArrayClass::Vtable());
    // (The segment part's constructor only returns it.)
    std::memset(&m->curveX, 0, sizeof(m->curveX));
    std::memset(&m->curveY, 0, sizeof(m->curveY));
    m->segment = -1;
    m->Clear();
    SetBits(m->speedScale, kTenth);
    SetBits(m->baseTurnRate, kTenth);
    carriedVtable = mfc::impl::CarriedArrayClass::Vtable();
    carried = nullptr;
    carriedGrowBy = 0;
    carriedCapacity = 0;
    carriedCount = 0;
    controller = nullptr;
    player = nullptr;
    Reset();
}

void CUnit::Reset() {
    scriptName.clear();
    prototype23C = -1;
    heightOffset = 0.0f;
    sizeClass = 3;
    SetBits(unitRadius, kThreeQuarters);
    SetBits(standingHeight, kHalf);
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
    Params()->Reset();
    std::memset(weapons, 0, sizeof(weapons));
    std::memset(quickItems, 0, sizeof(quickItems));
    std::memset(armor, 0, sizeof(armor));
    std::memset(spells, 0, sizeof(spells));
    if (carried) {
        gamecrt::operator_delete(carried);
        carried = nullptr;
    }
    carriedCapacity = 0;
    carriedCount = 0;
    altPassCosts = 0;
    MapObject()->NetState()->Reset();
    fightAtWill = 1;
}

CUnit::~CUnit() {
    // The release-from-the-player virtual, called directly like the original (through the vtable's memory, not
    // the object's).
    ReleaseUnit();
    DestroyMapObject();
    Reset();
    carriedVtable = mfc::impl::CarriedArrayClass::Vtable();
    if (carried) gamecrt::operator_delete(carried);
    UnitMotion* m = Motion();
    DestroyArray(m->stepSpeeds, mfc::impl::FloatArrayClass::Vtable());
    mfc::impl::DestructArrayOf<mfc::impl::MotionWaypointBaseClass>(m->waypoints);
    mfc::impl::DestructArrayOf<mfc::impl::MotionPointArrayClass>(m->points);
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
    // The effect particles (0x10 each) and looping sounds (0x14 each), MFC CArrays.
    SetVtable(&particles, gameslots::EffectObjectArrayClass::Vtable());
    particles.data = nullptr;
    particles.growBy = 0;
    particles.maxSize = 0;
    particles.size = 0;
    SetVtable(&sounds, mfc::impl::EffectSoundArrayClass::Vtable());
    sounds.data = nullptr;
    sounds.growBy = 0;
    sounds.maxSize = 0;
    sounds.size = 0;
    light = nullptr;
    lightStateA = 0;
    lightStateB = 0;
}

CMapObjectClientSpecific::~CMapObjectClientSpecific() {
    lightStateA = 0;
    lightStateB = 0;
    mfc::impl::ArrayAs<gamearrays::Particles>(particles).SetSize(0, -1);
    if (light) {
        light->flags_ &= ~0x200u;
        if (CLight* shown = light; shown && !(shown->flags_ & 0x80) && !(shown->flags_ & 0x200)) {
            shown->flags_ |= 0x80;
            TheTerrain()->RemoveLight(light);
            if (CUnit* unit = Unit()) {
                if (CObject3DSpecific* half = unit->Object3D()->attached) half->SetLit(0);
            }
        }
        int id = light->id_;
        stlp::ListRemoveInt(&TheLightFactory()->litIds_, id);
        light.Assign(nullptr);
    }
    owner = nullptr;
    if (light) light->Release();
    DestroyArray(sounds, mfc::impl::EffectSoundArrayClass::Vtable());
    SetVtable(&particles, gameslots::EffectObjectArrayClass::Vtable());
    if (EffectParticle* data = particles.data) {
        for (int i = 0; i < particles.size; ++i)
            if (data[i].source) data[i].source->Release();
        gamecrt::operator_delete(particles.data);
    }
}

// --- The server unit controller ---------------------------------------------------------------

CUnitServer::~CUnitServer() {
    ResetController();
    AIBrain* b = Brain();
    b->Init();
    b->memory.Destruct();
    mfc::impl::DestructHeader<mfc::impl::AttackOptionArrayClass>(&b->attention.spells);
    mfc::impl::DestructHeader<mfc::impl::AttackOptionArrayClass>(&b->attention.weapons);
    mfc::impl::DestructArrayOf<recordarrays::BehaviourArrayClass>(mfc::impl::ArrayAs<recordarrays::Behaviours>(b->alarmRecords));
    if (AIMode* current = b->current) BrainObject8CDestroy(current);
    gamelists::OwnedRefs::Destruct<gameslots::OwnedRefListClass>(mfc::impl::ListAs<gamelists::OwnedRefs>(&b->modes));
    BrainArrayDestroy(&b->seen);
    BrainArrayDestroy(&b->target);
    Brain14Destroy(&b->move.looks);
    idleAnimation.DestructSteps();
    moveAnimation.DestructSteps();
    SetVtable(&armourSpells, gameslots::ArmourSpellListClass::Vtable());
    mfc::impl::ListAs<gamelists::Holders>(&armourSpells)->RemoveAll();
    if (CEngineObject* leader = followedLeader) leader->Release();
    if (CMapObject* target = action2.target) target->Release();
    action2.target = nullptr;
    if (CMapObject* target = action.target) target->Release();
    action.target = nullptr;
    if (CEngineObject* effect = effectObject) effect->Release();
    DestroyArray(savedQueue, mfc::impl::AnimationStepsClass::Vtable());
    DestroyArray(currentPlan.animation, mfc::impl::AnimationStepsClass::Vtable());
    motion.excludedBlocks.vtable = mfc::impl::PointArrayClass::Vtable();
    if (motion.excludedBlocks.data) gamecrt::operator_delete(motion.excludedBlocks.data);
    mfc::impl::DestructList<mfc::impl::CellListClass>(motion.cells);
    mfc::impl::DestructList<mfc::impl::CellListClass>(motion.steps);
    DestroyArray(queue, mfc::impl::AnimationStepsClass::Vtable());
}
