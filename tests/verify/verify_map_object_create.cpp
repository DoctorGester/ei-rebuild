// Checks for map_object_create.cpp: the Create slots of units (0052E400) and torches (0052BFC0)
// allocate (the network fields) and reset the object, so only ours runs. Afterwards: result 1,
// the DeltaState at +0x1C4 holds exactly 14 (units) / 5 (torches) fields with a buffer at least
// that large, its mode is idle (4) and, for a unit, the carried array is empty (Reset); a torch's
// +0x238 is null. Units: every unit made (the zone loader, AddUnitToServer; newgame). Torches:
// zones with torches (class 0x3A).
#include "verify.h"

#include "../../src/game/map_object.h"
#include "../../src/game/map_object_figures.h"
#include "../../src/game/net_delta.h"
#include "../../src/game/torch.h"
#include "../../src/game/unit.h"

#include <cstdint>

namespace {
void* s_unusedUnit;
void* s_unusedTorch;

const DeltaState* NetState(const void* object) {
    return reinterpret_cast<const DeltaState*>(static_cast<const std::uint8_t*>(object) + 0x1C4);
}
int Capacity(const DeltaState* state) { return static_cast<int>(state->fieldCapacity_); }

const char* CheckState(const DeltaState* state, int fields) {
    if (state->fieldCount_ != fields) return "field count";
    if (!state->fields_ || Capacity(state) < fields) return "field buffer";
    if (state->mode_ != DeltaState::kIdle) return "mode";
    return nullptr;
}
} // namespace

static int __fastcall CreateUnitVerify(CUnit* unit) {
    int result = unit->CreateUnit();
    const char* problem = result != 1 ? "result" : CheckState(NetState(unit), 14);
    if (!problem && unit->carriedCount != 0) problem = "carried items";
    ei::verify::Check("CUnit::CreateUnit", true, problem == nullptr, "unit %d: %s (fields %d)", unit->id_,
                      problem ? problem : "", NetState(unit)->fieldCount_);
    return result;
}
EI_VERIFY(0x0052E400, CreateUnitVerify, 0, s_unusedUnit);

static int __fastcall TorchCreateVerify(CTorchObject* torch) {
    int result = torch->CreateTorch();
    const char* problem = result != 1 ? "result" : CheckState(torch->NetState(), 5);
    if (!problem && torch->spec) problem = "spec";
    ei::verify::Check("CTorchObject::CreateTorch", true, problem == nullptr, "object %d: %s (fields %d)", torch->id_,
                      problem ? problem : "", torch->NetState()->fieldCount_);
    return result;
}
EI_VERIFY(0x0052BFC0, TorchCreateVerify, 0, s_unusedTorch);

// --- CMapObject's Create / Destroy (A-U) ---------------------------------------------------------
// Ours only (they release the companion and allocate the network fields). Create: result 1, no
// companion, 4 fields, mode idle, changeStamp -1, no effects, +0x174 5, +0x178 1, the second name
// empty, base scale 0, no figure. Destroy: no effects, no companion, no figure, parts or side
// half. Every map object made or removed (zone loads, spawns, removals: every scenario).

namespace {
void* s_unusedCreate;
void* s_unusedDestroy;

const CObject3D* As3D(const CMapObject* object) { return reinterpret_cast<const CObject3D*>(object); }
} // namespace

static int __fastcall CreateMapObjectVerify(CMapObject* object) {
    int result = object->CreateMapObject();
    const char* problem = result != 1 ? "result" : CheckState(NetState(object), 4);
    if (!problem && object->companion) problem = "companion";
    if (!problem && (object->changeStamp != -1 || object->effects || object->effectCount)) problem = "stamp / effects";
    if (!problem && (object->updateCountdown != 5 || object->animationSpeed != 1.0f)) problem = "+0x174 / animationSpeed";
    if (!problem && (!object->name2 || object->name2[0])) problem = "second name";
    if (!problem && (object->baseScale.x != 0.0f || object->baseScale.y != 0.0f || object->baseScale.z != 0.0f))
        problem = "base scale";
    if (!problem && (As3D(object)->figure || As3D(object)->attached)) problem = "figure / side half";
    ei::verify::Check("CMapObject::CreateMapObject", true, problem == nullptr, "object %d: %s", object->id_,
                      problem ? problem : "");
    return result;
}
EI_VERIFY(0x0050E770, CreateMapObjectVerify, 0, s_unusedCreate);

static void __fastcall DestroyMapObjectVerify(CMapObject* object) {
    object->DestroyMapObject();
    const CObject3D* body = As3D(object);
    const char* problem = nullptr;
    if (object->effectCount != 0) problem = "effects";
    else if (object->companion) problem = "companion";
    else if (body->figure || body->attached || body->animation) problem = "figure / side half / animation";
    else if (body->parts.size || body->pendingParts.size || body->effectSources.head) problem = "parts / sources";
    ei::verify::Check("CMapObject::DestroyMapObject", true, problem == nullptr, "object %d: %s", object->id_,
                      problem ? problem : "");
}
EI_VERIFY(0x0050E840, DestroyMapObjectVerify, 0, s_unusedDestroy);
