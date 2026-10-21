// Constructors and destructors of the map object family: CObject3D, CMapObject, CUnit, their
// server / client companions and the server-side unit controller. See
// docs/notes/map_objects.md ("Lifetime").
#include "ai_map.h"        // AIPath
#include "ai_motion.h"     // UnitMotion
#include "combat.h"
#include "core_streams.h"
#include "core_transform.h"
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

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/stlp_list.h"
#include "lib/mfc_array.h"
#include "lib/mfc_list.h"
#include "originals_effects.h"
#include "originals_library.h"
#include "originals_ui.h"
#include "originals_units.h"
#include "originals_core.h"
#include "originals_ai.h"
#include "game_data.h"

#include <cstring>
#include <new>

namespace {
using ei::SetVtable;

// Vtables.
constexpr std::uintptr_t kPartListVtable = std::uintptr_t(0x0073DE4C);
constexpr std::uintptr_t kFrameArrayVtable = std::uintptr_t(0x0073DE40);
constexpr std::uintptr_t kEffectArrayVtable = std::uintptr_t(0x0073DBA0);
constexpr std::uintptr_t kRecordArrayVtable = std::uintptr_t(0x0073DB94);
constexpr std::uintptr_t kCarriedArrayVtable = std::uintptr_t(0x0073EB84);
// The unit motion's arrays (UnitMotion: points, waypoints, step speeds) while constructed / destroyed.
constexpr std::uintptr_t kMotionPointsVtable = std::uintptr_t(0x0073EB9C);
constexpr std::uintptr_t kMotionWaypointsVtable = std::uintptr_t(0x0073EB90);
constexpr std::uintptr_t kMotionSpeedsVtable = std::uintptr_t(0x0073D490);
constexpr std::uintptr_t kMotionPointsEndVtable = std::uintptr_t(0x0073EBA8);
constexpr std::uintptr_t kMotionWaypointsEndVtable = std::uintptr_t(0x0073EBB4);
constexpr std::uintptr_t kMotionSpeedsEndVtable = std::uintptr_t(0x0073D3E8);
constexpr std::uintptr_t kParticleArrayVtable = std::uintptr_t(0x0073DC48);
constexpr std::uintptr_t kSoundArrayVtable = std::uintptr_t(0x0073DC3C);
constexpr std::uintptr_t kUnitServerVtable = std::uintptr_t(0x0073EF38);
constexpr std::uintptr_t kControllerVtable = std::uintptr_t(0x0073EDBC);
constexpr std::uintptr_t kServerArrayVtable = std::uintptr_t(0x0073EF2C);
constexpr std::uintptr_t kArmourSpellListVtable = std::uintptr_t(0x0073F010);

constexpr std::uint32_t kStreamLimit = 2000000000;   // a memory stream's size limit
constexpr std::uint32_t kOne = 0x3F800000;
constexpr std::uint32_t kTenth = 0x3DCCCCCD;          // 0.1
constexpr std::uint32_t kThreeQuarters = 0x3F400000;  // 0.75
constexpr std::uint32_t kHalf = 0x3F000000;           // 0.5

// The figures' default scale (007AF1D0, three floats).
const void* DefaultFigureScale() { return ei::GamePointer<const void>(0x007AF1D0); }

// A float stored as the dword the original moves.
void SetBits(float& field, std::uint32_t bits) { std::memcpy(&field, &bits, 4); }

// Not rewritten, forwarders:
// 0040CB00. Quaternion::SetIdentity.
__declspec(naked) void* __fastcall SetIdentity(Quaternion*) EI_ORIGINAL(0x0040CB00)
// 006FE925 / 006FE958 / 006FE9B8. CStringArray constructor, destructor, SetSize.
// 004CD080 / 0052E230. The unit motion's step speed array constructor and the constructor of its
// segment part (+0x50, empty); the motion's constructor then clears it (005C6270 UnitMotion::Clear).
// 006CA720. Light manager: removes a light.
// 00511FB0. The light list (CLightFactory::litIds_): erases the entries with id *id.
// The server unit controller's members (00546230).
void __fastcall BrainObject8CDestroy(void* a0) { RefObjectRelease(a0); }
void __fastcall BrainArrayDestroy(void* a0) { CountedRefResetOriginal(a0); }
void __fastcall Brain14Destroy(void* a0) { mfc::impl::DestructHeader<mfc::impl::AiLookArrayClass>(a0); }

// The engine object fields every constructor here sets (the base constructor inlined).
void InitEngineObject(CEngineObject* object) {
    object->refCount_ = 0;
    object->id_ = -1;
    object->prototypeId_ = 0;
    object->type_ = 0;
    object->flags_ = 0;
    object->owner_ = nullptr;
}

// A CString member (some layouts keep it as its data pointer): constructed empty (afxEmptyString's
// data), or destroyed.
void ConstructString(void* string) { new (string) mfc::CString(); }
void DestroyString(void* string) { static_cast<mfc::CString*>(string)->~CString(); }

// A memory stream member (vtable 0073BD90 while it frees its buffer, then its base's).
void DestroyStream(CMemoryStream* stream) {
    SetVtable(stream, kMemoryStreamVtable);
    if (stream->data_) gamecrt::operator_delete(stream->data_);
    SetVtable(stream, kStreamVtable);
}

// A plain CArray member: its vtable, then its data freed.
template <typename Array>
void DestroyArray(Array& array, std::uintptr_t vtable) {
    SetVtable(&array, vtable);
    if (array.data) gamecrt::operator_delete(array.data);
}
void DestroyArray(AnimationSteps& array, std::uintptr_t vtable) {
    SetVtable(&array, vtable);
    if (array.steps) gamecrt::operator_delete(array.steps);
}
} // namespace

