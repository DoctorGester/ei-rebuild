// Constructors and destructors of the map object family: CObject3D, CMapObject, CUnit, their
// server / client companions and the server-side unit controller. See
// docs/notes/map_objects.md ("Lifetime").
#include "combat.h"
#include "core_streams.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "unit.h"

#include "lib/crt.h"
#include "lib/mfc.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "originals_effects.h"
#include "originals_library.h"
#include "originals_ui.h"
#include "originals_units.h"
#include "originals_core.h"

#include <cstring>

namespace {
template <typename T>
T& At(void* base, int offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(base) + offset);
}
void SetVtable(void* object, std::uintptr_t vtable) { At<std::uintptr_t>(object, 0) = vtable; }
void SetDword(void* object, int offset, std::uint32_t value) { At<std::uint32_t>(object, offset) = value; }
// An empty CString (afxEmptyString's data) at `offset`.
void SetEmptyString(void* object, int offset) { At<char*>(object, offset) = *reinterpret_cast<char**>(0x00796E48); }

// Vtables.
constexpr std::uintptr_t kEngineObjectVtable = 0x0073B8E0;
constexpr std::uintptr_t kTransformVtable = 0x0073BA38;
constexpr std::uintptr_t kObject3DVtable = 0x0073DDD4;
constexpr std::uintptr_t kPartListVtable = 0x0073DE4C;
constexpr std::uintptr_t kFrameArrayVtable = 0x0073DE40;
constexpr std::uintptr_t kMapObjectVtable = 0x0073DAA0;
constexpr std::uintptr_t kEffectArrayVtable = 0x0073DBA0;
constexpr std::uintptr_t kRecordArrayVtable = 0x0073DB94;
// CMemoryStream's second vtable (the network queue's streams, 0073C960).
constexpr std::uintptr_t kQueueStreamVtable = 0x0073C960;
constexpr std::uintptr_t kUnitVtable = 0x0073EA8C;
constexpr std::uintptr_t kCarriedArrayVtable = 0x0073EB84;
constexpr std::uintptr_t kCompanionBaseVtable = 0x0073DC54;
constexpr std::uintptr_t kServerCompanionVtable = 0x0073DD6C;
constexpr std::uintptr_t kClientCompanionVtable = 0x0073DBEC;
constexpr std::uintptr_t kParticleArrayVtable = 0x0073DC48;
constexpr std::uintptr_t kSoundArrayVtable = 0x0073DC3C;
constexpr std::uintptr_t kUnitServerVtable = 0x0073EF38;
constexpr std::uintptr_t kControllerVtable = 0x0073EDBC;
constexpr std::uintptr_t kServerArrayVtable = 0x0073EF2C;

constexpr std::uint32_t kStreamLimit = 2000000000;   // a memory stream's size limit
constexpr std::uint32_t kOne = 0x3F800000;

