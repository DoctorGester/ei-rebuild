// Virtual slot 2 (Create) of map objects (0050E770; Destroy 0050E840 next to it), units (0052E400)
// and torches (0052BFC0): the map object's Create, then the object's number of network fields. See unit.h (CUnit::CreateUnit), map_object.h
// (TorchCreate) and docs/notes/units.md ("Eighth round (U6)").
#include "map_object.h"
#include "game_strings.h"
#include "map_object_figures.h"
#include "net_delta.h"
#include "unit.h"

#include "lib/crt.h"
#include "lib/mfc.h"

#include "ei/hook.h"
#include "originals_library.h"

#include <cstdint>

namespace {
constexpr int kNetState = 0x1C4;          // the DeltaState (its fields: a CArray of 0xA0-byte records)
constexpr int kUnitNetFields = 14;
constexpr int kTorchNetFields = 5;
constexpr int kTorchAttached = 0x238;     // a counted object (the torch's light or flame)

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(object) + offset);
}

DeltaState* NetState(void* object) {
    return reinterpret_cast<DeltaState*>(static_cast<std::uint8_t*>(object) + kNetState);
}

// Not rewritten (forwarders):
// 0050E770. CMapObject's virtual slot 2 (Create): back to a blank map object. Returns 1.
__declspec(naked) int __fastcall MapObjectCreate(void*) EI_ORIGINAL(0x0050E770)
// 0050FA30. CArray<DeltaField>::SetSize (growBy -1 keeps the step). Both functions below inline it;
// the inlined copies do exactly what it does.
__declspec(naked) void __fastcall NetFieldsSetSize(DeltaState*, void*, int, int) EI_ORIGINAL(0x0050FA30)
} // namespace

// --- CMapObject's Create / Destroy (A-U) ---------------------------------------------------------

namespace {
constexpr int kMapObjectNetFields = 4;
constexpr int kInitialCountdown = 5;      // CMapObject::unknown_174

CObject3D* AsObject3D(CMapObject* object) { return reinterpret_cast<CObject3D*>(object); }

void DetachCompanion(CMapObject* object) {
    if (CEngineObject* companion = object->companion) {
        CompanionOwner(companion) = nullptr;
        companion->Release();
    }
    object->companion = nullptr;
}
} // namespace

int CMapObject::CreateMapObject() {
    AsObject3D(this)->Create3D();
    DetachCompanion(this);
    PackedVitals()->Reset();
    unknown_178 = 1.0f;
    PackedVitals()->Reset();
    NetFieldsSetSize(NetState(), nullptr, kMapObjectNetFields, -1);
    NetState()->Reset();
    changeStamp = -1;
    if (effects) {
        gamecrt::operator_delete(effects);
        effects = nullptr;
    }
    effectCapacity = 0;
    effectCount = 0;
    unknown_174 = kInitialCountdown;
    *reinterpret_cast<mfc::CString*>(&name2) = game::EmptyText();
    baseScale[0] = baseScale[1] = baseScale[2] = 0.0f;
    return 1;
}
EI_REPLACE_METHOD(0x0050E770, CMapObject::CreateMapObject);

void CMapObject::DestroyMapObject() {
    EffectArraySetSizeOriginal(&effectsVtable, nullptr, 0, -1);
    DetachCompanion(this);
    AsObject3D(this)->Destroy3D();
}
EI_REPLACE_METHOD(0x0050E840, CMapObject::DestroyMapObject);

// --- Units and torches -----------------------------------------------------------------------------

int CUnit::CreateUnit() {
    MapObjectCreate(this);
    Reset();
    NetFieldsSetSize(NetState(this), nullptr, kUnitNetFields, -1);
    return 1;
}
EI_REPLACE_METHOD(0x0052E400, CUnit::CreateUnit);

int __fastcall TorchCreate(CMapObject* torch) {
    MapObjectCreate(torch);
    CEngineObject*& attached = At<CEngineObject*>(torch, kTorchAttached);
    CEngineObject* old = attached;
    attached = nullptr;
    if (old) old->Release();
    DeltaState* state = NetState(torch);
    NetFieldsSetSize(state, nullptr, kTorchNetFields, -1);
    state->Reset();
    return 1;
}
EI_REPLACE(0x0052BFC0, TorchCreate);
