// CUnit's own virtual slots (vtable 0073EA8C, shared by CFakeUnit 0073BFE8): the per-tick update
// and the slots that pass map object events on to the unit's controller (CUnit + 0x240), plus
// standing on the ground. See unit.h and docs/notes/units.md.
#include "unit.h"

#include "map_object.h"
#include "object_tables.h"
#include "unit_db.h"
#include "world.h"

#include "ei/hook.h"

#include <cstring>

#pragma float_control(precise, on)

namespace {
constexpr int kClassUnitServer = 0x3E;        // CUnitServer (combat.h)
constexpr int kControllerType = 0x48;         // CEngineObject::type_ of unit controllers
constexpr std::uint32_t kFlagFiguresLoaded = 0x80;
constexpr std::uint8_t kMovesOnWater = 2;     // CUnit::movementKind

// The world's slots 29 / 30 give the ground and water surface heights at (x, y), in ST0.
CWorld* WorldOf(CUnit* unit) { return static_cast<CWorld*>(unit->World()); }
} // namespace

// Map object (CMapObject, map_object.cpp) and CObject3D methods, through their game addresses.
// 0050F1C0 / 0050F2D0 are rewritten (map_object.cpp): calling the addresses keeps their verify
// wrappers in the path.
__declspec(naked) int __fastcall CallMapObjectUpdate(CUnit*) EI_ORIGINAL(0x0050F1C0)
__declspec(naked) std::uintptr_t __fastcall CallRecomputeScale(CUnit*) EI_ORIGINAL(0x0050F2D0)
// 00517700, CObject3D::SetFigureScale: the figures' scale from `scale`, three floats. Rewritten in
// map_object_figures.cpp, called through its address.
__declspec(naked) std::uintptr_t __fastcall CallSetFigureScale(CUnit*, void*, const Vector3*) EI_ORIGINAL(0x00517700)
// 00517500, CObject3D::LoadFigures, slot 26: loads the figures. map_object_figures.cpp.
__declspec(naked) std::uintptr_t __fastcall CallLoadFigures(CUnit*) EI_ORIGINAL(0x00517500)
// 0050E890, CMapObject::AttachObject, slot 19: attaches a companion of another type. map_object.cpp.
__declspec(naked) std::uintptr_t __fastcall CallAttachCompanion(CUnit*, void*, CEngineObject*) EI_ORIGINAL(0x0050E890)
// 0050E970, CMapObject::PackVitals, slot 32: packs the server object's vitals into +0x17C.
// map_object.cpp.
__declspec(naked) int __fastcall CallMapObjectStatsChanged(CUnit*) EI_ORIGINAL(0x0050E970)
// 00552F40, CUnitServer::SetStats: copies `stats` into its own copy. unit_create.cpp.
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
    controller->RunTick();
    CallMapObjectUpdate(this);
    return 1;
}
EI_REPLACE_METHOD(0x0052F7D0, CUnit::Tick);

std::uintptr_t CUnit::RecomputeUnitScale() {
    CallRecomputeScale(this);
    return Controller()->Resettle();
}
EI_REPLACE_METHOD(0x00532A80, CUnit::RecomputeUnitScale);

std::uintptr_t CUnit::EffectsChanged() { return Controller()->EffectsChanged(); }
EI_REPLACE_METHOD(0x00532AB0, CUnit::EffectsChanged);

std::uintptr_t CUnit::ResetUnitScale() {
    return CallSetFigureScale(this, nullptr, &MapObject()->baseScale);
}
EI_REPLACE_METHOD(0x00532AA0, CUnit::ResetUnitScale);

std::uintptr_t CUnit::LoadUnitFigures() {
    if (flags_ & kFlagFiguresLoaded) return 1;
    std::uintptr_t result = CallLoadFigures(this);
    Controller()->FiguresLoaded();
    return result;
}
EI_REPLACE_METHOD(0x0052E910, CUnit::LoadUnitFigures);