// Not rewritten, forwarders:
// 0040CB00. Quaternion::SetIdentity.
__declspec(naked) void* __fastcall SetIdentity(void*) EI_ORIGINAL(0x0040CB00)
// 006FE925 / 006FE958 / 006FE9B8. CStringArray constructor, destructor, SetSize.
// 0047FB70. The network record array and its streams: emptied.
__declspec(naked) void __fastcall ResetNetRecords(void*) EI_ORIGINAL(0x0047FB70)
// 00510340. A network record's destructor.
__declspec(naked) void __fastcall DestroyNetRecord(void*) EI_ORIGINAL(0x00510340)
// 00516080. CObject3D: releases its parts, animation and figure.
__declspec(naked) void __fastcall ReleaseParts(CObject3D*) EI_ORIGINAL(0x00516080)
// 004CD080 / 0052E230 / 005C6270. The unit's +0x2A4 array constructor, the +0x2CC constructor
// (empty) and the AI memory sub-object's constructor.
__declspec(naked) void __fastcall ConstructUnitArray(void*) EI_ORIGINAL(0x004CD080)
__declspec(naked) void* __fastcall ConstructUnit2CC(void*) EI_ORIGINAL(0x0052E230)
__declspec(naked) void __fastcall ConstructUnitMemory(void*) EI_ORIGINAL(0x005C6270)
// 0050E840. CMapObject's Destroy (slot 3), called directly.
__declspec(naked) void __fastcall MapObjectDestroy(void*) EI_ORIGINAL(0x0050E840)
// 005121B0. The client companion's particle array: SetSize.
__declspec(naked) void __fastcall SetParticleCount(void*, void*, int, int) EI_ORIGINAL(0x005121B0)
// 006CA720. Light manager: removes a light.
// 00511FB0. The light list (007AF748 + 0xD4): erases the entries with id *id.
__declspec(naked) void __fastcall EraseLightId(void*, void*, const int*) EI_ORIGINAL(0x00511FB0)
// 00511B90. A counted reference: assign.
__declspec(naked) void __fastcall AssignReference(CEngineObject**, void*, CEngineObject*) EI_ORIGINAL(0x00511B90)
// The server unit controller's members (00546230).
__declspec(naked) void __fastcall ControllerClearOrders(void*) EI_ORIGINAL(0x00546480)
__declspec(naked) void __fastcall BrainMemoryDestroy(void*) EI_ORIGINAL(0x00545CB0)
__declspec(naked) void __fastcall BrainListDestroy(void*) EI_ORIGINAL(0x00557410)
__declspec(naked) void __fastcall BrainList90Destroy(void*) EI_ORIGINAL(0x00557890)
void __fastcall BrainObject8CDestroy(void* a0) { RefObjectRelease(a0); }
__declspec(naked) void __fastcall BrainList70Destroy(void*) EI_ORIGINAL(0x005575F0)
__declspec(naked) void __fastcall BrainArrayDestroy(void*) EI_ORIGINAL(0x0052B030)
__declspec(naked) void __fastcall Brain14Destroy(void*) EI_ORIGINAL(0x004CB770)
inline void __fastcall ControllerListDestroy(void* list) { AnimationStepsDestroyOriginal(list); }
__declspec(naked) void __fastcall Controller91CDestroy(void*) EI_ORIGINAL(0x00558020)
// Rewritten elsewhere or here, through their game addresses (verify wrappers in the path):
// 005233B0 UnitVitalsPacked::Reset, 00523370 UnitVitals::Reset, 00523550 UnitParams::Reset,
// 00455930 CEngineObject::DestructBase, and this file's constructors and destructor bodies.
__declspec(naked) int __fastcall ResetPackedVitals(void*) EI_ORIGINAL(0x005233B0)
__declspec(naked) void* __fastcall ResetVitals(void*) EI_ORIGINAL(0x00523370)
__declspec(naked) int __fastcall ResetParams(void*) EI_ORIGINAL(0x00523550)
__declspec(naked) void __fastcall DestructBaseAt(void*) EI_ORIGINAL(0x00455930)
__declspec(naked) CObject3D* __fastcall ConstructObject3DAt(void*) EI_ORIGINAL(0x00515BD0)
__declspec(naked) void __fastcall DestructObject3DAt(void*) EI_ORIGINAL(0x00515D70)
__declspec(naked) CMapObject* __fastcall ConstructMapObjectAt(void*) EI_ORIGINAL(0x0050E2D0)
__declspec(naked) void __fastcall DestructMapObjectAt(void*) EI_ORIGINAL(0x0050E660)
__declspec(naked) void __fastcall ResetUnitAt(void*) EI_ORIGINAL(0x0052E5B0)

// The engine object fields every constructor here sets (the base constructor inlined).
void InitEngineObject(void* object) {
    SetDword(object, 0x04, 0);            // refCount_
    SetDword(object, 0x0C, 0xFFFFFFFFu);  // id_
    SetDword(object, 0x18, 0);
    SetDword(object, 0x10, 0);            // type_
    SetDword(object, 0x08, 0);            // flags_
    SetDword(object, 0x14, 0);            // owner_
}

