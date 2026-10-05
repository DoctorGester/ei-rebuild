// Virtual slot 2 (Create) of units (0052E400) and torches (0052BFC0): the map object's Create,
// then the object's number of network fields. See unit.h (CUnit::CreateUnit), map_object.h
// (TorchCreate) and docs/notes/units.md ("Eighth round (U6)").
#include "map_object.h"
#include "net_delta.h"
#include "unit.h"

#include "ei/hook.h"

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
