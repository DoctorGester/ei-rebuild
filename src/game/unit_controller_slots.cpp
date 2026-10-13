// Small virtual slots of a unit's controllers (A-U12): the abstract base (vtable 0073EDBC; the
// server's CUnitServer, 0073EF38, and the client's CUnitClientSpecific, 0073ECD8, keep most of its
// empty slots), its lifetime, and the client controller's small methods. Slot names:
// CUnitController (unit.h).
#include "unit.h"
#include "unit_client_models.h"

#include "effects.h"   // CParticleSource

#include "ei/hook.h"
#include "ei/vcall.h"   // SetVtable
#include "lib/crt.h"

namespace {
constexpr std::uintptr_t kControllerVtable = std::uintptr_t(0x0073EDBC);
constexpr std::uintptr_t kClientClassName = std::uintptr_t(0x0078F9C4);   // "CUnitClientSpecific"
constexpr int kClientClassId = 0x3D;

// A particle source as the counted engine object it is (effects.h declares only its own members).
CEngineObject* AsObject(CParticleSource* particleSource) { return reinterpret_cast<CEngineObject*>(particleSource); }

// A marker source stopped, released and forgotten.
void DropMarker(CParticleSource*& marker) {
    if (!marker) return;
    marker->StopAndClear();
    AsObject(marker)->Release();
    marker = nullptr;
}
} // namespace

// --- The base controller's lifetime -----------------------------------------------------------------

// 00533DB0. The destructor body: the base's vtable, no unit, the engine object's.
void __fastcall ControllerDestruct(CUnitController* self) {
    ei::SetVtable(self, kControllerVtable);
    self->unit_ = nullptr;
    self->DestructBase();
}
EI_REPLACE(0x00533DB0, ControllerDestruct);

// 00533D90 (slot 0, RET 4).
CUnitController* __fastcall ControllerDelete(CUnitController* self, void*, unsigned flags) {
    ControllerDestruct(self);
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}
EI_REPLACE(0x00533D90, ControllerDelete);

// 00533DD0 (slot 2): no unit; 1. 00533DE0 (slot 3): no unit.
int __fastcall ControllerCreate(CUnitController* self) {
    self->unit_ = nullptr;
    return 1;
}
EI_REPLACE(0x00533DD0, ControllerCreate);
void __fastcall ControllerDestroy(CUnitController* self) { self->unit_ = nullptr; }
EI_REPLACE(0x00533DE0, ControllerDestroy);

// --- The base controller's empty slots ---------------------------------------------------------------
// Most are under five bytes (too short to hook); the server's and the client's vtables share them
// as listed.

// 00533B90 (slot 13 FiguresLoaded; the server's too): 1.
int __fastcall ControllerFiguresLoaded(CUnitController*) { return 1; }
EI_REPLACE(0x00533B90, ControllerFiguresLoaded);

// Plain RET, nothing: slots 15, 22, 23, 28, 38, 41, 43, 44, 45, 48, 49, 52 and 53 (the server keeps
// 15, 41, 43, 48 and 52; the client 22, 23, 28, 38, 44, 45, 49 and 53), and the client's own 17
// (00534130) and 15 (00535580).
void __fastcall ControllerNothing(CUnitController*) {}
EI_REPLACE(0x00533BB0, ControllerNothing);
EI_REPLACE(0x00533C00, ControllerNothing);
EI_REPLACE(0x00533C10, ControllerNothing);
EI_REPLACE(0x00533C20, ControllerNothing);
EI_REPLACE(0x00533CA0, ControllerNothing);
EI_REPLACE(0x00533CD0, ControllerNothing);
EI_REPLACE(0x00533CF0, ControllerNothing);
EI_REPLACE(0x00533D00, ControllerNothing);
EI_REPLACE(0x00533D10, ControllerNothing);
EI_REPLACE(0x00533D40, ControllerNothing);
EI_REPLACE(0x00533D50, ControllerNothing);
EI_REPLACE(0x00533D70, ControllerNothing);
EI_REPLACE(0x00533D80, ControllerNothing);
EI_REPLACE(0x00534130, ControllerNothing);
EI_REPLACE(0x00535580, ControllerNothing);