std::uintptr_t CUnit::AttachCompanion(CEngineObject* attached) {
    if (!attached || attached->type_ != kControllerType) return CallAttachCompanion(this, nullptr, attached);
    if (controller) {
        Controller()->unit_ = nullptr;
        controller->Release();
    }
    controller = attached;
    static_cast<CUnitController*>(attached)->unit_ = this;
    return static_cast<std::uintptr_t>(++controller->refCount_);
}
EI_REPLACE_METHOD(0x0052E870, CUnit::AttachCompanion);

int CUnit::StatsChanged() {
    CallMapObjectStatsChanged(this);
    if (controller) Controller()->RepackStats();
    return 1;
}
EI_REPLACE_METHOD(0x0052EB50, CUnit::StatsChanged);

std::uintptr_t CUnit::ScaleEffectChanged(int value) {
    if (!controller) return 0;
    return static_cast<std::uintptr_t>(Controller()->RefreshVitals(value));
}
EI_REPLACE_METHOD(0x0052EB30, CUnit::ScaleEffectChanged);

std::uintptr_t CUnit::SetServerStats(const UnitStats* stats) {
    int classId = controller->GetClassId();
    if (classId != kClassUnitServer) return static_cast<std::uintptr_t>(classId);
    return CallServerSetStats(controller, nullptr, stats);
}
EI_REPLACE_METHOD(0x00530A20, CUnit::SetServerStats);

std::uintptr_t CUnit::UpdateSizeClasses() { return Controller()->UpdateSizeClasses(); }
EI_REPLACE_METHOD(0x00530A50, CUnit::UpdateSizeClasses);

float* CUnit::StandPoint(float* out) {
    float x = position.x;
    float y = position.y;
    volatile float lift = static_cast<float>(ModeSpeed());   // the original inlines it
    CWorld* world = WorldOf(this);
    double height;
    if (movementKind == kMovesOnWater) {
        if (controller->GetClassId() == kClassUnitServer) {
            height = world->SurfaceHeightAt(x, y) + position.z;
        } else {
            height = world->SurfaceHeightAt(x, y) + heightOffset;
        }
    } else {
        height = world->GroundHeightAt(x, y);
    }
    out[0] = x;
    out[1] = y;
    out[2] = static_cast<float>(height + lift);
    return out;
}
EI_REPLACE_METHOD(0x005303A0, CUnit::StandPoint);

void CUnit::PlaceUnitOnGround(float offset) {
    float x = position.x;
    float y = position.y;
    heightOffset = offset;
    CWorld* world = WorldOf(this);
    if (movementKind == kMovesOnWater) {
        if (controller->GetClassId() == kClassUnitServer) {
            position.z = static_cast<float>(world->SurfaceHeightAt(x, y) + position.z);
        } else {
            position.z = static_cast<float>(world->SurfaceHeightAt(x, y) + heightOffset);
        }
    } else {
        position.z = static_cast<float>(world->GroundHeightAt(x, y));
    }
}
EI_REPLACE_METHOD(0x0052F740, CUnit::PlaceUnitOnGround);

void CUnit::SetFromStats(UnitStats* stats) {
    CallPackStats(stats, nullptr, this, Params());
    float curve = static_cast<float>(CallHitPointCurve(stats));
    float limit = CallScaleHitPoints(this, curve);
    maxHitPoints = static_cast<std::int16_t>(GameFtol(limit));
    hitPoints = static_cast<std::int16_t>(GameFtol(stats->Reduce(limit)));
}
EI_REPLACE_METHOD(0x00532A20, CUnit::SetFromStats);

int CUnit::Level() {
    if (IsTextKey(id_) & 0xFF) return Controller()->Level();
    const auto* prototype =
        reinterpret_cast<const UnitPrototypeRecord*>(unitdb::Prototypes().At(unknown_18, unitdb::kPrototypeSize));
    return static_cast<int>(prototype->level);
}
EI_REPLACE_METHOD(0x00532D40, CUnit::Level);
