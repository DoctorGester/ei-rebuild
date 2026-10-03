// CUnit's own virtual slots (vtable 0073EA8C, shared by CFakeUnit 0073BFE8): the per-tick update
// and the slots that pass map object events on to the unit's controller (CUnit + 0x240), plus
// standing on the ground. See unit.h and docs/notes/units.md.
#include "unit.h"

#include "object_tables.h"

#include "ei/hook.h"
#include "ei/vcall.h"

#include <cstring>

#pragma float_control(precise, on)

namespace {
constexpr int kClassUnitServer = 0x3E;        // CUnitServer (combat.h)
constexpr int kControllerType = 0x48;         // CEngineObject::type_ of unit controllers
constexpr std::uint32_t kFlagFiguresLoaded = 0x80;
constexpr std::uint8_t kMovesOnWater = 2;     // CUnit::movementKind

// Controller slots (vtable 0073EDBC and the classes deriving from it).
constexpr int kControllerUpdate = 6;
constexpr int kControllerFiguresLoaded = 13;
constexpr int kControllerScaleEffect = 19;
constexpr int kControllerStatsChanged = 22;
constexpr int kControllerSizeClasses = 23;
constexpr int kControllerScaleChanged = 44;
constexpr int kControllerEffectsChanged = 49;

// World slots: ground height (29) and water surface height (30) at (x, y), in ST0.
constexpr int kWorldGroundHeight = 0x74 / 4;
constexpr int kWorldSurfaceHeight = 0x78 / 4;

CEngineObject* WorldOf(CUnit* unit) { return static_cast<CEngineObject*>(unit->World()); }
} // namespace

// Map object (CMapObject, map_object.cpp) and CObject3D methods, through their game addresses.
// 0050F1C0 / 0050F2D0 are rewritten (map_object.cpp): calling the addresses keeps their verify
// wrappers in the path.
__declspec(naked) int __fastcall CallMapObjectUpdate(CUnit*) EI_ORIGINAL(0x0050F1C0)
__declspec(naked) std::uintptr_t __fastcall CallRecomputeScale(CUnit*) EI_ORIGINAL(0x0050F2D0)
// 00517700 (CObject3D): sets the figures' scale from `scale` (three floats). Not rewritten.
__declspec(naked) std::uintptr_t __fastcall CallSetFigureScale(CUnit*, void*, const float*) EI_ORIGINAL(0x00517700)
// 00517500 (CObject3D slot 26): loads the figures. Not rewritten.
__declspec(naked) std::uintptr_t __fastcall CallLoadFigures(CUnit*) EI_ORIGINAL(0x00517500)
// 0050E890 (CMapObject slot 19): attaches a companion of another type. Not rewritten.
__declspec(naked) std::uintptr_t __fastcall CallAttachCompanion(CUnit*, void*, CEngineObject*) EI_ORIGINAL(0x0050E890)
// 0050E970 (CMapObject slot 32): packs the server object's vitals into +0x17C. Not rewritten.
__declspec(naked) int __fastcall CallMapObjectStatsChanged(CUnit*) EI_ORIGINAL(0x0050E970)
// 00552F40 (CUnitServer): copies `stats` into its own copy. Not rewritten (combat range).
__declspec(naked) std::uintptr_t __fastcall CallServerSetStats(CEngineObject*, void*, const UnitStats*)
    EI_ORIGINAL(0x00552F40)
// 00522CE0 / 00523E80 / 00523E00 (unit_vitals.cpp, unit_stats_pack.cpp).
__declspec(naked) void __fastcall CallPackStats(UnitStats*, void*, CUnit*, UnitParams*) EI_ORIGINAL(0x00522CE0)
__declspec(naked) double __fastcall CallHitPointCurve(UnitStats*) EI_ORIGINAL(0x00523E80)
__declspec(naked) float __stdcall CallScaleHitPoints(CUnit*, float) EI_ORIGINAL(0x00523E00)

namespace {
// The game's _ftol (006E853C): truncation of ST0 to 64 bits, low dword in EAX.
__declspec(naked) int __cdecl GameFtol(double) {
    __asm {
        fld qword ptr [esp + 4]
        mov eax, 0x006E853C
        call eax
        ret
    }
}
} // namespace

int CUnit::Tick() {
    ei::VCall<int>(controller, kControllerUpdate);
    CallMapObjectUpdate(this);
    return 1;
}
EI_REPLACE_METHOD(0x0052F7D0, CUnit::Tick);

std::uintptr_t CUnit::RecomputeUnitScale() {
    CallRecomputeScale(this);
    return ei::VCall<std::uintptr_t>(controller, kControllerScaleChanged);
}
EI_REPLACE_METHOD(0x00532A80, CUnit::RecomputeUnitScale);

