// Constructors and destructors of the map object family: CObject3D, CMapObject, CUnit, their
// server / client companions and the server-side unit controller. See
// docs/notes/map_objects.md ("Lifetime").
#include "ai_map.h"        // AIPath
#include "ai_motion.h"     // UnitMotion
#include "combat.h"
#include "core_streams.h"
#include "core_transform.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "net_delta.h"     // DeltaState
#include "spell.h"         // CLightFactory
#include "unit.h"
#include "unit_ai.h"       // AIBrain

#include "lib/crt.h"
#include "lib/mfc.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "originals_effects.h"
#include "originals_library.h"
#include "originals_ui.h"
#include "originals_units.h"
#include "originals_core.h"
#include "originals_ai.h"

#include <cstring>
#include <new>

namespace {
using ei::SetVtable;

// Vtables.
constexpr std::uintptr_t kPartListVtable = std::uintptr_t(0x0073DE4C);
constexpr std::uintptr_t kFrameArrayVtable = std::uintptr_t(0x0073DE40);
constexpr std::uintptr_t kEffectArrayVtable = std::uintptr_t(0x0073DBA0);
constexpr std::uintptr_t kRecordArrayVtable = std::uintptr_t(0x0073DB94);
// CMemoryStream's second vtable (the network queue's streams, 0073C960).
constexpr std::uintptr_t kQueueStreamVtable = std::uintptr_t(0x0073C960);
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
const void* DefaultFigureScale() { return reinterpret_cast<const void*>(0x007AF1D0); }

// A float stored as the dword the original moves.
void SetBits(float& field, std::uint32_t bits) { std::memcpy(&field, &bits, 4); }

// Not rewritten, forwarders:
// 0040CB00. Quaternion::SetIdentity.
__declspec(naked) void* __fastcall SetIdentity(Quaternion*) EI_ORIGINAL(0x0040CB00)
// 006FE925 / 006FE958 / 006FE9B8. CStringArray constructor, destructor, SetSize.
// 0047FB70. The network record array and its streams: emptied.
__declspec(naked) void __fastcall ResetNetRecords(DeltaState*) EI_ORIGINAL(0x0047FB70)
// 00510340. A network record's destructor.
__declspec(naked) void __fastcall DestroyNetRecord(DeltaField*) EI_ORIGINAL(0x00510340)
// 00516080. CObject3D: releases its parts, animation and figure.
__declspec(naked) void __fastcall ReleaseParts(CObject3D*) EI_ORIGINAL(0x00516080)
// 004CD080 / 0052E230 / 005C6270. The unit motion's step speed array constructor, the constructor
// of its segment part (+0x50, empty) and the motion's own constructor.
__declspec(naked) void __fastcall ConstructUnitArray(mfc::CArray<float>*) EI_ORIGINAL(0x004CD080)
__declspec(naked) void* __fastcall ConstructUnit2CC(float*) EI_ORIGINAL(0x0052E230)
__declspec(naked) void __fastcall ConstructUnitMemory(UnitMotion*) EI_ORIGINAL(0x005C6270)
inline void __fastcall SetParticleCount(void* a0, void*, int a2, int a3) { CompanionParticlesSetSizeOriginal(a0, nullptr, a2, a3); }
// 006CA720. Light manager: removes a light.
// 00511FB0. The light list (CLightFactory::litIds_): erases the entries with id *id.
// 00511B90. A counted reference: assign.
void __fastcall AssignReference(CEngineObject** a0, void*, CEngineObject* a2) { CountedRefStoreOriginal(a0, nullptr, a2); }
// The server unit controller's members (00546230).
__declspec(naked) void __fastcall ControllerClearOrders(void*) EI_ORIGINAL(0x00546480)
__declspec(naked) void __fastcall BrainMemoryDestroy(void*) EI_ORIGINAL(0x00545CB0)
__declspec(naked) void __fastcall BrainListDestroy(void*) EI_ORIGINAL(0x00557410)
__declspec(naked) void __fastcall BrainList90Destroy(void*) EI_ORIGINAL(0x00557890)
void __fastcall BrainObject8CDestroy(void* a0) { RefObjectRelease(a0); }
__declspec(naked) void __fastcall BrainList70Destroy(void*) EI_ORIGINAL(0x005575F0)
void __fastcall BrainArrayDestroy(void* a0) { CountedRefResetOriginal(a0); }
void __fastcall Brain14Destroy(void* a0) { AiLookArrayDestructOriginal(a0); }
inline void __fastcall ControllerListDestroy(void* list) { AnimationStepsDestroyOriginal(list); }
__declspec(naked) void __fastcall Controller91CDestroy(void*) EI_ORIGINAL(0x00558020)
// Rewritten elsewhere or here, through their game addresses (verify wrappers in the path):
// 005233B0 UnitVitalsPacked::Reset, 00523370 UnitVitals::Reset, 00523550 UnitParams::Reset,
// 00455930 CEngineObject::DestructBase, and this file's constructors and destructor bodies.
__declspec(naked) int __fastcall ResetPackedVitals(void*) EI_ORIGINAL(0x005233B0)
__declspec(naked) int __fastcall ResetParams(void*) EI_ORIGINAL(0x00523550)
__declspec(naked) CObject3D* __fastcall ConstructObject3DAt(void*) EI_ORIGINAL(0x00515BD0)
__declspec(naked) void __fastcall DestructObject3DAt(void*) EI_ORIGINAL(0x00515D70)
__declspec(naked) CMapObject* __fastcall ConstructMapObjectAt(void*) EI_ORIGINAL(0x0050E2D0)
__declspec(naked) void __fastcall ResetUnitAt(void*) EI_ORIGINAL(0x0052E5B0)

// The engine object fields every constructor here sets (the base constructor inlined).
void InitEngineObject(CEngineObject* object) {
    object->refCount_ = 0;
    object->id_ = -1;
    object->unknown_18 = 0;
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
    InitEngineObject(this);
    SetIdentity(&orientation);
    SetIdentity(&worldOrientation);
    SetVtable(this, CTransformObject::kVtableAddress);
    ConstructString(&name);
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
    StringArrayConstruct(&partNameArrayVtable);
    std::memset(&boundsFlags, 0, 0xE * 4);
    partBoxArrayVtable = reinterpret_cast<void*>(kFrameArrayVtable);
    partBoxes = nullptr;
    partBoxGrowBy = 0;
    partBoxCapacity = 0;
    partBoxCount = 0;
    SetVtable(this, CObject3D::kVtableAddress);
    attached = nullptr;
    animation = nullptr;
    std::memset(&scale, 0, sizeof(scale));
    std::memcpy(&defaultScale, DefaultFigureScale(), sizeof(defaultScale));
    return this;
}
EI_REPLACE_METHOD(0x00515BD0, CObject3D::Construct);

void CObject3D::DestructBody() {
    SetVtable(this, CObject3D::kVtableAddress);
    ReleaseParts(this);
    partBoxArrayVtable = reinterpret_cast<void*>(kFrameArrayVtable);
    if (partBoxes) gamecrt::operator_delete(partBoxes);
    StringArrayDestruct(&partNameArrayVtable);
    if (CEngineObject* current = figure) {
        if (--current->refCount_ == 0 && current) delete current;   // slot 0 with 1
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
    DestroyString(&name);
    SetVtable(this, CTransformObject::kVtableAddress);
    EngineObjectDestructBase(this);
}
EI_REPLACE_METHOD(0x00515D70, CObject3D::DestructBody);

// --- CMapObject -------------------------------------------------------------------------------

CMapObject* CMapObject::Construct() {
    ConstructObject3DAt(this);
    effectsVtable = reinterpret_cast<void*>(kEffectArrayVtable);
    effects = nullptr;
    effectGrowBy = 0;
    effectCapacity = 0;
    effectCount = 0;
    ConstructString(&name2);
    // The network records (DeltaField, 0xA0 each) and their two memory streams.
    DeltaState* records = NetState();
    SetVtable(records, kRecordArrayVtable);
    records->fields_ = nullptr;
    records->unknown_10 = 0;
    records->unknown_C = 0;
    records->fieldCount_ = 0;
    CMemoryStream* stream = &records->header_[0];
    stream->version_ = kStreamLimit;
    stream->ok_ = 1;
    SetVtable(stream, kMemoryStreamVtable);
    stream->data_ = nullptr;
    stream->Reset();
    SetVtable(stream, kQueueStreamVtable);
    stream = &records->header_[1];
    stream->ConstructBase();
    SetVtable(stream, kMemoryStreamVtable);
    stream->data_ = nullptr;
    stream->Reset();
    SetVtable(stream, kQueueStreamVtable);
    ResetNetRecords(records);
    SetVtable(this, CMapObject::kVtableAddress);
    companion = nullptr;
    ResetPackedVitals(PackedVitals());
    sectorCellX = -1;
    sectorCellY = -1;
    SetBits(animationSpeed, kOne);
    return this;
}
EI_REPLACE_METHOD(0x0050E2D0, CMapObject::Construct);

void CMapObject::DestructBody() {
    SetVtable(this, CMapObject::kVtableAddress);
    DeltaState* records = NetState();
    DestroyStream(&records->header_[1]);
    DestroyStream(&records->header_[0]);
    SetVtable(records, kRecordArrayVtable);
    if (DeltaField* data = records->fields_) {
        int count = records->fieldCount_;
        for (int i = 0; i < count; ++i) DestroyNetRecord(data + i);
        gamecrt::operator_delete(records->fields_);
    }
    DestroyString(&name2);
    effectsVtable = reinterpret_cast<void*>(kEffectArrayVtable);
    if (effects) gamecrt::operator_delete(effects);
    DestructObject3DAt(this);
}
EI_REPLACE_METHOD(0x0050E660, CMapObject::DestructBody);

// --- CUnit ------------------------------------------------------------------------------------

CUnit* CUnit::Construct() {
    ConstructMapObjectAt(this);
    ConstructString(&scriptName);
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
    ConstructUnitArray(&m->stepSpeeds);
    SetVtable(&m->stepSpeeds, kMotionSpeedsVtable);
    ConstructUnit2CC(&m->fromX);
    std::memset(&m->curveX, 0, sizeof(m->curveX));
    std::memset(&m->curveY, 0, sizeof(m->curveY));
    m->segment = -1;
    ConstructUnitMemory(m);
    SetBits(m->speedScale, kTenth);
    SetBits(m->baseTurnRate, kTenth);
    unknown_49C = static_cast<std::uint32_t>(kCarriedArrayVtable);
    carried = nullptr;
    carriedGrowBy = 0;
    carriedCapacity = 0;
    carriedCount = 0;
    SetVtable(this, CUnit::kVtableAddress);
    controller = nullptr;
    player = nullptr;
    ResetUnitAt(this);
    return this;
}
EI_REPLACE_METHOD(0x0052E110, CUnit::Construct);

void CUnit::Reset() {
    reinterpret_cast<mfc::CString*>(&scriptName)->Empty();
    prototype23C = -1;
    heightOffset = 0.0f;
    sizeClass = 3;
    SetBits(unknown_328, kThreeQuarters);
    SetBits(standingHeight, kHalf);
    relationsA = 0;
    relationsB = 0xFFFFFFFFu;
    side = 0;
    if (controller) {
        static_cast<CUnitServer*>(controller)->unit = nullptr;
        controller->Release();
    }
    unknown_254 = -1;
    unknown_255 = -1;
    controller = nullptr;
    player = nullptr;
    unknown_256 = 0;
    unknown_258 = 0;
    unknown_25C = 0;
    unknown_260 = 0;
    moveMode = 2;
    moveMode2 = 2;
    unknown_270 = -1;
    dead = 0;
    unknown_278 = 0;
    activeWeapon = 0xFFFFFFFFu;
    ResetParams(Params());
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
    unknown_43C = 0;
    ResetNetRecords(MapObject()->NetState());
    unknown_438 = 1;
}
EI_REPLACE_METHOD(0x0052E5B0, CUnit::Reset);

void CUnit::DestructBody() {
    SetVtable(this, CUnit::kVtableAddress);
    // The vtable's slot 5 (0052E740, the release from the player), called through the vtable's
    // memory as the original does.
    using Slot = void(__fastcall*)(CUnit*);
    reinterpret_cast<const Slot*>(CUnit::kVtableAddress)[5](this);
    MapObjectDestroy(this);
    ResetUnitAt(this);
    unknown_49C = static_cast<std::uint32_t>(kCarriedArrayVtable);
    if (carried) gamecrt::operator_delete(carried);
    UnitMotion* m = Motion();
    DestroyArray(m->stepSpeeds, kMotionSpeedsEndVtable);
    DestroyArray(m->waypoints, kMotionWaypointsEndVtable);
    DestroyArray(m->points, kMotionPointsEndVtable);
    DestroyString(&scriptName);
    DestructMapObjectAt(this);
}
EI_REPLACE_METHOD(0x0052E310, CUnit::DestructBody);

CUnit* CUnit::DeletingDestructor(std::uint32_t flags) {
    UnitDestructBody(this);
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
    StringArrayConstruct(&stringsVtable);
    SetVtable(this, CMapObjectServer::kVtableAddress);
    owner = nullptr;
    UnitVitalsReset(&vitals);
    return this;
}
EI_REPLACE_METHOD(0x00515610, CMapObjectServer::Construct);

void CMapObjectServer::DestructBody() {
    SetVtable(this, CMapObjectServer::kVtableAddress);
    UnitVitalsReset(&vitals);
    StringArraySetSize(&stringsVtable, nullptr, 0, -1);
    owner = nullptr;
    StringArrayDestruct(&stringsVtable);
    name7C.~CString();
    name78.~CString();
    SetVtable(this, CMapObjectCompanion::kVtableAddress);
    owner = nullptr;
    EngineObjectDestructBase(this);
}
EI_REPLACE_METHOD(0x00515720, CMapObjectServer::DestructBody);

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
    unknown_20 = 0;
    unknown_24 = 0;
    return this;
}
EI_REPLACE_METHOD(0x00510450, CMapObjectClientSpecific::Construct);

void CMapObjectClientSpecific::DestructBody() {
    SetVtable(this, CMapObjectClientSpecific::kVtableAddress);
    unknown_20 = 0;
    unknown_24 = 0;
    SetParticleCount(&particles, nullptr, 0, -1);
    if (light) {
        light->flags_ &= ~0x200u;
        if (CEngineObject* shown = light; shown && !(shown->flags_ & 0x80) && !(shown->flags_ & 0x200)) {
            shown->flags_ |= 0x80;
            RemoveLight(LightManager(), nullptr, light);
            if (CUnit* unit = Unit()) {
                if (CEngineObject* half = unit->Object3D()->attached)
                    static_cast<CObject3DSpecific*>(half)->SetLit(0);
            }
        }
        int id = light->id_;
        LightIdsRemoveOriginal(&TheLightFactory()->litIds_, nullptr, &id);
        AssignReference(&light, nullptr, nullptr);
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
    EngineObjectDestructBase(this);
}
EI_REPLACE_METHOD(0x00510560, CMapObjectClientSpecific::DestructBody);

// --- The server unit controller ---------------------------------------------------------------

void CUnitServer::DestructBody() {
    SetVtable(this, kUnitServerVtable);
    ControllerClearOrders(this);
    AIBrain* b = Brain();
    BrainResetAll(b);
    BrainMemoryDestroy(&b->memory);
    BrainListDestroy(&b->attention.spells);
    BrainListDestroy(&b->attention.weapons);
    BrainList90Destroy(&b->alarmRecords);
    if (AIMode* current = b->current) BrainObject8CDestroy(current);
    BrainList70Destroy(&b->modes);
    BrainArrayDestroy(&b->seen);
    BrainArrayDestroy(&b->target);
    Brain14Destroy(&b->looks);
    ControllerListDestroy(&idleAnimation);
    ControllerListDestroy(&moveAnimation);
    SetVtable(&armourSpells, kArmourSpellListVtable);
    Controller91CDestroy(&armourSpells);
    if (CEngineObject* leader = followedLeader) leader->Release();
    if (CEngineObject* target = action2.target) target->Release();
    action2.target = nullptr;
    if (CEngineObject* target = action.target) target->Release();
    action.target = nullptr;
    if (CEngineObject* effect = effectObject) effect->Release();
    DestroyArray(savedQueue, kServerArrayVtable);
    DestroyArray(currentPlan.animation, kServerArrayVtable);
    motion.pointsVtable = reinterpret_cast<void*>(AIPath::kPointsVtable);
    if (motion.points) gamecrt::operator_delete(motion.points);
    CellListDestroy(&motion.cells);
    CellListDestroy(&motion.steps);
    DestroyArray(queue, kServerArrayVtable);
    SetVtable(this, kControllerVtable);
    unit = nullptr;
    EngineObjectDestructBase(this);
}
EI_REPLACE_METHOD(0x00546230, CUnitServer::DestructBody);