// --- CObject3D --------------------------------------------------------------------------------

CObject3D* CObject3D::Construct() {
    SetVtable(this, CEngineObject::kVtableAddress);
    SetVtable(this, CTransformObject::kVtableAddress);
    ::new (&name) mfc::CString;
    SetUpObject3D();
    SetVtable(this, CObject3D::kVtableAddress);
    return this;
}
EI_REPLACE_METHOD(0x00515BD0, CObject3D::Construct);

CObject3D::CObject3D() { SetUpObject3D(); }

void CObject3D::SetUpObject3D() {
    InitEngineObject(this);
    SetIdentity(&orientation);
    SetIdentity(&worldOrientation);
    // The part list (an MFC CPtrList: head, tail, count, free list, blocks, block size 10).
    SetVtable(&effectSources, kPartListVtable);
    effectSources.count = 0;
    effectSources.free = nullptr;
    effectSources.tail = nullptr;
    effectSources.head = nullptr;
    effectSources.blocks = nullptr;
    effectSources.blockSize = 10;
    std::memset(&parts, 0, sizeof(parts));
    std::memset(&pendingParts, 0, sizeof(pendingParts));
    figure = nullptr;
    partNames.Construct();
    std::memset(&boundsFlags, 0, 0xE * 4);
    partBoxArrayVtable = ei::GamePointer<void>(kFrameArrayVtable);
    partBoxes = nullptr;
    partBoxGrowBy = 0;
    partBoxCapacity = 0;
    partBoxCount = 0;
    attached = nullptr;
    animation = nullptr;
    std::memset(&scale, 0, sizeof(scale));
    std::memcpy(&defaultScale, DefaultFigureScale(), sizeof(defaultScale));
    // The original leaves the view position as the heap held it, and the draw sorts read it for an
    // object listed only for its shadow before it was ever in view (a NaN there made UnitOrderLess
    // irreflexive: S17a). It starts at zero here, and in the original through a harness patch
    // (harness_leftovers.cpp).
    std::memset(&viewPosition, 0, sizeof(viewPosition));
}

void CObject3D::DestructBody() {
    SetVtable(this, CObject3D::kVtableAddress);
    TearDownObject3D();
    name.~CString();
    SetVtable(this, CTransformObject::kVtableAddress);
    static_cast<CEngineObject*>(this)->DestructBase();
}
EI_REWRITTEN(0x00515D70, CObject3D::DestructBody);

// The name is destroyed after the body, then CEngineObject's part.
CObject3D::~CObject3D() { TearDownObject3D(); }