// A memory stream member (vtable 0073BD90 while it frees its buffer at +0x18, then its base's).
void DestroyStream(void* stream) {
    SetVtable(stream, kMemoryStreamVtable);
    if (void* buffer = At<void*>(stream, 0x18)) gamecrt::operator_delete(buffer);
    SetVtable(stream, kStreamVtable);
}

// A plain CArray member: its vtable, then its data freed.
void DestroyArray(void* array, std::uintptr_t vtable) {
    SetVtable(array, vtable);
    if (void* data = At<void*>(array, 4)) gamecrt::operator_delete(data);
}
} // namespace

// --- CObject3D --------------------------------------------------------------------------------

CObject3D* CObject3D::Construct() {
    SetVtable(this, kEngineObjectVtable);
    InitEngineObject(this);
    SetIdentity(reinterpret_cast<char*>(this) + 0x34);
    SetIdentity(reinterpret_cast<char*>(this) + 0x44);
    SetVtable(this, kTransformVtable);
    SetEmptyString(this, 0xA4);
    // The part list (an MFC CPtrList: head, tail, count, free list, blocks, block size 10).
    SetDword(this, 0xA8, kPartListVtable);
    SetDword(this, 0xB4, 0);
    SetDword(this, 0xB8, 0);
    SetDword(this, 0xB0, 0);
    SetDword(this, 0xAC, 0);
    SetDword(this, 0xBC, 0);
    SetDword(this, 0xC0, 10);
    std::memset(&parts, 0, sizeof(parts));
    std::memset(&pendingParts, 0, sizeof(pendingParts));
    figure = nullptr;
    StringArrayConstruct(reinterpret_cast<char*>(this) + 0x110);
    std::memset(&boundsFlags, 0, 0xE * 4);
    SetDword(this, 0x15C, kFrameArrayVtable);
    SetDword(this, 0x160, 0);
    SetDword(this, 0x16C, 0);
    SetDword(this, 0x168, 0);
    SetDword(this, 0x164, 0);
    SetVtable(this, kObject3DVtable);
    attached = nullptr;
    animation = nullptr;
    std::memset(scale, 0, sizeof(scale));
    std::memcpy(reinterpret_cast<char*>(this) + 0xF4, reinterpret_cast<const void*>(0x007AF1D0), 12);
    return this;
}
EI_REPLACE_METHOD(0x00515BD0, CObject3D::Construct);

void CObject3D::DestructBody() {
    SetVtable(this, kObject3DVtable);
    ReleaseParts(this);
    DestroyArray(reinterpret_cast<char*>(this) + 0x15C, kFrameArrayVtable);
    StringArrayDestruct(reinterpret_cast<char*>(this) + 0x110);
    if (CEngineObject* current = figure) {
        if (--current->refCount_ == 0 && current) ei::VCall<void>(current, 0, 1);
    }
    if (pendingParts.data) gamecrt::operator_delete(pendingParts.data[-1]);
    if (parts.data) gamecrt::operator_delete(parts.data[-1]);
    // The part list: emptied, its blocks freed.
    void* list = reinterpret_cast<char*>(this) + 0xA8;
    SetVtable(list, kPartListVtable);
    auto* blocks = At<mfc::CPlex*>(list, 0x14);
    SetDword(list, 0xC, 0);
    SetDword(list, 0x10, 0);
    SetDword(list, 0x8, 0);
    SetDword(list, 0x4, 0);
    mfc::PlexFreeDataChain(blocks);
    SetDword(list, 0x14, 0);
    At<mfc::CString>(this, 0xA4).~CString();
    SetVtable(this, kTransformVtable);
    DestructBaseAt(this);
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
    SetEmptyString(this, 0x1A8);
    // The network records (+0x1C4, 0xA0 each) and their two memory streams.
    char* records = reinterpret_cast<char*>(this) + 0x1C4;
    SetVtable(records, kRecordArrayVtable);
    SetDword(records, 0x04, 0);
    SetDword(records, 0x10, 0);
    SetDword(records, 0x0C, 0);
    SetDword(records, 0x08, 0);
    char* stream = records + 0x24;
    SetDword(stream, 0x08, kStreamLimit);
    At<std::uint8_t>(stream, 0x05) = 1;
    SetVtable(stream, kMemoryStreamVtable);
    SetDword(stream, 0x18, 0);
    reinterpret_cast<CMemoryStream*>(stream)->Reset();
    SetVtable(stream, kQueueStreamVtable);
    stream = records + 0x4C;
    reinterpret_cast<CStream*>(stream)->ConstructBase();
    SetVtable(stream, kMemoryStreamVtable);
    SetDword(stream, 0x18, 0);
    reinterpret_cast<CMemoryStream*>(stream)->Reset();
    SetVtable(stream, kQueueStreamVtable);
    ResetNetRecords(records);
    SetVtable(this, kMapObjectVtable);
    companion = nullptr;
    ResetPackedVitals(reinterpret_cast<char*>(this) + 0x17C);
    sectorCellX = -1;
    sectorCellY = -1;
    SetDword(this, 0x178, kOne);
    return this;
}
EI_REPLACE_METHOD(0x0050E2D0, CMapObject::Construct);

