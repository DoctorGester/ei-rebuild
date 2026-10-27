// Small virtual slots of a unit's controllers: the abstract base (the
// server's CUnitServer and the client's CUnitClientSpecific keep most of its
// empty slots), its lifetime and the client controller's small methods. Slot names: see CUnitController
// in unit.h.
#include "unit.h"
#include "unit_client_models.h"

#include "effects.h"   // CParticleSource
#include "texture.h"   // CTexture

#include "ei/hook.h"

#include <cstdint>

namespace {
constexpr const char* kClientClassName = "CUnitClientSpecific";
constexpr int kClientClassId = 0x3D;

// A marker source stopped, released and forgotten.
void DropMarker(CParticleSource*& marker) {
    if (!marker) return;
    marker->StopAndClear();
    marker->Release();
    marker = nullptr;
}
} // namespace

// --- The base controller's lifetime -----------------------------------------------------------------

// No unit.
CUnitController::~CUnitController() { unit = nullptr; }
EI_REWRITTEN_SPECIAL(0x00533DB0, CUnitController::~CUnitController);
EI_REWRITTEN_SPECIAL(0x00533D90, CUnitController::~CUnitController);

// No unit; 1: no unit.
int CUnitController::Create() {
    unit = nullptr;
    return 1;
}
EI_REWRITTEN(0x00533DD0, CUnitController::Create);
void CUnitController::Destroy() { unit = nullptr; }
EI_REWRITTEN(0x00533DE0, CUnitController::Destroy);

// --- The base controller's empty slots ---------------------------------------------------------------
// Most are under five bytes (too short to hook); the server's and the client's vtables share them
// as listed.

// Answers 1.
int CUnitController::FiguresLoaded() { return 1; }
EI_REWRITTEN(0x00533B90, CUnitController::FiguresLoaded);

// Do-nothing virtuals: 15, 22, 23, 28, 38, 41, 43, 44, 45, 48, 49, 52 and 53 (the server keeps
// 15, 41, 43, 48 and 52; the client 22, 23, 28, 38, 44, 45, 49 and 53), and the client's own 17
// and 15.
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

// Do-nothing virtuals with one argument: 14 (RestartAnimation), 18 (PlayerChanged), 40 (AdvanceTime), 42
// (ObjectDeleted; the client's too) and 47 (Wound).
void CUnitController::IgnoreArgument(int) {}
EI_REPLACE_METHOD(0x00533BA0, CUnitController::IgnoreArgument);
EI_REPLACE_METHOD(0x00533BC0, CUnitController::IgnoreArgument);
EI_REPLACE_METHOD(0x00533CC0, CUnitController::IgnoreArgument);
EI_REPLACE_METHOD(0x00533CE0, CUnitController::IgnoreArgument);
EI_REPLACE_METHOD(0x00533D30, CUnitController::IgnoreArgument);

// Do-nothing virtuals with two arguments: 32 (SetArmourModels), 33 (SetWeaponModels) and 51 (Say).
void CUnitController::IgnoreTwoArguments(int, int) {}
EI_REPLACE_METHOD(0x00533C60, CUnitController::IgnoreTwoArguments);
EI_REPLACE_METHOD(0x00533C70, CUnitController::IgnoreTwoArguments);
EI_REPLACE_METHOD(0x00533D60, CUnitController::IgnoreTwoArguments);

// Nothing.
void CUnitController::IgnoreThreeArguments(int, int, int) {}
EI_REPLACE_METHOD(0x00533D20, CUnitController::IgnoreThreeArguments);

// 0: slots 29 (RefreshEquipment), 30 (RefreshModel), 31 (RefreshWounds) and 39 (TamingStage; the
// client's too).
int __fastcall ControllerZero(CUnitController*) { return 0; }
EI_REPLACE(0x00533C30, ControllerZero);
EI_REPLACE(0x00533C40, ControllerZero);
EI_REPLACE(0x00533C50, ControllerZero);
EI_REPLACE(0x00533CB0, ControllerZero);

// 0 with one argument: slots 19 (RefreshVitals) and 21 (Heal); the client's too.
int CUnitController::RefreshVitals(char) { return 0; }
EI_REWRITTEN(0x00533BD0, CUnitController::RefreshVitals);
int CUnitController::Heal(float) { return 0; }
EI_REWRITTEN(0x00533BF0, CUnitController::Heal);

// Answers 0.
int CUnitController::TakeHit(DamageInfo*, CUnit*, CUnit*) { return 0; }
EI_REWRITTEN(0x00533BE0, CUnitController::TakeHit);

// False in AL: slots 34 (WeaponAdded) and 35 (RemoveWeaponModel).
bool CUnitController::WeaponAdded(std::uint32_t) { return false; }
EI_REWRITTEN(0x00533C80, CUnitController::WeaponAdded);
bool CUnitController::RemoveWeaponModel(std::uint32_t) { return false; }
EI_REWRITTEN(0x00533C90, CUnitController::RemoveWeaponModel);

// --- The client controller -------------------------------------------------------------------------------

// The order part of the unit's path. Four bytes (too short
// to hook).
PathOrder* __fastcall ClientControllerPath(CUnitClientSpecific* self) { return self->MotionPath(); }
EI_REPLACE(0x00533DF0, ClientControllerPath);

const char* CUnitClientSpecific::GetClassName() { return kClientClassName; }
EI_REWRITTEN(0x00533E00, CUnitClientSpecific::GetClassName);
int CUnitClientSpecific::GetClassId() { return kClientClassId; }
EI_REWRITTEN(0x00533E10, CUnitClientSpecific::GetClassId);

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
EI_REWRITTEN(0x00535240, CUnitClientSpecific::SetMarkers);

void CUnitClientSpecific::DropFaceTexture() {
    CTexture* texture = faceTexture;
    faceTexture = nullptr;
    if (texture) texture->Release();
    blinks = 0;
}
EI_REWRITTEN(0x00538EA0, CUnitClientSpecific::DropFaceTexture);