void CObject3D::TearDownObject3D() {
    Destroy3D();
    partBoxArrayVtable = ei::GamePointer<void>(kFrameArrayVtable);
    if (partBoxes) gamecrt::operator_delete(partBoxes);
    partNames.Destruct();
    if (CLinkMesh* current = figure) {
        if (--current->refCount_ == 0) delete current;
    }
    if (pendingParts.data) gamecrt::operator_delete(pendingParts.data[-1]);
    if (parts.data) gamecrt::operator_delete(parts.data[-1]);
    // The part list: emptied, its blocks freed.
    SetVtable(&effectSources, kPartListVtable);
    mfc::CPlex* blocks = effectSources.blocks;
    effectSources.count = 0;
    effectSources.free = nullptr;
    effectSources.tail = nullptr;
    effectSources.head = nullptr;
    mfc::PlexFreeDataChain(blocks);
    effectSources.blocks = nullptr;
}

// --- CMapObject -------------------------------------------------------------------------------

CMapObject* CMapObject::Construct() {
    CObject3D::Construct();   // 00515BD0
    ::new (&name2) mfc::CString;
    SetUpMapObject();
    SetVtable(this, CMapObject::kVtableAddress);
    return this;
}
EI_REWRITTEN(0x0050E2D0, CMapObject::Construct);

CMapObject::CMapObject() { SetUpMapObject(); }

void CMapObject::SetUpMapObject() {
    effectsVtable = ei::GamePointer<void>(kEffectArrayVtable);
    effects = nullptr;
    effectGrowBy = 0;
    effectCapacity = 0;
    effectCount = 0;
    // The network records (DeltaField, 0xA0 each) and their two memory streams.
    DeltaState* records = NetState();
    SetVtable(records, kRecordArrayVtable);
    records->fields_ = nullptr;
    records->fieldGrowBy_ = 0;
    records->fieldCapacity_ = 0;
    records->fieldCount_ = 0;
    CMemoryStream* stream = &records->header_[0];
    stream->version_ = kStreamLimit;
    stream->ok_ = 1;
    SetVtable(stream, kMemoryStreamVtable);
    stream->data_ = nullptr;
    stream->Reset();
    SetVtable(stream, CMemoryStream::kQueueVtableAddress);
    stream = &records->header_[1];
    stream->ConstructBase();
    SetVtable(stream, kMemoryStreamVtable);
    stream->data_ = nullptr;
    stream->Reset();
    SetVtable(stream, CMemoryStream::kQueueVtableAddress);
    records->Reset();
    companion = nullptr;
    PackedVitals()->Reset();
    sectorCellX = -1;
    sectorCellY = -1;
    SetBits(animationSpeed, kOne);
}

void CMapObject::DestructBody() {
    SetVtable(this, CMapObject::kVtableAddress);
    TearDownMapObject();
    name2.~CString();
    effectsVtable = ei::GamePointer<void>(kEffectArrayVtable);
    if (effects) gamecrt::operator_delete(effects);
    CObject3D::DestructBody();   // 00515D70
}

// The second name is destroyed after the body (before the effects in the original), then the 3D
// object.
CMapObject::~CMapObject() {
    TearDownMapObject();
    effectsVtable = ei::GamePointer<void>(kEffectArrayVtable);
    if (effects) gamecrt::operator_delete(effects);
}

void CMapObject::TearDownMapObject() {
    DeltaState* records = NetState();
    DestroyStream(&records->header_[1]);
    DestroyStream(&records->header_[0]);
    SetVtable(records, kRecordArrayVtable);
    if (DeltaField* data = records->fields_) {
        int count = records->fieldCount_;
        for (int i = 0; i < count; ++i) (data + i)->Destruct();
        gamecrt::operator_delete(records->fields_);
    }
}
EI_REWRITTEN(0x0050E660, CMapObject::DestructBody);
// The compiler's jump thunks to it (functions.csv "thunk" rows; S16a: the standalone image has no
// code at them): the same replacement, so their calls skip its check.
EI_REPLACE_METHOD(0x00529B20, CMapObject::DestructBody);
EI_REPLACE_METHOD(0x00529B50, CMapObject::DestructBody);

// --- CUnit ------------------------------------------------------------------------------------