void CMapObject::DestructBody() {
    SetVtable(this, kMapObjectVtable);
    char* records = reinterpret_cast<char*>(this) + 0x1C4;
    DestroyStream(records + 0x4C);
    DestroyStream(records + 0x24);
    SetVtable(records, kRecordArrayVtable);
    if (char* data = At<char*>(records, 4)) {
        int count = At<int>(records, 8);
        for (int i = 0; i < count; ++i) DestroyNetRecord(data + 0xA0 * i);
        gamecrt::operator_delete(At<void*>(records, 4));
    }
    At<mfc::CString>(this, 0x1A8).~CString();
    effectsVtable = reinterpret_cast<void*>(kEffectArrayVtable);
    if (effects) gamecrt::operator_delete(effects);
    DestructObject3DAt(this);
}
EI_REPLACE_METHOD(0x0050E660, CMapObject::DestructBody);

// --- CUnit ------------------------------------------------------------------------------------

CUnit* CUnit::Construct() {
    ConstructMapObjectAt(this);
    SetEmptyString(this, 0x238);
    // The AI memory sub-object (+0x27C): two arrays, then +0x2A4 and +0x2CC.
    char* memory = reinterpret_cast<char*>(this) + 0x27C;
    SetDword(memory, 0x04, 0);
    SetDword(memory, 0x10, 0);
    SetDword(memory, 0x0C, 0);
    SetDword(memory, 0x08, 0);
    SetVtable(memory, 0x0073EB9C);
    SetDword(memory, 0x18, 0);
    SetDword(memory, 0x24, 0);
    SetDword(memory, 0x20, 0);
    SetDword(memory, 0x1C, 0);
    SetDword(memory, 0x14, 0x0073EB90);
    ConstructUnitArray(memory + 0x28);
    SetVtable(memory + 0x28, 0x0073D490);
    char* sub = memory + 0x50;
    ConstructUnit2CC(sub);
    for (int offset = 0x28; offset <= 0x44; offset += 4) SetDword(sub, offset, 0);
    SetDword(sub, 0x50, 0xFFFFFFFFu);
    ConstructUnitMemory(memory);
    SetDword(memory, 0x3C, 0x3DCCCCCD);   // 0.1
    SetDword(memory, 0x40, 0x3DCCCCCD);
    SetDword(this, 0x49C, kCarriedArrayVtable);
    carried = nullptr;
    carriedGrowBy = 0;
    carriedCapacity = 0;
    carriedCount = 0;
    SetVtable(this, kUnitVtable);
    controller = nullptr;
    player = nullptr;
    ResetUnitAt(this);
    return this;
}
EI_REPLACE_METHOD(0x0052E110, CUnit::Construct);

