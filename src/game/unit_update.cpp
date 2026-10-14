// CUnit's own virtual slots (vtable 0073EA8C, shared by CFakeUnit 0073BFE8): the per-tick update
// and the slots that pass map object events on to the unit's controller (CUnit + 0x240), plus
// standing on the ground. See unit.h and docs/notes/units.md.
#include "unit.h"

#include "map_object.h"
#include "object_tables.h"
#include "unit_db.h"
#include "world.h"

#include "ei/hook.h"
#include "originals_units.h"

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

// The map object's (map_object.cpp) and the 3D object's (map_object_figures.cpp) bodies these slots
// run are rewritten and called directly.
// 00552F40, CUnitServer::SetStats: copies `stats` into its own copy. unit_create.cpp.
__declspec(naked) std::uintptr_t __fastcall CallServerSetStats(CEngineObject*, void*, const UnitStats*)
    EI_ORIGINAL(0x00552F40)
// 00522CE0 / 00523E80 / 00523E00 (unit_vitals.cpp, unit_stats_pack.cpp).
__declspec(naked) void __fastcall CallPackStats(UnitStats*, void*, CUnit*, UnitParams*) EI_ORIGINAL(0x00522CE0)
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
    Update();
    return 1;
}
EI_REPLACE_METHOD(0x0052F7D0, CUnit::Tick);

std::uintptr_t CUnit::RecomputeScale() {
    CMapObject::RecomputeScale();
    return Controller()->Resettle();
}
std::uintptr_t __fastcall UnitRecomputeScaleHook(CUnit* self) { return self->CUnit::RecomputeScale(); }
EI_REPLACE(0x00532A80, UnitRecomputeScaleHook);

std::uintptr_t CUnit::EffectsChanged() { return Controller()->EffectsChanged(); }
std::uintptr_t __fastcall UnitEffectsChangedHook(CUnit* self) { return self->CUnit::EffectsChanged(); }
EI_REPLACE(0x00532AB0, UnitEffectsChangedHook);

std::uint32_t CUnit::ApplyBaseScale() { return static_cast<std::uint32_t>(SetFigureScale(&baseScale.x)); }
std::uint32_t __fastcall UnitApplyBaseScaleHook(CUnit* self) { return self->CUnit::ApplyBaseScale(); }
EI_REPLACE(0x00532AA0, UnitApplyBaseScaleHook);

std::uintptr_t CUnit::LoadFigures() {
    if (flags_ & kFlagFiguresLoaded) return 1;
    std::uintptr_t result = static_cast<std::uintptr_t>(CObject3D::LoadFigures());
    Controller()->FiguresLoaded();
    return result;
}
std::uintptr_t __fastcall UnitLoadFiguresHook(CUnit* self) { return self->CUnit::LoadFigures(); }
EI_REPLACE(0x0052E910, UnitLoadFiguresHook);

std::uintptr_t CUnit::AttachObject(CEngineObject* object) {
    if (!object || object->type_ != kControllerType) return CMapObject::AttachObject(object);
    if (controller) {
        Controller()->unit_ = nullptr;
        controller->Release();
    }
    controller = object;
    static_cast<CUnitController*>(object)->unit_ = this;
    return static_cast<std::uintptr_t>(++controller->refCount_);
}
std::uintptr_t __fastcall UnitAttachObjectHook(CUnit* self, void*, CEngineObject* object) {
    return self->CUnit::AttachObject(object);
}
EI_REPLACE(0x0052E870, UnitAttachObjectHook);

int CUnit::StatsChanged() {
    PackVitals();
    if (controller) Controller()->RepackStats();
    return 1;
}
int __fastcall UnitStatsChangedHook(CUnit* self) { return self->CUnit::StatsChanged(); }
EI_REPLACE(0x0052EB50, UnitStatsChangedHook);

std::uintptr_t CUnit::ScaleEffectChanged(int value) {
    if (!controller) return 0;
    return static_cast<std::uintptr_t>(Controller()->RefreshVitals(value));
}
std::uintptr_t __fastcall UnitScaleEffectChangedHook(CUnit* self, void*, int value) {
    return self->CUnit::ScaleEffectChanged(value);
}
EI_REPLACE(0x0052EB30, UnitScaleEffectChangedHook);

std::uintptr_t CUnit::SetStats(const UnitStats* stats) {
    int classId = controller->GetClassId();
    if (classId != kClassUnitServer) return static_cast<std::uintptr_t>(classId);
    return CallServerSetStats(controller, nullptr, stats);
}
std::uintptr_t __fastcall UnitSetStatsHook(CUnit* self, void*, const UnitStats* stats) { return self->CUnit::SetStats(stats); }
EI_REPLACE(0x00530A20, UnitSetStatsHook);

std::uintptr_t CUnit::UpdateSizeClasses() { return Controller()->UpdateSizeClasses(); }
std::uintptr_t __fastcall UnitUpdateSizeClassesHook(CUnit* self) { return self->CUnit::UpdateSizeClasses(); }
EI_REPLACE(0x00530A50, UnitUpdateSizeClassesHook);

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

void CUnit::PlaceOnGround(float offset) {
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
void __fastcall UnitPlaceOnGroundHook(CUnit* self, void*, float offset) { self->CUnit::PlaceOnGround(offset); }
EI_REPLACE(0x0052F740, UnitPlaceOnGroundHook);

void CUnit::SetFromStats(UnitStats* stats) {
    CallPackStats(stats, nullptr, this, Params());
    float curve = static_cast<float>(UnitStatsHitPointCurve(stats));
    float limit = CallScaleHitPoints(this, curve);
    packedVitals.maxHitPoints = static_cast<std::int16_t>(GameFtol(limit));
    packedVitals.hitPoints = static_cast<std::int16_t>(GameFtol(stats->Reduce(limit)));
}
EI_REPLACE_METHOD(0x00532A20, CUnit::SetFromStats);

int CUnit::Level() {
    if (IsTextKey(id_) & 0xFF) return Controller()->Level();
    const auto* prototype =
        reinterpret_cast<const UnitPrototypeRecord*>(unitdb::Prototypes().At(unknown_18, unitdb::kPrototypeSize));
    return static_cast<int>(prototype->level);
}
EI_REPLACE_METHOD(0x00532D40, CUnit::Level);