CUnit::CUnit() {
    // The unit's motion (+0x27C): two arrays, then the step speeds and the segment part.
    UnitMotion* m = Motion();
    m->points.data = nullptr;
    m->points.growBy = 0;
    m->points.maxSize = 0;
    m->points.size = 0;
    SetVtable(&m->points, kMotionPointsVtable);
    m->waypoints.data = nullptr;
    m->waypoints.growBy = 0;
    m->waypoints.maxSize = 0;
    m->waypoints.size = 0;
    SetVtable(&m->waypoints, kMotionWaypointsVtable);
    mfc::impl::ConstructArrayOf<mfc::impl::FloatArrayClass>(m->stepSpeeds);
    SetVtable(&m->stepSpeeds, kMotionSpeedsVtable);
    // (0052E230, the segment part's constructor at +0x50, only returns it.)
    std::memset(&m->curveX, 0, sizeof(m->curveX));
    std::memset(&m->curveY, 0, sizeof(m->curveY));
    m->segment = -1;
    m->Clear();
    SetBits(m->speedScale, kTenth);
    SetBits(m->baseTurnRate, kTenth);
    carriedVtable = static_cast<std::uint32_t>(ei::GameAddress(kCarriedArrayVtable));
    carried = nullptr;
    carriedGrowBy = 0;
    carriedCapacity = 0;
    carriedCount = 0;
    controller = nullptr;
    player = nullptr;
    Reset();
}

CUnit* CUnit::Construct() { return new (this) CUnit(); }
EI_REPLACE_METHOD(0x0052E110, CUnit::Construct);