void CUnit::Reset() {
    At<mfc::CString>(this, 0x238).Empty();
    prototype23C = -1;
    heightOffset = 0.0f;
    sizeClass = 3;
    SetDword(this, 0x328, 0x3F400000);   // 0.75
    SetDword(this, 0x32C, 0x3F000000);   // 0.5
    relationsA = 0;
    relationsB = 0xFFFFFFFFu;
    side = 0;
    if (controller) {
        At<CUnit*>(controller, 0x1C) = nullptr;
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
    SetDword(this, 0x278, 0);
    activeWeapon = 0xFFFFFFFFu;
    ResetParams(reinterpret_cast<char*>(this) + 0x330);
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
    ResetNetRecords(reinterpret_cast<char*>(this) + 0x1C4);
    unknown_438 = 1;
}
EI_REPLACE_METHOD(0x0052E5B0, CUnit::Reset);

void CUnit::DestructBody() {
    SetVtable(this, kUnitVtable);
    // The vtable's slot 5 (0052E740, the release from the player), called directly.
    reinterpret_cast<void(__fastcall*)(CUnit*)>(*reinterpret_cast<void**>(kUnitVtable + 0x14))(this);
    MapObjectDestroy(this);
    ResetUnitAt(this);
    DestroyArray(reinterpret_cast<char*>(this) + 0x49C, kCarriedArrayVtable);
    DestroyArray(reinterpret_cast<char*>(this) + 0x2A4, 0x0073D3E8);
    DestroyArray(reinterpret_cast<char*>(this) + 0x290, 0x0073EBB4);
    DestroyArray(reinterpret_cast<char*>(this) + 0x27C, 0x0073EBA8);
    At<mfc::CString>(this, 0x238).~CString();
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
    SetVtable(this, kCompanionBaseVtable);
    SetDword(this, 0x1C, 0);
    SetEmptyString(this, 0x78);
    SetEmptyString(this, 0x7C);
    StringArrayConstruct(reinterpret_cast<char*>(this) + 0x80);
    SetVtable(this, kServerCompanionVtable);
    SetDword(this, 0x1C, 0);
    ResetVitals(reinterpret_cast<char*>(this) + 0x20);
    return this;
}
EI_REPLACE_METHOD(0x00515610, CMapObjectServer::Construct);

void CMapObjectServer::DestructBody() {
    SetVtable(this, kServerCompanionVtable);
    ResetVitals(reinterpret_cast<char*>(this) + 0x20);
    char* names = reinterpret_cast<char*>(this) + 0x80;
    StringArraySetSize(names, nullptr, 0, -1);
    SetDword(this, 0x1C, 0);
    StringArrayDestruct(names);
    At<mfc::CString>(this, 0x7C).~CString();
    At<mfc::CString>(this, 0x78).~CString();
    SetVtable(this, kCompanionBaseVtable);
    SetDword(this, 0x1C, 0);
    DestructBaseAt(this);
}
EI_REPLACE_METHOD(0x00515720, CMapObjectServer::DestructBody);

CMapObjectClientSpecific* CMapObjectClientSpecific::Construct() {
    InitEngineObject(this);
    SetDword(this, 0x1C, 0);
    // The effect particles (0x10 each) and looping sounds (0x14 each), MFC CArrays.
    SetDword(this, 0x28, kParticleArrayVtable);
    SetDword(this, 0x2C, 0);
    SetDword(this, 0x38, 0);
    SetDword(this, 0x34, 0);
    SetDword(this, 0x30, 0);
    SetDword(this, 0x3C, kSoundArrayVtable);
    SetDword(this, 0x40, 0);
    SetDword(this, 0x4C, 0);
    SetDword(this, 0x48, 0);
    SetDword(this, 0x44, 0);
    SetDword(this, 0x50, 0);   // the light
    SetVtable(this, kClientCompanionVtable);
    SetDword(this, 0x20, 0);
    SetDword(this, 0x24, 0);
    return this;
}
EI_REPLACE_METHOD(0x00510450, CMapObjectClientSpecific::Construct);

void CMapObjectClientSpecific::DestructBody() {
    SetVtable(this, kClientCompanionVtable);
    SetDword(this, 0x20, 0);
    SetDword(this, 0x24, 0);
    char* particles = reinterpret_cast<char*>(this) + 0x28;
    SetParticleCount(particles, nullptr, 0, -1);
    auto*& light = At<CEngineObject*>(this, 0x50);
    if (light) {
        light->flags_ &= ~0x200u;
        if (CEngineObject* shown = light; shown && !(shown->flags_ & 0x80) && !(shown->flags_ & 0x200)) {
            shown->flags_ |= 0x80;
            RemoveLight(*reinterpret_cast<void**>(0x007AF738), nullptr, light);
            if (CEngineObject* owner = At<CEngineObject*>(this, 0x1C)) {
                if (CEngineObject* attached = At<CEngineObject*>(owner, 0x94))
                    ei::VCall<void>(attached, 0x68 / 4, 0);
            }
        }
        int id = light->id_;
        EraseLightId(*reinterpret_cast<char**>(0x007AF748) + 0xD4, nullptr, &id);
        AssignReference(&light, nullptr, nullptr);
    }
    SetDword(this, 0x1C, 0);
    if (light) light->Release();
    DestroyArray(reinterpret_cast<char*>(this) + 0x3C, kSoundArrayVtable);
    SetVtable(particles, kParticleArrayVtable);
    if (char* data = At<char*>(particles, 4)) {
        int count = At<int>(particles, 8);
        for (int i = 0; i < count; ++i)
            if (CEngineObject* source = At<CEngineObject*>(data + 0x10 * i, 8)) source->Release();
        gamecrt::operator_delete(At<void*>(particles, 4));
    }
    SetVtable(this, kCompanionBaseVtable);
    SetDword(this, 0x1C, 0);
    DestructBaseAt(this);
}
EI_REPLACE_METHOD(0x00510560, CMapObjectClientSpecific::DestructBody);

// --- The server unit controller ---------------------------------------------------------------

void CUnitServer::DestructBody() {
    SetVtable(this, kUnitServerVtable);
    ControllerClearOrders(this);
    char* brainBase = reinterpret_cast<char*>(this) + 0x980;
    BrainResetAll(brainBase);
    BrainMemoryDestroy(brainBase + 0xD0);
    BrainListDestroy(brainBase + 0xA4 + 0x14);
    BrainListDestroy(brainBase + 0xA4);
    BrainList90Destroy(brainBase + 0x90);
    if (void* object = At<void*>(brainBase, 0x8C)) BrainObject8CDestroy(object);
    BrainList70Destroy(brainBase + 0x70);
    BrainArrayDestroy(brainBase + 0x30);
    BrainArrayDestroy(brainBase + 0x2C);
    Brain14Destroy(brainBase + 0x14);
    ControllerListDestroy(reinterpret_cast<char*>(this) + 0x960);
    ControllerListDestroy(reinterpret_cast<char*>(this) + 0x944);
    SetDword(this, 0x91C, 0x0073F010);
    Controller91CDestroy(reinterpret_cast<char*>(this) + 0x91C);
    if (auto* object = At<CEngineObject*>(this, 0x1DC)) object->Release();
    if (auto* object = At<CEngineObject*>(this, 0x1BC)) object->Release();
    SetDword(this, 0x1BC, 0);
    if (auto* object = At<CEngineObject*>(this, 0x198)) object->Release();
    SetDword(this, 0x198, 0);
    if (auto* object = At<CEngineObject*>(this, 0x17C)) object->Release();
    DestroyArray(reinterpret_cast<char*>(this) + 0x148, kServerArrayVtable);
    DestroyArray(reinterpret_cast<char*>(this) + 0xC4, kServerArrayVtable);
    char* paths = reinterpret_cast<char*>(this) + 0x3C;
    DestroyArray(paths + 0x70, 0x0073F07C);
    CellListDestroy(paths + 0x3C);
    CellListDestroy(paths + 0x20);
    DestroyArray(reinterpret_cast<char*>(this) + 0x24, kServerArrayVtable);
    SetVtable(this, kControllerVtable);
    SetDword(this, 0x1C, 0);
    DestructBaseAt(this);
}
EI_REPLACE_METHOD(0x00546230, CUnitServer::DestructBody);