// RET 4, nothing: slots 14 (RestartAnimation), 18 (PlayerChanged), 40 (AdvanceTime), 42
// (ObjectDeleted; the client's too) and 47 (Wound).
void __fastcall ControllerIgnore(CUnitController*, void*, int) {}
EI_REPLACE(0x00533BA0, ControllerIgnore);
EI_REPLACE(0x00533BC0, ControllerIgnore);
EI_REPLACE(0x00533CC0, ControllerIgnore);
EI_REPLACE(0x00533CE0, ControllerIgnore);
EI_REPLACE(0x00533D30, ControllerIgnore);

// RET 8, nothing: slots 32 (SetArmourModels), 33 (SetWeaponModels) and 51 (Say).
void __fastcall ControllerIgnoreTwo(CUnitController*, void*, int, int) {}
EI_REPLACE(0x00533C60, ControllerIgnoreTwo);
EI_REPLACE(0x00533C70, ControllerIgnoreTwo);
EI_REPLACE(0x00533D60, ControllerIgnoreTwo);

// 00533D20 (slot 46 StatChanged, RET 0xC): nothing.
void __fastcall ControllerIgnoreThree(CUnitController*, void*, int, int, int) {}
EI_REPLACE(0x00533D20, ControllerIgnoreThree);

// 0: slots 29 (RefreshEquipment), 30 (RefreshModel), 31 (RefreshWounds) and 39 (TamingStage; the
// client's too).
int __fastcall ControllerZero(CUnitController*) { return 0; }
EI_REPLACE(0x00533C30, ControllerZero);
EI_REPLACE(0x00533C40, ControllerZero);
EI_REPLACE(0x00533C50, ControllerZero);
EI_REPLACE(0x00533CB0, ControllerZero);

// 0 with one argument (RET 4): slots 19 (RefreshVitals) and 21 (Heal); the client's too.
int __fastcall ControllerZeroOne(CUnitController*, void*, int) { return 0; }
EI_REPLACE(0x00533BD0, ControllerZeroOne);
EI_REPLACE(0x00533BF0, ControllerZeroOne);

// 00533BE0 (slot 20 TakeHit, RET 0xC; the client's too): 0.
int __fastcall ControllerZeroThree(CUnitController*, void*, int, int, int) { return 0; }
EI_REPLACE(0x00533BE0, ControllerZeroThree);

// False in AL (RET 4): slots 34 (WeaponAdded) and 35 (RemoveWeaponModel).
bool __fastcall ControllerFalse(CUnitController*, void*, int) { return false; }
EI_REPLACE(0x00533C80, ControllerFalse);
EI_REPLACE(0x00533C90, ControllerFalse);

// --- The client controller -------------------------------------------------------------------------------

// 00533DF0 (slot 16): the order part of the unit's path. Four bytes (too short to hook).
AIPath* __fastcall ClientControllerPath(CUnitClientSpecific* self) { return self->MotionPath(); }
EI_REPLACE(0x00533DF0, ClientControllerPath);

// 00533E00 / 00533E10 (slots 11 / 12).
const char* __fastcall ClientControllerClassName(CUnitClientSpecific*) {
    return reinterpret_cast<const char*>(kClientClassName);
}
EI_REPLACE(0x00533E00, ClientControllerClassName);
int __fastcall ClientControllerClassId(CUnitClientSpecific*) { return kClientClassId; }
EI_REPLACE(0x00533E10, ClientControllerClassId);

void CUnitClientSpecific::SetMarkers(CParticleSource* path, CParticleSource* marker) {
    DropMarker(sources[1]);
    DropMarker(sources[2]);
    if (path) {
        sources[1] = path;
        ++path->refCount_;
    }
    if (marker) {
        sources[2] = marker;
        ++marker->refCount_;
    }
}
EI_REPLACE_METHOD(0x00535240, CUnitClientSpecific::SetMarkers);

void CUnitClientSpecific::DropFaceTexture() {
    CEngineObject* texture = faceTexture;
    faceTexture = nullptr;
    if (texture) texture->Release();
    blinks = 0;
}
EI_REPLACE_METHOD(0x00538EA0, CUnitClientSpecific::DropFaceTexture);