void CUnit::Reset() {
    scriptName.Empty();
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
EI_REPLACE_METHOD(0x0052E5B0, CUnit::Reset);

CUnit::~CUnit() {
    // The vtable's slot 5 (0052E740, the release from the player), called directly as the original
    // does (through the vtable's memory, not the object's).
    ReleaseUnit();
    DestroyMapObject();
    Reset();
    carriedVtable = static_cast<std::uint32_t>(ei::GameAddress(kCarriedArrayVtable));
    if (carried) gamecrt::operator_delete(carried);
    UnitMotion* m = Motion();
    DestroyArray(m->stepSpeeds, kMotionSpeedsEndVtable);
    DestroyArray(m->waypoints, kMotionWaypointsEndVtable);
    DestroyArray(m->points, kMotionPointsEndVtable);
}

void CUnit::DestructBody() { this->CUnit::~CUnit(); }
EI_REPLACE_METHOD(0x0052E310, CUnit::DestructBody);

CUnit* CUnit::DeletingDestructor(std::uint32_t flags) {
    this->CUnit::~CUnit();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x0052E2A0, CUnit::DeletingDestructor);

// --- Companions -------------------------------------------------------------------------------

CMapObjectServer* CMapObjectServer::Construct() {
    InitEngineObject(this);
    SetVtable(this, CMapObjectCompanion::kVtableAddress);
    owner = nullptr;
    ConstructString(&name78);
    ConstructString(&name7C);
    strings.Construct();
    SetVtable(this, CMapObjectServer::kVtableAddress);
    owner = nullptr;
    vitals.Reset();
    return this;
}
EI_REPLACE_METHOD(0x00515610, CMapObjectServer::Construct);

void CMapObjectServer::DestructBody() {
    SetVtable(this, CMapObjectServer::kVtableAddress);
    vitals.Reset();
    strings.SetSize(0, -1);
    owner = nullptr;
    strings.Destruct();
    name7C.~CString();
    name78.~CString();
    SetVtable(this, CMapObjectCompanion::kVtableAddress);
    owner = nullptr;
    static_cast<CEngineObject*>(this)->DestructBase();
}
EI_REWRITTEN(0x00515720, CMapObjectServer::DestructBody);

CMapObjectClientSpecific* CMapObjectClientSpecific::Construct() {
    InitEngineObject(this);
    owner = nullptr;
    // The effect particles (0x10 each) and looping sounds (0x14 each), MFC CArrays.
    SetVtable(&particles, kParticleArrayVtable);
    particles.data = nullptr;
    particles.growBy = 0;
    particles.maxSize = 0;
    particles.size = 0;
    SetVtable(&sounds, kSoundArrayVtable);
    sounds.data = nullptr;
    sounds.growBy = 0;
    sounds.maxSize = 0;
    sounds.size = 0;
    light = nullptr;
    SetVtable(this, CMapObjectClientSpecific::kVtableAddress);
    lightStateA = 0;
    lightStateB = 0;
    return this;
}
EI_REPLACE_METHOD(0x00510450, CMapObjectClientSpecific::Construct);

void CMapObjectClientSpecific::DestructBody() {
    SetVtable(this, CMapObjectClientSpecific::kVtableAddress);
    lightStateA = 0;
    lightStateB = 0;
    mfc::impl::ArrayAs<gamearrays::Particles>(particles).SetSize(0, -1);
    if (light) {
        light->flags_ &= ~0x200u;
        if (CEngineObject* shown = light; shown && !(shown->flags_ & 0x80) && !(shown->flags_ & 0x200)) {
            shown->flags_ |= 0x80;
            LightManager()->RemoveLight(static_cast<CLight*>(light));
            if (CUnit* unit = Unit()) {
                if (CEngineObject* half = unit->Object3D()->attached)
                    static_cast<CObject3DSpecific*>(half)->SetLit(0);
            }
        }
        int id = light->id_;
        stlp::ListRemoveInt(&TheLightFactory()->litIds_, nullptr, &id);
        ObjectHolderAssign(&light, nullptr, nullptr);
    }
    owner = nullptr;
    if (light) light->Release();
    DestroyArray(sounds, kSoundArrayVtable);
    SetVtable(&particles, kParticleArrayVtable);
    if (EffectParticle* data = particles.data) {
        for (int i = 0; i < particles.size; ++i)
            if (data[i].source) reinterpret_cast<CEngineObject*>(data[i].source)->Release();
        gamecrt::operator_delete(particles.data);
    }
    SetVtable(this, CMapObjectCompanion::kVtableAddress);
    owner = nullptr;
    static_cast<CEngineObject*>(this)->DestructBase();
}
EI_REWRITTEN(0x00510560, CMapObjectClientSpecific::DestructBody);

// --- The server unit controller ---------------------------------------------------------------

void CUnitServer::DestructBody() {
    SetVtable(this, kUnitServerVtable);
    ResetController();
    AIBrain* b = Brain();
    b->Init();
    b->memory.Destruct();
    mfc::impl::DestructHeader<mfc::impl::AttackOptionArrayClass>(&b->attention.spells);
    mfc::impl::DestructHeader<mfc::impl::AttackOptionArrayClass>(&b->attention.weapons);
    mfc::impl::DestructArrayOf<recordarrays::BehaviourArrayClass>(mfc::impl::ArrayAs<recordarrays::Behaviours>(b->alarmRecords));
    if (AIMode* current = b->current) BrainObject8CDestroy(current);
    gamelists::OwnedRefs::Destruct<gamelists::OwnedRefListClass>(mfc::impl::ListAs<gamelists::OwnedRefs>(&b->modes));   // 005575F0
    BrainArrayDestroy(&b->seen);
    BrainArrayDestroy(&b->target);
    Brain14Destroy(&b->looks);
    idleAnimation.DestructSteps();
    moveAnimation.DestructSteps();
    SetVtable(&armourSpells, kArmourSpellListVtable);
    mfc::impl::ListAs<gamelists::Holders>(&armourSpells)->RemoveAll();   // 00558020
    if (CEngineObject* leader = followedLeader) leader->Release();
    if (CEngineObject* target = action2.target) target->Release();
    action2.target = nullptr;
    if (CEngineObject* target = action.target) target->Release();
    action.target = nullptr;
    if (CEngineObject* effect = effectObject) effect->Release();
    DestroyArray(savedQueue, kServerArrayVtable);
    DestroyArray(currentPlan.animation, kServerArrayVtable);
    motion.excludedBlocks.vtable = ei::GamePointer<void>(AIPath::kExcludedBlocksVtable);
    if (motion.excludedBlocks.data) gamecrt::operator_delete(motion.excludedBlocks.data);
    mfc::impl::DestructList<mfc::impl::CellListClass>(motion.cells);
    mfc::impl::DestructList<mfc::impl::CellListClass>(motion.steps);
    DestroyArray(queue, kServerArrayVtable);
    SetVtable(this, kControllerVtable);
    unit = nullptr;
    static_cast<CEngineObject*>(this)->DestructBase();
}
EI_REWRITTEN(0x00546230, CUnitServer::DestructBody);
