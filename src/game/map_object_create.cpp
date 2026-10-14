// Virtual slot 2 (Create) of map objects (0050E770; Destroy 0050E840 next to it), units (0052E400)
// and torches (0052BFC0): the map object's Create, then the object's number of network fields. See unit.h (CUnit::CreateUnit), torch.h
// (CTorchObject::CreateTorch) and docs/notes/units.md ("Eighth round (U6)").
#include "map_object.h"
#include "game_strings.h"
#include "map_object_figures.h"
#include "net_delta.h"
#include "server_specific_manager.h"   // CTorchObjectSpec
#include "torch.h"
#include "unit.h"

#include "lib/crt.h"
#include "lib/mfc.h"

#include "ei/hook.h"
#include "originals_library.h"
#include "originals_units.h"

#include <cstdint>

namespace {
constexpr int kUnitNetFields = 14;
constexpr int kTorchNetFields = 5;

// Not rewritten (forwarders):
// 0050FA30. CArray<DeltaField>::SetSize (growBy -1 keeps the step). Both functions below inline it;
// the inlined copies do exactly what it does.
inline void NetFieldsSetSize(DeltaState* state, void*, int size, int growBy) { NetFieldsSetSizeOriginal(state, nullptr, size, growBy); }
} // namespace

// --- CMapObject's Create / Destroy (A-U) ---------------------------------------------------------

namespace {
constexpr int kMapObjectNetFields = 4;
constexpr int kInitialCountdown = 5;      // CMapObject::unknown_174

// 00515F70 (object3d_lifetime.cpp), called through its game address so its verify wrapper stays in
// the path (this is its only caller besides the 3D object's vtable).
__declspec(naked) int __fastcall CallCreate3D(CObject3D*) EI_ORIGINAL(0x00515F70)

void DetachCompanion(CMapObject* object) {
    if (CEngineObject* companion = object->companion) {
        CompanionOwner(companion) = nullptr;
        companion->Release();
    }
    object->companion = nullptr;
}
} // namespace

int CMapObject::CreateMapObject() {
    CallCreate3D(Object3D());
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
    baseScale.x = baseScale.y = baseScale.z = 0.0f;
    return 1;
}
EI_REPLACE_METHOD(0x0050E770, CMapObject::CreateMapObject);

void CMapObject::DestroyMapObject() {
    EffectArraySetSizeOriginal(&effectsVtable, nullptr, 0, -1);
    DetachCompanion(this);
    Object3D()->Destroy3D();
}
EI_REPLACE_METHOD(0x0050E840, CMapObject::DestroyMapObject);

// --- Units and torches -----------------------------------------------------------------------------

int CUnit::CreateUnit() {
    MapObjectCreate(this);
    Reset();
    NetFieldsSetSize(MapObject()->NetState(), nullptr, kUnitNetFields, -1);
    return 1;
}
EI_REPLACE_METHOD(0x0052E400, CUnit::CreateUnit);

int CTorchObject::CreateTorch() {
    MapObjectCreate(this);
    CTorchObjectSpec* old = spec;
    spec = nullptr;
    if (old) old->Release();
    DeltaState* state = NetState();
    NetFieldsSetSize(state, nullptr, kTorchNetFields, -1);
    state->Reset();
    return 1;
}
EI_REPLACE_METHOD(0x0052BFC0, CTorchObject::CreateTorch);