std::uintptr_t CUnit::EffectsChanged() { return ei::VCall<std::uintptr_t>(controller, kControllerEffectsChanged); }
EI_REPLACE_METHOD(0x00532AB0, CUnit::EffectsChanged);

std::uintptr_t CUnit::ResetUnitScale() {
    return CallSetFigureScale(this, nullptr, reinterpret_cast<const float*>(reinterpret_cast<char*>(this) + 0x1AC));
}
EI_REPLACE_METHOD(0x00532AA0, CUnit::ResetUnitScale);

std::uintptr_t CUnit::LoadUnitFigures() {
    if (flags_ & kFlagFiguresLoaded) return 1;
    std::uintptr_t result = CallLoadFigures(this);
    ei::VCall<int>(controller, kControllerFiguresLoaded);
    return result;
}
EI_REPLACE_METHOD(0x0052E910, CUnit::LoadUnitFigures);

std::uintptr_t CUnit::AttachCompanion(CEngineObject* companion) {
    if (!companion || companion->type_ != kControllerType) return CallAttachCompanion(this, nullptr, companion);
    if (controller) {
        *reinterpret_cast<CUnit**>(reinterpret_cast<char*>(controller) + 0x1C) = nullptr;
        controller->Release();
    }
    controller = companion;
    *reinterpret_cast<CUnit**>(reinterpret_cast<char*>(companion) + 0x1C) = this;
    return static_cast<std::uintptr_t>(++controller->refCount_);
}
EI_REPLACE_METHOD(0x0052E870, CUnit::AttachCompanion);

int CUnit::StatsChanged() {
    CallMapObjectStatsChanged(this);
    if (controller) ei::VCall<int>(controller, kControllerStatsChanged);
    return 1;
}
EI_REPLACE_METHOD(0x0052EB50, CUnit::StatsChanged);

std::uintptr_t CUnit::ScaleEffectChanged(int value) {
    if (!controller) return 0;
    return ei::VCall<std::uintptr_t>(controller, kControllerScaleEffect, value);
}
EI_REPLACE_METHOD(0x0052EB30, CUnit::ScaleEffectChanged);

std::uintptr_t CUnit::SetServerStats(const UnitStats* stats) {
    int classId = controller->GetClassId();
    if (classId != kClassUnitServer) return static_cast<std::uintptr_t>(classId);
    return CallServerSetStats(controller, nullptr, stats);
}
EI_REPLACE_METHOD(0x00530A20, CUnit::SetServerStats);

std::uintptr_t CUnit::UpdateSizeClasses() { return ei::VCall<std::uintptr_t>(controller, kControllerSizeClasses); }
EI_REPLACE_METHOD(0x00530A50, CUnit::UpdateSizeClasses);

float* CUnit::StandPoint(float* out) {
    float x = positionX;
    float y = positionY;
    volatile float lift = static_cast<float>(ModeSpeed());   // the original inlines it
    CEngineObject* world = WorldOf(this);
    double height;
    if (movementKind == kMovesOnWater) {
        if (controller->GetClassId() == kClassUnitServer) {
            height = ei::VCall<double>(world, kWorldSurfaceHeight, x, y) + positionZ;
        } else {
            height = ei::VCall<double>(world, kWorldSurfaceHeight, x, y) + heightOffset;
        }
    } else {
        height = ei::VCall<double>(world, kWorldGroundHeight, x, y);
    }
    out[0] = x;
    out[1] = y;
    out[2] = static_cast<float>(height + lift);
    return out;
}
EI_REPLACE_METHOD(0x005303A0, CUnit::StandPoint);

void CUnit::PlaceUnitOnGround(float offset) {
    float x = positionX;
    float y = positionY;
    heightOffset = offset;
    CEngineObject* world = WorldOf(this);
    if (movementKind == kMovesOnWater) {
        if (controller->GetClassId() == kClassUnitServer) {
            positionZ = static_cast<float>(ei::VCall<double>(world, kWorldSurfaceHeight, x, y) + positionZ);
        } else {
            positionZ = static_cast<float>(ei::VCall<double>(world, kWorldSurfaceHeight, x, y) + heightOffset);
        }
    } else {
        positionZ = static_cast<float>(ei::VCall<double>(world, kWorldGroundHeight, x, y));
    }
}
EI_REPLACE_METHOD(0x0052F740, CUnit::PlaceUnitOnGround);

void CUnit::SetFromStats(UnitStats* stats) {
    CallPackStats(stats, nullptr, this, reinterpret_cast<UnitParams*>(reinterpret_cast<char*>(this) + 0x330));
    float curve = static_cast<float>(CallHitPointCurve(stats));
    float limit = CallScaleHitPoints(this, curve);
    maxHitPoints = static_cast<std::int16_t>(GameFtol(limit));
    hitPoints = static_cast<std::int16_t>(GameFtol(stats->Reduce(limit)));
}
EI_REPLACE_METHOD(0x00532A20, CUnit::SetFromStats);
